"""Minimize / restore / query another process's window (Windows, ctypes).

Why this exists: 窗口最小化静音 是"窗口状态 -> 引擎音量"的联动，而它的触发条件
(SDL_WINDOW_MINIMIZED) 只有窗口真的被最小化才会出现。无头跑 `--screenshot` 的窗口
永远是最小化不了的，所以验证只能从外面把窗口按下去。

用 Win32 而不是 PowerShell 的 Add-Type/SendKeys：
- 沙箱里 Add-Type 会被安全策略拦下（runtime code compilation）；
- SendKeys 走的是键盘焦点，跟"窗口状态"没关系。

找窗口靠 (image name -> pid -> EnumWindows 里有标题且可见的那个)，所以同一台机器上
跑着别的 cppsekai 时可以用 --image 或先自己关掉它。

    python .workbuddy/tools/win_window.py minimize
    python .workbuddy/tools/win_window.py restore
    python .workbuddy/tools/win_window.py status
    python .workbuddy/tools/win_window.py minimize --image chartdl.exe
"""

import argparse
import ctypes
import subprocess
import sys
from ctypes import wintypes

u32 = ctypes.WinDLL("user32", use_last_error=True)
CallbackType = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

SW_MINIMIZE = 6
SW_RESTORE = 9
SW_SHOW = 5

u32.EnumWindows.argtypes = [CallbackType, wintypes.LPARAM]
u32.IsWindowVisible.argtypes = [wintypes.HWND]
u32.IsIconic.argtypes = [wintypes.HWND]
u32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
u32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
u32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
u32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]


def pids_of(image):
    """Every pid whose image name is `image` (tasklist skips the PATH games)."""
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq %s" % image, "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    pids = []
    for line in out.splitlines():
        cells = [c.strip().strip('"') for c in line.split('","')]
        if len(cells) >= 2 and cells[0].lower() == image.lower():
            try:
                pids.append(int(cells[1]))
            except ValueError:
                pass
    return pids


def windows_of(pids):
    """Visible, titled top-level windows owned by any of `pids`."""
    found = []

    def cb(hwnd, _param):
        pid = wintypes.DWORD()
        u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids and u32.IsWindowVisible(hwnd):
            length = u32.GetWindowTextLengthW(hwnd)
            if length > 0:
                buf = ctypes.create_unicode_buffer(length + 1)
                u32.GetWindowTextW(hwnd, buf, length + 1)
                found.append((hwnd, buf.value))
        return True

    u32.EnumWindows(CallbackType(cb), 0)
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["minimize", "restore", "status"])
    ap.add_argument("--image", default="cppsekai.exe",
                    help="process image name to look for (default cppsekai.exe)")
    args = ap.parse_args()

    pids = pids_of(args.image)
    if not pids:
        print("no %s process running" % args.image)
        return 1
    wins = windows_of(pids)
    if not wins:
        print("%s is running (pid %s) but has no visible titled window" % (args.image, pids))
        return 1

    hwnd, title = wins[0]
    if len(wins) > 1:
        print("note: %d windows found, using the first" % len(wins))
    print("pid=%s hwnd=%s title=%r" % (pids, hwnd, title))

    if args.action == "minimize":
        u32.ShowWindow(hwnd, SW_MINIMIZE)
    elif args.action == "restore":
        u32.ShowWindow(hwnd, SW_RESTORE)
    print("isIconic=%s" % bool(u32.IsIconic(hwnd)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
