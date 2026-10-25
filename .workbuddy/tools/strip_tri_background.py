# -*- coding: utf-8 -*-
"""把 pjsk 的 tex_sekai_01_2.png（2x2 四色三角表）裁成"只有三角形"的版本。

原图每个 cell 里，三角形外面还有一层 alpha=255 的底色渐变。官方的 mesh
(sekai_PT_05) 本身就是三角形，所以那层底色永远不会被采样；我们拿正方形
quad 贴图，就会把底色一起画成方块。这里按 alpha 把底色抹掉：

  三角形内部      alpha 235 -> 100（自上而下渐变）
  三角形白描边    alpha ~235
  底色            alpha 恒为 255

阈值取 252 以上判为底色。原图只作参考，不放进仓库。

用法:
  python strip_tri_background.py [原图] [输出]
默认:
  原图 D:/Tools/rea/work/jp/tex_sekai/tex_sekai_01_2.png
  输出 <repo>/assets/fx/tap_tri.png
"""
import os
import sys

from PIL import Image

DEFAULT_SRC = r'D:/Tools/rea/work/jp/tex_sekai/tex_sekai_01_2.png'
THRESHOLD = 252


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, '..', '..'))
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(repo, 'assets', 'fx', 'tap_tri.png')

    if not os.path.exists(src):
        print('缺少原图:', src)
        return 1

    im = Image.open(src).convert('RGBA')
    w, h = im.size
    px = im.load()

    cleared = 0
    kept = 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a >= THRESHOLD:
                px[x, y] = (r, g, b, 0)
                cleared += 1
            else:
                kept += 1

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    im.save(dst)
    print('原图 %s (%dx%d)' % (src, w, h))
    print('抹掉底色 %d px (%.1f%%)，保留三角形 %d px (%.1f%%)'
          % (cleared, 100.0 * cleared / (w * h), kept, 100.0 * kept / (w * h)))
    print('输出', dst)

    # 顺便报告三角形在 cell 里的包围盒，方便以后核对 quad 比例
    label_w, label_h = w // 2, h // 2
    xs, ys = [], []
    for y in range(label_h):
        for x in range(label_w):
            if px[x, y][3] > 8:
                xs.append(x)
                ys.append(y)
    if xs:
        print('cell(0,0) 三角形包围盒 x %d..%d  y %d..%d  ->  宽高比 %.3f'
              % (min(xs), max(xs), min(ys), max(ys),
                 (max(ys) - min(ys) + 1) / float(max(xs) - min(xs) + 1)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
