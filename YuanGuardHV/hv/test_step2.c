#include <ntddk.h>
#include "svm_vcpu.h"
#include "debug.h"
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;
void DriverUnload(struct _DRIVER_OBJECT*d){(void)d;svm_core_cleanup();}
NTSTATUS DriverEntry(struct _DRIVER_OBJECT*d,PUNICODE_STRING r){
    d->DriverUnload=DriverUnload;(void)r;
    LOG_INFO("step2");
    int sv=svm_core_init();
    if(sv)return(NTSTATUS)sv;
    return STATUS_SUCCESS;
}
