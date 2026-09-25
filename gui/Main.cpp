#include "DriverClient.h"
#include <thread>
#include <memory>
#include <commctrl.h>

using namespace DarkEdenProtocol;
using namespace DarkEdenClient;
namespace
{
enum : int { Process = 101, Refresh, Connect, Address, Pointer, Type, ReadX, ReadY, ReadBoth, Clear, XValue, YValue, Status, Log };
constexpr UINT Finished = WM_APP + 1;
HWND window = nullptr;
HFONT normalFont = nullptr, titleFont = nullptr, valueFont = nullptr;
HBRUSH background = nullptr;
int dpi = 96;
bool busy = false, errorState = false;
std::thread worker;
struct Job { DWORD pid; Request request; Response response{}; bool success = false; wchar_t error[2048]{}; };
std::unique_ptr<Job> job;

int Scale(int x) { return MulDiv(x, dpi, 96); }
HWND Control(int id) { return GetDlgItem(window, id); }
void Text(int id, const std::wstring& text) { SetWindowTextW(Control(id), text.c_str()); }
std::wstring Text(int id)
{
    const int length = GetWindowTextLengthW(Control(id));
    std::wstring result(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(Control(id), &result[0], length + 1);
    result.resize(length);
    return result;
}
void LogLine(const std::wstring& message)
{
    SYSTEMTIME time; GetLocalTime(&time);
    wchar_t timestamp[32]; swprintf_s(timestamp, L"[%02u:%02u:%02u] ", time.wHour, time.wMinute, time.wSecond);
    HWND log = Control(Log);
    if (GetWindowTextLengthW(log) > 48000) SetWindowTextW(log, L"");
    SendMessageW(log, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    const auto line = std::wstring(timestamp) + message + L"\r\n\r\n";
    SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(log, EM_SCROLLCARET, 0, 0);
}
void StatusText(const std::wstring& message, bool error = false)
{
    errorState = error;
    Text(Status, message);
    InvalidateRect(Control(Status), nullptr, TRUE);
    LogLine(message);
}
void ResetValues() { Text(XValue, L"—"); Text(YValue, L"—"); }
void SetBusy(bool value)
{
    busy = value;
    const int ids[] = {Process, Refresh, Connect, Address, Pointer, Type, ReadX, ReadY, ReadBoth};
    for (const int id : ids) EnableWindow(Control(id), !value);
}
void RefreshProcesses()
{
    ResetValues();
    HWND combo = Control(Process);
    const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    const DWORD previous = selection == CB_ERR ? 0 : static_cast<DWORD>(SendMessageW(combo, CB_GETITEMDATA, selection, 0));
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const auto processes = FindProcesses();
    int select = 0;
    for (size_t i = 0; i < processes.size(); ++i)
    {
        const auto label = L"darkeden.exe  ·  PID " + std::to_wstring(processes[i]);
        const auto index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, index, processes[i]);
        if (processes[i] == previous) select = static_cast<int>(i);
    }
    if (!processes.empty())
    {
        SendMessageW(combo, CB_SETCURSEL, select, 0);
        StatusText(L"대상 프로세스를 선택하고 좌표를 요청하세요.");
    }
    else StatusText(L"darkeden.exe가 실행 중이지 않습니다. 실행 후 새로고침하세요.", true);
}
void CheckConnection()
{
    Handle device(CreateFileW(DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!device) StatusText(DeviceError(GetLastError()), true);
    else StatusText(L"드라이버 장치 연결 성공. 좌표 버튼으로 읽기를 요청하세요.");
}
void StartRead(Axis axis)
{
    if (busy) return;
    ResetValues();
    const auto selected = SendMessageW(Control(Process), CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) { StatusText(L"darkeden.exe를 실행하고 대상 프로세스를 선택하세요.", true); return; }
    uint64_t address;
    try { address = ParseAddress(Text(Address)); }
    catch (const std::invalid_argument&)
    { StatusText(L"기준 주소를 0이 아닌 16진수로 입력하세요. 예: 0x009CB97C", true); return; }
    auto next = std::make_unique<Job>();
    next->pid = static_cast<DWORD>(SendMessageW(Control(Process), CB_GETITEMDATA, selected, 0));
    next->request = {Version, sizeof(Request), 0, address, axis,
        SendMessageW(Control(Pointer), CB_GETCURSEL, 0, 0) == 0 ? 4u : 8u,
        SendMessageW(Control(Type), CB_GETCURSEL, 0, 0) == 0 ? ValueType::Int32 : ValueType::Float32, 0};
    if (next->request.pointerBytes == 4 && address > UINT32_MAX)
    { StatusText(L"32비트 포인터의 기준 주소는 0xFFFFFFFF 이하여야 합니다.", true); return; }
    job = std::move(next);
    SetBusy(true);
    StatusText(L"커널 드라이버에서 좌표를 읽는 중…");
    try
    {
        worker = std::thread([]
        {
            try
            {
                std::wstring error;
                job->success = DarkEdenClient::Read(job->pid, job->request, job->response, error);
                wcsncpy_s(job->error, error.c_str(), _TRUNCATE);
            }
            catch (const std::exception& ex)
            {
                wcscpy_s(job->error, L"C++ 예외: ");
                MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, job->error + 8, 2038);
            }
            catch (...) { wcscpy_s(job->error, L"알 수 없는 C++ 예외가 발생했습니다."); }
            PostMessageW(window, Finished, 0, 0);
        });
    }
    catch (...) { SetBusy(false); job.reset(); throw; }
}
void FinishRead()
{
    if (worker.joinable()) worker.join();
    SetBusy(false);
    if (!job) return;
    if (!job->success) StatusText(job->error, true);
    else
    {
        const auto& result = job->response;
        if (result.axis != Axis::Y) Text(XValue, FormatValue(result.xBits, result.valueType));
        if (result.axis != Axis::X) Text(YValue, FormatValue(result.yBits, result.valueType));
        StatusText(L"읽기 성공 · PID " + std::to_wstring(job->pid) + L" · " +
            std::to_wstring(result.bytesRead) + L" 바이트 · NTSTATUS 0x00000000");
        LogLine(L"X = " + Text(XValue) + L" / Y = " + Text(YValue) + L" / 기준 주소 " + Hex(job->request.baseAddress));
    }
    job.reset();
}
HWND Add(const wchar_t* kind, const wchar_t* label, DWORD style, int id, int x, int y, int w, int h, HFONT font = nullptr)
{
    HWND control = CreateWindowExW(wcscmp(kind, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0,
        kind, label, WS_CHILD | WS_VISIBLE | style, Scale(x), Scale(y), Scale(w), Scale(h),
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!control) throw std::runtime_error("Failed to create a window control.");
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : normalFont), TRUE);
    return control;
}
void BuildControls()
{
    Add(L"STATIC", L"DarkEden 좌표 리더", 0, 0, 24, 18, 750, 34, titleFont);
    Add(L"STATIC", L"커널 드라이버 연결 · X / Y 좌표 조회", 0, 0, 24, 57, 750, 24);
    Add(L"STATIC", L"대상 프로세스", 0, 0, 24, 96, 130, 23);
    Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, Process, 24, 122, 416, 220);
    Add(L"BUTTON", L"새로고침", WS_TABSTOP, Refresh, 454, 120, 142, 32);
    Add(L"BUTTON", L"드라이버 연결 확인", WS_TABSTOP, Connect, 610, 120, 186, 32);
    Add(L"STATIC", L"기준 주소 (절대 주소 · 16진수)", 0, 0, 24, 172, 300, 23);
    Add(L"STATIC", L"포인터 크기", 0, 0, 360, 172, 180, 23);
    Add(L"STATIC", L"좌표 자료형", 0, 0, 584, 172, 180, 23);
    Add(L"EDIT", L"0x009CB97C", ES_AUTOHSCROLL | WS_TABSTOP, Address, 24, 198, 312, 29);
    SendMessageW(Control(Address), EM_SETLIMITTEXT, 32, 0);
    Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, Pointer, 360, 198, 200, 160);
    Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, Type, 584, 198, 212, 160);
    for (auto label : {L"32비트 (4바이트)", L"64비트 (8바이트)"})
        SendMessageW(Control(Pointer), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    for (auto label : {L"정수 (int32)", L"실수 (float32)"})
        SendMessageW(Control(Type), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendMessageW(Control(Pointer), CB_SETCURSEL, 0, 0);
    SendMessageW(Control(Type), CB_SETCURSEL, 0, 0);
    Add(L"BUTTON", L"X좌표 요청", WS_TABSTOP, ReadX, 24, 253, 248, 42);
    Add(L"BUTTON", L"Y좌표 요청", WS_TABSTOP, ReadY, 286, 253, 248, 42);
    Add(L"BUTTON", L"X + Y 둘 다 요청", WS_TABSTOP | BS_DEFPUSHBUTTON, ReadBoth, 548, 253, 248, 42);
    Add(L"STATIC", L"X 좌표", 0, 0, 24, 318, 340, 23);
    Add(L"STATIC", L"Y 좌표", 0, 0, 418, 318, 340, 23);
    Add(L"STATIC", L"—", SS_LEFT, XValue, 24, 346, 378, 50, valueFont);
    Add(L"STATIC", L"—", SS_LEFT, YValue, 418, 346, 378, 50, valueFont);
    Add(L"STATIC", L"준비 중…", SS_LEFT, Status, 24, 409, 772, 62);
    Add(L"STATIC", L"실행 결과 / 오류 / 예외 기록", 0, 0, 24, 487, 600, 24);
    Add(L"BUTTON", L"기록 지우기", WS_TABSTOP, Clear, 654, 480, 142, 30);
    Add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, Log, 24, 520, 772, 137);
    SendMessageW(Control(Log), EM_SETLIMITTEXT, 65535, 0);
    RefreshProcesses();
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    try
    {
        switch (message)
        {
        case WM_CREATE: window = hwnd; BuildControls(); return 0;
        case WM_COMMAND:
            if (LOWORD(wparam) == Clear) { Text(Log, L""); return 0; }
            if (busy) return 0;
            if (HIWORD(wparam) == BN_CLICKED)
            {
                switch (LOWORD(wparam))
                {
                case Refresh: RefreshProcesses(); return 0;
                case Connect: CheckConnection(); return 0;
                case ReadX: StartRead(Axis::X); return 0;
                case ReadY: StartRead(Axis::Y); return 0;
                case ReadBoth: StartRead(Axis::Both); return 0;
                }
            }
            if ((HIWORD(wparam) == CBN_SELCHANGE && (LOWORD(wparam) == Process || LOWORD(wparam) == Pointer || LOWORD(wparam) == Type)) ||
                (LOWORD(wparam) == Address && HIWORD(wparam) == EN_CHANGE))
            {
                ResetValues();
                if (Control(Log)) StatusText(L"설정이 변경되었습니다. 좌표를 다시 요청하세요.");
            }
            return 0;
        case Finished: FinishRead(); return 0;
        case WM_CTLCOLORSTATIC:
        {
            const auto dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, RGB(247, 249, 252));
            SetTextColor(dc, reinterpret_cast<HWND>(lparam) == Control(Status) && errorState ? RGB(183, 38, 46) : RGB(27, 42, 64));
            return reinterpret_cast<LRESULT>(background);
        }
        case WM_CLOSE:
            if (busy) { StatusText(L"요청 처리 중입니다. 응답을 받은 뒤 닫을 수 있습니다."); return 0; }
            DestroyWindow(hwnd); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    }
    catch (const std::exception& ex)
    {
        wchar_t details[1024]{};
        MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, details, 1024);
        if (Control(Status) && Control(Log))
        {
            ResetValues();
            StatusText(std::wstring(L"GUI 예외: ") + details, true);
        }
        else MessageBoxW(hwnd, details, L"GUI 초기화 오류", MB_ICONERROR);
        if (message == WM_CREATE) return -1;
    }
    catch (...)
    {
        MessageBoxW(hwnd, L"처리하지 못한 GUI 예외가 발생했습니다.", L"예외", MB_ICONERROR);
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    SetProcessDPIAware();
    HDC dc = GetDC(nullptr); dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(nullptr, dc);
    normalFont = CreateFontW(-Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
    titleFont = CreateFontW(-Scale(25), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
    valueFont = CreateFontW(-Scale(32), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
    background = CreateSolidBrush(RGB(247, 249, 252));
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.lpfnWndProc = WindowProcedure;
    cls.lpszClassName = L"DarkEdenCoordinateReaderGui"; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hIcon = LoadIcon(nullptr, IDI_APPLICATION); cls.hbrBackground = background;
    if (!RegisterClassExW(&cls)) return 1;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, Scale(820), Scale(681)}; AdjustWindowRect(&bounds, style, FALSE);
    HWND main = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"DarkEden 좌표 리더 · Kernel",
        style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, nullptr);
    if (!main) return 1;
    ShowWindow(main, show); UpdateWindow(main);
    MSG message{};
    int result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        if (!IsDialogMessageW(main, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (worker.joinable()) worker.join();
    DeleteObject(normalFont); DeleteObject(titleFont); DeleteObject(valueFont); DeleteObject(background);
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
