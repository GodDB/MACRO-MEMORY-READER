# Windows 커널 메모리 읽기 및 좌표 GUI

Windows GUI와 WDM 드라이버를 추가했습니다. **실행·빌드·설치 절차는 [GUI-DRIVER.md](GUI-DRIVER.md)**를 확인하세요.
`DarkEdenMemoryReader.sln`에서 GUI와 드라이버를 빌드할 수 있습니다.
GUI는 X, Y, 둘 다 요청과 결과/오류 표시를 제공하며 실제 읽기는 `Example.cpp`를 포함한
커널 드라이버에서 수행합니다. 현재 제공된 GUI 실행 파일은 드라이버 없이 화면을 열 수 있지만,
좌표를 읽으려면 `.sys`를 Windows에 로드해야 합니다.
프로토콜 v2 GUI는 PID와 좌표 요청만 전달합니다. 대상 프로세스에 대한 `OpenProcess`와
`NtOpenProcess` 호출은 없습니다. 드라이버의 `InitializeForRequest`가 요청자 문맥에서
`ZwOpenProcess(0x1010)`를 호출하고, 실제 핸들 권한·이미지 이름 검사와 메모리 읽기를 수행합니다.
`OBJ_KERNEL_HANDLE | OBJ_FORCE_ACCESS_CHECK`를 사용하며, 권한이 부족하면 오류를 반환합니다.
GUI와 드라이버를 **둘 다 v2로 교체**해야 합니다. 새 GUI는 `build/gui/ProtocolV2`에 있습니다.
`build/driver/Release/DarkEdenReader.sys`에 x64 **개발용 테스트 서명** 빌드를 생성했습니다.
새 빌드의 Windows 서명 검증은 Valid입니다. 이번 변경에서 드라이버 서비스 교체·로드는 하지 않았습니다.
MSVC/WDK 빌드, GUI·좌표 테스트와 실제 초기화 코드를 사용하는 커널 API 모의 테스트가 통과했습니다.
이 결과는 새 드라이버를 로드한 실제 좌표 읽기 검증을 대신하지 않습니다.

아래는 기존 커널 읽기 클래스와 포인터 경로에 대한 설명입니다.

`KernelProcessMemoryReader`는 Windows 커널 드라이버 안에서 대상 프로세스의
가상 주소에 저장된 값을 읽는 C++ 클래스입니다. PID로 한 번 초기화한 후
`Read<T>(address)`로 값, NTSTATUS, 읽은 바이트 수를 반환합니다.

## 사용 예

아래 코드는 드라이버의 `PASSIVE_LEVEL` 실행 경로에서 사용합니다.
`darkedenPid`와 `address`는 호출자가 제공하는 실제 PID와 가상 주소입니다.

```cpp
KernelProcessMemoryReader reader;
NTSTATUS status = reader.Initialize(darkedenPid);
if (!NT_SUCCESS(status)) {
    return status;
}

auto result = reader.Read<LONG>(address); // 4바이트 정수
if (!result.Succeeded()) {
    return result.status;
}
LONG value = result.value;
```

- `LONG`: 4바이트 정수, `float`: 4바이트 실수, `ULONGLONG`: 8바이트 정수.
- 자료형과 읽을 크기는 실제 메모리 데이터 형식과 일치해야 합니다.
- 한 번에 256바이트 이하의 단순 자료형/구조체를 읽습니다.
- 실패한 결과의 `value`는 0으로 초기화된 상태를 유지합니다.
  실제 값 0과 읽기 실패는 반드시 `Succeeded()`로 구분합니다.
- `bytesRead`에는 실패 시에도 실제 복사된 바이트 수가 남습니다.
  일부만 읽힌 데이터는 `value`에 전달하지 않습니다.

## darkeden.exe 지정

현재 클래스의 입력은 실행 파일명이 아닌 PID입니다. Windows PowerShell에서
다음 명령으로 PID를 확인해 `Initialize`에 HANDLE 형식으로 전달할 수 있습니다.

```powershell
Get-Process -Name darkeden | Select-Object Id, ProcessName
```

