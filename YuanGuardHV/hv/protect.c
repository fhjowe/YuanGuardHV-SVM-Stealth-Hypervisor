#include <ntifs.h>
#include "protect.h"
#include "svm_vcpu.h"
#include "debug.h"
#include "control_plane.h"

extern npt_mgr_t g_npt;

NTKERNELAPI NTSTATUS PsLookupProcessByProcessId(HANDLE ProcessId, PEPROCESS *Process);
NTKERNELAPI NTSTATUS MmCopyVirtualMemory(
    PEPROCESS SourceProcess, PVOID SourceAddress,
    PEPROCESS TargetProcess, PVOID TargetAddress,
    SIZE_T BufferSize, KPROCESSOR_MODE PreviousMode,
    PSIZE_T NumberOfBytesCopied);
NTKERNELAPI NTSTATUS ZwFlushBuffersFile(HANDLE FileHandle,
                                        PIO_STATUS_BLOCK IoStatusBlock);

static const uint64_t YGHV_PT_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

static void yghv_hook_diag(const char *stage, NTSTATUS st) {
    static const char hex[] = "0123456789abcdef";
    static ULONG diag_lines = 0;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    char buf[96];
    size_t n = 0;
    int i;

    /* REV-048: cap the diagnostic log so it cannot grow without bound. */
    if (++diag_lines > 4096)
        return;

    RtlInitUnicodeString(&name, L"\\SystemRoot\\yghv_hook.log");
    InitializeObjectAttributes(&oa, &name,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_APPEND_DATA, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
        FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0)))
        return;
    while (stage[n] && n < sizeof(buf) - 16) {
        buf[n] = stage[n];
        n++;
    }
    buf[n++] = ' '; buf[n++] = 's'; buf[n++] = 't'; buf[n++] = '=';
    buf[n++] = '0'; buf[n++] = 'x';
    for (i = 7; i >= 0; i--)
        buf[n++] = hex[((uint32_t)st >> (i * 4)) & 0xF];
    buf[n++] = '\r'; buf[n++] = '\n';
    ZwWriteFile(h, NULL, NULL, NULL, &iosb, buf, (ULONG)n, NULL, NULL);
    ZwFlushBuffersFile(h, &iosb);
    ZwClose(h);
}

/* REV-036: yghv_hook_diag performs Zw* file I/O, which requires PASSIVE_LEVEL.
   Every diag call site runs under g_protect_lock (FAST_MUTEX -> APC_LEVEL),
   an IRQL violation on exactly the error paths the diag serves.  Record the
   failure in memory (LOG_ERROR is IRQL-safe) and flush the log after the lock
   is released. */
static const char *g_hook_diag_stage = NULL;
static NTSTATUS g_hook_diag_status = 0;

static void yghv_hook_diag_mark(const char *stage, NTSTATUS st) {
    g_hook_diag_stage = stage;
    g_hook_diag_status = st;
    LOG_ERROR("hook diag: %s st=0x%x", stage, (unsigned)st);
}

static void yghv_hook_diag_flush(void) {
    if (g_hook_diag_stage) {
        yghv_hook_diag(g_hook_diag_stage, g_hook_diag_status);
        g_hook_diag_stage = NULL;
        g_hook_diag_status = 0;
    }
}

yghv_protect_state_t g_protect;
yghv_protect_hook_t g_protect_hooks[YGHV_PROTECT_MAX_HOOKS];
uint64_t g_protect_cr3_list[YGHV_PROTECT_MAX_TARGETS];
static FAST_MUTEX g_protect_lock;

static BOOLEAN yghv_protect_is_target_cr3_locked(uint64_t cr3) {
    uint32_t i;
    /* 9.249 (run_c10 evidence): with CR4.PCIDE=1 the CR3 in the VMCB carries
     * the PCID in bits 11:0 (and possibly bit 63 no-flush), while targets[]
     * stores KPROCESS.DirectoryTableBase = PFN only. A raw == compare always
     * fails -> every protected write was DENY'd. Compare PFN bits only
     * (51:12); PCID/no-flush must not affect target matching. */
    uint64_t cr3_pfn = cr3 & YGHV_PT_ADDR_MASK;
    for (i = 0; i < g_protect.target_count; i++) {
        if (g_protect.targets[i].cr3 != 0 &&
            (g_protect.targets[i].cr3 & YGHV_PT_ADDR_MASK) == cr3_pfn)
            return TRUE;
    }
    return FALSE;
}

static yghv_protect_page_t *yghv_protect_find_page_locked(uint64_t gpa) {
    uint32_t t, i;
    uint64_t page = gpa & ~0xFFFULL;
    for (t = 0; t < g_protect.target_count; t++) {
        for (i = 0; i < g_protect.targets[t].page_count; i++) {
            if (g_protect.targets[t].pages[i].gpa == page)
                return &g_protect.targets[t].pages[i];
        }
    }
    return NULL;
}

static yghv_protect_target_t *yghv_protect_find_target_by_cr3_locked(
    uint64_t cr3) {
    uint32_t i;
    uint64_t cr3_pfn = cr3 & YGHV_PT_ADDR_MASK;   /* 9.249: PCID-immune */
    for (i = 0; i < g_protect.target_count; i++) {
        if (g_protect.targets[i].cr3 != 0 &&
            (g_protect.targets[i].cr3 & YGHV_PT_ADDR_MASK) == cr3_pfn)
            return &g_protect.targets[i];
    }
    return NULL;
}

static yghv_protect_target_t *yghv_protect_find_target_by_pid_locked(
    uint32_t pid) {
    uint32_t i;
    for (i = 0; i < g_protect.target_count; i++) {
        if (g_protect.targets[i].pid == pid)
            return &g_protect.targets[i];
    }
    return NULL;
}

static void yghv_protect_refresh_cr3_list_locked(void) {
    uint32_t i;
    for (i = 0; i < YGHV_PROTECT_MAX_TARGETS; i++) {
        g_protect_cr3_list[i] = (i < g_protect.target_count)
            ? g_protect.targets[i].cr3 : 0;
    }
}

static NTSTATUS yghv_protect_add_page_for_locked(yghv_protect_target_t *t,
    uint64_t target_va);
static NTSTATUS yghv_protect_remove_page_for_locked(yghv_protect_target_t *t,
    uint64_t target_va);
static int yghv_protect_arm_page_locked(yghv_protect_page_t *p);
static int yghv_protect_disarm_page_locked(yghv_protect_page_t *p);
static NTSTATUS yghv_protect_start_locked(void);
static NTSTATUS yghv_protect_stop_locked(void);
static NTSTATUS yghv_protect_install_hook_locked(uint8_t hook_id, uint64_t func_va);
static NTSTATUS yghv_protect_remove_hook_locked(uint8_t hook_id);

/* 9.272: PA->VA via an arithmetic direct map whose BASE IS DERIVED AT BOOT
 * from MmGetVirtualForPhysical (run 206c5f proved the helper is selective —
 * NULL for some in-use RAM pages — but correct where it answers; and 206c5g
 * proved the textbook base 0xFFFF800000000000 is NOT readable for all RAM on
 * this box: bugcheck 0xD1 faulting at 0xFFFF8000001ADF80). Deriving the base
 * keeps the deterministic arithmetic channel while inheriting whatever real
 * layout Windows uses. Fail-safe: base 0 (derivation failed) or PA at/above
 * the captured RAM ceiling => NULL; every caller checks. */
static uint64_t g_pa_va_base;
static uint64_t g_pa_ceiling;

PVOID yghv_pa_to_va(uint64_t pa) {
    if (g_pa_va_base == 0 || pa >= g_pa_ceiling)
        return NULL;
    return (PVOID)(g_pa_va_base + pa);
}

