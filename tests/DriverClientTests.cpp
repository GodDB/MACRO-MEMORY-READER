#include "../gui/DriverClient.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace DarkEdenClient;
    using namespace DarkEdenProtocol;
    assert(ParseAddress(L" 0x009CB97C ") == DefaultBaseAddress);
    assert(ParseAddress(L"009cb97c") == DefaultBaseAddress);
    assert(ParseAddress(L"0X7FFFFFFFFFFFFFFF") == INT64_MAX);
    for (auto invalid : {L"", L"0", L"0x", L"-1", L"+1", L"12 ZZ", L"xyz", L"10000000000000000", L"8000000000000000"})
    {
        bool threw = false;
        try { (void)ParseAddress(invalid); } catch (const std::invalid_argument&) { threw = true; }
        assert(threw);
    }
    assert(FormatValue(0, ValueType::Int32) == L"0");
    assert(FormatValue(static_cast<uint32_t>(-123), ValueType::Int32) == L"-123");
    assert(FormatValue(0x3FA00000, ValueType::Float32) == L"1.25");
    Request request{Version, sizeof(Request), 4, DefaultBaseAddress, Axis::Both, 4, ValueType::Int32, 0};
    Response response{Version, sizeof(Response), 0, 8, Axis::Both, ValueType::Int32, 0, 456};
    assert(ValidResponse(request, response, sizeof(response)));
    assert(!ValidResponse(request, response, sizeof(response) - 1));
    response.bytesRead = 4;
    assert(!ValidResponse(request, response, sizeof(response)));
    response.status = static_cast<int32_t>(0x8000000D);
    assert(!ValidResponse(request, response, sizeof(response))); // Failed responses must not contain values.
    response.yBits = 0;
    assert(ValidResponse(request, response, sizeof(response)));
    response.axis = Axis::X;
    assert(!ValidResponse(request, response, sizeof(response)));
    response.axis = Axis::Both;
    response.version = 1;
    assert(!ValidResponse(request, response, sizeof(response)));
    assert(DeviceError(ERROR_FILE_NOT_FOUND).find(L"Win32 2") != std::wstring::npos);
    assert(DeviceError(ERROR_ACCESS_DENIED).find(L"Win32 5") != std::wstring::npos);
    const auto stopped = FormatDeviceError(ERROR_FILE_NOT_FOUND,
        {ERROR_SUCCESS, SERVICE_STOPPED, ERROR_FAILED_DRIVER_ENTRY, 0});
    assert(stopped.find(L"Win32 647") != std::wstring::npos);
    assert(stopped.find(L"Win32 2") != std::wstring::npos);
    assert(stopped.find(L"초기화에 실패") != std::wstring::npos);
    assert(stopped.find(L"설치되지 않았") == std::wstring::npos);
    assert(FormatDeviceError(ERROR_FILE_NOT_FOUND, {ERROR_SERVICE_DOES_NOT_EXIST, 0, 0, 0})
        .find(L"설치되지 않았") != std::wstring::npos);
    assert(FormatDeviceError(ERROR_PATH_NOT_FOUND, {ERROR_SUCCESS, SERVICE_RUNNING, 0, 0})
        .find(L"실행 중") != std::wstring::npos);
    assert(FormatDeviceError(ERROR_FILE_NOT_FOUND, {ERROR_ACCESS_DENIED, 0, 0, 0})
        .find(L"상태 조회 실패") != std::wstring::npos);
    assert(FormatDeviceError(ERROR_FILE_NOT_FOUND, {ERROR_SUCCESS, SERVICE_START_PENDING, 0, 0})
        .find(L"상태 전환 중") != std::wstring::npos);
    assert(StatusMessage(static_cast<int32_t>(0xC0000005)).find(L"예외") != std::wstring::npos);
    std::cout << "Driver client validation and formatting tests passed.\n";
}
