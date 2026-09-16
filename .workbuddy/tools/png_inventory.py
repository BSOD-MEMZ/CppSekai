#!/usr/bin/env python3
"""PNG inventory: decoded size (the thing that costs RAM/VRAM) vs file size.

Textures are uploaded at their decoded dimensions, so a 2048x2048 plate costs
16 MB of texture memory no matter how well the PNG compresses. This walks a
directory and prints the biggest *decoded* PNGs first - that is the list to look
at when the goal is memory rather than disk.

Usage:
  python png_inventory.py <dir> [topN]
"""
import os
import struct
import sys

sys.stdout.reconfigure(encoding='utf-8')


def png_dim(path):
    try:
        with open(path, 'rb') as f:
            head = f.read(33)
    except OSError:
        return None
    if head[:8] != b'\x89PNG\r\n\x1a\n' or head[12:16] != b'IHDR':
        return None
    w, h = struct.unpack('>II', head[16:24])
    return w, h, head[24], head[25]


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'assets'
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 45
    rows = []
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.lower().endswith('.png'):
                continue
            p = os.path.join(dirpath, fn)
            d = png_dim(p)
            if not d:
                continue
            w, h, bit, ct = d
            rows.append((w * h * 4, os.path.getsize(p), w, h, bit, ct, p))
    rows.sort(reverse=True)
    tot_mem = sum(r[0] for r in rows)
    tot_disk = sum(r[1] for r in rows)
    print('PNG count=%d  decoded=%.1f MB  disk=%.1f MB' % (len(rows), tot_mem / 1e6, tot_disk / 1e6))
    print('%-9s %-9s %-13s %-4s %-3s %s' % ('decMB', 'diskKB', 'dims', 'bit', 'ct', 'path'))
    for m, sz, w, h, bit, ct, p in rows[:top]:
        print('%-9.2f %-9.1f %-13s %-4d %-3d %s'
              % (m / 1e6, sz / 1024, '%dx%d' % (w, h), bit, ct, p.replace(os.sep, '/')))


if __name__ == '__main__':
    main()
