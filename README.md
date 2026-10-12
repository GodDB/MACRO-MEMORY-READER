# Windows 커널 메모리 읽기 및 Python 값 API

## 주변 캐릭터 ID로 선택 (2026-10-12)

Python GUI에서 **주변 캐릭터 → 목록 새로고침**을 누르고 한 명을 고른 뒤
**선택한 캐릭터 지정**을 누릅니다. 앱은 선택한 행의 ID를 C++ 엔진과
`SelectTarget` IOCTL을 거쳐 `DarkEdenReader` 커널 드라이버에 전달합니다.
Cheat Engine, 마우스 이동, 마우스 클릭은 선택 동작에 쓰이지 않습니다.

드라이버는 `darkeden.exe+AB3254`의 선택 컨트롤러를 확인하고,
`darkeden.exe+AB31D0`의 주변 캐릭터 트리에서 요청 ID와 객체 ID·좌표를
검증한 뒤 컨트롤러 `+0x28`의 4바이트 ID 한 곳만 갱신합니다.
게임 버전이 바뀌어 컨트롤러 vtable이 달라지면 쓰지 않고 오류를 반환합니다.
Python에서 직접 호출하려면 `select_nearby_character(pid, character_id)`를 사용합니다.
이 기능은 프로토콜 버전 3의 새 드라이버와 C++ 엔진 DLL이 함께 필요합니다.

2026-10-12 새 드라이버 서명·설치·실행과 자체 프로세스 IOCTL 검사는 통과했습니다.
당시 게임 프로세스가 종료돼 실제 게임 화면의 선택 표시는 아직 검증하지 못했습니다.

## 주변 캐릭터 목록 포인터 (2026-10-11)

Cheat Engine Lua Engine에서 `dofile([[C:\Users\Administrator\Desktop\MACRO\auto_nearby.lua]])`를 실행하면
현재 주변 캐릭터의 이름, X, Y, ID 및 객체 주소를 출력합니다. 읽기 전용입니다.

32비트 게임의 모듈 기준 경로는 다음과 같습니다.

```text
manager = read32(darkeden.exe + 0x00AB31D0)
header  = read32(manager + 0x34)
root    = read32(header + 0x04)
```

`root`부터 왼쪽 자식 `node+0x00`, 오른쪽 자식 `node+0x08`을 순회합니다.
`header`는 빈 자식의 센티널이며, `node+0x04`는 부모 포인터입니다.
각 노드에서 `read32(node+0x14)`가 캐릭터 객체이고, `node+0x10`은 ID입니다.
객체의 `+0x08`은 ID, `+0x0C`와 `+0x0E`는 각각 2바이트 X/Y 좌표입니다.
유저 ID는 맵에 따라 `0xFFFF`를 넘으므로 16비트 상한으로 거르지 않습니다.
이 필터를 제거한 뒤 다른 맵에서 `코요타`, `비빔`, 본인 `helloworl`을
포함한 26개 항목의 이름과 좌표를 실제 게임에서 확인했습니다.
이름은 ID와 함께 저장된 길이 지정 ASCII 문자열에서 찾습니다.

게임 재시작 전후에 같은 모듈 경로로 `dongbeom`, `heroman`, `mobility`, `crazyfuc`
네 캐릭터를 다시 읽어 확인했습니다. 중간 주소와 `crazyfuc` ID는 재시작 시 바뀌었습니다.

