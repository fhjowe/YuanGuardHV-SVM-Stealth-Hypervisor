#include <ntddk.h>
#include <ntstrsafe.h>
#include "svm_defs.h"
#include "svm_vcpu.h"
#include "npt.h"
#include "control_plane.h"
#include "protect.h"
#include "debug.h"

#define YGHV_R1_SKIP_NPT_TEST 0
#define YGHV_R1_NPT_UNIT_TEST 1

npt_mgr_t g_npt;
uint64_t g_npt_test_pa;
volatile int g_npt_test_active;
void *g_guest_code_page = NULL;
uint64_t g_guest_hb_va = 0;
uint64_t g_guest_npt_va = 0;
HANDLE g_trace_file = NULL;

extern const uint8_t svm_trampoline_test_guest[];
extern const uint8_t svm_trampoline_test_guest_resume[];
extern const uint8_t svm_trampoline_test_guest_end[];
extern const uint8_t svm_trampoline_test_npt_guest[];
extern const uint8_t svm_trampoline_test_npt_guest_resume[];
extern const uint8_t svm_trampoline_test_npt_guest_end[];
extern const uint8_t svm_trampoline_test_prot_write[];
extern const uint8_t svm_trampoline_test_prot_write_resume[];
extern const uint8_t svm_trampoline_test_prot_write_end[];

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

static void yghv_trace(const char *msg) {
    IO_STATUS_BLOCK iosb;
    size_t msg_len = 0;
    char buf[256];

    if (!g_trace_file) return;
    if (!NT_SUCCESS(RtlStringCchLengthA(msg, sizeof(buf) - 2, &msg_len))) return;
    RtlCopyMemory(buf, msg, msg_len);
    buf[msg_len] = '\r';
    buf[msg_len + 1] = '\n';
    ZwWriteFile(g_trace_file, NULL, NULL, NULL, &iosb, buf, (ULONG)(msg_len + 2), NULL, NULL);
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
    uint64_t buf_pa, test_pa, test_pa2;
    uint64_t entry, trans;
    NTSTATUS st;

    buf = MmAllocateContiguousMemory(
        HV_LARGE_PAGE_SIZE * 4, (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!buf) {
        LOG_ERROR("r1 unit: alloc failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(buf, HV_LARGE_PAGE_SIZE * 4);
    buf_pa = MmGetPhysicalAddress(buf).QuadPart;
    test_pa = (buf_pa + HV_LARGE_PAGE_SIZE - 1) & ~(uint64_t)(HV_LARGE_PAGE_SIZE - 1);
    test_pa2 = test_pa + HV_LARGE_PAGE_SIZE;
    if (test_pa2 + HV_LARGE_PAGE_SIZE > buf_pa + HV_LARGE_PAGE_SIZE * 4) {
        LOG_ERROR("r1 unit: no aligned 2MB slots buf_pa=0x%llx", buf_pa);
        MmFreeContiguousMemory(buf);
        return STATUS_UNSUCCESSFUL;
    }
    LOG_ERROR("r1 unit: buf_pa=0x%llx test_pa=0x%llx test_pa2=0x%llx",
        buf_pa, test_pa, test_pa2);

    entry = npt_read_entry(&g_npt, test_pa);
    trans = npt_translate(&g_npt, test_pa);
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
    st = STATUS_SUCCESS;

done:
    MmFreeContiguousMemory(buf);
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
    (void)d;
    KeSetSystemAffinityThread((KAFFINITY)1);
    g_npt_test_active = 0;
    svm_core_stop_all_residents();
    svm_core_wait_all_stopped(g_vcpu_count);
    yghv_protect_cleanup();
    if (g_guest_code_page)
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
    g_guest_code_page = NULL;
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

    sv = npt_init(&g_npt, 0x400000000ULL);
    if (sv) {
        LOG_ERROR("npt_init failed 0x%x", sv);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
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
            KeRevertToUserAffinityThread();
            return (NTSTATUS)sv;
        }
    }
    LOG_ERROR("NPT enabled for %u cores: pml4=0x%llx", online, g_npt.pml4_pa);
    yghv_trace("npt set ok");

#if !YGHV_R1_SKIP_NPT_TEST
    /* Single-core NPT permission test first. */
    npt_test_buf = MmAllocateContiguousMemory(
        HV_LARGE_PAGE_SIZE * 2, (PHYSICAL_ADDRESS){ .QuadPart = 0xFFFFFFFF });
    if (!npt_test_buf) {
        LOG_ERROR("npt_test_buf allocation failed");
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
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
    g_npt_test_active = 0;
    if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
    g_guest_code_page = NULL;
    yghv_trace_close();

    sv = yghv_protect_start();
    if (sv) {
        LOG_ERROR("persistent protect start failed 0x%x", sv);
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        npt_test_buf = NULL;
        g_npt_test_active = 0;
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        KeRevertToUserAffinityThread();
        return (NTSTATUS)sv;
    }
    for (i = 0; i < online; i++) {
        if (!g_vcpus[i]) continue;
        g_vcpus[i]->regs.rcx = g_vmmcall_auth_cookie;
        g_vcpus[i]->vmcb->state.rip = g_guest_hb_va;
    }
    sv = svm_core_start_persistent_residents(online);
    if (sv) {
        LOG_ERROR("persistent residents start failed 0x%x", sv);
        svm_core_stop_all_residents();
        svm_core_wait_all_stopped(online);
        yghv_protect_cleanup();
        npt_cleanup(&g_npt);
        svm_core_cleanup();
        if (npt_test_buf) MmFreeContiguousMemory(npt_test_buf);
        npt_test_buf = NULL;
        g_npt_test_active = 0;
        if (g_guest_code_page) MmFreeContiguousMemory(g_guest_code_page);
        g_guest_code_page = NULL;
        KeRevertToUserAffinityThread();
        return (NTSTATUS)sv;
    }
    svm_core_wait_remote_ready(online);
    LOG_ERROR("persistent protect mode active: %u cores", online);
    KeRevertToUserAffinityThread();
    return STATUS_SUCCESS;
}
