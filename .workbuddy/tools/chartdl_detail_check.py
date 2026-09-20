"""Dump chartdl's 下载内容 checkboxes for one song row, without a screenshot.

Why this exists
---------------
The panel's whole job is "which boxes can I tick", and that state is invisible in
a PNG unless someone looks at it - and the failure we hit (a level table with no
row for the CN-only songs -> all five difficulty boxes greyed out) looked like a
perfectly normal panel.

So: launch `chartdl.exe --select <row>` (the app selects the row itself, which is
the only cross-process-safe way - LVM_SETITEMSTATE needs a pointer, and the
common controls do not marshal that), then read every BUTTON back with
GetWindowTextW / IsWindowEnabled / BM_GETCHECK. Those three are parameter-free,
so they work from another process.

Usage:  python chartdl_detail_check.py <chartdl.exe> <row> [seconds]
"""

import ctypes
import subprocess
import sys
import time
from ctypes import wintypes

user32 = ctypes.windll.user32

BM_GETCHECK = 0x00F0
BST_CHECKED = 1
MAIN_CLASS = "CppSekaiChartDl"


def find_main(timeout: float = 15.0) -> int:
    cls = ctypes.create_unicode_buffer(256)
    deadline = time.time() + timeout
    while time.time() < deadline:
        hit: list[int] = []

        def cb(h, _):
            if user32.IsWindowVisible(h):
                user32.GetClassNameW(h, cls, 256)
                if cls.value == MAIN_CLASS:
                    hit.append(h)
            return True

        user32.EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
        if hit:
            return hit[0]
        time.sleep(0.2)
    return 0


def descendants(root: int) -> list[int]:
    out: list[int] = []

    def cb(h, _):
        out.append(h)
        return True

    user32.EnumChildWindows(root, ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
    return out


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    exe, row = sys.argv[1], int(sys.argv[2])
    settle = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0

    proc = subprocess.Popen([exe, "--select", str(row)])
    try:
        window = find_main()
        if window == 0:
            print("!! main window never appeared")
            return 1
        time.sleep(settle)
        cls = ctypes.create_unicode_buffer(256)
        text = ctypes.create_unicode_buffer(512)
        rect = wintypes.RECT()
        buttons = 0
        for child in descendants(window):
            user32.GetClassNameW(child, cls, 256)
            if cls.value != "Button":
                continue
            user32.GetWindowTextW(child, text, 512)
            user32.GetWindowRect(child, ctypes.byref(rect))
            buttons += 1
            print("button {:>3}  id={:<5} {:>4}  check={}  visible={}  y={:<5}  {}".format(
                buttons,
                user32.GetDlgCtrlID(child),
                "ON" if user32.IsWindowEnabled(child) else "OFF",
                user32.SendMessageW(child, BM_GETCHECK, 0, 0),
                "yes" if user32.IsWindowVisible(child) else "no",
                rect.top,
                text.value))
        print(f"({buttons} checkboxes)")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
