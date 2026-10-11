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
constexpr UInt32 ReadText = CTL_CODE(DeviceType, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr wchar_t DevicePath[] = L"\\\\.\\DarkEdenMemoryReader";
constexpr UInt64 DefaultBaseAddress = 0x009CB97C;
constexpr UInt64 DefaultDirectOffset = 0x009E3868;
constexpr UInt64 DefaultXOffset = 0x009FB03C; // X: 32-bit pointers +4, +0xA8, then value at +0x20C.
constexpr UInt64 DefaultYOffset = 0x009DF9A8; // Y: 32-bit pointers +C, +44, then value at +210.
constexpr UInt64 DefaultTextOffset = 0x000A8B14;
constexpr UInt64 OtherCharacterNameOffset = 0x00AB31D0;
enum class Axis : UInt32 { X = 1, Y = 2, Both = 3 };
enum class ValueType : UInt32 { Int32 = 1, Float32 = 2 };

struct Request
{
    UInt32 version;
    UInt32 size;
    UInt64 processId;     // PID only; the driver opens and owns the process handle.
    UInt64 baseAddress;   // Module-relative offset of the value or first pointer slot.
    Axis axis;
    UInt32 pointerBytes;  // 0: direct 4-byte value (Axis::X only); 4/8: pointer chain.
    ValueType valueType;
    UInt32 reserved;      // 0: legacy/direct; 1-4: player paths; 5: other character's current packed 16-bit X/Y.
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

// A separate command keeps the existing coordinate wire format unchanged.
struct TextRequest
{
    UInt32 version;
    UInt32 size;
    UInt64 processId;
    UInt64 baseAddress; // darkeden.exe-relative address of a 32-bit pointer.
    UInt32 characterBytes; // 1: UTF-8, 2: UTF-16LE.
    UInt32 reserved; // 0: own name pointer; 1: other character name pointer chain.
};
struct TextResponse
{
    UInt32 version;
    UInt32 size;
    Int32 status;
    UInt32 bytesRead; // Data bytes, excluding the terminator.
    UInt64 textAddress;
    UInt32 terminated;
    UInt32 reserved;
    unsigned char data[256];
};
static_assert(sizeof(TextRequest) == 32 && sizeof(TextResponse) == 288, "Text ABI mismatch");
inline bool ValidTextRequest(const TextRequest& r) noexcept
{
    return r.version == Version && r.size == sizeof(TextRequest) && r.processId != 0 &&
        r.processId <= 0xFFFFFFFFULL && r.baseAddress != 0 &&
        r.characterBytes >= 1 && r.characterBytes <= 2 && r.reserved <= 1;
}

inline bool ValidRequest(const Request& r) noexcept
{
    return r.version == Version && r.size == sizeof(Request) &&
        (r.reserved == 0 || (r.reserved == 1 && r.pointerBytes == 4 && r.axis == Axis::X) ||
         (r.reserved == 2 && r.pointerBytes == 4 && r.axis == Axis::Y) ||
         (r.reserved == 3 && r.pointerBytes == 4 && r.axis == Axis::X) ||
         (r.reserved == 4 && r.pointerBytes == 4 && r.axis == Axis::Y) ||
         (r.reserved == 5 && r.pointerBytes == 4 && r.axis == Axis::Both &&
          r.valueType == ValueType::Int32)) &&
        r.processId != 0 && r.processId <= 0xFFFFFFFFULL && r.baseAddress != 0 &&
        (r.axis == Axis::X || r.axis == Axis::Y || r.axis == Axis::Both) &&
        ((r.pointerBytes == 0 && r.axis == Axis::X) || r.pointerBytes == 4 || r.pointerBytes == 8) &&
        (r.valueType == ValueType::Int32 || r.valueType == ValueType::Float32);
}
}
