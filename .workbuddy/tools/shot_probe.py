#!/usr/bin/env python3
"""Pixel probe for aligning CppSekai screens against a reference capture.

Alignment work on the HUD / result screen is all about "where exactly is that
box and what colour is it", which eyeballing a downscaled PNG cannot answer.
This dumps the image to raw RGB (via ImageMagick) and prints, for a region:

  * an ASCII colour-class map (one char per NxN block) so the geometry is
    visible in text form,
  * the bounding box of "ink" (pixels far from the region's median colour),
  * the region's most common exact colours.

Usage (Git Bash / cmd):

  python shot_probe.py <image> <x0> <y0> <x1> <y1> [block]
  python shot_probe.py mine.png 400 0 1400 200

Decoding uses ImageMagick (`magick`) when it is on PATH and Pillow otherwise -
the work machines have neither installed the same way, and a missing `magick`
used to make this tool useless right when a screenshot needed checking.

The ASCII map legend:  .  background   #  bright/white   -  mid   %  very bright
                       r  red/pink     g  green         b  blue     ?  other
"""
import os
import shutil
import subprocess
import sys
import statistics
import tempfile
from collections import Counter


def magick():
    for name in ('magick', 'magick.exe'):
        found = shutil.which(name)
        if found:
            return found
    return None


def load(path):
    tool = magick()
    if tool:
        raw = os.path.join(tempfile.gettempdir(), 'shot_probe.raw')
        size = subprocess.run([tool, 'identify', '-format', '%w %h', path],
                              capture_output=True, text=True, check=True).stdout.split()
        w, h = int(size[0]), int(size[1])
        subprocess.run([tool, path, '-depth', '8', 'rgb:' + raw], check=True)
        with open(raw, 'rb') as fh:
            return fh.read(), w, h
    from PIL import Image
    with Image.open(path) as img:
        img = img.convert('RGB')
        w, h = img.size
        return img.tobytes(), w, h


def luminance(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def classify(c):
    r, g, b = c
    lum = luminance(c)
    if lum < 70:
        return '.'
    if lum > 210:
        return '#'
    if r > 150 and g < r * 0.75 and b > 60:
        return 'r'
    if g > 140 and r < g * 0.8:
        return 'g'
    if b > 130 and r < b * 0.9:
        return 'b'
    if lum > 150:
        return '-'
    return '?'


def main():
    path = sys.argv[1]
    x0, y0, x1, y1 = (int(v) for v in sys.argv[2:6])
    block = int(sys.argv[6]) if len(sys.argv) > 6 else 8
    data, w, h = load(path)

    def px(x, y):
        i = (y * w + x) * 3
        return (data[i], data[i + 1], data[i + 2])

    region = [(x, y) for y in range(y0, min(y1, h)) for x in range(x0, min(x1, w))]
    med = statistics.median(luminance(px(x, y)) for x, y in region)
    print('%s region x %d..%d y %d..%d  (median lum %.0f)' % (path, x0, x1, y0, y1, med))

    ink = [(x, y) for x, y in region if abs(luminance(px(x, y)) - med) > 45]
    if ink:
        xs = [p[0] for p in ink]
        ys = [p[1] for p in ink]
        print('  ink bbox: x %d..%d (w=%d)  y %d..%d (h=%d)  px=%d'
              % (min(xs), max(xs), max(xs) - min(xs) + 1,
                 min(ys), max(ys), max(ys) - min(ys) + 1, len(ink)))
    colors = Counter(px(x, y) for x, y in region)
    print('  top colours:', colors.most_common(5))

    print('  map (%dx%d blocks):' % (block, block))
    header = '      ' + ''.join(str((x // 100) % 10) for x in range(x0, min(x1, w), block))
    print(header)
    for y in range(y0, min(y1, h), block):
        row = ''.join(classify(px(x, y)) for x in range(x0, min(x1, w), block))
        print('  %4d %s' % (y, row))


if __name__ == '__main__':
    main()
