#include "Example.h"
#include "CoordinateRequest.h"

// Call from driver code at PASSIVE_LEVEL. PID/address come from the caller.
// This is an integration example, not a standalone driver entry point.
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS ReadDarkEdenInt32(HANDLE darkedenPid, ULONG_PTR address, LONG* value)
{
    if (value == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *value = 0;

    KernelProcessMemoryReader reader;
    NTSTATUS status = reader.Initialize(darkedenPid);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    const auto result = reader.Read<LONG>(address);
    if (!result.Succeeded())
    {
        return result.status;
    }

    *value = result.value;
    return STATUS_SUCCESS;
}

// This is the entry called by the GUI's IOCTL. Actual reads still execute here
// in kernel mode via KernelProcessMemoryReader and MmCopyMemory.
void ExecuteDarkEdenRequest(const DarkEdenProtocol::Request& request,
                           DarkEdenProtocol::Response& response) noexcept
{
    using namespace DarkEdenProtocol;
    response = {Version, sizeof(Response), STATUS_INVALID_PARAMETER, 0, request.axis, request.valueType, 0, 0};
    if (!ValidRequest(request)) return;
    KernelProcessMemoryReader memory;
    response.status = memory.InitializeForRequest(
        reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(request.processId)));
    if (!NT_SUCCESS(response.status)) return;
    Request resolved = request;
    ULONG_PTR absoluteAddress = 0;
    response.status = memory.ResolveImageOffset(
        static_cast<ULONG_PTR>(request.baseAddress), &absoluteAddress);
    if (!NT_SUCCESS(response.status)) return;
    resolved.baseAddress = static_cast<UInt64>(absoluteAddress);
    DispatchCoordinateRequest(memory, resolved, response);
}

// Use the confirmed absolute base address 0x009CB97C.
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS ReadDarkEdenCoordinates(
    HANDLE darkedenPid,
    DarkEdenCoordinateReader::Coordinates* coordinates)
{
    if (coordinates == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *coordinates = {};

    KernelProcessMemoryReader memory;
    const NTSTATUS status = memory.Initialize(darkedenPid);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    DarkEdenCoordinateReader reader(memory);
    const auto result = reader.Read();
    if (!result.Succeeded())
    {
        return result.status;
    }
    *coordinates = result.value;
    return STATUS_SUCCESS;
}

// Each of the four results has its own status; this function's return status
// only indicates whether process initialization succeeded.
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS ReadAllDarkEdenCoordinateFormats(HANDLE darkedenPid,
                                         DarkEdenAllCoordinates* readings)
{
    if (readings == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *readings = {};
    KernelProcessMemoryReader memory;
    const NTSTATUS status = memory.Initialize(darkedenPid);
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    *readings = ReadAllDarkEdenCoordinates(memory);
    return STATUS_SUCCESS;
}
