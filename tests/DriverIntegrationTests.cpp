// Run elevated with the signed driver already loaded. Build this test executable
// as build/tests/fixture/darkeden.exe; it reads only its own synthetic memory.
#include "../gui/DriverClient.h"
#include <iostream>

int main()
{
    using namespace DarkEdenProtocol;
    using namespace DarkEdenClient;
    void* allocation = nullptr;
    // A low allocation exercises both 32-bit and 64-bit target pointer formats.
    for (uintptr_t address = 0x20000000; address < 0x60000000 && !allocation; address += 0x100000)
        allocation = VirtualAlloc(reinterpret_cast<void*>(address), 0x1000,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!allocation) { std::cerr << "Cannot allocate fixture memory.\n"; return 1; }
    auto data = static_cast<unsigned char*>(allocation);
    const auto base = reinterpret_cast<uintptr_t>(allocation);
    auto finish = [&](int result) { VirtualFree(allocation, 0, MEM_RELEASE); return result; };
    unsigned checks = 0;
    for (uint32_t pointerBytes : {4u, 8u})
    {
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
                Request request{Version, sizeof(Request), 0, base, axis, pointerBytes, type, 0};
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
        Request request{Version, sizeof(Request), 0, base, Axis::Both, pointerBytes, ValueType::Int32, 0};
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
