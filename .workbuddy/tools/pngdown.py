"""Nearest-neighbour downscale of a PNG (RGBA8, non-interlaced) to a max width.

Companion to pngcrop.py for the case where the whole image matters (checking
what a big backdrop/asset actually contains) rather than one detail.

    python .workbuddy/tools/pngdown.py in.png out.png [MAXWIDTH]
"""

import struct
import sys
import zlib

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from pngcrop import read_png  # noqa: E402  (same folder, tiny helper)


def write_png(path, width, height, rows):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    body = b"".join(b"\x00" + bytes(row) for row in rows)
    blob = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(body)) + chunk(b"IEND", b""))
    with open(path, "wb") as handle:
        handle.write(blob)


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    max_width = int(sys.argv[3]) if len(sys.argv) > 3 else 512

    width, height, pixels = read_png(src)
    step = max(1, width // max_width)
    out_w, out_h = max(1, width // step), max(1, height // step)
    rows = []
    for y in range(out_h):
        base = y * step * width
        row = bytearray()
        for x in range(out_w):
            start = (base + x * step) * 4
            row += pixels[start:start + 4]
        rows.append(row)
    write_png(dst, out_w, out_h, rows)
    print(f"{src} {width}x{height} -> {dst} {out_w}x{out_h}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
