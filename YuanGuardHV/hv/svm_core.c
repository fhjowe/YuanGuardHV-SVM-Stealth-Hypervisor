#include <ntddk.h>
#include "svm_defs.h"
#include "svm_vcpu.h"
#include "vmcb.h"
#include "debug.h"
#define YGHV_DEBUG_LOG  /* development only */

svm_vcpu_t *g_vcpus[SVM_MAX_CORES];
ULONG g_vcpu_count;

/* --- MSR helpers --- */
static uint64_t yg_read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void yg_write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

/* --- Segment helpers --- */
static uint16_t yg_read_cs(void) {
    uint16_t cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    return cs;
}

static uint16_t yg_read_ss(void) {
    uint16_t ss;
    __asm__ volatile("mov %%ss, %0" : "=r"(ss));
    return ss;
}

static uint16_t yg_read_ds(void) {
    uint16_t ds;
    __asm__ volatile("mov %%ds, %0" : "=r"(ds));
    return ds;
}

static uint16_t yg_read_es(void) {
    uint16_t es;
    __asm__ volatile("mov %%es, %0" : "=r"(es));
    return es;
}

static void yg_read_gdtr(uint64_t *base, uint16_t *limit) {
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    *limit = gdtr.limit;
    *base = gdtr.base;
}

static void yg_read_idtr(uint64_t *base, uint16_t *limit) {
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr;
    __asm__ volatile("sidt %0" : "=m"(idtr));
    *limit = idtr.limit;
    *base = idtr.base;
}

static uint16_t yg_read_tr(void) {
    uint16_t tr;
    __asm__ volatile("str %0" : "=r"(tr));
    return tr;
}

static uint64_t yg_read_rflags(void) {
    uint64_t rflags;
    __asm__ volatile(
        "pushfq\n"
        "pop %0\n"
        : "=r"(rflags)
    );
    return rflags;
}

/* --- CR helpers --- */
static uint64_t yg_read_cr0(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}
static uint64_t yg_read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}
static uint64_t yg_read_cr4(void) {
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    return cr4;
}

/* --- SVM detection --- */
int cpu_has_svm(void) {
    int cpu_info[4];
    __cpuidex(cpu_info, CPUID_AMD_EXTENDED, 0);
    if ((uint32_t)cpu_info[0] < CPUID_AMD_SVM)
        return 0;
    __cpuidex(cpu_info, CPUID_AMD_SVM, 0);
    return (cpu_info[2] & CPUID_SVM_FEATURE_SVM) != 0;
}

static int cpu_has_npt(void) {
    int cpu_info[4];
    __cpuidex(cpu_info, CPUID_AMD_NPT, 0);
    return (cpu_info[3] & CPUID_NPT_FEATURE_NPT) != 0;
}

