"""Coarse ASCII preview of a PNG, for eyeballing layout without an image viewer.

Each cell is one block of pixels: the character says what the block looks like
(' ' dark, '.' mid, '#' light, 'P' pink, 'M' mint, 'R' red, 'W' near-white),
so a card's shape, its text rows and its coloured buttons are all visible in a
terminal. Diagnostic only - never an assertion.
"""
import sys
import pathlib

sys.path.insert(0, str(pathlib.Path.home() / ".workbuddy/binaries/python/envs/default/Lib/site-packages"))
from PIL import Image

path = sys.argv[1]
cols = int(sys.argv[2]) if len(sys.argv) > 2 else 64
rows = int(sys.argv[3]) if len(sys.argv) > 3 else 30

im = Image.open(path).convert("RGB")
W, H = im.size
px = im.load()
out = []
for ry in range(rows):
    line = []
    for rx in range(cols):
        x0, x1 = W * rx // cols, max(W * (rx + 1) // cols, W * rx // cols + 1)
        y0, y1 = H * ry // rows, max(H * (ry + 1) // rows, H * ry // rows + 1)
        r = g = b = n = 0
        for y in range(y0, y1, max(1, (y1 - y0) // 4)):
            for x in range(x0, x1, max(1, (x1 - x0) // 4)):
                pr, pg, pb = px[x, y]
                r += pr; g += pg; b += pb; n += 1
        r //= n; g //= n; b //= n
        if r > 200 and g > 200 and b > 200:
            ch = " "
        elif r > 250 and 60 < g < 130 and 110 < b < 190:
            ch = "P"          # note pink
        elif r < 180 and g > 200 and b > 180:
            ch = "M"          # mint
        elif r > 220 and g < 170 and b < 180:
            ch = "R"          # red
        elif r < 90 and g < 90 and b < 90:
            ch = "#"          # dark
        else:
            lum = (r + g + b) // 3
            ch = "." if lum < 150 else ("-" if lum < 205 else "+")
        line.append(ch)
    out.append("".join(line))
print(f"{path}  {W}x{H}  ({cols}x{rows} cells)")
for line in out:
    print(line)
