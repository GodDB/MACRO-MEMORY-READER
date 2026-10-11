#pragma once
#include "shared/DriverProtocol.h"

// The slot/root address has already been resolved from the target image base.
template <typename TMemory>
void ReadTextAtAddress(const TMemory& memory, ULONG_PTR address, unsigned characterBytes,
                       DarkEdenProtocol::TextResponse& response) noexcept
{
    using namespace DarkEdenProtocol;
    if (characterBytes != 1 && characterBytes != 2)
    { response.status = STATUS_INVALID_PARAMETER; return; }
    if (address == 0 || address > 0xFFFFFFFFu)
    { response.status = STATUS_INVALID_ADDRESS; return; }
    response.textAddress = address;
    for (UInt32 index = 0; index + characterBytes <= sizeof(response.data); index += characterBytes)
    {
        if (address > 0xFFFFFFFFu - index)
        { response.status = STATUS_INTEGER_OVERFLOW; return; }
        const ULONG_PTR characterAddress = address + index;
        if (characterBytes == 1)
        {
            const auto value = memory.template Read<unsigned char>(characterAddress);
            if (!value.Succeeded())
            { response.status = NT_SUCCESS(value.status) ? STATUS_PARTIAL_COPY : value.status; return; }
            if (value.value == 0) { response.terminated = 1; response.status = STATUS_SUCCESS; return; }
            response.data[index] = value.value;
        }
        else
        {
            const auto value = memory.template Read<USHORT>(characterAddress);
            if (!value.Succeeded())
            { response.status = NT_SUCCESS(value.status) ? STATUS_PARTIAL_COPY : value.status; return; }
            if (value.value == 0) { response.terminated = 1; response.status = STATUS_SUCCESS; return; }
            response.data[index] = static_cast<unsigned char>(value.value);
            response.data[index + 1] = static_cast<unsigned char>(value.value >> 8);
        }
        response.bytesRead += characterBytes;
    }
    response.status = STATUS_SUCCESS; // Bounded preview; no terminator within 256 bytes.
}

template <typename TMemory>
void ReadPointedText(const TMemory& memory, ULONG_PTR slot, unsigned characterBytes,
                     DarkEdenProtocol::TextResponse& response) noexcept
{
    if (characterBytes != 1 && characterBytes != 2)
    { response.status = STATUS_INVALID_PARAMETER; return; }
    const auto pointer = memory.template Read<ULONG>(slot);
    if (!pointer.Succeeded())
    {
        response.status = NT_SUCCESS(pointer.status) ? STATUS_PARTIAL_COPY : pointer.status;
        return;
    }
    ReadTextAtAddress(memory, pointer.value, characterBytes, response);
}

template <typename TMemory>
void ReadOtherCharacterText(const TMemory& memory, ULONG_PTR root, unsigned characterBytes,
                            DarkEdenProtocol::TextResponse& response) noexcept
{
    if (characterBytes != 1 && characterBytes != 2)
    { response.status = STATUS_INVALID_PARAMETER; return; }
    // Cheat Engine shows offsets from the final string back to the module root.
    constexpr ULONG offsets[] = {0x34, 0, 0x14, 0x544, 0};
    ULONG_PTR address = root;
    for (const ULONG offset : offsets)
    {
        const auto pointer = memory.template Read<ULONG>(address);
        if (!pointer.Succeeded())
        {
            response.status = NT_SUCCESS(pointer.status) ? STATUS_PARTIAL_COPY : pointer.status;
            return;
        }
        if (pointer.value == 0)
        { response.status = STATUS_INVALID_ADDRESS; return; }
        if (pointer.value > 0xFFFFFFFFu - offset)
        { response.status = STATUS_INTEGER_OVERFLOW; return; }
        address = static_cast<ULONG_PTR>(pointer.value) + offset;
    }
    ReadTextAtAddress(memory, address, characterBytes, response);
}
