# DarkEden 커널 좌표 리더

## 현재 산출물 (2026-09-25)

- `build/driver/Release/DarkEdenReader.sys`: MSVC/WDK로 빌드한 x64 커널 드라이버, SHA256 테스트 서명 완료.
- `build/driver/Release/signing/DarkEdenReader-Test.cer`: 공개 테스트 인증서.
- `build/gui/ProtocolV2/DarkEdenReaderGui.exe`: PID 요청을 사용하는 새 GUI. 드라이버도 v2로 교체해야 합니다.
- 테스트 인증서 지문: `313CA2F1B0EE2F9F8D96996B66B37058D2495BD3`.
- 인증서 유효기간 종료: 2027-09-25 01:59:36 KST. 타임스탬프는 포함하지 않았습니다.
- 새 드라이버의 Windows Authenticode 검증은 Valid입니다.
- 이번 수정에서 부팅 설정·신뢰 저장소 변경이나 드라이버 서비스 교체는 하지 않았습니다.
- 새 드라이버의 실제 로드·IOCTL 검증은 아직 수행하지 않았습니다. 현재 실행 중인 이전 드라이버는 새 PID 요청과 호환되지 않습니다.

빌드와 테스트 서명은 완료됐지만 Microsoft 배포 서명 및 실제 커널 실행 검증을 의미하지 않습니다.

### 설치 없는 빌드 재실행

Microsoft 공식 VS 패키지에서 추출한 MSVC 19.44.35229와 공식 NuGet SDK/WDK
10.0.26100.6584를 사용했습니다. Visual Studio 설치나 시스템 PATH 변경은 필요하지 않았습니다.
도구는 다음 작업 폴더에 있습니다.

```text
C:\Users\saqwz\Documents\Codex\2026-09-25\dl-x20\work\driver-toolchain
```

```powershell
.\scripts\Build-Driver.ps1 -ToolchainRoot 'C:\Users\saqwz\Documents\Codex\2026-09-25\dl-x20\work\driver-toolchain'
.\scripts\Build-GuiMsvc.ps1 -ToolchainRoot 'C:\Users\saqwz\Documents\Codex\2026-09-25\dl-x20\work\driver-toolchain' -OutputDirectory 'C:\Users\saqwz\OneDrive\Desktop\매크로 작업\DarkEdenMemoryReader\build\gui\ProtocolV2' -RunTests
.\scripts\Sign-Driver.ps1 -SignTool 'C:\Users\saqwz\Documents\Codex\2026-09-25\dl-x20\work\driver-toolchain\microsoft.windows.sdk.cpp\c\bin\10.0.26100.0\x64\signtool.exe' -CertificateThumbprint '313CA2F1B0EE2F9F8D96996B66B37058D2495BD3'
```

빌드하면 기존 서명이 사라지므로 매번 다시 서명해야 합니다. 개인 키는 현재 사용자의
인증서 저장소 `CurrentUser\My`에 내보내기 불가로 생성했으며 프로젝트나 전달 파일에 포함하지 않습니다.
스크립트 실행 정책이 차단된 환경에서는 허용된 개발용 PowerShell 환경을 사용하세요.

## 구성

```text
DarkEdenReaderGui.exe
  → CreateFile(\\.\DarkEdenMemoryReader) / DeviceIoControl
  → driver/Driver.cpp (WDM 드라이버, IOCTL 검증)
  → Example.cpp::ExecuteDarkEdenRequest
  → KernelProcessMemoryReader::InitializeForRequest(PID)
  → ZwOpenProcess(0x1010) / ZwQueryObject / 이미지 이름 검사
  → DarkEdenCoordinateReader.h
  → KernelProcessMemoryReader.cpp
  → KeStackAttachProcess + MmCopyMemory
  → NTSTATUS / 읽은 바이트 수 / 좌표를 GUI에 반환
```

