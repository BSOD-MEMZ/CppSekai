#!/usr/bin/env python3
"""Reconstruct the pjsk song-select FILTER dialog (`MusicSelectFilterDialog`, prefab 1732)
as a to-scale schematic PNG.

Every number below comes from the shipped prefab (AssetRipper) + libil2cpp:

  window      1168 x 1032  (DialogSize.Large = SIZE_DIC[2] = 1032, DialogSizeFitter .cctor 0x4CB0274)
  bg          bg_base_r30_wh + bg_base_shadow_r30; close btn 44x44 at (-15,-15) from the top-right
  content     vertical list: padding L64 R64 T3 B0, spacing 32
  title       1032 x 44 (32px text + 4px underline bg_base_wh)
  radio grid  cell 288x72, spacing (56,24), 3 fixed columns, padding 8/8
  check grid  cell 288x72, spacing (56,16), 3 fixed columns, padding 8/8
  radio       72x72 btn_circle_h56_wh + icon_radio_gn 36x36 + 256x50 label at +78
  checkbox    56x56 bg_base_r16_wh (+ shadow) + icon_check_pk 44x34 + 200x50 label at +78
  buttons     UIPartsCommonButton 336x96 x2, horizontal spacing 24, centred

usage: python gen_select_filter_dialog.py [out.png]
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else r'D:/Dev/CppSekai/docs/select-filter-dialog.png'

W, H = 1920, 1080
WIN_W, WIN_H = 1168, 1032
SCALE = 0.88

FONT = None
for cand in (r'C:\Windows\Fonts\meiryo.ttc', r'C:\Windows\Fonts\YuGothM.ttc',
             r'C:\Windows\Fonts\msgothic.ttc', r'C:\Windows\Fonts\msyh.ttc'):
    if os.path.exists(cand):
        FONT = cand
        break

BG = (22, 24, 31)
PANEL = (250, 250, 253)
PANEL_EDGE = (206, 206, 220)
BOX = (237, 237, 245)
INK = (68, 68, 102)
INK_SOFT = (120, 120, 150)
DIM = (152, 152, 174)
ACCENT = (119, 238, 221)

# dialog-space -> canvas mapping
OX = (W - WIN_W * SCALE) / 2.0
OY = (H - WIN_H * SCALE) / 2.0 + 12


def X(v):
    return OX + v * SCALE


def Y(v):
    return OY + v * SCALE


def S(v):
    return v * SCALE


def font(size):
    if FONT:
        try:
            return ImageFont.truetype(FONT, max(9, int(round(size * SCALE))))
        except Exception:
            pass
    return ImageFont.load_default()


def text(dr, xy, s, size, fill=INK, anchor='la'):
    dr.text((X(xy[0]), Y(xy[1])), s, font=font(size), fill=fill, anchor=anchor)


def rounded(dr, box, r, fill=None, outline=None, width=1):
    dr.rounded_rectangle((X(box[0]), Y(box[1]), X(box[2]), Y(box[3])),
                         radius=S(r), fill=fill, outline=outline,
                         width=max(1, int(round(width * SCALE))))


CX0 = (WIN_W - 1032) / 2.0
CX1 = CX0 + 1032


def section(dr, y, title, key):
    text(dr, (CX0 + 6, y + 2), title, 30, INK)
    text(dr, (CX1 - 4, y + 10), key, 15, DIM, anchor='ra')
    dr.rectangle((X(CX0), Y(y + 40), X(CX1), Y(y + 44)), fill=(255, 255, 255))
    return y + 44


def radio_row(dr, x, y, label, sub, on=False):
    dr.ellipse((X(x), Y(y), X(x + 72), Y(y + 72)), fill=(255, 255, 255))
    if on:
        dr.ellipse((X(x + 16), Y(y + 16), X(x + 56), Y(y + 56)), fill=(96, 228, 210))
    else:
        dr.ellipse((X(x + 16), Y(y + 16), X(x + 56), Y(y + 56)),
                   outline=(199, 199, 214), width=max(1, int(round(2 * SCALE))))
    text(dr, (x + 78, y + 18), label, 26, INK)
    text(dr, (x + 78, y + 50), sub, 14, DIM)


def checkbox_row(dr, x, y, label, sub, on=False):
    dr.rounded_rectangle((X(x + 8), Y(y + 8), X(x + 64), Y(y + 64)), radius=S(16),
                         fill=(255, 255, 255), outline=(212, 212, 226))
    if on:
        dr.line((X(x + 20), Y(y + 38), X(x + 33), Y(y + 50)), fill=(96, 228, 210),
                width=max(2, int(round(5 * SCALE))))
        dr.line((X(x + 33), Y(y + 50), X(x + 53), Y(y + 25)), fill=(96, 228, 210),
                width=max(2, int(round(5 * SCALE))))
    text(dr, (x + 78, y + 18), label, 26, INK)
    text(dr, (x + 78, y + 50), sub, 14, DIM)


def main():
    img = Image.new('RGB', (W, H), BG)
    dr = ImageDraw.Draw(img)

    # window + shadow
    rounded(dr, (-8, -6, WIN_W + 8, WIN_H + 10), 34, fill=(14, 15, 20))
    rounded(dr, (0, 0, WIN_W, WIN_H), 30, fill=PANEL, outline=PANEL_EDGE)

    # close button 44x44 at (-15,-15) from the top-right
    cbx, cby = WIN_W - 15 - 44, 15
    dr.ellipse((X(cbx), Y(cby), X(cbx + 44), Y(cby + 44)), outline=(178, 178, 196),
               width=max(1, int(round(2 * SCALE))))
    dr.line((X(cbx + 13), Y(cby + 13), X(cbx + 31), Y(cby + 31)), fill=(140, 140, 160), width=2)
    dr.line((X(cbx + 31), Y(cby + 13), X(cbx + 13), Y(cby + 31)), fill=(140, 140, 160), width=2)

    # content column
    rounded(dr, (CX0, 92, CX1, 884), 20, fill=BOX)

    y = 98
    # ---------------- クリア状況 (sibling order; group index annotated)
    y = section(dr, y, 'クリア状況', 'WORD_FILTER_TITLE_CLEAR')
    y += 30
    clears = [('すべて', 'idx 0 · None', True),
              ('未クリア', 'idx 1 · NotClear', False),
              ('未フルコンボ', 'idx 2 · NotFullCombo', False),
              ('未AP', 'idx 4 · NotAP', False),
              ('未スコアS', 'idx 3 · NotScoreS', False)]
    for i, (lab, sub, on) in enumerate(clears):
        radio_row(dr, CX0 + 8 + (i % 3) * 344, y + (i // 3) * 96, lab, sub, on)
    y += 2 * 72 + 24

    # ---------------- MV
    y = section(dr, y + 26, 'MV', 'WORD_FILTER_TITLE_MV')
    y += 30
    mvs = [('3DMVあり', 'idx 0 · MV', True),
           ('2DMVあり', 'idx 1 · MV2D', True),
           ('原曲MVあり', 'idx 2 · OriginalMV', True),
           ('MVなし', 'idx 3 · None', True)]
    for i, (lab, sub, on) in enumerate(mvs):
        checkbox_row(dr, CX0 + 8 + (i % 3) * 344, y + (i // 3) * 88, lab, sub, on)
    y += 2 * 72 + 16

    # ---------------- 楽曲
    y = section(dr, y + 26, '楽曲', 'WORD_MUSIC')
    y += 30
    mus = [('すべて', 'idx 0 · None', True),
           ('書き下ろし楽曲', 'idx 1 · NewlyWrittenMusic', False),
           ('APPENDあり', 'idx 2 · ExistAppend', False),
           ('クリエイト譜面あり', 'idx 3 · ExistCustomScore', False)]
    for i, (lab, sub, on) in enumerate(mus):
        radio_row(dr, CX0 + 8 + (i % 3) * 344, y + (i // 3) * 96, lab, sub, on)

    # ---------------- footer buttons
    bw, bh, gap = 336, 96, 24
    bx = (WIN_W - (bw * 2 + gap)) / 2.0
    by = WIN_H - 24 - bh
    for i, lab in enumerate(('キャンセル', '決定')):
        x = bx + i * (bw + gap)
        fill = (255, 255, 255) if i == 0 else (231, 253, 250)
        edge = (208, 208, 224) if i == 0 else ACCENT
        rounded(dr, (x, by, x + bw, by + bh), 40, fill=fill, outline=edge, width=3)
        text(dr, (x + bw / 2.0, by + bh / 2.0), lab, 30, INK, anchor='mm')

    # captions (canvas coordinates, outside the dialog)
    left_px = X(0)
    dr.text((left_px, 26),
            'pjsk 選曲画面 — 絞り込みダイアログ  MusicSelectFilterDialog (prefab 1732 / DialogType 14)',
            font=font(34), fill=(232, 236, 246), anchor='la')
    dr.text((left_px, 76),
            '1168 x 1032 (DialogSize.Large) · bg_base_r30_wh + shadow r30 · 内容区 bg_base_r20_wh a=0.298 · ScrollbarVertical 6px',
            font=font(19), fill=(150, 158, 176), anchor='la')
    dr.text((left_px, 1008),
            'prefab 実測サイズから再構成した模式図（ゲーム画面のスクショではない / 88% 縮小）· 単選の index は MusicSelect*FilterType の値そのもの · MV は「未チェック集合」方式',
            font=font(19), fill=(140, 148, 168), anchor='la')
    dr.text((left_px, 1036),
            '「クリア状況」には idx 5 = NotReleaseMaster（MASTER未解放）の残骸ノードもあるが m_IsActive=0 / indexToggles 非登録で表示されない',
            font=font(19), fill=(168, 138, 138), anchor='la')

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print('wrote', OUT)


main()
