"""Render a measured schematic of the pjsk song-select top band (1920x1080 canvas).

Rects come from the prefab dump (ms_geom.py + hdr_rects.py), never eyeballed:
  ScreenLayerHeader              prefab 694
  ScreenLayerFreeLiveMusicSelect prefab 702
Sprites: tex_all_sprite (AssetStudio export of the jp 7.1.0 asset pack).
9-slice borders are the real m_Border values:
  btn_round_r16_wh  56x56  border 28   (the search field's ends are r28, not r16)
  btn_round_h56_wh  82x72  border 39/34
  btn_circle56/80_wh       border 0    (plain discs, drawn 1:1)

The band is drawn light on purpose: the real bar sits over the blurred jacket art,
and the 「SOLO LIVE」logo is dark navy - on a dark mock its fill disappears and only
the cyan offset shadow shows, which makes the whole block look like an outline.
"""
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

SPR = r'D:\Tools\rea\work\jp\tex_all_sprite'
OUT = r'D:\Dev\CppSekai\docs\select-topbar-1920.png'
FONT = r'C:\Windows\Fonts\msyh.ttc'

W, H = 1920, 980
PAGE = (34, 36, 50)
BAND = (232, 235, 243)
INK = (236, 239, 246)
DARK = (52, 56, 74)
DIM = (154, 162, 182)
ACCENT = (214, 84, 140)
NAVY = (68, 68, 102)
PINK = (255, 120, 175)


def sprite(name, tint=None):
    im = Image.open(os.path.join(SPR, name + '.png')).convert('RGBA')
    if tint:
        px = im.load()
        for y in range(im.height):
            for x in range(im.width):
                r, g, b, a = px[x, y]
                px[x, y] = (tint[0], tint[1], tint[2], a)
    return im


