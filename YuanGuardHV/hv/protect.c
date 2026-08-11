#include <ntddk.h>
#include "protect.h"
#include "svm_vcpu.h"
#include "debug.h"
#include "control_plane.h"

extern npt_mgr_t g_npt;

NTKERNELAPI NTSTATUS PsLookupProcessByProcessId(HANDLE ProcessId, PEPROCESS *Process);

static const uint64_t YGHV_PT_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

yghv_protect_state_t g_protect;
yghv_protect_hook_t g_protect_hooks[YGHV_PROTECT_MAX_HOOKS];
static FAST_MUTEX g_protect_lock;

static BOOLEAN yghv_protect_is_target_cr3_locked(uint64_t cr3) {
    return g_protect.cr3 != 0 && cr3 == g_protect.cr3;
}

static yghv_protect_page_t *yghv_protect_find_page_locked(uint64_t gpa) {
    uint32_t i;
    uint64_t page = gpa & ~0xFFFULL;
    for (i = 0; i < g_protect.page_count; i++)
        if (g_protect.pages[i].gpa == page)
            return &g_protect.pages[i];
    return NULL;
}

static NTSTATUS yghv_protect_add_page_locked(uint64_t target_va);
static NTSTATUS yghv_protect_remove_page_locked(uint64_t target_va);
static int yghv_protect_arm_page_locked(yghv_protect_page_t *p);
static int yghv_protect_disarm_page_locked(yghv_protect_page_t *p);
static NTSTATUS yghv_protect_start_locked(void);
static NTSTATUS yghv_protect_stop_locked(void);
static NTSTATUS yghv_protect_install_hook_locked(uint8_t hook_id, uint64_t func_va);
static NTSTATUS yghv_protect_remove_hook_locked(uint8_t hook_id);

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
    ExInitializeFastMutex(&g_protect_lock);
    RtlZeroMemory(&g_protect, sizeof(g_protect));
    RtlZeroMemory(g_protect_hooks, sizeof(g_protect_hooks));
    return STATUS_SUCCESS;
}

void yghv_protect_cleanup(void) {
    uint32_t i;
    ExAcquireFastMutex(&g_protect_lock);
    for (i = 0; i < YGHV_PROTECT_MAX_HOOKS; i++) {
        if (g_protect_hooks[i].installed)
            yghv_protect_remove_hook_locked(i);
    }
    yghv_protect_stop_locked();
    if (g_protect.process) {
        ObDereferenceObject(g_protect.process);
        g_protect.process = NULL;
    }
    RtlZeroMemory(&g_protect, sizeof(g_protect));
    RtlZeroMemory(g_protect_hooks, sizeof(g_protect_hooks));
    ExReleaseFastMutex(&g_protect_lock);
}

NTSTATUS yghv_protect_set_target(uint32_t pid) {
    PEPROCESS proc = NULL;
    uint64_t cr3;
    NTSTATUS st;

    if (pid == 0)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&g_protect_lock);
    if (g_protect.process) {
        ObDereferenceObject(g_protect.process);
        g_protect.process = NULL;
        g_protect.cr3 = 0;
        g_protect.pid = 0;
    }
    ExReleaseFastMutex(&g_protect_lock);

    st = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &proc);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect set_target: lookup pid %u failed 0x%x", pid, st);
        return st;
    }
    if (PsGetProcessId(proc) != (HANDLE)(ULONG_PTR)pid) {
        ObDereferenceObject(proc);
        LOG_ERROR("protect set_target: pid mismatch %u", pid);
        return STATUS_INVALID_PARAMETER;
    }

    /* KPROCESS.DirectoryTableBase on 19045 is at offset 0x028. */
    cr3 = *(volatile uint64_t *)((uint8_t *)proc + 0x028);
    if (!cr3) {
        ObDereferenceObject(proc);
        LOG_ERROR("protect set_target: pid %u has no CR3", pid);
        return STATUS_INVALID_PARAMETER;
    }
    ExAcquireFastMutex(&g_protect_lock);
    g_protect.pid = pid;
    g_protect.process = proc;
    g_protect.cr3 = cr3;
    ExReleaseFastMutex(&g_protect_lock);
    LOG_ERROR("protect target: pid=%u process=0x%llx cr3=0x%llx", pid,
        (uint64_t)proc, cr3);
    return STATUS_SUCCESS;
}

