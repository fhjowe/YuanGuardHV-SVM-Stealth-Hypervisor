#include <ntddk.h>
#include "control_ioctl.h"
#include "control_device.h"
#include "protect.h"
#include "debug.h"

NTKERNELAPI PEPROCESS IoGetRequestorProcess(PIRP Irp);

static PDEVICE_OBJECT g_yghv_device = NULL;

typedef struct {
    PEPROCESS owner_process;
    uint64_t owner_cr3;
} yghv_ctl_ctx_t;

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

static NTSTATUS yghv_control_dispatch_create(PDEVICE_OBJECT dev, PIRP irp) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PEPROCESS proc = IoGetRequestorProcess(irp);
    yghv_ctl_ctx_t *ctx;
    (void)dev;

    if (!proc) {
        LOG_ERROR("control device: create has no requestor process");
        return yghv_control_complete(irp, STATUS_ACCESS_DENIED, 0);
    }
    ctx = (yghv_ctl_ctx_t *)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(*ctx), YGHV_TAG);
    if (!ctx)
        return yghv_control_complete(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    ctx->owner_process = proc;
    ctx->owner_cr3 = *(volatile uint64_t *)((uint8_t *)proc + 0x028);
    ObReferenceObject(proc);
    stack->FileObject->FsContext = ctx;
    return yghv_control_complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS yghv_control_dispatch_cleanup(PDEVICE_OBJECT dev, PIRP irp) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    yghv_ctl_ctx_t *ctx = (yghv_ctl_ctx_t *)stack->FileObject->FsContext;
    (void)dev;

    if (ctx) {
        stack->FileObject->FsContext = NULL;
        ObDereferenceObject(ctx->owner_process);
        ExFreePoolWithTag(ctx, YGHV_TAG);
    }
    return yghv_control_complete(irp, STATUS_SUCCESS, 0);
}

static BOOLEAN yghv_control_ioctl_authorized(PIRP irp) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    yghv_ctl_ctx_t *ctx = (yghv_ctl_ctx_t *)stack->FileObject->FsContext;
    PEPROCESS proc = IoGetRequestorProcess(irp);
    uint64_t cr3;

    if (!ctx || !proc || proc != ctx->owner_process)
        return FALSE;
    /* P0 hardening: the controlling client must run with SeDebug enabled
       (elevated debugger-style client), not just any process that opened
       the device. */
    {
        LUID dbg_luid = RtlConvertLongToLuid(SE_DEBUG_PRIVILEGE);
        if (!SeSinglePrivilegeCheck(dbg_luid, UserMode)) {
            LOG_ERROR("control device: caller lacks SeDebugPrivilege");
            return FALSE;
        }
    }
    cr3 = *(volatile uint64_t *)((uint8_t *)proc + 0x028);
    return ctx->owner_cr3 != 0 && cr3 == ctx->owner_cr3;
}

static NTSTATUS yghv_control_dispatch_ioctl(PDEVICE_OBJECT dev, PIRP irp) {
    IO_STACK_LOCATION *stack;
    ULONG code, in_len, out_len;
    PVOID buf;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG info = 0;
    (void)dev;

    stack = IoGetCurrentIrpStackLocation(irp);
    if (!yghv_control_ioctl_authorized(irp)) {
        LOG_ERROR("control device: unauthorized caller rejected");
        return yghv_control_complete(irp, STATUS_ACCESS_DENIED, 0);
    }
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
    driver->MajorFunction[IRP_MJ_CREATE] = yghv_control_dispatch_create;
    driver->MajorFunction[IRP_MJ_CLOSE] = yghv_control_dispatch_open;
    driver->MajorFunction[IRP_MJ_CLEANUP] = yghv_control_dispatch_cleanup;
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
