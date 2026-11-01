"""Draw a 1920x1080 overlay comparing pjsk's music-select cell phone with ours.

Reference numbers come from `ScreenLayerFreeLiveCellPhone` (prefab PathID 704),
dumped by D:\\Tools\\rea\\work\\jp\\ms_geom.py -> ms_phone.json:
    CellPhone  size 754.3 x 1416.7, anchoredPosition (562, -202) relative to the
               screen centre, m_LocalEulerAnglesHint.z = 3 (Unity CCW), scale 1
Ours come from game/SongSelect.cpp drawSongSelect() at 1080p / scale 1.

Usage: python select_phone_overlay.py <out.png>
"""
import math
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 1920, 1080
CX, CY = W / 2, H / 2

FONT = 'C:/Windows/Fonts/msyh.ttc'


def font(size):
    try:
        return ImageFont.truetype(FONT, size)
    except OSError:
        return ImageFont.load_default()


def corners(cx, cy, w, h, deg):
    """Corners of a rect rotated by `deg` in a y-down space (positive = clockwise)."""
    rad = math.radians(deg)
    c, s = math.cos(rad), math.sin(rad)
    out = []
    for dx, dy in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
        out.append((cx + dx * c - dy * s, cy + dx * s + dy * c))
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'select_phone_overlay.png'

    # --- pjsk --------------------------------------------------------------
    # prefab: centre (562, -202) from the screen centre; Unity +y is up, so the
    # y-down screen centre y is CY + 202.
    ref = dict(cx=CX + 562.0, cy=CY + 202.0, w=754.3, h=1416.7, deg=-3.0)
    # --- ours --------------------------------------------------------------
    # phoneH = 1080 - 24, phoneW = phoneH * 1034/1942, right edge at W - 30,
    # centre y = (H - phoneH)/2 + 78, tilt +5 deg clockwise.
    our_h = 1080 - 24
    our_w = our_h * (1034.0 / 1942.0)
    our_top = (1080 - our_h) / 2 + 78  # SongSelect.cpp: phoneY is the top edge
    ours = dict(cx=W - 30 - our_w / 2, cy=our_top + our_h / 2, w=our_w, h=our_h, deg=5.0)

    img = Image.new('RGB', (W, H), (18, 20, 30))
    d = ImageDraw.Draw(img, 'RGBA')

    # list blocks (left column): reference 212..1124, ours 150..600
    d.rectangle([212, 0, 1124, H], fill=(46, 52, 74, 70), outline=(120, 140, 200, 120), width=2)
    d.rectangle([150, 0, 600, H], fill=(70, 46, 60, 60), outline=(200, 120, 150, 120), width=2)
    d.text((228, 24), 'pjsk list  912x1080 @x212', font=font(26), fill=(150, 175, 235, 255))
    d.text((168, 60), 'ours list  x150..600', font=font(24), fill=(225, 155, 180, 255))

    for spec, col, label in (
        (ref, (255, 196, 90, 255), 'pjsk CellPhone  754.3 x 1416.7  rot -3deg'),
        (ours, (120, 220, 255, 255), 'ours  562 x 1056  rot +5deg'),
    ):
        pts = corners(spec['cx'], spec['cy'], spec['w'], spec['h'], spec['deg'])
        d.polygon(pts, fill=col[:3] + (34,))
        d.line(pts + [pts[0]], fill=col, width=4)
        # centre cross
        x, y = spec['cx'], spec['cy']
        d.line([x - 14, y, x + 14, y], fill=col, width=2)
        d.line([x, y - 14, x, y + 14], fill=col, width=2)

    # jackards: reference 376 square, 229 below the phone top, centred
    jc = (ref['cx'] - 2.6, ref['cy'] - ref['h'] / 2 + 229 + 188)
    d.rectangle([jc[0] - 188, jc[1] - 188, jc[0] + 188, jc[1] + 188], outline=(255, 196, 90, 140), width=2)
    d.text((jc[0] - 150, jc[1] - 12), 'jacket 376', font=font(24), fill=(255, 210, 140, 220))

    # difficulty row: 5 x 96, total 576, at the phone centre -20.5 (y-down +20.5)
    dc = (ref['cx'], ref['cy'] + 20.5)
    for i in range(5):
        x = dc[0] - 576 / 2 + i * 115.2
        d.ellipse([x + 9.6, dc[1] - 48, x + 105.6, dc[1] + 48], outline=(255, 196, 90, 130), width=2)

    # text / annotations
    d.text((40, H - 150), 'y=0 reference: Unity CanvasScaler = 1920x1080 (ScaleWithScreenSize, match width)',
           font=font(26), fill=(200, 210, 235, 255))
    d.text((40, H - 110), 'pjsk phone: 33.7 from the top, 20.9 from the right, 370.4 cut off below the screen',
           font=font(26), fill=(255, 196, 90, 255))
    d.text((40, H - 72), 'ours: 90 from the top, 30 from the right, 66 cut off below - and 25%% narrower',
           font=font(26), fill=(120, 220, 255, 255))

    d.rectangle([0, 0, W - 1, H - 1], outline=(90, 100, 130, 255), width=3)
    img.save(out)
    print('wrote', out)

    for name, spec in (('pjsk', ref), ('ours', ours)):
        l, r = spec['cx'] - spec['w'] / 2, spec['cx'] + spec['w'] / 2
        t, b = spec['cy'] - spec['h'] / 2, spec['cy'] + spec['h'] / 2
        print('%-5s w=%.1f h=%.1f  box x %.1f..%.1f  y %.1f..%.1f  visW=%.0f%% visH=%.0f%%'
              % (name, spec['w'], spec['h'], l, r, t, b,
                 100.0 * (min(r, W) - max(l, 0)) / spec['w'],
                 100.0 * (min(b, H) - max(t, 0)) / spec['h']))


if __name__ == '__main__':
    main()
