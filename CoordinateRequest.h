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
    if (request.pointerBytes == 4 && request.valueType == ValueType::Int32)
        ReadCoordinateRequest<TMemory, ULONG, LONG>(memory, request, response);
    else if (request.pointerBytes == 4)
        ReadCoordinateRequest<TMemory, ULONG, float>(memory, request, response);
    else if (request.valueType == ValueType::Int32)
        ReadCoordinateRequest<TMemory, ULONGLONG, LONG>(memory, request, response);
    else
        ReadCoordinateRequest<TMemory, ULONGLONG, float>(memory, request, response);
}
