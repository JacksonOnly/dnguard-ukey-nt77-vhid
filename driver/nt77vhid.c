#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>
#include <wdmsec.h>

#include "nt77vhid_public.h"
#include "../src/protocol/nt77_protocol.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD Nt77EvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP Nt77EvtDeviceCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL Nt77EvtIoDeviceControl;
EVT_VHF_ASYNC_OPERATION Nt77EvtVhfSetFeature;
EVT_VHF_ASYNC_OPERATION Nt77EvtVhfGetFeature;

typedef struct NT77_DEVICE_CONTEXT {
    VHFHANDLE VhfHandle;
    WDFSPINLOCK Lock;
    NT77_PROTOCOL_STATE Protocol;
    UCHAR PendingResponse[NT77_PACKET_SIZE];
    BOOLEAN ResponseReady;
} NT77_DEVICE_CONTEXT, *PNT77_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NT77_DEVICE_CONTEXT, Nt77GetContext)

static const UCHAR Nt77ReportDescriptor[] = {
    0x06, 0x00, 0xFF,       /* Usage Page (Vendor Defined 0xFF00) */
    0x09, 0x01,             /* Usage (1) */
    0xA1, 0x01,             /* Collection (Application) */
    0x15, 0x00,             /* Logical Minimum (0) */
    0x26, 0xFF, 0x00,       /* Logical Maximum (255) */
    0x75, 0x08,             /* Report Size (8) */
    0x95, 0x30,             /* Report Count (48) */
    0x19, 0x01,             /* Usage Minimum (1) */
    0x29, 0x30,             /* Usage Maximum (48) */
    0xB1, 0x02,             /* Feature (Data,Var,Abs) */
    0xC0                    /* End Collection */
};

static NTSTATUS Nt77GetFeaturePayload(
    PHID_XFER_PACKET Packet,
    PUCHAR *Payload)
{
    if (Packet == NULL || Payload == NULL || Packet->reportBuffer == NULL ||
        Packet->reportId != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Packet->reportBufferLen == NT77_PACKET_SIZE) {
        *Payload = Packet->reportBuffer;
        return STATUS_SUCCESS;
    }

    if (Packet->reportBufferLen == NT77_PACKET_SIZE + 1u &&
        Packet->reportBuffer[0] == 0) {
        *Payload = Packet->reportBuffer + 1;
        return STATUS_SUCCESS;
    }

    return STATUS_BUFFER_TOO_SMALL;
}

static VOID Nt77ResetExchange(PNT77_DEVICE_CONTEXT Context)
{
    Nt77ProtocolResetSession(&Context->Protocol);
    RtlZeroMemory(Context->PendingResponse, sizeof(Context->PendingResponse));
    Context->ResponseReady = FALSE;
}

static BOOLEAN Nt77IsConfigurationIoctl(ULONG IoControlCode)
{
    switch (IoControlCode) {
    case IOCTL_NT77_SET_SERIAL:
    case IOCTL_NT77_SET_LOGIN_MD5:
    case IOCTL_NT77_SET_HARDWARE_ID:
    case IOCTL_NT77_WRITE_STORAGE:
    case IOCTL_NT77_CLEAR_STORAGE:
        return TRUE;
    default:
        return FALSE;
    }
}

VOID Nt77EvtVhfSetFeature(
    PVOID VhfClientContext,
    VHFOPERATIONHANDLE VhfOperationHandle,
    PVOID VhfOperationContext,
    PHID_XFER_PACKET HidTransferPacket)
{
    PNT77_DEVICE_CONTEXT context = (PNT77_DEVICE_CONTEXT)VhfClientContext;
    NT77_PROCESS_RESULT result;
    NTSTATUS status;
    PUCHAR payload = NULL;

    UNREFERENCED_PARAMETER(VhfOperationContext);
    status = Nt77GetFeaturePayload(HidTransferPacket, &payload);
    if (NT_SUCCESS(status)) {
        WdfSpinLockAcquire(context->Lock);
        result = Nt77ProtocolProcess(
            &context->Protocol, payload, context->PendingResponse);
        context->ResponseReady = (result == NT77_PROCESS_OK) ? TRUE : FALSE;
        WdfSpinLockRelease(context->Lock);

        if (result == NT77_PROCESS_BAD_FRAME) {
            status = STATUS_DATA_ERROR;
        } else if (result != NT77_PROCESS_OK) {
            status = STATUS_INVALID_PARAMETER;
        }
    }

    (VOID)VhfAsyncOperationComplete(VhfOperationHandle, status);
}

