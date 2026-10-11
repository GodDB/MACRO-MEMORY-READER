#include "../gui/DriverClient.h"
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    using namespace DarkEdenProtocol;
    using namespace DarkEdenClient;
    if (argc != 2 && argc != 3) { std::wcerr << L"Usage: ReadDirectValue.exe <PID> [--x|--xy] (--single aliases --x)\n"; return 2; }
    const bool single = argc == 3 && (wcscmp(argv[2], L"--x") == 0 || wcscmp(argv[2], L"--single") == 0);
    const bool xy = argc == 3 && wcscmp(argv[2], L"--xy") == 0;
    if (argc == 3 && !single && !xy) return 2;
    wchar_t* end = nullptr;
    const auto pid = wcstoul(argv[1], &end, 10);
    if (pid == 0 || *end != L'\0') return 2;
    Request request{Version, sizeof(Request), 0, DefaultDirectOffset, Axis::X, 0, ValueType::Int32, 0};
    if (single) { request.baseAddress = DefaultXOffset; request.pointerBytes = 4; request.reserved = 3; }
    Response response{};
    std::wstring error;
    if (xy)
    {
        request.baseAddress = DefaultXOffset;
        request.axis = Axis::Both;
        if (!ReadPlayerCoordinates(pid, request, response, error))
        { std::wcerr << error << L"\n"; return 1; }
        std::wcout << L"PID " << pid << L" X=" << FormatValue(response.xBits, ValueType::Int32)
            << L" Y=" << FormatValue(response.yBits, ValueType::Int32) << L" bytes=" << response.bytesRead << L"\n";
        return 0;
    }
    if (!Read(pid, request, response, error))
    { std::wcerr << error << L"\n"; return 1; }
    std::wcout << L"PID " << pid << L" darkeden.exe + " << Hex(request.baseAddress)
        << (single ? L" -> read32 +4 -> read32 +A8 -> read32 +20C" : L" -> direct")
        << L" | bytes=" << response.bytesRead << L" | raw=" << Hex(response.xBits)
        << L" | int32=" << FormatValue(response.xBits, ValueType::Int32)
        << L" | float32=" << FormatValue(response.xBits, ValueType::Float32) << L"\n";
    return 0;
}
