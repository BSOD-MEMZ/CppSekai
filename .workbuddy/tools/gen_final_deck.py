"""Draw the deck-selector schematic for docs/final-deck-1920.png.

Every rectangle below is transcribed from prefab 703
(`ScreenLayerFreeLiveFinalConfirmation`, collection `{"C":{"B":{"P":[0]},"I":0}`,
path id 703) — the numbers came out of `build/tmp_re/pwalk2.py 703` +
`ptree2.py` (GameObject YAML -> absolute 1920x1080 rect), not from screenshots.

    python .workbuddy/tools/gen_final_deck.py docs/final-deck-1920.png
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = None
for _path in ("C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf"):
    try:
        FONT = ImageFont.truetype(_path, 18)
        break
    except OSError:
        continue

W, H = 1920, 1080

# 颜色只用来区分零件，不是游戏里的真实配色。
BG = (38, 34, 58)
PANEL = (223, 223, 234)
BAR = (188, 188, 208)
CARD = (176, 189, 200)
INK = (68, 68, 102)
DIM = (160, 160, 180)
WARN = (255, 120, 140)


def box(d, x0, y0, x1, y1, fill=None, outline=None, width=2):
    d.rectangle([x0, y0, x1, y1], fill=fill, outline=outline, width=width)


def txt(d, xy, s, fill=(240, 240, 250)):
    d.text(xy, s, fill=fill, font=FONT)


def main(out):
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)

    # DeckBase: 70.4,256 1080x624  bg_base_three_r16_wh #DFDFEA a80%
    box(d, 70.4, 256, 70.4 + 1080, 256 + 624, PANEL)

    # DeckCardList: HLG align=4 spacing=7.66 -> 5 x 200x510, x0=94.28, step 207.66
    for i in range(5):
        x = 94.28 + i * 207.66
        box(d, x, 346, x + 200, 346 + 510, CARD, (120, 130, 140), 1)
        d.ellipse([x + 64, 346 + 219, x + 136, 346 + 291], fill=(145, 159, 175))
        txt(d, (x + 92, 346 + 238), "+", (235, 240, 245))

    # DeckName: 70.4,200.7 375.7x55.4 bg_base_half_r16_wh（上圆角、下直角）
    box(d, 70.4, 200.7, 70.4 + 375.7, 200.7 + 55.4, PANEL, (150, 150, 170), 1)
    txt(d, (92, 217), "DeckName 375.7x55.4 (当前队伍)", INK)

    # DeckSelectList/Base: 70.4,127.6 720.13x56 bg_base_round_h56_wh #BCBCD0 a90%
    box(d, 70.4, 127.6, 70.4 + 720.13, 127.6 + 56, BAR)
    # Base/CellRoot: 同一矩形，HLG align=3 spacing=0 → 5 格均分 144.03x56
    for i in range(5):
        x0 = 70.4 + i * 144.026
        box(d, x0, 127.6, x0 + 144.026, 183.6, None, (110, 110, 140), 1)
        txt(d, (x0 + 60, 146), "%02d" % (i + 1), INK)

    # Base/Arrow/UIPartsArrow{Left,Right}: 72x102 @ ±33.6, scale 0.38 → 27.4x38.8
    for cx in (36.8, 824.1):
        hw, hh = 72 * 0.38 / 2, 102 * 0.38 / 2
        box(d, cx - hw, 155.6 - hh, cx + hw, 155.6 + hh, None, WARN, 2)

    # DeckListView arrows: 左 21.4..93.4 y 518..620 / 右 1134..1206 y 493.8..595.8
    box(d, 21.4, 518, 93.4, 620, None, WARN, 2)
    box(d, 1134, 493.8, 1206, 595.8, None, WARN, 2)

    # 手机左缘 1144.85
    d.line([1144.85, 0, 1144.85, H], fill=(240, 240, 250), width=1)
    txt(d, (1152, 14), "phone left edge 1144.85")

    # 图例
    txt(d, (70, 30), "ScreenLayerFreeLiveFinalConfirmation (prefab 703) — 1920x1080")
    txt(d, (70, 50), "卡组区 · 全部矩形实测自 prefab YAML", DIM)
    txt(d, (70, 896), "红框 = prefab 里存在、但这一页不该画的箭头：", WARN)
    txt(d, (70, 918), "· 条内翻页箭头 72x102 @±33.6 scale 0.38（实显 27.4x38.8），"
                      "UIPartsSelector paging，5 格正好铺满 720 → 关", WARN)
    txt(d, (70, 940), "· DeckListView 大箭头 72x102：左 21.4..93.4 / 右 1134..1206，"
                      "右边那颗整个压在手机底下", WARN)

    im.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "docs/final-deck-1920.png")
