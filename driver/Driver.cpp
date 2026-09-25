#include <ntifs.h>
#include <wdmsec.h>
#include "../Example.h"

namespace
{
// Unique device class; do not reuse a class GUID belonging to another driver.
const GUID DeviceClass = {0x8c95e94a, 0x1702, 0x4c6c, {0xa9,0x37,0x2f,0x63,0x99,0x02,0xdb,0x50}};
UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\DarkEdenMemoryReader");
UNICODE_STRING LinkName = RTL_CONSTANT_STRING(L"\\DosDevices\\DarkEdenMemoryReader");

// Keep startup diagnostics even when SCM reports only ERROR_FAILED_DRIVER_ENTRY.
void RecordInitialization(PUNICODE_STRING registryPath, ULONG stage, NTSTATUS status)
{
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, registryPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE key = nullptr;
    if (NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes)))
    {
        UNICODE_STRING stageName = RTL_CONSTANT_STRING(L"InitializationStage");
        UNICODE_STRING statusName = RTL_CONSTANT_STRING(L"InitializationStatus");
        ZwSetValueKey(key, &stageName, 0, REG_DWORD, &stage, sizeof(stage));
        ZwSetValueKey(key, &statusName, 0, REG_DWORD, &status, sizeof(status));
        ZwClose(key);
    }
}

NTSTATUS Complete(PIRP irp, NTSTATUS status, ULONG_PTR bytes = 0)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = bytes;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

DRIVER_DISPATCH Unsupported;
NTSTATUS Unsupported(PDEVICE_OBJECT, PIRP irp)
{
    return Complete(irp, STATUS_INVALID_DEVICE_REQUEST);
}
DRIVER_DISPATCH OpenClose;
NTSTATUS OpenClose(PDEVICE_OBJECT, PIRP irp)
{
    const auto stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->MajorFunction == IRP_MJ_CREATE && stack->FileObject->FileName.Length != 0)
        return Complete(irp, STATUS_OBJECT_NAME_NOT_FOUND);
    return Complete(irp, STATUS_SUCCESS);
}

DRIVER_DISPATCH DeviceControl;
NTSTATUS DeviceControl(PDEVICE_OBJECT, PIRP irp)
{
    using namespace DarkEdenProtocol;
    const auto stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->Parameters.DeviceIoControl.IoControlCode != ReadCoordinates)
        return Complete(irp, STATUS_INVALID_DEVICE_REQUEST);
    if (stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(Request) ||
        stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Response) ||
        irp->AssociatedIrp.SystemBuffer == nullptr)
        return Complete(irp, STATUS_BUFFER_TOO_SMALL);

    // Never queue this work: forced access checks must use the requesting
    // thread's effective token, not a system worker's security context.
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || irp->RequestorMode != UserMode ||
        IoGetRequestorProcess(irp) != PsGetCurrentProcess())
        return Complete(irp, STATUS_INVALID_DEVICE_STATE);

    const Request request = *static_cast<const Request*>(irp->AssociatedIrp.SystemBuffer);
    if (request.version != Version) return Complete(irp, STATUS_REVISION_MISMATCH);
    if (!ValidRequest(request)) return Complete(irp, STATUS_INVALID_PARAMETER);
    Response response{};
    ExecuteDarkEdenRequest(request, response);
    // Read failures travel in response.status, so the GUI can still display
    // bytesRead and the original NTSTATUS. Transport errors use IoStatus.
    RtlCopyMemory(irp->AssociatedIrp.SystemBuffer, &response, sizeof(response));
    return Complete(irp, STATUS_SUCCESS, sizeof(response));
}

DRIVER_UNLOAD Unload;
void Unload(PDRIVER_OBJECT driver)
{
    IoDeleteSymbolicLink(&LinkName);
    if (driver->DeviceObject) IoDeleteDevice(driver->DeviceObject);
}
}

extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registryPath)
{
    RecordInitialization(registryPath, 1, STATUS_SUCCESS);
    for (ULONG i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) driver->MajorFunction[i] = Unsupported;
    driver->MajorFunction[IRP_MJ_CREATE] = OpenClose;
    driver->MajorFunction[IRP_MJ_CLOSE] = OpenClose;
    driver->MajorFunction[IRP_MJ_CLEANUP] = OpenClose;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;
    driver->DriverUnload = Unload;

    PDEVICE_OBJECT device = nullptr;
    NTSTATUS status = IoCreateDeviceSecure(driver, 0, &DeviceName,
        DarkEdenProtocol::DeviceType, FILE_DEVICE_SECURE_OPEN, FALSE,
        &SDDL_DEVOBJ_SYS_ALL_ADM_ALL, &DeviceClass, &device);
    RecordInitialization(registryPath, 2, status);
    if (!NT_SUCCESS(status)) return status;
    status = IoCreateSymbolicLink(&LinkName, &DeviceName);
    RecordInitialization(registryPath, 3, status);
    if (!NT_SUCCESS(status)) { IoDeleteDevice(device); return status; }
    device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
