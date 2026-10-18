"""教学卡片用的两张图，都是透明底。

1) seewo_small.png —— 把 Drafts/seewo.png（自带 alpha 的商品图）的屏幕横向缩到 58%，
   外框尺寸不变。屏幕上 logo 两侧各挖一条纯蓝、三段拼回去；两侧空出来的地方用原图
   这一行的边框色平铺，外缘 3px 与内侧亮边原样保留。底部长条 / `seewo` 印字 /
   右下指示灯全部保留 —— 荒诞感来自「小屏 + 巨框」。
2) kb_mouse.png —— 12 键示意图（Z S X D C V / G B H N J M + 鼠标），自绘、透明底。

用法：python .workbuddy/tools/story_assets.py
"""
from collections import deque
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SRC_DIR = ROOT / "assets" / "story" / "img"
SEEWO_SRC = ROOT / "Drafts" / "seewo.png"
SEEWO_DST = SRC_DIR / "seewo_small.png"
KB_SRC = SRC_DIR / "kb_mouse_raw.jpg"
KB_DST = SRC_DIR / "kb_mouse.png"

# seewo 原图是**自带 alpha 的商品图**（800x490），不是白底实拍：
#   设备 30,22 - 765,461；蓝色屏幕 43,35 - 750,434；白字 logo 287..518。
# 目标：屏幕横向缩到 58%、外框尺寸一分不变 —— 两侧边框变宽，屏幕显得又小又方，
# 荒诞感来自「小屏 + 巨框」，而 seewo 的底部长条、右下指示灯、左上角反光全都留着。
DEV_L, DEV_T, DEV_R, DEV_B = 30, 22, 765, 461
SCR_L, SCR_T, SCR_R, SCR_B = 43, 35, 750, 434
NEW_SCR_W = 410          # 原 708 → 58%
KEEP_CORNER = 60          # 屏幕左右各保留的圆角宽度，不参与剪切


def _lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(4))


def shrink_seewo() -> Image.Image:
    im = Image.open(SEEWO_SRC).convert("RGBA")
    cut = (SCR_R - SCR_L - NEW_SCR_W) // 2   # 每侧要挖掉的蓝屏宽度

    # 屏幕内容：保留左右圆角，从 logo 两侧各挖一条纯蓝，三个片段接起来。
    # 接缝两边在同一个 y 上颜色几乎相同，再交叉淡化 3px 就看不出来了。
    l_end = SCR_L + KEEP_CORNER
    r_start = SCR_R - KEEP_CORNER
    left = im.crop((SCR_L, SCR_T, l_end, SCR_B))
    mid = im.crop((l_end + cut, SCR_T, r_start - cut, SCR_B))
    right = im.crop((r_start, SCR_T, SCR_R, SCR_B))

    screen = Image.new("RGBA", (left.width + mid.width + right.width, SCR_B - SCR_T))
    x = 0
    for part in (left, mid, right):
        screen.paste(part, (x, 0))
        x += part.width

    out = im.copy()
    dst_x = (DEV_L + DEV_R) // 2 - screen.width // 2
    dst_y = SCR_T
    px = out.load()

    def bezel_col(x0: int, y: int) -> tuple:
        return px[x0, y]

    # 左右两侧空出来的区域：不能把整条剖面拉宽 —— 原图在 x=37 有一条内阴影，
    # 拉宽会变成一道几十像素的黑带。做法是「保留两端，中间用这一行的框体色平铺」：
    #   · 外侧 3px（silhouette + 高光）原样保留，切边才不会发虚；
    #   · 内侧 4px（屏幕边上的亮边）原样贴到新屏幕边上；
    #   · 中间逐行取原边框的中间色（x=34 / x=759），于是上下明暗过渡还是真的。
    for y in range(SCR_T, SCR_B):
        gap_l = dst_x - DEV_L
        body_l = px[34, y]
        for u in range(gap_l):
            if u < 3:
                out.putpixel((DEV_L + u, y), px[DEV_L + u, y])
            elif u >= gap_l - 4:
                out.putpixel((DEV_L + u, y), px[SCR_L - (gap_l - u), y])
            else:
                out.putpixel((DEV_L + u, y), body_l)

        gap_r = DEV_R - (dst_x + screen.width)
        body_r = px[759, y]
        for u in range(gap_r):
            x_dst = dst_x + screen.width + u
            if u < 4:
                out.putpixel((x_dst, y), px[SCR_R + 2 + u, y])
            elif u >= gap_r - 2:
                out.putpixel((x_dst, y), px[DEV_R - (gap_r - u), y])
            else:
                out.putpixel((x_dst, y), body_r)

    out.paste(screen, (dst_x, dst_y))
    return out


