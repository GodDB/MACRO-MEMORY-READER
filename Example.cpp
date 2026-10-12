#include "Example.h"
#include "CoordinateRequest.h"
#include "TextRequest.h"

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

void ExecuteDarkEdenTextRequest(const DarkEdenProtocol::TextRequest& request,
                                DarkEdenProtocol::TextResponse& response) noexcept
{
    using namespace DarkEdenProtocol;
    response = {};
    response.version = Version;
    response.size = sizeof(TextResponse);
    response.status = STATUS_INVALID_PARAMETER;
    if (!ValidTextRequest(request)) return;
    KernelProcessMemoryReader memory;
    response.status = memory.InitializeForRequest(
        reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(request.processId)));
    if (!NT_SUCCESS(response.status)) return;
    ULONG_PTR slot = 0;
    response.status = memory.ResolveImageOffset(static_cast<ULONG_PTR>(request.baseAddress), &slot);
    if (!NT_SUCCESS(response.status)) return;
    if (request.reserved == 1)
        ReadOtherCharacterText(memory, slot, request.characterBytes, response);
    else
        ReadPointedText(memory, slot, request.characterBytes, response);
}

void ExecuteSelectTargetRequest(const DarkEdenProtocol::SelectTargetRequest& request,
                                DarkEdenProtocol::SelectTargetResponse& response) noexcept
{
    using namespace DarkEdenProtocol;
    response = {Version, sizeof(response), STATUS_INVALID_PARAMETER, 0};
    if (!ValidSelectTargetRequest(request)) return;
    KernelProcessMemoryReader memory;
    response.status = memory.InitializeForRequest(
        reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(request.processId)), true);
    if (!NT_SUCCESS(response.status)) return;

    ULONG_PTR controllerField = 0, treeField = 0, vtableAddress = 0;
    response.status = memory.ResolveImageOffset(TargetControllerOffset, &controllerField);
    if (!NT_SUCCESS(response.status)) return;
    response.status = memory.ResolveImageOffset(OtherCharacterNameOffset, &treeField);
    if (!NT_SUCCESS(response.status)) return;
    response.status = memory.ResolveImageOffset(TargetControllerVtableOffset, &vtableAddress);
    if (!NT_SUCCESS(response.status)) return;

    const auto controller = memory.Read<UInt32>(controllerField);
    const auto manager = memory.Read<UInt32>(treeField);
    if (!controller.Succeeded()) { response.status = controller.status; return; }
    if (!manager.Succeeded()) { response.status = manager.status; return; }
    if (!controller.value || !manager.value)
    { response.status = STATUS_NOT_FOUND; return; }
    const auto vtable = memory.Read<UInt32>(controller.value);
    if (!vtable.Succeeded()) { response.status = vtable.status; return; }
    if (vtable.value != vtableAddress)
    { response.status = STATUS_REVISION_MISMATCH; return; }

    const auto header = memory.Read<UInt32>(static_cast<ULONG_PTR>(manager.value) + 0x34);
    if (!header.Succeeded()) { response.status = header.status; return; }
    if (!header.value) { response.status = STATUS_NOT_FOUND; return; }
    const auto root = memory.Read<UInt32>(static_cast<ULONG_PTR>(header.value) + 4);
    if (!root.Succeeded()) { response.status = root.status; return; }
    UInt32 node = root.value;
    bool found = false;
    for (unsigned depth = 0; depth < 64 && node && node != header.value; ++depth)
    {
        const auto key = memory.Read<UInt32>(static_cast<ULONG_PTR>(node) + 0x10);
        if (!key.Succeeded()) { response.status = key.status; return; }
        if (key.value == request.targetId)
        {
            const auto actor = memory.Read<UInt32>(static_cast<ULONG_PTR>(node) + 0x14);
            if (!actor.Succeeded()) { response.status = actor.status; return; }
            if (actor.value)
            {
                const auto id = memory.Read<UInt32>(static_cast<ULONG_PTR>(actor.value) + 8);
                const auto xy = memory.Read<UInt32>(static_cast<ULONG_PTR>(actor.value) + 0x0C);
                if (id.Succeeded() && xy.Succeeded() && id.value == request.targetId &&
                    (xy.value & 0xFFFFu) < 5000 && (xy.value >> 16) < 5000)
                    found = true;
            }
            break;
        }
        const auto child = memory.Read<UInt32>(static_cast<ULONG_PTR>(node) +
            (request.targetId < key.value ? 0 : 8));
        if (!child.Succeeded()) { response.status = child.status; return; }
        if (!child.value || child.value == header.value) break;
        const auto parent = memory.Read<UInt32>(static_cast<ULONG_PTR>(child.value) + 4);
        if (!parent.Succeeded()) { response.status = parent.status; return; }
        if (parent.value != node) break;
        node = child.value;
    }
    if (!found) { response.status = STATUS_NOT_FOUND; return; }

    const ULONG_PTR slot = static_cast<ULONG_PTR>(controller.value) + 0x28;
    response.status = memory.WriteUInt32(slot, request.targetId);
    if (!NT_SUCCESS(response.status)) return;
    const auto selected = memory.Read<UInt32>(slot);
    if (!selected.Succeeded()) { response.status = selected.status; return; }
    response.selectedId = selected.value;
    response.status = selected.value == request.targetId ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
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
