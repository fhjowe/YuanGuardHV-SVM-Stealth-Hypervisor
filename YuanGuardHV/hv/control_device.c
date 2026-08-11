#include <ntddk.h>
#include "control_ioctl.h"
#include "control_device.h"
#include "protect.h"
#include "debug.h"

static PDEVICE_OBJECT g_yghv_device = NULL;

static NTSTATUS yghv_control_complete(PIRP irp, NTSTATUS status, ULONG info) {
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = info;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS yghv_control_dispatch_default(PDEVICE_OBJECT dev, PIRP irp) {
    (void)dev;
    return yghv_control_complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
}

static NTSTATUS yghv_control_dispatch_open(PDEVICE_OBJECT dev, PIRP irp) {
    (void)dev;
    return yghv_control_complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS yghv_control_dispatch_ioctl(PDEVICE_OBJECT dev, PIRP irp) {
    IO_STACK_LOCATION *stack;
    ULONG code, in_len, out_len;
    PVOID buf;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG info = 0;
    (void)dev;

    stack = IoGetCurrentIrpStackLocation(irp);
    code = stack->Parameters.DeviceIoControl.IoControlCode;
    in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
    out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    buf = irp->AssociatedIrp.SystemBuffer;

    switch (code) {
    case IOCTL_YGHV_SET_TARGET: {
        yghv_ioctl_set_target_t *in = (yghv_ioctl_set_target_t *)buf;
        if (in_len < sizeof(*in)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = yghv_protect_set_target(in->pid);
        break;
    }
    case IOCTL_YGHV_ADD_PAGE: {
        yghv_ioctl_va_t *in = (yghv_ioctl_va_t *)buf;
        if (in_len < sizeof(*in)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = yghv_protect_add_page(in->target_va);
        break;
    }
    case IOCTL_YGHV_REMOVE_PAGE: {
        yghv_ioctl_va_t *in = (yghv_ioctl_va_t *)buf;
        if (in_len < sizeof(*in)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = yghv_protect_remove_page(in->target_va);
        break;
    }
    case IOCTL_YGHV_START_PROTECT:
        status = yghv_protect_start();
        break;
    case IOCTL_YGHV_STOP_PROTECT:
        status = yghv_protect_stop();
        break;
    case IOCTL_YGHV_GET_STATE: {
        yghv_ioctl_state_t *out = (yghv_ioctl_state_t *)buf;
        ULONG active, pid, page_count;
        if (out_len < sizeof(*out)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        yghv_protect_get_state(&active, &pid, &page_count);
        out->active = active;
        out->pid = pid;
        out->page_count = page_count;
        info = sizeof(*out);
        break;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    if (!NT_SUCCESS(status))
        info = 0;
    return yghv_control_complete(irp, status, info);
}

NTSTATUS yghv_control_device_init(PDRIVER_OBJECT driver) {
    UNICODE_STRING dev_name, link_name;
    PDEVICE_OBJECT dev = NULL;
    NTSTATUS status;
    ULONG i;

    RtlInitUnicodeString(&dev_name, YGHV_DEVICE_NAME_STRING);
    RtlInitUnicodeString(&link_name, YGHV_DOS_DEVICE_NAME_STRING);

    status = IoCreateDevice(driver, 0, &dev_name, FILE_DEVICE_UNKNOWN, 0,
                            FALSE, &dev);
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("control device: IoCreateDevice failed 0x%x", status);
        return status;
    }
    status = IoCreateSymbolicLink(&link_name, &dev_name);
    if (!NT_SUCCESS(status)) {
        LOG_ERROR("control device: IoCreateSymbolicLink failed 0x%x", status);
        IoDeleteDevice(dev);
        return status;
    }

    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
        driver->MajorFunction[i] = yghv_control_dispatch_default;
    driver->MajorFunction[IRP_MJ_CREATE] = yghv_control_dispatch_open;
    driver->MajorFunction[IRP_MJ_CLOSE] = yghv_control_dispatch_open;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = yghv_control_dispatch_ioctl;

    dev->Flags |= DO_BUFFERED_IO;
    dev->Flags &= ~DO_DEVICE_INITIALIZING;
    g_yghv_device = dev;

    LOG_INFO("control device created");
    return STATUS_SUCCESS;
}

void yghv_control_device_cleanup(PDRIVER_OBJECT driver) {
    UNICODE_STRING link_name;
    (void)driver;

    RtlInitUnicodeString(&link_name, YGHV_DOS_DEVICE_NAME_STRING);
    IoDeleteSymbolicLink(&link_name);
    if (g_yghv_device) {
        IoDeleteDevice(g_yghv_device);
        g_yghv_device = NULL;
    }
    LOG_INFO("control device removed");
}
