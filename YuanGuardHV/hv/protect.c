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