Python 앱에서도 `list_nearby_characters(pid)`로 현재 목록을 읽을 수 있습니다.
반환값은 `[{"name": "dongbeom", "race": "Vampire", "life": "생존", "id": 12039, "x": 52, "y": 50}, ...]`
형식이며, GUI의 **주변 캐릭터 → 목록 새로고침** 버튼에서 이름·종족·X·Y·ID를
표시합니다. 2026-10-12부터 **상태** 열도 함께 표시합니다. 경로는
`Python → C++ DLL → DarkEdenReader 드라이버`입니다.
일반 캐릭터 이름은 객체의 `+0x544` 포인터가 가리키는 문자열이고, 이 포인터가
비어 있는 본인 캐릭터는 기존 본인 이름 경로에서 읽습니다. 이름 경로가 모두
비어 있으면 `ID_...`로 표시합니다. 드라이버가 실행 중이어야 앱에서 읽을 수 있습니다.
종족은 객체의 `+0x558`, `+0x610` 값을 함께 읽어, 관찰된 조합
`(0x42, 0)=Ousters`, `(0, 2)=Vampire`, `(0x63, 0)=Slayer`만 표시합니다.
다른 조합은 `미확인`입니다. `heroman`·`hellow`(Ousters),
`dongbeom`·`mobility`(Vampire), `companion`(Slayer)으로 실제 조회를 확인했습니다.
생존 상태는 객체 `+0x548`의 4바이트 값으로 `1=생존`, `0=사망`, 그 외는
`미확인`으로 표시합니다. 같은 `dongbeom`의 생존·사망 스냅샷과 부활 스냅샷에서
각각 `1 → 0 → 1`을 확인했습니다.
2026-10-11 재빌드 후 `DarkEdenReader` 실행과 GUI 실행을 확인했습니다.
당시 게임 프로세스가 실행 중이지 않아 실제 게임 목록 조회는 아직 검증하지
못했습니다. C++ 엔진 DLL 빌드와 Python 응답 처리 검사는 통과했습니다.

## Python API와 테스트 화면 (2026-10-10)

현재 사용 경로는 `Python → C++ DLL → 커널 드라이버`입니다. Python에서는
`read_value(타입, pid)`로 내 X/Y좌표와 내 캐릭터명을 읽습니다.
주변 캐릭터의 이름과 좌표는 `list_nearby_characters(pid)`로 함께 조회합니다.

```python
from python.darkeden import ValueType, read_value

x = read_value(ValueType.X)                  # int
y = read_value(ValueType.Y)                  # int
name = read_value(ValueType.CHARACTER_NAME) # str
```

`pid`를 생략하면 실행 중인 `darkeden.exe`가 정확히 하나일 때 자동 선택합니다.
여러 개면 `read_value(ValueType.X, pid=1234)`처럼 PID를 지정합니다.
타입 ID는 C++의 `DE_ReadValue` 함수에서 분기하며, 새 타입은 그 함수와 Python의
`ValueType` 열거형에 추가할 수 있습니다.

빌드: `scripts\Build-Engine.ps1 -ToolchainRoot <MSVC/SDK toolchain 경로>` 및
`scripts\Build-Driver.ps1 -ToolchainRoot <MSVC/WDK toolchain 경로>`.
테스트 GUI: `scripts\Run-Gui.ps1` 또는 `python python\gui.py`.
기존 C++ GUI는 솔루션의 기본 빌드 대상에서 제외했습니다.

캐릭터명은 CE 화면의 `darkeden.exe+000A8B14`에서 32비트 포인터를 읽고,
그 포인터가 가리키는 `+0` 위치에서 최대 256바이트를 NUL까지 읽습니다.
UTF-8을 우선 해석하고, 실패하면 CP949를 사용합니다. UTF-16LE 문자열로
보이는 짧은 응답은 재조회합니다. CE 화면의 `0158CB88`은 해당 실행 시점의
주소이므로 코드에 고정하지 않았습니다. 새 문자열 명령을 사용하려면
새 드라이버를 서명하고 적용해야 합니다. 현재 실제 게임에서 캐릭터명 값은
아직 확인하지 않았습니다.

주변 목록은 `darkeden.exe+00AB31D0`에서 시작하는 트리를 순회합니다.
각 캐릭터 객체의 `+0x544`에서 이름 포인터를, `+0x0C/+0x0E`에서 현재
X/Y좌표를 읽습니다. GUI의 단일 상대방 이름·좌표 항목은 제거했습니다.

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
