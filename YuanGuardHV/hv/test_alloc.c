#include <ntddk.h>
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;
void DriverUnload(PDRIVER_OBJECT d){(void)d;}
NTSTATUS DriverEntry(PDRIVER_OBJECT d,PUNICODE_STRING r){
    (void)r;
    d->DriverUnload=DriverUnload;
    void*p=MmAllocateContiguousMemory(0x1000,(PHYSICAL_ADDRESS){-1});
    if(p)MmFreeContiguousMemory(p);
    DbgPrint("[TEST] Loaded\n");
    return STATUS_SUCCESS;
}
