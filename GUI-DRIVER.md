# DarkEden 커널 좌표 리더

## 현재 Python 호출 경로 (2026-10-10)

솔루션의 기본 사용자 영역 빌드 대상은 `DarkEdenValueEngine.dll`입니다.
`python\darkeden.py`의 `read_value(ValueType, pid=None)`가 DLL을 호출하고,
`python\gui.py`는 내 X/Y좌표, 내 캐릭터명, 상대방 이름과 현재 좌표를 보여주는 테스트 화면입니다.

```powershell
.\scripts\Build-Engine.ps1 -ToolchainRoot '<toolchain 경로>'
.\scripts\Build-Driver.ps1 -ToolchainRoot '<toolchain 경로>'
.\scripts\Run-Gui.ps1
```

새 캐릭터명 기능은 `ReadText` IOCTL을 사용하므로 빌드한 새 드라이버를 서명하고
적용해야 동작합니다. 이전 C++ GUI 소스와 빌드 스크립트는 기존 작업을 보존하기
위해 남겨두었으며 솔루션의 기본 빌드 대상에서는 제외했습니다.

상대방 이름은 `darkeden.exe+00AB31D0`에서 시작하는 32비트 포인터 경로
`34 → 0 → 14 → 544 → 0`을 사용합니다. 마지막 `+0`은
문자열의 주소이며 추가 역참조를 하지 않습니다. TextRequest의 reserved=1로
구분하고, 기존 내 캐릭터명 요청은 reserved=0으로 유지합니다.
새 드라이버의 자체 메모리 통합 검사로 경로를 확인합니다.
상대방 현재 좌표는 같은 루트에서 `34 → 0 → 14`를 따라간 객체의
`+0x0C/+0x0E`에 있는 16비트 값을 사용합니다. 좌표 두 값은 한 번의
드라이버 요청으로 읽고 GUI에서 200ms 간격으로 자동 갱신할 수 있습니다.

## 현재 X/Y 조회 (2026-10-10)

GUI 기본 모드 '내 좌표 X / Y (32비트)'에서 X, Y, 둘 다 조회를 지원합니다.
- X: p1 = read32(imageBase + 0x009FB03C); p2 = read32(p1 + 4); p3 = read32(p2 + 0xA8); X = readInt32(p3 + 0x20C).
- Y: p1 = read32(imageBase + 0x009DF9A8); p2 = read32(p1 + 0xC); p3 = read32(p2 + 0x44); Y = readInt32(p3 + 0x210).
CE의 오프셋은 16진수로 해석합니다. X의 실제 추적 순서는 4 → A8 → 20C이며 Y는 C → 44 → 210입니다.
CE 화면의 최종 주소 2D6707B4와 값 32는 해당 실행 시점의 예시이며 프로그램에 고정하지 않습니다.
X 시작 오프셋은 화면 입력값을 사용하고 Y 시작 오프셋은 위 경로로 고정합니다.
두 좌표 요청은 X와 Y를 순서대로 별도 IOCTL로 읽으므로 원자적 스냅샷은 아닙니다.
한쪽이라도 실패하면 좌표 쌍을 표시하지 않고 실패한 축의 오류를 표시합니다.
기존 직접 읽기와 옛 32/64비트 좌표 경로는 선택 가능한 모드로 유지합니다.
X 요청은 reserved=3, pointerBytes=4, axis=X이며 GUI와 드라이버를 함께 갱신해야 합니다. 기존 reserved=1 단일 포인터 요청의 의미는 유지하여 이전 드라이버가 새 X 경로를 잘못 해석하지 않게 합니다.
Y 요청은 reserved=4, pointerBytes=4, axis=Y 경로를 사용합니다. 이전 reserved=2 경로는 호환용으로 남겨두었습니다.
진단 도구는 ReadDirectValue.exe <PID> --x로 X만, --xy로 X/Y를 조회합니다. --single은 --x의 호환 별칭입니다.
검증: GUI, 엔진, 드라이버 빌드 및 회귀 테스트 통과. Y 경로는 첨부 화면의 포인터 주소와 값 141을 재현한 메모리 테스트로 확인했습니다. 새 드라이버의 설치 및 실제 게임 좌표값은 아직 확인하지 않았습니다.
아래 단일 X 모드 설명은 이전 변경 이력입니다.


## X좌표 경로 적용 (2026-09-27)

최신 CE 화면에 맞춰 기본 X 시작 오프셋을 0x001B6068로 변경했습니다.
X = readInt32(readUInt32(darkeden.exe 이미지 베이스 + 0x001B6068) + 0).
화면의 최종 주소 016A3868 및 값 48은 해당 실행 시점의 결과이며 코드에 고정하지 않습니다.
단일 포인터 모드의 결과와 요청 버튼을 X좌표로 표시합니다. Y 경로는 아직 미확인입니다.
기존 단일 포인터 지원 드라이버를 그대로 사용하며 GUI와 진단 도구만 다시 빌드합니다.
아래 0x001BD6B0 관련 내용은 이전 경로 기록입니다.


## 단일 포인터 모드 (2026-09-27)

