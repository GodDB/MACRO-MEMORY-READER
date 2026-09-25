#include "KernelProcessMemoryReader.h"

KernelProcessMemoryReader::~KernelProcessMemoryReader() noexcept
{
    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (process_ != nullptr)
    {
        ObDereferenceObject(process_);
    }
}

NTSTATUS KernelProcessMemoryReader::Initialize(HANDLE processId) noexcept
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (process_ != nullptr)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (processId == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PEPROCESS process = nullptr;
    const NTSTATUS status = PsLookupProcessByProcessId(processId, &process);
    if (NT_SUCCESS(status))
    {
        process_ = process;
    }
    return status;
}

NTSTATUS KernelProcessMemoryReader::InitializeForRequest(HANDLE processId) noexcept
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || process_ != nullptr)
        return STATUS_INVALID_DEVICE_STATE;
    if (processId == nullptr || reinterpret_cast<ULONG_PTR>(processId) > MAXULONG)
        return STATUS_INVALID_PARAMETER;

    // PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION (winnt.h).
    // The caller's security context remains active throughout this operation.
    constexpr ACCESS_MASK ProcessVmRead = 0x0010;
    constexpr ACCESS_MASK RequestedAccess = ProcessVmRead | 0x1000;
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, nullptr,
        OBJ_KERNEL_HANDLE | OBJ_FORCE_ACCESS_CHECK, nullptr, nullptr);
    CLIENT_ID client{};
    client.UniqueProcess = processId;
    HANDLE processHandle = nullptr;
    NTSTATUS status = ZwOpenProcess(&processHandle, RequestedAccess, &attributes, &client);
    if (!NT_SUCCESS(status)) return status;

    // Opening can return a handle with reduced rights. Verify the actual mask
    // before resolving a kernel-owned handle (KernelMode alone does not do so).
    PUBLIC_OBJECT_BASIC_INFORMATION info{};
    ULONG returned = 0;
    status = ZwQueryObject(processHandle, ObjectBasicInformation, &info, sizeof(info), &returned);
    if (NT_SUCCESS(status) && returned < sizeof(info)) status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(status) && (info.GrantedAccess & RequestedAccess) != RequestedAccess)
        status = STATUS_ACCESS_DENIED;
    PEPROCESS process = nullptr;
    if (NT_SUCCESS(status))
        status = ObReferenceObjectByHandle(processHandle, RequestedAccess,
            *PsProcessType, KernelMode, reinterpret_cast<void**>(&process), nullptr);
    ZwClose(processHandle);
    if (!NT_SUCCESS(status)) return status;

    // Resolve the image from the referenced object, avoiding PID reuse races.
    PUNICODE_STRING fullName = nullptr;
    status = SeLocateProcessImageName(process, &fullName);
    if (NT_SUCCESS(status))
    {
        UNICODE_STRING leaf = *fullName;
        USHORT start = static_cast<USHORT>(fullName->Length / sizeof(WCHAR));
        while (start > 0 && fullName->Buffer[start - 1] != L'\\') --start;
        leaf.Buffer += start;
        leaf.Length = static_cast<USHORT>(fullName->Length - start * sizeof(WCHAR));
        leaf.MaximumLength = leaf.Length;
        UNICODE_STRING expected = RTL_CONSTANT_STRING(L"darkeden.exe");
        if (!RtlEqualUnicodeString(&leaf, &expected, TRUE)) status = STATUS_OBJECT_NAME_INVALID;
        ExFreePool(fullName);
    }
    if (!NT_SUCCESS(status))
    {
        ObDereferenceObject(process);
        return status;
    }
    process_ = process;
    return STATUS_SUCCESS;
}

NTSTATUS KernelProcessMemoryReader::CopyValue(
    ULONG_PTR address, void* output, SIZE_T size, SIZE_T* bytesRead) const noexcept
{
    *bytesRead = 0;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || process_ == nullptr)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    // Only accept a complete range in the target's user address space.
    // Subtraction avoids integer overflow in address + size.
    const ULONG_PTR highestUserAddress =
        reinterpret_cast<ULONG_PTR>(MmHighestUserAddress);
    if (address == 0 || size == 0 || size > MaxReadSize ||
        address > highestUserAddress || size - 1 > highestUserAddress - address)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // MmCopyMemory requires a nonpageable destination. The active kernel stack
    // remains resident during this synchronous call. Copy to the caller's
    // output only after detaching, so it need not be nonpaged pool memory.
    UCHAR scratch[MaxReadSize] = {};
    MM_COPY_ADDRESS source = {};
    source.VirtualAddress = reinterpret_cast<void*>(address);
    KAPC_STATE apcState = {};
    SIZE_T copied = 0;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    KeStackAttachProcess(reinterpret_cast<PRKPROCESS>(process_), &apcState);
    __try
    {
        __try
        {
            status = MmCopyMemory(scratch, source, size, MM_COPY_MEMORY_VIRTUAL,
                                  &copied);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            status = GetExceptionCode();
        }
    }
    __finally
    {
        KeUnstackDetachProcess(&apcState);
    }

    *bytesRead = copied;
    if (!NT_SUCCESS(status))
    {
        return status;
    }
    if (copied != size)
    {
        return STATUS_PARTIAL_COPY;
    }

    RtlCopyMemory(output, scratch, size);
    return STATUS_SUCCESS;
}
