"""Python API for the DarkEden C++ value engine.

    from darkeden import ValueType, read_value
    print(read_value(ValueType.X))
"""

from __future__ import annotations

import ctypes
import json
from enum import IntEnum
from pathlib import Path


class ValueType(IntEnum):
    X = 1
    Y = 2
    CHARACTER_NAME = 3
    OTHER_CHARACTER_NAME = 4
    OTHER_CHARACTER_COORDINATES = 5


class DarkEdenError(RuntimeError):
    pass


_engine = None


def _load_engine():
    global _engine
    if _engine is not None:
        return _engine
    root = Path(__file__).resolve().parents[1]
    candidates = (
        root / "build" / "engine" / "Release" / "DarkEdenValueEngine.dll",
        root / "build" / "engine" / "Debug" / "DarkEdenValueEngine.dll",
    )
    path = next((candidate for candidate in candidates if candidate.is_file()), None)
    if path is None:
        raise DarkEdenError("C++ 엔진 DLL이 없습니다. 먼저 엔진을 빌드하세요.")
    try:
        library = ctypes.WinDLL(str(path))
    except OSError as exc:
        raise DarkEdenError(f"C++ 엔진 DLL을 열 수 없습니다: {exc}") from exc
    library.DE_ReadValue.argtypes = (
        ctypes.c_uint32, ctypes.c_uint32, ctypes.c_wchar_p, ctypes.c_uint32,
        ctypes.c_wchar_p, ctypes.c_uint32,
    )
    library.DE_ReadValue.restype = ctypes.c_int
    library.DE_ListProcesses.argtypes = (ctypes.POINTER(ctypes.c_uint32), ctypes.c_uint32)
    library.DE_ListProcesses.restype = ctypes.c_uint32
    library.DE_ListNearbyCharacters.argtypes = (
        ctypes.c_uint32, ctypes.c_wchar_p, ctypes.c_uint32,
        ctypes.c_wchar_p, ctypes.c_uint32,
    )
    library.DE_ListNearbyCharacters.restype = ctypes.c_int
    _engine = library
    return library


def list_processes() -> list[int]:
    """Return PIDs of running darkeden.exe processes."""
    library = _load_engine()
    ids = (ctypes.c_uint32 * 128)()
    count = library.DE_ListProcesses(ids, len(ids))
    return list(ids[:count])


def read_value(value_type: ValueType, pid: int | None = None) -> int | str:
    """Read one value. Coordinates return int; character name returns str."""
    kind = ValueType(value_type)
    if pid is None:
        processes = list_processes()
        if not processes:
            raise DarkEdenError("darkeden.exe가 실행 중이지 않습니다.")
        if len(processes) > 1:
            raise DarkEdenError("프로세스가 여러 개입니다. pid를 지정하세요.")
        pid = processes[0]
    if not isinstance(pid, int) or not 0 < pid <= 0xFFFFFFFF:
        raise ValueError("pid는 양의 32비트 정수여야 합니다.")
    output = ctypes.create_unicode_buffer(512)
    error = ctypes.create_unicode_buffer(1024)
    code = _load_engine().DE_ReadValue(pid, int(kind), output, len(output), error, len(error))
    if code:
        raise DarkEdenError(error.value or f"값을 읽지 못했습니다 (오류 {code}).")
    return int(output.value) if kind in (ValueType.X, ValueType.Y) else output.value


def list_nearby_characters(pid: int | None = None) -> list[dict[str, int | str]]:
    """Read nearby characters as name, race, ID, X and Y dictionaries."""
    if pid is None:
        processes = list_processes()
        if not processes:
            raise DarkEdenError("darkeden.exe가 실행 중이지 않습니다.")
        if len(processes) > 1:
            raise DarkEdenError("프로세스가 여러 개입니다. pid를 지정하세요.")
        pid = processes[0]
    if not isinstance(pid, int) or not 0 < pid <= 0xFFFFFFFF:
        raise ValueError("pid는 양의 32비트 정수여야 합니다.")
    output = ctypes.create_unicode_buffer(65536)
    error = ctypes.create_unicode_buffer(1024)
    code = _load_engine().DE_ListNearbyCharacters(
        pid, output, len(output), error, len(error)
    )
    if code:
        raise DarkEdenError(error.value or f"주변 목록을 읽지 못했습니다 (오류 {code}).")
    try:
        characters = json.loads(output.value)
    except json.JSONDecodeError as exc:
        raise DarkEdenError("주변 목록 응답 형식이 올바르지 않습니다.") from exc
    if not isinstance(characters, list):
        raise DarkEdenError("주변 목록 응답 형식이 올바르지 않습니다.")
    return characters
