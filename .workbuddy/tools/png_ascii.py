#!/usr/bin/env python3
"""Print a coarse ASCII silhouette of a PNG's alpha channel.

Used to read white-on-transparent wordmark sprites (txt_liveclear_wh etc.) that are
invisible in an image viewer. Reuses the pure-python decoder in pngcrop.py.

Usage: python .workbuddy/tools/png_ascii.py <png> [columns]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngcrop import read_png  # noqa: E402


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 110
    w, h, px = read_png(sys.argv[1])
    # PNG rows are top-down in read_png's output; alpha is the 4th byte of RGBA.
    step_x = max(1, w // cols)
    step_y = max(1, step_x * 2)  # characters are twice as tall as they are wide
    for y in range(0, h, step_y):
        line = []
        for x in range(0, w, step_x):
            a = 0
            for dy in range(0, step_y, max(1, step_y // 3)):
                for dx in range(0, step_x, max(1, step_x // 3)):
                    yy, xx = y + dy, x + dx
                    if yy < h and xx < w:
                        a = max(a, px[(yy * w + xx) * 4 + 3])
            line.append('#' if a > 160 else ('+' if a > 60 else ('.' if a > 12 else ' ')))
        print(''.join(line).rstrip())
    print(f'--- {os.path.basename(sys.argv[1])} {w}x{h}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