숫자 PID를 변환하는 예: `ULongToHandle(pid)`.
프로세스가 여러 개라면 대상 인스턴스를 선택해야 합니다.
초기화 이후에는 프로세스 객체 참조를 유지하므로 같은 PID가 재사용되어도
다른 프로세스로 자동 전환되지 않습니다. 대상 종료 후에는 새 인스턴스로
초기화해야 합니다. 객체 참조가 대상 프로세스의 종료를 막지는 않습니다.

## 주소와 포인터

입력은 대상 프로세스의 **실제 가상 주소**입니다. 모듈 기준 오프셋만 알고
있다면 실제 모듈 시작 주소를 더해야 하며, 재실행 시 주소는 바뀔 수 있습니다.
`Read`는 지정 주소의 데이터를 한 번 읽습니다. 포인터 체인은 자동 추적하지 않습니다.
32비트 대상의 포인터 값은 `Read<ULONG>(pointerAddress)`, 64비트 대상의
포인터 값은 `Read<ULONGLONG>(pointerAddress)`로 읽은 후, 성공 여부를 확인하고
반환된 주소로 다시 `Read`를 호출합니다. 드라이버 자신의 포인터 크기를 대상
프로세스의 포인터 크기로 가정하면 안 됩니다.

## x·y 좌표 읽기

`DarkEdenCoordinateReader.h`에 좌표 읽기 기능을 추가했습니다.
사용자가 확인한 기준은 **darkeden.exe 모듈 기준 오프셋 `0x009CB97C`**입니다.
`ReadAllDarkEdenCoordinates(memory)`는 32비트/64비트 포인터 경로를 각각
추적하고, 각 좌표를 4바이트 정수와 float로 해석한 네 결과를 반환합니다.
같은 포인터 경로의 정수/실수 결과는 한 번 읽은 동일한 바이트에서 만듭니다.
각 결과의 `Succeeded()`를 별도로 확인해야 하며, 읽기 성공만으로 올바른
자료형이 자동 판별되는 것은 아닙니다. 실제 자료형은 아직 확인되지 않았습니다.

```cpp
auto all = ReadAllDarkEdenCoordinates(memory); // Initialize 성공 이후 호출
// all.pointer32Int.value.x / .y
// all.pointer32Float.value.x / .y
// all.pointer64Int.value.x / .y
// all.pointer64Float.value.x / .y
if (all.pointer32Int.Succeeded()) {
    LONG x = all.pointer32Int.value.x;
    LONG y = all.pointer32Int.value.y;
    // x, y를 호출자에게 전달
}
```

`DarkEdenCoordinateReader` 단독 사용 시 기본 형식은 **32비트 포인터,
4바이트 부호 있는 정수 좌표**입니다. 오프셋은 아래 순서로 해석합니다. `read32`는 해당 주소의
4바이트 포인터를 읽는다는 뜻입니다.

```text
baseAddress = 0x009CB97C
p1 = read32(baseAddress)
p2 = read32(p1 + 0xC)
p3 = read32(p2 + 0x44)
x = readInt32(p3 + 0x214)
y = readInt32(p3 + 0x218)
```

다음 예시는 `memory.Initialize(darkedenPid)`가 성공한 이후에 사용합니다.

```cpp
#include "DarkEdenCoordinateReader.h"

DarkEdenCoordinateReader coordinates(memory);

auto result = coordinates.Read(); // 절대 주소 0x009CB97C 사용

if (!result.Succeeded()) {
    return result.status;
}
LONG x = result.value.x;
LONG y = result.value.y;
```

저수준 좌표 reader의 `ReadFromModule`은 테스트용으로 모듈 베이스를 직접 받습니다.
GUI IOCTL 경로에서는 드라이버가 실행 중인 darkeden.exe 이미지 베이스를 Ring0에서
확인하고 오프셋을 더하므로 GUI가 모듈을 열거나 열거하지 않습니다.

개별 좌표는 `ReadX(baseAddress)`, `ReadY(baseAddress)`로 읽습니다.
이 두 함수와 `Read(baseAddress)`의 주소 기본값은 `0x009CB97C`입니다.
다른 주소를 지정할 때에는 **첫 번째 포인터가 저장된 모듈 기준 오프셋**을 전달합니다.
마지막 오프셋에서는 포인터를 한 번 더 역참조하지 않습니다.

