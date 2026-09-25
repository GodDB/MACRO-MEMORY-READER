#pragma once
#include <stddef.h>

// Fixed-width, pointer-free wire format shared by the x64 GUI and WDM driver.
// Include Windows.h/winioctl.h or ntifs.h before this header for CTL_CODE.
namespace DarkEdenProtocol
{
using UInt32 = unsigned int;
using UInt64 = unsigned long long;
using Int32 = int;
static_assert(sizeof(UInt32) == 4 && sizeof(Int32) == 4 && sizeof(UInt64) == 8, "Unsupported integer widths");
constexpr UInt32 Version = 2;
constexpr UInt32 DeviceType = 0x8000;
constexpr UInt32 ReadCoordinates = CTL_CODE(DeviceType, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr wchar_t DevicePath[] = L"\\\\.\\DarkEdenMemoryReader";
constexpr UInt64 DefaultBaseAddress = 0x009CB97C;
enum class Axis : UInt32 { X = 1, Y = 2, Both = 3 };
enum class ValueType : UInt32 { Int32 = 1, Float32 = 2 };

struct Request
{
    UInt32 version;
    UInt32 size;
    UInt64 processId;     // PID only; the driver opens and owns the process handle.
    UInt64 baseAddress;   // Module-relative offset of the first pointer slot.
    Axis axis;
    UInt32 pointerBytes;
    ValueType valueType;
    UInt32 reserved;
};
struct Response
{
    UInt32 version;
    UInt32 size;
    Int32 status;         // NTSTATUS for the read (transport status is separate).
    UInt32 bytesRead;     // Final value bytes only; 0 for pointer-chain failures.
    Axis axis;
    ValueType valueType;
    UInt32 xBits;         // int32/float32 bit representation; zero on failure.
    UInt32 yBits;
};
static_assert(sizeof(Request) == 40 && offsetof(Request, axis) == 24, "Request ABI mismatch");
static_assert(sizeof(Response) == 32 && offsetof(Response, xBits) == 24, "Response ABI mismatch");

inline bool ValidRequest(const Request& r) noexcept
{
    return r.version == Version && r.size == sizeof(Request) && r.reserved == 0 &&
        r.processId != 0 && r.processId <= 0xFFFFFFFFULL && r.baseAddress != 0 &&
        (r.axis == Axis::X || r.axis == Axis::Y || r.axis == Axis::Both) &&
        (r.pointerBytes == 4 || r.pointerBytes == 8) &&
        (r.valueType == ValueType::Int32 || r.valueType == ValueType::Float32);
}
}
