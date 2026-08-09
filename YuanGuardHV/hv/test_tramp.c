#include <ntddk.h>
#include "svm_vcpu.h"
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;
void DriverUnload(PDRIVER_OBJECT d){(void)d;}
extern uint64_t svm_vmrun_trampoline(svm_vcpu_t *vcpu);
NTSTATUS DriverEntry(PDRIVER_OBJECT d,PUNICODE_STRING r){
    (void)r; d->DriverUnload=DriverUnload;
    DbgPrint("[TST] trampoline addr=%p\n", svm_vmrun_trampoline);
    return STATUS_SUCCESS;
}
