#pragma once

#include "KernelProcessMemoryReader.h"

template <typename TCoordinate>
struct DarkEdenCoordinates
{
    TCoordinate x;
    TCoordinate y;
};

// TMemoryReader can also be a fake reader for testing pointer-chain traversal.
template <typename TMemoryReader = KernelProcessMemoryReader,
          typename TPointer = ULONG, typename TCoordinate = LONG>
class BasicDarkEdenCoordinateReader final
{
public:
    static_assert(DarkEdenTypes::Same<TPointer, ULONG>::value ||
                  DarkEdenTypes::Same<TPointer, ULONGLONG>::value,
                  "Use ULONG for 32-bit pointers or ULONGLONG for 64-bit pointers.");
    static_assert(DarkEdenTypes::Same<TCoordinate, LONG>::value ||
                  DarkEdenTypes::Same<TCoordinate, float>::value,
                  "Coordinates must be a 4-byte LONG or float.");

    using Coordinates = DarkEdenCoordinates<TCoordinate>;
    static_assert(sizeof(Coordinates) == 8, "Coordinates must occupy 8 bytes.");

    static constexpr ULONG_PTR BaseOffset = 0x009CB97C;

    explicit BasicDarkEdenCoordinateReader(const TMemoryReader& memory) noexcept
        : memory_(memory)
    {
    }

    // basePointerAddress is an absolute address of the first pointer slot.
    _IRQL_requires_(PASSIVE_LEVEL)
    MemoryReadResult<TCoordinate> ReadX(ULONG_PTR basePointerAddress = BaseOffset) const noexcept
    {
        return ReadAtOffset<TCoordinate>(basePointerAddress, 0x214);
    }

    _IRQL_requires_(PASSIVE_LEVEL)
    MemoryReadResult<TCoordinate> ReadY(ULONG_PTR basePointerAddress = BaseOffset) const noexcept
    {
        return ReadAtOffset<TCoordinate>(basePointerAddress, 0x218);
    }

    // Resolve the shared chain once and copy adjacent x/y fields together.
    // The target can still update memory during this read; it is not atomic.
    _IRQL_requires_(PASSIVE_LEVEL)
    MemoryReadResult<Coordinates> Read(ULONG_PTR basePointerAddress = BaseOffset) const noexcept
    {
        return ReadAtOffset<Coordinates>(basePointerAddress, 0x214);
    }

    // Use when 0x009CB97C is relative to the darkeden.exe module base.
    _IRQL_requires_(PASSIVE_LEVEL)
    MemoryReadResult<Coordinates> ReadFromModule(ULONG_PTR moduleBase) const noexcept
    {
        ULONG_PTR basePointerAddress = 0;
        const NTSTATUS status = AddOffset(moduleBase, BaseOffset, basePointerAddress);
        if (!NT_SUCCESS(status))
        {
            MemoryReadResult<Coordinates> result{};
            result.status = status;
            return result;
        }
        return Read(basePointerAddress);
    }

private:
    const TMemoryReader& memory_; // Must outlive this reader.

    static NTSTATUS AddOffset(ULONGLONG pointer, ULONG_PTR offset,
                              ULONG_PTR& address) noexcept
    {
        // Reject null pointers and overflow in both target and host widths.
        const ULONGLONG targetMax = static_cast<TPointer>(~TPointer{0});
        const ULONGLONG hostMax = ~ULONG_PTR{0};
        const ULONGLONG limit = targetMax < hostMax ? targetMax : hostMax;
        if (pointer == 0)
        {
            return STATUS_INVALID_ADDRESS;
        }
        if (pointer > limit || offset > limit - pointer)
        {
            return STATUS_INTEGER_OVERFLOW;
        }
        address = static_cast<ULONG_PTR>(pointer + offset);
        return STATUS_SUCCESS;
    }

    template <typename TValue>
    MemoryReadResult<TValue> ReadAtOffset(ULONG_PTR basePointerAddress,
                                         ULONG finalOffset) const noexcept
    {
        MemoryReadResult<TValue> failure{};
        ULONG_PTR address = 0;
        failure.status = AddOffset(basePointerAddress, 0, address);
        if (!NT_SUCCESS(failure.status))
        {
            return failure;
        }

        const ULONG offsets[] = {0xC, 0x44, finalOffset};
        for (const ULONG offset : offsets)
        {
            const auto pointer = memory_.template Read<TPointer>(address);
            if (!pointer.Succeeded())
            {
                // bytesRead describes the final value, not intermediate pointers.
                failure.status = NT_SUCCESS(pointer.status)
                    ? STATUS_PARTIAL_COPY : pointer.status;
                return failure;
            }
            failure.status = AddOffset(pointer.value, offset, address);
            if (!NT_SUCCESS(failure.status))
            {
                return failure;
            }
        }

        // The last offset identifies the value itself, not another pointer.
        return memory_.template Read<TValue>(address);
    }
};

using DarkEdenCoordinateReader = BasicDarkEdenCoordinateReader<>;

struct DarkEdenAllCoordinates
{
    MemoryReadResult<DarkEdenCoordinates<LONG>> pointer32Int;
    MemoryReadResult<DarkEdenCoordinates<float>> pointer32Float;
    MemoryReadResult<DarkEdenCoordinates<LONG>> pointer64Int;
    MemoryReadResult<DarkEdenCoordinates<float>> pointer64Float;
};

namespace DarkEdenDetail
{
    inline MemoryReadResult<DarkEdenCoordinates<float>> InterpretAsFloat(
        const MemoryReadResult<DarkEdenCoordinates<LONG>>& source) noexcept
    {
        MemoryReadResult<DarkEdenCoordinates<float>> result{};
        static_assert(sizeof(result.value) == sizeof(source.value), "Size mismatch.");
        result.status = source.status;
        result.bytesRead = source.bytesRead;
        if (source.Succeeded())
        {
            // Copy representations, not numeric values; both views use one read.
            const auto* from = reinterpret_cast<const unsigned char*>(&source.value);
            auto* to = reinterpret_cast<unsigned char*>(&result.value);
            for (SIZE_T i = 0; i < sizeof(result.value); ++i)
            {
                to[i] = from[i];
            }
        }
        return result;
    }
}

// Diagnostic views: success does not identify the correct pointer/data type.
template <typename TMemoryReader>
_IRQL_requires_(PASSIVE_LEVEL)
DarkEdenAllCoordinates ReadAllDarkEdenCoordinates(
    const TMemoryReader& memory,
    ULONG_PTR basePointerAddress = DarkEdenCoordinateReader::BaseOffset) noexcept
{
    BasicDarkEdenCoordinateReader<TMemoryReader, ULONG, LONG> reader32(memory);
    BasicDarkEdenCoordinateReader<TMemoryReader, ULONGLONG, LONG> reader64(memory);
    DarkEdenAllCoordinates result{};
    result.pointer32Int = reader32.Read(basePointerAddress);
    result.pointer32Float = DarkEdenDetail::InterpretAsFloat(result.pointer32Int);
    result.pointer64Int = reader64.Read(basePointerAddress);
    result.pointer64Float = DarkEdenDetail::InterpretAsFloat(result.pointer64Int);
    return result;
}