/* --- VCPU allocation --- */
int svm_alloc_vcpu(uint32_t core_id, svm_vcpu_t **out) {
    svm_vcpu_t *vcpu;

    vcpu = (svm_vcpu_t *)ExAllocatePoolWithTag(NonPagedPool, sizeof(svm_vcpu_t), YGHV_TAG);
    if (!vcpu) goto fail;
    RtlZeroMemory(vcpu, sizeof(svm_vcpu_t));

    /* VMCB — 4KB aligned physical page */
    vcpu->vmcb = (vmcb_t *)MmAllocateContiguousMemory(HV_PAGE_SIZE, (PHYSICAL_ADDRESS){ .QuadPart = (ULONGLONG)-1 });
    if (!vcpu->vmcb) goto fail_vmcb;
    RtlZeroMemory(vcpu->vmcb, HV_PAGE_SIZE);
    vcpu->vmcb_pa = MmGetPhysicalAddress(vcpu->vmcb).QuadPart;

    /* Host VMCB — for VMLOAD after VMRUN exit (resident mode) */
    vcpu->host_vmcb = (vmcb_t *)MmAllocateContiguousMemory(HV_PAGE_SIZE, (PHYSICAL_ADDRESS){ .QuadPart = (ULONGLONG)-1 });
    if (!vcpu->host_vmcb) goto fail_host_vmcb;
    RtlZeroMemory(vcpu->host_vmcb, HV_PAGE_SIZE);
    vcpu->host_vmcb_pa = MmGetPhysicalAddress(vcpu->host_vmcb).QuadPart;

    /* HSave area — 4KB aligned */
    vcpu->hsave = MmAllocateContiguousMemory(HV_PAGE_SIZE, (PHYSICAL_ADDRESS){ .QuadPart = (ULONGLONG)-1 });
    if (!vcpu->hsave) goto fail_hsave;
    RtlZeroMemory(vcpu->hsave, HV_PAGE_SIZE);
    vcpu->hsave_pa = MmGetPhysicalAddress(vcpu->hsave).QuadPart;

    /* Host stack — 4 pages */
    vcpu->host_stack = MmAllocateContiguousMemory(SVM_HOST_STACK_PAGES * HV_PAGE_SIZE,
        (PHYSICAL_ADDRESS){ .QuadPart = (ULONGLONG)-1 });
    if (!vcpu->host_stack) goto fail_stack;
    RtlZeroMemory(vcpu->host_stack, SVM_HOST_STACK_PAGES * HV_PAGE_SIZE);
    vcpu->host_stack_top = (uint64_t)vcpu->host_stack + SVM_HOST_STACK_PAGES * HV_PAGE_SIZE - 8;

    /* MSRPM — 2 pages, zero = allow MSR access (bit=1 means intercept) */
    vcpu->msrpm = MmAllocateContiguousMemory(SVM_MSRPM_PAGES * HV_PAGE_SIZE,
        (PHYSICAL_ADDRESS){ .QuadPart = (ULONGLONG)-1 });
    if (!vcpu->msrpm) goto fail_msrpm;
    RtlZeroMemory(vcpu->msrpm, SVM_MSRPM_PAGES * HV_PAGE_SIZE);
    vcpu->msrpm_pa = MmGetPhysicalAddress(vcpu->msrpm).QuadPart;

    /* IOPM — 3 pages, zero = allow IO (bit=1 means intercept) */
    vcpu->iopm = MmAllocateContiguousMemory(SVM_IOPM_PAGES * HV_PAGE_SIZE,
        (PHYSICAL_ADDRESS){ .QuadPart = (ULONGLONG)-1 });
    if (!vcpu->iopm) goto fail_iopm;
    RtlZeroMemory(vcpu->iopm, SVM_IOPM_PAGES * HV_PAGE_SIZE);
    vcpu->iopm_pa = MmGetPhysicalAddress(vcpu->iopm).QuadPart;

    vcpu->resident_state = SVM_RESIDENT_OFF;
    *out = vcpu;
    return STATUS_SUCCESS;

fail_iopm:
    MmFreeContiguousMemory(vcpu->msrpm);
fail_msrpm:
    MmFreeContiguousMemory(vcpu->host_stack);
fail_stack:
    MmFreeContiguousMemory(vcpu->hsave);
fail_hsave:
    MmFreeContiguousMemory(vcpu->host_vmcb);
fail_host_vmcb:
    MmFreeContiguousMemory(vcpu->vmcb);
fail_vmcb:
    ExFreePool(vcpu);
fail:
    LOG_ERROR("svm_alloc_vcpu: allocation failed for core %u", core_id);
    return STATUS_INSUFFICIENT_RESOURCES;
}

static void svm_free_vcpu(svm_vcpu_t *vcpu) {
    if (!vcpu) return;
    if (vcpu->iopm)  MmFreeContiguousMemory(vcpu->iopm);
    if (vcpu->msrpm) MmFreeContiguousMemory(vcpu->msrpm);
    if (vcpu->host_stack) MmFreeContiguousMemory(vcpu->host_stack);
    if (vcpu->hsave) MmFreeContiguousMemory(vcpu->hsave);
    if (vcpu->host_vmcb) MmFreeContiguousMemory(vcpu->host_vmcb);
    if (vcpu->vmcb) MmFreeContiguousMemory(vcpu->vmcb);
    ExFreePool(vcpu);
}

