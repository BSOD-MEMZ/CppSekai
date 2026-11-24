#!/usr/bin/env python3
"""把 filter 弹窗要用的 5 张官方 sprite 从 build/spr_fix 复制进 assets/select。

这些是 prefab MusicSelectFilterDialog（1732）里实际引用的贴图：
  bg_base_r30_wh          窗口底板（九宫格 r30，86x86）
  bg_base_shadow_r30      窗口阴影（86x86）
  bg_base_wh              段标题下划线（4x4，纯白，染成 #A7A7BC）
  icon_check_pk           复选框的勾（44x34，pink）
  btn_close_dbl           关闭 X（44x44）
  bg_base_round_vertical_h6_wh  滚动条轨道（6x14）
  bg_base_round_vertical_h8_wh  滚动条把手（8x18）

用法: python .workbuddy/tools/copy_filter_sprites.py [srcdir]
"""
import pathlib
import shutil
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build" / "spr_fix"
DST = REPO / "assets" / "select"

NAMES = [
    "bg_base_r30_wh",
    "bg_base_shadow_r30",
    "bg_base_wh",
    "icon_check_pk",
    "btn_close_dbl",
    "bg_base_round_vertical_h6_wh",
    "bg_base_round_vertical_h8_wh",
]


def main():
    DST.mkdir(parents=True, exist_ok=True)
    missing = []
    for name in NAMES:
        src = SRC / (name + ".png")
        if not src.exists():
            missing.append(name)
            continue
        shutil.copy2(src, DST / (name + ".png"))
        print("copied", name)
    if missing:
        print("MISSING:", missing)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
