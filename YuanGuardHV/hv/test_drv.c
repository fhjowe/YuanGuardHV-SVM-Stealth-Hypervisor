#include <ntddk.h>

DRIVER_INITIALIZE DriverEntry;

NTSTATUS DriverEntry(
    _In_ struct _DRIVER_OBJECT *DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    (void)DriverObject;
    (void)RegistryPath;
    DbgPrint("Hello from test driver\n");
    return STATUS_SUCCESS;
}