static void yghv_pa_to_va_init(void) {
    /* sample the helper on this driver's own pages: ground truth VA from
     * MmGetVirtualForPhysical, cross-checked byte-for-byte. */
    static volatile uint64_t probe[2] = { 0x1122334455667788ULL,
                                          0x8877665544332211ULL };
    PHYSICAL_ADDRESS pa;
    PVOID hv;
    g_pa_va_base = 0;
    g_pa_ceiling = 0;
    {
        /* RAM ceiling from the physical memory ranges (PASSIVE, boot time) */
        PPHYSICAL_MEMORY_RANGE r = MmGetPhysicalMemoryRanges();
        if (r) {
            int i;
            for (i = 0; i < 64; i++) {
                if (r[i].BaseAddress.QuadPart == 0 &&
                    r[i].NumberOfBytes.QuadPart == 0)
                    break;
                {
                    uint64_t end = (uint64_t)r[i].BaseAddress.QuadPart +
                                   (uint64_t)r[i].NumberOfBytes.QuadPart;
                    if (end > g_pa_ceiling)
                        g_pa_ceiling = end;
                }
            }
            ExFreePool(r);
        }
        if (g_pa_ceiling == 0)
            return;
    }
    pa.QuadPart = (LONGLONG)MmGetPhysicalAddress((PVOID)probe).QuadPart;
    hv = MmGetVirtualForPhysical(pa);
    if (!hv)
        return;
    if (RtlCompareMemory(hv, (PVOID)probe, 16) == 16)
        g_pa_va_base = (uint64_t)hv - (uint64_t)pa.QuadPart;
}

