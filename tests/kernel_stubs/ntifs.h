#pragma once
// API fakes for access/cleanup regression tests, not a kernel emulator.
#include "../stubs/ntifs.h"
#include <cassert>
#include <cstring>
#include <cwchar>
using UCHAR = unsigned char;
using WCHAR = wchar_t;
using USHORT = unsigned short;
using ACCESS_MASK = ULONG;
using PRKPROCESS = void*;
constexpr UCHAR PASSIVE_LEVEL = 0;
constexpr bool TRUE = true;
constexpr ULONG MAXULONG = 0xFFFFFFFFu;
constexpr ULONG OBJ_KERNEL_HANDLE = 0x200;
constexpr ULONG OBJ_FORCE_ACCESS_CHECK = 0x400;
constexpr ULONG MM_COPY_MEMORY_VIRTUAL = 2;
constexpr NTSTATUS STATUS_INVALID_DEVICE_STATE = static_cast<NTSTATUS>(0xC0000184u);
constexpr NTSTATUS STATUS_INFO_LENGTH_MISMATCH = static_cast<NTSTATUS>(0xC0000004u);
constexpr NTSTATUS STATUS_ACCESS_DENIED = static_cast<NTSTATUS>(0xC0000022u);
constexpr NTSTATUS STATUS_OBJECT_NAME_INVALID = static_cast<NTSTATUS>(0xC0000033u);
enum KPROCESSOR_MODE { KernelMode, UserMode };
enum OBJECT_INFORMATION_CLASS { ObjectBasicInformation };
struct UNICODE_STRING { USHORT Length; USHORT MaximumLength; WCHAR* Buffer; };
using PUNICODE_STRING = UNICODE_STRING*;
struct OBJECT_ATTRIBUTES { ULONG Length; HANDLE RootDirectory; UNICODE_STRING* ObjectName; ULONG Attributes; };
inline void InitializeObjectAttributes(OBJECT_ATTRIBUTES* a, UNICODE_STRING* name,
    ULONG flags, HANDLE root, void*)
{ *a = {sizeof(*a), root, name, flags}; }
struct CLIENT_ID { HANDLE UniqueProcess; HANDLE UniqueThread; };
struct PUBLIC_OBJECT_BASIC_INFORMATION
{ ULONG Attributes; ACCESS_MASK GrantedAccess; ULONG HandleCount; ULONG PointerCount; ULONG Reserved[10]; };
struct MM_COPY_ADDRESS { void* VirtualAddress; };
struct KAPC_STATE { int unused; };
extern void** PsProcessType;
extern void* MmHighestUserAddress;
UCHAR KeGetCurrentIrql();
NTSTATUS PsLookupProcessByProcessId(HANDLE, PEPROCESS*);
NTSTATUS ZwOpenProcess(HANDLE*, ACCESS_MASK, OBJECT_ATTRIBUTES*, CLIENT_ID*);
NTSTATUS ZwQueryObject(HANDLE, OBJECT_INFORMATION_CLASS, void*, ULONG, ULONG*);
NTSTATUS ZwClose(HANDLE);
NTSTATUS ObReferenceObjectByHandle(HANDLE, ACCESS_MASK, void*, KPROCESSOR_MODE, void**, void*);
void ObDereferenceObject(void*);
NTSTATUS SeLocateProcessImageName(PEPROCESS, UNICODE_STRING**);
bool RtlEqualUnicodeString(const UNICODE_STRING*, const UNICODE_STRING*, bool);
void ExFreePool(void*);
void KeStackAttachProcess(PRKPROCESS, KAPC_STATE*);
void KeUnstackDetachProcess(KAPC_STATE*);
NTSTATUS MmCopyMemory(void*, MM_COPY_ADDRESS, SIZE_T, ULONG, SIZE_T*);
#define NT_ASSERT(value) assert(value)
#define RTL_CONSTANT_STRING(value) {static_cast<USHORT>(sizeof(value) - sizeof(WCHAR)), static_cast<USHORT>(sizeof(value)), const_cast<WCHAR*>(value)}
#define RtlCopyMemory std::memcpy
#define EXCEPTION_EXECUTE_HANDLER 1
extern "C" unsigned long __cdecl _exception_code(void);
#define GetExceptionCode() static_cast<NTSTATUS>(_exception_code())
