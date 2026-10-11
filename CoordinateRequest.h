#pragma once
#include "DarkEdenCoordinateReader.h"
#include "shared/DriverProtocol.h"

// Independent of device transport; used by Example.cpp and fake-memory tests.
template <typename TMemory, typename TPointer, typename TValue>
void ReadCoordinateRequest(const TMemory& memory, const DarkEdenProtocol::Request& request,
                           DarkEdenProtocol::Response& response) noexcept
{
    using namespace DarkEdenProtocol;
    BasicDarkEdenCoordinateReader<TMemory, TPointer, TValue> reader(memory);
    response = {Version, sizeof(Response), STATUS_UNSUCCESSFUL, 0, request.axis, request.valueType, 0, 0};
    // The transport entry resolves the module-relative offset before calling
    // this helper. Fake-memory tests may continue to pass absolute addresses.
    const auto base = static_cast<ULONG_PTR>(request.baseAddress);
    if (static_cast<UInt64>(base) != request.baseAddress)
    {
        response.status = STATUS_INTEGER_OVERFLOW;
        return;
    }
    if (request.axis == Axis::Both)
    {
        const auto result = reader.Read(base);
        response.status = result.Succeeded() ? STATUS_SUCCESS :
            (NT_SUCCESS(result.status) ? STATUS_PARTIAL_COPY : result.status);
        response.bytesRead = static_cast<UInt32>(result.bytesRead);
        if (result.Succeeded())
        {
            const auto* source = reinterpret_cast<const unsigned char*>(&result.value);
            auto* x = reinterpret_cast<unsigned char*>(&response.xBits);
            auto* y = reinterpret_cast<unsigned char*>(&response.yBits);
            for (SIZE_T i = 0; i < 4; ++i) { x[i] = source[i]; y[i] = source[i + 4]; }
        }
    }
    else
    {
        const auto result = request.axis == Axis::X ? reader.ReadX(base) : reader.ReadY(base);
        response.status = result.Succeeded() ? STATUS_SUCCESS :
            (NT_SUCCESS(result.status) ? STATUS_PARTIAL_COPY : result.status);
        response.bytesRead = static_cast<UInt32>(result.bytesRead);
        if (result.Succeeded())
        {
            auto* destination = reinterpret_cast<unsigned char*>(
                request.axis == Axis::X ? &response.xBits : &response.yBits);
            const auto* source = reinterpret_cast<const unsigned char*>(&result.value);
            for (SIZE_T i = 0; i < 4; ++i) destination[i] = source[i];
        }
    }
}

template <typename TMemory>
void DispatchCoordinateRequest(const TMemory& memory, const DarkEdenProtocol::Request& request,
                               DarkEdenProtocol::Response& response) noexcept
{
    using namespace DarkEdenProtocol;
    response = {Version, sizeof(Response), STATUS_INVALID_PARAMETER, 0, request.axis, request.valueType, 0, 0};
    if (!ValidRequest(request)) return;
    if (request.reserved == 5)
    {
        ULONG_PTR address = static_cast<ULONG_PTR>(request.baseAddress);
        if (static_cast<UInt64>(address) != request.baseAddress)
        { response.status = STATUS_INTEGER_OVERFLOW; return; }
        // Root -> character object -> packed current X/Y at +0x0C.
        constexpr ULONG offsets[] = {0x34, 0, 0x14, 0x0C};
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
        const auto packed = memory.template Read<ULONG>(address);
        response.status = packed.Succeeded() ? STATUS_SUCCESS :
            (NT_SUCCESS(packed.status) ? STATUS_PARTIAL_COPY : packed.status);
        response.bytesRead = static_cast<UInt32>(packed.bytesRead);
        if (packed.Succeeded())
        {
            response.xBits = packed.value & 0xFFFFu;
            response.yBits = packed.value >> 16;
        }
        return;
    }
    if (request.pointerBytes == 0 || request.reserved == 1 || request.reserved == 2 || request.reserved == 3 || request.reserved == 4)
    {
        // The entry point has already resolved image base + module offset.
        auto address = static_cast<ULONG_PTR>(request.baseAddress);
        if (static_cast<UInt64>(address) != request.baseAddress)
        {
            response.status = STATUS_INTEGER_OVERFLOW;
            return;
        }
        // CE lists offsets from the final value back toward the module root.
        const ULONG offsets[] = {
            request.reserved == 4 ? 0xCu : request.reserved >= 2 ? 4u : 0u,
            request.reserved == 4 ? 0x44u : request.reserved == 3 ? 0xA8u : 0x900u,
            request.reserved == 4 ? 0x210u : 0x20Cu};
        const unsigned levels = request.reserved == 4 ? 3u : request.reserved;
        for (unsigned level = 0; level < levels; ++level)
        {
            const auto pointer = memory.template Read<ULONG>(address);
            if (!pointer.Succeeded())
            {
                response.status = NT_SUCCESS(pointer.status) ? STATUS_PARTIAL_COPY : pointer.status;
                return;
            }
            if (pointer.value == 0)
            {
                response.status = STATUS_INVALID_ADDRESS;
                return;
            }
            if (pointer.value > 0xFFFFFFFFu - offsets[level])
            {
                response.status = STATUS_INTEGER_OVERFLOW;
                return;
            }
            address = static_cast<ULONG_PTR>(pointer.value) + offsets[level];
        }
        const auto result = memory.template Read<ULONG>(address);
        response.status = result.Succeeded() ? STATUS_SUCCESS :
            (NT_SUCCESS(result.status) ? STATUS_PARTIAL_COPY : result.status);
        response.bytesRead = static_cast<UInt32>(result.bytesRead);
        if (result.Succeeded())
        {
            if (request.axis == Axis::Y) response.yBits = result.value;
            else response.xBits = result.value;
        }
        return;
    }
    if (request.pointerBytes == 4 && request.valueType == ValueType::Int32)
        ReadCoordinateRequest<TMemory, ULONG, LONG>(memory, request, response);
    else if (request.pointerBytes == 4)
        ReadCoordinateRequest<TMemory, ULONG, float>(memory, request, response);
    else if (request.valueType == ValueType::Int32)
        ReadCoordinateRequest<TMemory, ULONGLONG, LONG>(memory, request, response);
    else
        ReadCoordinateRequest<TMemory, ULONGLONG, float>(memory, request, response);
}