GUI는 `ReadProcessMemory`를 사용하지 않습니다. `Example.cpp`는 독립 실행 파일이
아니라 `.sys`에 함께 빌드되며, 좌표 버튼이 이 파일의 함수를 호출합니다.

GUI에는 대상 프로세스 열기·권한 조회·이미지 이름 확인이 없습니다. PID와 좌표 설정을
드라이버에 보내며, 드라이버가 `ZwOpenProcess`로 `PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION`
(`0x1010`)을 요청합니다. 요청자 스레드에서 동기 처리하며 작업 큐로 옮기지 않습니다.
커널 핸들은 `OBJ_KERNEL_HANDLE | OBJ_FORCE_ACCESS_CHECK`로 열고 `ZwQueryObject`로 실제 부여 권한을
검사합니다. 권한 검사 성공 후 참조한 프로세스 객체에서 이름을 확인하고 좌표를 읽습니다.
핸들은 드라이버에서 닫고 객체 참조는 reader 소멸 시 해제합니다. GUI에 커널 핸들을 노출하지 않습니다.
커널 예제용 `Initialize(PID)`의 `PsLookupProcessByProcessId`는 GUI 요청 경로에서 사용하지 않습니다.

공식 계약: [ZwOpenProcess / NtOpenProcess](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-zwopenprocess).

GUI를 MSVC로 빌드하고 프로세스 접근 API import가 없음을 확인했습니다.
GUI·좌표·커널 접근 모의 테스트가 통과했습니다. 커널 테스트는 실제 초기화 코드를 가짜 NT API에
연결하여 `0x1010` 요청, 강제 접근 검사 속성, 권한 부족·조회 실패 시 중단, 핸들/객체 해제를 검사합니다.
프로토콜 v1 및 0·32비트 범위 초과 PID는 거부합니다. 모의 테스트는 실제 Windows 접근 제어 검증을 대신하지 않습니다.
통합 테스트 실행 파일도 새 프로토콜로 빌드했지만 새 드라이버 로드 전에는 실행하지 않습니다.
MinGW 빌드에서는 런타임의 스케줄러 함수가 `OpenProcess` import를 추가할 수 있으므로,
실행 파일에서도 해당 참조를 제외하려면 솔루션의 MSVC GUI 빌드를 사용하세요.

GUI는 실행 중인 `darkeden.exe` 목록을 표시합니다. 여러 개면 PID로 선택합니다.
`X좌표 요청`, `Y좌표 요청`, `X + Y 둘 다 요청` 버튼이 각각 다른 요청을 보냅니다.
X/Y 단독 요청은 해당 좌표 4바이트만, 둘 다 요청은 인접한 8바이트를 읽습니다.
32/64비트 포인터 및 int32/float32 좌표를 선택할 수 있습니다.

기본값은 기존 코드와 동일하게 **절대 주소 `0x009CB97C`, 32비트 포인터, int32**입니다.
게임 버전과 실제 메모리 구조에 맞는지 확인해야 합니다. 기준 주소는 항상 16진수입니다.

```text
p1 = readPointer(baseAddress)
p2 = readPointer(p1 + 0xC)
p3 = readPointer(p2 + 0x44)
x  = readValue(p3 + 0x214)
y  = readValue(p3 + 0x218)
```

결과 0과 읽기 실패는 별도로 처리합니다. 새 요청이나 설정 변경 시 이전 결과를 지우고,
요청하지 않은 축은 `—`로 표시합니다. 시간, 결과, Win32 오류, NTSTATUS, 부분 읽기
바이트 수, C++ 예외는 GUI 기록에 남습니다. 읽기는 작업 스레드에서 수행하며 중복 요청을
차단합니다. 요청 중에는 응답을 받은 다음 창을 닫을 수 있습니다.

## 빌드

### 드라이버와 GUI: Visual Studio / WDK

- Windows x64, Visual Studio 2022의 C++ 데스크톱 개발 도구(v143)
- 호환되는 Windows SDK와 WDK, WDK Visual Studio 확장
- C++17, WDM `WindowsKernelModeDriver10.0` 툴셋