`Read`는 공통 포인터 경로를 한 번 따라간 후 연속된 x·y 8바이트를 함께
읽습니다. 원자적 스냅샷을 보장하지는 않습니다. 중간 포인터 읽기가 실패하면
즉시 오류를 반환하며, 그때 `bytesRead`는 최종 좌표를 읽지 않았으므로 0입니다.
최종 좌표 복사 중 실패하면 최종 읽기의 바이트 수가 반환됩니다.
포인터가 0이거나 주소 덧셈이 대상 포인터 범위를 초과하는 경우도 처리합니다.

좌표가 실수 또는 대상 포인터가 64비트라면 형식을 명시할 수 있습니다.

```cpp
// 32비트 포인터 + float 좌표
BasicDarkEdenCoordinateReader<KernelProcessMemoryReader, ULONG, float> floats(memory);
// 64비트 포인터 + 4바이트 정수 좌표
BasicDarkEdenCoordinateReader<KernelProcessMemoryReader, ULONGLONG, LONG> wide(memory);
```

원본 `memory` 객체는 좌표 reader보다 오래 유지해야 합니다.

## 프로젝트에 추가

Windows 10 x64용 Visual Studio C++/WDK 커널 드라이버 프로젝트에 `.h`와
`.cpp`를 추가하고 C++17을 사용합니다. Windows 커널 라이브러리
`NtosKrnl.lib`가 필요합니다. `Example.cpp`는 호출 예시입니다.

`driver/Driver.cpp`에 `DriverEntry`, 장치 생성과 사용자 앱 통신을 구현했습니다.
GUI 요청은 PID와 좌표 설정을 담아 `ExecuteDarkEdenRequest`로 진입하고,
`InitializeForRequest`가 커널 핸들을 열어 호출자의 접근 권한과 대상 이미지 이름을 검사합니다.
기존 PID 기반 `Initialize` 자체는 권한 검사 API가 아니며 사용자 요청 경계에서 사용하지 않습니다.
드라이버 빌드 프로젝트와 설치/제거 스크립트 및 개발용 테스트 서명 `.sys`를 포함합니다.
Microsoft 배포 서명이 아니므로 테스트용 Windows 설정이 필요합니다.

클래스의 생성·초기화·읽기·소멸은 모두 `PASSIVE_LEVEL`에서 수행합니다.
객체는 유효한 커널 메모리에 두고, 초기화 및 소멸을 읽기와 동시에 실행하지
않도록 호출자가 동기화해야 합니다. 읽기 코드는 pageable 섹션으로 옮기지 마세요.
대상이 메모리를 변경/해제하는 동안 읽으면 실패하거나 일관되지 않은 값을
얻을 수 있으며, 여러 번의 읽기를 하나의 원자적 스냅샷으로 보장하지 않습니다.

## 검증 상태

최초 클래스는 macOS에서 작성되었고, GUI 추가 시 Windows에서 GUI 빌드와 C++ 테스트를
검증했습니다. 이후 Microsoft MSVC 19.44 / WDK 10.0.26100.6584로 x64 드라이버를
빌드하고 SHA256 테스트 서명 및 파일 무결성을 검증했습니다. 2026-09-25 게임 종료 후 드라이버 로드와 장치 연결에 성공했습니다. 자체 프로세스 메모리의 실제 IOCTL 읽기 14개 검사도 통과했습니다. 실제 게임 좌표는 아직 검증하지 않았습니다.
Windows 테스트 환경에서 알려진 값을 가진 자체 테스트
프로세스를 대상으로 정상 읽기, 주소 0, 해제된 주소, 페이지 경계를 넘는
부분 읽기, 대상 프로세스 종료, 초기화 전 읽기를 확인해야 합니다.

좌표 추적 로직은 가짜 메모리를 이용하는 C++ 테스트로 검증할 수 있습니다.
정상 좌표, 개별 x/y, 값 0, 모듈 기준 주소, 각 중간 단계 실패, null 포인터,
주소 오버플로, 최종 부분 읽기, float 좌표, 64비트 포인터, 네 형식별 결과와
오류 전파를 포함합니다.
`tests/stubs/ntifs.h`는 테스트용 타입 정의이며 드라이버 프로젝트의 include
경로에 추가하면 안 됩니다. 이 테스트는 WDK 컴파일/실제 커널 동작을 검증하지 않습니다.

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -Itests/stubs -I. \
  tests/CoordinateReaderTests.cpp -o /tmp/darkeden-coordinate-tests
