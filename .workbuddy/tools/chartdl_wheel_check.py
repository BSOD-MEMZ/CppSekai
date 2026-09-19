"""Live check: does a real WM_MOUSEWHEEL scroll the chartdl 下载内容 panel?

`--scroll-detail` exercises the scroll *arithmetic*, but not the delivery path -
the wheel has to reach the subclass installed on the 下载内容 group box and on
the body container inside it (see detailGroupProc in downloader/chartdl.cpp).

This posts a genuine WM_MOUSEWHEEL at the group box of a running chartdl and
grabs the window before/after with PrintWindow, so a broken subclass fails
loudly instead of silently doing nothing. The two grabs are also diffed, which
is the real assertion: an unchanged image means the wheel did not land.

Usage:  python chartdl_wheel_check.py <chartdl.exe> <outdir> [--select N] [--dpi N]
"""

import ctypes
import os
import sys
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

WM_MOUSEWHEEL = 0x020A
WHEEL_DELTA = 120

# Pillow from the managed venv, same as the other tools in this folder.
sys.path.insert(0, os.path.expanduser("~/.workbuddy/binaries/python/envs/default/Lib/site-packages"))


def find_window(sub: str, timeout: float = 12.0) -> int:
    buf = ctypes.create_unicode_buffer(512)
    deadline = time.time() + timeout
    while time.time() < deadline:
        hit: list[int] = []

        def cb(h, _):
            if user32.IsWindowVisible(h):
                n = user32.GetWindowTextW(h, buf, 512)
                if n and sub in buf.value:
                    hit.append(h)
            return True

        user32.EnumWindows(
            ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
        if hit:
            return hit[0]
        time.sleep(0.25)
    return 0


def find_child(parent: int, cls: str, text: str) -> int:
    out: list[int] = []
    cls_buf = ctypes.create_unicode_buffer(256)
    txt_buf = ctypes.create_unicode_buffer(512)

    def cb(h, _):
        user32.GetClassNameW(h, cls_buf, 256)
        user32.GetWindowTextW(h, txt_buf, 512)
        if cls_buf.value == cls and txt_buf.value == text:
            out.append(h)
            return False
        return True

    user32.EnumChildWindows(
        parent, ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
    return out[0] if out else 0


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]


def grab(hwnd: int, path: str):
    """PrintWindow the whole window into a PNG via Pillow."""
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w = rect.right - rect.left
    h = rect.bottom - rect.top
    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 2)  # PW_RENDERFULLCONTENT

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth = w
    bi.biHeight = -h
    bi.biPlanes = 1
    bi.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)

    from PIL import Image
    src = buf.raw
    img = Image.frombuffer("RGBA", (w, h), bytes(src), "raw", "BGRA", 0, 1)
    img.convert("RGB").save(path)
    return w, h


def wheel(hwnd: int, notches: int) -> None:
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    x = (rect.left + rect.right) // 2
    y = (rect.top + rect.bottom) // 2
    lparam = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
    wparam = (((WHEEL_DELTA * notches) & 0xFFFF) << 16)
    user32.PostMessageW(hwnd, WM_MOUSEWHEEL, wparam, lparam)


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    exe, outdir = sys.argv[1], sys.argv[2]
    select, dpi = 543, 192
    args = sys.argv[3:]
    for i, a in enumerate(args):
        if a == "--select":
            select = int(args[i + 1])
        elif a == "--dpi":
            dpi = int(args[i + 1])
    os.makedirs(outdir, exist_ok=True)
    proc = subprocess.Popen([exe, "--dpi", str(dpi), "--select", str(select)],
                            cwd=os.path.dirname(exe),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        hwnd = find_window("CppSekai 谱面下载器")
        if not hwnd:
            print("FAIL: main window never appeared")
            return 1
        time.sleep(2.5)
        group = find_child(hwnd, "Button", "下载内容")
        if not group:
            print("FAIL: 下载内容 group box not found")
            return 1
        rect = wintypes.RECT()
        user32.GetWindowRect(group, ctypes.byref(rect))
        print(f"[win] group box {rect.right - rect.left}x{rect.bottom - rect.top} "
              f"at {rect.left},{rect.top}")

        user32.SetForegroundWindow(hwnd)
        time.sleep(0.4)
        before = os.path.join(outdir, "before.png")
        grab(hwnd, before)
        print("[grab] before ->", before)

        print("[send] 5 notches down to the group box")
        for _ in range(5):
            wheel(group, -1)
            time.sleep(0.15)
        time.sleep(0.8)

        after = os.path.join(outdir, "after.png")
        grab(hwnd, after)
        print("[grab] after  ->", after)

        from PIL import Image, ImageChops
        a = Image.open(before).convert("RGB")
        b = Image.open(after).convert("RGB")
        if a.size != b.size:
            print(f"note: window resized {a.size} -> {b.size}")
        else:
            diff = ImageChops.difference(a, b)
            changed = sum(1 for p in diff.getdata() if p != (0, 0, 0))
            print(f"[diff] {changed} px changed ({100.0 * changed / (a.size[0] * a.size[1]):.2f}%)")
            if changed == 0:
                print("FAIL: wheel changed nothing - the subclass never scrolled")
                return 1
        return 0
    finally:
        proc.terminate()


if __name__ == "__main__":
    import subprocess
    sys.exit(main())
