"""ctypes wrapper around the C++ game logic (cpp/src/capi.h).

The shared library is built on demand with CMake the first time it is needed.
Set CW_LIB to use a prebuilt library from somewhere else.
"""
import ctypes
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CPP_DIR = ROOT / "cpp"
BUILD_DIR = CPP_DIR / "build"
LIB_NAME = "libcomplexweeper_capi.so" if sys.platform != "darwin" else "libcomplexweeper_capi.dylib"

# Game states returned by Engine.state
READY, PLAYING, WON, LOST = 0, 1, 2, 3
# Solver move kinds and reasons (see capi.h)
KIND_NONE, KIND_OPEN, KIND_FLAG, KIND_RETYPE = 0, 1, 2, 3
REASON_FIRST_CLICK, REASON_CERTAIN, REASON_GUESS = 0, 1, 2
# Modes
MODE_COMPLEX, MODE_HYPER = 0, 1

# Difficulty names, in the order of the library's presets (the mine counts differ per mode).
LEVEL_NAMES = ("Beginner", "Intermediate", "Expert")


class Cell(ctypes.Structure):
    _fields_ = [
        ("clue", ctypes.c_int16),
        ("open", ctypes.c_uint8),
        ("flag", ctypes.c_uint8),
        ("mine", ctypes.c_uint8),
        ("blank", ctypes.c_uint8),
        ("reserved", ctypes.c_uint8 * 2),
    ]


class Move(ctypes.Structure):
    _fields_ = [
        ("kind", ctypes.c_int32),
        ("cell", ctypes.c_int32),
        ("reason", ctypes.c_int32),
        ("type", ctypes.c_int32),
        ("retyped", ctypes.c_int32),
        ("risk", ctypes.c_float),
    ]


def _build_library():
    """Configure and build the shared library with CMake."""
    subprocess.run(
        ["cmake", "-S", str(CPP_DIR), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release"],
        check=True, stdout=subprocess.DEVNULL)
    subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--target", "complexweeper_capi", "-j"],
        check=True, stdout=subprocess.DEVNULL)


def _library_path():
    override = os.environ.get("CW_LIB")
    if override:
        return Path(override)
    path = BUILD_DIR / LIB_NAME
    sources = [p for p in (CPP_DIR / "src").iterdir() if p.suffix in (".cpp", ".hpp", ".h")]
    newest_source = max(p.stat().st_mtime for p in sources)
    if not path.exists() or path.stat().st_mtime < newest_source:
        _build_library()
    return path


def _load():
    lib = ctypes.CDLL(str(_library_path()))
    c_int, c_u32, c_vp = ctypes.c_int, ctypes.c_uint32, ctypes.c_void_p
    sigs = {
        "cw_create": (c_vp, []),
        "cw_destroy": (None, [c_vp]),
        "cw_new_game": (None, [c_vp, c_int, c_int, c_int, c_int, c_u32]),
        "cw_set_judge_loose": (None, [c_vp, c_int]),
        "cw_set_solver_orientation": (None, [c_vp, c_int]),
        "cw_preset": (c_int, [c_int, c_int, ctypes.POINTER(c_int), ctypes.POINTER(c_int), ctypes.POINTER(c_int)]),
        "cw_width": (c_int, [c_vp]),
        "cw_height": (c_int, [c_vp]),
        "cw_mines": (c_int, [c_vp]),
        "cw_mode": (c_int, [c_vp]),
        "cw_state": (c_int, [c_vp]),
        "cw_boom_cell": (c_int, [c_vp]),
        "cw_marked_count": (c_int, [c_vp]),
        "cw_undo_depth": (c_int, [c_vp]),
        "cw_get_cells": (None, [c_vp, ctypes.POINTER(Cell)]),
        "cw_click": (c_int, [c_vp, c_int, c_u32]),
        "cw_cycle_flag": (c_int, [c_vp, c_int]),
        "cw_chord": (c_int, [c_vp, c_int]),
        "cw_solver_step": (c_int, [c_vp, c_u32, ctypes.POINTER(Move)]),
        "cw_undo": (c_int, [c_vp]),
    }
    for name, (res, args) in sigs.items():
        fn = getattr(lib, name)
        fn.restype = res
        fn.argtypes = args
    return lib


class Engine:
    """One game session: rules, solver and undo history, all implemented in C++."""

    def __init__(self):
        self._lib = _load()
        self._s = self._lib.cw_create()
        if not self._s:
            raise MemoryError("cw_create failed")
        self.new_game(*self.preset(MODE_COMPLEX, "Beginner"), MODE_COMPLEX, 1)

    def close(self):
        if self._s:
            self._lib.cw_destroy(self._s)
            self._s = None

    __del__ = close

    # ---- game setup and queries ----
    def preset(self, mode, level):
        """(width, height, mines) of a difficulty level ("Beginner", ...) in the given mode."""
        w, h, m = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
        if not self._lib.cw_preset(mode, LEVEL_NAMES.index(level), ctypes.byref(w), ctypes.byref(h), ctypes.byref(m)):
            raise ValueError("no such preset")
        return w.value, h.value, m.value

    def new_game(self, width, height, mines, mode, seed):
        self._lib.cw_new_game(self._s, width, height, mines, mode, seed & 0xFFFFFFFF)

    def set_solver_orientation(self, orientation):
        """Which of the equivalent flag labelings the solver produces (0..7, 0 is canonical)."""
        self._lib.cw_set_solver_orientation(self._s, orientation)

    width = property(lambda self: self._lib.cw_width(self._s))
    height = property(lambda self: self._lib.cw_height(self._s))
    mines = property(lambda self: self._lib.cw_mines(self._s))
    mode = property(lambda self: self._lib.cw_mode(self._s))
    state = property(lambda self: self._lib.cw_state(self._s))
    boom_cell = property(lambda self: self._lib.cw_boom_cell(self._s))
    flags = property(lambda self: self._lib.cw_marked_count(self._s))
    undo_depth = property(lambda self: self._lib.cw_undo_depth(self._s))

    def cells(self):
        """All cells in row-major order (a ctypes array of Cell)."""
        n = self.width * self.height
        arr = (Cell * n)()
        self._lib.cw_get_cells(self._s, arr)
        return arr

    # ---- manual actions: return True if the position changed ----
    def click(self, cell, now_ms=0):
        return bool(self._lib.cw_click(self._s, cell, now_ms & 0xFFFFFFFF))

    def cycle_flag(self, cell):
        return bool(self._lib.cw_cycle_flag(self._s, cell))

    def chord(self, cell):
        return bool(self._lib.cw_chord(self._s, cell))

    # ---- solver and undo ----
    def solver_step(self, now_ms=0):
        """Perform one solver step. Returns a Move (with .kind, .cell, .reason, .type, .retyped, .risk),
        or None if there is nothing to do."""
        mv = Move()
        if self._lib.cw_solver_step(self._s, now_ms & 0xFFFFFFFF, ctypes.byref(mv)):
            return mv
        return None

    def undo(self):
        return bool(self._lib.cw_undo(self._s))
