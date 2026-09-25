#include "../KernelProcessMemoryReader.h"
#include <cwctype>
#include <iostream>

namespace {
struct State {
    UCHAR irql = PASSIVE_LEVEL;
    NTSTATUS openStatus = STATUS_SUCCESS, queryStatus = STATUS_SUCCESS;
    NTSTATUS referenceStatus = STATUS_SUCCESS, imageStatus = STATUS_SUCCESS;
    ULONG access = 0x1010, queryLength = sizeof(PUBLIC_OBJECT_BASIC_INFORMATION);
    int opens = 0, closes = 0, references = 0, releases = 0, frees = 0, lookups = 0;
    const WCHAR* image = L"C:\\fixture\\darkeden.exe";
} state;
HANDLE testPid = reinterpret_cast<HANDLE>(1234);
HANDLE testHandle = reinterpret_cast<HANDLE>(0x200);
int object;
void* processType = &object;
}
void** PsProcessType = &processType;
void* MmHighestUserAddress = reinterpret_cast<void*>(0x7FFFFFFFFFFFULL);
UCHAR KeGetCurrentIrql() { return state.irql; }
NTSTATUS PsLookupProcessByProcessId(HANDLE, PEPROCESS*)
{ ++state.lookups; return STATUS_UNSUCCESSFUL; }
NTSTATUS ZwOpenProcess(HANDLE* handle, ACCESS_MASK access, OBJECT_ATTRIBUTES* a, CLIENT_ID* id)
{
    ++state.opens;
    assert(access == 0x1010);
    assert(a->Length == sizeof(*a) && a->ObjectName == nullptr && a->RootDirectory == nullptr);
    assert(a->Attributes == (OBJ_KERNEL_HANDLE | OBJ_FORCE_ACCESS_CHECK));
    assert(id->UniqueProcess == testPid && id->UniqueThread == nullptr);
    *handle = NT_SUCCESS(state.openStatus) ? testHandle : nullptr;
    return state.openStatus;
}
NTSTATUS ZwQueryObject(HANDLE handle, OBJECT_INFORMATION_CLASS kind, void* info, ULONG size, ULONG* returned)
{
    assert(handle == testHandle && kind == ObjectBasicInformation);
    assert(size == sizeof(PUBLIC_OBJECT_BASIC_INFORMATION));
    static_cast<PUBLIC_OBJECT_BASIC_INFORMATION*>(info)->GrantedAccess = state.access;
    *returned = state.queryLength;
    return state.queryStatus;
}
NTSTATUS ZwClose(HANDLE handle) { assert(handle == testHandle); ++state.closes; return STATUS_SUCCESS; }
NTSTATUS ObReferenceObjectByHandle(HANDLE handle, ACCESS_MASK access, void* type,
    KPROCESSOR_MODE mode, void** process, void*)
{
    ++state.references;
    assert(handle == testHandle && access == 0x1010 && type == *PsProcessType && mode == KernelMode);
    assert((state.access & 0x1010) == 0x1010);
    if (NT_SUCCESS(state.referenceStatus)) *process = &object;
    return state.referenceStatus;
}
void ObDereferenceObject(void* process) { assert(process == &object); ++state.releases; }
NTSTATUS SeLocateProcessImageName(PEPROCESS process, UNICODE_STRING** name)
{
    assert(process == &object);
    static UNICODE_STRING path;
    path.Buffer = const_cast<WCHAR*>(state.image);
    path.Length = static_cast<USHORT>(std::wcslen(state.image) * sizeof(WCHAR));
    path.MaximumLength = path.Length;
    if (NT_SUCCESS(state.imageStatus)) *name = &path;
    return state.imageStatus;
}
bool RtlEqualUnicodeString(const UNICODE_STRING* a, const UNICODE_STRING* b, bool ignoreCase)
{
    if (a->Length != b->Length) return false;
    for (SIZE_T i = 0; i < a->Length / sizeof(WCHAR); ++i)
        if (ignoreCase ? std::towlower(a->Buffer[i]) != std::towlower(b->Buffer[i]) : a->Buffer[i] != b->Buffer[i])
            return false;
    return true;
}
void ExFreePool(void*) { ++state.frees; }
void KeStackAttachProcess(PRKPROCESS, KAPC_STATE*) { assert(false); }
void KeUnstackDetachProcess(KAPC_STATE*) { assert(false); }
NTSTATUS MmCopyMemory(void*, MM_COPY_ADDRESS, SIZE_T, ULONG, SIZE_T*) { assert(false); return STATUS_UNSUCCESSFUL; }

