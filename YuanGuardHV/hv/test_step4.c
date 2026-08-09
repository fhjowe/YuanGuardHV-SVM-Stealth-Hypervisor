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
    int sv=svm_core_init(); if(sv)return(NTSTATUS)sv;
    sv=npt_init(&g_npt,0x40000000ULL); if(sv){svm_core_cleanup();return(NTSTATUS)sv;}
    sv=svm_core_set_npt(0,g_npt.pml4_pa); if(sv){npt_cleanup(&g_npt);svm_core_cleanup();return(NTSTATUS)sv;}
    LOG_INFO("ok");
    return STATUS_SUCCESS;
}
