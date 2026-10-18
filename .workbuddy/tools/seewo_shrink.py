"""把 Drafts/seewo.png 的屏幕改小、外框尺寸不变（教学卡片用）。

做法：屏幕是纯蓝的，白字 logo 在正中。在 logo 两侧各剪掉一条空白蓝带，
把剩下的蓝色往中间拼，空出来的屏幕区域用**原有边框像素横向拉伸**补上。
结果是：外框（黑边 + 灰色内框 + 底部 seewo 条）尺寸一点没变，屏幕却窄了一圈，
logo 仍然居中 —— 看着就是一台屏幕特别小的 seewo。

用法：python .workbuddy/tools/seewo_shrink.py
输出：assets/story/img/seewo_small.png
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "Drafts" / "seewo.png"
DST = ROOT / "assets" / "story" / "img" / "seewo_small.png"

# 实测：蓝色屏幕 x 43..750，白字 logo x 287..518（y 211..261）
SCREEN_L, SCREEN_R = 43, 751          # 右边界不含
CUT_L = (60, 190)                     # logo 左边的空白蓝带
CUT_R = (610, 740)                    # logo 右边的空白蓝带
BEZEL_L = (30, 44)                    # 屏幕左侧的内框（含高光/凹槽）
BEZEL_R = (751, 765)                  # 屏幕右侧的内框


def main() -> None:
    im = Image.open(SRC).convert("RGB")
    y0, y1 = 35, 435  # 屏幕上下边界

    left = im.crop((SCREEN_L, y0, CUT_L[0], y1))
    middle = im.crop((CUT_L[1], y0, CUT_R[0], y1))
    right = im.crop((CUT_R[1], y0, SCREEN_R, y1))

    screen_w = left.width + middle.width + right.width
    screen = Image.new("RGB", (screen_w, y1 - y0))
    x = 0
    for part in (left, middle, right):
        screen.paste(part, (x, 0))
        x += part.width

    cx = (SCREEN_L + SCREEN_R) // 2
    dst_x = cx - screen_w // 2
    out = im.copy()
    # 空出来的两条：纯黑当外框，靠屏幕那一侧贴一条**镜像的内框剖面**
    # （原图的内框带高光和凹槽，直接拉伸会糊成一片，镜像一段最像真的）。
    margin_l = dst_x - SCREEN_L
    margin_r = SCREEN_R - (dst_x + screen_w)
    if margin_l > 0:
        ImageDraw.Draw(out).rectangle([SCREEN_L, y0, dst_x, y1 - 1], fill=(0, 0, 0))
        edge = im.crop((BEZEL_L[0], y0, BEZEL_L[1], y1))
        out.paste(edge, (dst_x - edge.width, y0))
    if margin_r > 0:
        right_x = dst_x + screen_w
        ImageDraw.Draw(out).rectangle([right_x, y0, SCREEN_R - 1, y1 - 1], fill=(0, 0, 0))
        edge = im.crop((BEZEL_R[0], y0, BEZEL_R[1], y1))
        out.paste(edge, (right_x, y0))
    out.paste(screen, (dst_x, y0))

    DST.parent.mkdir(parents=True, exist_ok=True)
    out.save(DST)
    print(f"{SRC.name} {im.size} -> {DST.relative_to(ROOT)} {out.size}")
    print(f"screen {SCREEN_R - SCREEN_L}px -> {screen_w}px at x={dst_x}")


if __name__ == "__main__":
    main()
