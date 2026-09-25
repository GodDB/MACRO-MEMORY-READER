#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../shared/DriverProtocol.h"

namespace DarkEdenClient
{
class Handle
{
    HANDLE value_;
public:
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
};

inline std::wstring Hex(uint64_t value, int width = 8)
{
    std::wostringstream out;
    out << L"0x" << std::hex << std::uppercase << std::setw(width) << std::setfill(L'0') << value;
    return out.str();
}
inline std::wstring WindowsError(DWORD code)
{
    wchar_t buffer[1024]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, buffer, 1024, nullptr);
    std::wstring text(buffer);
    while (!text.empty() && iswspace(text.back())) text.pop_back();
    return L"Win32 " + std::to_wstring(code) + L" (" + Hex(code) + L"): " + text;
}
inline std::wstring StatusMessage(int32_t status)
{
    switch (static_cast<uint32_t>(status))
    {
    case 0xC0000022: return L"커널에서 대상 프로세스 접근이 거부되었습니다. 요청 권한: 0x00001010.";
    case 0xC000000B: return L"대상 PID가 유효하지 않습니다. 프로세스 목록을 새로고침하세요.";
    case 0xC0000008: return L"프로세스 핸들이 유효하지 않습니다.";
    case 0xC000000D: return L"잘못된 주소 또는 요청입니다.";
    case 0xC0000141: return L"포인터가 0이거나 주소가 유효하지 않습니다.";
    case 0xC0000095: return L"주소 계산이 포인터 범위를 초과했습니다.";
    case 0x8000000D: return L"메모리를 전부 읽지 못했습니다. 주소와 프로세스 상태를 확인하세요.";
    case 0xC0000005: return L"메모리 접근 예외가 발생했습니다.";
    case 0xC000010A: return L"대상 프로세스가 종료 중입니다.";
    case 0xC0000024: return L"프로세스 핸들이 아닌 핸들이 전달되었습니다.";
    case 0xC0000034: return L"대상 프로세스의 이미지 이름을 찾을 수 없습니다.";
    case 0xC0000033: return L"대상 프로세스가 darkeden.exe가 아닙니다.";
    default: return L"커널 읽기 요청에 실패했습니다. NTSTATUS를 확인하세요.";
    }
}

inline uint64_t ParseAddress(const std::wstring& text)
{
    size_t start = 0, end = text.size();
    while (start < end && iswspace(text[start])) ++start;
    while (end > start && iswspace(text[end - 1])) --end;
    if (end - start >= 2 && text[start] == L'0' && (text[start + 1] == L'x' || text[start + 1] == L'X')) start += 2;
    if (start == end) throw std::invalid_argument("Enter a hexadecimal address.");
    uint64_t value = 0;
    for (size_t i = start; i < end; ++i)
    {
        const wchar_t c = text[i];
        const unsigned digit = c >= L'0' && c <= L'9' ? c - L'0' :
            c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : 16;
        if (digit >= 16 || value > (UINT64_MAX - digit) / 16)
            throw std::invalid_argument("Invalid or overflowing hexadecimal address.");
        value = value * 16 + digit;
    }
    if (value == 0 || value > INT64_MAX) throw std::invalid_argument("Enter a nonzero user-space address.");
    return value;
}

inline std::vector<DWORD> FindProcesses()
{
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) throw std::runtime_error("Process enumeration failed.");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<DWORD> processes;
    if (!Process32FirstW(snapshot.get(), &entry))
    {
        if (GetLastError() == ERROR_NO_MORE_FILES) return processes;
        throw std::runtime_error("Process enumeration failed.");
    }
    do
    {
        if (_wcsicmp(entry.szExeFile, L"darkeden.exe") == 0) processes.push_back(entry.th32ProcessID);
    } while (Process32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw std::runtime_error("Process enumeration interrupted.");
    return processes;
}

struct DriverServiceStatus
{
    DWORD queryError = ERROR_SUCCESS;
    DWORD state = 0;
    DWORD win32ExitCode = 0;
    DWORD serviceSpecificExitCode = 0;
};

inline DriverServiceStatus QueryDriverService()
{
    DriverServiceStatus result;
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { result.queryError = GetLastError(); return result; }
    SC_HANDLE service = OpenServiceW(manager, L"DarkEdenReader", SERVICE_QUERY_STATUS);
    if (!service) result.queryError = GetLastError();
    else
    {
        SERVICE_STATUS_PROCESS status{};
        DWORD needed = 0;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed))
            result.queryError = GetLastError();
        else
        {
            result.state = status.dwCurrentState;
            result.win32ExitCode = status.dwWin32ExitCode;
            result.serviceSpecificExitCode = status.dwServiceSpecificExitCode;
        }
        CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
    return result;
}