def nine_slice(im, w, h, border):
    im = im.convert('RGBA')
    sw, sh = im.size
    bx, by = border if isinstance(border, tuple) else (border, border)
    bx = max(1, min(bx, (sw - 1) // 2, (w - 1) // 2))
    by = max(1, min(by, (sh - 1) // 2, (h - 1) // 2))
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    out.paste(im.crop((0, 0, bx, by)), (0, 0))
    out.paste(im.crop((sw - bx, 0, sw, by)), (w - bx, 0))
    out.paste(im.crop((0, sh - by, bx, sh)), (0, h - by))
    out.paste(im.crop((sw - bx, sh - by, sw, sh)), (w - bx, h - by))
    out.paste(im.crop((bx, 0, sw - bx, by)).resize((w - 2 * bx, by)), (bx, 0))
    out.paste(im.crop((bx, sh - by, sw - bx, sh)).resize((w - 2 * bx, by)), (bx, h - by))
    out.paste(im.crop((0, by, bx, sh - by)).resize((bx, h - 2 * by)), (0, by))
    out.paste(im.crop((sw - bx, by, sw, sh - by)).resize((bx, h - 2 * by)), (w - bx, by))
    out.paste(im.crop((bx, by, sw - bx, sh - by)).resize((w - 2 * bx, h - 2 * by)), (bx, by))
    return out


def main():
    img = Image.new('RGB', (W, H), PAGE)
    d = ImageDraw.Draw(img)
    f_head = ImageFont.truetype(FONT, 27)
    f_body = ImageFont.truetype(FONT, 24)
    f_small = ImageFont.truetype(FONT, 17)
    f_sub = ImageFont.truetype(FONT, 30)

    def paste(im, x, y, shadow=0.0, blur=7):
        """Composite with the soft drop shadow the game puts under white pills."""
        if shadow > 0:
            layer = Image.new('RGBA', (im.width + 24, im.height + 24), (0, 0, 0, 0))
            mask = Image.new('L', (im.width, im.height), 0)
            mask.paste(im.split()[-1], (0, 0))
            shadow_mask = mask.filter(ImageFilter.GaussianBlur(blur)).point(
                lambda v: int(v * shadow))
            black = Image.new('RGBA', im.size, (40, 44, 62, 255))
            layer.paste(black, (12, 14), shadow_mask)
            img.paste(layer, (int(round(x)) - 12, int(round(y)) - 12), layer)
        img.paste(im, (int(round(x)), int(round(y))), im)

    def dashed(x, y, w, h, color=ACCENT):
        step = 9
        for i in range(0, int(w), step * 2):
            d.line([x + i, y, min(x + i + step, x + w), y], fill=color)
            d.line([x + i, y + h, min(x + i + step, x + w), y + h], fill=color)
        for i in range(0, int(h), step * 2):
            d.line([x, y + i, x, min(y + i + step, y + h)], fill=color)
            d.line([x + w, y + i, x + w, min(y + i + step, y + h)], fill=color)

    def center_text(cx, cy, text, font, fill, stroke=0, stroke_fill=None):
        box = d.textbbox((0, 0), text, font=font, stroke_width=stroke)
        d.text((cx - (box[2] - box[0]) / 2 - box[0], cy - (box[3] - box[1]) / 2 - box[1]),
               text, font=font, fill=fill, stroke_width=stroke, stroke_fill=stroke_fill)

    # ---------------- row A ------------------------------------------------
    d.text((24, 14), '① ScreenLayerHeader（全局顶栏 prefab 694）：单人 / 自由演出选曲时显示的三件套',
           font=f_head, fill=INK)
    ay = 96
    d.rectangle([0, ay, W, ay + 144], fill=BAND)

    # back button: white disc 96x96 @ (16,16) + 42x42 arrow tinted #444466
    paste(sprite('btn_circle80_wh'), 16, ay + 16, shadow=0.22)
    paste(sprite('icon_back_wh', NAVY), 43, ay + 43)
    d.rectangle([16, ay + 16, 112, ay + 112], outline=(150, 156, 172), width=1)

    # screen-name block: pill 604x62 @ (107,61); logo 369x59 native + sub-name inline
    pill_x, pill_y, pill_w, pill_h = 107, ay + 61, 604, 62
    dashed(pill_x, pill_y, pill_w, pill_h)
    logo = sprite('title_freeLive')
    paste(logo, pill_x + 14, pill_y + (pill_h - logo.height) / 2)
    tri_x = pill_x + 14 + logo.width + 16
    tri_y = pill_y + (pill_h - 28) / 2
    d.polygon([(tri_x, tri_y + 3), (tri_x, tri_y + 25), (tri_x + 21, tri_y + 14)], fill=PINK)
    center_text(tri_x + 21 + 14 + 80, pill_y + pill_h / 2, '楽曲選択', f_sub,
                (255, 255, 255), stroke=3, stroke_fill=(72, 76, 110))

    # menu button: white disc 96x96 @ (1808,16) + hamburger 40x34 + badge 32 @ (1872,16)
    paste(sprite('btn_circle80_wh'), 1808, ay + 16, shadow=0.22)
    paste(sprite('icon_menu_wh', NAVY), 1836, ay + 47)
    paste(sprite('badge_attention_rd'), 1872, ay + 16, shadow=0.18, blur=3)
    d.rectangle([1808, ay + 16, 1904, ay + 112], outline=(150, 156, 172), width=1)

    d.text((736, ay + 46), '← 标题块 604x62（bg_contentsName 未导出）', font=f_small, fill=(120, 126, 146))

    # ---------------- row B ------------------------------------------------
    d.text((24, 274), '② ScreenLayerFreeLiveMusicSelect（选曲列表层 prefab 702）：和顶栏挤在同一条 96px 带里的控件',
           font=f_head, fill=INK)
    by = 356
    d.rectangle([0, by, W, by + 144], fill=BAND)

    # search field 760.8x72 @ (196,28) - btn_round_r16_wh sliced with border 28
    paste(nine_slice(sprite('btn_round_r16_wh'), 761, 72, 28), 196, by + 28, shadow=0.18)
    d.rectangle([196, by + 28, 957, by + 100], outline=(190, 195, 208), width=1)
    d.text((226, by + 52), '楽曲名・アーティスト名で検索', font=f_body, fill=(158, 163, 178))
    paste(sprite('icon_search_h34_wh', (150, 156, 172)), 898, by + 47)

    # filter 72x72 @ (1380,27)
    paste(sprite('btn_circle56_wh'), 1380, by + 27, shadow=0.22)
    paste(sprite('icon_refine_dbl', NAVY), 1401, by + 49)
    d.rectangle([1380, by + 27, 1452, by + 99], outline=(150, 156, 172), width=1)

    # sort dropdown 280x56 @ (1476.5,36) - btn_round_h56_wh sliced 39/34
    paste(nine_slice(sprite('btn_round_h56_wh'), 280, 56, (39, 34)), 1477, by + 36, shadow=0.18)
    d.rectangle([1477, by + 36, 1757, by + 92], outline=(190, 195, 208), width=1)
    center_text(1477 + 132, by + 64, '並び替え', f_body, (96, 102, 126))

    # the row-A menu button, for the x-alignment reference only
    d.rectangle([1808, by + 29, 1904, by + 101], outline=(150, 156, 172), width=1)
    center_text(1856, by + 65, '菜单', f_small, (120, 126, 146))
    done = d.textbbox((0, 0), '（同一 x，见上）', font=f_small)
    d.text((1904 - (done[2] - done[0]), by + 106), '（同一 x，见上）', font=f_small, fill=(120, 126, 146))

    # ---------------- table ------------------------------------------------
    rows = [
        ('返回 UIPartsBackButton', '96 × 96  @ (16, 16)', 'btn_circle80_wh + icon_back_wh 42×42 #444466'),
        ('标题块 ScreenNamePanel', '604 × 62  @ (107, 61)', 'bg_contentsName（切片图未导出）；标题图 title_freeLive 369×59'),
        ('菜单 UIPartsMenuButton', '96 × 96  @ (1808, 16)', 'btn_circle80_wh + icon_menu_wh 40×34 #444466'),
        ('菜单角标 badge', '32 × 32  @ (1872, 16)', 'badge_attention_rd（未读提示，平时隐藏）'),
        ('搜索 UIPartsSearchField', '760.8 × 72  @ (196, 28)', 'btn_round_r16_wh（九宫格 border 28）+ icon_search_h34_wh 34²'),
        ('筛选 FilterButton', '72 × 72  @ (1380, 27)', 'btn_circle56_wh（72² 圆，1:1）+ icon_refine_dbl 29×28'),
        ('排序 UIPartsDropdown', '280 × 56  @ (1476.5, 36)', 'btn_round_h56_wh（border 39/34）；名字是运行时文本'),
    ]
    d.text((24, 528), '量到的东西（1920×1080 画布，单位 px；y 从屏幕上沿往下量）', font=f_head, fill=INK)
    y = 570
    d.text((32, y), '元素', font=f_small, fill=(255, 168, 202))
    d.text((560, y), '矩形', font=f_small, fill=(255, 168, 202))
    d.text((830, y), '贴图 / 说明', font=f_small, fill=(255, 168, 202))
    y += 30
    for name, rect, note in rows:
        d.text((32, y), name, font=f_small, fill=INK)
        d.text((560, y), rect, font=f_small, fill=INK)
        d.text((830, y), note, font=f_small, fill=DIM)
        y += 27

    d.text((24, y + 10),
           '选曲界面的 ScreenLayerData：DisplayHeader = Show、DisplayCategory = 0x107（Menu|Rank|Currency|LiveBoost）、'
           'DisplayPlayerInfo = Hide、DisplayBackUIScreen = Show、DisplayScreenName = Show。',
           font=f_small, fill=DIM)
    d.text((24, y + 36),
           '→ 等级 / 宝石 / 体力那一簇（PlayerInfo）在这一屏是关掉的，顶栏只剩「返回 + 标题 + 菜单」；'
           '搜索 / 排序 / 筛选属于下面那层，不是顶栏 prefab 的零件。',
           font=f_small, fill=DIM)

    img.save(OUT)
    print('wrote', OUT, img.size)


if __name__ == '__main__':
    main()
