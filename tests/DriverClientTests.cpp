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
    Request otherCoordinates{Version, sizeof(Request), 4, OtherCharacterNameOffset,
                             Axis::Both, 4, ValueType::Int32, 5};
    Response otherCoordinatesResponse{Version, sizeof(Response), 0, 4, Axis::Both,
                                      ValueType::Int32, 59, 34};
    assert(ValidRequest(otherCoordinates));
    assert(ValidResponse(otherCoordinates, otherCoordinatesResponse, sizeof(otherCoordinatesResponse)));
    otherCoordinatesResponse.bytesRead = 8;
    assert(!ValidResponse(otherCoordinates, otherCoordinatesResponse, sizeof(otherCoordinatesResponse)));
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
    Request pair{Version, sizeof(Request), 0, DefaultXOffset, Axis::Both, 4, ValueType::Int32, 3};
    int calls = 0;
    bool failY = false;
    auto transport = [&](DWORD pid, Request part, Response& out, std::wstring& error) {
        ++calls;
        part.processId = pid;
        assert(ValidRequest(part));
        if (part.axis == Axis::X) {
            assert(part.baseAddress == 0x009FB03C && part.reserved == 3);
            out = {Version, sizeof(Response), 0, 4, Axis::X, part.valueType, 48, 0};
        } else {
            assert(part.baseAddress == 0x009DF9A8 && part.reserved == 4);
            if (failY) { error = L"test error"; return false; }
            out = {Version, sizeof(Response), 0, 4, Axis::Y, part.valueType, 0, 137};
        }
        return true;
    };
    std::wstring pairError;
    assert(ReadPlayerCoordinatesWith(transport, 123, pair, response, pairError));
    assert(calls == 2 && response.xBits == 48 && response.yBits == 137 && response.bytesRead == 8);
    failY = true;
    assert(!ReadPlayerCoordinatesWith(transport, 123, pair, response, pairError));
    assert(response.xBits == 0 && response.yBits == 0 && pairError == L"Y: test error");
    failY = false;
    calls = 0;
    pair.axis = Axis::Y;
    assert(ReadPlayerCoordinatesWith(transport, 123, pair, response, pairError));
    assert(calls == 1 && response.xBits == 0 && response.yBits == 137 && response.bytesRead == 4);
    std::cout << "Driver client validation, paired coordinates and formatting tests passed.\n";
}
