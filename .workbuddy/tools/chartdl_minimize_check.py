"""Live check: does minimize + restore leave chartdl's two-column split alone?

WM_SIZE arrives with a 0x0 client area while the window is minimized, and
layoutChildren() clamps both dragged widths (gListWidth, gBottomHeight) against
the *current* client size and stores the result back into the same global. A
0x0 pass therefore collapses the user's split to the floors, and it stays
collapsed after the window comes back - the columns look "auto resized".

This minimizes a real chartdl and restores it (WM_SYSCOMMAND, the same thing the
title-bar button sends), grabbing the window with PrintWindow before and after.
Two assertions:
  * the left column's right edge must be in the same place, reported in pixels;
  * the whole image must be identical - a split that moved shows up as a diff.

Usage:  python chartdl_minimize_check.py <chartdl.exe> <outdir> [--select N] [--dpi N]
"""

import ctypes
import os
import sys
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

WM_SYSCOMMAND = 0x0112
SC_MINIMIZE = 0xF020
SC_RESTORE = 0xF120
PW_CLIENTONLY = 0x00000001
PW_RENDERFULLCONTENT = 0x00000002

sys.path.insert(0, os.path.expanduser("~/.workbuddy/binaries/python/envs/default/Lib/site-packages"))


def find_window(pid: int, timeout: float = 15.0) -> int:
    """The *own* window of the instance we launched.

    Matched by process id, not by title: chartdl allows several instances, and a
    title match also picks up a leftover window from an earlier run (or the one
    the user has open), which sits minimized at -32000 with a 0x0 client - and
    grabbing that instead of ours is exactly how this check would lie.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        hit: list[int] = []

        def cb(h, _):
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(h, ctypes.byref(owner))
            if owner.value != pid or not user32.IsWindowVisible(h) or user32.IsIconic(h):
                return True
            rect = wintypes.RECT()
            user32.GetClientRect(h, ctypes.byref(rect))
            if rect.right <= 0 or rect.bottom <= 0:
                return True
            hit.append(h)
            return True

        user32.EnumWindows(
            ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
        if hit:
            return hit[0]
        time.sleep(0.25)
    return 0


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


def grab(hwnd: int, path: str) -> None:
    """PrintWindow(PW_CLIENTONLY) into a 32bpp DIB, written as a PNG via PIL."""
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    width, height = rect.right, rect.bottom
    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    header = BITMAPINFOHEADER()
    header.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    header.biWidth = width
    header.biHeight = -height  # top-down
    header.biPlanes = 1
    header.biBitCount = 32
    header.biCompression = 0  # BI_RGB
    bits = ctypes.c_void_p()
    bmp = gdi32.CreateDIBSection(mem, ctypes.byref(header), 0, ctypes.byref(bits), None, 0)
    old = gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT)
    buf = ctypes.string_at(bits, width * height * 4)
    gdi32.SelectObject(mem, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)

    from PIL import Image
    img = Image.frombuffer("RGBA", (width, height), buf, "raw", "BGRA", 0, 1)
    img.convert("RGB").save(path)


def list_right_edge(path: str) -> int:
    """Right edge of the white song list - i.e. where the left column ends.

    Column-wise, not row-wise: a single row can land on text or on a checkbox,
    while over a vertical band the list's own columns are nearly all white and
    the gap to the right panel is the window's flat background.
    """
    from PIL import Image
    img = Image.open(path).convert("RGB")
    width, height = img.size
    px = img.load()
    y0, y1 = height // 8, height // 2
    edge = 0
    for x in range(8, width):
        white = 0
        for y in range(y0, y1):
            if min(px[x, y]) >= 245:
                white += 1
        if white * 10 >= (y1 - y0) * 8:  # >= 80% white
            edge = x
    return edge


def main() -> int:
    import subprocess

    args = [a for a in sys.argv[1:] if a != "--no-minimize"]
    control = "--no-minimize" in sys.argv
    if len(args) < 2:
        print(__doc__.strip())
        return 2
    exe, outdir = args[0], args[1]
    extra = args[2:]
    os.makedirs(outdir, exist_ok=True)
    # Big enough that the window is up and settled before anything is posted.
    proc = subprocess.Popen([exe, "--screenshot-time", "30"] + extra, cwd=os.getcwd())
    try:
        hwnd = find_window(proc.pid)
        if not hwnd:
            print("FAIL: our chartdl instance never showed a window (exit code",
                proc.poll(), ")")
            return 1
        print("[win] pid", proc.pid, "hwnd", hwnd)
        time.sleep(2.0)

        before = os.path.join(outdir, "min_before.png")
        grab(hwnd, before)
        edge_before = list_right_edge(before)
        print("[before] left column ends at x =", edge_before)

        minimized = True
        if control:
            # Control run: nothing is minimized, the same two grabs are taken
            # apart in the same amount of time. It has to come out
            # pixel-identical, otherwise the diff below is measuring grab noise
            # instead of the layout.
            print("[post] control run: nothing posted, only the same wait")
            time.sleep(3.5)
        else:
            user32.PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0)
            time.sleep(1.5)
            minimized = user32.IsIconic(hwnd) != 0
            print("[post] minimized:", minimized)
            user32.PostMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0)
            time.sleep(2.0)
            print("[post] restored:", user32.IsIconic(hwnd) == 0)

        after = os.path.join(outdir, "min_after.png")
        grab(hwnd, after)
        edge_after = list_right_edge(after)
        print("[after ] left column ends at x =", edge_after)

        from PIL import Image, ImageChops
        a = Image.open(before).convert("RGB")
        b = Image.open(after).convert("RGB")
        changed = 0
        if a.size != b.size:
            print(f"note: window resized {a.size} -> {b.size}")
        else:
            diff = ImageChops.difference(a, b)
            changed = sum(1 for p in diff.getdata() if p != (0, 0, 0))
        print(f"[diff] {changed} px changed")
        if edge_before == edge_after and changed == 0:
            print("OK: the split survived the cycle untouched")
            return 0
        if not control and not minimized:
            print("FAIL: the window never minimized, so nothing was exercised")
            return 1
        print(f"FAIL: split moved {edge_before} -> {edge_after}, {changed} px changed")
        return 1
    finally:
        proc.terminate()


if __name__ == "__main__":
    sys.exit(main())
