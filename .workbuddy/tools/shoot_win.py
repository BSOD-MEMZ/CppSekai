"""Grab a running window's client area to a PNG.

The game's own `--screenshot` writes the framebuffer at exit; this one works on
a window that is still running, which is what a scripted UI check needs (click
something, look at the result, click again).

Usage:  python shoot_win.py "<window title substring>" out.png [--mirror]

`--mirror` flips the grab left-right **and is the default this game needs** —
verified 2026-09-18 against a `--screenshot` dump of the same UI: the raw
PrintWindow rows of this SDL/OpenGL window come back order-reversed, and the
only transform that lands on the reference image is a horizontal mirror.
Pass `--raw` to get the untransformed grab (used once, to prove that).

A single-colour (blank) grab means PrintWindow got nothing at all — that
happens while the window is occluded or has not painted yet. Retry rather than
assuming the UI is empty; a blank grab is NOT the same as a UI with no dialog.
"""

import ctypes
import sys
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def find(sub: str):
    found = []

    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if not n:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        if sub in buf.value:
            found.append((hwnd, buf.value))
            return False
        return True

    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return found[0] if found else (None, None)


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    hwnd, title = find(sys.argv[1])
    if not hwnd:
        print("window not found:", sys.argv[1])
        return 1
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    w, h = rect.right, rect.bottom
    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    # PW_CLIENTONLY (1) | PW_RENDERFULLCONTENT (2) - the second is required for
    # GL-composited windows, otherwise the client area comes back blank.
    user32.PrintWindow(hwnd, mem, 3)
    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth = w
    bi.biHeight = -h
    bi.biPlanes = 1
    bi.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
    mirror = "--mirror" in sys.argv or "--raw" not in sys.argv
    try:
        from PIL import Image  # type: ignore
        img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1)
        if mirror:
            img = img.transpose(Image.FLIP_LEFT_RIGHT)
        img.convert("RGB").save(sys.argv[2])
    except ImportError:
        # No Pillow: write an uncompressed BMP, which Read can open too.
        import struct
        row = w * 4
        with open(sys.argv[2], "wb") as f:
            f.write(struct.pack("<2sIHHI", b"BM", 54 + row * h, 0, 0, 54))
            f.write(struct.pack("<IiiHHIIiiII", 40, w, h, 1, 32, 0, row * h, 0, 0, 0, 0))
            f.write(buf.raw)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    print(f"saved {sys.argv[2]} ({w}x{h}) title={title!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
