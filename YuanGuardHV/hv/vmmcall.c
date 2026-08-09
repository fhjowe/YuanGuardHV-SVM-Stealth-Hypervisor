#include <ntddk.h>
#include "svm_defs.h"
#include "svm_vcpu.h"
#include "vmcb.h"
#include "control_plane.h"
#include "debug.h"

#define YUANGUARD_VERSION 0x00010000

int vmmcall_dispatch(svm_vcpu_t *vcpu) {
    uint64_t cmd = vcpu->regs.rax;

    switch (cmd) {

    case YGHV_CMD_HEARTBEAT:
        vcpu->regs.rax = YGHV_STATUS_OK;
        LOG_INFO("VMMCALL heartbeat from core %u", vcpu->resident_index);
        return 0;

    case YGHV_CMD_STOP_INTERNAL:
        LOG_INFO("VMMCALL stop — broadcasting to all cores");
        svm_core_stop_all_residents();
        vcpu->regs.rax = YGHV_STATUS_OK;
        return 1;

    case YGHV_CMD_VERSION:
        vcpu->regs.rax = YUANGUARD_VERSION;
        return 0;

    case YGHV_CMD_STATS:
        vcpu->regs.rax = vcpu->resident_exits;
        vcpu->regs.rbx = vcpu->resident_msr_exits;
        vcpu->regs.rcx = vcpu->resident_cr_exits;
        vcpu->regs.rdx = vcpu->resident_interrupt_exits;
        return 0;

    default:
        LOG_ERROR("Unknown VMMCALL cmd=0x%llx RIP=0x%llx",
            cmd, vcpu->vmcb->state.rip);
        vcpu->regs.rax = YGHV_STATUS_ERROR;
        return 0;
    }
}
