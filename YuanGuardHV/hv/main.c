#include <ntddk.h>
#include "svm_defs.h"
#include "svm_vcpu.h"
#include "npt.h"
#include "debug.h"
npt_mgr_t g_npt;

DRIVER_INITIALIZE DriverEntry;

NTSTATUS DriverEntry(struct _DRIVER_OBJECT*d,PUNICODE_STRING r){
    (void)d;(void)r;
    int sv;

    LOG_INFO("DriverEntry start");
    sv=svm_core_init();
    if(sv){LOG_ERROR("svm_core_init failed 0x%x",sv);return(NTSTATUS)sv;}
    LOG_INFO("svm_core_init ok");

    sv=npt_init(&g_npt,0x40000000ULL);
    if(sv){LOG_ERROR("npt_init failed 0x%x",sv);svm_core_cleanup();return(NTSTATUS)sv;}
    LOG_INFO("npt_init ok");

    /* inline npt setup — avoids 1168 */
    {
        svm_vcpu_t *v = svm_core_get_vcpu(0);
        if(!v){npt_cleanup(&g_npt);svm_core_cleanup();return STATUS_NOT_FOUND;}
        v->vmcb->control.np_enable = SVM_NP_ENABLE;
        v->vmcb->control.ncr3 = g_npt.pml4_pa;
    }
    LOG_INFO("npt set");

    LOG_INFO("entering resident");
    svm_core_enter_resident_current(0);
    LOG_INFO("resident exited");

    npt_cleanup(&g_npt);
    svm_core_cleanup();
    return STATUS_SUCCESS;
}
