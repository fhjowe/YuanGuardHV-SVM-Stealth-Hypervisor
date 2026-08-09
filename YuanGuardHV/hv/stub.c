#include <ntddk.h>
#include "svm_defs.h"
#include "debug.h"

#define YGHV_DEBUG_LOG

DRIVER_INITIALIZE DriverEntry;

NTSTATUS DriverEntry(
    _In_ struct _DRIVER_OBJECT *DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    (void)DriverObject;
    (void)RegistryPath;
    LOG_INFO("YuanGuardHV: stub loaded");
    return STATUS_SUCCESS;
}
