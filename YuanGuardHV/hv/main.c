#include <ntddk.h>
#include <ntstrsafe.h>
#include "svm_defs.h"
#include "svm_vcpu.h"
#include "npt.h"
#include "control_plane.h"
#include "control_device.h"
#include "protect.h"
#include "debug.h"

#define YGHV_R1_SKIP_NPT_TEST 0
#define YGHV_R1_NPT_UNIT_TEST 1
#define YGHV_RESIDENT_WORKLOAD_TEST 1
#ifndef YGHV_BAREMETAL_NO_RESIDENT
#define YGHV_BAREMETAL_NO_RESIDENT 0
#endif
#ifndef YGHV_BAREMETAL_STEP
#define YGHV_BAREMETAL_STEP 0
#endif

NTKERNELAPI NTSTATUS ZwFlushBuffersFile(HANDLE FileHandle,
                                        PIO_STATUS_BLOCK IoStatusBlock);

npt_mgr_t g_npt;
uint64_t g_npt_test_pa;
volatile int g_npt_test_active;
volatile BOOLEAN g_persistent_mode = FALSE;
void *g_guest_code_page = NULL;
void *g_resident_workload_page = NULL;
HANDLE g_hook_rendezvous_thread = NULL;
uint64_t g_guest_hb_va = 0;
uint64_t g_guest_npt_va = 0;
HANDLE g_trace_file = NULL;
volatile LONG g_os_guest_test_active = 0;
volatile LONG g_os_guest_counter = 0;
volatile ULONG64 g_os_guest_cpuid_acc = 0;
volatile BOOLEAN g_os_resident_mode = FALSE;
volatile BOOLEAN g_os_guest_intr_intercept = FALSE;
volatile BOOLEAN g_os_guest_host_isr = FALSE;
volatile BOOLEAN g_os_guest_inject_intr = FALSE;
volatile BOOLEAN g_os_guest_avic_irr_inject = FALSE;
volatile BOOLEAN g_os_guest_avic_eoi_only = FALSE;
volatile BOOLEAN g_os_guest_avic_timer_emu = FALSE;
void *g_avic_eoi_apic_va = NULL;
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} yghv_idtr_desc_t;
yghv_idtr_desc_t g_avic_eoi_idtr;
yghv_idtr_desc_t g_avic_old_idtr;
volatile BOOLEAN g_os_guest_delay_quiet = FALSE;
volatile BOOLEAN g_v96_apic_tpr_stress = FALSE;
void *g_v96_apic_tpr_va = NULL;
volatile BOOLEAN g_v97_hlt_intercept = FALSE;
volatile BOOLEAN g_v98_apic_shadow = FALSE;
void *g_v98_apic_shadow_va = NULL;
uint64_t g_v98_apic_shadow_pa = 0;
void *g_v98_real_apic_va = NULL;
volatile ULONG g_v98_last_tpr = 0;
volatile ULONG g_v98_last_icrl = 0;
volatile ULONG g_v98_last_icrh = 0;
volatile ULONG g_v98_last_lvtt = 0;
volatile ULONG g_v98_last_tmict = 0;
volatile ULONG g_v98_last_tdcr = 0;
static volatile LONG g_v99_allcore_ready = 0;
static volatile LONG g_v99_allcore_go = 0;
static volatile LONG g_v99_allcore_abort = 0;
static volatile LONG g_v99_allcore_online = 0;
volatile BOOLEAN g_os_resident_log_active = FALSE;
volatile ULONG64 g_os_resident_exits = 0;
volatile BOOLEAN g_v100_monitor_active = FALSE;
volatile BOOLEAN g_v100_guest_entered = FALSE;
volatile BOOLEAN g_v101_gp_intercept = FALSE;
volatile BOOLEAN g_v101_gp_seen = FALSE;
volatile uint64_t g_v101_gp_exitcode = 0;
volatile uint64_t g_v101_gp_err = 0;
volatile uint64_t g_v101_gp_rip = 0;
volatile uint64_t g_v101_gp_rsp = 0;
volatile uint64_t g_v101_gp_cr3 = 0;
volatile uint64_t g_v101_gp_gs_base = 0;
volatile BOOLEAN g_v102_catchall = FALSE;
static KEVENT g_os_guest_done_events[SVM_MAX_CORES];
static KEVENT g_os_resident_stop_event;

extern const uint8_t svm_trampoline_test_guest[];
extern const uint8_t svm_trampoline_test_guest_resume[];
extern const uint8_t svm_trampoline_test_guest_end[];
extern const uint8_t svm_os_seamless_cont[];
extern const uint8_t svm_trampoline_test_npt_guest[];
extern const uint8_t svm_trampoline_test_npt_guest_resume[];
extern const uint8_t svm_trampoline_test_npt_guest_end[];
extern const uint8_t svm_trampoline_test_prot_write[];
extern const uint8_t svm_trampoline_test_prot_write_resume[];
extern const uint8_t svm_trampoline_test_prot_write_end[];
extern const uint8_t svm_trampoline_test_hook_guest[];
extern const uint8_t svm_trampoline_test_hook_guest_end[];
extern const uint8_t svm_trampoline_test_min_guest[];
extern const uint8_t svm_trampoline_test_min_guest_end[];
extern const uint8_t svm_trampoline_test_cpuid_guest[];
extern const uint8_t svm_trampoline_test_cpuid_guest_end[];
extern const uint8_t svm_trampoline_test_resident_guest[];
extern const uint8_t svm_trampoline_test_resident_guest_end[];
extern const uint8_t yghv_avic_eoi_isr[];

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;