/* --- Read segment descriptor attrib/limit/base from GDT --- */
static void yg_read_seg_descriptor(uint16_t selector, uint16_t *attrib, uint32_t *limit, uint64_t *base) {
    uint64_t gdt_base;
    uint16_t gdt_limit;
    yg_read_gdtr(&gdt_base, &gdt_limit);

    if ((selector & ~7) >= gdt_limit) {
        /* Invalid selector — use zero values */
        *attrib = 0;
        *limit = 0;
        *base = 0;
        return;
    }

    uint64_t *desc = (uint64_t *)(gdt_base + (selector & ~7));
    uint64_t hi = desc[1];
    uint64_t lo = desc[0];

    /* Extract attribute (bits 40-55 of GDT entry) */
    *attrib = (uint16_t)((hi >> 8) & 0xF0FF);

    /* Extract limit */
    *limit = (uint32_t)((lo & 0xFFFF) | (hi & 0xF0000));
    if (hi & (1ULL << 55)) /* Granularity: 4KB units */
        *limit = (*limit << 12) | 0xFFF;

    /* Extract base */
    *base = ((lo >> 16) & 0xFFFF) | ((lo >> 32) & 0xFF000000) |
            ((hi << 16) & 0xFF000000) | ((hi >> 16) & 0xFF);
}

/* --- VCPU preparation --- */
void svm_prepare_vcpu(svm_vcpu_t *vcpu, uint64_t guest_rip) {
    vmcb_control_t *ctrl = &vcpu->vmcb->control;
    vmcb_state_t *state = &vcpu->vmcb->state;
    uint64_t gdt_base, idt_base;
    uint16_t gdt_limit, idt_limit;

    /* — Control area — */
    ctrl->general1_intercepts = 0;
    ctrl->general1_intercepts = 0;
    ctrl->general2_intercepts = INTR_GEN2(SVM_INTERCEPT_VMMCALL);
    ctrl->cr_read_intercepts = 0;
    ctrl->cr_write_intercepts = 0;
    ctrl->dr_read_intercepts = 0;
    ctrl->dr_write_intercepts = 0;
    ctrl->exception_intercepts = 0;

    /* Bitmap base addresses */
    ctrl->msrpm_base_pa = vcpu->msrpm_pa;
    ctrl->iopm_base_pa = vcpu->iopm_pa;

    /* NPT disabled for minimal test */
    ctrl->np_enable = 0;
    /* Guest ASID — ASID 0 reserved for host, use core_id+1 in real multi-core */
    ctrl->guest_asid = 1;
    ctrl->tlb_control = 0x01;  /* Flush guest TLB for this ASID on next VMRUN */

    /* VMCB clean bits — 0 = force re-read all fields on VMRUN */
    ctrl->vmcb_clean_bits = 0;

    /* — State area — */
    /* Segment selectors (mirror host) */
    state->cs_selector = yg_read_cs();
    state->ss_selector = yg_read_ss();
    state->ds_selector = yg_read_ds();
    state->es_selector = yg_read_es();

    /* Read segment descriptors for attrib/limit/base */
    yg_read_seg_descriptor(state->cs_selector, &state->cs_attrib, &state->cs_limit, &state->cs_base);
    yg_read_seg_descriptor(state->ss_selector, &state->ss_attrib, &state->ss_limit, &state->ss_base);
    yg_read_seg_descriptor(state->ds_selector, &state->ds_attrib, &state->ds_limit, &state->ds_base);
    yg_read_seg_descriptor(state->es_selector, &state->es_attrib, &state->es_limit, &state->es_base);
    /* FS/GS base from MSR — leave attributes/limits from GDT if selector is in GDT, or zero */
    state->gs_selector = 0;
    state->gs_base = yg_read_msr(0xC0000101); /* MSR_GS_BASE */
    state->fs_selector = 0;
    state->fs_base = yg_read_msr(0xC0000100); /* MSR_FS_BASE */

    /* Descriptor tables — mirror host */
    yg_read_gdtr(&gdt_base, &gdt_limit);
    state->gdtr_base = gdt_base;
    state->gdtr_limit = gdt_limit;
    yg_read_idtr(&idt_base, &idt_limit);
    state->idtr_base = idt_base;
    state->idtr_limit = idt_limit;

    /* TR — mirror host */
    state->tr_selector = yg_read_tr();
    yg_read_seg_descriptor(state->tr_selector, &state->tr_attrib, &state->tr_limit, &state->tr_base);

    /* LDTR — null (x64 kernel doesn't use LDT) */
    state->ldtr_selector = 0;

    /* Control registers — mirror host */
    state->cr0 = yg_read_cr0();
    state->cr3 = yg_read_cr3();
    state->cr4 = yg_read_cr4();
    state->efer = yg_read_msr(MSR_EFER);

    /* DR6/DR7 — mirror host */
    state->dr6 = 0xFFFF0FF0;
    state->dr7 = 0x400;

    /* RFLAGS — use host rflags with IF set */
    state->rflags = yg_read_rflags() | 0x200;

    /* CPL — ring 0 */
    state->cpl = 0;

    /* GIF — set */
    state->gif = 1;

    /* Guest RIP/RSP */
    state->rip = guest_rip;
    state->rsp = vcpu->host_stack_top;

    /* Syscall MSRs — mirror host */
    state->star = yg_read_msr(0xC0000081);
    state->lstar = yg_read_msr(0xC0000082);
    state->cstar = yg_read_msr(0xC0000083);
    state->sfmask = yg_read_msr(0xC0000084);
    state->sysenter_cs = yg_read_msr(0x174);
    state->sysenter_esp = yg_read_msr(0x175);
    state->sysenter_eip = yg_read_msr(0x176);
    state->kernel_gs_base = yg_read_msr(0xC0000102); /* MSR_KERNEL_GS_BASE */
    state->cr2 = 0;

    LOG_INFO("svm_prepare_vcpu: guest_rip=0x%llx, cr3=0x%llx, efer=0x%llx",
        guest_rip, state->cr3, state->efer);
}