def cutout_white(im: Image.Image, inner: int = 238, outer: int = 226) -> Image.Image:
    """Flood from the border over near-white pixels; alpha ramps between
    `outer` and `inner` luma so shadows stay soft."""
    rgb = im.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    alpha = Image.new("L", (w, h), 255)
    ap = alpha.load()

    def white(x: int, y: int) -> bool:
        r, g, b = px[x, y]
        return (r + g + b) / 3.0 >= outer

    seen = [[False] * w for _ in range(h)]
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            if white(x, y) and not seen[y][x]:
                seen[y][x] = True
                q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if white(x, y) and not seen[y][x]:
                seen[y][x] = True
                q.append((x, y))
    while q:
        x, y = q.popleft()
        luma = sum(px[x, y]) / 3.0
        t = (inner - luma) / max(1.0, float(inner - outer))
        ap[x, y] = int(max(0.0, min(1.0, t)) * 255.0)
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx] and white(nx, ny):
                seen[ny][nx] = True
                q.append((nx, ny))

    out = rgb.convert("RGBA")
    out.putalpha(alpha)
    return out


def cutout_saturation(im: Image.Image, lo: int = 20, hi: int = 38) -> Image.Image:
    rgb = im.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    alpha = Image.new("L", (w, h), 255)
    ap = alpha.load()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            sat = max(r, g, b) - min(r, g, b)
            if sat >= hi:
                ap[x, y] = 0
            elif sat <= lo:
                ap[x, y] = 255
            else:
                ap[x, y] = int((hi - sat) / float(hi - lo) * 255.0)
    out = rgb.convert("RGBA")
    out.putalpha(alpha)
    return out


def trim(im: Image.Image, pad: int = 8) -> Image.Image:
    bbox = im.getchannel("A").getbbox()
    if bbox is None:
        return im
    l, t, r, b = bbox
    return im.crop((max(0, l - pad), max(0, t - pad), min(im.width, r + pad), min(im.height, b + pad)))


def draw_keys() -> Image.Image:
    """默认 12 键的示意图：Z S X D C V / G B H N J M + 一只鼠标。

    先按 2 倍尺寸画再缩一半：圆角和描边才有抗锯齿。背景全透明；
    键帽是白面 + 灰紫描边，落在白色教学卡片上也分得清。
    鼠标故意画得比键帽高，缩到卡片里才不会变成一根胶囊。
    """
    from PIL import ImageDraw, ImageFont

    W, H = 1900, 760
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 92)
    except OSError:
        font = ImageFont.load_default()

    labels = ["Z", "S", "X", "D", "C", "V", "G", "B", "H", "N", "J", "M"]
    key_w, key_h, gap = 220, 220, 24
    x0, y0 = 40, 120
    for i, label in enumerate(labels):
        col, row = i % 6, i // 6
        x = x0 + col * (key_w + gap)
        y = y0 + row * (key_h + gap)
        d.rounded_rectangle([x + 4, y + 10, x + key_w + 4, y + key_h + 10], radius=30,
            fill=(60, 62, 88, 45))
        d.rounded_rectangle([x, y, x + key_w, y + key_h], radius=30, fill=(238, 240, 249, 255),
            outline=(169, 173, 198, 255), width=6)
        tw = d.textlength(label, font=font)
        d.text((x + (key_w - tw) / 2, y + key_h * 0.5 - 54), label, font=font, fill=(60, 60, 90, 255))

    # 鼠标：和键帽同一套描边 / 圆角，高度对齐两排键
    mx, my, mw, mh = 1600, 128, 250, 444
    d.rounded_rectangle([mx + 4, my + 10, mx + mw + 4, my + mh + 10], radius=115, fill=(60, 62, 88, 45))
    d.rounded_rectangle([mx, my, mx + mw, my + mh], radius=115, fill=(238, 240, 249, 255),
        outline=(169, 173, 198, 255), width=6)
    # 左右键分界线 + 滚轮
    d.line([mx + 40, my + 132, mx + mw - 40, my + 132], fill=(169, 173, 198, 255), width=5)
    d.line([mx + mw / 2, my + 20, mx + mw / 2, my + 132], fill=(169, 173, 198, 255), width=5)
    d.rounded_rectangle([mx + mw / 2 - 17, my + 52, mx + mw / 2 + 17, my + 108], radius=17,
        fill=(150, 154, 182, 255))

    return im.resize((W // 2, H // 2), Image.LANCZOS)


def main() -> None:
    seewo = trim(shrink_seewo(), pad=2)
    seewo.save(SEEWO_DST)
    print("seewo", seewo.size)

    kb = trim(draw_keys(), pad=6)
    kb.save(KB_DST)
    print("keyboard", kb.size)


if __name__ == "__main__":
    main()