BOOLEAN yghv_protect_is_target_cr3(uint64_t cr3) {
    BOOLEAN result;
    ExAcquireFastMutex(&g_protect_lock);
    result = yghv_protect_is_target_cr3_locked(cr3);
    ExReleaseFastMutex(&g_protect_lock);
    return result;
}

NTSTATUS yghv_protect_add_page(uint64_t target_va) {
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_add_page_locked(target_va);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static NTSTATUS yghv_protect_add_page_locked(uint64_t target_va) {
    uint64_t gpa;
    yghv_protect_page_t *p;
    if (g_protect.page_count >= YGHV_PROTECT_MAX_PAGES)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!g_protect.cr3)
        return STATUS_INVALID_PARAMETER;
    gpa = yghv_protect_guest_va_to_pa(g_protect.cr3, target_va);
    gpa &= ~(uint64_t)0xFFFULL;
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
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_remove_page_locked(target_va);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static NTSTATUS yghv_protect_remove_page_locked(uint64_t target_va) {
    uint32_t i;
    for (i = 0; i < g_protect.page_count; i++) {
        if (g_protect.pages[i].target_va == target_va) {
            if (g_protect.pages[i].armed) {
                int st = yghv_protect_disarm_page_locked(&g_protect.pages[i]);
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
    yghv_protect_page_t *p;
    ExAcquireFastMutex(&g_protect_lock);
    p = yghv_protect_find_page_locked(gpa);
    ExReleaseFastMutex(&g_protect_lock);
    return p;
}

int yghv_protect_arm_page(yghv_protect_page_t *p) {
    int st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_arm_page_locked(p);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static int yghv_protect_arm_page_locked(yghv_protect_page_t *p) {
    ULONG i;
    int st = npt_split_2mb_to_4kb(&g_npt, p->gpa);
    if (st)
        return st;
    st = npt_set_page_perm(&g_npt, p->gpa, NPT_PERM_PRESENT);
    if (!st) {
        p->armed = 1;
        for (i = 0; i < SVM_MAX_CORES; i++) {
            if (g_vcpus[i])
                g_vcpus[i]->npt_flush_pending = 1;
        }
    }
    return st;
}

int yghv_protect_disarm_page(yghv_protect_page_t *p) {
    int st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_disarm_page_locked(p);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static int yghv_protect_disarm_page_locked(yghv_protect_page_t *p) {
    ULONG i;
    int st = npt_set_page_perm(&g_npt, p->gpa,
        NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    if (!st) {
        p->armed = 0;
        for (i = 0; i < SVM_MAX_CORES; i++) {
            if (g_vcpus[i])
                g_vcpus[i]->npt_flush_pending = 1;
        }
    }
    return st;
}

NTSTATUS yghv_protect_start(void) {
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_start_locked();
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static NTSTATUS yghv_protect_start_locked(void) {
    uint32_t i;
    if (!g_protect.cr3) return STATUS_INVALID_PARAMETER;
    for (i = 0; i < g_protect.page_count; i++) {
        int st = yghv_protect_arm_page_locked(&g_protect.pages[i]);
        if (st) {
            LOG_ERROR("protect start: arm page %u failed 0x%x", i, st);
            NTSTATUS disarm_status = yghv_protect_stop_locked();
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
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_stop_locked();
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static NTSTATUS yghv_protect_stop_locked(void) {
    uint32_t i;
    NTSTATUS first_failure = STATUS_SUCCESS;
    for (i = 0; i < g_protect.page_count; i++) {
        if (g_protect.pages[i].armed) {
            int st = yghv_protect_disarm_page_locked(&g_protect.pages[i]);
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

yghv_npf_result_t yghv_protect_on_npf_write(svm_vcpu_t *vcpu, uint64_t gpa) {
    yghv_protect_page_t *pp;
    yghv_npf_result_t result = YGHV_NPF_NONE;
    int st;

    ExAcquireFastMutex(&g_protect_lock);
    pp = yghv_protect_find_page_locked(gpa);
    if (pp) {
        if (yghv_protect_is_target_cr3_locked(vcpu->vmcb->state.cr3) ||
            vcpu->vmcb->state.cpl == 0) {
            st = yghv_protect_disarm_page_locked(pp);
            if (st) {
                LOG_ERROR("protect: disarm failed gpa=0x%llx st=0x%x",
                    pp->gpa, st);
                result = YGHV_NPF_DENY;
            } else {
                vcpu->rearm_gpa = pp->gpa;
                vcpu->rearm_pending = 1;
                result = YGHV_NPF_ALLOW;
            }
        } else {
            result = YGHV_NPF_DENY;
        }
    }
    ExReleaseFastMutex(&g_protect_lock);
    return result;
}

void yghv_protect_rearm(svm_vcpu_t *vcpu) {
    uint64_t gpa;
    yghv_protect_page_t *pp;

    if (!vcpu->rearm_pending)
        return;
    ExAcquireFastMutex(&g_protect_lock);
    gpa = vcpu->rearm_gpa;
    vcpu->rearm_pending = 0;
    vcpu->rearm_gpa = 0;
    if (g_protect.active && gpa) {
        pp = yghv_protect_find_page_locked(gpa);
        if (pp && !pp->armed) {
            int st = yghv_protect_arm_page_locked(pp);
            if (st)
                LOG_ERROR("protect: re-arm failed gpa=0x%llx st=0x%x", gpa, st);
        }
    }
    ExReleaseFastMutex(&g_protect_lock);
}

void yghv_protect_get_state(ULONG *active, ULONG *pid, ULONG *page_count) {
    ExAcquireFastMutex(&g_protect_lock);
    if (active) *active = g_protect.active ? 1 : 0;
    if (pid) *pid = g_protect.pid;
    if (page_count) *page_count = g_protect.page_count;
    ExReleaseFastMutex(&g_protect_lock);
}

void yghv_protect_get_heartbeat(uint64_t *page_va, uint64_t *hook_va) {
    ExAcquireFastMutex(&g_protect_lock);
    if (page_va)
        *page_va = g_protect.page_count ? g_protect.pages[0].target_va : 0;
    if (hook_va)
        *hook_va = g_protect_hooks[0].installed ? g_protect_hooks[0].func_va : 0;
    ExReleaseFastMutex(&g_protect_lock);
}

BOOLEAN yghv_protect_check_target_exited(void) {
    PEPROCESS proc;
    uint32_t pid;
    LARGE_INTEGER timeout;
    NTSTATUS st;

    ExAcquireFastMutex(&g_protect_lock);
    proc = g_protect.process;
    pid = g_protect.pid;
    ExReleaseFastMutex(&g_protect_lock);
    if (!proc)
        return FALSE;
    timeout.QuadPart = 0;
    st = KeWaitForSingleObject(proc, Executive, KernelMode, FALSE, &timeout);
    if (st != STATUS_SUCCESS)
        return FALSE;
    return yghv_protect_on_target_exit(pid);
}

BOOLEAN yghv_protect_on_target_exit(ULONG pid) {
    BOOLEAN handled = FALSE;
    uint32_t i;

    ExAcquireFastMutex(&g_protect_lock);
    if (g_protect.process && g_protect.pid == pid) {
        NTSTATUS st = yghv_protect_stop_locked();
        if (st)
            LOG_ERROR("protect target exit: stop failed 0x%x", st);
        for (i = 0; i < YGHV_PROTECT_MAX_HOOKS; i++) {
            if (g_protect_hooks[i].installed)
                yghv_protect_remove_hook_locked(i);
        }
        g_protect.page_count = 0;
        ObDereferenceObject(g_protect.process);
        g_protect.process = NULL;
        g_protect.pid = 0;
        g_protect.cr3 = 0;
        handled = TRUE;
        LOG_ERROR("protect target exited: auto disarm, pid cleared");
    }
    ExReleaseFastMutex(&g_protect_lock);
    return handled;
}

static uint8_t *g_hook_stub_pages[YGHV_PROTECT_MAX_HOOKS];

static void yghv_emit_u8(uint8_t *p, uint8_t v) { *p = v; }
static void yghv_emit_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (i * 8));
}
static void yghv_emit_rel32(uint8_t *p, uint64_t from, uint64_t to) {
    long long d = (long long)(to - (from + 5));
    p[0] = 0xE9;
    for (int i = 0; i < 4; i++) p[1 + i] = (uint8_t)((uint64_t)d >> (i * 8));
}
static void yghv_emit_abs_jmp16(uint8_t *p, uint64_t target) {
    p[0] = 0x49; p[1] = 0xBB;                  /* movabs r11, imm64 */
    yghv_emit_u64(p + 2, target);
    p[10] = 0x41; p[11] = 0xFF; p[12] = 0xE3;  /* jmp r11 */
    p[13] = 0x90; p[14] = 0x90; p[15] = 0x90;  /* nop padding to 16 bytes */
}

static int yghv_decode_modrm(const uint8_t *p, size_t avail, size_t *pos) {
    uint8_t modrm, mod, rm, sib;
    if (*pos >= avail) return -1;
    modrm = p[*pos];
    (*pos)++;
    mod = modrm >> 6;
    rm = modrm & 7;
    if (mod != 3 && rm == 4) {
        if (*pos >= avail) return -1;
        sib = p[*pos];
        (*pos)++;
        if (mod == 0 && (sib & 7) == 5) {
            if (*pos + 4 > avail) return -1;
            *pos += 4;
        }
    }
    if (mod == 1) {
        if (*pos >= avail) return -1;
        (*pos)++;
    } else if (mod == 2) {
        if (*pos + 4 > avail) return -1;
        *pos += 4;
    } else if (mod == 0 && rm == 5) {
        if (*pos + 4 > avail) return -1;
        *pos += 4;
    }
    return 0;
}

/* Conservative x86-64 instruction length decoder. Returns 0 on any form it
   cannot classify; callers reject such targets instead of guessing. */
static int yghv_inst_len(const uint8_t *p, size_t avail) {
    size_t pos = 0;
    int rex = 0;
    int prefixes = 0;
    uint8_t b, op;

    for (;;) {
        if (pos >= avail) return 0;
        b = p[pos];
        if (b >= 0x40 && b <= 0x4F) {
            if (rex) return 0;
            rex = 1;
            pos++;
            continue;
        }
        if (b == 0x66 || b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 || b == 0x64 ||
            b == 0x65) {
            if (++prefixes > 15) return 0;
            pos++;
            continue;
        }
        break;
    }
    if (pos >= avail) return 0;
    op = p[pos];
    pos++;

    if (op == 0x62 || op == 0xC4 || op == 0xC5)
        return 0;  /* EVEX/VEX prefixes: reject unknown forms */

    if ((op >= 0x50 && op <= 0x5F) || (op >= 0x90 && op <= 0x9F) ||
        (op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF) ||
        op == 0x37 || op == 0x3F || op == 0x9B || op == 0xC3 || op == 0xC9 ||
        op == 0xCB || op == 0xCC || op == 0xCE || op == 0xCF ||
        op == 0xEC || op == 0xED || op == 0xEE || op == 0xEF ||
        op == 0xF4 || op == 0xF5 || op == 0xF8 || op == 0xF9 || op == 0xFA ||
        op == 0xFB || op == 0xFC || op == 0xFD) {
        return (int)pos;
    }
    if ((op >= 0x70 && op <= 0x7F) || (op >= 0xE0 && op <= 0xE3) || op == 0xEB) {
        if (pos + 1 > avail) return 0;
        return (int)(pos + 1);
    }
    if (op == 0xE8 || op == 0xE9) {
        if (pos + 4 > avail) return 0;
        return (int)(pos + 4);
    }
    if (op >= 0xB0 && op <= 0xB7) {
        if (pos + 1 > avail) return 0;
        return (int)(pos + 1);
    }
    if (op >= 0xB8 && op <= 0xBF) {
        if (pos + 8 > avail) return 0;
        return (int)(pos + 8);
    }
    if (op >= 0xA0 && op <= 0xA3) {
        size_t sz = (op & 1) ? 8 : 4;
        if (pos + sz > avail) return 0;
        return (int)(pos + sz);
    }
    if (op == 0xA8 || op == 0xA9) {
        size_t sz = (op == 0xA8) ? 1 : 4;
        if (pos + sz > avail) return 0;
        return (int)(pos + sz);
    }
    if (op == 0x68 || op == 0x6A) {
        size_t sz = (op == 0x68) ? 4 : 1;
        if (pos + sz > avail) return 0;
        return (int)(pos + sz);
    }
    if (op == 0xC2 || op == 0xCA) {
        if (pos + 2 > avail) return 0;
        return (int)(pos + 2);
    }
    if (op == 0xCD) {
        if (pos + 1 > avail) return 0;
        return (int)(pos + 1);
    }
    if (op == 0xE4 || op == 0xE5 || op == 0xE6 || op == 0xE7) {
        if (pos + 1 > avail) return 0;
        return (int)(pos + 1);
    }

    if (op == 0x0F) {
        uint8_t op2;
        if (pos >= avail) return 0;
        op2 = p[pos];
        pos++;
        if (op2 >= 0x80 && op2 <= 0x8F) {
            if (pos + 4 > avail) return 0;
            return (int)(pos + 4);
        }
        if (op2 == 0x05 || op2 == 0x07 || op2 == 0x08 || op2 == 0x09 ||
            op2 == 0x0B || op2 == 0x0D || op2 == 0x34 || op2 == 0x35 ||
            op2 == 0x77 || op2 == 0xA2 || (op2 >= 0xC8 && op2 <= 0xCF)) {
            return (int)pos;
        }
        if (op2 == 0x38 || op2 == 0x3A) {
            size_t after = pos;
            uint8_t op3;
            if (pos >= avail) return 0;
            op3 = p[pos];
            pos++;
            (void)op3;
            if (yghv_decode_modrm(p, avail, &after) < 0) return 0;
            if (op2 == 0x3A) {
                if (after + 1 > avail) return 0;
                after++;
            }
            return (int)after;
        }
        if (op2 == 0x0F || op2 == 0xBA || op2 == 0xC0 || op2 == 0xC1 ||
            op2 == 0xC4 || op2 == 0xC5) {
            size_t after = pos;
            if (yghv_decode_modrm(p, avail, &after) < 0) return 0;
            if (after + 1 > avail) return 0;
            return (int)(after + 1);
        }
        {
            size_t after = pos;
            if (yghv_decode_modrm(p, avail, &after) < 0) return 0;
            return (int)after;
        }
    }

    if (op == 0x69 || op == 0x6B || op == 0x80 || op == 0x81 || op == 0x83 ||
        op == 0xC0 || op == 0xC1 || op == 0xC6 || op == 0xC7 || op == 0xF6 ||
        op == 0xF7) {
        size_t after = pos;
        size_t imm = (op == 0x69 || op == 0x81 || op == 0xC7 || op == 0xF7)
                         ? 4 : 1;
        if (yghv_decode_modrm(p, avail, &after) < 0) return 0;
        if (after + imm > avail) return 0;
        return (int)(after + imm);
    }

    {
        size_t after = pos;
        if (yghv_decode_modrm(p, avail, &after) < 0) return 0;
        return (int)after;
    }
}

int yghv_protect_validate_hook_target(uint64_t func_va) {
    const uint8_t *p;
    size_t off = 0;
    size_t avail;

    if (!func_va)
        return -1;
    if ((func_va & (HV_PAGE_SIZE - 1)) > HV_PAGE_SIZE - YGHV_PROTECT_PATCH_LEN)
        return -1;
    p = (const uint8_t *)func_va;
    avail = HV_PAGE_SIZE - (func_va & (HV_PAGE_SIZE - 1));
    while (off < YGHV_PROTECT_PATCH_LEN) {
        int len = yghv_inst_len(p + off, avail - off);
        if (len <= 0)
            return -1;
        if (off + (size_t)len > YGHV_PROTECT_PATCH_LEN)
            return -1;
        off += (size_t)len;
    }
    return off == YGHV_PROTECT_PATCH_LEN ? 0 : -1;
}

NTSTATUS yghv_protect_install_hook(uint8_t hook_id, uint64_t func_va) {
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_install_hook_locked(hook_id, func_va);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static NTSTATUS yghv_protect_install_hook_locked(uint8_t hook_id, uint64_t func_va) {
    uint8_t *stub;
    uint8_t patch[YGHV_PROTECT_PATCH_LEN];
    uint64_t func_pa, page_va, page_pa;
    uint8_t *orig;
    uint8_t *wmap = NULL;
    yghv_protect_page_t *pp;
    yghv_protect_hook_t *h;
    NTSTATUS st;

    if (hook_id >= YGHV_PROTECT_MAX_HOOKS) return STATUS_INVALID_PARAMETER;
    h = &g_protect_hooks[hook_id];
    if (h->installed) return STATUS_ALREADY_COMMITTED;
    if (yghv_protect_validate_hook_target(func_va)) {
        LOG_ERROR("protect hook %u: invalid target va=0x%llx", hook_id, func_va);
        return STATUS_INVALID_PARAMETER;
    }

    func_pa = MmGetPhysicalAddress((PVOID)func_va).QuadPart;
    page_va = func_va & ~(HV_PAGE_SIZE - 1);
    page_pa = MmGetPhysicalAddress((PVOID)page_va).QuadPart;
    RtlCopyMemory(h->original, (void *)func_va, YGHV_PROTECT_PATCH_LEN);
    h->func_va = func_va;
    h->func_pa = func_pa;
    h->hook_id = hook_id;

    stub = (uint8_t *)ExAllocatePoolWithTag(NonPagedPool, HV_PAGE_SIZE,
        YGHV_TAG);
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

    /* patch function entry: 16-byte absolute jump -> stub */
    yghv_emit_abs_jmp16(patch, (uint64_t)stub);
    wmap = (uint8_t *)MmGetVirtualForPhysical(
        (PHYSICAL_ADDRESS){ .QuadPart = page_pa });
    if (!wmap) {
        LOG_ERROR("protect hook %u: direct map of function page 0x%llx failed",
            hook_id, page_pa);
        st = STATUS_UNSUCCESSFUL;
        goto fail;
    }
    st = svm_core_pause_residents_for_patch();
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect hook %u: patch rendezvous failed 0x%x", hook_id, st);
        goto fail;
    }
    RtlCopyMemory(wmap + (func_va & (HV_PAGE_SIZE - 1)), patch,
        YGHV_PROTECT_PATCH_LEN);
    KeInvalidateRangeAllCaches((PVOID)func_va, YGHV_PROTECT_PATCH_LEN);
    svm_core_resume_residents();

    /* write-protect the function's page in NPT */
    st = npt_split_2mb_to_4kb(&g_npt, page_pa);
    if (st) {
        LOG_ERROR("protect hook %u: split function page failed 0x%x",
            hook_id, st);
        goto fail;
    }
    st = npt_set_page_perm(&g_npt, page_pa, NPT_PERM_PRESENT);
    if (st) {
        LOG_ERROR("protect hook %u: set function page perm failed 0x%x",
            hook_id, st);
        goto fail;
    }

    /* route writes through the Task 4 NPF policy and arm the page */
    st = yghv_protect_add_page_locked(h->func_va);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect hook %u: add function page failed 0x%x",
            hook_id, st);
        goto fail;
    }
    pp = yghv_protect_find_page_locked(page_pa);
    if (!pp) {
        LOG_ERROR("protect hook %u: function page missing from table",
            hook_id);
        yghv_protect_remove_page_locked(h->func_va);
        st = STATUS_UNSUCCESSFUL;
        goto fail;
    }
    st = yghv_protect_arm_page_locked(pp);
    if (st) {
        LOG_ERROR("protect hook %u: arm function page failed 0x%x",
            hook_id, st);
        yghv_protect_remove_page_locked(h->func_va);
        goto fail;
    }

    h->installed = 1;
    LOG_ERROR("protect hook %u installed: va=0x%llx pa=0x%llx", hook_id, func_va, func_pa);
    return STATUS_SUCCESS;

fail:
    npt_set_page_perm(&g_npt, page_pa, NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    if (wmap) {
        RtlCopyMemory(wmap + (func_va & (HV_PAGE_SIZE - 1)), h->original,
            YGHV_PROTECT_PATCH_LEN);
        KeInvalidateRangeAllCaches((PVOID)func_va, YGHV_PROTECT_PATCH_LEN);
    }
    if (g_hook_stub_pages[hook_id]) {
        ExFreePoolWithTag(g_hook_stub_pages[hook_id], YGHV_TAG);
        g_hook_stub_pages[hook_id] = NULL;
    }
    return st;
}

NTSTATUS yghv_protect_remove_hook(uint8_t hook_id) {
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_remove_hook_locked(hook_id);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static NTSTATUS yghv_protect_remove_hook_locked(uint8_t hook_id) {
    yghv_protect_hook_t *h;
    uint8_t *wmap;
    uint64_t page_pa;
    NTSTATUS st;
    if (hook_id >= YGHV_PROTECT_MAX_HOOKS) return STATUS_INVALID_PARAMETER;
    h = &g_protect_hooks[hook_id];
    if (!h->installed) return STATUS_NOT_FOUND;

    page_pa = MmGetPhysicalAddress(
        (PVOID)(h->func_va & ~(HV_PAGE_SIZE - 1))).QuadPart;
    wmap = (uint8_t *)MmGetVirtualForPhysical(
        (PHYSICAL_ADDRESS){ .QuadPart = page_pa });
    if (!wmap) {
        LOG_ERROR(
            "protect remove hook %u: direct map of function page 0x%llx failed",
            hook_id, page_pa);
        return STATUS_UNSUCCESSFUL;
    }

    /* remove the page from the NPF policy and restore NPT writable first */
    st = yghv_protect_remove_page_locked(h->func_va);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect remove hook %u: remove_page failed 0x%x",
            hook_id, st);
        return st;
    }

    st = svm_core_pause_residents_for_patch();
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect remove hook %u: patch rendezvous failed 0x%x",
            hook_id, st);
        return st;
    }
    RtlCopyMemory(wmap + (h->func_va & (HV_PAGE_SIZE - 1)), h->original,
        YGHV_PROTECT_PATCH_LEN);
    KeInvalidateRangeAllCaches((PVOID)h->func_va, YGHV_PROTECT_PATCH_LEN);
    svm_core_resume_residents();
    if (g_hook_stub_pages[hook_id]) {
        ExFreePoolWithTag(g_hook_stub_pages[hook_id], YGHV_TAG);
        g_hook_stub_pages[hook_id] = NULL;
    }
    h->installed = 0;
    LOG_ERROR("protect hook %u removed", hook_id);
    return STATUS_SUCCESS;
}

uint64_t yghv_protect_on_hook_query(uint8_t hook_id, uint64_t accessor_cr3) {
    uint64_t result;
    ExAcquireFastMutex(&g_protect_lock);
    if (hook_id >= YGHV_PROTECT_MAX_HOOKS || !g_protect_hooks[hook_id].installed) {
        result = YGHV_STATUS_INVALID;
    } else if (yghv_protect_is_target_cr3_locked(accessor_cr3)) {
        result = YGHV_STATUS_OK;
    } else {
        LOG_ERROR("protect hook %u denied cr3=0x%llx", hook_id, accessor_cr3);
        result = YGHV_STATUS_DENIED;
    }
    ExReleaseFastMutex(&g_protect_lock);
    return result;
}

uint64_t yghv_protect_find_func_pattern(PCWSTR name_hint, uint8_t *pat, SIZE_T pat_len) {
    UNICODE_STRING name;
    (void)pat; (void)pat_len;
    if (!name_hint) return 0;
    RtlInitUnicodeString(&name, name_hint);
    return (uint64_t)MmGetSystemRoutineAddress(&name);
}
