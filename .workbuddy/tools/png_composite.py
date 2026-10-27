#!/usr/bin/env python3
"""Composite a PNG over a solid colour and write the result.

White-on-transparent sprites (pjsk's txt_*_wh wordmarks) are invisible in an image
viewer; flattening them onto a dark backdrop makes them readable.

Usage: python .workbuddy/tools/png_composite.py <in.png> <out.png> [r,g,b]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngcrop import read_png, write_png  # noqa: E402


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    bg = [0x22, 0x20, 0x33]
    if len(sys.argv) > 3:
        bg = [int(v) for v in sys.argv[3].split(',')]
    w, h, px = read_png(sys.argv[1])
    out = bytearray(w * h * 4)
    for i in range(w * h):
        r, g, b, a = px[i * 4:i * 4 + 4]
        ia = 255 - a
        out[i * 4 + 0] = (r * a + bg[0] * ia) // 255
        out[i * 4 + 1] = (g * a + bg[1] * ia) // 255
        out[i * 4 + 2] = (b * a + bg[2] * ia) // 255
        out[i * 4 + 3] = 255
    write_png(sys.argv[2], w, h, out)
    print(f'wrote {sys.argv[2]} {w}x{h}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
