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
uint64_t g_control_cr3;
extern volatile BOOLEAN g_persistent_mode;

static int yghv_vmmcall_authorized(svm_vcpu_t *vcpu) {
    if (vcpu->regs.rcx != g_vmmcall_auth_cookie)
        return 0;
    if (vcpu->vmcb->state.cpl != 0)
        return 0;
    if (g_control_cr3 && vcpu->vmcb->state.cr3 != g_control_cr3)
        return 0;
    return 1;
}

int vmmcall_dispatch(svm_vcpu_t *vcpu) {
    uint64_t cmd = vcpu->regs.rax;

    switch (cmd) {

    case YGHV_CMD_HEARTBEAT: {
        ULONG active;
        uint64_t page_va = 0, hook_va = 0;
        yghv_protect_get_state(&active, NULL, NULL);
        if (active || g_persistent_mode) {
            if (yghv_protect_is_target_cr3(vcpu->vmcb->state.cr3)) {
                yghv_protect_get_heartbeat(&page_va, &hook_va);
                vcpu->regs.rdi = page_va;
                vcpu->regs.rsi = hook_va;
            } else {
                vcpu->regs.rdi = 0;
                vcpu->regs.rsi = 0;
            }
            if ((vcpu->resident_exits % 10000ULL) == 0)
                yghv_protect_check_target_exited();
            if ((vcpu->resident_exits % 10000ULL) == 0)
                LOG_ERROR("heartbeat protect exits=%llu core=%u page=0x%llx hook=0x%llx last=0x%llx",
                    vcpu->resident_exits, vcpu->resident_index,
                    vcpu->regs.rdi, vcpu->regs.rsi, vcpu->regs.rdx);
            vcpu->regs.rax = YGHV_STATUS_OK;
            return 0;
        }
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
        if (!yghv_vmmcall_authorized(vcpu)) {
            LOG_ERROR("Unauthorized STOP_INTERNAL from core %u", vcpu->resident_index);
            vcpu->regs.rax = YGHV_STATUS_ERROR;
            return 0;
        }
        LOG_INFO("VMMCALL stop — broadcasting to all cores");
        svm_core_stop_all_residents();
        vcpu->regs.rax = YGHV_STATUS_OK;
        return 1;

    case YGHV_CMD_SHUTDOWN:
        if (!yghv_vmmcall_authorized(vcpu)) {
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
        if (!yghv_vmmcall_authorized(vcpu)) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_set_target((uint32_t)vcpu->regs.rdx);
        return 0;

    case YGHV_CMD_ADD_PAGE:
        if (!yghv_vmmcall_authorized(vcpu)) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_add_page(vcpu->regs.rdx);
        return 0;

    case YGHV_CMD_REMOVE_PAGE:
        if (!yghv_vmmcall_authorized(vcpu)) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_remove_page(vcpu->regs.rdx);
        return 0;

    case YGHV_CMD_START_PROTECT:
        if (!yghv_vmmcall_authorized(vcpu)) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = (uint64_t)yghv_protect_start();
        return 0;

    case YGHV_CMD_STOP_PROTECT: {
        if (!yghv_vmmcall_authorized(vcpu)) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        NTSTATUS st = yghv_protect_stop();
        vcpu->regs.rax = (uint64_t)st;
        return 0;
    }

    case YGHV_CMD_GET_STATE:
        if (!yghv_vmmcall_authorized(vcpu)) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        {
            ULONG active, pid, page_count;
            yghv_protect_get_state(&active, &pid, &page_count);
            vcpu->regs.rax = active ? 1 : 0;
            vcpu->regs.rbx = page_count;
            vcpu->regs.rcx = pid;
        }
        return 0;

    case YGHV_CMD_HOOK_QUERY:
        if (!yghv_vmmcall_authorized(vcpu)) {
            vcpu->regs.rax = YGHV_STATUS_DENIED;
            return 0;
        }
        vcpu->regs.rax = yghv_protect_on_hook_query(
            (uint8_t)vcpu->regs.rbx, vcpu->vmcb->state.cr3);
        return 0;

    default:
        LOG_ERROR("Unknown VMMCALL cmd=0x%llx RIP=0x%llx",
            cmd, vcpu->vmcb->state.rip);
        vcpu->regs.rax = YGHV_STATUS_ERROR;
        return 0;
    }
}
