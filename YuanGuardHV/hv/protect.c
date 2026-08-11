#include <ntddk.h>
#include "protect.h"
#include "debug.h"
#include "control_plane.h"

extern npt_mgr_t g_npt;

NTKERNELAPI NTSTATUS PsLookupProcessByProcessId(HANDLE ProcessId, PEPROCESS *Process);

static const uint64_t YGHV_PT_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

yghv_protect_state_t g_protect;
yghv_protect_hook_t g_protect_hooks[YGHV_PROTECT_MAX_HOOKS];

static uint64_t yghv_pt_read(uint64_t table_pa, uint64_t index) {
    uint64_t *va;
    if (!table_pa) return 0;
    va = MmGetVirtualForPhysical((PHYSICAL_ADDRESS){ .QuadPart = table_pa });
    if (!va) return 0;
    return va[index];
}

uint64_t yghv_protect_guest_va_to_pa(uint64_t cr3, uint64_t va) {
    uint64_t pml4e, pdpte, pde, pte;
    pml4e = yghv_pt_read(cr3 & YGHV_PT_ADDR_MASK, (va >> 39) & 0x1FF);
    if (!(pml4e & 1)) return 0;
    pdpte = yghv_pt_read(pml4e & YGHV_PT_ADDR_MASK, (va >> 30) & 0x1FF);
    if (!(pdpte & 1)) return 0;
    if (pdpte & (1ULL << 7))  /* 1GB page */
        return (pdpte & 0x000FFFFFC0000000ULL) | (va & 0x3FFFFFFFULL);
    pde = yghv_pt_read(pdpte & YGHV_PT_ADDR_MASK, (va >> 21) & 0x1FF);
    if (!(pde & 1)) return 0;
    if (pde & (1ULL << 7))  /* 2MB page */
        return (pde & 0x000FFFFFFFE00000ULL) | (va & 0x1FFFFFULL);
    pte = yghv_pt_read(pde & YGHV_PT_ADDR_MASK, (va >> 12) & 0x1FF);
    if (!(pte & 1)) return 0;
    return (pte & YGHV_PT_ADDR_MASK) | (va & 0xFFFULL);
}

NTSTATUS yghv_protect_init(void) {
    RtlZeroMemory(&g_protect, sizeof(g_protect));
    RtlZeroMemory(g_protect_hooks, sizeof(g_protect_hooks));
    return STATUS_SUCCESS;
}

void yghv_protect_cleanup(void) {
    yghv_protect_stop();
    if (g_protect.process) {
        ObDereferenceObject(g_protect.process);
        g_protect.process = NULL;
    }
    RtlZeroMemory(&g_protect, sizeof(g_protect));
    RtlZeroMemory(g_protect_hooks, sizeof(g_protect_hooks));
}

NTSTATUS yghv_protect_set_target(uint32_t pid) {
    PEPROCESS proc = NULL;
    uint64_t cr3;
    NTSTATUS st;

    if (g_protect.process) {
        ObDereferenceObject(g_protect.process);
        g_protect.process = NULL;
        g_protect.cr3 = 0;
        g_protect.pid = 0;
    }

    st = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &proc);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect set_target: lookup pid %u failed 0x%x", pid, st);
        return st;
    }

    /* KPROCESS.DirectoryTableBase on 19045 is at offset 0x028. */
    cr3 = *(volatile uint64_t *)((uint8_t *)proc + 0x028);
    if (!cr3) {
        ObDereferenceObject(proc);
        LOG_ERROR("protect set_target: pid %u has no CR3", pid);
        return STATUS_INVALID_PARAMETER;
    }
    g_protect.pid = pid;
    g_protect.process = proc;
    g_protect.cr3 = cr3;
    LOG_ERROR("protect target: pid=%u process=0x%llx cr3=0x%llx", pid,
        (uint64_t)proc, cr3);
    return STATUS_SUCCESS;
}

BOOLEAN yghv_protect_is_target_cr3(uint64_t cr3) {
    return g_protect.cr3 != 0 && cr3 == g_protect.cr3;
}

NTSTATUS yghv_protect_add_page(uint64_t target_va) {
    uint64_t gpa;
    yghv_protect_page_t *p;
    if (g_protect.page_count >= YGHV_PROTECT_MAX_PAGES)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!g_protect.cr3)
        return STATUS_INVALID_PARAMETER;
    gpa = yghv_protect_guest_va_to_pa(g_protect.cr3, target_va);
    if (!gpa) {
        LOG_ERROR("protect add_page: va 0x%llx not mapped", target_va);
        return STATUS_INVALID_ADDRESS;
    }
    p = &g_protect.pages[g_protect.page_count++];
    p->gpa = gpa;
    p->target_va = target_va;
    p->flags = YGHV_PROTECT_MEM;
    p->armed = 0;
    LOG_ERROR("protect add_page: va=0x%llx gpa=0x%llx", target_va, gpa);
    return STATUS_SUCCESS;
}