static void yghv_trace_init(void) {
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    NTSTATUS st;

    RtlInitUnicodeString(&name, L"\\SystemRoot\\yghv_progress.log");
    InitializeObjectAttributes(&oa, &name,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    st = ZwCreateFile(&g_trace_file, GENERIC_WRITE, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OVERWRITE_IF,
        FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(st))
        LOG_ERROR("yghv_trace_init failed 0x%x", st);
}

void yghv_trace(const char *msg) {
    IO_STATUS_BLOCK iosb;
    size_t msg_len = 0;
    char buf[256];

    if (!g_trace_file) return;
    if (!NT_SUCCESS(RtlStringCchLengthA(msg, sizeof(buf) - 2, &msg_len))) return;
    RtlCopyMemory(buf, msg, msg_len);
    buf[msg_len] = '\r';
    buf[msg_len + 1] = '\n';
    ZwWriteFile(g_trace_file, NULL, NULL, NULL, &iosb, buf, (ULONG)(msg_len + 2), NULL, NULL);
    /* Write-through: a hard freeze must leave the last milestone on disk. */
    ZwFlushBuffersFile(g_trace_file, &iosb);
}

void yghv_trace_u64(const char *label, uint64_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[64];
    size_t n = 0;
    int i;
    while (label[n] && n < sizeof(buf) - 1) {
        buf[n] = label[n];
        n++;
    }
    buf[n++] = '=';
    buf[n++] = '0';
    buf[n++] = 'x';
    for (i = 15; i >= 0; i--)
        buf[n++] = hex[(v >> (i * 4)) & 0xF];
    buf[n] = 0;
    yghv_trace(buf);
}

static void yghv_trace_close(void) {
    if (g_trace_file) {
        ZwClose(g_trace_file);
        g_trace_file = NULL;
    }
}

static void yghv_patch_rel_jump(uint8_t *code, size_t resume_off, size_t start_off) {
    uint8_t *p = code + resume_off;
    if (p[0] == 0xE9) {
        int32_t disp = (int32_t)((code + start_off) - (p + 5));
        *(int32_t *)(p + 1) = disp;
    } else if (p[0] == 0xEB) {
        char disp = (char)((code + start_off) - (p + 2));
        *(char *)(p + 1) = disp;
    } else {
        LOG_ERROR("yghv_patch_rel_jump: unexpected opcode 0x%x", p[0]);
    }
}

static NTSTATUS yghv_prepare_guest_code(void) {
    uint8_t *page;
    size_t hb_size, npt_size;
    uint8_t *npt_dst;

    page = (uint8_t *)MmAllocateContiguousMemory(
        HV_PAGE_SIZE, (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!page) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(page, HV_PAGE_SIZE);

    hb_size = (size_t)(svm_trampoline_test_guest_end - svm_trampoline_test_guest);
    npt_size = (size_t)(svm_trampoline_test_npt_guest_end - svm_trampoline_test_npt_guest);

    RtlCopyMemory(page, svm_trampoline_test_guest, hb_size);
    yghv_patch_rel_jump(page,
        (size_t)(svm_trampoline_test_guest_resume - svm_trampoline_test_guest), 0);

    npt_dst = page + 0x100;
    RtlCopyMemory(npt_dst, svm_trampoline_test_npt_guest, npt_size);
    yghv_patch_rel_jump(npt_dst,
        (size_t)(svm_trampoline_test_npt_guest_resume - svm_trampoline_test_npt_guest), 0);

    g_guest_code_page = page;
    g_guest_hb_va = (uint64_t)page;
    g_guest_npt_va = (uint64_t)npt_dst;
    return STATUS_SUCCESS;
}

static NTSTATUS yghv_npt_map_ram(npt_mgr_t *m) {
    PPHYSICAL_MEMORY_RANGE ranges;
    ULONG i;

    ranges = MmGetPhysicalMemoryRanges();
    if (!ranges) return STATUS_UNSUCCESSFUL;

    for (i = 0; ranges[i].NumberOfBytes.QuadPart != 0; i++) {
        uint64_t base = ranges[i].BaseAddress.QuadPart;
        uint64_t end = base + ranges[i].NumberOfBytes.QuadPart;
        NTSTATUS st = npt_identity_map_range(m, base, end);
        if (st) {
            ExFreePool(ranges);
            return st;
        }
    }
    ExFreePool(ranges);
    return STATUS_SUCCESS;
}

static void yghv_exclude_driver(PDRIVER_OBJECT d) {
    uint64_t start = (uint64_t)d->DriverStart & ~(HV_PAGE_SIZE - 1);
    uint64_t end = ((uint64_t)d->DriverStart + d->DriverSize + HV_PAGE_SIZE - 1) &
                   ~(uint64_t)(HV_PAGE_SIZE - 1);

    while (start < end) {
        PHYSICAL_ADDRESS pa = MmGetPhysicalAddress((PVOID)start);
        npt_exclude_pa(&g_npt, pa.QuadPart);
        start += HV_PAGE_SIZE;
    }
}

static void yghv_exclude_hv_private(PDRIVER_OBJECT d) {
    ULONG i;

    /* VMware nested SVM currently rejects VMRUN when guest NPT no longer
       maps hypervisor-private pages. Keep exclusions disabled until bare-metal
       validation; only the guest code page copy and auth are active. */
    for (i = 0; i < SVM_MAX_CORES; i++) {
        svm_vcpu_t *v = g_vcpus[i];
        if (!v) continue;
        (void)v;
    }
    (void)d;
}

static NTSTATUS yghv_r1_npt_unit_test(void) {
    void *buf = NULL;
    uint64_t test_pa, test_pa2;
    uint64_t entry, trans;
    NTSTATUS st;

    yghv_trace("r1 start");
    buf = ExAllocatePoolWithTag(NonPagedPool, HV_PAGE_SIZE, YGHV_TAG);
    if (!buf) {
        LOG_ERROR("r1 unit: alloc failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(buf, HV_PAGE_SIZE);
    test_pa = MmGetPhysicalAddress(buf).QuadPart;
    test_pa2 = test_pa + HV_LARGE_PAGE_SIZE;
    LOG_ERROR("r1 unit: buf_pa=0x%llx test_pa=0x%llx test_pa2=0x%llx",
        test_pa, test_pa, test_pa2);
    yghv_trace("r1 alloc ok");

    entry = npt_read_entry(&g_npt, test_pa);
    trans = npt_translate(&g_npt, test_pa);
    yghv_trace_u64("r1 test_pa", test_pa);
    yghv_trace_u64("r1 trans", trans);
    LOG_ERROR("r1 unit: large entry=0x%llx trans=0x%llx", entry, trans);
    if (trans != test_pa) {
        LOG_ERROR("r1 unit: large identity FAILED");
        st = STATUS_UNSUCCESSFUL;
        goto done;
    }

    st = npt_split_2mb_to_4kb(&g_npt, test_pa);
    LOG_ERROR("r1 unit: split rc=0x%x", st);
    if (st) goto done;
    entry = npt_read_entry(&g_npt, test_pa);
    trans = npt_translate(&g_npt, test_pa);
    LOG_ERROR("r1 unit: split entry=0x%llx trans=0x%llx", entry, trans);
    if (!(entry & NPT_PERM_PRESENT) || trans != test_pa) {
        LOG_ERROR("r1 unit: split identity FAILED");
        st = STATUS_UNSUCCESSFUL;
        goto done;
    }
    yghv_trace("r1 split ok");

    st = npt_set_page_perm(&g_npt, test_pa,
        NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    entry = npt_read_entry(&g_npt, test_pa);
    trans = npt_translate(&g_npt, test_pa);
    LOG_ERROR("r1 unit: perm rw entry=0x%llx trans=0x%llx rc=0x%x", entry, trans, st);
    if (st || !(entry & NPT_PERM_WRITABLE) || trans != test_pa) {
        LOG_ERROR("r1 unit: perm rw FAILED");
        st = STATUS_UNSUCCESSFUL;
        goto done;
    }

    st = npt_set_page_perm(&g_npt, test_pa,
        NPT_PERM_PRESENT | NPT_PERM_NX);
    entry = npt_read_entry(&g_npt, test_pa);
    trans = npt_translate(&g_npt, test_pa);
    LOG_ERROR("r1 unit: perm nx entry=0x%llx trans=0x%llx rc=0x%x", entry, trans, st);
    if (st || !(entry & NPT_PERM_NX) || trans != test_pa) {
        LOG_ERROR("r1 unit: perm nx FAILED");
        st = STATUS_UNSUCCESSFUL;
        goto done;
    }

    st = npt_set_page_perm(&g_npt, test_pa,
        NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    entry = npt_read_entry(&g_npt, test_pa);
    LOG_ERROR("r1 unit: perm restore entry=0x%llx rc=0x%x", entry, st);
    if (st || (entry & NPT_PERM_NX) || !(entry & NPT_PERM_WRITABLE)) {
        LOG_ERROR("r1 unit: perm restore FAILED");
        st = STATUS_UNSUCCESSFUL;
        goto done;
    }

    st = npt_set_page_perm_range(&g_npt, test_pa2, HV_PAGE_SIZE * 2,
        NPT_PERM_PRESENT | NPT_PERM_NX);
    entry = npt_read_entry(&g_npt, test_pa2);
    trans = npt_translate(&g_npt, test_pa2);
    LOG_ERROR("r1 unit: range nx entry=0x%llx trans=0x%llx rc=0x%x", entry, trans, st);
    if (st || !(entry & NPT_PERM_NX) || trans != test_pa2) {
        LOG_ERROR("r1 unit: range nx FAILED");
        st = STATUS_UNSUCCESSFUL;
        goto done;
    }
    yghv_trace("r1 range nx ok");

    st = npt_set_page_perm_range(&g_npt, test_pa2, HV_PAGE_SIZE * 2,
        NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
    entry = npt_read_entry(&g_npt, test_pa2);
    LOG_ERROR("r1 unit: range restore entry=0x%llx rc=0x%x", entry, st);
    if (st || (entry & NPT_PERM_NX) || !(entry & NPT_PERM_WRITABLE)) {
        LOG_ERROR("r1 unit: range restore FAILED");
        st = STATUS_UNSUCCESSFUL;
        goto done;
    }

    LOG_ERROR("r1 unit: PASS");
    yghv_trace("r1 unit pass");
    st = STATUS_SUCCESS;

done:
    if (buf) ExFreePoolWithTag(buf, YGHV_TAG);
    return st;
}

static NTSTATUS yghv_protect_test(void) {
    void *buf;
    uint64_t buf_pa, buf_va;
    uint64_t entry_after;
    svm_vcpu_t *v;
    NTSTATUS st;
    int ok = 1;

    /* Phase B: policy matrix (synthetic) */
    g_protect.cr3 = 0x1000ULL;   /* fake target CR3 */
    if (!yghv_protect_is_target_cr3(0x1000ULL)) ok = 0;
    if (yghv_protect_is_target_cr3(0x2000ULL)) ok = 0;
    LOG_ERROR("protect test: policy %s", ok ? "PASS" : "FAIL");
    if (!ok) return STATUS_UNSUCCESSFUL;

    /* Phase C: real write trap, target = System (current process) */
    st = yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect test: set_target FAILED 0x%x", st);
        g_protect.cr3 = 0;   /* don't leave the synthetic policy CR3 behind */
        return st;
    }
    buf = MmAllocateContiguousMemory(HV_PAGE_SIZE,
        (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!buf) {
        yghv_protect_cleanup();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(buf, HV_PAGE_SIZE);
    buf_va = (uint64_t)buf;
    buf_pa = MmGetPhysicalAddress(buf).QuadPart;
    st = yghv_protect_add_page(buf_va);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect test: setup FAILED 0x%x", st);
        MmFreeContiguousMemory(buf);
        return st;
    }
    st = yghv_protect_start();
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect test: setup FAILED 0x%x", st);
        yghv_protect_stop();
        yghv_protect_remove_page(buf_va);
        MmFreeContiguousMemory(buf);
        return st;
    }
    entry_after = npt_read_entry(&g_npt, buf_pa);
    if (entry_after & NPT_PERM_WRITABLE) {
        LOG_ERROR("protect test: page still writable after arm");
        ok = 0;
    }

    v = svm_core_get_vcpu(0);
    v->regs.rdi = buf_va;
    v->regs.rcx = g_vmmcall_auth_cookie;
    v->vmcb->state.rip = (uint64_t)svm_trampoline_test_prot_write;
    v->vmcb->state.rax = 0;
    /* Resident exit from the NPT test clears intercepts + NPT; restore them
       so the write is trapped and STOP_INTERNAL VMMCALL can stop the loop. */
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    if (!NT_SUCCESS(svm_core_set_npt(0, g_npt.pml4_pa))) {
        LOG_ERROR("protect test: npt restore FAILED");
        yghv_protect_stop();
        yghv_protect_remove_page(buf_va);
        MmFreeContiguousMemory(buf);
        return STATUS_UNSUCCESSFUL;
    }
    svm_core_enter_resident_current(0);
    entry_after = npt_read_entry(&g_npt, buf_pa);
    if (entry_after & NPT_PERM_WRITABLE) {
        LOG_ERROR("protect test: page not re-armed after write");
        ok = 0;
    }
    LOG_ERROR("protect test: real write %s", ok ? "PASS" : "FAIL");

    /* Second write to the same page: TLB flush + re-arm must keep trapping. */
    v->regs.rdi = buf_va;
    v->regs.rcx = g_vmmcall_auth_cookie;
    v->vmcb->state.rip = (uint64_t)svm_trampoline_test_prot_write;
    v->vmcb->state.rax = 0;
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    if (!NT_SUCCESS(svm_core_set_npt(0, g_npt.pml4_pa))) {
        LOG_ERROR("protect test: npt restore #2 FAILED");
        yghv_protect_stop();
        yghv_protect_remove_page(buf_va);
        MmFreeContiguousMemory(buf);
        return STATUS_UNSUCCESSFUL;
    }
    svm_core_enter_resident_current(0);
    entry_after = npt_read_entry(&g_npt, buf_pa);
    if (entry_after & NPT_PERM_WRITABLE) {
        LOG_ERROR("protect test: page not re-armed after second write");
        ok = 0;
    }
    LOG_ERROR("protect test: real write #2 %s", ok ? "PASS" : "FAIL");
    st = yghv_protect_stop();
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect test: stop FAILED 0x%x", st);
        yghv_protect_remove_page(buf_va);
        MmFreeContiguousMemory(buf);
        return st;
    }
    st = yghv_protect_remove_page(buf_va);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("protect test: remove_page FAILED 0x%x", st);
        MmFreeContiguousMemory(buf);
        return st;
    }
    MmFreeContiguousMemory(buf);
    return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

__attribute__((naked, noinline)) static void yghv_hook_test_dummy(void) {
    __asm__ volatile(
        "nop; nop; nop; nop; nop; nop; nop; nop;"
        "nop; nop; nop; nop; nop; nop; nop; nop;"
        "ret");
}

__attribute__((naked, noinline)) static void yghv_hook_test_dummy2(void) {
    __asm__ volatile(
        "nop; nop; nop; nop; nop; nop; nop; nop;"
        "nop; nop; nop; nop; nop; nop; nop; nop;"
        "ret");
}

static NTSTATUS yghv_hook_test(void) {
    uint64_t term;
    uint64_t open;
    uint64_t dummy;
    uint64_t dummy2;
    uint64_t dummy_page_va;
    uint64_t dummy2_page_va;
    uint64_t gpa;
    uint64_t gpa2;
    uint64_t entry_before, entry_after;
    int ok = 1;

    term = yghv_protect_find_func_pattern(L"ZwTerminateProcess", NULL, 0);
    LOG_ERROR("protect hook test: ZwTerminateProcess=0x%llx", term);
    open = yghv_protect_find_func_pattern(L"NtOpenProcess", NULL, 0);
    LOG_ERROR("protect hook test: NtOpenProcess=0x%llx", open);
    dummy = (uint64_t)yghv_hook_test_dummy;
    dummy_page_va = dummy & ~(HV_PAGE_SIZE - 1);
    gpa = MmGetPhysicalAddress((PVOID)dummy_page_va).QuadPart;
    if (yghv_protect_install_hook(0, (uint64_t)yghv_hook_test_dummy)) {
        LOG_ERROR("protect hook test: install FAILED");
        return STATUS_UNSUCCESSFUL;
    }
    entry_before = npt_read_entry(&g_npt, gpa);
    if (entry_before & NPT_PERM_WRITABLE) ok = 0;

    if (yghv_protect_on_hook_query(0, g_protect.cr3) != YGHV_STATUS_OK) ok = 0;
    if (yghv_protect_on_hook_query(0, g_protect.cr3 + 0x1000) != YGHV_STATUS_DENIED) ok = 0;

    if (!NT_SUCCESS(yghv_protect_remove_hook(0))) {
        LOG_ERROR("protect hook test: remove FAILED");
        ok = 0;
    }
    entry_after = npt_read_entry(&g_npt, gpa);
    if (!(entry_after & NPT_PERM_WRITABLE)) ok = 0;
    if (memcmp((void *)dummy, &g_protect_hooks[0].original, YGHV_PROTECT_PATCH_LEN) != 0) ok = 0;

    dummy2 = (uint64_t)yghv_hook_test_dummy2;
    dummy2_page_va = dummy2 & ~(HV_PAGE_SIZE - 1);
    gpa2 = MmGetPhysicalAddress((PVOID)dummy2_page_va).QuadPart;
    if (yghv_protect_install_hook(1, (uint64_t)yghv_hook_test_dummy2)) {
        LOG_ERROR("protect hook test: install1 FAILED");
        ok = 0;
    } else {
        entry_before = npt_read_entry(&g_npt, gpa2);
        if (entry_before & NPT_PERM_WRITABLE) ok = 0;

        if (yghv_protect_on_hook_query(1, g_protect.cr3) != YGHV_STATUS_OK) ok = 0;
        if (yghv_protect_on_hook_query(1, g_protect.cr3 + 0x1000) != YGHV_STATUS_DENIED) ok = 0;

        if (!NT_SUCCESS(yghv_protect_remove_hook(1))) {
            LOG_ERROR("protect hook test: remove1 FAILED");
            ok = 0;
        }
        entry_after = npt_read_entry(&g_npt, gpa2);
        if (!(entry_after & NPT_PERM_WRITABLE)) ok = 0;
        if (memcmp((void *)dummy2, &g_protect_hooks[1].original, YGHV_PROTECT_PATCH_LEN) != 0) ok = 0;
    }

    LOG_ERROR("protect hook test: %s", ok ? "PASS" : "FAIL");
    return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS yghv_hook_resident_test(void) {
    uint64_t dummy = (uint64_t)yghv_hook_test_dummy;
    uint64_t real_cr3;
    svm_vcpu_t *v;
    NTSTATUS st;
    int ok = 1;

    st = yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("hook resident test: set_target FAILED 0x%x", st);
        return st;
    }
    if (yghv_protect_install_hook(0, dummy)) {
        LOG_ERROR("hook resident test: install FAILED");
        return STATUS_UNSUCCESSFUL;
    }
    real_cr3 = g_protect.cr3;

    v = svm_core_get_vcpu(0);
    v->regs.rsi = dummy;
    v->regs.rcx = g_vmmcall_auth_cookie;
    v->vmcb->state.cr3 = real_cr3;
    v->vmcb->state.rip = (uint64_t)svm_trampoline_test_hook_guest;
    v->vmcb->state.rax = 0;
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    if (!NT_SUCCESS(svm_core_set_npt(0, g_npt.pml4_pa))) {
        LOG_ERROR("hook resident test: npt restore FAILED");
        yghv_protect_remove_hook(0);
        return STATUS_UNSUCCESSFUL;
    }
    svm_core_enter_resident_current(0);
    LOG_ERROR("hook resident test: allow rdx=0x%llx", v->regs.rdx);
    if (v->regs.rdx != 0)
        ok = 0;

    /* Deny: keep guest CR3 valid, shift g_protect.cr3 out of match. */
    g_protect.cr3 = real_cr3 + 0x1000;
    v->regs.rsi = dummy;
    v->regs.rcx = g_vmmcall_auth_cookie;
    v->vmcb->state.cr3 = real_cr3;
    v->vmcb->state.rip = (uint64_t)svm_trampoline_test_hook_guest;
    v->vmcb->state.rax = 0;
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    if (!NT_SUCCESS(svm_core_set_npt(0, g_npt.pml4_pa))) {
        LOG_ERROR("hook resident test: npt restore #2 FAILED");
        g_protect.cr3 = real_cr3;
        yghv_protect_remove_hook(0);
        return STATUS_UNSUCCESSFUL;
    }
    svm_core_enter_resident_current(0);
    LOG_ERROR("hook resident test: deny rdx=0x%llx", v->regs.rdx);
    if (v->regs.rdx != 0xC0000022ULL)
        ok = 0;
    g_protect.cr3 = real_cr3;

    st = yghv_protect_remove_hook(0);
    if (!NT_SUCCESS(st)) {
        LOG_ERROR("hook resident test: remove FAILED 0x%x", st);
        ok = 0;
    }
    LOG_ERROR("hook resident test: %s", ok ? "PASS" : "FAIL");
    return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS yghv_hook_boundary_test(void) {
    void *buf;
    uint64_t va;
    int ok = 1;

    buf = MmAllocateContiguousMemory(
        HV_PAGE_SIZE * 2, (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(buf, HV_PAGE_SIZE * 2);
    va = ((uint64_t)buf + HV_PAGE_SIZE - 1) & ~(uint64_t)(HV_PAGE_SIZE - 1);

    /* valid target: 16 NOPs then ret */
    RtlFillMemory((void *)va, 16, 0x90);
    *(uint8_t *)(va + 16) = 0xC3;
    if (yghv_protect_validate_hook_target(va) != 0)
        ok = 0;

    /* patch region crossing the 4KB page boundary must be rejected */
    if (yghv_protect_validate_hook_target(va + HV_PAGE_SIZE - 8) == 0)
        ok = 0;

    /* instruction stream crossing offset 16 (13 NOPs + E9 rel32) rejected */
    RtlZeroMemory((void *)va, 32);
    RtlFillMemory((void *)va, 13, 0x90);
    *(uint8_t *)(va + 13) = 0xE9;
    *(uint8_t *)(va + 17) = 0x01;
    if (yghv_protect_validate_hook_target(va) == 0)
        ok = 0;

    LOG_ERROR("hook boundary test: %s", ok ? "PASS" : "FAIL");
    MmFreeContiguousMemory(buf);
    return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS yghv_cpuid_stealth_test(void) {
    svm_vcpu_t *v;
    int ok = 1;

    v = svm_core_get_vcpu(0);
    v->regs.rcx = g_vmmcall_auth_cookie;
    v->regs.rax = 0;
    v->vmcb->state.rip = (uint64_t)svm_trampoline_test_cpuid_guest;
    v->vmcb->state.rax = 0;
    v->vmcb->control.general1_intercepts = INTERCEPT_CPUID;
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    if (!NT_SUCCESS(svm_core_set_npt(0, g_npt.pml4_pa))) {
        LOG_ERROR("cpuid stealth test: npt restore FAILED");
        return STATUS_UNSUCCESSFUL;
    }
    svm_core_enter_resident_current(0);
    LOG_ERROR("cpuid stealth test: leaf1_ecx=0x%llx hyper=0x%llx/0x%llx/0x%llx/0x%llx svm_ecx=0x%llx svm_leaf_eax=0x%llx",
        v->regs.r14,
        v->regs.r8, v->regs.r9, v->regs.r10, v->regs.r11,
        v->regs.r12, v->regs.r13);
    if (v->regs.r14 & (1ULL << 31))
        ok = 0;
    if (v->regs.r8 != 0 || v->regs.r9 != 0 || v->regs.r10 != 0 ||
        v->regs.r11 != 0)
        ok = 0;
    if (v->regs.r12 & (1ULL << 2))
        ok = 0;
    if (v->regs.r13 != 0)
        ok = 0;
    LOG_ERROR("cpuid stealth test: %s", ok ? "PASS" : "FAIL");
    return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static __declspec(noinline) void yghv_os_guest_main(void) {
    uint32_t a, b, c, d;

    for (;;) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                                 : "a"(1) : "memory");
        InterlockedIncrement(&g_os_guest_counter);
        g_os_guest_cpuid_acc += a;
        (void)__rdtsc();
    }
}

__declspec(noinline) __declspec(noreturn)
void yghv_os_guest_host_done(svm_vcpu_t *vcpu) {
    uint32_t core = vcpu ? vcpu->resident_index : 0;
    yghv_trace_u64("os guest host done", core);
    if (g_v101_gp_seen) {
        yghv_trace(g_v102_catchall ? "v102 fault captured" : "v101 gp captured");
        yghv_trace_u64("v101 gp exitcode", g_v101_gp_exitcode);
        yghv_trace_u64("v101 gp err", g_v101_gp_err);
        yghv_trace_u64("v101 gp rip", g_v101_gp_rip);
        yghv_trace_u64("v101 gp rsp", g_v101_gp_rsp);
        yghv_trace_u64("v101 gp cr3", g_v101_gp_cr3);
        yghv_trace_u64("v101 gp gsbase", g_v101_gp_gs_base);
        yghv_trace_u64("v101 gp cs", vcpu ? vcpu->vmcb->state.cs_selector : 0);
        yghv_trace_u64("v101 gp ss", vcpu ? vcpu->vmcb->state.ss_selector : 0);
    }
    if (core < SVM_MAX_CORES)
        KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID yghv_os_guest_thread(PVOID ctx) {
    uint32_t core = (uint32_t)(uintptr_t)ctx;
    svm_vcpu_t *v;

    KeSetSystemAffinityThread((KAFFINITY)(1ULL << core));
    v = svm_core_get_vcpu(core);
    if (!v || g_vcpu_count <= core) {
        if (core < SVM_MAX_CORES)
            KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
    }

    svm_prepare_vcpu(v, (uint64_t)yghv_os_guest_main);
    /* svm_prepare_vcpu's final VMSAVE overwrites RIP/RSP with the current
       context; restore the guest entry point like the synthetic tests do. */
    v->vmcb->state.rip = (uint64_t)yghv_os_guest_main;
    v->vmcb->state.rsp = 0;
    v->vmcb->control.general1_intercepts =
        INTERCEPT_CPUID | INTERCEPT_RDTSC |
        INTR_GEN1(SVM_INTERCEPT_SHUTDOWN) |
        INTR_GEN1(SVM_INTERCEPT_MSR_PROT);
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    v->vmcb->control.exception_intercepts = 0;
    v->vmcb->control.tlb_control = 0;
    v->vmcb->control.vmcb_clean_bits = 0;
    v->resident_index = core;
    svm_core_set_npt(core, g_npt.pml4_pa);
    yghv_trace_u64("os guest thread enter", core);
    svm_trampoline_os_enter(v, 0);
    /* Trampoline exits via yghv_os_guest_host_done; this is a fallback. */
    if (core < SVM_MAX_CORES)
        KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID yghv_os_guest_seamless_thread(PVOID ctx) {
    uint32_t core = (uint32_t)(uintptr_t)ctx;
    svm_vcpu_t *v;
    volatile ULONG i;
    uint32_t a, b, c, d;

    KeSetSystemAffinityThread((KAFFINITY)(1ULL << core));
    v = svm_core_get_vcpu(core);
    if (!v || g_vcpu_count <= core) {
        if (core < SVM_MAX_CORES)
            KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
    }
    svm_prepare_vcpu(v, (uint64_t)svm_os_seamless_cont);
    v->vmcb->state.rip = (uint64_t)svm_os_seamless_cont;
    v->vmcb->state.rsp = 0;
    v->vmcb->control.general1_intercepts =
        INTERCEPT_CPUID | INTERCEPT_RDTSC |
        INTR_GEN1(SVM_INTERCEPT_SHUTDOWN) |
        INTR_GEN1(SVM_INTERCEPT_MSR_PROT);
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    v->vmcb->control.exception_intercepts = 0;
    v->vmcb->control.tlb_control = 0;
    v->vmcb->control.vmcb_clean_bits = 0;
    v->resident_index = core;
    svm_core_set_npt(core, g_npt.pml4_pa);
    yghv_trace_u64("os seamless enter", core);
    svm_trampoline_os_enter(v, 0);
    /* Seamless continuation: this caller now runs in guest mode. */
    for (i = 0; i < 5000; i++) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                                 : "a"(1) : "memory");
        InterlockedIncrement(&g_os_guest_counter);
        (void)__rdtsc();
    }
    for (;;) {
        __asm__ volatile("pause");
    }
}

static VOID yghv_os_guest_resident_thread(PVOID ctx) {
    uint32_t core = (uint32_t)(uintptr_t)ctx;
    svm_vcpu_t *v;

    KeSetSystemAffinityThread((KAFFINITY)(1ULL << core));
    v = svm_core_get_vcpu(core);
    if (!v || g_vcpu_count <= core) {
        if (core < SVM_MAX_CORES)
            KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
    }
    svm_prepare_vcpu(v, (uint64_t)svm_os_seamless_cont);
    v->vmcb->state.rip = (uint64_t)svm_os_seamless_cont;
    v->vmcb->state.rsp = 0;
    v->vmcb->control.general1_intercepts =
        INTERCEPT_CPUID | INTERCEPT_RDTSC |
        INTR_GEN1(SVM_INTERCEPT_SHUTDOWN) |
        INTR_GEN1(SVM_INTERCEPT_MSR_PROT);
    if (g_os_guest_intr_intercept) {
        v->vmcb->control.general1_intercepts |=
            INTR_GEN1(SVM_INTERCEPT_INTR) | INTR_GEN1(SVM_INTERCEPT_NMI);
    }
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    if (g_v97_hlt_intercept) {
        v->vmcb->control.general1_intercepts |=
            INTR_GEN1(SVM_INTERCEPT_HLT);
        v->vmcb->control.general2_intercepts |=
            INTR_GEN2(SVM_INTERCEPT_MWAIT) |
            INTR_GEN2(SVM_INTERCEPT_MWAIT_COND);
    }
    v->vmcb->control.exception_intercepts = 0;
    v->vmcb->control.tlb_control = 0;
    v->vmcb->control.vmcb_clean_bits = 0;
    v->resident_index = core;
    svm_core_set_npt(core, g_npt.pml4_pa);
    if (g_v96_apic_tpr_stress && !g_v96_apic_tpr_va) {
        PHYSICAL_ADDRESS apic_pa;
        apic_pa.QuadPart = 0xFEE00000ULL;
        g_v96_apic_tpr_va = MmMapIoSpace(apic_pa, HV_PAGE_SIZE, MmNonCached);
        if (!g_v96_apic_tpr_va)
            LOG_ERROR("v96: map APIC TPR failed");
        else
            yghv_trace("v96 tpr stress armed");
    }
    g_os_resident_mode = TRUE;
    yghv_trace_u64("os resident enter", core);
    if (core < SVM_MAX_CORES)
        KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
    svm_trampoline_os_enter(v, 1);
    /* Guest continuation: block forever so the scheduler keeps this core
       running the rest of Windows in guest mode. */
    KeWaitForSingleObject(&g_os_resident_stop_event, Executive,
                          KernelMode, FALSE, NULL);
    for (;;) {
        __asm__ volatile("pause");
    }
}

static VOID yghv_resident_alive_thread(PVOID ctx) {
    LARGE_INTEGER delay;
    ULONG seconds = 0;
    (void)ctx;

    KeSetSystemAffinityThread((KAFFINITY)1);
    delay.QuadPart = -5LL * 10000000LL;
    for (;;) {
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
        seconds += 5;
        yghv_trace_u64("resident alive", seconds);
    }
}

static VOID yghv_resident_watchdog_thread(PVOID ctx) {
    LARGE_INTEGER delay;
    ULONG64 last = 0;
    ULONG stall = 0;
    (void)ctx;

    KeSetSystemAffinityThread((KAFFINITY)1);
    delay.QuadPart = -1LL * 10000000LL;
    for (;;) {
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
        if (!g_os_resident_mode)
            continue;
        if (g_os_resident_exits == last) {
            if (++stall >= 3) {
                KeBugCheckEx(0xE2, 0x59475644,
                             (ULONG_PTR)g_os_resident_exits,
                             (ULONG_PTR)stall, 0);
            }
        } else {
            last = g_os_resident_exits;
            stall = 0;
        }
    }
}

static void yghv_v100_dump(svm_vcpu_t *vcpu) {
    uint64_t seq;
    uint64_t start;
    uint64_t n;
    ULONG i;

    if (!vcpu)
        return;
    seq = vcpu->v100_seq;
    if (seq == 0)
        return;
    yghv_trace("v100 freeze site");
    yghv_trace_u64("v100 seq", seq);
    yghv_trace_u64("v100 exits", vcpu->resident_exits);
    yghv_trace_u64("v100 intr exits", vcpu->resident_interrupt_exits);
    yghv_trace_u64("v100 msr exits", vcpu->resident_msr_exits);
    yghv_trace_u64("v100 cr exits", vcpu->resident_cr_exits);
    yghv_trace_u64("v100 last exitcode",
        vcpu->vmcb->control.exitcode);
    yghv_trace_u64("v100 last rip", vcpu->vmcb->state.rip);
    yghv_trace_u64("v100 last cr3", vcpu->vmcb->state.cr3);
    yghv_trace_u64("v100 last rsp", vcpu->vmcb->state.rsp);
    yghv_trace_u64("v100 last rflags", vcpu->vmcb->state.rflags);
    yghv_trace_u64("v100 last cpl", vcpu->vmcb->state.cpl);

    start = (seq >= YGHV_V100_RING_ENTRIES)
                ? seq - (YGHV_V100_RING_ENTRIES - 1)
                : 0;
    n = seq - start + 1;
    for (i = 0; i < n; i++) {
        uint64_t s = start + i;
        svm_v100_ring_entry_t *e = &vcpu->v100_ring[s % YGHV_V100_RING_ENTRIES];
        if (e->seq != s)
            continue;
        yghv_trace_u64("v100 seq", s);
        yghv_trace_u64("v100 exit", e->exitcode);
        yghv_trace_u64("v100 i1", e->exitinfo1);
        yghv_trace_u64("v100 i2", e->exitinfo2);
        yghv_trace_u64("v100 rip", e->rip);
        yghv_trace_u64("v100 cr3", e->cr3);
        yghv_trace_u64("v100 rsp", e->rsp);
        yghv_trace_u64("v100 rf", e->rflags);
        yghv_trace_u64("v100 cpl", e->cpl);
    }
}

static VOID yghv_v100_monitor_thread(PVOID ctx) {
    LARGE_INTEGER delay;
    ULONG64 last_exits = 0;
    uint64_t last_seq = 0;
    ULONG stall = 0;
    BOOLEAN dumped = FALSE;
    (void)ctx;

    KeSetSystemAffinityThread((KAFFINITY)1);
    delay.QuadPart = -250LL * 10000LL;
    for (;;) {
        svm_vcpu_t *v;

        KeDelayExecutionThread(KernelMode, FALSE, &delay);
        if (!g_v100_monitor_active)
            continue;
        if (!g_v100_guest_entered)
            continue;
        v = svm_core_get_vcpu(1);
        if (!v) {
            if (++stall >= 4) {
                yghv_trace("v100 vcpu1 unavailable");
                break;
            }
            continue;
        }
        yghv_trace_u64("v100 pulse exits", v->resident_exits);
        yghv_trace_u64("v100 pulse seq", v->v100_seq);
        yghv_trace_u64("v100 pulse last exit", v->vmcb->control.exitcode);
        yghv_trace_u64("v100 pulse rip", v->vmcb->state.rip);
        yghv_trace_u64("v100 pulse cr3", v->vmcb->state.cr3);
        yghv_trace_u64("v100 pulse rsp", v->vmcb->state.rsp);
        if (v->resident_exits == last_exits && v->v100_seq == last_seq) {
            if (++stall >= 4 && !dumped) {
                yghv_trace_u64("v100 stall exits", v->resident_exits);
                yghv_trace_u64("v100 stall ticks",
                               (uint64_t)__rdtsc());
                yghv_v100_dump(v);
                dumped = TRUE;
            }
        } else {
            last_exits = v->resident_exits;
            last_seq = v->v100_seq;
            stall = 0;
        }
    }
}

static VOID yghv_os_guest_resident_spin_thread(PVOID ctx) {
    uint32_t core = (uint32_t)(uintptr_t)ctx;
    svm_vcpu_t *v;
    uint32_t a, b, c, d;

    KeSetSystemAffinityThread((KAFFINITY)(1ULL << core));
    v = svm_core_get_vcpu(core);
    if (!v || g_vcpu_count <= core) {
        if (core < SVM_MAX_CORES)
            KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
    }
    svm_prepare_vcpu(v, (uint64_t)svm_os_seamless_cont);
    v->vmcb->state.rip = (uint64_t)svm_os_seamless_cont;
    v->vmcb->state.rsp = 0;
    v->vmcb->control.general1_intercepts =
        INTERCEPT_CPUID | INTERCEPT_RDTSC |
        INTR_GEN1(SVM_INTERCEPT_SHUTDOWN) |
        INTR_GEN1(SVM_INTERCEPT_MSR_PROT);
    if (g_os_guest_intr_intercept) {
        v->vmcb->control.general1_intercepts |=
            INTR_GEN1(SVM_INTERCEPT_INTR) | INTR_GEN1(SVM_INTERCEPT_NMI);
    }
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    if (g_v97_hlt_intercept) {
        v->vmcb->control.general1_intercepts |=
            INTR_GEN1(SVM_INTERCEPT_HLT);
        v->vmcb->control.general2_intercepts |=
            INTR_GEN2(SVM_INTERCEPT_MWAIT) |
            INTR_GEN2(SVM_INTERCEPT_MWAIT_COND);
    }
    v->vmcb->control.exception_intercepts = 0;
    v->vmcb->control.tlb_control = 0;
    v->vmcb->control.vmcb_clean_bits = 0;
    v->resident_index = core;
    svm_core_set_npt(core, g_npt.pml4_pa);
    g_os_resident_mode = TRUE;
    yghv_trace_u64("os resident spin enter", core);
    if (core < SVM_MAX_CORES)
        KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
    svm_trampoline_os_enter(v, 1);
    /* Guest continuation: spin without blocking so the scheduler never
       switches this core to another thread while in guest mode. */
    for (;;) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                                 : "a"(1) : "memory");
        (void)__rdtsc();
        if (g_v96_apic_tpr_stress && g_v96_apic_tpr_va) {
            *(volatile ULONG *)((ULONG_PTR)g_v96_apic_tpr_va + APIC_OFFSET_TPR) = 0;
        }
    }
}

static VOID yghv_os_guest_allcore_thread(PVOID ctx) {
    uint32_t core = (uint32_t)(uintptr_t)ctx;
    svm_vcpu_t *v;

    KeSetSystemAffinityThread((KAFFINITY)(1ULL << core));
    v = svm_core_get_vcpu(core);
    if (!v || g_vcpu_count <= core) {
        InterlockedExchange(&g_v99_allcore_abort, 1);
        if (core < SVM_MAX_CORES)
            KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
    }
    svm_prepare_vcpu(v, (uint64_t)svm_os_seamless_cont);
    v->vmcb->state.rip = (uint64_t)svm_os_seamless_cont;
    v->vmcb->state.rsp = 0;
    v->vmcb->control.general1_intercepts =
        INTERCEPT_CPUID | INTERCEPT_RDTSC |
        INTR_GEN1(SVM_INTERCEPT_SHUTDOWN) |
        INTR_GEN1(SVM_INTERCEPT_MSR_PROT) |
        INTR_GEN1(SVM_INTERCEPT_INTR) | INTR_GEN1(SVM_INTERCEPT_NMI);
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    v->vmcb->control.exception_intercepts = 0;
    v->vmcb->control.tlb_control = 0;
    v->vmcb->control.vmcb_clean_bits = 0;
    v->resident_index = core;
    svm_core_set_npt(core, g_npt.pml4_pa);
    g_os_resident_mode = TRUE;

    InterlockedIncrement(&g_v99_allcore_ready);
    while (!g_v99_allcore_go && !g_v99_allcore_abort) {
        __asm__ volatile("pause");
    }
    if (g_v99_allcore_abort) {
        yghv_trace_u64("allcore abort", core);
        if (core < SVM_MAX_CORES)
            KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
    }

    yghv_trace_u64("allcore enter", core);
    if (core < SVM_MAX_CORES)
        KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
    svm_trampoline_os_enter(v, 1);
    /* Guest continuation: block so the scheduler switches threads on this core. */
    KeWaitForSingleObject(&g_os_resident_stop_event, Executive,
                          KernelMode, FALSE, NULL);
    for (;;) {
        __asm__ volatile("pause");
    }
}

static VOID yghv_os_guest_resident_delay_thread(PVOID ctx) {
    uint32_t core = (uint32_t)(uintptr_t)ctx;
    svm_vcpu_t *v;
    uint32_t a, b, c, d;
    LARGE_INTEGER delay;

    KeSetSystemAffinityThread((KAFFINITY)(1ULL << core));
    v = svm_core_get_vcpu(core);
    if (!v || g_vcpu_count <= core) {
        if (core < SVM_MAX_CORES)
            KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
    }
    svm_prepare_vcpu(v, (uint64_t)svm_os_seamless_cont);
    v->vmcb->state.rip = (uint64_t)svm_os_seamless_cont;
    v->vmcb->state.rsp = 0;
    v->vmcb->control.general1_intercepts =
        INTERCEPT_CPUID | INTERCEPT_RDTSC |
        INTR_GEN1(SVM_INTERCEPT_SHUTDOWN) |
        INTR_GEN1(SVM_INTERCEPT_MSR_PROT);
    if (g_os_guest_intr_intercept) {
        v->vmcb->control.general1_intercepts |=
            INTR_GEN1(SVM_INTERCEPT_INTR) | INTR_GEN1(SVM_INTERCEPT_NMI);
    }
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    if (g_v97_hlt_intercept) {
        v->vmcb->control.general1_intercepts |=
            INTR_GEN1(SVM_INTERCEPT_HLT);
        v->vmcb->control.general2_intercepts |=
            INTR_GEN2(SVM_INTERCEPT_MWAIT) |
            INTR_GEN2(SVM_INTERCEPT_MWAIT_COND);
    }
    v->vmcb->control.exception_intercepts = 0;
    if (g_v101_gp_intercept) {
        /* Turn the context-switch fault into a VMEXIT so we can capture the
           site before Windows exception dispatch destroys the stack. */
        v->vmcb->control.exception_intercepts =
            (1ULL << 8) | (1ULL << 11) | (1ULL << 12) | (1ULL << 13);
    }
    if (g_v102_catchall) {
        /* v102: catch every guest fault vector plus HLT. If the machine
           still stops without any of these, it is a platform-level halt. */
        v->vmcb->control.exception_intercepts = 0xFFFFFFFFULL;
        v->vmcb->control.general1_intercepts |=
            INTR_GEN1(SVM_INTERCEPT_HLT);
    }
    v->vmcb->control.tlb_control = 0;
    v->vmcb->control.vmcb_clean_bits = 0;
    v->resident_index = core;
    svm_core_set_npt(core, g_npt.pml4_pa);
    if (g_v98_apic_shadow && !g_v98_apic_shadow_va) {
        PHYSICAL_ADDRESS apic_pa;
        apic_pa.QuadPart = 0xFEE00000ULL;
        g_v98_apic_shadow_va = MmAllocateContiguousMemory(
            HV_PAGE_SIZE, (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
        if (!g_v98_apic_shadow_va) {
            LOG_ERROR("v98: shadow page alloc failed");
        } else {
            RtlZeroMemory(g_v98_apic_shadow_va, HV_PAGE_SIZE);
            g_v98_apic_shadow_pa = MmGetPhysicalAddress(g_v98_apic_shadow_va).QuadPart;
            g_v98_real_apic_va = MmMapIoSpace(apic_pa, HV_PAGE_SIZE, MmNonCached);
            if (!g_v98_real_apic_va) {
                LOG_ERROR("v98: map real APIC failed");
            } else {
                RtlCopyMemory(g_v98_apic_shadow_va, g_v98_real_apic_va, HV_PAGE_SIZE);
                g_v98_last_tpr = READ_REGISTER_ULONG(
                    (PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_TPR));
                g_v98_last_icrl = READ_REGISTER_ULONG(
                    (PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_ICRL));
                g_v98_last_icrh = READ_REGISTER_ULONG(
                    (PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_ICRH));
                g_v98_last_lvtt = READ_REGISTER_ULONG(
                    (PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_LVTT));
                g_v98_last_tmict = READ_REGISTER_ULONG(
                    (PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_TMICT));
                g_v98_last_tdcr = READ_REGISTER_ULONG(
                    (PULONG)((ULONG_PTR)g_v98_real_apic_va + APIC_OFFSET_TDCR));
                if (npt_map_page(&g_npt, 0xFEE00000ULL, g_v98_apic_shadow_pa,
                                 NPT_4K_PAGE_FLAGS | (1ULL << 4) | (1ULL << 63)) != STATUS_SUCCESS)
                    LOG_ERROR("v98: npt_map_page failed");
                v->vmcb->control.tlb_control = SVM_TLB_CONTROL_FLUSH;
                yghv_trace_u64("v98 apic shadow armed", g_v98_apic_shadow_pa);
            }
        }
    }
    g_os_resident_mode = TRUE;
    yghv_trace_u64("os resident delay enter", core);
    g_v100_guest_entered = TRUE;
    if (core < SVM_MAX_CORES)
        KeSetEvent(&g_os_guest_done_events[core], IO_NO_INCREMENT, FALSE);
    svm_trampoline_os_enter(v, 1);
    /* Guest continuation: block once so the scheduler performs at least one
       real context switch inside guest mode, then return to the proven spin
       shape with the host-ISR path. */
    delay.QuadPart = -1LL * 10000000LL;
    if (!g_os_guest_delay_quiet)
        yghv_trace("os resident delay guest block");
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    if (!g_os_guest_delay_quiet)
        yghv_trace("os resident delay guest wake");
    for (;;) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                                 : "a"(1) : "memory");
        (void)__rdtsc();
    }
}

static void *yghv_avic_alloc_page(uint64_t *pa_out) {
    void *page;

    page = MmAllocateContiguousMemory(HV_PAGE_SIZE,
        (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!page)
        return NULL;
    RtlZeroMemory(page, HV_PAGE_SIZE);
    *pa_out = MmGetPhysicalAddress(page).QuadPart;
    return page;
}

static void yghv_avic_cleanup(svm_vcpu_t *vcpu) {
    if (!vcpu)
        return;
    if (vcpu->avic_host_apic_va) {
        MmUnmapIoSpace(vcpu->avic_host_apic_va, HV_PAGE_SIZE);
        vcpu->avic_host_apic_va = NULL;
    }
    if (vcpu->avic_physical_id_table) {
        MmFreeContiguousMemory(vcpu->avic_physical_id_table);
        vcpu->avic_physical_id_table = NULL;
        vcpu->avic_physical_id_pa = 0;
    }
    if (vcpu->avic_logical_id_table) {
        MmFreeContiguousMemory(vcpu->avic_logical_id_table);
        vcpu->avic_logical_id_table = NULL;
        vcpu->avic_logical_id_pa = 0;
    }
    if (vcpu->avic_backing_page) {
        MmFreeContiguousMemory(vcpu->avic_backing_page);
        vcpu->avic_backing_page = NULL;
        vcpu->avic_backing_pa = 0;
    }
}

static uint32_t yghv_avic_ffs_u32(uint32_t value) {
    uint32_t i;
    for (i = 0; i < 32; i++) {
        if (value & (1U << i))
            return i;
    }
    return 0;
}

static NTSTATUS yghv_avic_prepare(svm_vcpu_t *vcpu, uint32_t core) {
    int cpu_info[4];
    KAFFINITY old_affinity;
    uint32_t guest_apic_id, host_apic_id;
    uint32_t ldr, dfr, logical_id, table_index;
    uint64_t entry;
    PHYSICAL_ADDRESS apic_pa;
    NTSTATUS st = STATUS_SUCCESS;

    if (!vcpu)
        return STATUS_INVALID_PARAMETER;

    __cpuidex(cpu_info, CPUID_AMD_NPT, 0);
    if (!(cpu_info[3] & CPUID_NPT_FEATURE_AVIC)) {
        LOG_ERROR("step25: AVIC unsupported by CPU");
        return STATUS_HV_FEATURE_UNAVAILABLE;
    }

    vcpu->avic_backing_page = yghv_avic_alloc_page(&vcpu->avic_backing_pa);
    if (!vcpu->avic_backing_page) { st = STATUS_INSUFFICIENT_RESOURCES; goto fail; }
    vcpu->avic_logical_id_table = yghv_avic_alloc_page(&vcpu->avic_logical_id_pa);
    if (!vcpu->avic_logical_id_table) { st = STATUS_INSUFFICIENT_RESOURCES; goto fail; }
    vcpu->avic_physical_id_table = yghv_avic_alloc_page(&vcpu->avic_physical_id_pa);
    if (!vcpu->avic_physical_id_table) { st = STATUS_INSUFFICIENT_RESOURCES; goto fail; }

    old_affinity = KeSetSystemAffinityThreadEx((KAFFINITY)(1ULL << core));
    __cpuidex(cpu_info, 1, 0);
    guest_apic_id = ((uint32_t)cpu_info[1] >> 24) & 0xFF;
    KeSetSystemAffinityThread(old_affinity);
    if (guest_apic_id == 0xFF) {
        LOG_ERROR("step25: guest APIC ID 0xFF reserved");
        st = STATUS_INVALID_PARAMETER;
        goto fail;
    }
    host_apic_id = guest_apic_id;
    vcpu->avic_apic_id = host_apic_id;

    apic_pa.QuadPart = APIC_DEFAULT_PHYS_BASE;
    vcpu->avic_host_apic_va = MmMapIoSpace(apic_pa, HV_PAGE_SIZE, MmNonCached);
    if (!vcpu->avic_host_apic_va) {
        LOG_ERROR("step25: map host APIC failed");
        st = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }
    RtlCopyMemory(vcpu->avic_backing_page, vcpu->avic_host_apic_va, HV_PAGE_SIZE);

    ldr = READ_REGISTER_ULONG((PULONG)((ULONG_PTR)vcpu->avic_host_apic_va + APIC_OFFSET_LDR));
    dfr = READ_REGISTER_ULONG((PULONG)((ULONG_PTR)vcpu->avic_host_apic_va + APIC_OFFSET_DFR));

    entry = (vcpu->avic_backing_pa & AVIC_PHYSICAL_ID_ENTRY_BACKING_PAGE_MASK) |
            AVIC_PHYSICAL_ID_ENTRY_VALID |
            AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING |
            (host_apic_id & AVIC_PHYSICAL_ID_ENTRY_HOST_ID_MASK);
    ((uint64_t *)vcpu->avic_physical_id_table)[guest_apic_id] = entry;

    logical_id = (ldr >> 24) & 0xFF;
    if (logical_id) {
        if (dfr == 0xFFFFFFFF) {
            table_index = yghv_avic_ffs_u32(logical_id) * 4;
        } else {
            uint32_t cluster = (logical_id >> 4) & 0xF;
            uint32_t apic_ix = yghv_avic_ffs_u32(logical_id & 0xF);
            table_index = cluster < 15 ? cluster * 16 + apic_ix * 4 : 0;
        }
        if (table_index + 4 <= HV_PAGE_SIZE) {
            *(volatile uint32_t *)((ULONG_PTR)vcpu->avic_logical_id_table + table_index) =
                AVIC_LOGICAL_ID_ENTRY_VALID |
                (guest_apic_id & AVIC_LOGICAL_ID_ENTRY_GUEST_ID_MASK);
        }
    }

    vcpu->vmcb->control.avic_apic_bar = APIC_DEFAULT_PHYS_BASE;
    vcpu->vmcb->control.avic_backing_page = vcpu->avic_backing_pa;
    vcpu->vmcb->control.avic_logical_id = vcpu->avic_logical_id_pa;
    vcpu->vmcb->control.avic_physical_id =
        (vcpu->avic_physical_id_pa & AVIC_PHYSICAL_ID_ENTRY_BACKING_PAGE_MASK) |
        (0xFE & AVIC_PHYSICAL_MAX_INDEX_MASK);
    vcpu->vmcb->control.vintr |= SVM_INT_CTL_AVIC_ENABLE;
    vcpu->vmcb->control.vintr &= ~SVM_INT_CTL_X2APIC_MODE;
    vcpu->vmcb->control.vmcb_clean_bits = 0;

    yghv_trace_u64("avic prepare backing", vcpu->avic_backing_pa);
    yghv_trace_u64("avic prepare apicid", guest_apic_id);
    yghv_trace_u64("avic ctl vintr", vcpu->vmcb->control.vintr);
    yghv_trace_u64("avic bar", vcpu->vmcb->control.avic_apic_bar);
    yghv_trace_u64("avic backing", vcpu->vmcb->control.avic_backing_page);
    yghv_trace_u64("avic phys", vcpu->vmcb->control.avic_physical_id);
    yghv_trace_u64("avic log", vcpu->vmcb->control.avic_logical_id);
    return STATUS_SUCCESS;

fail:
    yghv_avic_cleanup(vcpu);
    return st;
}

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} yghv_idt_entry_t;

_Static_assert(sizeof(yghv_idt_entry_t) == 16, "IDT entry size");

static void yghv_avic_read_idtr(uint64_t *base, uint16_t *limit) {
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr;
    __asm__ volatile("sidt %0" : "=m"(idtr));
    *limit = idtr.limit;
    *base = idtr.base;
}

static void yghv_avic_load_idtr(uint64_t base, uint16_t limit) {
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr;
    idtr.limit = limit;
    idtr.base = base;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

static void yghv_avic_restore_idt(svm_vcpu_t *vcpu) {
    if (!vcpu || !vcpu->avic_host_idt)
        return;
    ExFreePoolWithTag(vcpu->avic_host_idt, YGHV_TAG);
    vcpu->avic_host_idt = NULL;
    g_avic_eoi_apic_va = NULL;
}

static NTSTATUS yghv_avic_install_eoi_idt(svm_vcpu_t *vcpu) {
    yghv_idt_entry_t *idt;
    uint64_t handler;
    uint16_t cs;
    int i;

    if (!vcpu || !vcpu->avic_host_apic_va)
        return STATUS_INVALID_PARAMETER;
    if (vcpu->avic_host_idt)
        return STATUS_SUCCESS;

    idt = (yghv_idt_entry_t *)ExAllocatePoolWithTag(NonPagedPool, 0x1000, YGHV_TAG);
    if (!idt)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(idt, 0x1000);

    yghv_avic_read_idtr(&vcpu->avic_old_idt_base, &vcpu->avic_old_idt_limit);
    g_avic_old_idtr.limit = vcpu->avic_old_idt_limit;
    g_avic_old_idtr.base = vcpu->avic_old_idt_base;

    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    handler = (uint64_t)yghv_avic_eoi_isr;
    for (i = 0; i < 256; i++) {
        idt[i].offset_low  = (uint16_t)(handler & 0xFFFF);
        idt[i].selector    = cs;
        idt[i].ist         = 0;
        idt[i].type_attr   = 0x8E;
        idt[i].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
        idt[i].offset_high = (uint32_t)(handler >> 32);
        idt[i].reserved    = 0;
    }

    vcpu->avic_host_idt = idt;
    g_avic_eoi_apic_va = vcpu->avic_host_apic_va;
    g_avic_eoi_idtr.limit = 0xFFF;
    g_avic_eoi_idtr.base = (uint64_t)idt;
    return STATUS_SUCCESS;
}

static NTSTATUS yghv_baremetal_step_test(int step) {
    svm_vcpu_t *v = svm_core_get_vcpu(0);
    void *npf_page = NULL;
    void *prot_page = NULL;
    uint64_t npf_pa = 0;
    NTSTATUS st;

    if (!v)
        return STATUS_NOT_FOUND;
    if (step > 102)
        return STATUS_NOT_IMPLEMENTED;
    yghv_trace_u64("bm step", (uint64_t)step);
    yghv_trace("bm start");
    g_os_guest_host_isr = FALSE;
    g_os_guest_inject_intr = FALSE;
    g_os_guest_avic_irr_inject = FALSE;
    g_os_guest_avic_eoi_only = FALSE;
    g_os_guest_avic_timer_emu = FALSE;
    g_os_guest_delay_quiet = FALSE;
    g_v96_apic_tpr_stress = FALSE;
    g_v97_hlt_intercept = FALSE;
    g_v98_apic_shadow = FALSE;
    g_v101_gp_intercept = FALSE;
    g_v101_gp_seen = FALSE;
    g_v102_catchall = FALSE;
    g_os_resident_log_active = FALSE;

    /* v96: step20 PASS baseline + persistent guest physical-APIC TPR writes. */
    if (step == 96) {
        g_v96_apic_tpr_stress = TRUE;
        step = 20;
    }
    /* v97: step23 blocking baseline + intercept guest HLT/MWAIT so idle
       instructions are emulated by the VMM instead of halting in guest mode. */
    if (step == 97) {
        g_v97_hlt_intercept = TRUE;
        step = 23;
    }
    /* v98: step23 blocking baseline + NPT-remapped shadow xAPIC page. */
    if (step == 98) {
        g_v98_apic_shadow = TRUE;
        step = 23;
    }
    /* v101: step100 blocking baseline + intercept #DF/#NP/#SS/#GP so the
       guest context-switch fault becomes a VMEXIT we can capture. */
    if (step == 101) {
        g_v101_gp_intercept = TRUE;
        step = 100;
    }
    /* v102: catch all guest faults + HLT to prove or rule out an
       interceptable exception as the machine-stop cause. */
    if (step == 102) {
        g_v102_catchall = TRUE;
        step = 100;
    }

    if (step == 6) {
        ULONG i;
        for (i = 0; i < g_vcpu_count; i++) {
            svm_vcpu_t *cv = g_vcpus[i];
            if (!cv) continue;
            cv->regs.rcx = g_vmmcall_auth_cookie;
            cv->vmcb->state.rip = (uint64_t)svm_trampoline_test_guest;
            cv->vmcb->state.rax = 0;
            cv->vmcb->control.general2_intercepts =
                INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
        }
        st = svm_core_start_remote_residents(g_vcpu_count);
        if (st)
            return st;
        svm_core_wait_remote_ready(g_vcpu_count);
        yghv_trace("bm multi vmrun");
        svm_core_enter_resident_current(0);
        svm_core_wait_all_stopped(g_vcpu_count);
        yghv_trace("bm multi done");
        return STATUS_SUCCESS;
    }

    if (step == 8) {
        ULONG i;
        yghv_trace("bm persistent start");
        g_resident_workload_page = ExAllocatePoolWithTag(
            NonPagedPool, HV_PAGE_SIZE, YGHV_TAG);
        if (!g_resident_workload_page)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(g_resident_workload_page, HV_PAGE_SIZE);
        st = yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
        if (!st)
            st = yghv_protect_add_page((uint64_t)g_resident_workload_page);
        if (!st)
            st = yghv_protect_install_hook(0, (uint64_t)yghv_hook_test_dummy);
        if (!st)
            st = yghv_protect_start();
        if (st) {
            LOG_ERROR("bm step 8: setup failed 0x%x", st);
            yghv_protect_cleanup();
            if (g_resident_workload_page) ExFreePoolWithTag(g_resident_workload_page, YGHV_TAG);
            g_resident_workload_page = NULL;
            return st;
        }
        for (i = 0; i < g_vcpu_count; i++) {
            svm_vcpu_t *cv = g_vcpus[i];
            if (!cv) continue;
            cv->regs.rcx = g_vmmcall_auth_cookie;
            cv->vmcb->state.cr3 = g_protect.cr3;
            cv->vmcb->control.general1_intercepts = INTERCEPT_CPUID;
            cv->vmcb->control.general2_intercepts =
                INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
            if (i == 0) {
                cv->vmcb->state.rip = (uint64_t)svm_trampoline_test_resident_guest;
                cv->regs.rdi = (uint64_t)g_resident_workload_page;
                cv->regs.rsi = (uint64_t)yghv_hook_test_dummy;
            } else {
                cv->vmcb->state.rip = (uint64_t)svm_trampoline_test_guest;
                cv->regs.rdi = 0;
                cv->regs.rsi = 0;
            }
            cv->vmcb->state.rax = 0;
        }
        st = svm_core_start_persistent_residents(g_vcpu_count);
        if (st) {
            LOG_ERROR("bm step 8: persistent start failed 0x%x", st);
            yghv_protect_cleanup();
            if (g_resident_workload_page) ExFreePoolWithTag(g_resident_workload_page, YGHV_TAG);
            g_resident_workload_page = NULL;
            return st;
        }
        svm_core_wait_remote_ready(g_vcpu_count);
        yghv_trace("bm persistent running");
        return STATUS_SUCCESS;
    }

    if (step == 9) {
        ULONG i;
        yghv_trace("bm persistent hb start");
        st = yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
        if (!st)
            st = yghv_protect_start();  /* 0 pages: keepalive heartbeat only */
        if (st) {
            LOG_ERROR("bm step 9: setup failed 0x%x", st);
            yghv_protect_cleanup();
            return st;
        }
        for (i = 0; i < g_vcpu_count; i++) {
            svm_vcpu_t *cv = g_vcpus[i];
            if (!cv) continue;
            cv->regs.rcx = g_vmmcall_auth_cookie;
            cv->vmcb->state.cr3 = g_control_cr3;
            cv->vmcb->control.general1_intercepts = INTERCEPT_CPUID;
            cv->vmcb->control.general2_intercepts =
                INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
            cv->vmcb->state.rip = (uint64_t)svm_trampoline_test_guest;
            cv->regs.rdi = 0;
            cv->regs.rsi = 0;
            cv->vmcb->state.rax = 0;
        }
        st = svm_core_start_persistent_residents(g_vcpu_count);
        if (st) {
            LOG_ERROR("bm step 9: persistent start failed 0x%x", st);
            yghv_protect_cleanup();
            return st;
        }
        svm_core_wait_remote_ready(g_vcpu_count);
        yghv_trace("bm persistent hb running");
        return STATUS_SUCCESS;
    }

    if (step == 10) {
        ULONG i;
        ULONG bm_cores = 2;
        yghv_trace("bm persistent hb 2core start");
        st = yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
        if (!st)
            st = yghv_protect_start();  /* 0 pages: keepalive heartbeat only */
        if (st) {
            LOG_ERROR("bm step 10: setup failed 0x%x", st);
            yghv_protect_cleanup();
            return st;
        }
        for (i = 0; i < bm_cores && i < g_vcpu_count; i++) {
            svm_vcpu_t *cv = g_vcpus[i];
            if (!cv) continue;
            cv->regs.rcx = g_vmmcall_auth_cookie;
            cv->vmcb->state.cr3 = g_control_cr3;
            cv->vmcb->control.general1_intercepts = INTERCEPT_CPUID;
            cv->vmcb->control.general2_intercepts =
                INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
            cv->vmcb->state.rip = (uint64_t)svm_trampoline_test_guest;
            cv->regs.rdi = 0;
            cv->regs.rsi = 0;
            cv->vmcb->state.rax = 0;
        }
        st = svm_core_start_persistent_residents(bm_cores);
        if (st) {
            LOG_ERROR("bm step 10: persistent start failed 0x%x", st);
            yghv_protect_cleanup();
            return st;
        }
        svm_core_wait_remote_ready(bm_cores);
        yghv_trace("bm persistent hb 2core running");
        return STATUS_SUCCESS;
    }

    if (step == 11) {
        ULONG i;
        ULONG bm_cores = 2;
        yghv_trace("bm bounded hb 2core intr start");
        for (i = 0; i < bm_cores && i < g_vcpu_count; i++) {
            svm_vcpu_t *cv = g_vcpus[i];
            if (!cv) continue;
            cv->regs.rcx = g_vmmcall_auth_cookie;
            cv->vmcb->state.cr3 = g_control_cr3;
            cv->vmcb->control.general1_intercepts =
                INTERCEPT_CPUID | INTR_GEN1(SVM_INTERCEPT_INTR) |
                INTR_GEN1(SVM_INTERCEPT_NMI) |
                INTR_GEN1(SVM_INTERCEPT_SHUTDOWN);
            cv->vmcb->control.general2_intercepts =
                INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
            cv->vmcb->state.rip = (uint64_t)svm_trampoline_test_guest;
            cv->regs.rdi = 0;
            cv->regs.rsi = 0;
            cv->vmcb->state.rax = 0;
        }
        st = svm_core_start_remote_residents(bm_cores);
        if (st)
            return st;
        svm_core_wait_remote_ready(bm_cores);
        yghv_trace("bm bounded hb 2core intr vmrun");
        svm_core_enter_resident_current(0);
        svm_core_wait_all_stopped(bm_cores);
        for (i = 0; i < bm_cores && i < g_vcpu_count; i++) {
            svm_vcpu_t *cv = g_vcpus[i];
            if (!cv) continue;
            const char *lbl = (i == 0) ? "bm s11 c0" : "bm s11 c1";
            yghv_trace_u64(lbl, cv->resident_interrupt_exits);
            yghv_trace_u64("bm s11 ext", cv->resident_exits);
        }
        yghv_trace("bm bounded hb 2core intr done");
        return STATUS_SUCCESS;
    }

    if (step == 12) {
        HANDLE thread;
        NTSTATUS st12;
        LARGE_INTEGER timeout;

        yghv_trace("bm os guest start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        g_os_guest_test_active = 1;
        g_os_guest_counter = 0;
        st12 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st12)) {
            LOG_ERROR("bm step 12: thread create failed 0x%x", st12);
            return st12;
        }
        timeout.QuadPart = -60LL * 10000000LL;
        st12 = KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                                     KernelMode, FALSE, &timeout);
        ZwClose(thread);
        g_os_guest_test_active = 0;
        yghv_trace_u64("os guest counter", g_os_guest_counter);
        yghv_trace("bm os guest done");
        if (st12 == STATUS_TIMEOUT)
            return st12;
        return g_os_guest_counter > 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }

    if (step == 13) {
        ULONG i;
        ULONG j;
        ULONG bm_cores = 2;
        HANDLE threads[SVM_MAX_CORES] = { 0 };
        LARGE_INTEGER timeout;
        NTSTATUS st13 = STATUS_SUCCESS;

        yghv_trace("bm os guest multi start");
        g_os_guest_test_active = 1;
        g_os_guest_counter = 0;
        timeout.QuadPart = -60LL * 10000000LL;
        for (i = 1; i <= bm_cores; i++) {
            KeInitializeEvent(&g_os_guest_done_events[i], NotificationEvent, FALSE);
            st13 = PsCreateSystemThread(&threads[i], THREAD_ALL_ACCESS, NULL,
                                        NULL, NULL, yghv_os_guest_thread,
                                        (PVOID)(uintptr_t)i);
            if (!NT_SUCCESS(st13)) {
                LOG_ERROR("bm step 13: thread core %u failed 0x%x", i, st13);
                break;
            }
        }
        if (!NT_SUCCESS(st13)) {
            g_os_guest_test_active = 0;
            for (j = 1; j <= bm_cores; j++)
                if (threads[j]) ZwClose(threads[j]);
            return st13;
        }
        for (i = 1; i <= bm_cores; i++) {
            st13 = KeWaitForSingleObject(&g_os_guest_done_events[i], Executive,
                                         KernelMode, FALSE, &timeout);
            if (st13 == STATUS_TIMEOUT)
                break;
        }
        for (i = 1; i <= bm_cores; i++)
            if (threads[i]) ZwClose(threads[i]);
        g_os_guest_test_active = 0;
        for (i = 1; i <= bm_cores; i++) {
            if (!g_vcpus[i]) continue;
            yghv_trace_u64(i == 1 ? "os guest c1 exits" : "os guest c2 exits",
                           g_vcpus[i]->resident_exits);
        }
        yghv_trace_u64("os guest counter", g_os_guest_counter);
        yghv_trace("bm os guest multi done");
        if (st13 == STATUS_TIMEOUT)
            return st13;
        return g_os_guest_counter > 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }

    if (step == 14) {
        ULONG i;
        ULONG j;
        ULONG bm_cores = g_vcpu_count;
        HANDLE threads[SVM_MAX_CORES] = { 0 };
        LARGE_INTEGER timeout;
        NTSTATUS st14 = STATUS_SUCCESS;

        yghv_trace("bm os guest all start");
        g_os_guest_test_active = 1;
        g_os_guest_counter = 0;
        timeout.QuadPart = -120LL * 10000000LL;
        for (i = 0; i < bm_cores; i++) {
            KeInitializeEvent(&g_os_guest_done_events[i], NotificationEvent, FALSE);
            st14 = PsCreateSystemThread(&threads[i], THREAD_ALL_ACCESS, NULL,
                                        NULL, NULL, yghv_os_guest_thread,
                                        (PVOID)(uintptr_t)i);
            if (!NT_SUCCESS(st14)) {
                LOG_ERROR("bm step 14: thread core %u failed 0x%x", i, st14);
                break;
            }
        }
        if (!NT_SUCCESS(st14)) {
            g_os_guest_test_active = 0;
            for (j = 0; j < bm_cores; j++)
                if (threads[j]) ZwClose(threads[j]);
            return st14;
        }
        for (i = 0; i < bm_cores; i++) {
            st14 = KeWaitForSingleObject(&g_os_guest_done_events[i], Executive,
                                         KernelMode, FALSE, &timeout);
            if (st14 == STATUS_TIMEOUT)
                break;
        }
        for (i = 0; i < bm_cores; i++)
            if (threads[i]) ZwClose(threads[i]);
        g_os_guest_test_active = 0;
        for (i = 0; i < bm_cores; i++) {
            if (!g_vcpus[i]) continue;
            yghv_trace_u64("os guest exits", g_vcpus[i]->resident_exits);
        }
        yghv_trace_u64("os guest counter", g_os_guest_counter);
        yghv_trace("bm os guest all done");
        if (st14 == STATUS_TIMEOUT)
            return st14;
        return g_os_guest_counter > 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }

    if (step == 15) {
        HANDLE thread;
        NTSTATUS st15;
        LARGE_INTEGER timeout;

        yghv_trace("bm os seamless start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        g_os_guest_test_active = 1;
        g_os_guest_counter = 0;
        st15 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_seamless_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st15)) {
            LOG_ERROR("bm step 15: thread create failed 0x%x", st15);
            return st15;
        }
        timeout.QuadPart = -60LL * 10000000LL;
        st15 = KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                                     KernelMode, FALSE, &timeout);
        ZwClose(thread);
        g_os_guest_test_active = 0;
        yghv_trace_u64("os guest counter", g_os_guest_counter);
        yghv_trace("bm os seamless done");
        if (st15 == STATUS_TIMEOUT)
            return st15;
        return g_os_guest_counter > 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }

    if (step == 16) {
        ULONG i;
        ULONG j;
        ULONG bm_cores = g_vcpu_count;
        HANDLE threads[SVM_MAX_CORES] = { 0 };
        LARGE_INTEGER timeout;
        NTSTATUS st16 = STATUS_SUCCESS;

        yghv_trace("bm os seamless all start");
        g_os_guest_test_active = 1;
        g_os_guest_counter = 0;
        timeout.QuadPart = -120LL * 10000000LL;
        for (i = 0; i < bm_cores; i++) {
            KeInitializeEvent(&g_os_guest_done_events[i], NotificationEvent, FALSE);
            st16 = PsCreateSystemThread(&threads[i], THREAD_ALL_ACCESS, NULL,
                                        NULL, NULL,
                                        yghv_os_guest_seamless_thread,
                                        (PVOID)(uintptr_t)i);
            if (!NT_SUCCESS(st16)) {
                LOG_ERROR("bm step 16: thread core %u failed 0x%x", i, st16);
                break;
            }
        }
        if (!NT_SUCCESS(st16)) {
            g_os_guest_test_active = 0;
            for (j = 0; j < bm_cores; j++)
                if (threads[j]) ZwClose(threads[j]);
            return st16;
        }
        for (i = 0; i < bm_cores; i++) {
            st16 = KeWaitForSingleObject(&g_os_guest_done_events[i], Executive,
                                         KernelMode, FALSE, &timeout);
            if (st16 == STATUS_TIMEOUT)
                break;
        }
        for (i = 0; i < bm_cores; i++)
            if (threads[i]) ZwClose(threads[i]);
        g_os_guest_test_active = 0;
        for (i = 0; i < bm_cores; i++) {
            if (!g_vcpus[i]) continue;
            yghv_trace_u64("os seamless exits", g_vcpus[i]->resident_exits);
        }
        yghv_trace_u64("os guest counter", g_os_guest_counter);
        yghv_trace("bm os seamless all done");
        if (st16 == STATUS_TIMEOUT)
            return st16;
        return g_os_guest_counter > 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }

    if (step == 17) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st17;

        yghv_trace("bm os resident start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st17 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st17)) {
            LOG_ERROR("bm step 17: thread create failed 0x%x", st17);
            g_os_resident_mode = FALSE;
            g_os_resident_log_active = FALSE;
            return st17;
        }
        st17 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st17)) {
            LOG_ERROR("bm step 17: alive thread create failed 0x%x", st17);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_resident_log_active = FALSE;
            return st17;
        }
        st17 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st17)) {
            LOG_ERROR("bm step 17: watchdog thread create failed 0x%x", st17);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_resident_log_active = FALSE;
            return st17;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident running");
        return STATUS_SUCCESS;
    }

    if (step == 18) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st18;

        yghv_trace("bm os resident spin start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = FALSE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st18 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_spin_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st18)) {
            LOG_ERROR("bm step 18: thread create failed 0x%x", st18);
            g_os_resident_mode = FALSE;
            g_os_resident_log_active = FALSE;
            return st18;
        }
        st18 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st18)) {
            LOG_ERROR("bm step 18: alive thread create failed 0x%x", st18);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_resident_log_active = FALSE;
            return st18;
        }
        st18 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st18)) {
            LOG_ERROR("bm step 18: watchdog thread create failed 0x%x", st18);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_resident_log_active = FALSE;
            return st18;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident spin running");
        return STATUS_SUCCESS;
    }

    if (step == 19) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st19;

        yghv_trace("bm os resident spin intr start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st19 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_spin_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st19)) {
            LOG_ERROR("bm step 19: thread create failed 0x%x", st19);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_resident_log_active = FALSE;
            return st19;
        }
        st19 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st19)) {
            LOG_ERROR("bm step 19: alive thread create failed 0x%x", st19);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_resident_log_active = FALSE;
            return st19;
        }
        st19 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st19)) {
            LOG_ERROR("bm step 19: watchdog thread create failed 0x%x", st19);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_resident_log_active = FALSE;
            return st19;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident spin intr running");
        return STATUS_SUCCESS;
    }

    if (step == 20) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st20;

        yghv_trace("bm os resident spin host-isr start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st20 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_spin_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st20)) {
            LOG_ERROR("bm step 20: thread create failed 0x%x", st20);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            return st20;
        }
        st20 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st20)) {
            LOG_ERROR("bm step 20: alive thread create failed 0x%x", st20);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            return st20;
        }
        st20 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st20)) {
            LOG_ERROR("bm step 20: watchdog thread create failed 0x%x", st20);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            return st20;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident spin host-isr running");
        return STATUS_SUCCESS;
    }

    if (step == 21) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st21;

        yghv_trace("bm os resident spin inject start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_inject_intr = TRUE;
        g_os_guest_host_isr = FALSE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st21 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_spin_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st21)) {
            LOG_ERROR("bm step 21: thread create failed 0x%x", st21);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_inject_intr = FALSE;
            g_os_resident_log_active = FALSE;
            return st21;
        }
        st21 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st21)) {
            LOG_ERROR("bm step 21: alive thread create failed 0x%x", st21);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_inject_intr = FALSE;
            g_os_resident_log_active = FALSE;
            return st21;
        }
        st21 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st21)) {
            LOG_ERROR("bm step 21: watchdog thread create failed 0x%x", st21);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_inject_intr = FALSE;
            g_os_resident_log_active = FALSE;
            return st21;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident spin inject running");
        return STATUS_SUCCESS;
    }

    if (step == 22) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st22;

        yghv_trace("bm os resident block host-isr start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = FALSE;
        g_os_guest_inject_intr = FALSE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st22 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st22)) {
            LOG_ERROR("bm step 22: thread create failed 0x%x", st22);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            return st22;
        }
        st22 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st22)) {
            LOG_ERROR("bm step 22: alive thread create failed 0x%x", st22);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            return st22;
        }
        st22 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st22)) {
            LOG_ERROR("bm step 22: watchdog thread create failed 0x%x", st22);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            return st22;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident block host-isr running");
        return STATUS_SUCCESS;
    }

    if (step == 23) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st23;

        yghv_trace("bm os resident block delay start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st23 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_delay_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st23)) {
            LOG_ERROR("bm step 23: thread create failed 0x%x", st23);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            return st23;
        }
        st23 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st23)) {
            LOG_ERROR("bm step 23: alive thread create failed 0x%x", st23);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            return st23;
        }
        st23 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st23)) {
            LOG_ERROR("bm step 23: watchdog thread create failed 0x%x", st23);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            return st23;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident block delay running");
        return STATUS_SUCCESS;
    }

    if (step == 24) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st24;

        yghv_trace("bm os resident block delay quiet start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st24 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_delay_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st24)) {
            LOG_ERROR("bm step 24: thread create failed 0x%x", st24);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            return st24;
        }
        st24 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st24)) {
            LOG_ERROR("bm step 24: alive thread create failed 0x%x", st24);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            return st24;
        }
        st24 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st24)) {
            LOG_ERROR("bm step 24: watchdog thread create failed 0x%x", st24);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            return st24;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident block delay quiet running");
        return STATUS_SUCCESS;
    }

    if (step == 25) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st25;
        svm_vcpu_t *av = svm_core_get_vcpu(1);

        yghv_trace("bm os resident block avic start");
        if (!av || g_vcpu_count <= 1) {
            LOG_ERROR("bm step 25: vcpu1 unavailable");
            return STATUS_NOT_FOUND;
        }
        st25 = yghv_avic_prepare(av, 1);
        if (!NT_SUCCESS(st25)) {
            LOG_ERROR("bm step 25: avic prepare failed 0x%x", st25);
            return st25;
        }

        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st25 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_delay_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st25)) {
            LOG_ERROR("bm step 25: thread create failed 0x%x", st25);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st25;
        }
        st25 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st25)) {
            LOG_ERROR("bm step 25: alive thread create failed 0x%x", st25);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st25;
        }
        st25 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st25)) {
            LOG_ERROR("bm step 25: watchdog thread create failed 0x%x", st25);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st25;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident block avic running");
        return STATUS_SUCCESS;
    }

    if (step == 26) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st26;
        svm_vcpu_t *av = svm_core_get_vcpu(1);

        yghv_trace("bm os resident avic spin start");
        if (!av || g_vcpu_count <= 1) {
            LOG_ERROR("bm step 26: vcpu1 unavailable");
            return STATUS_NOT_FOUND;
        }
        st26 = yghv_avic_prepare(av, 1);
        if (!NT_SUCCESS(st26)) {
            LOG_ERROR("bm step 26: avic prepare failed 0x%x", st26);
            return st26;
        }

        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st26 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_spin_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st26)) {
            LOG_ERROR("bm step 26: thread create failed 0x%x", st26);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st26;
        }
        st26 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st26)) {
            LOG_ERROR("bm step 26: alive thread create failed 0x%x", st26);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st26;
        }
        st26 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st26)) {
            LOG_ERROR("bm step 26: watchdog thread create failed 0x%x", st26);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st26;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident avic spin running");
        return STATUS_SUCCESS;
    }

    if (step == 27) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st27;
        svm_vcpu_t *av = svm_core_get_vcpu(1);

        yghv_trace("bm os resident block avic direct start");
        if (!av || g_vcpu_count <= 1) {
            LOG_ERROR("bm step 27: vcpu1 unavailable");
            return STATUS_NOT_FOUND;
        }
        st27 = yghv_avic_prepare(av, 1);
        if (!NT_SUCCESS(st27)) {
            LOG_ERROR("bm step 27: avic prepare failed 0x%x", st27);
            return st27;
        }

        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = FALSE;
        g_os_guest_host_isr = FALSE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st27 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_delay_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st27)) {
            LOG_ERROR("bm step 27: thread create failed 0x%x", st27);
            g_os_resident_mode = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st27;
        }
        st27 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st27)) {
            LOG_ERROR("bm step 27: alive thread create failed 0x%x", st27);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st27;
        }
        st27 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st27)) {
            LOG_ERROR("bm step 27: watchdog thread create failed 0x%x", st27);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st27;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident block avic direct running");
        return STATUS_SUCCESS;
    }

    if (step == 28) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st28;
        svm_vcpu_t *av = svm_core_get_vcpu(1);

        yghv_trace("bm os resident avic irr spin start");
        if (!av || g_vcpu_count <= 1) {
            LOG_ERROR("bm step 28: vcpu1 unavailable");
            return STATUS_NOT_FOUND;
        }
        st28 = yghv_avic_prepare(av, 1);
        if (!NT_SUCCESS(st28)) {
            LOG_ERROR("bm step 28: avic prepare failed 0x%x", st28);
            return st28;
        }

        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_avic_irr_inject = TRUE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st28 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_spin_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st28)) {
            LOG_ERROR("bm step 28: thread create failed 0x%x", st28);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st28;
        }
        st28 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st28)) {
            LOG_ERROR("bm step 28: alive thread create failed 0x%x", st28);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st28;
        }
        st28 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st28)) {
            LOG_ERROR("bm step 28: watchdog thread create failed 0x%x", st28);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st28;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident avic irr spin running");
        return STATUS_SUCCESS;
    }

    if (step == 29) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st29;
        svm_vcpu_t *av = svm_core_get_vcpu(1);

        yghv_trace("bm os resident avic scan spin start");
        if (!av || g_vcpu_count <= 1) {
            LOG_ERROR("bm step 29: vcpu1 unavailable");
            return STATUS_NOT_FOUND;
        }
        st29 = yghv_avic_prepare(av, 1);
        if (!NT_SUCCESS(st29)) {
            LOG_ERROR("bm step 29: avic prepare failed 0x%x", st29);
            return st29;
        }

        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_avic_irr_inject = TRUE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st29 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_spin_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st29)) {
            LOG_ERROR("bm step 29: thread create failed 0x%x", st29);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st29;
        }
        st29 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st29)) {
            LOG_ERROR("bm step 29: alive thread create failed 0x%x", st29);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st29;
        }
        st29 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st29)) {
            LOG_ERROR("bm step 29: watchdog thread create failed 0x%x", st29);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st29;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident avic scan spin running");
        return STATUS_SUCCESS;
    }

    if (step == 30) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st30;
        svm_vcpu_t *av = svm_core_get_vcpu(1);

        yghv_trace("bm os resident avic scan block start");
        if (!av || g_vcpu_count <= 1) {
            LOG_ERROR("bm step 30: vcpu1 unavailable");
            return STATUS_NOT_FOUND;
        }
        st30 = yghv_avic_prepare(av, 1);
        if (!NT_SUCCESS(st30)) {
            LOG_ERROR("bm step 30: avic prepare failed 0x%x", st30);
            return st30;
        }

        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_avic_irr_inject = TRUE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st30 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_delay_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st30)) {
            LOG_ERROR("bm step 30: thread create failed 0x%x", st30);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st30;
        }
        st30 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st30)) {
            LOG_ERROR("bm step 30: alive thread create failed 0x%x", st30);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st30;
        }
        st30 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st30)) {
            LOG_ERROR("bm step 30: watchdog thread create failed 0x%x", st30);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st30;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident avic scan block running");
        return STATUS_SUCCESS;
    }

    if (step == 31) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st31;
        svm_vcpu_t *av = svm_core_get_vcpu(1);

        yghv_trace("bm os resident avic stack block start");
        if (!av || g_vcpu_count <= 1) {
            LOG_ERROR("bm step 31: vcpu1 unavailable");
            return STATUS_NOT_FOUND;
        }
        st31 = yghv_avic_prepare(av, 1);
        if (!NT_SUCCESS(st31)) {
            LOG_ERROR("bm step 31: avic prepare failed 0x%x", st31);
            return st31;
        }

        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_avic_irr_inject = TRUE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st31 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_delay_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st31)) {
            LOG_ERROR("bm step 31: thread create failed 0x%x", st31);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st31;
        }
        st31 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st31)) {
            LOG_ERROR("bm step 31: alive thread create failed 0x%x", st31);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st31;
        }
        st31 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st31)) {
            LOG_ERROR("bm step 31: watchdog thread create failed 0x%x", st31);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st31;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident avic stack block running");
        return STATUS_SUCCESS;
    }

    if (step == 32) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st32;
        svm_vcpu_t *av = svm_core_get_vcpu(1);

        yghv_trace("bm os resident avic eoi spin start");
        if (!av || g_vcpu_count <= 1) {
            LOG_ERROR("bm step 32: vcpu1 unavailable");
            return STATUS_NOT_FOUND;
        }
        av->resident_index = 1;
        st32 = yghv_avic_prepare(av, 1);
        if (!NT_SUCCESS(st32)) {
            LOG_ERROR("bm step 32: avic prepare failed 0x%x", st32);
            return st32;
        }
        st32 = yghv_avic_install_eoi_idt(av);
        if (!NT_SUCCESS(st32)) {
            LOG_ERROR("bm step 32: eoi idt install failed 0x%x", st32);
            yghv_avic_cleanup(av);
            return st32;
        }

        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = FALSE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_avic_irr_inject = TRUE;
        g_os_guest_avic_eoi_only = TRUE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st32 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_spin_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st32)) {
            LOG_ERROR("bm step 32: thread create failed 0x%x", st32);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_avic_eoi_only = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_restore_idt(av);
            yghv_avic_cleanup(av);
            return st32;
        }
        st32 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st32)) {
            LOG_ERROR("bm step 32: alive thread create failed 0x%x", st32);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_avic_eoi_only = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_restore_idt(av);
            yghv_avic_cleanup(av);
            return st32;
        }
        st32 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st32)) {
            LOG_ERROR("bm step 32: watchdog thread create failed 0x%x", st32);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_avic_eoi_only = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_restore_idt(av);
            yghv_avic_cleanup(av);
            return st32;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident avic eoi spin running");
        return STATUS_SUCCESS;
    }

    if (step == 33) {
        HANDLE thread;
        HANDLE alive;
        HANDLE watchdog;
        NTSTATUS st33;
        svm_vcpu_t *av = svm_core_get_vcpu(1);
        uint32_t lvtt;
        uint32_t clock_vector;

        yghv_trace("bm os resident avic timer block start");
        if (!av || g_vcpu_count <= 1) {
            LOG_ERROR("bm step 33: vcpu1 unavailable");
            return STATUS_NOT_FOUND;
        }
        av->resident_index = 1;
        st33 = yghv_avic_prepare(av, 1);
        if (!NT_SUCCESS(st33)) {
            LOG_ERROR("bm step 33: avic prepare failed 0x%x", st33);
            return st33;
        }
        svm_avic_timer_init(av);
        lvtt = READ_REGISTER_ULONG(
            (PULONG)((ULONG_PTR)av->avic_host_apic_va + APIC_OFFSET_LVTT));
        clock_vector = lvtt & 0xFF;
        svm_avic_start_timer(av, clock_vector, 10);
        WRITE_REGISTER_ULONG(
            (PULONG)((ULONG_PTR)av->avic_host_apic_va + APIC_OFFSET_TMICT), 0);
        WRITE_REGISTER_ULONG(
            (PULONG)((ULONG_PTR)av->avic_host_apic_va + APIC_OFFSET_TDCR), 0);

        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_guest_avic_irr_inject = TRUE;
        g_os_guest_avic_eoi_only = FALSE;
        g_os_guest_avic_timer_emu = TRUE;
        g_os_guest_delay_quiet = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        st33 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_delay_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st33)) {
            LOG_ERROR("bm step 33: thread create failed 0x%x", st33);
            if (av->avic_timer_initialized) {
                KeCancelTimer(&av->avic_timer);
                av->avic_timer_armed = FALSE;
            }
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_avic_timer_emu = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st33;
        }
        st33 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st33)) {
            LOG_ERROR("bm step 33: alive thread create failed 0x%x", st33);
            if (av->avic_timer_initialized) {
                KeCancelTimer(&av->avic_timer);
                av->avic_timer_armed = FALSE;
            }
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_avic_timer_emu = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st33;
        }
        st33 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st33)) {
            LOG_ERROR("bm step 33: watchdog thread create failed 0x%x", st33);
            if (av->avic_timer_initialized) {
                KeCancelTimer(&av->avic_timer);
                av->avic_timer_armed = FALSE;
            }
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_guest_avic_irr_inject = FALSE;
            g_os_guest_avic_timer_emu = FALSE;
            g_os_guest_delay_quiet = FALSE;
            g_os_resident_log_active = FALSE;
            yghv_avic_cleanup(av);
            return st33;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(watchdog);
        yghv_trace("bm os resident avic timer block running");
        return STATUS_SUCCESS;
    }

    if (step == 99) {
        ULONG i;
        ULONG j;
        ULONG bm_cores = g_vcpu_count;
        HANDLE threads[SVM_MAX_CORES] = { 0 };
        HANDLE alive = NULL;
        HANDLE watchdog = NULL;
        NTSTATUS st99 = STATUS_SUCCESS;

        yghv_trace("bm os allcore resident start");
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        InterlockedExchange(&g_v99_allcore_ready, 0);
        InterlockedExchange(&g_v99_allcore_go, 0);
        InterlockedExchange(&g_v99_allcore_abort, 0);
        InterlockedExchange(&g_v99_allcore_online, (LONG)bm_cores);

        for (i = 0; i < bm_cores; i++) {
            KeInitializeEvent(&g_os_guest_done_events[i], NotificationEvent, FALSE);
            st99 = PsCreateSystemThread(&threads[i], THREAD_ALL_ACCESS, NULL,
                                        NULL, NULL,
                                        yghv_os_guest_allcore_thread,
                                        (PVOID)(uintptr_t)i);
            if (!NT_SUCCESS(st99)) {
                LOG_ERROR("bm step 99: thread core %u failed 0x%x", i, st99);
                break;
            }
        }
        if (!NT_SUCCESS(st99)) {
            InterlockedExchange(&g_v99_allcore_abort, 1);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            for (j = 0; j < bm_cores; j++)
                if (threads[j]) ZwClose(threads[j]);
            return st99;
        }

        st99 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st99)) {
            LOG_ERROR("bm step 99: alive thread create failed 0x%x", st99);
            InterlockedExchange(&g_v99_allcore_abort, 1);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            for (j = 0; j < bm_cores; j++)
                if (threads[j]) ZwClose(threads[j]);
            return st99;
        }
        st99 = PsCreateSystemThread(&watchdog, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_watchdog_thread, NULL);
        if (!NT_SUCCESS(st99)) {
            LOG_ERROR("bm step 99: watchdog thread create failed 0x%x", st99);
            InterlockedExchange(&g_v99_allcore_abort, 1);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            for (j = 0; j < bm_cores; j++)
                if (threads[j]) ZwClose(threads[j]);
            return st99;
        }

        InterlockedExchange(&g_v99_allcore_go, 1);
        for (i = 0; i < bm_cores; i++) {
            KeWaitForSingleObject(&g_os_guest_done_events[i], Executive,
                                  KernelMode, FALSE, NULL);
        }
        ZwClose(alive);
        ZwClose(watchdog);
        for (i = 0; i < bm_cores; i++)
            if (threads[i]) ZwClose(threads[i]);
        yghv_trace("bm os allcore resident running");
        return STATUS_SUCCESS;
    }

    if (step == 100) {
        HANDLE thread;
        HANDLE alive;
        HANDLE monitor;
        NTSTATUS st100;
        svm_vcpu_t *mv;

        yghv_trace("bm os resident freeze-site start");
        KeInitializeEvent(&g_os_guest_done_events[1], NotificationEvent, FALSE);
        KeInitializeEvent(&g_os_resident_stop_event, NotificationEvent, FALSE);
        g_os_guest_intr_intercept = TRUE;
        g_os_guest_host_isr = TRUE;
        g_os_guest_inject_intr = FALSE;
        g_os_resident_mode = TRUE;
        g_os_resident_log_active = TRUE;
        g_v100_monitor_active = TRUE;
        g_v100_guest_entered = FALSE;
        /* step100 isolation: guest continuation performs no file I/O
           (no yghv_trace), leaving only a pure KeDelayExecutionThread
           block, to separate "any guest-mode scheduler switch" from
           "blocking file write triggers the 0x139". */
        g_os_guest_delay_quiet = TRUE;
        mv = svm_core_get_vcpu(1);
        if (mv) {
            RtlZeroMemory(mv->v100_ring, sizeof(mv->v100_ring));
            mv->v100_seq = 0;
        }
        st100 = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_os_guest_resident_delay_thread,
                                    (PVOID)(uintptr_t)1);
        if (!NT_SUCCESS(st100)) {
            LOG_ERROR("bm step 100: thread create failed 0x%x", st100);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            g_v100_monitor_active = FALSE;
            return st100;
        }
        st100 = PsCreateSystemThread(&alive, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_resident_alive_thread, NULL);
        if (!NT_SUCCESS(st100)) {
            LOG_ERROR("bm step 100: alive thread create failed 0x%x", st100);
            ZwClose(thread);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            g_v100_monitor_active = FALSE;
            return st100;
        }
        st100 = PsCreateSystemThread(&monitor, THREAD_ALL_ACCESS, NULL, NULL,
                                    NULL, yghv_v100_monitor_thread, NULL);
        if (!NT_SUCCESS(st100)) {
            LOG_ERROR("bm step 100: monitor thread create failed 0x%x", st100);
            ZwClose(thread);
            ZwClose(alive);
            g_os_resident_mode = FALSE;
            g_os_guest_intr_intercept = FALSE;
            g_os_guest_host_isr = FALSE;
            g_os_resident_log_active = FALSE;
            g_v100_monitor_active = FALSE;
            return st100;
        }
        KeWaitForSingleObject(&g_os_guest_done_events[1], Executive,
                              KernelMode, FALSE, NULL);
        ZwClose(thread);
        ZwClose(alive);
        ZwClose(monitor);
        yghv_trace("bm os resident freeze-site running");
        return STATUS_SUCCESS;
    }

    v->regs.rcx = g_vmmcall_auth_cookie;
    v->regs.rax = 0;
    v->resident_index = 0;
    v->vmcb->control.general1_intercepts = 0;
    v->vmcb->control.general2_intercepts =
        INTR_GEN2(SVM_INTERCEPT_VMRUN) | INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    v->vmcb->control.exception_intercepts = 0;
    if (step >= 2) {
        v->vmcb->control.np_enable = SVM_NP_ENABLE;
        v->vmcb->control.ncr3 = g_npt.pml4_pa;
    } else {
        v->vmcb->control.np_enable = 0;
        v->vmcb->control.ncr3 = 0;
    }
    if (step == 4 || step == 5 || step == 7)
        v->vmcb->control.general1_intercepts = 0;
    else if (step >= 3)
        v->vmcb->control.general1_intercepts = INTERCEPT_CPUID;
    if (step == 5)
        v->vmcb->control.exception_intercepts = (1ULL << 1);

    if (step == 4) {
        npf_page = ExAllocatePoolWithTag(NonPagedPool, HV_PAGE_SIZE, YGHV_TAG);
        if (!npf_page) {
            LOG_ERROR("bm step 4: alloc failed");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(npf_page, HV_PAGE_SIZE);
        npf_pa = MmGetPhysicalAddress(npf_page).QuadPart;
        st = npt_split_2mb_to_4kb(&g_npt, npf_pa);
        if (!st)
            st = npt_set_page_perm(&g_npt, npf_pa, 0);
        if (st) {
            LOG_ERROR("bm step 4: setup failed 0x%x", st);
            ExFreePoolWithTag(npf_page, YGHV_TAG);
            return st;
        }
        g_npt_test_pa = npf_pa;
        g_npt_test_active = 1;
        v->regs.rdi = (uint64_t)npf_page;
        v->vmcb->state.rip = (uint64_t)svm_trampoline_test_npt_guest;
        yghv_trace_u64("bm npf pa", npf_pa);
    } else if (step == 7) {
        uint64_t dummy = (uint64_t)yghv_hook_test_dummy;
        st = yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
        if (!st)
            st = yghv_protect_install_hook(0, dummy);
        if (st) {
            LOG_ERROR("bm step 7: hook setup failed 0x%x", st);
            return st;
        }
        v->regs.rsi = dummy;
        v->vmcb->state.rip = (uint64_t)svm_trampoline_test_hook_guest;
        yghv_trace_u64("bm hook va", dummy);
    } else if (step == 5) {
        prot_page = ExAllocatePoolWithTag(NonPagedPool, HV_PAGE_SIZE, YGHV_TAG);
        if (!prot_page) {
            LOG_ERROR("bm step 5: alloc failed");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(prot_page, HV_PAGE_SIZE);
        st = yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
        if (!st)
            st = yghv_protect_add_page((uint64_t)prot_page);
        if (!st)
            st = yghv_protect_start();
        if (st) {
            LOG_ERROR("bm step 5: setup failed 0x%x", st);
            ExFreePoolWithTag(prot_page, YGHV_TAG);
            return st;
        }
        v->regs.rdi = (uint64_t)prot_page;
        v->vmcb->state.rip = (uint64_t)svm_trampoline_test_prot_write;
        yghv_trace_u64("bm prot va", (uint64_t)prot_page);
    } else if (step == 1 || step == 2) {
        v->vmcb->state.rip = (uint64_t)svm_trampoline_test_min_guest;
    } else if (step >= 3) {
        v->vmcb->state.rip = (uint64_t)svm_trampoline_test_cpuid_guest;
    }
    v->vmcb->state.rax = 0;

    yghv_trace("bm vmrun");
    svm_core_enter_resident_current(0);
    if (step == 4) {
        g_npt_test_active = 0;
        npt_set_page_perm(&g_npt, npf_pa, NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
        ExFreePoolWithTag(npf_page, YGHV_TAG);
    }
    if (step == 5) {
        yghv_protect_stop();
        yghv_protect_remove_page((uint64_t)prot_page);
        ExFreePoolWithTag(prot_page, YGHV_TAG);
    }
    if (step == 7) {
        yghv_trace_u64("bm hook rdx", v->regs.rdx);
        yghv_protect_remove_hook(0);
    }
    yghv_trace("bm exit");
    yghv_trace_u64("bm rax", v->regs.rax);
    st = STATUS_SUCCESS;
    return st;
}

static VOID yghv_hook_rendezvous_thread(PVOID context) {
    void *page;
    uint64_t target_va;
    LARGE_INTEGER delay;
    ULONG pid;
    NTSTATUS st_install, st_remove;
    (void)context;

    delay.QuadPart = -8 * 10000000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    page = ExAllocatePoolWithTag(NonPagedPool, HV_PAGE_SIZE * 2, YGHV_TAG);
    if (!page) {
        LOG_ERROR("hook rendezvous test: alloc failed");
        PsTerminateSystemThread(STATUS_INSUFFICIENT_RESOURCES);
        return;
    }
    RtlZeroMemory(page, HV_PAGE_SIZE * 2);
    target_va = ((uint64_t)page + HV_PAGE_SIZE - 1) &
                ~(uint64_t)(HV_PAGE_SIZE - 1);
    RtlFillMemory((void *)target_va, 16, 0x90);
    *(uint8_t *)(target_va + 16) = 0xC3;

    yghv_protect_get_state(NULL, &pid, NULL);
    if (pid == 0)
        yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());

    st_install = yghv_protect_install_hook(1, target_va);
    LOG_ERROR("hook rendezvous test: install rc=0x%x", st_install);
    delay.QuadPart = -1000 * 10000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    st_remove = yghv_protect_remove_hook(1);
    LOG_ERROR("hook rendezvous test: remove rc=0x%x", st_remove);
    LOG_ERROR("hook rendezvous test: %s",
        (NT_SUCCESS(st_install) && NT_SUCCESS(st_remove)) ? "PASS" : "FAIL");

    ExFreePoolWithTag(page, YGHV_TAG);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static void yghv_hook_rendezvous_join(void) {
    PETHREAD thread_obj = NULL;
    NTSTATUS status;
    if (!g_hook_rendezvous_thread)
        return;
    status = ObReferenceObjectByHandle(
        g_hook_rendezvous_thread, THREAD_ALL_ACCESS, *PsThreadType,
        KernelMode, (PVOID *)&thread_obj, NULL);
    if (NT_SUCCESS(status) && thread_obj) {
        KeWaitForSingleObject(thread_obj, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(thread_obj);
    } else {
        LOG_ERROR("hook rendezvous: ObReferenceObjectByHandle failed 0x%x",
            status);
    }
    ZwClose(g_hook_rendezvous_thread);
    g_hook_rendezvous_thread = NULL;
}

static NTSTATUS yghv_make_guest_code_executable(void) {
    uint64_t pa = MmGetPhysicalAddress(g_guest_code_page).QuadPart;
    NTSTATUS st = npt_split_2mb_to_4kb(&g_npt, pa);
    if (st) return st;
    return npt_set_page_perm(&g_npt, pa, NPT_PERM_PRESENT | NPT_PERM_WRITABLE);
}

static void yghv_init_auth_cookie(void) {
    LARGE_INTEGER st, ticks;
    KeQuerySystemTime(&st);
    KeQueryTickCount(&ticks);
    g_vmmcall_auth_cookie = (uint64_t)st.QuadPart ^ (uint64_t)ticks.QuadPart;
    if (!g_vmmcall_auth_cookie)
        g_vmmcall_auth_cookie = 0x59484756ULL;
}

void DriverUnload(struct _DRIVER_OBJECT *d) {
    yghv_hook_rendezvous_join();
    g_npt_test_active = 0;
    svm_core_stop_all_residents();
    svm_core_wait_all_stopped(g_vcpu_count);
    yghv_control_device_cleanup(d);
    KeSetSystemAffinityThread((KAFFINITY)1);
    yghv_protect_cleanup();
    if (g_guest_code_page)
    if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
    g_guest_code_page = NULL;
    if (g_resident_workload_page) MmFreeContiguousMemory(g_resident_workload_page);
    g_resident_workload_page = NULL;
    npt_cleanup(&g_npt);
    svm_core_cleanup();
    KeRevertToUserAffinityThread();
    LOG_INFO("DriverUnload");
}

NTSTATUS DriverEntry(struct _DRIVER_OBJECT*d,PUNICODE_STRING r){
    (void)d;(void)r;
    int sv;
    void *npt_test_buf = NULL;
    ULONG i;
    ULONG online;

    d->DriverUnload = DriverUnload;
    d->Flags |= DRVO_LEGACY_DRIVER;
    LOG_INFO("DriverEntry start");

    KeSetSystemAffinityThread((KAFFINITY)1);
    yghv_init_auth_cookie();
    yghv_trace_init();
    sv = yghv_protect_init();
    if (sv) {
        LOG_ERROR("yghv_protect_init failed 0x%x", sv);
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return sv;
    }
    yghv_trace("entry");
    LOG_ERROR("driver entry pre");

    /* The direct-map pages returned by MmAllocateContiguousMemory are NX in
       Windows, so the test guest keeps executing from the driver image. */
    g_guest_hb_va = (uint64_t)svm_trampoline_test_guest;
    g_guest_npt_va = (uint64_t)svm_trampoline_test_npt_guest;
    LOG_ERROR("guest code driver va hb=0x%llx npt=0x%llx", g_guest_hb_va, g_guest_npt_va);

    sv = svm_core_init();
    if (sv) {
        LOG_ERROR("svm_core_init failed 0x%x", sv);
        yghv_trace("fail svm_core_init");
        yghv_trace_close();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        KeRevertToUserAffinityThread();
        return (NTSTATUS)sv;
    }
    LOG_INFO("svm_core_init ok");
    yghv_trace("svm_core_init ok");
    g_control_cr3 = g_vcpus[0]->vmcb->state.cr3;
    LOG_ERROR("control cr3=0x%llx", g_control_cr3);

    online = KeQueryActiveProcessorCount(NULL);
    if (online > SVM_MAX_CORES) online = SVM_MAX_CORES;
    LOG_ERROR("active processors: %u", online);
    yghv_trace("cores counted");

    for (i = 1; i < online; i++) {
        sv = svm_alloc_vcpu(i, &g_vcpus[i]);
        if (sv) {
            LOG_ERROR("svm_alloc_vcpu core %u failed 0x%x", i, sv);
            svm_core_cleanup();
            if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
            g_guest_code_page = NULL;
            yghv_trace_close();
            KeRevertToUserAffinityThread();
            return (NTSTATUS)sv;
        }
        g_vcpu_count++;
    }

    KeIpiGenericCall(svm_core_ipi_prepare_vcpu, 0);
    yghv_trace("ipi prepare done");
    for (i = 0; i < online; i++) {
        if (!g_vcpus[i]) continue;
        g_vcpus[i]->regs.rcx = g_vmmcall_auth_cookie;
        g_vcpus[i]->vmcb->state.rip = g_guest_hb_va;
    }

    sv = npt_init(&g_npt, 0);
    if (!sv)
        sv = yghv_npt_map_ram(&g_npt);
    if (sv) {
        LOG_ERROR("npt_init/map_ram failed 0x%x", sv);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return (NTSTATUS)sv;
    }
    LOG_INFO("npt_init ok");
    yghv_trace("npt_init ok");

#if YGHV_R1_NPT_UNIT_TEST
    sv = yghv_r1_npt_unit_test();
    if (sv) {
        LOG_ERROR("r1 npt unit test failed 0x%x", sv);
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return sv;
    }
#endif

    yghv_trace("map ram n/a");

    yghv_exclude_hv_private(d);
    yghv_trace("exclude private ok");
    yghv_trace("guest code exec n/a");

    for (i = 0; i < online; i++) {
        sv = svm_core_set_npt(i, g_npt.pml4_pa);
        if (sv) {
            LOG_ERROR("svm_core_set_npt core %u failed 0x%x", i, sv);
            npt_cleanup(&g_npt);
            svm_core_cleanup();
            if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
            g_guest_code_page = NULL;
            yghv_trace_close();
            KeRevertToUserAffinityThread();
            return (NTSTATUS)sv;
        }
    }
    LOG_ERROR("NPT enabled for %u cores: pml4=0x%llx", online, g_npt.pml4_pa);
    yghv_trace("npt set ok");

#if YGHV_BAREMETAL_NO_RESIDENT
    /* Bare-metal smoke: this host hard-freezes on the first VMRUN, so validate
       only non-resident paths (r1 NPT API, hook boundary, hook install/remove)
       and hand off to the control device. */
    sv = yghv_r1_npt_unit_test();
    if (!sv)
        sv = yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
    if (!sv)
        sv = yghv_hook_boundary_test();
    if (!sv)
        sv = yghv_hook_test();
    if (sv) {
        LOG_ERROR("bare-metal smoke: non-resident tests failed 0x%x", sv);
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return sv;
    }
    sv = yghv_control_device_init(d);
    if (sv) {
        LOG_ERROR("bare-metal smoke: control device init failed 0x%x", sv);
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return sv;
    }
    LOG_ERROR("bare-metal smoke: non-resident tests PASS, control device ready");
    KeRevertToUserAffinityThread();
    return STATUS_SUCCESS;
#endif

#if YGHV_BAREMETAL_STEP >= 1
    {
        NTSTATUS st2 = yghv_baremetal_step_test(YGHV_BAREMETAL_STEP);
        yghv_trace("bm done");
        if (YGHV_BAREMETAL_STEP < 17 || !g_os_resident_log_active)
            yghv_trace_close();
        KeRevertToUserAffinityThread();
        return st2;
    }
#endif

#if !YGHV_R1_SKIP_NPT_TEST
    /* Single-core NPT permission test first. */
    npt_test_buf = MmAllocateContiguousMemory(
        HV_LARGE_PAGE_SIZE * 2,
        (PHYSICAL_ADDRESS){ .QuadPart = -1 });
    if (!npt_test_buf) {
        LOG_ERROR("npt_test_buf allocation failed");
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(npt_test_buf, HV_LARGE_PAGE_SIZE * 2);
    uint64_t buf_pa = MmGetPhysicalAddress(npt_test_buf).QuadPart;
    uint64_t page_pa = (buf_pa + HV_LARGE_PAGE_SIZE - 1) & ~(uint64_t)(HV_LARGE_PAGE_SIZE - 1);
    if (page_pa + HV_LARGE_PAGE_SIZE > buf_pa + HV_LARGE_PAGE_SIZE * 2) {
        LOG_ERROR("npt_test_buf has no aligned 2MB slot");
        MmFreeContiguousMemory(npt_test_buf);
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    uint64_t test_va = (uint64_t)npt_test_buf + (page_pa - buf_pa);
    g_npt_test_pa = page_pa;
    g_npt_test_active = 1;
    LOG_ERROR("NPT test page: va=0x%llx pa=0x%llx", test_va, page_pa);
    yghv_trace("test page ready");

    sv = npt_split_2mb_to_4kb(&g_npt, page_pa);
    if (sv) {
        LOG_ERROR("npt_split test page failed 0x%x", sv);
        MmFreeContiguousMemory(npt_test_buf);
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return (NTSTATUS)sv;
    }
    sv = npt_set_page_perm(&g_npt, page_pa, 0);
    if (sv) {
        LOG_ERROR("npt_set_page_perm failed 0x%x", sv);
        MmFreeContiguousMemory(npt_test_buf);
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return (NTSTATUS)sv;
    }
    svm_vcpu_t *npt_vcpu = svm_core_get_vcpu(0);
    npt_vcpu->regs.rdi = test_va;
    npt_vcpu->regs.rcx = g_vmmcall_auth_cookie;
    npt_vcpu->vmcb->state.rip = g_guest_npt_va;
    npt_vcpu->vmcb->state.rax = 0;
    LOG_ERROR("NPT guest: rip=0x%llx code_pa=0x%llx rdi=0x%llx rdi_pa=0x%llx cr3=0x%llx",
        npt_vcpu->vmcb->state.rip,
        MmGetPhysicalAddress((PVOID)g_guest_npt_va).QuadPart,
        npt_vcpu->regs.rdi,
        MmGetPhysicalAddress((PVOID)npt_vcpu->regs.rdi).QuadPart,
        npt_vcpu->vmcb->state.cr3);
    LOG_ERROR("NPT diag: test_pa=0x%llx trans_test=0x%llx trans_code=0x%llx",
        page_pa,
        npt_translate(&g_npt, page_pa),
        npt_translate(&g_npt, MmGetPhysicalAddress((PVOID)g_guest_npt_va).QuadPart));

    LOG_INFO("entering NPT test resident");
    yghv_trace("before npt test");
    svm_core_enter_resident_current(0);
    LOG_INFO("NPT test resident exited");
    yghv_trace("after npt test");
    g_npt_test_active = 0;
#else
    yghv_trace("npt test skipped");
#endif

    if (!NT_SUCCESS(yghv_protect_test())) {
        LOG_ERROR("protect test failed");
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        npt_test_buf = NULL;
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(yghv_hook_test())) {
        LOG_ERROR("protect hook test failed");
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        npt_test_buf = NULL;
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(yghv_hook_resident_test())) {
        LOG_ERROR("protect hook resident test failed");
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        npt_test_buf = NULL;
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(yghv_hook_boundary_test())) {
        LOG_ERROR("protect hook boundary test failed");
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        npt_test_buf = NULL;
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(yghv_cpuid_stealth_test())) {
        LOG_ERROR("cpuid stealth test failed");
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        npt_test_buf = NULL;
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return STATUS_UNSUCCESSFUL;
    }

    /* Reset CPU0 to the heartbeat guest for the multi-core resident test. */
    svm_core_prepare_vcpu_other(0);
    yghv_trace("cpu0 reset");
    g_vcpus[0]->regs.rcx = g_vmmcall_auth_cookie;
    g_vcpus[0]->vmcb->state.rip = g_guest_hb_va;
    svm_core_set_npt(0, g_npt.pml4_pa);

    sv = svm_core_start_remote_residents(online);
    if (sv) {
        LOG_ERROR("svm_core_start_remote_residents failed 0x%x", sv);
        svm_core_stop_all_residents();
        svm_core_wait_all_stopped(online);
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        g_npt_test_active = 0;
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return (NTSTATUS)sv;
    }
    yghv_trace("remote started");
    svm_core_wait_remote_ready(online);
    yghv_trace("remote ready");

    LOG_INFO("entering multi-core resident on CPU0");
    yghv_trace("before heartbeat");
    svm_core_enter_resident_current(0);
    LOG_INFO("CPU0 resident exited");
    yghv_trace("after heartbeat");
    svm_core_wait_all_stopped(online);
    yghv_trace("all stopped");

    if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
    npt_test_buf = NULL;
    g_npt_test_active = 0;
    if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
    g_guest_code_page = NULL;
    yghv_trace_close();

#if YGHV_RESIDENT_WORKLOAD_TEST
    g_resident_workload_page = MmAllocateContiguousMemory(
        HV_PAGE_SIZE, (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!g_resident_workload_page) {
        LOG_ERROR("resident workload: page alloc failed");
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(g_resident_workload_page, HV_PAGE_SIZE);
    sv = yghv_protect_set_target((uint32_t)(ULONG_PTR)PsGetCurrentProcessId());
    if (!sv)
        sv = yghv_protect_add_page((uint64_t)g_resident_workload_page);
    if (!sv)
        sv = yghv_protect_install_hook(0, (uint64_t)yghv_hook_test_dummy);
    if (sv) {
        LOG_ERROR("resident workload: setup failed 0x%x", sv);
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_resident_workload_page) MmFreeContiguousMemory(g_resident_workload_page);
        g_resident_workload_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return sv;
    }
#endif

    sv = yghv_protect_start();
    if (sv) {
        LOG_ERROR("persistent protect start failed 0x%x", sv);
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_resident_workload_page) MmFreeContiguousMemory(g_resident_workload_page);
        g_resident_workload_page = NULL;
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        npt_test_buf = NULL;
        g_npt_test_active = 0;
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return (NTSTATUS)sv;
    }
    KeIpiGenericCall(svm_core_ipi_prepare_vcpu, 0);
    svm_core_prepare_vcpu_other(0);
    for (i = 0; i < online; i++) {
        if (!g_vcpus[i]) continue;
        g_vcpus[i]->regs.rcx = g_vmmcall_auth_cookie;
#if YGHV_RESIDENT_WORKLOAD_TEST
        if (i == 0) {
            g_vcpus[i]->vmcb->state.rip =
                (uint64_t)svm_trampoline_test_resident_guest;
            g_vcpus[i]->regs.rdi = (uint64_t)g_resident_workload_page;
            g_vcpus[i]->regs.rsi = (uint64_t)yghv_hook_test_dummy;
        } else {
            g_vcpus[i]->vmcb->state.rip = g_guest_hb_va;
            g_vcpus[i]->regs.rdi = 0;
            g_vcpus[i]->regs.rsi = 0;
        }
        g_vcpus[i]->vmcb->state.cr3 = g_protect.cr3;
#else
        g_vcpus[i]->vmcb->state.rip = g_guest_hb_va;
#endif
        sv = svm_core_set_npt(i, g_npt.pml4_pa);
        if (sv) {
            LOG_ERROR("persistent vcpu prepare core %u failed 0x%x", i, sv);
            yghv_protect_cleanup();
            npt_cleanup(&g_npt);
            svm_core_cleanup();
            if (g_resident_workload_page) MmFreeContiguousMemory(g_resident_workload_page);
            g_resident_workload_page = NULL;
            if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
            npt_test_buf = NULL;
            g_npt_test_active = 0;
            if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
            g_guest_code_page = NULL;
            yghv_trace_close();
            KeRevertToUserAffinityThread();
            return (NTSTATUS)sv;
        }
    }
    sv = yghv_control_device_init(d);
    if (!sv)
        sv = svm_core_start_persistent_residents(online);
    if (sv) {
        LOG_ERROR("persistent residents/control device start failed 0x%x", sv);
        svm_core_wait_remote_ready(online);
        svm_core_stop_all_residents();
        svm_core_wait_all_stopped(online);
        yghv_control_device_cleanup(d);
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_resident_workload_page) MmFreeContiguousMemory(g_resident_workload_page);
        g_resident_workload_page = NULL;
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        npt_test_buf = NULL;
        g_npt_test_active = 0;
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        yghv_trace_close();
        KeRevertToUserAffinityThread();
        return (NTSTATUS)sv;
    }
    svm_core_wait_remote_ready(online);
    g_persistent_mode = TRUE;
    {
        NTSTATUS status = PsCreateSystemThread(
            &g_hook_rendezvous_thread, THREAD_ALL_ACCESS, NULL, NULL, NULL,
            yghv_hook_rendezvous_thread, NULL);
        if (!NT_SUCCESS(status)) {
            LOG_ERROR("hook rendezvous: thread create failed 0x%x", status);
            g_hook_rendezvous_thread = NULL;
        }
    }
    LOG_ERROR("persistent protect mode active: %u cores", online);
    KeRevertToUserAffinityThread();
    return STATUS_SUCCESS;
}