CE 화면에서 확인한 시작점 darkeden.exe + 0x001BD6B0에서 32비트 포인터를
한 번 읽고, 그 주소 + 0에서 4바이트를 읽는 모드를 GUI 기본값으로 적용했습니다.
자료형 기본값은 int32이며 float32도 선택할 수 있습니다. Y 주소는 미확인으로
이 모드에서는 Y/둘 다 요청을 제공하지 않습니다.
프로토콜 v2의 reserved=1, pointerBytes=4, axis=X 조합으로 요청합니다.
이전 드라이버는 이 요청을 거부하므로 새 GUI와 새 드라이버가 모두 필요합니다.
GUI/드라이버 빌드 및 세 회귀 테스트가 통과했습니다. 테스트 서명 Valid 확인.
게임 실행 중 새 드라이버 시작은 오류 647로 실패했습니다. 게임 정상 종료 후
scripts\Test-Environment.ps1 -OpenGui로 시작 및 실측을 이어서 확인해야 합니다.


## 현재 기본 읽기 방식 (2026-09-27)

GUI 기본값을 **32비트 포인터 경로 / int32 / 시작 오프셋 0x009CB97C**로 복원했습니다.
드라이버가 darkeden.exe 이미지 베이스에 시작 오프셋을 더한 뒤,
포인터를 읽어 C → 44 → 214(X)/218(Y) 경로를 추적합니다.
기존 드라이버가 이 경로를 지원하므로 GUI만 다시 빌드하며 드라이버 교체는 필요 없습니다.
직접 값 읽기는 선택 가능한 보조 모드로 유지합니다. 이 모드에서 0x9E3868을 읽으려면
읽기 방식과 오프셋을 직접 바꾸세요. 모드 전환 시 오프셋이 자동 변경되지는 않습니다.
이 기본 경로의 실제 게임 주소 유효성은 별도로 확인해야 합니다.

아래 직접 읽기 추가 및 설치 기록은 변경 이력입니다.


## 직접 값 읽기 추가 (2026-09-27)

GUI 기본 모드는 **직접 값 읽기**, 기본 모듈 오프셋은 **0x009E3868**입니다.
`darkeden.exe` 이미지 베이스에 입력 오프셋을 더한 위치에서 정확히 4바이트를
한 번 읽습니다. 포인터 역참조 및 기존 C → 44 → 214/218 경로는 사용하지 않습니다.
자료형은 CE의 Type과 맞춰 int32 또는 float32를 선택하고 **값 읽기 (4바이트)**를 누릅니다.
Y 위치는 아직 확인되지 않아 직접 모드에서는 Y/둘 다 버튼을 비활성화합니다.

기존 포인터 경로는 읽기 방식에서 32/64비트 포인터 경로를 선택하면 사용할 수 있습니다.
오프셋은 자동 전환되지 않으므로 기존 경로를 사용할 때는 해당 시작 오프셋을 입력하세요.
프로토콜 v2의 요청 크기는 유지하며, 기존에 거부하던 pointerBytes=0을 직접 읽기로
확장했습니다. 이전 드라이버는 이 요청을 거부하므로 GUI와 드라이버 모두 다시 빌드하고
새 드라이버를 서명·로드해야 합니다. 기존 프로세스 접근 권한 검사는 유지됩니다.

이번 PC에서 MSVC GUI/드라이버 빌드와 세 회귀 테스트 실행이 통과했습니다.
개발용 서명(Windows 검증 Valid), 인증서 신뢰 등록, DarkEdenReader 수동 시작 서비스 등록,
TESTSIGNING 부팅 설정을 완료했습니다. 현재는 재부팅 대기이며 실제 드라이버 읽기는 미검증입니다.
재부팅 후 관리자 PowerShell에서 `scripts\Test-Environment.ps1 -OpenGui`를 실행하면
자체 메모리 통합 검사, 실행 중 게임의 직접 값 조회, GUI 열기를 수행합니다.
세부 설치 결과는 `build\driver\Release\test-setup-result.json`에 있습니다.

아래의 과거 빌드·서명·실행 기록은 이번 직접 읽기 변경의 검증 결과가 아닙니다.


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

기본값은 **darkeden.exe 모듈 기준 오프셋 `0x009CB97C`, 32비트 포인터, int32**입니다.
게임 버전과 실제 메모리 구조에 맞는지 확인해야 합니다. 기준 주소는 항상 16진수입니다.

```text
baseAddress = darkeden.exe image base + 0x009CB97C
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

## 재부팅 후 실제 검증 (2026-09-27 10:04)

- 테스트 서명 모드 활성화 확인, DarkEdenReader 서비스 Running 및 초기화 상태 0 확인.
- 실제 드라이버 IOCTL 자체 메모리 검사 13개 통과: 직접 읽기 6개 및 64비트 포인터 경로 7개.
- 32비트 포인터 경로는 테스트 이미지가 4GiB 위에 로드되어 이번 실제 검사에서는 건너뜀.
- GUI 실행 완료. 게임 프로세스가 없어 실제 게임의 0x9E3868 값 읽기는 아직 미검증.