`DarkEdenMemoryReader.sln`을 열고 `Debug|x64` 또는 `Release|x64`로 빌드합니다.
Developer Command Prompt에서도 다음 명령을 사용할 수 있습니다.

```bat
msbuild DarkEdenMemoryReader.sln /m /p:Configuration=Release /p:Platform=x64
```

출력:

- `build\gui\Release\DarkEdenReaderGui.exe`
- `build\driver\Release\DarkEdenReader.sys`

기본 드라이버 프로젝트는 **서명하지 않은 바이너리**를 생성합니다. 빌드 성공만으로
실제 Windows에서 로드되는 것은 아닙니다. 대상 Windows의 커널 드라이버 서명 정책에
맞게 서명해야 합니다. 일반 배포 서명과 개발용 테스트 환경은 Microsoft 문서를 따르세요.

### GUI만 빌드: MinGW-w64

GUI는 WDK 없이도 빌드되지만, 메모리 조회에는 로드된 드라이버가 필요합니다.

```powershell
.\scripts\Build-Gui.ps1 -Compiler 'C:\path\to\g++.exe' -RunTests
```

출력은 `build\gui\DarkEdenReaderGui.exe`입니다. PowerShell 실행 정책으로 스크립트가
차단되면 Visual Studio 빌드를 사용하거나 터미널에서 컴파일러를 직접 실행합니다.

```bat
g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -mwindows -static -DUNICODE -D_UNICODE gui\Main.cpp -o build\gui\DarkEdenReaderGui.exe -luser32 -lgdi32 -lcomctl32
g++ -std=c++17 -Wall -Wextra -Werror -static -Itests/stubs -I. tests\CoordinateReaderTests.cpp -o build\tests\CoordinateReaderTests.exe
g++ -std=c++17 -Wall -Wextra -Werror -static -DUNICODE -D_UNICODE tests\DriverClientTests.cpp -o build\tests\DriverClientTests.exe
build\tests\CoordinateReaderTests.exe
build\tests\DriverClientTests.exe
```

직접 빌드할 경우 `build\gui`, `build\tests` 폴더를 먼저 만드세요.

## 드라이버 설치 및 실행

**서명된 드라이버**가 준비된 후 관리자 PowerShell에서:

```powershell
.\scripts\Install-Driver.ps1 -DriverPath '.\build\driver\Release\DarkEdenReader.sys'
```

설치 스크립트는 유효한 내장 서명을 확인하고 `ProgramData\DarkEdenMemoryReader`로
복사한 뒤, 수동 시작 서비스 `DarkEdenReader`를 등록하고 시작합니다. 내장 서명이
유효해도 해당 Windows의 커널 서명 정책을 충족하지 않으면 로드가 거부될 수 있습니다.
이미 서비스가 있으면 덮어쓰지 않습니다. 교체 전 GUI를 닫고 제거 스크립트를 실행하세요.
스크립트가 차단되는 환경에서는 관리자 터미널에서 서명된 파일의 절대 경로로 등록합니다.

```bat
sc.exe create DarkEdenReader type= kernel start= demand binPath= "C:\absolute\path\DarkEdenReader.sys"
sc.exe start DarkEdenReader
sc.exe query DarkEdenReader
```

1. `darkeden.exe`를 실행합니다.
2. GUI를 **관리자 권한**으로 실행합니다.
3. `드라이버 연결 확인`을 누릅니다.
4. `새로고침` 후 대상 PID, 기준 주소, 포인터 크기, 좌표 자료형을 선택합니다.
5. X / Y / 둘 다 요청 버튼으로 읽습니다.

드라이버 제거는 GUI를 닫은 후:

```powershell
.\scripts\Uninstall-Driver.ps1
```

또는 관리자 터미널에서 `sc.exe stop DarkEdenReader`, `sc.exe delete DarkEdenReader`.
제거 스크립트는 서비스만 제거하고 `.sys` 파일을 보존합니다.

