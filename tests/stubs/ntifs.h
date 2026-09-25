#pragma once
// Type-only test shim. This does not emulate or validate Windows kernel APIs.
#include <cstddef>
#include <cstdint>
using NTSTATUS = std::int32_t;
using SIZE_T = std::size_t;
using LONG = std::int32_t;
using ULONG = std::uint32_t;
using ULONGLONG = std::uint64_t;
using ULONG_PTR = std::uintptr_t;
using HANDLE = void*;
using PEPROCESS = void*;
#define _IRQL_requires_(level)
#define NT_SUCCESS(status) ((status) >= 0)
constexpr NTSTATUS STATUS_SUCCESS = 0;
constexpr NTSTATUS STATUS_UNSUCCESSFUL = static_cast<NTSTATUS>(0xC0000001u);
constexpr NTSTATUS STATUS_PARTIAL_COPY = static_cast<NTSTATUS>(0x8000000Du);
constexpr NTSTATUS STATUS_INVALID_ADDRESS = static_cast<NTSTATUS>(0xC0000141u);
constexpr NTSTATUS STATUS_INTEGER_OVERFLOW = static_cast<NTSTATUS>(0xC0000095u);
constexpr NTSTATUS STATUS_INVALID_PARAMETER = static_cast<NTSTATUS>(0xC000000Du);
#define METHOD_BUFFERED 0
#define FILE_READ_ACCESS 1
#define CTL_CODE(type, function, method, access) (((type) << 16) | ((access) << 14) | ((function) << 2) | (method))
