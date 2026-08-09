#include <ntddk.h>
#include "svm_vcpu.h"
#include "npt.h"
#include "debug.h"
npt_mgr_t g_npt;
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;
void DriverUnload(struct _DRIVER_OBJECT*d){(void)d;npt_cleanup(&g_npt);svm_core_cleanup();}
NTSTATUS DriverEntry(struct _DRIVER_OBJECT*d,PUNICODE_STRING r){
    d->DriverUnload=DriverUnload;(void)r;
    if(svm_core_init()){svm_core_cleanup();return STATUS_UNSUCCESSFUL;}
    if(npt_init(&g_npt,0x40000000ULL)){svm_core_cleanup();return STATUS_UNSUCCESSFUL;}
    svm_core_get_vcpu(0)->vmcb->control.np_enable = 1;
    svm_core_get_vcpu(0)->vmcb->control.ncr3 = g_npt.pml4_pa;
    LOG_INFO("set_npt inline ok");
    return STATUS_SUCCESS;
}
