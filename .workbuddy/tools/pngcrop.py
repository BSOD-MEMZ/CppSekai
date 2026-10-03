#!/usr/bin/env python3
"""Crop + nearest-neighbour zoom a PNG (8-bit gray/RGB/RGBA, non-interlaced).

The toolchain has no Pillow / ImageMagick, but the headless screenshot checks
(``--screenshot``) produce 1920x1080 RGBA PNGs and a magnified crop is often
the only way to eyeball small UI details.

    python .workbuddy/tools/pngcrop.py in.png out.png X Y W H [ZOOM]
"""
import struct
import sys
import zlib


def read_png(path):
    raw = open(path, "rb").read()
    assert raw[:8] == b"\x89PNG\r\n\x1a\n", "not a png"
    pos = 8
    idat = b""
    width = height = 0
    while pos < len(raw):
        (length,) = struct.unpack(">I", raw[pos : pos + 4])
        ctype = raw[pos + 4 : pos + 8]
        data = raw[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", data)
            # 0 = gray, 2 = RGB, 6 = RGBA. Chrome's --screenshot writes plain RGB,
            # so accepting it here is what lets a headless browser shot be cropped
            # and zoomed like the game's RGBA screenshots.
            assert depth == 8 and color in (0, 2, 6) and interlace == 0, "only 8-bit gray/RGB/RGBA"
        elif ctype == b"IDAT":
            idat += data
        elif ctype == b"IEND":
            break
    buf = zlib.decompress(idat)
    bpp = 1 if color == 0 else (3 if color == 2 else 4)
    stride = width * bpp
    out = bytearray(height * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        ft = buf[p]
        p += 1
        line = bytearray(buf[p : p + stride])
        p += stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride : (y + 1) * stride] = line
        prev = line
    if bpp == 4:
        return width, height, out
    # Expand to RGBA once, so main() can keep slicing 4 bytes per pixel.
    rgba = bytearray(width * height * 4)
    for i in range(width * height):
        if bpp == 1:
            g = out[i]
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = g
        else:
            rgba[i * 4 : i * 4 + 3] = out[i * 3 : i * 3 + 3]
        rgba[i * 4 + 3] = 255
    return width, height, rgba


def write_png(path, width, height, pixels):
    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += pixels[y * stride : (y + 1) * stride]

    def chunk(ctype, data):
        return (
            struct.pack(">I", len(data))
            + ctype
            + data
            + struct.pack(">I", zlib.crc32(ctype + data) & 0xFFFFFFFF)
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    if len(sys.argv) < 7:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    x, y, w, h = (int(v) for v in sys.argv[3:7])
    zoom = int(sys.argv[7]) if len(sys.argv) > 7 else 1
    iw, ih, px = read_png(src)
    w = min(w, iw - x)
    h = min(h, ih - y)
    sstride = iw * 4
    out = bytearray(w * zoom * h * zoom * 4)
    ostride = w * zoom * 4
    for oy in range(h * zoom):
        sy = y + oy // zoom
        row = oy * ostride
        for ox in range(w * zoom):
            sx = x + ox // zoom
            s = sy * sstride + sx * 4
            out[row + ox * 4 : row + ox * 4 + 4] = px[s : s + 4]
    write_png(dst, w * zoom, h * zoom, out)
    print("wrote %s (%dx%d)" % (dst, w * zoom, h * zoom))
    return 0


if __name__ == "__main__":
    sys.exit(main())
