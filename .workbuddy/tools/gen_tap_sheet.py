#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 CppSekai 的点击涟漪图集（自绘，不依赖任何官方美术）。

参数来自对 pjsk 日服 7.1.0 粒子的实测：
  - 主涟漪 `PT_01`：4x4 图集（16 格，用 14 帧），生命周期 0.5s，
    frameOverTime 线性 0->1（等效 32fps），startSpeed 0（原地扩张）。
  - 细环层：4x4，16 帧，生命周期 0.45s。
  - 柔光层：单张 256x256 径向光斑，0.5s 淡出。

输出：
  assets/fx/tap_sheet.png    1024x1024，4x4，256/帧，前 14 帧有效
  assets/fx/tap_sheet_1.png  1024x1024，4x4，16/帧
  assets/fx/tap_flare.png     256x256

用法： python gen_tap_sheet.py [输出目录]
"""
import os
import sys
import math

from PIL import Image, ImageDraw, ImageFilter

TILE = 256
TILES = 4
SS = 4  # 超采样倍数


def ring_layer(radius, thickness, color, alpha):
    """在 TILE 尺寸的 RGBA 上画一圈抗锯齿环（超采样后缩）"""
    big = Image.new("RGBA", (TILE * SS, TILE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    cx = cy = TILE * SS / 2.0
    r = radius * SS
    w = max(1, int(thickness * SS))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=color + (alpha,), width=w)
    return big.resize((TILE, TILE), Image.LANCZOS)


def disc(radius, color, alpha):
    big = Image.new("RGBA", (TILE * SS, TILE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    cx = cy = TILE * SS / 2.0
    r = radius * SS
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color + (alpha,))
    return big.resize((TILE, TILE), Image.LANCZOS)


def over(dst, src):
    return Image.alpha_composite(dst, src)


def ease_out_cubic(t):
    return 1.0 - (1.0 - t) ** 3


def ease_out_quad(t):
    return 1.0 - (1.0 - t) ** 2


def frame_main(i, n):
    """主涟漪：中心点 -> 双环扩张（外白内青）-> 变亮 -> 淡出"""
    t = i / float(n - 1)
    img = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))

    # 起始两帧有一个小实心点，之后被环取代
    if t < 0.10:
        k = 1.0 - t / 0.10
        img = over(img, disc(7 + 2 * (1 - k), (245, 255, 255), int(230 * k)))

    grow = ease_out_quad(t)
    # 外环：白，扩张最快，后期淡出
    outer_r = 44 + grow * 82
    outer_a = int(255 * max(0.0, 1.0 - max(0.0, (t - 0.50)) / 0.50) ** 1.5)
    if outer_a > 0:
        img = over(img, ring_layer(outer_r, 8 - 2.5 * t, (255, 255, 255), int(outer_a * 0.92)))

    # 内环：青绿，比外环慢一点、更粗、更早出现
    inner_r = 24 + ease_out_cubic(min(1.0, t * 1.10)) * 72
    inner_a = int(240 * max(0.0, 1.0 - max(0.0, (t - 0.42)) / 0.58) ** 1.15)
    if inner_a > 0:
        img = over(img, ring_layer(inner_r, 9 - 3.0 * t, (0, 214, 205), inner_a))

    # 极淡的内圈发热感（只在最亮那几帧，别做成实心）
    glow_a = int(16 * max(0.0, 1.0 - abs(t - 0.35) / 0.35))
    if glow_a > 0:
        img = over(img, ring_layer(inner_r - 7, 16, (150, 250, 240), glow_a))

    return img.filter(ImageFilter.GaussianBlur(0.4))


def frame_thin(i, n):
    """细环：单根，匀速扩张 + 线性淡出"""
    t = i / float(n - 1)
    r = 42 + ease_out_quad(t) * 82
    a = int(230 * max(0.0, 1.0 - t) ** 0.85)
    return ring_layer(r, 6 - 2 * t, (235, 250, 255), a)


def build_sheet(path, frames, used=None):
    sheet = Image.new("RGBA", (TILE * TILES, TILE * TILES), (0, 0, 0, 0))
    for idx, im in enumerate(frames):
        col, row = idx % TILES, idx // TILES
        sheet.paste(im, (col * TILE, row * TILE))
    sheet.save(path)
    print("写出 %-34s %dx%d  帧数 %d" % (path, sheet.width, sheet.height, len(frames)))


def build_flare(path):
    im = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
    px = im.load()
    cx = cy = TILE / 2.0
    for y in range(TILE):
        for x in range(TILE):
            dx = (x + 0.5 - cx) / cx
            dy = (y + 0.5 - cy) / cy
            r = math.sqrt(dx * dx + dy * dy)
            if r >= 1.0:
                continue
            a = (1.0 - r) ** 2.4
            px[x, y] = (255, 255, 255, int(255 * a))
    im.save(path)
    print("写出 %-34s %dx%d" % (path, im.width, im.height))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "assets", "fx")
    os.makedirs(out, exist_ok=True)

    # 主涟漪：前 14 格有效（与实测一致），后 2 格留空
    main_frames = [frame_main(i, 14) for i in range(14)]
    main_frames += [Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0)) for _ in range(2)]
    build_sheet(os.path.join(out, "tap_sheet.png"), main_frames)

    build_sheet(os.path.join(out, "tap_sheet_1.png"), [frame_thin(i, 16) for i in range(16)])
    build_flare(os.path.join(out, "tap_flare.png"))


if __name__ == "__main__":
    main()
