#include <ntddk.h>
#include "svm_defs.h"
#include "svm_vcpu.h"
#include "vmcb.h"
#include "control_plane.h"
#include "protect.h"
#include "debug.h"

#define YUANGUARD_VERSION 0x00010000

#define YGHV_HEARTBEAT_LOG_INTERVAL  1000ULL
#define YGHV_HEARTBEAT_TEST_LIMIT    10000ULL

uint64_t g_vmmcall_auth_cookie;

int vmmcall_dispatch(svm_vcpu_t *vcpu) {
    uint64_t cmd = vcpu->regs.rax;

    switch (cmd) {

    case YGHV_CMD_HEARTBEAT: {
        if ((vcpu->resident_exits % YGHV_HEARTBEAT_LOG_INTERVAL) == 0)
            LOG_ERROR("heartbeat exits=%llu core=%u",
                vcpu->resident_exits, vcpu->resident_index);
        if (vcpu->resident_exits >= YGHV_HEARTBEAT_TEST_LIMIT) {
            LOG_ERROR("heartbeat limit reached, stopping resident loop");
            svm_core_stop_all_residents();
            vcpu->regs.rax = YGHV_STATUS_OK;
            return 1;
        }
        vcpu->regs.rax = YGHV_STATUS_OK;
        return 0;
    }

    case YGHV_CMD_STOP_INTERNAL:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            LOG_ERROR("Unauthorized STOP_INTERNAL from core %u", vcpu->resident_index);
            vcpu->regs.rax = YGHV_STATUS_ERROR;
            return 0;
        }
        LOG_INFO("VMMCALL stop — broadcasting to all cores");
        svm_core_stop_all_residents();
        vcpu->regs.rax = YGHV_STATUS_OK;
        return 1;

    case YGHV_CMD_SHUTDOWN:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            LOG_ERROR("Unauthorized SHUTDOWN from core %u", vcpu->resident_index);
            vcpu->regs.rax = YGHV_STATUS_ERROR;
            return 0;
        }
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

    case YGHV_CMD_SET_TARGET:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_set_target((uint32_t)vcpu->regs.rdx);
        return 0;

    case YGHV_CMD_ADD_PAGE:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_add_page(vcpu->regs.rdx);
        return 0;

    case YGHV_CMD_REMOVE_PAGE:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_remove_page(vcpu->regs.rdx);
        return 0;

    case YGHV_CMD_START_PROTECT:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_start();
        return 0;

    case YGHV_CMD_STOP_PROTECT:
        if (vcpu->regs.rcx != g_vmmcall_auth_cookie) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        yghv_protect_stop();
        vcpu->regs.rax = YGHV_STATUS_OK;
        return 0;

    case YGHV_CMD_GET_STATE:
        vcpu->regs.rax = g_protect.active ? 1 : 0;
        vcpu->regs.rbx = g_protect.page_count;
        vcpu->regs.rcx = g_protect.pid;
        return 0;

    default:
        LOG_ERROR("Unknown VMMCALL cmd=0x%llx RIP=0x%llx",
            cmd, vcpu->vmcb->state.rip);
        vcpu->regs.rax = YGHV_STATUS_ERROR;
        return 0;
    }
}