inline std::wstring FormatDeviceError(DWORD code, const DriverServiceStatus& service)
{
    std::wstring hint;
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
    {
        if (service.queryError == ERROR_SERVICE_DOES_NOT_EXIST)
            hint = L"DarkEdenReader 서비스가 설치되지 않았습니다. 서명된 드라이버를 설치하세요.\r\n";
        else if (service.queryError != ERROR_SUCCESS)
            hint = L"드라이버 장치를 찾을 수 없습니다. 서비스 상태 조회 실패: " +
                WindowsError(service.queryError) + L"\r\n";
        else if (service.state == SERVICE_STOPPED)
        {
            hint = L"드라이버 서비스는 설치되어 있지만 중지 상태입니다.\r\n";
            if (service.win32ExitCode != ERROR_SUCCESS)
                hint += L"마지막 서비스 오류: " + WindowsError(service.win32ExitCode) + L"\r\n";
            if (service.win32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR)
                hint += L"서비스 고유 오류: " + Hex(service.serviceSpecificExitCode) + L"\r\n";
            if (service.win32ExitCode == ERROR_FAILED_DRIVER_ENTRY)
                hint += L"드라이버 초기화에 실패했습니다. 설치·재부팅 반복 전에 로드 진단 결과를 확인하세요.\r\n";
            else if (service.win32ExitCode == ERROR_SUCCESS)
                hint += L"관리자 권한으로 드라이버 서비스를 시작하세요.\r\n";
        }
        else if (service.state == SERVICE_RUNNING)
            hint = L"서비스는 실행 중이지만 통신 장치를 찾을 수 없습니다. 설치된 드라이버 버전과 초기화 기록을 확인하세요.\r\n";
        else
            hint = L"드라이버 서비스가 상태 전환 중입니다. 잠시 후 다시 확인하세요.\r\n";
    }
    else if (code == ERROR_ACCESS_DENIED)
        hint = L"드라이버 연결 권한이 없습니다. GUI를 관리자 권한으로 실행하세요.\r\n";
    else hint = L"드라이버 연결 실패.\r\n";
    return hint + WindowsError(code);
}

inline std::wstring DeviceError(DWORD code)
{
    return FormatDeviceError(code,
        (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) ? QueryDriverService() : DriverServiceStatus{});
}

inline bool ValidResponse(const DarkEdenProtocol::Request& request,
                          const DarkEdenProtocol::Response& response, DWORD bytes)
{
    using namespace DarkEdenProtocol;
    if (bytes != sizeof(Response) || response.version != Version || response.size != sizeof(Response) ||
        response.axis != request.axis || response.valueType != request.valueType) return false;
    const uint32_t expected = request.axis == Axis::Both ? 8 : 4;
    if (response.bytesRead > expected || (response.status >= 0 && response.bytesRead != expected)) return false;
    if (response.status < 0 && (response.xBits != 0 || response.yBits != 0)) return false;
    if (request.axis == Axis::X && response.yBits != 0) return false;
    if (request.axis == Axis::Y && response.xBits != 0) return false;
    return true;
}

inline bool Read(DWORD pid, DarkEdenProtocol::Request request,
                 DarkEdenProtocol::Response& response, std::wstring& error)
{
    using namespace DarkEdenProtocol;
    response = {};
    Handle device(CreateFileW(DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!device) { error = DeviceError(GetLastError()); return false; }
    request.processId = pid;
    if (!ValidRequest(request)) { error = L"유효하지 않은 좌표 요청입니다."; return false; }
    DWORD returned = 0;
    if (!DeviceIoControl(device.get(), ReadCoordinates, &request, sizeof(request),
        &response, sizeof(response), &returned, nullptr))
    {
        const DWORD code = GetLastError();
        error = L"드라이버 통신 실패.\r\n" + WindowsError(code);
        if (code == ERROR_REVISION_MISMATCH || code == ERROR_INVALID_PARAMETER)
            error += L"\r\nGUI와 드라이버를 모두 프로토콜 v2 빌드로 맞추세요. 이전 드라이버는 PID 요청을 지원하지 않습니다.";
        return false;
    }
    if (!ValidResponse(request, response, returned))
    { response = {}; error = L"드라이버 응답 형식이 올바르지 않습니다. GUI와 드라이버 버전을 맞추세요."; return false; }
    if (response.status < 0)
    {
        error = StatusMessage(response.status) + L"\r\nNTSTATUS " + Hex(static_cast<uint32_t>(response.status)) +
            L" / 최종 읽기 " + std::to_wstring(response.bytesRead) + L" 바이트";
        return false;
    }
    return true;
}

inline std::wstring FormatValue(uint32_t bits, DarkEdenProtocol::ValueType type)
{
    if (type == DarkEdenProtocol::ValueType::Int32)
    { int32_t value; std::memcpy(&value, &bits, 4); return std::to_wstring(value); }
    float value;
    std::memcpy(&value, &bits, 4);
    std::wostringstream out;
    out << std::setprecision(9) << value;
    return out.str();
}
}
