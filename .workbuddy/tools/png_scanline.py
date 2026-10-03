#!/usr/bin/env python3
"""Print a horizontal scanline of a PNG as RGB plus distance from a base colour.

The use case this exists for: **measuring a shadow / edge falloff instead of
guessing at it**. ImGui has no blur primitive, so ui::dropShadow fakes one with
concentric rounded rects - and "how wide and how dark" is very easy to get
wrong by eye (it was: five equal steps put six times the reference's alpha at
6px out and read as a grey smear).

Scan outward from a control's edge on the reference site's screenshot, then scan
the same edge in our own `--screenshot` output, and compare the two columns:

    python .workbuddy/tools/png_scanline.py shot.png <y> <x0> <x1> [step]

Both PNGs must be 8-bit gray/RGB/RGBA - pngcrop.py's reader does the decoding,
so a Chrome screenshot (plain RGB) works the same as a game one (RGBA).

Read the output as "how much darker than the far-field background", where the
first sampled pixel is taken as that background - so x0 must sit far enough from
the control that the shadow has already died out (start ~30px away and let the
printout find the edge). Pick x0 inside the fringe and every `d` comes out
relative to an already-shadowed pixel, which understates the whole curve.

    x=1076  rgb(205,205,217)  d=( -30, -30, -25)   <- 1px outside the control
    x=1077  rgb(131,233,220)  d=(-104,  -2, -22)   <- the control's own edge

To turn a `d` back into an alpha over a known background, solve for a in
`d = a * (shadowChannel - backgroundChannel)`. For the standard navy
#444466 over the #ebebf2 card that divisor is 167 on R/G, so d = -30 is
alpha ~= 0.18. The reference's own curve, for comparison:

    1px -31 (0.19)   3px -18 (0.11)   5px -9 (0.054)   7px -4 (0.024)
    2px -23 (0.14)   4px -13 (0.078)  6px -6 (0.036)   9px -2 (0.012)  11px 0
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pngcrop  # noqa: E402  - sibling module, needs the path above first


def scan(path, y, x0, x1, step=1):
    width, height, px = pngcrop.read_png(path)
    if not (0 <= y < height):
        raise SystemExit('y=%d is outside the image (height %d)' % (y, height))
    print('%s  %dx%d  scanline y=%d  x=%d..%d' % (path, width, height, y, x0, x1))
    base = None
    for x in range(max(0, x0), min(x1, width), step):
        i = (y * width + x) * 4
        r, g, b = px[i], px[i + 1], px[i + 2]
        if base is None:
            base = (r, g, b)
        print('  x=%4d  rgb(%3d,%3d,%3d)  d=(%4d,%4d,%4d)'
              % (x, r, g, b, r - base[0], g - base[1], b - base[2]))


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 1
    scan(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]),
         int(sys.argv[5]) if len(sys.argv) > 5 else 1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
