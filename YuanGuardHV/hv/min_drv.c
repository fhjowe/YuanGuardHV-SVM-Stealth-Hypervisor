#include <ntddk.h>

#define YGHV_TAG 'vhGY'

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;

void DriverUnload(PDRIVER_OBJECT DriverObject) {
    (void)DriverObject;
    DbgPrint("[MINTEST] DriverUnload called\n");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    (void)DriverObject; (void)RegistryPath;
    DbgPrint("[MINTEST] DriverEntry called - basic driver load test\n");
    DriverObject->DriverUnload = DriverUnload;
    return STATUS_SUCCESS;
}