NTSTATUS yghv_protect_remove_page(uint64_t target_va) {
    uint32_t i;
    for (i = 0; i < g_protect.page_count; i++) {
        if (g_protect.pages[i].target_va == target_va) {
            if (g_protect.pages[i].armed) {
                int st = yghv_protect_disarm_page(&g_protect.pages[i]);
                if (st)
                    return (NTSTATUS)st;
            }
            g_protect.pages[i] = g_protect.pages[g_protect.page_count - 1];
            g_protect.page_count--;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

yghv_protect_page_t *yghv_protect_find_page(uint64_t gpa) {
    uint32_t i;
    uint64_t page = gpa & ~0xFFFULL;
    for (i = 0; i < g_protect.page_count; i++)
        if (g_protect.pages[i].gpa == page)
            return &g_protect.pages[i];
    return NULL;
}

int yghv_protect_arm_page(yghv_protect_page_t *p) {
    int st = npt_split_2mb_to_4kb(&g_npt, p->gpa);
    if (st)
        return st;
    st = npt_set_page_perm(&g_npt, p->gpa, NPT_PERM_PRESENT);
    if (!st) p->armed = 1;
    return st;
}

int yghv_protect_disarm_page(yghv_protect_page_t *p) {
    int st = npt_set_page_perm(&g_npt, p->gpa,
        NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    if (!st) p->armed = 0;
    return st;
}

NTSTATUS yghv_protect_start(void) {
    uint32_t i;
    if (!g_protect.cr3) return STATUS_INVALID_PARAMETER;
    for (i = 0; i < g_protect.page_count; i++) {
        int st = yghv_protect_arm_page(&g_protect.pages[i]);
        if (st) {
            LOG_ERROR("protect start: arm page %u failed 0x%x", i, st);
            NTSTATUS disarm_status = yghv_protect_stop();
            if (disarm_status != STATUS_SUCCESS) {
                LOG_ERROR("protect start: rollback disarm failed 0x%x",
                    disarm_status);
                g_protect.active = TRUE;
                return STATUS_UNSUCCESSFUL;
            }
            return (NTSTATUS)st;
        }
    }
    g_protect.active = TRUE;
    LOG_ERROR("protect start: %u pages armed", g_protect.page_count);
    return STATUS_SUCCESS;
}

NTSTATUS yghv_protect_stop(void) {
    uint32_t i;
    NTSTATUS first_failure = STATUS_SUCCESS;
    for (i = 0; i < g_protect.page_count; i++) {
        if (g_protect.pages[i].armed) {
            int st = yghv_protect_disarm_page(&g_protect.pages[i]);
            if (st && first_failure == STATUS_SUCCESS) {
                first_failure = (NTSTATUS)st;
                LOG_ERROR("protect stop: disarm page %u failed 0x%x", i, st);
            }
        }
    }
    if (first_failure != STATUS_SUCCESS)
        return STATUS_UNSUCCESSFUL;
    g_protect.active = FALSE;
    return STATUS_SUCCESS;
}

static uint8_t *g_hook_stub_pages[YGHV_PROTECT_MAX_HOOKS];

/* clang-cl exposes no __readcr0/__writecr0 intrinsics on this toolchain. */
static uint64_t yghv_read_cr0(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}
static void yghv_write_cr0(uint64_t v) {
    __asm__ volatile("mov %0, %%cr0" :: "r"(v) : "memory");
}

static void yghv_emit_u8(uint8_t *p, uint8_t v) { *p = v; }
static void yghv_emit_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (i * 8));
}
static void yghv_emit_rel32(uint8_t *p, uint64_t from, uint64_t to) {
    long long d = (long long)(to - (from + 5));
    p[0] = 0xE9;
    for (int i = 0; i < 4; i++) p[1 + i] = (uint8_t)((uint64_t)d >> (i * 8));
}

NTSTATUS yghv_protect_install_hook(uint8_t hook_id, uint64_t func_va) {
    uint8_t *stub;
    uint64_t func_pa, page_va, page_pa;
    uint8_t *orig;
    yghv_protect_hook_t *h;

    if (hook_id >= YGHV_PROTECT_MAX_HOOKS) return STATUS_INVALID_PARAMETER;
    h = &g_protect_hooks[hook_id];
    if (h->installed) return STATUS_ALREADY_COMMITTED;

    func_pa = MmGetPhysicalAddress((PVOID)func_va).QuadPart;
    page_va = func_va & ~(HV_PAGE_SIZE - 1);
    page_pa = MmGetPhysicalAddress((PVOID)page_va).QuadPart;
    RtlCopyMemory(h->original, (void *)func_va, YGHV_PROTECT_PATCH_LEN);
    h->func_va = func_va;
    h->func_pa = func_pa;
    h->hook_id = hook_id;

    stub = (uint8_t *)MmAllocateContiguousMemory(HV_PAGE_SIZE,
        (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!stub) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(stub, HV_PAGE_SIZE);
    g_hook_stub_pages[hook_id] = stub;

    /* entry: push rax/rcx/rbx, mov rbx,hook_id, movabs rcx,cookie,
       mov rax,HOOK_QUERY, vmmcall, test rax,rax, jnz deny */
    uint8_t *p = stub;
    p[0]=0x50; p[1]=0x51; p[2]=0x53;               /* push rax,rcx,rbx */
    p[3]=0x48; p[4]=0xC7; p[5]=0xC3;               /* mov rbx, imm32 */
    p[6]=hook_id; p[7]=0; p[8]=0; p[9]=0;
    p[10]=0x48; p[11]=0xB9;                        /* movabs rcx, imm64 */
    yghv_emit_u64(p+12, g_vmmcall_auth_cookie);
    p[20]=0x48; p[21]=0xB8;                        /* movabs rax, HOOK_QUERY */
    yghv_emit_u64(p+22, YGHV_CMD_HOOK_QUERY);
    p[30]=0x0F; p[31]=0x01; p[32]=0xD9;            /* vmmcall */
    p[33]=0x48; p[34]=0x85; p[35]=0xC0;            /* test rax,rax */
    p[36]=0x75; p[37]=0x08;                        /* jnz +8 -> deny at 0x2E */
    p[38]=0x5B; p[39]=0x59; p[40]=0x58;            /* pop rbx,rcx,rax */
    p[41]=0xE9;                                    /* jmp rel32 -> original slot */
    /* rel32 patched below */
    p[46]=0x5B; p[47]=0x59; p[48]=0x58;            /* deny: pop rbx,rcx,rax */
    p[49]=0x48; p[50]=0xB8;                        /* movabs rax, STATUS_ACCESS_DENIED */
    yghv_emit_u64(p+51, 0xC0000022ULL);
    p[59]=0xC3;                                    /* ret */

    /* original slot at offset 0x40 */
    orig = stub + 0x40;
    RtlCopyMemory(orig, h->original, YGHV_PROTECT_PATCH_LEN);
    /* jump back to func_va+16 after original bytes */
    orig[0x10] = 0x49; orig[0x11] = 0xBB;          /* movabs r11, imm64 */
    yghv_emit_u64(orig + 0x12, func_va + YGHV_PROTECT_PATCH_LEN);
    orig[0x1A] = 0x41; orig[0x1B] = 0xFF; orig[0x1C] = 0xE3;  /* jmp r11 */

    yghv_emit_rel32(p + 41, (uint64_t)(p + 41), (uint64_t)orig);

    /* patch function entry: E9 rel32 -> stub */
    uint64_t old_cr0 = yghv_read_cr0();
    yghv_write_cr0(old_cr0 & ~(1ULL << 16));       /* clear CR0.WP */
    yghv_emit_rel32((uint8_t *)func_va, func_va, (uint64_t)stub);
    yghv_write_cr0(old_cr0);

    /* write-protect the function's page in NPT */
    npt_set_page_perm(&g_npt, page_pa, NPT_PERM_PRESENT);
    h->installed = 1;
    LOG_ERROR("protect hook %u installed: va=0x%llx pa=0x%llx", hook_id, func_va, func_pa);
    return STATUS_SUCCESS;
}

NTSTATUS yghv_protect_remove_hook(uint8_t hook_id) {
    yghv_protect_hook_t *h;
    uint64_t page_pa;
    if (hook_id >= YGHV_PROTECT_MAX_HOOKS) return STATUS_INVALID_PARAMETER;
    h = &g_protect_hooks[hook_id];
    if (!h->installed) return STATUS_NOT_FOUND;
    uint64_t old_cr0 = yghv_read_cr0();
    yghv_write_cr0(old_cr0 & ~(1ULL << 16));
    RtlCopyMemory((void *)h->func_va, h->original, YGHV_PROTECT_PATCH_LEN);
    yghv_write_cr0(old_cr0);
    page_pa = MmGetPhysicalAddress((PVOID)(h->func_va & ~(HV_PAGE_SIZE - 1))).QuadPart;
    npt_set_page_perm(&g_npt, page_pa, NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    if (g_hook_stub_pages[hook_id]) {
        MmFreeContiguousMemory(g_hook_stub_pages[hook_id]);
        g_hook_stub_pages[hook_id] = NULL;
    }
    h->installed = 0;
    LOG_ERROR("protect hook %u removed", hook_id);
    return STATUS_SUCCESS;
}

uint64_t yghv_protect_on_hook_query(uint8_t hook_id, uint64_t accessor_cr3) {
    if (hook_id >= YGHV_PROTECT_MAX_HOOKS || !g_protect_hooks[hook_id].installed)
        return YGHV_STATUS_INVALID;
    if (yghv_protect_is_target_cr3(accessor_cr3))
        return YGHV_STATUS_OK;
    LOG_ERROR("protect hook %u denied cr3=0x%llx", hook_id, accessor_cr3);
    return YGHV_STATUS_DENIED;
}

uint64_t yghv_protect_find_func_pattern(PCWSTR name_hint, uint8_t *pat, SIZE_T pat_len) {
    UNICODE_STRING name;
    (void)pat; (void)pat_len;
    RtlInitUnicodeString(&name, name_hint);
    return (uint64_t)MmGetSystemRoutineAddress(&name);
}
