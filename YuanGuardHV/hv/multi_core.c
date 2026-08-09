#include <ntddk.h>
#include "svm_defs.h"
#include "svm_vcpu.h"
#include "debug.h"

static void svm_dpc_resident_start(struct _KDPC *dpc, void *ctx, void *arg1, void *arg2) {
    (void)dpc;(void)ctx;(void)arg1;(void)arg2;
    uint32_t core = (uint32_t)KeGetCurrentProcessorNumber();
    if (core < SVM_MAX_CORES && g_vcpus[core])
        svm_core_enter_resident_current(core);
}

void svm_core_start_remote_residents(ULONG online) {
    ULONG i;
    for (i = 1; i < online && i < SVM_MAX_CORES; i++) {
        KDPC *dpc;
        if (!g_vcpus[i]) continue;
        dpc = (KDPC *)ExAllocatePoolWithTag(NonPagedPool, sizeof(KDPC), YGHV_TAG);
        if (!dpc) continue;
        KeInitializeDpc(dpc, svm_dpc_resident_start, dpc);
        KeSetTargetProcessorDpc(dpc, (CCHAR)i);
        KeInsertQueueDpc(dpc, NULL, NULL);
        LOG_INFO("DPC queued for core %u", i);
    }
}

void svm_core_wait_all_stopped(ULONG online) {
    ULONG i;
    LARGE_INTEGER timeout;
    timeout.QuadPart = -100000;
    for (;;) {
        ULONG running = 0;
        for (i = 0; i < online && i < SVM_MAX_CORES; i++) {
            if (g_vcpus[i] && g_vcpus[i]->resident_state == SVM_RESIDENT_ACTIVE)
                running++;
        }
        if (running == 0) break;
        KeDelayExecutionThread(KernelMode, FALSE, &timeout);
    }
    LOG_INFO("All cores stopped");
}