static uint64_t yghv_pt_read(uint64_t table_pa, uint64_t index) {
    uint64_t *va;
    if (!table_pa) return 0;
    va = (uint64_t *)yghv_pa_to_va(table_pa);
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

static uint64_t yghv_protect_resolve_va_for(yghv_protect_target_t *t,
    uint64_t target_va) {
    KAPC_STATE apc;
    uint64_t pa;

    if (!t || !t->process)
        return 0;

    KeStackAttachProcess(t->process, &apc);
    pa = MmGetPhysicalAddress((PVOID)target_va).QuadPart;
    KeUnstackDetachProcess(&apc);
    if (pa)
        return pa;

    /* Demand-paged pages have no present PTE; fault one byte in. */
    {
        SIZE_T copied = 0;
        UCHAR tmp;
        NTSTATUS st = MmCopyVirtualMemory(t->process, (PVOID)target_va,
                                          IoGetCurrentProcess(), &tmp, 1,
                                          KernelMode, &copied);
        if (!NT_SUCCESS(st) || copied != 1)
            return 0;
    }

    KeStackAttachProcess(t->process, &apc);
    pa = MmGetPhysicalAddress((PVOID)target_va).QuadPart;
    KeUnstackDetachProcess(&apc);
    return pa;
}

static uint64_t yghv_protect_resolve_va(uint64_t target_va) {
    return yghv_protect_resolve_va_for(&g_protect.targets[0], target_va);
}

NTSTATUS yghv_protect_init(void) {
    yghv_pa_to_va_init();
    ExInitializeFastMutex(&g_protect_lock);
    RtlZeroMemory(&g_protect, sizeof(g_protect));
    RtlZeroMemory(g_protect_hooks, sizeof(g_protect_hooks));
    g_protect.target_count = 1;
    g_protect.config.auto_disarm = 1;
    g_protect.config.deny_status = 0xC0000022;
    yghv_protect_refresh_cr3_list_locked();
    return STATUS_SUCCESS;
}

void yghv_protect_cleanup(void) {
    uint32_t i, t;
    ExAcquireFastMutex(&g_protect_lock);
    for (i = 0; i < YGHV_PROTECT_MAX_HOOKS; i++) {
        if (g_protect_hooks[i].installed)
            yghv_protect_remove_hook_locked(i);
    }
    yghv_protect_stop_locked();
    for (t = 0; t < g_protect.target_count; t++) {
        if (g_protect.targets[t].process) {
            ObDereferenceObject(g_protect.targets[t].process);
            g_protect.targets[t].process = NULL;
        }
    }
    RtlZeroMemory(&g_protect, sizeof(g_protect));
    RtlZeroMemory(g_protect_hooks, sizeof(g_protect_hooks));
    RtlZeroMemory(g_protect_cr3_list, sizeof(g_protect_cr3_list));
    ExReleaseFastMutex(&g_protect_lock);
    yghv_hook_diag_flush();
}

NTSTATUS yghv_protect_set_target(uint32_t pid) {
    PEPROCESS proc = NULL;
    uint64_t cr3;
    NTSTATUS st;
    uint32_t idx;
    yghv_protect_target_t *t;
    uint32_t i;

    if (pid == 0)
        return STATUS_INVALID_PARAMETER;

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
    t = yghv_protect_find_target_by_pid_locked(pid);
    if (t) {
        idx = (uint32_t)(t - g_protect.targets);
    } else if (g_protect.targets[0].pid == 0) {
        idx = 0;
    } else if (g_protect.target_count < YGHV_PROTECT_MAX_TARGETS) {
        idx = g_protect.target_count++;
        RtlZeroMemory(&g_protect.targets[idx], sizeof(g_protect.targets[idx]));
    } else {
        ExReleaseFastMutex(&g_protect_lock);
        ObDereferenceObject(proc);
        LOG_ERROR("protect set_target: target table full");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    t = &g_protect.targets[idx];
    if (t->page_count) {
        BOOLEAN was_armed[YGHV_PROTECT_MAX_PAGES];
        ULONG j;
        for (j = 0; j < t->page_count; j++)
            was_armed[j] = t->pages[j].armed ? TRUE : FALSE;
        for (i = 0; i < t->page_count; i++) {
            if (t->pages[i].armed) {
                int ds = yghv_protect_disarm_page_locked(&t->pages[i]);
                if (ds) {
                    LOG_ERROR("protect set_target: disarm page %u failed 0x%x",
                        i, ds);
                    /* REV-044: roll back the pages already disarmed so the
                       slot keeps its original armed state on this failure
                       path instead of a mixed armed/disarmed state. */
                    for (j = 0; j < t->page_count; j++) {
                        if (was_armed[j] && !t->pages[j].armed)
                            yghv_protect_arm_page_locked(&t->pages[j]);
                    }
                    ExReleaseFastMutex(&g_protect_lock);
                    ObDereferenceObject(proc);
                    return STATUS_UNSUCCESSFUL;
                }
            }
        }
        t->page_count = 0;
    }
    if (t->process)
        ObDereferenceObject(t->process);
    t->pid = pid;
    t->process = proc;
    t->cr3 = cr3;
    yghv_protect_refresh_cr3_list_locked();
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
    st = yghv_protect_add_page_for_locked(&g_protect.targets[0], target_va);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

NTSTATUS yghv_protect_add_page_for(uint64_t cr3, uint64_t target_va) {
    NTSTATUS st;
    yghv_protect_target_t *t;
    ExAcquireFastMutex(&g_protect_lock);
    t = yghv_protect_find_target_by_cr3_locked(cr3);
    if (!t) {
        ExReleaseFastMutex(&g_protect_lock);
        return STATUS_ACCESS_DENIED;
    }
    st = yghv_protect_add_page_for_locked(t, target_va);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

#if defined(YGHV_BAREMETAL_STEP) && (YGHV_BAREMETAL_STEP == 206)
/* 9.253: island-safe armed-page ring. The 9.252 progress.log traces ran under
 * ExAcquireFastMutex (APC_LEVEL) while all cores were virtualized; the run_c10
 * wedge was exactly such a synchronous ZwWriteFile that never completed (the
 * selftest thread sat 40min between the two adjacent trace calls). Runtime
 * file I/O from this driver is banned — record to memory here, flush in
 * DriverUnload after Sv206CoopUnload brings the cores back to bare metal. */
static uint64_t g_s206_addlog[16];
static uint32_t g_s206_addidx;
static void yghv_s206_addlog_record(uint64_t va, uint64_t gpa) {
    if (g_s206_addidx + 2 <= 16) {
        g_s206_addlog[g_s206_addidx++] = va;
        g_s206_addlog[g_s206_addidx++] = gpa;
    }
}
void yghv_s206_flush_addlog(void) {
    uint32_t i;
    for (i = 0; i + 1 < g_s206_addidx; i += 2) {
        yghv_trace_u64("s206 addlog va", g_s206_addlog[i]);
        yghv_trace_u64("s206 addlog gpa", g_s206_addlog[i + 1]);
    }
}
#endif

static NTSTATUS yghv_protect_add_page_for_locked(yghv_protect_target_t *t,
    uint64_t target_va) {
    uint64_t gpa;
    yghv_protect_page_t *p;
    uint32_t i;
    if (!t || t->page_count >= YGHV_PROTECT_MAX_PAGES)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!t->cr3)
        return STATUS_INVALID_PARAMETER;
    /* Duplicate (target, va) entries would alias the same gpa in two slots
       and corrupt the armed/disarm accounting (find_page only ever sees the
       first). Idempotent no-op keeps a repeated protect-page call safe. */
    for (i = 0; i < t->page_count; i++) {
        if (t->pages[i].target_va == target_va)
            return STATUS_SUCCESS;
    }
    gpa = yghv_protect_resolve_va_for(t, target_va);
    gpa &= ~(uint64_t)0xFFFULL;
    if (!gpa) {
        LOG_ERROR("protect add_page: va 0x%llx not mapped", target_va);
        return STATUS_INVALID_ADDRESS;
    }
    /* 9.258 (206-C3): fill the entry BEFORE publishing page_count — the
     * island's lock-free readers scan by page_count, so incrementing first
     * exposed a garbage entry window (batch-change quiesce, store-order
     * release on x86). */
    p = &t->pages[t->page_count];
    p->gpa = gpa;
    p->target_va = target_va;
    p->flags = YGHV_PROTECT_MEM;
    p->armed = 0;
    t->page_count++;
#if defined(YGHV_BAREMETAL_STEP) && (YGHV_BAREMETAL_STEP == 206)
    /* 9.253: island ring only (was 9.252 progress.log traces — file I/O under
       FastMutex during live virtualization wedged run_c10 for 40min). */
    yghv_s206_addlog_record(target_va, gpa);
#endif
    return STATUS_SUCCESS;
}

NTSTATUS yghv_protect_remove_page(uint64_t target_va) {
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_remove_page_for_locked(&g_protect.targets[0], target_va);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

NTSTATUS yghv_protect_remove_page_for(uint64_t cr3, uint64_t target_va) {
    NTSTATUS st;
    yghv_protect_target_t *t;
    ExAcquireFastMutex(&g_protect_lock);
    t = yghv_protect_find_target_by_cr3_locked(cr3);
    if (!t) {
        ExReleaseFastMutex(&g_protect_lock);
        return STATUS_ACCESS_DENIED;
    }
    st = yghv_protect_remove_page_for_locked(t, target_va);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

/* 206-C4: cross-process protection. The 0x801/0x802 variants resolve the
 * target slot by the CALLER's CR3, which only works when the controlling
 * process IS the target (selftest). Product semantics need an external
 * controller to arm a page inside another process, so these resolve the
 * slot by pid instead; the target itself must already be registered via
 * set-target. resolve_va_for already walks the TARGET's page tables
 * (KeStackAttachProcess), so no further changes are needed. General code,
 * deliberately not 206-gated: protect.c compiles in the default build too
 * and an unmatched-symbol default link (LNK2019) is worse than dead code. */
NTSTATUS yghv_protect_add_page_for_pid(uint32_t pid, uint64_t target_va) {
    NTSTATUS st;
    yghv_protect_target_t *t;
    if (pid == 0)
        return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&g_protect_lock);
    t = yghv_protect_find_target_by_pid_locked(pid);
    if (!t) {
        ExReleaseFastMutex(&g_protect_lock);
        return STATUS_NOT_FOUND;
    }
    st = yghv_protect_add_page_for_locked(t, target_va);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

NTSTATUS yghv_protect_remove_page_for_pid(uint32_t pid, uint64_t target_va) {
    NTSTATUS st;
    yghv_protect_target_t *t;
    if (pid == 0)
        return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&g_protect_lock);
    t = yghv_protect_find_target_by_pid_locked(pid);
    if (!t) {
        ExReleaseFastMutex(&g_protect_lock);
        return STATUS_NOT_FOUND;
    }
    st = yghv_protect_remove_page_for_locked(t, target_va);
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static NTSTATUS yghv_protect_remove_page_for_locked(yghv_protect_target_t *t,
    uint64_t target_va) {
    uint32_t i;
    if (!t)
        return STATUS_NOT_FOUND;
    for (i = 0; i < t->page_count; i++) {
        if (t->pages[i].target_va == target_va) {
            if (t->pages[i].armed) {
                int st = yghv_protect_disarm_page_locked(&t->pages[i]);
                if (st)
                    return (NTSTATUS)st;
            }
            t->pages[i] = t->pages[t->page_count - 1];
            t->page_count--;
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

/* 9.258 (206-C3): island-safe silent reopen for the vendored NPF handler's
 * NONE-on-write path (unknown/removed armed page). Lock-free, no telemetry:
 * the guest PTE is writable, so the right move is to restore the NPT to
 * writable and let the write re-execute — an injected #PF there loops
 * forever (spurious-fault retry, the c12 victim storm). TLB propagation is
 * free: this core gets TlbControl=1 from the caller, and every CR3-write
 * exit already flushes. */
void yghv_protect_reopen_page_bare(uint64_t gpa) {
    if (!g_protect.active)
        return;
    (void)npt_set_page_perm(&g_npt, gpa & ~(uint64_t)0xFFFULL,
        NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
}

/* 9.258 (206-C3): target-exit watchdog. Registered via
 * PsSetCreateProcessNotifyRoutine (main.c); fires on process teardown.
 * Without this, a dead target's armed pages leak write-protected NPT state
 * onto freed physical pages — the next owner's writes hit stale NPT
 * entries (the c12 victim storm amplifier). PASSIVE_LEVEL callback context,
 * mutex-protected, memory + NPT bit ops only. */
void yghv_protect_on_process_exit(uint32_t pid) {
    yghv_protect_target_t *t;
    uint32_t i;

    if (pid == 0)
        return;
    ExAcquireFastMutex(&g_protect_lock);
    t = yghv_protect_find_target_by_pid_locked(pid);
    if (t) {
        for (i = 0; i < t->page_count; i++) {
            if (t->pages[i].armed)
                (void)yghv_protect_disarm_page_locked(&t->pages[i]);
        }
        if (t->process)
            ObDereferenceObject(t->process);
        RtlZeroMemory(t, sizeof(*t));
        yghv_protect_refresh_cr3_list_locked();
    }
    ExReleaseFastMutex(&g_protect_lock);
}

NTSTATUS yghv_protect_start(void) {
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_start_locked();
    ExReleaseFastMutex(&g_protect_lock);
    return st;
}

static NTSTATUS yghv_protect_start_locked(void) {
    uint32_t t, i;
    ULONG total = 0;
    for (t = 0; t < g_protect.target_count; t++) {
        if (g_protect.targets[t].cr3)
            total += g_protect.targets[t].page_count;
    }
    /* REV-042: 0 pages means nothing to arm — reject (the default persistent
       path always has >= 1 page: workload + hook).  The gated baremetal
       step-9 "keepalive only" flow expects a success here; that experiment
       should add a keepalive page or be updated, it is not the default path. */
    if (!total)
        return STATUS_INVALID_PARAMETER;
    for (t = 0; t < g_protect.target_count; t++) {
        for (i = 0; i < g_protect.targets[t].page_count; i++) {
            int st = yghv_protect_arm_page_locked(&g_protect.targets[t].pages[i]);
            if (st) {
                LOG_ERROR("protect start: arm target %u page %u failed 0x%x",
                    t, i, st);
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
    }
    g_protect.active = TRUE;
    LOG_ERROR("protect start: %u pages armed", total);
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
    uint32_t t, i;
    NTSTATUS first_failure = STATUS_SUCCESS;
    for (t = 0; t < g_protect.target_count; t++) {
        for (i = 0; i < g_protect.targets[t].page_count; i++) {
            if (g_protect.targets[t].pages[i].armed) {
                int st = yghv_protect_disarm_page_locked(
                    &g_protect.targets[t].pages[i]);
                if (st && first_failure == STATUS_SUCCESS) {
                    first_failure = (NTSTATUS)st;
                    LOG_ERROR("protect stop: disarm target %u page %u failed 0x%x",
                        t, i, st);
                }
            }
        }
    }
    if (first_failure != STATUS_SUCCESS)
        return STATUS_UNSUCCESSFUL;
    g_protect.active = FALSE;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* 9.249 step206-C2: LOCK-FREE in-island NPF verdict for the vendored    */
/* upstream handler (svm_simplevm206.cpp). Constraints of that context:  */
/* IRQL = DISPATCH (SvHandleVmExit raises), GIF=0, and NO blocking —     */
/* ExAcquireFastMutex would bugcheck (DISPATCH) / deadlock (island,      */
/* 9.229 lesson twice). So this path must be pure memory + NPT           */
/* arithmetic (direct-map writes), no locks, no LOG/trace, no allocs.    */
/*                                                                       */
/* Lock-free correctness: the protected-page table (g_protect.targets)   */
/* is nonpaged and only mutated at PASSIVE under g_protect_lock by       */
/* IOCTLs. The island reads it WITHOUT the lock, so an IOCTL racing an   */
/* NPF verdict may see a torn view. Mitigations: (a) mutations are       */
/* small fixed-struct writes (single u64 gpa / u8 flags — naturally      */
/* atomic on x64 for aligned u64); (b) a wrong verdict's worst case is   */
/* one spurious #PF or one missed protection hit, self-correcting on     */
/* the next write; (c) the add/remove IOCTL paths will set               */
/* g_protect.active=0 (quiesce) around batch mutations in 206-C3.        */
/* TLB: npt_set_page_perm flips the NPT PTE; the handler sets            */
/* TlbControl=1 (flush-all on next VMRUN) when a flip happened — no      */
/* INVLPGA needed since we do not tag per-ASID in the vendored VMCB.     */
/* ------------------------------------------------------------------ */
/* 9.269: island-safe control probe for the DENY GVA decoder's walk
 * diagnostics — walks the first armed target's (cr3, va) pair, whose gpa the
 * control plane already knows. Lock-free read of the same tables the NPF
 * verdict scans; pure arithmetic; no telemetry. 0 = no armed target. */
uint64_t yghv_protect_control_walk_gpa(void) {
    uint64_t diag[5];
    yghv_protect_control_walk_diag(diag);
    return diag[0];
}

/* 9.270: full-trace variant -- out[0]=final gpa, out[1]=direct-map VA of the
 * PML4 page (0 = MmGetVirtualForPhysical returned NULL), out[2]=PML4E raw,
 * out[3]=direct-map VA of the PDP page, out[4]=PDPTE raw. Island-safe pure
 * reads; classifies walk-fail as helper-NULL vs wrong-content. */
void yghv_protect_control_walk_diag(uint64_t out[5]) {
    uint32_t t;
    uint64_t cr3 = 0, va = 0;
    static const uint64_t M = 0x000FFFFFFFFFF000ULL;
    out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0; out[4] = 0;
    for (t = 0; t < g_protect.target_count; t++) {
        if (g_protect.targets[t].cr3 != 0 &&
            g_protect.targets[t].page_count > 0) {
            cr3 = g_protect.targets[t].cr3;
            va = g_protect.targets[t].pages[0].target_va;
            break;
        }
    }
    if (!cr3)
        return;
    {
        uint64_t *v1;
        v1 = (uint64_t *)yghv_pa_to_va(cr3 & M);
        out[1] = (uint64_t)v1;
        if (!v1)
            return;
        {
            uint64_t pml4e = v1[(va >> 39) & 0x1FF];
            uint64_t *v2;
            out[2] = pml4e;
            if (!(pml4e & 1)) {
                out[0] = yghv_protect_guest_va_to_pa(cr3, va);
                return;
            }
            v2 = (uint64_t *)yghv_pa_to_va(pml4e & M);
            out[3] = (uint64_t)v2;
            if (!v2)
                return;
            out[4] = v2[(va >> 30) & 0x1FF];
        }
        out[0] = yghv_protect_guest_va_to_pa(cr3, va);
    }
}

yghv_npf_result_t yghv_protect_on_npf_write_bare(uint64_t guest_cr3,
                                                 uint32_t cpl,
                                                 uint64_t gpa,
                                                 uint64_t *rearm_gpa_out,
                                                 int *flip_out) {
    yghv_protect_page_t *pp;
    yghv_npf_result_t result = YGHV_NPF_NONE;
    int st;

    *flip_out = 0;
    if (!g_protect.active) {
        /* REV-045: protection off — no disarm/re-arm from stray writes. */
        return YGHV_NPF_NONE;
    }
    pp = yghv_protect_find_page_locked(gpa);   /* lock-free read */
    if (pp) {
        if (yghv_protect_is_target_cr3_locked(guest_cr3) || cpl == 0) {
            if (g_protect.config.auto_disarm) {
                st = npt_set_page_perm(&g_npt, pp->gpa,
                    NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
                if (st) {
                    result = YGHV_NPF_DENY;   /* could not reopen: deny */
                } else {
                    pp->armed = 0;
                    *rearm_gpa_out = pp->gpa;
                    *flip_out = 1;
                    result = YGHV_NPF_ALLOW;
                }
            } else {
                result = YGHV_NPF_DENY;       /* deny = inject #PF, stay armed */
            }
        } else {
            result = YGHV_NPF_DENY;           /* foreign write: deny */
        }
    }
    return result;
}

/* 9.249: re-arm (flip back to read-only) for the vendored handler's #DB path.
 * Same discipline as above: no locks. gpa comes from the vcpu's rearm slot. */
int yghv_protect_arm_page_bare(uint64_t gpa) {
    yghv_protect_page_t *pp;
    int st;

    if (!g_protect.active)
        return 0;
    pp = yghv_protect_find_page_locked(gpa);
    if (!pp || pp->armed)
        return 0;
    st = npt_split_2mb_to_4kb(&g_npt, pp->gpa);
    if (st)
        return st;
    st = npt_set_page_perm(&g_npt, pp->gpa, NPT_PERM_PRESENT);
    if (!st)
        pp->armed = 1;
    return st;
}

yghv_npf_result_t yghv_protect_on_npf_write(svm_vcpu_t *vcpu, uint64_t gpa) {
    yghv_protect_page_t *pp;
    yghv_npf_result_t result = YGHV_NPF_NONE;
    int st;

    ExAcquireFastMutex(&g_protect_lock);
    if (!g_protect.active) {
        /* REV-045: protection is off — do not disarm/re-arm pages from an
           NPF write (a page left armed after a failed stop must not be
           permanently disarmed by one write). */
        ExReleaseFastMutex(&g_protect_lock);
        return YGHV_NPF_NONE;
    }
    pp = yghv_protect_find_page_locked(gpa);
    if (pp) {
        if (yghv_protect_is_target_cr3_locked(vcpu->vmcb->state.cr3) ||
            vcpu->vmcb->state.cpl == 0) {
            if (g_protect.config.auto_disarm) {
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
    if (pid) *pid = g_protect.targets[0].pid;
    if (page_count) *page_count = g_protect.targets[0].page_count;
    ExReleaseFastMutex(&g_protect_lock);
}

void yghv_protect_get_heartbeat(uint64_t *page_va, uint64_t *hook_va) {
    ExAcquireFastMutex(&g_protect_lock);
    if (page_va)
        *page_va = g_protect.targets[0].page_count ? g_protect.targets[0].pages[0].target_va : 0;
    if (hook_va)
        *hook_va = g_protect_hooks[0].installed ? g_protect_hooks[0].func_va : 0;
    ExReleaseFastMutex(&g_protect_lock);
}

void yghv_protect_get_target(ULONG *active, ULONG *pid, ULONG_PTR *cr3,
    ULONG *page_count, ULONG *hook_count) {
    ULONG i;
    ExAcquireFastMutex(&g_protect_lock);
    if (active) *active = g_protect.active ? 1 : 0;
    if (pid) *pid = g_protect.targets[0].pid;
    if (cr3) *cr3 = g_protect.targets[0].cr3;
    if (page_count) *page_count = g_protect.targets[0].page_count;
    if (hook_count) {
        ULONG n = 0;
        for (i = 0; i < YGHV_PROTECT_MAX_HOOKS; i++) {
            if (g_protect_hooks[i].installed)
                n++;
        }
        *hook_count = n;
    }
    ExReleaseFastMutex(&g_protect_lock);
}

void yghv_protect_get_pages_info(yghv_protect_pages_info_t *info) {
    ULONG t, i, n = 0;
    ULONG cap = info->count;
    ExAcquireFastMutex(&g_protect_lock);
    for (t = 0; t < g_protect.target_count; t++) {
        for (i = 0; i < g_protect.targets[t].page_count && n < cap; i++) {
            info->pages[n].gpa = g_protect.targets[t].pages[i].gpa;
            info->pages[n].target_va = g_protect.targets[t].pages[i].target_va;
            info->pages[n].flags = g_protect.targets[t].pages[i].flags;
            info->pages[n].armed = g_protect.targets[t].pages[i].armed;
            RtlZeroMemory(info->pages[n].reserved,
                sizeof(info->pages[n].reserved));
            n++;
        }
    }
    /* returned = number of entries actually written (≤ cap); the previous
       uncapped cross-target total could exceed the 64-entry buffer and cause
       a userland over-read. */
    info->returned = n;
    ExReleaseFastMutex(&g_protect_lock);
}

void yghv_protect_get_targets_info(yghv_protect_targets_info_t *info) {
    ULONG i, n = 0;
    ULONG cap = info->count;
    ULONG total = 0;
    ExAcquireFastMutex(&g_protect_lock);
    for (i = 0; i < g_protect.target_count; i++) {
        yghv_protect_target_t *t = &g_protect.targets[i];
        total++;
        if (n < cap) {
            info->targets[n].pid = t->pid;
            info->targets[n].cr3 = t->cr3;
            info->targets[n].page_count = t->page_count;
            info->targets[n].active = (t->cr3 != 0) ? 1 : 0;
            info->targets[n].hook_count = 0;
            n++;
        }
    }
    info->returned = total;
    ExReleaseFastMutex(&g_protect_lock);
}

void yghv_protect_get_hooks_info(yghv_protect_hooks_info_t *info) {
    ULONG i;
    ULONG n;
    ExAcquireFastMutex(&g_protect_lock);
    n = 0;
    for (i = 0; i < YGHV_PROTECT_MAX_HOOKS; i++) {
        if (!g_protect_hooks[i].installed)
            continue;
        if (n < info->count) {
            info->hooks[n].func_va = g_protect_hooks[i].func_va;
            info->hooks[n].hook_id = g_protect_hooks[i].hook_id;
            info->hooks[n].installed = 1;
            info->hooks[n].patch_len = g_protect_hooks[i].patch_len;
            info->hooks[n].reserved = 0;
        }
        n++;
    }
    info->returned = n;
    ExReleaseFastMutex(&g_protect_lock);
}

NTSTATUS yghv_protect_clear(void) {
    uint32_t i, t;
    ExAcquireFastMutex(&g_protect_lock);
    if (g_protect.active)
        yghv_protect_stop_locked();
    for (i = 0; i < YGHV_PROTECT_MAX_HOOKS; i++) {
        if (g_protect_hooks[i].installed)
            yghv_protect_remove_hook_locked(i);
    }
    for (t = 0; t < g_protect.target_count; t++) {
        yghv_protect_target_t *target = &g_protect.targets[t];
        target->page_count = 0;
        if (target->process) {
            ObDereferenceObject(target->process);
            target->process = NULL;
        }
        target->pid = 0;
        target->cr3 = 0;
    }
    g_protect.target_count = 1;
    g_protect.active = FALSE;
    yghv_protect_refresh_cr3_list_locked();
    LOG_ERROR("protect clear: target/pages/hooks cleared");
    ExReleaseFastMutex(&g_protect_lock);
    yghv_hook_diag_flush();
    return STATUS_SUCCESS;
}

NTSTATUS yghv_protect_set_config(const yghv_protect_config_t *cfg) {
    if (!cfg)
        return STATUS_INVALID_PARAMETER;
    if (cfg->auto_disarm > 1 || cfg->deny_status == 0)
        return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&g_protect_lock);
    /* Strict deny (auto_disarm=0) makes every write to an armed page inject
       #PF. Persistent workload guests have no #PF handler, so reject the
       combination while protection is active. */
    if (cfg->auto_disarm == 0 && g_protect.active) {
        ExReleaseFastMutex(&g_protect_lock);
        return STATUS_ACCESS_DENIED;
    }
    g_protect.config.auto_disarm = cfg->auto_disarm;
    g_protect.config.deny_status = cfg->deny_status;
    LOG_ERROR("protect config: auto_disarm=%u deny_status=0x%x",
        g_protect.config.auto_disarm, g_protect.config.deny_status);
    ExReleaseFastMutex(&g_protect_lock);
    return STATUS_SUCCESS;
}

void yghv_protect_get_config(yghv_protect_config_t *out) {
    ExAcquireFastMutex(&g_protect_lock);
    *out = g_protect.config;
    ExReleaseFastMutex(&g_protect_lock);
}

BOOLEAN yghv_protect_check_target_exited(void) {
    PEPROCESS procs[YGHV_PROTECT_MAX_TARGETS];
    uint32_t pids[YGHV_PROTECT_MAX_TARGETS];
    ULONG n = 0;
    ULONG i;
    LARGE_INTEGER timeout;
    NTSTATUS st;
    BOOLEAN handled = FALSE;

    ExAcquireFastMutex(&g_protect_lock);
    for (i = 0; i < g_protect.target_count && n < YGHV_PROTECT_MAX_TARGETS;
         i++) {
        if (g_protect.targets[i].process) {
            procs[n] = g_protect.targets[i].process;
            pids[n] = g_protect.targets[i].pid;
            /* Hold our own reference so the object stays alive across the
               poll; otherwise a concurrent on_target_exit/clear can free it
               between the snapshot and KeWaitForSingleObject (UAF). */
            ObReferenceObject(procs[n]);
            n++;
        }
    }
    ExReleaseFastMutex(&g_protect_lock);
    if (!n)
        return FALSE;
    for (i = 0; i < n; i++) {
        timeout.QuadPart = 0;
        st = KeWaitForSingleObject(procs[i], Executive, KernelMode, FALSE,
                                   &timeout);
        if (st == STATUS_SUCCESS) {
            BOOLEAN match = FALSE;
            /* Re-check under the lock that this slot still owns this pid and
               this process before clearing, so a pid-reuse + retarget between
               snapshot and poll cannot clear the new target's slot. */
            ExAcquireFastMutex(&g_protect_lock);
            {
                yghv_protect_target_t *t =
                    yghv_protect_find_target_by_pid_locked(pids[i]);
                if (t && t->process == procs[i])
                    match = TRUE;
            }
            ExReleaseFastMutex(&g_protect_lock);
            if (match && yghv_protect_on_target_exit(pids[i]))
                handled = TRUE;
        }
        ObDereferenceObject(procs[i]);
    }
    return handled;
}

BOOLEAN yghv_protect_on_target_exit(ULONG pid) {
    BOOLEAN handled = FALSE;
    uint32_t i;
    yghv_protect_target_t *t;

    ExAcquireFastMutex(&g_protect_lock);
    t = yghv_protect_find_target_by_pid_locked(pid);
    if (t) {
        for (i = 0; i < t->page_count; i++) {
            if (t->pages[i].armed) {
                int ds = yghv_protect_disarm_page_locked(&t->pages[i]);
                if (ds)
                    LOG_ERROR("protect target exit: disarm page %u failed 0x%x",
                        i, ds);
            }
        }
        t->page_count = 0;
        if (t->process) {
            ObDereferenceObject(t->process);
            t->process = NULL;
        }
        t->pid = 0;
        t->cr3 = 0;
        yghv_protect_refresh_cr3_list_locked();
        handled = TRUE;
        LOG_ERROR("protect target exited: auto disarm, pid cleared");
    }
    ExReleaseFastMutex(&g_protect_lock);
    return handled;
}

static uint8_t *g_hook_stub_pages[YGHV_PROTECT_MAX_HOOKS];

static uint8_t *yghv_protect_map_writable_page(uint64_t page_va, PMDL *out_mdl) {
    PMDL mdl;
    PVOID map;

    *out_mdl = NULL;
    mdl = IoAllocateMdl((PVOID)page_va, HV_PAGE_SIZE, FALSE, FALSE, NULL);
    if (!mdl)
        return NULL;
    MmBuildMdlForNonPagedPool(mdl);
    map = MmMapLockedPagesSpecifyCache(mdl, KernelMode, MmCached, NULL,
        HighPagePriority, NormalPagePriority);
    if (!map) {
        IoFreeMdl(mdl);
        return NULL;
    }
    *out_mdl = mdl;
    return (uint8_t *)map;
}

static void yghv_protect_unmap_writable_page(PMDL mdl, uint8_t *map) {
    if (mdl && map)
        MmUnmapLockedPages(map, mdl);
    if (mdl)
        IoFreeMdl(mdl);
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
static void yghv_emit_abs_jmp(uint8_t *p, size_t len, uint64_t target) {
    p[0] = 0x48; p[1] = 0xB8;                  /* movabs rax, imm64 */
    yghv_emit_u64(p + 2, target);
    p[10] = 0xFF; p[11] = 0xE0;                /* jmp rax */
    for (size_t i = 12; i < len; i++)
        p[i] = 0x90;
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
static int yghv_inst_len(const uint8_t *p, size_t avail, int *rip_rel) {
    size_t pos = 0;
    int rex = 0;
    int prefixes = 0;
    uint8_t b, op;

    if (rip_rel)
        *rip_rel = 0;

    for (;;) {
        if (pos >= avail) return 0;
        b = p[pos];
        if (b >= 0x40 && b <= 0x4F) {
            if (rex) return 0;
            rex = b;   /* keep the REX byte; bit 3 = W (operand size) */
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

    /* REV-048: DAA/DAS (0x27/0x2F) and far call/jmp (0x9A/0xEA) are invalid in
       x86-64; reject them instead of falling into the generic ModRM branch and
       mis-decoding. */
    if (op == 0x27 || op == 0x2F || op == 0x9A || op == 0xEA)
        return 0;

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
        if (rip_rel) *rip_rel = 1;
        return (int)(pos + 1);
    }
    if (op == 0xE8 || op == 0xE9) {
        if (pos + 4 > avail) return 0;
        if (rip_rel) *rip_rel = 1;
        return (int)(pos + 4);
    }
    if (op >= 0xB0 && op <= 0xB7) {
        if (pos + 1 > avail) return 0;
        return (int)(pos + 1);
    }
    if (op >= 0xB8 && op <= 0xBF) {
        /* B8+rd: REX.W -> movabs r64, imm64 (8-byte imm); no REX.W -> mov
           r32, imm32 (4-byte imm).  Every Windows syscall stub begins with
           the no-REX `mov eax, imm32` form, so sizing it as imm64 was a
           wrong-boundary bug. */
        size_t imm = (rex & 0x08) ? 8 : 4;
        if (pos + imm > avail) return 0;
        return (int)(pos + imm);
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
            if (rip_rel) *rip_rel = 1;
            return (int)(pos + 4);
        }
        if (op2 == 0x05 || op2 == 0x07 || op2 == 0x08 || op2 == 0x09 ||
            op2 == 0x0B || op2 == 0x0D || op2 == 0x34 || op2 == 0x35 ||
            op2 == 0x77 || op2 == 0xA2 || (op2 >= 0xC8 && op2 <= 0xCF)) {
            return (int)pos;
        }
        if (op2 == 0x38 || op2 == 0x3A) {
            size_t after;
            uint8_t op3;
            if (pos >= avail) return 0;
            op3 = p[pos];
            pos++;
            (void)op3;
            /* `after` must point at the ModRM byte (after op3); previously it
               was captured before pos++ and decode_modrm started on the op3
               opcode itself (off-by-one) and RIP-relative detection probed the
               wrong byte. */
            after = pos;
            if (rip_rel && after < avail && (p[after] & 0xC7) == 0x05)
                *rip_rel = 1;
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
            if (rip_rel && after < avail && (p[after] & 0xC7) == 0x05)
                *rip_rel = 1;
            if (yghv_decode_modrm(p, avail, &after) < 0) return 0;
            if (after + 1 > avail) return 0;
            return (int)(after + 1);
        }
        {
            size_t after = pos;
            if (rip_rel && after < avail && (p[after] & 0xC7) == 0x05)
                *rip_rel = 1;
            if (yghv_decode_modrm(p, avail, &after) < 0) return 0;
            return (int)after;
        }
    }

    if (op == 0x69 || op == 0x6B || op == 0x80 || op == 0x81 || op == 0x83 ||
        op == 0xC0 || op == 0xC1 || op == 0xC6 || op == 0xC7) {
        size_t after = pos;
        size_t imm = (op == 0x69 || op == 0x81 || op == 0xC7)
                         ? 4 : 1;
        if (rip_rel && after < avail && (p[after] & 0xC7) == 0x05)
            *rip_rel = 1;
        if (yghv_decode_modrm(p, avail, &after) < 0) return 0;
        if (after + imm > avail) return 0;
        return (int)(after + imm);
    }

    if (op == 0xF6 || op == 0xF7) {
        size_t after = pos;
        size_t imm = 0;
        /* Group-3: only /0 (test) has an immediate; not/neg/mul/imul/div/idiv
           (F6/F7 /2../7) do not. */
        if (after < avail && ((p[after] >> 3) & 7) == 0)
            imm = (op == 0xF7) ? 4 : 1;
        if (rip_rel && after < avail && (p[after] & 0xC7) == 0x05)
            *rip_rel = 1;
        if (yghv_decode_modrm(p, avail, &after) < 0) return 0;
        if (after + imm > avail) return 0;
        return (int)(after + imm);
    }

    {
        size_t after = pos;
        if (rip_rel && after < avail && (p[after] & 0xC7) == 0x05)
            *rip_rel = 1;
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
    avail = HV_PAGE_SIZE - (func_va & (HV_PAGE_SIZE - 1));
    if (avail < YGHV_PROTECT_PATCH_MIN)
        return -1;
    p = (const uint8_t *)func_va;
    while (off < YGHV_PROTECT_PATCH_LEN) {
        int rip_rel = 0;
        int len = yghv_inst_len(p + off, avail - off, &rip_rel);
        if (len <= 0)
            return -1;
        if (rip_rel)
            return -1;
        off += (size_t)len;
        if (off >= YGHV_PROTECT_PATCH_MIN &&
            off <= YGHV_PROTECT_PATCH_LEN)
            return (int)off;
        if (off > YGHV_PROTECT_PATCH_LEN)
            return -1;
    }
    return -1;
}

NTSTATUS yghv_protect_install_hook(uint8_t hook_id, uint64_t func_va) {
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_install_hook_locked(hook_id, func_va);
    ExReleaseFastMutex(&g_protect_lock);
    yghv_hook_diag_flush();
    return st;
}

static NTSTATUS yghv_protect_install_hook_locked(uint8_t hook_id, uint64_t func_va) {
    uint8_t *stub;
    uint8_t patch[YGHV_PROTECT_PATCH_LEN];
    uint64_t func_pa, page_va, page_pa;
    uint8_t *orig;
    uint8_t *wmap = NULL;
    PMDL wmdl = NULL;
    yghv_protect_page_t *pp;
    yghv_protect_hook_t *h;
    int patch_len;
    NTSTATUS st = STATUS_UNSUCCESSFUL;

    if (hook_id >= YGHV_PROTECT_MAX_HOOKS) return STATUS_INVALID_PARAMETER;
    h = &g_protect_hooks[hook_id];
    if (h->installed) return STATUS_ALREADY_COMMITTED;
    patch_len = yghv_protect_validate_hook_target(func_va);
    if (patch_len < 0) {
        LOG_ERROR("protect hook %u: invalid target va=0x%llx", hook_id, func_va);
        return STATUS_INVALID_PARAMETER;
    }

    func_pa = MmGetPhysicalAddress((PVOID)func_va).QuadPart;
    page_va = func_va & ~(HV_PAGE_SIZE - 1);
    page_pa = MmGetPhysicalAddress((PVOID)page_va).QuadPart;
    h->patch_len = (uint8_t)patch_len;
    RtlCopyMemory(h->original, (void *)func_va, h->patch_len);
    h->func_va = func_va;
    h->func_pa = func_pa;
    h->hook_id = hook_id;

    stub = (uint8_t *)ExAllocatePoolWithTag(NonPagedPool, HV_PAGE_SIZE,
        YGHV_TAG);
    if (!stub) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(stub, HV_PAGE_SIZE);
    g_hook_stub_pages[hook_id] = stub;

    /* entry: push rax/rcx/rbx; compare current CR3 against every target
       slot's CR3 (unrolled), then jump to the original slot. Native callers
       are not in guest mode, so the decision must not depend on VMMCALL. */
    uint8_t *p = stub;
    int off = 0;
    int i;
    int je_pos[YGHV_PROTECT_MAX_TARGETS];
    p[off++]=0x50; p[off++]=0x51; p[off++]=0x53;   /* push rax,rcx,rbx */
    p[off++]=0x48; p[off++]=0x0F; p[off++]=0x20; p[off++]=0xD8; /* mov rax,cr3 */
    p[off++]=0x48; p[off++]=0x25;                  /* and rax,~0xFFF */
    p[off++]=0x00; p[off++]=0xF0; p[off++]=0xFF; p[off++]=0xFF;
    p[off++]=0x48; p[off++]=0xBB;                  /* movabs rbx,&targets */
    yghv_emit_u64(p+off, (uint64_t)&g_protect.targets[0]);
    off += 8;
    for (i = 0; i < YGHV_PROTECT_MAX_TARGETS; i++) {
        uint32_t disp = (uint32_t)(i * sizeof(yghv_protect_target_t) +
            offsetof(yghv_protect_target_t, cr3));
        p[off++]=0x48; p[off++]=0x8B; p[off++]=0x8B; /* mov rcx,[rbx+disp32] */
        p[off++]=(uint8_t)disp; p[off++]=(uint8_t)(disp >> 8);
        p[off++]=(uint8_t)(disp >> 16); p[off++]=(uint8_t)(disp >> 24);
        p[off++]=0x48; p[off++]=0x81; p[off++]=0xE1;
        p[off++]=0x00; p[off++]=0xF0; p[off++]=0xFF; p[off++]=0xFF;
        p[off++]=0x48; p[off++]=0x39; p[off++]=0xC1; /* cmp rax,rcx */
        je_pos[i] = off;
        p[off++]=0x74;                               /* je allow */
        p[off++]=0;
    }
    /* deny fall-through: pop and return deny_status */
    p[off++]=0x5B; p[off++]=0x59; p[off++]=0x58;
    p[off++]=0x48; p[off++]=0xB8;
    yghv_emit_u64(p+off, (uint64_t)&g_protect.config.deny_status);
    off += 8;
    p[off++]=0x8B; p[off++]=0x00;                   /* mov eax,[rax] */
    p[off++]=0xC3;                                  /* ret */
    while (off < 0x80)
        p[off++] = 0x90;
    for (i = 0; i < YGHV_PROTECT_MAX_TARGETS; i++) {
        int disp = 0x80 - (je_pos[i] + 2);
        p[je_pos[i] + 1] = (uint8_t)disp;
    }
    /* allow: pop and jump to original slot */
    p[off++]=0x5B; p[off++]=0x59; p[off++]=0x58;
    yghv_emit_rel32(p + off, (uint64_t)(p + off), (uint64_t)(stub + 0x90));
    off += 5;

    /* original slot at offset 0x90 */
    orig = stub + 0x90;
    RtlCopyMemory(orig, h->original, h->patch_len);
    /* jump back to func_va + patch_len after the copied original bytes */
    orig[h->patch_len] = 0x49; orig[h->patch_len+1] = 0xBB;
    yghv_emit_u64(orig + h->patch_len + 2, func_va + h->patch_len);
    orig[h->patch_len+10] = 0x41; orig[h->patch_len+11] = 0xFF;
    orig[h->patch_len+12] = 0xE3;                   /* jmp r11 */

    /* patch function entry: variable-length absolute jump -> stub */
    yghv_emit_abs_jmp(patch, h->patch_len, (uint64_t)stub);
    wmap = yghv_protect_map_writable_page(page_va, &wmdl);
    if (!wmap) {
        LOG_ERROR("protect hook %u: writable map of function page 0x%llx failed",
            hook_id, page_pa);
        yghv_hook_diag_mark("install:map", st);
        st = STATUS_UNSUCCESSFUL;
        goto fail;
    }
    st = svm_core_pause_residents_for_patch();
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect hook %u: patch rendezvous failed 0x%x", hook_id, st);
        yghv_hook_diag_mark("install:pause", st);
        goto fail;
    }
    RtlCopyMemory(wmap + (func_va & (HV_PAGE_SIZE - 1)), patch,
        h->patch_len);
    KeInvalidateRangeAllCaches((PVOID)func_va, h->patch_len);
    svm_core_resume_residents();

    /* write-protect the function's page in NPT */
    st = npt_split_2mb_to_4kb(&g_npt, page_pa);
    if (st) {
        LOG_ERROR("protect hook %u: split function page failed 0x%x",
            hook_id, st);
        yghv_hook_diag_mark("install:split", st);
        goto fail;
    }
    st = npt_set_page_perm(&g_npt, page_pa, NPT_PERM_PRESENT);
    if (st) {
        LOG_ERROR("protect hook %u: set function page perm failed 0x%x",
            hook_id, st);
        yghv_hook_diag_mark("install:perm", st);
        goto fail;
    }

    /* route writes through the Task 4 NPF policy and arm the page */
    st = yghv_protect_add_page_for_locked(&g_protect.targets[0], h->func_va);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect hook %u: add function page failed 0x%x",
            hook_id, st);
        yghv_hook_diag_mark("install:addpage", st);
        goto fail;
    }
    pp = yghv_protect_find_page_locked(page_pa);
    if (!pp) {
        LOG_ERROR("protect hook %u: function page missing from table",
            hook_id);
        yghv_hook_diag_mark("install:missing", st);
        yghv_protect_remove_page_for_locked(&g_protect.targets[0], h->func_va);
        st = STATUS_UNSUCCESSFUL;
        goto fail;
    }
    st = yghv_protect_arm_page_locked(pp);
    if (st) {
        LOG_ERROR("protect hook %u: arm function page failed 0x%x",
            hook_id, st);
        yghv_hook_diag_mark("install:arm", st);
        yghv_protect_remove_page_for_locked(&g_protect.targets[0], h->func_va);
        goto fail;
    }

    h->installed = 1;
    LOG_ERROR("protect hook %u installed: va=0x%llx pa=0x%llx len=%u",
        hook_id, func_va, func_pa, h->patch_len);
    return STATUS_SUCCESS;

fail:
    yghv_hook_diag_mark("install:fail", st);
    npt_set_page_perm(&g_npt, page_pa, NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    for (i = 0; i < SVM_MAX_CORES; i++) {
        if (g_vcpus[i])
            g_vcpus[i]->npt_flush_pending = 1;
    }
    if (wmap) {
        /* REV-040/041: the patch may already be applied with residents resumed;
           re-pause before restoring the original bytes and freeing the stub so
           the guest cannot execute a torn instruction or a freed stub. */
        NTSTATUS pst = svm_core_pause_residents_for_patch();
        RtlCopyMemory(wmap + (func_va & (HV_PAGE_SIZE - 1)), h->original,
            h->patch_len);
        KeInvalidateRangeAllCaches((PVOID)func_va, h->patch_len);
        if (NT_SUCCESS(pst))
            svm_core_resume_residents();
    }
    yghv_protect_unmap_writable_page(wmdl, wmap);
    if (g_hook_stub_pages[hook_id]) {
        ExFreePoolWithTag(g_hook_stub_pages[hook_id], YGHV_TAG);
        g_hook_stub_pages[hook_id] = NULL;
    }
    h->patch_len = 0;
    return st;
}

NTSTATUS yghv_protect_remove_hook(uint8_t hook_id) {
    NTSTATUS st;
    ExAcquireFastMutex(&g_protect_lock);
    st = yghv_protect_remove_hook_locked(hook_id);
    ExReleaseFastMutex(&g_protect_lock);
    yghv_hook_diag_flush();
    return st;
}

static NTSTATUS yghv_protect_remove_hook_locked(uint8_t hook_id) {
    yghv_protect_hook_t *h;
    uint8_t *wmap = NULL;
    PMDL wmdl = NULL;
    uint64_t page_pa;
    NTSTATUS st = STATUS_UNSUCCESSFUL;
    if (hook_id >= YGHV_PROTECT_MAX_HOOKS) return STATUS_INVALID_PARAMETER;
    h = &g_protect_hooks[hook_id];
    if (!h->installed) return STATUS_NOT_FOUND;

    page_pa = MmGetPhysicalAddress(
        (PVOID)(h->func_va & ~(HV_PAGE_SIZE - 1))).QuadPart;
    wmap = yghv_protect_map_writable_page(
        h->func_va & ~(HV_PAGE_SIZE - 1), &wmdl);
    if (!wmap) {
        LOG_ERROR(
            "protect remove hook %u: writable map of function page 0x%llx failed",
            hook_id, page_pa);
        yghv_hook_diag_mark("remove:map", st);
        return STATUS_UNSUCCESSFUL;
    }

    /* remove the page from the NPF policy and restore NPT writable first */
    st = yghv_protect_remove_page_for_locked(&g_protect.targets[0], h->func_va);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect remove hook %u: remove_page failed 0x%x",
            hook_id, st);
        yghv_hook_diag_mark("remove:removepage", st);
        yghv_protect_unmap_writable_page(wmdl, wmap);
        return st;
    }

    st = svm_core_pause_residents_for_patch();
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect remove hook %u: patch rendezvous failed 0x%x",
            hook_id, st);
        yghv_hook_diag_mark("remove:pause", st);
        /* REV-038: still restore the original bytes and clear the hook so a
           transient pause failure cannot strand the function permanently
           patched to a stub.  pause_residents_for_patch already resumed
           residents on timeout; restoring under the un-paused guest is
           best-effort (the REV-001 lock/barrier redesign removes this race). */
        RtlCopyMemory(wmap + (h->func_va & (HV_PAGE_SIZE - 1)), h->original,
            h->patch_len);
        KeInvalidateRangeAllCaches((PVOID)h->func_va, h->patch_len);
        yghv_protect_unmap_writable_page(wmdl, wmap);
        if (g_hook_stub_pages[hook_id]) {
            ExFreePoolWithTag(g_hook_stub_pages[hook_id], YGHV_TAG);
            g_hook_stub_pages[hook_id] = NULL;
        }
        h->installed = 0;
        h->patch_len = 0;
        return st;
    }
    RtlCopyMemory(wmap + (h->func_va & (HV_PAGE_SIZE - 1)), h->original,
        h->patch_len);
    KeInvalidateRangeAllCaches((PVOID)h->func_va, h->patch_len);
    svm_core_resume_residents();
    yghv_protect_unmap_writable_page(wmdl, wmap);
    if (g_hook_stub_pages[hook_id]) {
        ExFreePoolWithTag(g_hook_stub_pages[hook_id], YGHV_TAG);
        g_hook_stub_pages[hook_id] = NULL;
    }
    h->installed = 0;
    h->patch_len = 0;
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

/* 9.152: expose the generated stub page VA so main.c can map it into the
   dedicated guest CR3 (the guest calls the hooked dummy -> jumps to the stub). */
uint64_t yghv_protect_get_hook_stub_va(uint8_t hook_id) {
    if (hook_id >= YGHV_PROTECT_MAX_HOOKS)
        return 0;
    return (uint64_t)g_hook_stub_pages[hook_id];
}