/* --- SVM core init --- */
int svm_core_init(void) {
    uint64_t efer;
    NTSTATUS status;
    ULONG core_id = KeGetCurrentProcessorNumber();

    if (!cpu_has_svm()) {
        LOG_ERROR("SVM not supported on this CPU");
        return STATUS_HV_FEATURE_UNAVAILABLE;
    }
    LOG_INFO("SVM detected");

    if (cpu_has_npt())
        LOG_INFO("NPT supported");
    else
        LOG_WARN("NPT not supported — identity map won't be available");

    if (core_id >= SVM_MAX_CORES) {
        LOG_ERROR("Core %u exceeds SVM_MAX_CORES=%u", (unsigned)core_id, SVM_MAX_CORES);
        return STATUS_UNSUCCESSFUL;
    }

    /* Enable SVM if not already set (firmware/L0 may have set it). Save old state. */
    efer = yg_read_msr(MSR_EFER);
    if (efer & EFER_SVME) {
        LOG_INFO("EFER.SVME already set");
    } else {
        efer |= EFER_SVME;
        yg_write_msr(MSR_EFER, efer);
        LOG_INFO("EFER.SVME set by hypervisor");
    }

    /* Save old VM_HSAVE for cleanup restore */
    uint64_t old_hsave = yg_read_msr(MSR_VM_HSAVE);
    (void)old_hsave;

    /* Allocate and prepare VCPU for current core */
    status = (NTSTATUS)svm_alloc_vcpu(core_id, &g_vcpus[core_id]);
    if (!NT_SUCCESS(status)) {
        svm_core_cleanup();
        return status;
    }
    g_vcpu_count++;
    /* Save state for cleanup restore */
    g_vcpus[core_id]->old_hsave = yg_read_msr(MSR_VM_HSAVE);
    g_vcpus[core_id]->old_efer = yg_read_msr(MSR_EFER);

    svm_prepare_vcpu(g_vcpus[core_id], (uint64_t)svm_trampoline_test_guest);
    /* Set first VMMCALL to heartbeat so resident loop doesn't exit immediately */
    g_vcpus[core_id]->regs.rax = YGHV_CMD_HEARTBEAT;
    g_vcpus[core_id]->vmcb->state.rax = YGHV_CMD_HEARTBEAT;
    LOG_INFO("svm_core_init: VCPU[%u] ready", (unsigned)core_id);
    return STATUS_SUCCESS;
}