VOID Nt77EvtVhfGetFeature(
    PVOID VhfClientContext,
    VHFOPERATIONHANDLE VhfOperationHandle,
    PVOID VhfOperationContext,
    PHID_XFER_PACKET HidTransferPacket)
{
    PNT77_DEVICE_CONTEXT context = (PNT77_DEVICE_CONTEXT)VhfClientContext;
    NTSTATUS status;
    PUCHAR payload = NULL;

    UNREFERENCED_PARAMETER(VhfOperationContext);
    status = Nt77GetFeaturePayload(HidTransferPacket, &payload);
    if (NT_SUCCESS(status)) {
        WdfSpinLockAcquire(context->Lock);
        if (!context->ResponseReady) {
            status = STATUS_DEVICE_NOT_READY;
        } else {
            RtlCopyMemory(payload,
                          context->PendingResponse,
                          sizeof(context->PendingResponse));
            context->ResponseReady = FALSE;
        }
        WdfSpinLockRelease(context->Lock);
    }

    (VOID)VhfAsyncOperationComplete(VhfOperationHandle, status);
}

static NTSTATUS Nt77CreateVhfDevice(
    WDFDEVICE Device,
    PNT77_DEVICE_CONTEXT Context)
{
    VHF_CONFIG config;
    NTSTATUS status;

    VHF_CONFIG_INIT(
        &config,
        WdfDeviceWdmGetDeviceObject(Device),
        (USHORT)sizeof(Nt77ReportDescriptor),
        (PUCHAR)Nt77ReportDescriptor);
    config.VhfClientContext = Context;
    config.VendorID = 0x0925;
    config.ProductID = 0xA508;
    config.VersionNumber = 0x0100;
    config.EvtVhfAsyncOperationSetFeature = Nt77EvtVhfSetFeature;
    config.EvtVhfAsyncOperationGetFeature = Nt77EvtVhfGetFeature;

    status = VhfCreate(&config, &Context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = VhfStart(Context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        VhfDelete(Context->VhfHandle, TRUE);
        Context->VhfHandle = NULL;
    }
    return status;
}

VOID Nt77EvtIoDeviceControl(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t InputBufferLength,
    ULONG IoControlCode)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PNT77_DEVICE_CONTEXT context = Nt77GetContext(device);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    size_t information = 0;
    PVOID buffer = NULL;

    /* Fixed builds reject all configuration writes before touching user data. */
    if (Nt77IsConfigurationIoctl(IoControlCode) &&
        !Nt77ProtocolAllowsExternalConfiguration()) {
        WdfRequestCompleteWithInformation(
            Request, STATUS_ACCESS_DENIED, 0);
        return;
    }

    switch (IoControlCode) {
    case IOCTL_NT77_SET_SERIAL:
        if (InputBufferLength != sizeof(NT77VHID_SERIAL_CONFIG)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(NT77VHID_SERIAL_CONFIG), &buffer, NULL);
        if (NT_SUCCESS(status)) {
            WdfSpinLockAcquire(context->Lock);
            RtlCopyMemory(context->Protocol.Serial,
                          ((NT77VHID_SERIAL_CONFIG *)buffer)->Bytes,
                          sizeof(context->Protocol.Serial));
            context->Protocol.SerialConfigured = true;
            Nt77ResetExchange(context);
            WdfSpinLockRelease(context->Lock);
        }
        break;

    case IOCTL_NT77_SET_LOGIN_MD5:
        if (InputBufferLength != sizeof(NT77VHID_LOGIN_CONFIG)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(NT77VHID_LOGIN_CONFIG), &buffer, NULL);
        if (NT_SUCCESS(status)) {
            WdfSpinLockAcquire(context->Lock);
            RtlCopyMemory(context->Protocol.LoginDigest,
                          ((NT77VHID_LOGIN_CONFIG *)buffer)->Digest,
                          sizeof(context->Protocol.LoginDigest));
            context->Protocol.LoginConfigured = true;
            Nt77ResetExchange(context);
            WdfSpinLockRelease(context->Lock);
        }
        break;

    case IOCTL_NT77_SET_HARDWARE_ID:
        if (InputBufferLength != sizeof(NT77VHID_HARDWARE_ID_CONFIG)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = WdfRequestRetrieveInputBuffer(
            Request, sizeof(NT77VHID_HARDWARE_ID_CONFIG), &buffer, NULL);
        if (NT_SUCCESS(status)) {
            WdfSpinLockAcquire(context->Lock);
            RtlCopyMemory(context->Protocol.HardwareId,
                          ((NT77VHID_HARDWARE_ID_CONFIG *)buffer)->Bytes,
                          sizeof(context->Protocol.HardwareId));
            context->Protocol.HardwareIdConfigured = true;
            Nt77ResetExchange(context);
            WdfSpinLockRelease(context->Lock);
        }
        break;

    case IOCTL_NT77_WRITE_STORAGE:
        if (InputBufferLength < FIELD_OFFSET(NT77VHID_STORAGE_WRITE, Data)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        status = WdfRequestRetrieveInputBuffer(
            Request, FIELD_OFFSET(NT77VHID_STORAGE_WRITE, Data), &buffer, NULL);
        if (NT_SUCCESS(status)) {
            NT77VHID_STORAGE_WRITE *write = (NT77VHID_STORAGE_WRITE *)buffer;
            const size_t header = FIELD_OFFSET(NT77VHID_STORAGE_WRITE, Data);
            if ((size_t)write->Length != InputBufferLength - header ||
                (size_t)write->Offset + write->Length > NT77_STORAGE_SIZE) {
                status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }
            WdfSpinLockAcquire(context->Lock);
            RtlCopyMemory(&context->Protocol.Storage[write->Offset],
                          write->Data, write->Length);
            Nt77ResetExchange(context);
            WdfSpinLockRelease(context->Lock);
        }
        break;

    case IOCTL_NT77_CLEAR_STORAGE:
        if (InputBufferLength != 0) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        WdfSpinLockAcquire(context->Lock);
        RtlZeroMemory(context->Protocol.Storage,
                      sizeof(context->Protocol.Storage));
        Nt77ResetExchange(context);
        WdfSpinLockRelease(context->Lock);
        status = STATUS_SUCCESS;
        break;

    case IOCTL_NT77_QUERY_STATUS:
        if (OutputBufferLength < sizeof(NT77VHID_STATUS)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = WdfRequestRetrieveOutputBuffer(
            Request, sizeof(NT77VHID_STATUS), &buffer, NULL);
        if (NT_SUCCESS(status)) {
            NT77VHID_STATUS *result = (NT77VHID_STATUS *)buffer;
            WdfSpinLockAcquire(context->Lock);
            RtlZeroMemory(result, sizeof(*result));
            result->SerialConfigured = context->Protocol.SerialConfigured;
            result->LoginConfigured = context->Protocol.LoginConfigured;
            result->HardwareIdConfigured =
                context->Protocol.HardwareIdConfigured;
            result->LoggedIn = context->Protocol.LoggedIn;
            RtlCopyMemory(result->Serial, context->Protocol.Serial,
                          sizeof(result->Serial));
            RtlCopyMemory(result->HardwareId, context->Protocol.HardwareId,
                          sizeof(result->HardwareId));
            WdfSpinLockRelease(context->Lock);
            information = sizeof(*result);
        }
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, information);
}

VOID Nt77EvtDeviceCleanup(WDFOBJECT DeviceObject)
{
    PNT77_DEVICE_CONTEXT context = Nt77GetContext(DeviceObject);

    PAGED_CODE();
    if (context->VhfHandle != NULL) {
        VhfDelete(context->VhfHandle, TRUE);
        context->VhfHandle = NULL;
    }
}

NTSTATUS Nt77EvtDeviceAdd(
    WDFDRIVER Driver,
    PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    UNICODE_STRING symbolicLink;
    WDFDEVICE device;
    PNT77_DEVICE_CONTEXT context;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(DeviceInit, TRUE);
    WdfDeviceInitSetCharacteristics(
        DeviceInit, FILE_AUTOGENERATED_DEVICE_NAME, TRUE);
    status = WdfDeviceInitAssignSDDLString(
        DeviceInit, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, NT77_DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = Nt77EvtDeviceCleanup;
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = Nt77GetContext(device);
    context->VhfHandle = NULL;
    context->ResponseReady = FALSE;
    Nt77ProtocolInitialize(&context->Protocol);

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = device;
    status = WdfSpinLockCreate(&lockAttributes, &context->Lock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&symbolicLink, L"\\DosDevices\\Nt77VhidControl");
    status = WdfDeviceCreateSymbolicLink(device, &symbolicLink);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = Nt77EvtIoDeviceControl;
    status = WdfIoQueueCreate(
        device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    return Nt77CreateVhfDevice(device, context);
}

NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, Nt77EvtDeviceAdd);
    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE);
}
