"""Dump the multiplayer live HUD art (头像块 / 团队分条 / Fever / 块内 combo) out of the
jp asset tree into one folder, with an index and a contact sheet.

Source: D:/Tools/rea/work/jp/tex_all_sprite (2808 sprites, AssetStudioModCLI full export)
Output: build/multi_ui/   (build/ is git-ignored - these are official assets, never commit)

usage: python .workbuddy/tools/dump_multi_ui.py [srcdir] [outdir]
"""
import io
import os
import shutil
import sys

from PIL import Image, ImageDraw

SRC = sys.argv[1] if len(sys.argv) > 1 else r'D:/Tools/rea/work/jp/tex_all_sprite'
OUT = sys.argv[2] if len(sys.argv) > 2 else r'D:/Dev/CppSekai/build/multi_ui'
JP = r'D:/Tools/rea/work/jp'

# (prefab 节点 / 用途) -> 候选 sprite 名（按顺序找第一个存在的）
GROUPS = [
    ('头像块 MultiThumbnailView (prefab 1224)', [
        ('头像框', ['frame_player_multi']),
        ('卡面遮罩', ['thumb_mask']),
        ('缩略图色标', ['label_color_thumbnail']),
        ('YOU 标记', ['text_you_gn', 'txt_you_line_wh']),
        ('feverReady 标记（贴图内容是 "OK!"）', ['text_feverOK']),
        ('濒死/掉血效果', ['effect_playerScoreDanger_multi']),
        ('死亡态底', ['frame_multi_black']),
    ]),
    ('块内 ComboView（每人都有一套）', [
        ('COMBO 底', ['bg_combo_pl']),
        ('COMBO 光效', ['eff_combo']),
    ]),
    ('团队分条 MultiScoreView (prefab 1226)', [
        ('分条填充', ['gauge_bar_ingame_multiLive_s_wh']),
        ('分条底', ['bar_scoreGauge_multi']),
        ('分条遮罩', ['bar_scoreGauge_mask']),
        ('分条底板', ['bg_scoreGauge 1']),
        ('评级 S', ['ScoreRank_Mark_S']),
        ('评级 D', ['ScoreRank_Mark_D']),
    ]),
    ('Fever MultiFeverView (prefab 1232)', [
        ('Fever 轨道', ['gage_fever_lane']),
        ('Fever 线', ['line_fever']),
        ('SuperFever 线', ['line_superFever']),
        ('Fever 线文字', ['text_lineFever']),
        ('SuperFever 线文字', ['text_lineSuperFever']),
        ('SuperFever 图标', ['itemBox_superFever']),
        ('SuperFever 图标(开)', ['itemBox_superFever_open']),
    ]),
]

# 连击数字单独展开（10 张）
for i in range(10):
    GROUPS[1][1].append(('连击数字 %d' % i, ['number_combo_154px_%d_pr' % i]))

PREFABS = [
    (1224, 'ml_thumb', 'MultiThumbnailView'),
    (1226, 'ml_score', 'MultiScoreView'),
    (1225, 'ml_skilltext', 'multiSkillTextViewGroup'),
    (1232, 'ml_fever', 'FeverView'),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for group, items in GROUPS:
        for label, candidates in items:
            hit = None
            for c in candidates:
                p = os.path.join(SRC, c + '.png')
                if os.path.exists(p):
                    hit = p
                    break
            if hit is None:
                rows.append((group, label, candidates[0] + '.png', 'MISSING', 0, 0))
                continue
            name = os.path.basename(hit)
            shutil.copy2(hit, os.path.join(OUT, name))
            with Image.open(hit) as im:
                w, h = im.size
            rows.append((group, label, name, 'ok', w, h))

    # prefab 树一起放进来
    for pid, prefix, cls in PREFABS:
        for ext in ('.txt', '.json'):
            src = os.path.join(JP, prefix + ext)
            if os.path.exists(src):
                shutil.copy2(src, os.path.join(OUT, prefix + ext))

    # 索引
    lines = ['# 多人演出 UI 素材（本地 dump，勿提交）', '',
             '来源：日服 7.1.0 全量 sprite 导出 + AssetRipper prefab 树。'
             '这些是官方素材，**仅限本机研究**，不进 git、不进 dist。', '',
             '| 分组 | 用途 | 文件 | 尺寸 |', '|---|---|---|---|']
    for group, label, name, status, w, h in rows:
        size = '%d×%d' % (w, h) if status == 'ok' else '**缺**'
        lines.append('| %s | %s | `%s` | %s |' % (group, label, name, size))
    lines += ['', 'prefab 树：`ml_thumb.txt` / `ml_score.txt` / `ml_skilltext.txt` / `ml_fever.txt`'
              '（同名 .json 是给脚本用的扁平表）。', '',
              '⚠️ 头像块树里每个节点的 `sizeDelta` 都是 0 —— HUD 是 SpriteRenderer + 世界坐标，'
              'prefab 不给像素矩形，尺寸只能按本表里的贴图像素 + 截图量位置（见 HUD-UI-AUDIT.md）。']
    io.open(os.path.join(OUT, 'INDEX.md'), 'w', encoding='utf-8').write('\n'.join(lines))

    # contact sheet
    ok = [r for r in rows if r[3] == 'ok']
    if not ok:
        print('nothing copied')
        return
    cell_w, img_h, label_h = 200, 150, 26
    cols = 6
    rows_n = (len(ok) + cols - 1) // cols
    sheet = Image.new('RGBA', (cell_w * cols, (img_h + label_h) * rows_n), (24, 26, 34, 255))
    dr = ImageDraw.Draw(sheet)
    for i, (group, label, name, status, w, h) in enumerate(ok):
        cx = (i % cols) * cell_w
        cy = (i // cols) * (img_h + label_h)
        with Image.open(os.path.join(OUT, name)) as im:
            im = im.convert('RGBA')
            s = min((cell_w - 16) / max(im.width, 1), (img_h - 16) / max(im.height, 1), 4.0)
            im = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.NEAREST)
            sheet.alpha_composite(im, (cx + (cell_w - im.width) // 2, cy + (img_h - im.height) // 2))
        dr.text((cx + 4, cy + img_h + 4), name[:30], fill=(200, 210, 230, 255))
        dr.text((cx + 4, cy + img_h + 14), '%dx%d' % (w, h), fill=(120, 130, 150, 255))
    sheet.convert('RGB').save(os.path.join(OUT, 'contact_sheet.png'))

    print('copied %d sprites -> %s' % (len(ok), OUT))
    print('missing:', [r[2] for r in rows if r[3] != 'ok'])


if __name__ == '__main__':
    main()
