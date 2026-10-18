"""教学用的音符示意图：从游戏自己的精灵图集拼一张。

为什么不用实拍图/商品图：那张 seewo 无论怎么改，用户看到的都是「一台显示器」，
和「触摸屏怎么打音符」没有关系。这里直接用 assets/mmw/notes_01.png 里的
tap / hold / flick 三种音符，配上说明文字，一张图讲完三种手势。

用法：python .workbuddy/tools/story_assets.py
输出：assets/story/img/notes_touch.png（透明底）
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SHEET = ROOT / "assets" / "mmw" / "notes_01.png"
OUT = ROOT / "assets" / "story" / "img" / "notes_touch.png"

# 精灵在 notes_01.png 里的位置（手工框的，外面留一点余量再按 alpha 收边）
SPRITES = {
    "tap": (1080, 840, 1288, 1020),
    "hold": (900, 690, 1180, 810),
    "flick": (1308, 858, 1436, 1000),
}

W, H = 1240, 470
COLS = [210, 620, 1030]
LABELS = [
    ("Tap", "蓝点落到线时点一下"),
    ("Hold", "长条按住，尾巴到线再松"),
    ("Flick", "带箭头的，顺着箭头划"),
]


def sprite(name: str) -> Image.Image:
    sheet = Image.open(SHEET).convert("RGBA")
    crop = sheet.crop(SPRITES[name])
    # 图集里每颗音符脚下都拖着一块灰色的「轨道投影」：在深色板子上看就是一块脏
    # 梯形。按「近灰且不亮」把投影剔掉，只留粉/绿主体和白色描边。
    px = crop.load()
    for y in range(crop.height):
        for x in range(crop.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            sat = max(r, g, b) - min(r, g, b)
            if sat < 26 and (r + g + b) / 3.0 < 165:
                px[x, y] = (r, g, b, 0)
    bbox = crop.getchannel("A").getbbox()
    return crop.crop(bbox) if bbox else crop


def fit(im: Image.Image, box_w: int, box_h: int) -> Image.Image:
    scale = min(box_w / im.width, box_h / im.height)
    return im.resize((max(1, int(im.width * scale)), max(1, int(im.height * scale))), Image.LANCZOS)


def main() -> None:
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    try:
        big = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 36)
        small = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 24)
    except OSError:
        big = small = ImageFont.load_default()

    chip_w, chip_h = 330, 214
    for i, (name, cx) in enumerate(zip(["tap", "hold", "flick"], COLS)):
        # 精灵自带一层白色投影（图集里是给浅色轨道用的），直接放深色板上会变灰块。
        # 所以每颗先坐在一块浅色圆角「托盘」上，投影正好落在浅底上。
        draw.rounded_rectangle([cx - chip_w // 2, 8, cx + chip_w // 2, 8 + chip_h], radius=28,
            fill=(246, 247, 252, 235), outline=(200, 204, 224, 255), width=3)
        art = fit(sprite(name), chip_w - 46, chip_h - 38)
        canvas.alpha_composite(art, (cx - art.width // 2, 8 + (chip_h - art.height) // 2))

        title, hint = LABELS[i]
        tw = draw.textlength(title, font=big)
        # 文字带一点深色描边：这张图会落在深色板子上，也会落在浅色屏幕上
        draw.text((cx - tw / 2, 226), title, font=big, fill=(255, 255, 255, 255),
            stroke_width=3, stroke_fill=(28, 28, 46, 220))
        hw = draw.textlength(hint, font=small)
        draw.text((cx - hw / 2, 292), hint, font=small, fill=(226, 228, 242, 255),
            stroke_width=3, stroke_fill=(28, 28, 46, 200))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(OUT)
    print("notes", canvas.size, "->", OUT)


if __name__ == "__main__":
    main()