/tmp/darkeden-coordinate-tests
```

## 사용한 공식 API 문서

- [PsLookupProcessByProcessId](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-pslookupprocessbyprocessid): 프로세스 조회 및 참조 획득.
- [KeStackAttachProcess](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-kestackattachprocess): 대상 주소 공간 연결.
- [MmCopyMemory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-mmcopymemory): 메모리 복사와 읽은 바이트 수 반환.

## 2026-09-25 로드 오류 재검증

- 게임과 xhunter1이 실행 중인 상태: 정식 빌드 및 별도 최소 드라이버 모두 Win32 647로 실패.
- 사용자가 게임을 정상 종료한 상태: xhunter1 Stopped 확인, 동일한 서명 드라이버 시작 성공.
- 초기화 기록 InitializationStage=3, InitializationStatus=0 및 장치 열기 성공 확인.
- 동일 바이너리 비교이므로 이전 링크 옵션 변경이 해결 원인이었다고 볼 수 없습니다.
  게임/보호 프로그램의 실행 상태와 실패가 연관되지만 내부 차단 원인은 확정하지 않았습니다.
- GUI는 QueryServiceStatusEx로 미설치, 중지/시작 실패, 실행 중 장치 없음, 조회 실패를 구분합니다.
- Start-Driver.ps1의 GENERIC_READ 값은 Windows PowerShell 5.1에서 음수가 되지 않도록 UInt32 십진수로 전달합니다.
- Get-DriverDiagnostics.ps1은 서비스, 바이너리 서명·해시, 활성 테스트 서명 모드, 초기화 기록과 관련 이벤트를 읽기 전용으로 수집합니다.
- Deploy-Driver.ps1은 시작을 한 번만 시도하고 종료 코드와 시각을 남깁니다. 이전 초기화 기록은 새 시작 전에 초기화합니다.

### 실제 드라이버 회귀 검사

`tests/DriverIntegrationTests.cpp`는 자체 테스트 메모리만 읽습니다. 빌드 결과를
`build/tests/fixture/darkeden.exe`에 두고 관리자 권한으로 실행합니다.
운영 게임 파일을 덮어쓰면 안 됩니다. 이미 로드된 드라이버가 필요합니다.
32/64비트 포인터 각각에 대해 정수/실수 X·Y·둘 다 요청과 null 포인터 오류 전달을 검증합니다.
실제 게임 주소의 유효성을 검증하는 테스트는 아닙니다.

### 재확인한 공식 문서

- [DriverEntry 계약](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nc-wdm-driver_initialize)
- [보안 장치 생성](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdmsec/nf-wdmsec-wdmlibiocreatedevicesecure)
- [서비스 상태 조회](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryservicestatusex)
- [서비스 오류 코드 필드](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_status_process)
- [XIGNCODE 공식 FAQ의 xhunter1.sys 설명](https://faq.wellbia.com/index_en.html)

## 이전 프로토콜 v1의 접근 거부 진단 기록

2026-09-25 재검사에서 드라이버는 Running이지만, 관리자 권한에서도 darkeden.exe 핸들의
요청 접근 마스크 0x1010 중 0x1000만 실제 부여되었습니다. PROCESS_VM_READ(0x10)가 없어
실제 IOCTL 재시도도 NTSTATUS 0xC0000022, 최종 읽기 0바이트로 실패했습니다.
권한을 제한한 구체적인 구성 요소는 이 결과만으로 단정하지 않습니다.

GUI는 접근 거부 응답 시 NtQueryObject(ObjectBasicInformation)로 같은 핸들의
GrantedAccess를 조회하고 부족한 권한과 실제 마스크를 오류 로그에 표시합니다.
이 부가 조회가 실패하면 원래 드라이버 오류를 유지합니다. 커널의 UserMode 접근 검사와
프로세스 이름 검사는 그대로 적용됩니다. 실제 게임 좌표 읽기는 아직 성공하지 않았습니다.

검증: 좌표/GUI 회귀 테스트, 자체 조회 전용 핸들의 실제 권한 조회,
관리자 권한에서 현재 게임을 대상으로 한 요청의 진단 메시지 확인.

공식 계약:
- https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-obreferenceobjectbyhandle