int svm_core_cleanup(void) {
    ULONG i;

    /* Restore per-core MSR state before freeing memory */
    for (i = 0; i < SVM_MAX_CORES; i++) {
        if (g_vcpus[i]) {
            /* Restore saved MSR values if we modified them */
            if (g_vcpus[i]->old_hsave)
                yg_write_msr(MSR_VM_HSAVE, g_vcpus[i]->old_hsave);
            svm_free_vcpu(g_vcpus[i]);
            g_vcpus[i] = NULL;
        }
    }
    g_vcpu_count = 0;

    /* Disable SVM — only if we are the ones who set it.
       ponytail: nested virt may #GP on EFER wrmsr; skip cleanup there.
       On bare-metal, clearing EFER.SVME is safe after all VMs are stopped. */
    {
        uint64_t efer = yg_read_msr(MSR_EFER);
        if (efer & EFER_SVME) {
            efer &= ~EFER_SVME;
            yg_write_msr(MSR_EFER, efer);
            LOG_INFO("SVM disabled");
        }
    }

    return STATUS_SUCCESS;
}

int svm_core_prepare_probe(uint32_t count) {
    (void)count;
    return STATUS_NOT_IMPLEMENTED;
}

int svm_core_probe_current(uint32_t index, svm_probe_result_t *result) {
    (void)index;
    if (result) RtlZeroMemory(result, sizeof(*result));
    return STATUS_NOT_IMPLEMENTED;
}

int svm_core_run_trampoline_once(uint32_t core_id) {
    svm_vcpu_t *vcpu;
    uint64_t exitcode;

    if (core_id >= SVM_MAX_CORES || !g_vcpus[core_id])
        return -1;

    vcpu = g_vcpus[core_id];

    LOG_INFO("VMRUN single-shot: core=%u", core_id);

    exitcode = svm_vmrun_trampoline(vcpu);

    LOG_INFO("VMRUN exit: code=0x%llx, has_vmmcall=%d",
        exitcode, exitcode == SVM_EXIT_VMMCALL ? 1 : 0);

    return (exitcode == SVM_EXIT_VMMCALL) ? 0 : (int)exitcode;
}

int svm_core_run_trampoline_test(uint32_t core_id) {
    return svm_core_run_trampoline_once(core_id);
}

/* --- VCPU accessor --- */
svm_vcpu_t *svm_core_get_vcpu(uint32_t index) {
    if (index >= SVM_MAX_CORES || !g_vcpus[index])
        return NULL;
    return g_vcpus[index];
}

/* --- NPT configuration --- */
int svm_core_set_npt(uint32_t core_id, uint64_t ncr3) {
    svm_vcpu_t *vcpu = svm_core_get_vcpu(core_id);
    if (!vcpu) return STATUS_NOT_FOUND;
    /* ponytail: enable NPT with identity-map covering all phys memory */
    vcpu->vmcb->control.np_enable = SVM_NP_ENABLE;
    vcpu->vmcb->control.ncr3 = ncr3;
    vcpu->resident_index = core_id;
    return STATUS_SUCCESS;
}

extern int svm_dispatch_exit(svm_vcpu_t *vcpu);

/* --- Resident lifecycle --- */
int svm_core_prepare_resident(uint32_t count) {
    (void)count;
    return STATUS_SUCCESS;
}

