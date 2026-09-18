"""List visible top-level windows (hwnd, pid, class, title).

Handy when a scripted UI run has to find a window whose title is not known
up front (the game appends the player label: "CppSekai - 玩家 1").

Usage:  python winlist.py [substring]
"""

import ctypes
import sys
from ctypes import wintypes

user32 = ctypes.windll.user32
EnumWindows = user32.EnumWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def main() -> int:
    needle = sys.argv[1] if len(sys.argv) > 1 else ""

    def cb(hwnd, lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n == 0:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        cls = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cls, 256)
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        title = buf.value
        if needle and needle not in title:
            return True
        print(f"{hwnd}\t{pid.value}\t{cls.value}\t{title}")
        return True

    EnumWindows(EnumWindowsProc(cb), 0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
