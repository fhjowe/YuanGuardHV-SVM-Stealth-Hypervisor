#include <ntddk.h>
#include "svm_defs.h"
#include "svm_vcpu.h"
#include "vmcb.h"
#include "debug.h"

#define SVM_EVENTINJ_VALID        (1ULL << 31)
#define SVM_EVENTINJ_TYPE_EXC     (3ULL << 8)
#define SVM_EVENTINJ_ERROR_VALID  (1ULL << 11)

static void svm_emulate_cpuid(svm_vcpu_t *vcpu);
static int  svm_handle_msr(svm_vcpu_t *vcpu);
static int  svm_handle_cr(svm_vcpu_t *vcpu);

extern int vmmcall_dispatch(svm_vcpu_t *vcpu);

static void svm_advance_rip(svm_vcpu_t *vcpu) {
    vcpu->vmcb->state.rip = vcpu->vmcb->control.next_rip;
}

static void svm_writeback_gprs(svm_vcpu_t *vcpu) {
    /* RAX is loaded from VMCB state.rax by VMRUN; other regs from vcpu->regs */
    vcpu->vmcb->state.rax = vcpu->regs.rax;
}

static void svm_finish_exit(svm_vcpu_t *vcpu) {
    svm_advance_rip(vcpu);
    svm_writeback_gprs(vcpu);
}

int svm_dispatch_exit(svm_vcpu_t *vcpu) {
    uint64_t exitcode = vcpu->vmcb->control.exitcode;

    vcpu->resident_exits++;

    switch (exitcode) {

    case SVM_EXIT_VMMCALL: {
        int stop = vmmcall_dispatch(vcpu);
        if (stop) return 1;
        svm_finish_exit(vcpu);
        return 0;
    }

    case SVM_EXIT_CPUID:
        svm_emulate_cpuid(vcpu);
        svm_finish_exit(vcpu);
        return 0;

    case SVM_EXIT_MSR:
        svm_handle_msr(vcpu);
        svm_finish_exit(vcpu);
        return 0;

    case SVM_EXIT_CR0_READ:  case SVM_EXIT_CR0_WRITE:
    case SVM_EXIT_CR3_READ:  case SVM_EXIT_CR3_WRITE:
    case SVM_EXIT_CR4_READ:  case SVM_EXIT_CR4_WRITE:
    case SVM_EXIT_CR8_READ:  case SVM_EXIT_CR8_WRITE:
        svm_handle_cr(vcpu);
        svm_finish_exit(vcpu);
        return 0;

    /* NPF = fault-like, no RIP advance — instruction re-executes */
    case SVM_EXIT_NPF: {
        uint64_t info1 = vcpu->vmcb->control.exitinfo1;
        uint64_t pf_ec = 0;

        LOG_INFO("NPF: info1=0x%llx GPA=0x%llx RIP=0x%llx",
            info1, vcpu->vmcb->control.exitinfo2, vcpu->vmcb->state.rip);

        if (info1 & NPF_INFO1_PRESENT) pf_ec |= (1ULL << 0);
        if (info1 & NPF_INFO1_WRITE)   pf_ec |= (1ULL << 1);
        if (info1 & NPF_INFO1_USER)    pf_ec |= (1ULL << 2);
        if (info1 & NPF_INFO1_RSVD)    pf_ec |= (1ULL << 3);
        if (info1 & NPF_INFO1_EXEC)    pf_ec |= (1ULL << 4);

        vcpu->vmcb->control.event_injection =
            SVM_EVENTINJ_VALID | SVM_EVENTINJ_TYPE_EXC | SVM_EVENTINJ_ERROR_VALID | 0x0E | (pf_ec << 32);

        return 0;
    }

    /* Interrupts — no RIP advance, guest interrupt handler will execute */
    case SVM_EXIT_INTR:
    case SVM_EXIT_NMI:
        vcpu->resident_interrupt_exits++;
        return 0;

    /* Instruction-boundary exits — advance RIP */
    case SVM_EXIT_HLT:
    case SVM_EXIT_PAUSE:
    case SVM_EXIT_INVD:
    case SVM_EXIT_INVLPG:
    case SVM_EXIT_INVLPGA:
    case SVM_EXIT_WBINVD:
    case SVM_EXIT_MONITOR:
    case SVM_EXIT_MWAIT:
    case SVM_EXIT_MWAIT_COND:
    case SVM_EXIT_XSETBV:
    case SVM_EXIT_RDTSC:
    case SVM_EXIT_RDTSCP:
        svm_finish_exit(vcpu);
        return 0;

    case SVM_EXIT_EXCEPTION_UD:
        LOG_ERROR("Guest #UD at RIP=0x%llx", vcpu->vmcb->state.rip);
        return 1;

    case SVM_EXIT_SHUTDOWN:
        LOG_ERROR("Guest shutdown");
        return 1;

    default:
        LOG_ERROR("Unhandled exit: 0x%llx, info1=0x%llx, RIP=0x%llx",
            exitcode, vcpu->vmcb->control.exitinfo1, vcpu->vmcb->state.rip);
        return 1;
    }
}

static void svm_emulate_cpuid(svm_vcpu_t *vcpu) {
    int cpu_info[4];
    uint32_t leaf = (uint32_t)vcpu->regs.rax;
    uint32_t subleaf = (uint32_t)vcpu->regs.rcx;

    /* Stealth: hide hypervisor presence */
    if (leaf >= 0x40000000 && leaf <= 0x400000FF) {
        /* Hypervisor CPUID range — return zeros */
        cpu_info[0] = cpu_info[1] = cpu_info[2] = cpu_info[3] = 0;
    } else if (leaf == 0x80000001) {
        /* Extended features: clear SVM bit */
        __cpuidex(cpu_info, leaf, subleaf);
        cpu_info[2] &= ~(1 << 2); /* clear SVM feature bit */
    } else if (leaf == 0x8000000A) {
        /* SVM revision/features: return zeros */
        cpu_info[0] = cpu_info[1] = cpu_info[2] = cpu_info[3] = 0;
    } else {
        __cpuidex(cpu_info, leaf, subleaf);
    }

    vcpu->regs.rax = cpu_info[0];
    vcpu->regs.rbx = cpu_info[1];
    vcpu->regs.rcx = cpu_info[2];
    vcpu->regs.rdx = cpu_info[3];
}

static uint64_t svm_host_read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static int svm_handle_msr(svm_vcpu_t *vcpu) {
    uint32_t msr = (uint32_t)vcpu->regs.rcx;
    uint64_t data;

    vcpu->resident_msr_exits++;

    data = svm_host_read_msr(msr);
    vcpu->regs.rax = (uint32_t)data;
    vcpu->regs.rdx = (uint32_t)(data >> 32);

    return 0;
}

static int svm_handle_cr(svm_vcpu_t *vcpu) {
    vcpu->resident_cr_exits++;
    (void)vcpu;
    return 0;
}