int svm_core_enter_resident_current(uint32_t index) {
    svm_vcpu_t *vcpu = svm_core_get_vcpu(index);
    if (!vcpu) return STATUS_NOT_FOUND;

    vcpu->resident_state = SVM_RESIDENT_ACTIVE;
    LOG_INFO("Resident loop starting on core %u", (unsigned)index);

    while (vcpu->resident_state == SVM_RESIDENT_ACTIVE) {
        uint64_t exitcode = svm_vmrun_trampoline(vcpu);
        (void)exitcode;
        int stop = svm_dispatch_exit(vcpu);
        if (stop) break;
    }

    LOG_INFO("Resident loop exit: state=%d, exits=%llu",
        vcpu->resident_state, vcpu->resident_exits);

    /* Devirtualize */
    vcpu->resident_state = SVM_RESIDENT_STOPPED;
    vcpu->vmcb->control.general1_intercepts = 0;
    vcpu->vmcb->control.general2_intercepts = 0;
    vcpu->vmcb->control.np_enable = 0;

    return 0;
}

int svm_core_stop_resident_current(uint32_t index) {
    svm_vcpu_t *vcpu = svm_core_get_vcpu(index);
    if (!vcpu) return STATUS_NOT_FOUND;
    vcpu->resident_state = SVM_RESIDENT_STOPPING;
    return STATUS_SUCCESS;
}

int svm_core_resident_state(uint32_t index) {
    if (index >= SVM_MAX_CORES || !g_vcpus[index])
        return SVM_RESIDENT_OFF;
    return g_vcpus[index]->resident_state;
}

uint64_t svm_core_resident_exit_count(uint32_t index) {
    if (index >= SVM_MAX_CORES || !g_vcpus[index])
        return 0;
    return g_vcpus[index]->resident_exits;
}

/* --- Multi-core IPI callbacks --- */

int svm_core_prepare_vcpu_other(uint32_t core_id) {
    uint64_t efer;

    if (core_id >= SVM_MAX_CORES || !g_vcpus[core_id])
        return STATUS_NOT_FOUND;

    efer = yg_read_msr(MSR_EFER);
    if (!(efer & EFER_SVME)) {
        efer |= EFER_SVME;
        yg_write_msr(MSR_EFER, efer);
    }

    svm_prepare_vcpu(g_vcpus[core_id], (uint64_t)svm_trampoline_test_guest);
    g_vcpus[core_id]->regs.rax = YGHV_CMD_HEARTBEAT;
    g_vcpus[core_id]->vmcb->state.rax = YGHV_CMD_HEARTBEAT;

    LOG_INFO("svm_core_prepare_vcpu_other: VCPU[%u] ready", core_id);
    return STATUS_SUCCESS;
}

ULONG_PTR svm_core_ipi_prepare_vcpu(ULONG_PTR arg) {
    (void)arg;
    uint32_t core = (uint32_t)KeGetCurrentProcessorNumber();
    if (core == 0) return 0; /* core 0 already prepared by svm_core_init */
    svm_core_prepare_vcpu_other(core);
    return 0;
}

ULONG_PTR svm_core_ipi_set_npt(ULONG_PTR arg) {
    npt_mgr_t *npt = (npt_mgr_t *)arg;
    uint32_t core = (uint32_t)KeGetCurrentProcessorNumber();
    if (!npt || core >= SVM_MAX_CORES || !g_vcpus[core]) return 1;
    svm_core_set_npt(core, npt->pml4_pa);
    LOG_INFO("NPT set on core %u", core);
    return 0;
}

void svm_core_stop_all_residents(void) {
    ULONG i;
    for (i = 0; i < SVM_MAX_CORES; i++) {
        if (g_vcpus[i] && g_vcpus[i]->resident_state == SVM_RESIDENT_ACTIVE) {
            g_vcpus[i]->resident_state = SVM_RESIDENT_STOPPING;
            LOG_INFO("Core %u marked STOPPING", i);
        }
    }
}

