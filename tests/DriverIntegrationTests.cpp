// Run elevated with the signed driver already loaded. Build this test executable
// as build/tests/fixture/darkeden.exe; it reads only its own synthetic memory.
#include "../gui/DriverClient.h"
#include <iostream>

int main()
{
    using namespace DarkEdenProtocol;
    using namespace DarkEdenClient;
    // Store the fixture in this image so the request uses a genuine module offset.
    alignas(8) static unsigned char data[0x1000]{};
    const auto base = reinterpret_cast<uintptr_t>(data);
    const auto imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base < imageBase) return 1;
    const auto offset = base - imageBase;
    auto finish = [](int result) { return result; };
    unsigned checks = 0;
    // Exercise the exact new X/Y paths using low 32-bit pointers owned by us.
    unsigned char* low = nullptr;
    for (uintptr_t candidate = 0x20000000; candidate < 0x60000000 && !low; candidate += 0x100000)
        low = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(candidate),
            0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!low) { std::cerr << "Cannot allocate 32-bit pointer fixture.\n"; return 1; }
    const auto lowAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(low));
    auto newPaths = [&]() {
        for (auto axis : {Axis::X, Axis::Y}) {
            std::memcpy(data, &lowAddress, 4);
            const uint32_t second = lowAddress + 0x100;
            const uint32_t third = lowAddress + 0x300;
            std::memcpy(low + (axis == Axis::X ? 4 : 0xC), &second, 4);
            std::memcpy(low + (axis == Axis::X ? 0x1A8 : 0x144), &third, 4);
            for (auto type : {ValueType::Int32, ValueType::Float32}) {
                const uint32_t bits = type == ValueType::Int32 ? 137u : 0xC0200000u;
                std::memcpy(low + (axis == Axis::X ? 0x50C : 0x510), &bits, 4);
                Request request{Version, sizeof(Request), 0, offset, axis, 4, type, axis == Axis::X ? 3u : 4u};
                Response response{};
                std::wstring error;
                if (!Read(GetCurrentProcessId(), request, response, error))
                { std::wcerr << error << L"\n"; return false; }
                if (response.bytesRead != 4 || response.xBits != (axis == Axis::X ? bits : 0) ||
                    response.yBits != (axis == Axis::Y ? bits : 0)) return false;
                ++checks;
            }
            std::memset(data, 0, 4);
            Request invalid{Version, sizeof(Request), 0, offset, axis, 4, ValueType::Int32, axis == Axis::X ? 3u : 4u};
            Response response{};
            std::wstring error;
            if (Read(GetCurrentProcessId(), invalid, response, error) ||
                response.status != static_cast<int32_t>(0xC0000141u) || response.bytesRead != 0) return false;
            ++checks;
        }
        return true;
    };
    const bool newPathsPassed = newPaths();
    if (newPathsPassed)
    {
        std::memcpy(data, &lowAddress, 4);
        std::memcpy(low, "TestName", 9);
        TextRequest textRequest{Version, sizeof(TextRequest), 0, offset, 1, 0};
        TextResponse textResponse{};
        std::wstring textError;
        if (!DarkEdenClient::ReadText(GetCurrentProcessId(), textRequest, textResponse, textError) ||
            textResponse.textAddress != lowAddress || textResponse.bytesRead != 8 ||
            textResponse.terminated != 1 || std::memcmp(textResponse.data, "TestName", 8) != 0)
        { std::wcerr << L"Narrow string read failed: " << textError << L"\n"; VirtualFree(low, 0, MEM_RELEASE); return 1; }
        ++checks;
        const uint32_t wideAddress = lowAddress + 0x100;
        std::memcpy(data, &wideAddress, 4);
        const wchar_t wideName[] = L"AB";
        std::memcpy(low + 0x100, wideName, sizeof(wideName));
        textRequest.characterBytes = 2;
        textResponse = {};
        if (!DarkEdenClient::ReadText(GetCurrentProcessId(), textRequest, textResponse, textError) ||
            textResponse.bytesRead != 4 || textResponse.terminated != 1 ||
            textResponse.data[0] != 'A' || textResponse.data[2] != 'B')
        { std::wcerr << L"Wide string read failed: " << textError << L"\n"; VirtualFree(low, 0, MEM_RELEASE); return 1; }
        ++checks;

        auto putPointer = [&](size_t slot, uint32_t value) { std::memcpy(low + slot, &value, 4); };
        std::memcpy(data, &lowAddress, 4);
        putPointer(0x34, lowAddress + 0x100);
        putPointer(0x100, lowAddress + 0x200);
        putPointer(0x214, lowAddress + 0x300);
        putPointer(0x844, lowAddress + 0x900);
        std::memcpy(low + 0x900, "OtherName", 10);
        textRequest.characterBytes = 1;
        textRequest.reserved = 1;
        textResponse = {};
        if (!DarkEdenClient::ReadText(GetCurrentProcessId(), textRequest, textResponse, textError) ||
            textResponse.textAddress != lowAddress + 0x900 || textResponse.bytesRead != 9 ||
            textResponse.terminated != 1 || std::memcmp(textResponse.data, "OtherName", 9) != 0)
        { std::wcerr << L"Other character name path failed: " << textError << L"\n"; VirtualFree(low, 0, MEM_RELEASE); return 1; }
        ++checks;
        const uint32_t packedCoordinates = (34u << 16) | 59u;
        std::memcpy(low + 0x30C, &packedCoordinates, sizeof(packedCoordinates));
        Request otherCoordinates{Version, sizeof(Request), 0, offset, Axis::Both, 4, ValueType::Int32, 5};
        Response otherCoordinatesResponse{};
        std::wstring otherCoordinatesError;
        if (!Read(GetCurrentProcessId(), otherCoordinates, otherCoordinatesResponse, otherCoordinatesError) ||
            otherCoordinatesResponse.bytesRead != 4 || otherCoordinatesResponse.xBits != 59 ||
            otherCoordinatesResponse.yBits != 34)
        { std::wcerr << L"Other character current coordinates failed: " << otherCoordinatesError << L"\n";
          VirtualFree(low, 0, MEM_RELEASE); return 1; }
        ++checks;
    }
    VirtualFree(low, 0, MEM_RELEASE);
    if (!newPathsPassed) { std::cerr << "New X/Y pointer path test failed.\n"; return 1; }
    for (auto type : {ValueType::Int32, ValueType::Float32})
    {
        for (uint32_t bits : {0u, 0xFFFFFF85u, 0x3FA00000u})
        {
            std::memcpy(data, &bits, 4);
            Request request{Version, sizeof(Request), 0, offset, Axis::X, 0, type, 0};
            Response response{};
            std::wstring error;
            if (!Read(GetCurrentProcessId(), request, response, error))
            { std::wcerr << error << L"\n"; return 1; }
            if (response.bytesRead != 4 || response.xBits != bits || response.yBits != 0)
            { std::cerr << "Direct value mismatch.\n"; return 1; }
            ++checks;
        }
    }
    for (uint32_t pointerBytes : {4u, 8u})
    {
        if (pointerBytes == 4 && base + sizeof(data) - 1 > UINT32_MAX)
        {
            std::cout << "SKIP: 32-bit chain fixture is loaded above 4 GiB.\n";
            continue;
        }
        std::memset(data, 0, 0x1000);
        const uint64_t first = base + 0x100, second = base + 0x200, third = base + 0x300;
        std::memcpy(data, &first, pointerBytes);
        std::memcpy(data + 0x100 + 0xC, &second, pointerBytes);
        std::memcpy(data + 0x200 + 0x44, &third, pointerBytes);
        for (auto type : {ValueType::Int32, ValueType::Float32})
        {
            const uint32_t x = type == ValueType::Int32 ? 123u : 0x3FA00000u; // 1.25f
            const uint32_t y = type == ValueType::Int32 ? static_cast<uint32_t>(-456) : 0xC0200000u; // -2.5f
            std::memcpy(data + 0x300 + 0x214, &x, 4);
            std::memcpy(data + 0x300 + 0x218, &y, 4);
            for (auto axis : {Axis::X, Axis::Y, Axis::Both})
            {
                Request request{Version, sizeof(Request), 0, offset, axis, pointerBytes, type, 0};
                Response response{};
                std::wstring error;
                if (!Read(GetCurrentProcessId(), request, response, error))
                { std::wcerr << error << L"\n"; return finish(1); }
                if (response.xBits != (axis == Axis::Y ? 0 : x) ||
                    response.yBits != (axis == Axis::X ? 0 : y))
                { std::cerr << "Coordinate mismatch.\n"; return finish(1); }
                ++checks;
            }
        }
        std::memset(data, 0, pointerBytes);
        Request request{Version, sizeof(Request), 0, offset, Axis::Both, pointerBytes, ValueType::Int32, 0};
        Response response{};
        std::wstring error;
        if (Read(GetCurrentProcessId(), request, response, error) || response.status >= 0 ||
            response.xBits != 0 || response.yBits != 0 || response.bytesRead != 0)
        { std::cerr << "Null pointer error was not propagated.\n"; return finish(1); }
        ++checks;
    }
    std::cout << "PASS: " << checks << " real driver IOCTL checks (self-owned fixture only).\n";
    return finish(0);
}