## 권한 및 오류 처리

장치는 SYSTEM/관리자만 열 수 있습니다. 드라이버가 요청자 문맥에서 강제 접근 검사를 적용하여
커널 핸들을 생성하고, 부여된 권한에 `0x1010`이 모두 있는지 확인합니다. 이미지 이름도
참조된 프로세스 객체에서 검사합니다. 커널에서 직접 만든 핸들만 `KernelMode`로 참조하며,
이 호출 전에 실제 부여 권한 검사를 끝냅니다. 사용자 핸들은 요청으로 받지 않습니다.
대상 접근 권한이 거부되면 GUI에 오류가 표시됩니다. 실제 메모리 복사는 커널에서 수행됩니다.

드라이버는 `METHOD_BUFFERED`로 요청 크기, 버전, 축, 자료형, 포인터 크기, PID를 검사하고
응답을 초기화합니다. 임의 사용자 출력 포인터는 받지 않습니다. 읽기는 사용자 주소 범위와
최대 256바이트로 제한됩니다. 부분 읽기의 데이터는 좌표로 표시하지 않습니다.

`MmCopyMemory` 중 SEH 예외는 NTSTATUS로 변환하고 `__finally`에서 대상 프로세스 연결을
해제합니다. GUI에는 통신 오류와 C++ 예외도 표시합니다. 커널 버그체크(BSOD), OS 강제 종료,
프로세스 강제 종료와 같이 앱 자체가 계속 실행할 수 없는 상황은 GUI로 복구할 수 없습니다.

| 표시 | 의미 / 확인 항목 |
|---|---|
| Win32 2 / 3 | 장치가 없음. 드라이버 서비스 시작 여부 확인 |
| Win32 5 | 장치 또는 대상 프로세스 접근 거부 |
| NTSTATUS 0xC0000033 | 선택한 핸들의 대상이 darkeden.exe가 아님 |
| NTSTATUS 0xC0000141 | null 포인터 또는 유효하지 않은 주소 |
| NTSTATUS 0xC0000095 | 주소 오버플로 또는 잘못된 포인터 크기 |
| NTSTATUS 0x8000000D | 부분 읽기. 주소 변경, 해제, 종료 등 확인 |
| NTSTATUS 0xC0000005 | 메모리 접근 예외 |
| 서비스 시작 오류 577 | Windows가 드라이버 서명을 허용하지 않음 |

## 검증 범위

이 PC에서 MinGW-w64로 GUI와 C++ 테스트를 빌드했습니다. 테스트는 기존 좌표 경로,
독립 X/Y 요청, 두 좌표 요청, 0/음수, float, 64비트 포인터, null, 오버플로, 부분 읽기,
요청 검증, 응답 길이/버전/축 검증, 표시 문자열을 포함합니다.

설치형 Visual Studio 없이 공식 MSVC/SDK/WDK 패키지를 이용해 **드라이버 빌드와 테스트
서명 검증을 완료했습니다**. 드라이버 로드 및 실제 게임 메모리 읽기는 검증하지 않았습니다.
GUI 실행 파일만으로는 실제 좌표를 읽을 수 없습니다.
가짜 메모리 테스트는 커널 동작 검증을 대신하지 않습니다. 서명된 드라이버의 실제 검증은
별도의 테스트 Windows 환경에서 수행하세요. 한 번의 좌표 읽기도 원자적 스냅샷은 아닙니다.

## 참고 문서

- [WDK 다운로드](https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk)
- [Windows 드라이버 빌드](https://learn.microsoft.com/windows-hardware/drivers/develop/building-a-windows-driver)
- [커널 모드 코드 서명](https://learn.microsoft.com/windows-hardware/drivers/install/kernel-mode-code-signing-policy--windows-vista-and-later-)
- [핸들 권한 검사](https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/nf-wdm-obreferenceobjectbyhandle)
- [장치 ACL](https://learn.microsoft.com/windows-hardware/drivers/kernel/sddl-for-device-objects)
