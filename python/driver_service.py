"""Start the installed DarkEdenReader service from the test GUI."""

import hashlib
import os
from pathlib import Path
import re
import subprocess
import time
import winreg


SERVICE_NAME = "DarkEdenReader"
SC_EXE = str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "sc.exe")
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
CURRENT_DRIVER = Path(__file__).resolve().parents[1] / "build" / "driver" / "Release" / "DarkEdenReader.sys"


def _sc(*arguments):
    return subprocess.run(
        [SC_EXE, *arguments], capture_output=True, text=True,
        errors="replace", timeout=15, creationflags=NO_WINDOW,
    )


def _state():
    result = _sc("query", SERVICE_NAME)
    if result.returncode:
        raise RuntimeError("DarkEdenReader 서비스가 설치되어 있지 않거나 상태를 읽을 수 없습니다.")
    # The field label is localized; sc.exe keeps the state name in English.
    match = re.search(r":\s*(\d+)\s+(?:STOPPED|START_PENDING|STOP_PENDING|RUNNING|PAUSED|PAUSE_PENDING|CONTINUE_PENDING)\b", result.stdout)
    if not match:
        raise RuntimeError("드라이버 상태를 확인할 수 없습니다.")
    return int(match.group(1))


def service_status():
    try:
        return "실행 중" if _state() == 4 else "중지됨"
    except (OSError, subprocess.TimeoutExpired, RuntimeError):
        return "상태 확인 불가"


def _file_hash(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.digest()


def _installed_driver_path():
    key_path = rf"SYSTEM\CurrentControlSet\Services\{SERVICE_NAME}"
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_path) as key:
        image_path, _ = winreg.QueryValueEx(key, "ImagePath")
    path = os.path.expandvars(image_path.strip().strip('"'))
    if path.startswith("\\??\\"):
        path = path[4:]
    return Path(path)


def start_driver():
    """Start the installed service and report when it is an older build."""
    try:
        installed = _installed_driver_path()
        if not installed.is_file():
            return "설치된 드라이버 파일을 찾을 수 없습니다. 설치 상태를 확인하세요."
        warning = ""
        if CURRENT_DRIVER.is_file() and _file_hash(installed) != _file_hash(CURRENT_DRIVER):
            warning = " 설치된 드라이버가 현재 빌드와 달라 새 Y좌표 경로는 동작하지 않을 수 있습니다."
        if _state() == 4:
            return "드라이버가 이미 실행 중입니다." + warning
        result = _sc("start", SERVICE_NAME)
        if result.returncode:
            if result.returncode == 5:
                return "드라이버 시작 권한이 없습니다. 프로그램을 관리자 권한으로 실행하세요."
            return f"드라이버 시작 실패 (Windows 오류 {result.returncode})."
        for _ in range(20):
            if _state() == 4:
                return "드라이버가 실행 중입니다." + warning
            time.sleep(0.5)
        return "시작 요청을 보냈지만 실행 상태를 확인하지 못했습니다."
    except (OSError, subprocess.TimeoutExpired, RuntimeError) as exc:
        return f"드라이버 시작 실패: {exc}"