int main()
{
    {
        KernelProcessMemoryReader reader;
        assert(reader.InitializeForRequest(testPid) == STATUS_SUCCESS);
        assert(reader.InitializeForRequest(testPid) == STATUS_INVALID_DEVICE_STATE);
        assert(state.opens == 1 && state.closes == 1 && state.references == 1 && state.releases == 0);
        assert(state.lookups == 0 && state.frees == 1);
    }
    assert(state.releases == 1);
    state = {};
    {
        KernelProcessMemoryReader reader;
        assert(reader.InitializeForRequest(nullptr) == STATUS_INVALID_PARAMETER);
        assert(reader.InitializeForRequest(reinterpret_cast<HANDLE>(0x100000000ULL)) == STATUS_INVALID_PARAMETER);
        state.irql = 2;
        assert(reader.InitializeForRequest(testPid) == STATUS_INVALID_DEVICE_STATE);
        state.irql = PASSIVE_LEVEL;
    }
    assert(state.opens == 0);
    state = {}; state.openStatus = STATUS_ACCESS_DENIED;
    { KernelProcessMemoryReader reader; assert(reader.InitializeForRequest(testPid) == STATUS_ACCESS_DENIED); }
    assert(state.closes == 0 && state.references == 0);
    for (ULONG mask : {0u, 0x1000u, 0x10u})
    {
        state = {}; state.access = mask;
        { KernelProcessMemoryReader reader; assert(reader.InitializeForRequest(testPid) == STATUS_ACCESS_DENIED); }
        assert(state.closes == 1 && state.references == 0 && state.lookups == 0);
    }
    state = {}; state.queryStatus = STATUS_UNSUCCESSFUL;
    { KernelProcessMemoryReader reader; assert(reader.InitializeForRequest(testPid) == STATUS_UNSUCCESSFUL); }
    assert(state.closes == 1 && state.references == 0);
    state = {}; state.queryLength = 0;
    { KernelProcessMemoryReader reader; assert(reader.InitializeForRequest(testPid) == STATUS_INFO_LENGTH_MISMATCH); }
    assert(state.closes == 1 && state.references == 0);
    state = {}; state.referenceStatus = STATUS_UNSUCCESSFUL;
    { KernelProcessMemoryReader reader; assert(reader.InitializeForRequest(testPid) == STATUS_UNSUCCESSFUL); }
    assert(state.closes == 1 && state.releases == 0);
    state = {}; state.imageStatus = STATUS_UNSUCCESSFUL;
    { KernelProcessMemoryReader reader; assert(reader.InitializeForRequest(testPid) == STATUS_UNSUCCESSFUL); }
    assert(state.closes == 1 && state.releases == 1 && state.frees == 0);
    state = {}; state.image = L"C:\\fixture\\other.exe";
    { KernelProcessMemoryReader reader; assert(reader.InitializeForRequest(testPid) == STATUS_OBJECT_NAME_INVALID); }
    assert(state.closes == 1 && state.releases == 1 && state.frees == 1);
    state = {}; state.image = L"C:\\fixture\\DARKEDEN.EXE";
    { KernelProcessMemoryReader reader; assert(reader.InitializeForRequest(testPid) == STATUS_SUCCESS); }
    assert(state.closes == 1 && state.releases == 1);
    std::cout << "Kernel process access and cleanup tests passed (mock APIs).\n";
}
