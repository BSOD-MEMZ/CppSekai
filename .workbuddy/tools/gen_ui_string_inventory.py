"""Build UI-STRINGS.md: every source string that ends up on screen, by screen.

Reuses extract_ui_strings.py for tokenising; the (file, line range, title) table
below is the hand-curated part - ranges were read off the current tree, so a
line number that drifted just means the section moved, not that the string
vanished.
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
spec = importlib.util.spec_from_file_location('ex', os.path.join(HERE, 'extract_ui_strings.py'))
ex = importlib.util.module_from_spec(spec)
sys.modules['ex'] = ex
spec.loader.exec_module(ex)

# (file, lo, hi, heading)
SECTIONS = [
    ('main.cpp', 100, 140, '打开谱面 · 文件对话框'),
    ('main.cpp', 590, 670, '命令行 --help（面向高级用户，一般不进翻译）'),
    ('main.cpp', 2330, 2350, '启动失败 · OpenGL 版本过低'),
    ('main.cpp', 2405, 2430, '启动失败 · OpenGL 初始化失败'),
    ('main.cpp', 3510, 3525, '窗口标题 · 谱面下载器'),
    ('main.cpp', 3830, 3840, '多人游玩 · 暂停提示'),
    ('main.cpp', 4065, 4075, '谱面加载失败'),
    ('main.cpp', 4540, 4550, '未设置昵称'),
    ('main.cpp', 4610, 4630, '歌手版本标签'),
    ('main.cpp', 4690, 4700, '设置 · 页签名'),
    ('main.cpp', 4800, 5300, '设置 · 演奏 / 画面 / 外观'),
    ('main.cpp', 5360, 5700, '设置 · 判定 / 系统'),
    ('main.cpp', 5700, 5750, '设置 · 诊断（日志分类 / 结束实例）'),
    ('main.cpp', 5810, 5965, '设置 · 账户（用户 / 导入导出）'),
    ('main.cpp', 5970, 6120, '设置 · 关于'),
    ('main.cpp', 6150, 6520, '键位设置 / 多开确认'),
    ('main.cpp', 6550, 6570, '退出确认'),
    ('main.cpp', 6690, 6715, '删除歌曲 · 文件统计'),
    ('main.cpp', 6740, 6760, '删除歌曲确认'),
    ('main.cpp', 6810, 6885, '导入用户数据'),
    ('main.cpp', 6885, 6900, '多人游玩已开启 · 重启提示'),
    ('main.cpp', 8770, 8785, '关闭所有实例确认'),
    ('main.cpp', 9495, 9530, '多人 · 等待 / 选曲状态'),
    ('main.cpp', 9755, 9770, '谱面下载器缺失'),
    ('main.cpp', 9835, 9965, '多人 · 房主状态 / 加载失败'),
    ('main.cpp', 10180, 10195, '多人 · 房主放弃'),
    ('main.cpp', 11025, 11040, '多人 · 房主暂停'),
    ('main.cpp', 11135, 11170, '多人 · 继续 / 放弃 / 重试'),
    ('main.cpp', 11390, 11405, '关于本软件'),
    ('game/SongSelect.cpp', 500, 535, '选曲 · 版本标签'),
    ('game/SongSelect.cpp', 1080, 1090, '选曲 · 默认用户'),
    ('game/SongSelect.cpp', 1895, 1985, '选曲 · 五十音 / 分组标签'),
    ('game/SongSelect.cpp', 2060, 2075, '选曲 · 分组「其他」'),
    ('game/SongSelect.cpp', 2915, 2935, '选曲 · 版本分类'),
    ('game/SongSelect.cpp', 3595, 3660, '选曲 · 搜索框 / 右键菜单'),
    ('game/SongSelect.cpp', 3685, 3695, '选曲 · 排序 / 分组下拉'),
    ('game/SongSelect.cpp', 3785, 3820, '选曲 · 顶栏按钮'),
    ('game/SongSelect.cpp', 4120, 4145, '选曲 · 收藏 / 删除谱面'),
    ('game/SongSelect.cpp', 4265, 4295, '选曲 · 空列表 / 下载谱面'),
    ('game/SongSelect.cpp', 5340, 5350, '选曲 · 切换歌手'),
    ('game/SongSelect.cpp', 5500, 5520, '选曲 · 无其他演唱版本'),
    ('game/SongSelect.cpp', 5555, 5625, '选曲 · 个人资料卡'),
    ('game/SongSelect.cpp', 5670, 5825, '选曲 · 猜歌'),
    ('platform/NativeMenu.cpp', 1, 9999, '原生菜单栏'),
    ('game/Intro.cpp', 715, 735, '开头卡片 · 曲目信息'),
    ('game/PartyScreen.cpp', 160, 190, '多人 · 玩家名条'),
    ('game/Result.cpp', 1070, 1105, '结算 · 分数 / 最高分'),
    ('game/Result.cpp', 990, 1000, '结算 · 歌曲等级'),
    ('game/Result.cpp', 1220, 1275, '结算 · 升级 / 继续'),
    ('game/Story.cpp', 60, 70, '教学 · 命名提示'),
    ('game/Story.cpp', 840, 850, '教学 · 完成按钮'),
    ('game/Story.cpp', 910, 925, '教学 · 名字长度提示'),
    ('game/StoryUi.cpp', 895, 910, '教学 · 操作提示'),
    ('game/StoryUi.cpp', 955, 970, '教学 · 图片缺失'),
    ('game/Ui.cpp', 2010, 2020, '通用控件 · 等级'),
    ('game/SongSelect.cpp', 4405, 4460, '选曲 · 曲目信息'),
    ('game/SongSelect.cpp', 5110, 5130, '选曲 · 确定'),
    ('game/SongSelect.cpp', 5620, 5630, '选曲 · 关闭'),
    ('main.cpp', 1925, 1935, '导入 · 用户'),
    ('main.cpp', 1985, 1995, '导入 · 玩家'),
    ('main.cpp', 3080, 3090, '选曲 · 收藏 / 最近播放'),
    ('main.cpp', 4265, 4290, '设置 · 判定预设名'),
    ('main.cpp', 4375, 4385, '打开用户数据 · 文件对话框'),
    ('main.cpp', 4565, 4610, '角色 / 团体名对照表（数据，中日双名已内置，不需翻译）'),
    ('main.cpp', 5775, 5790, '设置 · 账户 / 新建用户'),
    ('main.cpp', 9525, 9540, '多人 · 准备中 / 加载中'),
    ('main.cpp', 10115, 10130, '多人 · 状态提示'),
    ('platform/Party.cpp', 660, 675, '多人 · 玩家'),
    ('platform/ShellIntegration.cpp', 10, 25, '系统集成 · 文件类型名'),
    ('platform/ChartDlGuide.cpp', 20, 30, '下载器 · 窗口标题匹配'),
    ('platform/ChartDlGuide.cpp', 250, 260, '下载器 · 状态栏文字匹配（改了就认不出来，不要翻译）'),
    ('downloader/chartdl.cpp', 1196, 1240, '下载器 · --help'),
    ('downloader/chartdl.cpp', 1475, 1490, '下载器 · 列表列名'),
    ('downloader/chartdl.cpp', 2185, 2195, '下载器 · 窗口标题'),
    ('downloader/chartdl.cpp', 3175, 3300, '下载器 · 详情面板'),
    ('downloader/chartdl.cpp', 3515, 3525, '下载器 · 窗口标题'),
    ('downloader/chartdl.cpp', 1170, 1200, '下载器 · 剩余时间'),
    ('downloader/chartdl.cpp', 1715, 1740, '下载器 · 列表列'),
    ('downloader/chartdl.cpp', 1900, 1920, '下载器 · 删除结果'),
    ('downloader/chartdl.cpp', 2255, 2300, '下载器 · 设置页 / 致谢'),
    ('downloader/chartdl.cpp', 2360, 2575, '下载器 · 主窗口控件'),
    ('downloader/chartdl.cpp', 2640, 2700, '下载器 · 托盘 / 气泡'),
    ('downloader/chartdl.cpp', 2740, 2810, '下载器 · 删除确认 / 提示'),
    ('downloader/chartdl.cpp', 2835, 2860, '下载器 · 下载队列状态'),
    ('downloader/chartdl.cpp', 2930, 2950, '下载器 · 已下载完整'),
    ('downloader/chartdl.cpp', 3020, 3070, '下载器 · 进度 / 完成'),
    ('downloader/chartdl.cpp', 3310, 3400, '下载器 · 详情面板'),
]

# Data, not UI: font families and song titles used as test fixtures. Character
# and unit names are kept on purpose (section 4565-4610) - they ship as a
# ja/zh pair already, so they need no translation, only awareness.
DROP_TEXT = {
    '微软雅黑', '宋体', '黑体', 'Malgun Gothic', 'Microsoft JhengHei', '微軟正黑體',
    'Noto Sans JP', 'Noto Sans SC', 'Microsoft YaHei', 'Microsoft YaHei UI', 'MS Gothic',
    'Meiryo UI', 'Yu Gothic', 'Yu Gothic UI', 'MS UI Gothic', 'Segoe UI',
    '1000年生きてる', 'みく', 'リン', 'ルカ', 'レン',
}


def collect():
    files = []
    for root in ('main.cpp', 'game', 'platform', 'downloader'):
        full = os.path.join(ROOT, root)
        if os.path.isfile(full):
            files.append(full)
        else:
            for dp, _d, names in os.walk(full):
                for nm in names:
                    if nm.endswith(('.cpp', '.hpp')):
                        files.append(os.path.join(dp, nm))
    rows = []
    for path in files:
        rel = os.path.relpath(path, ROOT).replace('\\', '/')
        src = open(path, encoding='utf-8', errors='replace').read()
        for line_no, offset, value in ex.tokens(src):
            call = ex.enclosing_call(src, offset)
            if call.split('::')[-1] in ex.LOG_CALLS:
                continue
            if not ex.keep(value, call.split('::')[-1]):
                continue
            text = value.strip()
            if not text or text in DROP_TEXT:
                continue
            if not any(ord(c) > 127 for c in text):
                continue  # ASCII-only: keys / gl names / log leftovers
            if text in ('…', '·', '、', '「', '」', '（', '）', '#'):
                continue
            rows.append((rel, line_no, text))
    return rows


def main():
    rows = collect()
    used = set()
    out = []
    total = 0
    for path, lo, hi, head in SECTIONS:
        hits = [r for r in rows if r[0] == path and lo <= r[1] <= hi]
        if not hits:
            continue
        for r in hits:
            used.add((r[0], r[1]))
        out.append('\n### %s\n' % head)
        out.append('| 文件:行 | 原文 |')
        out.append('|---|---|')
        for _p, line_no, text in sorted(hits, key=lambda r: r[1]):
            total += 1
            out.append('| `%s:%d` | %s |' % (path, line_no, text.replace('|', '\\|')))
    rest = [r for r in rows if (r[0], r[1]) not in used]
    if rest:
        out.append('\n### 其他（未归类，需人工确认）\n')
        out.append('| 文件:行 | 原文 |')
        out.append('|---|---|')
        for path, line_no, text in sorted(rest):
            total += 1
            out.append('| `%s:%d` | %s |' % (path, line_no, text.replace('|', '\\|')))

    head = HEADER.replace('@TOTAL@', str(total)).replace('@SECTIONS@', str(len(SECTIONS)))
    body = '\n'.join(out)
    with open(os.path.join(ROOT, 'UI-STRINGS.md'), 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(head + body + '\n')
    print('wrote UI-STRINGS.md: %d entries, %d unclassified' % (total, len(rest)), file=sys.stderr)


HEADER = """# UI-STRINGS.md — 会显示给用户的字符串清单

> 口径：**只收会出现在屏幕上、需要翻译的字符串**（截至 2026-11-18 的源码）。
> 由 `.workbuddy/tools/gen_ui_string_inventory.py` 生成，改了 UI 文字后重跑即可：
> `python .workbuddy/tools/gen_ui_string_inventory.py`
>
> **不收**：日志（`platform::logf` / `printf` / `appendLog` …）、内部错误文本、
> 资源路径与 json 键名、贴图里画好的字（`PERFECT` / `FULL COMBO` / `LIVE RESULT`
> 都是 sprite，源码里没有对应字符串）、以及数据表内容（`musics.json` 歌名、
> `music-aliases.json` 别名、`assets/story/scripts/*.json` 剧情台词）。
>
> 共 **@TOTAL@** 条，分 **@SECTIONS@** 个区块。行号会随代码漂移，区块标题比行号可靠。
>
> ⚠️ 两块**不要翻译**：
> 1. `platform/ChartDlGuide.cpp` 的「全部完成 / 完成，失败 / 下载完成 / 已经下载完整」——
>    那是用来**匹配**下载器状态栏文字的，翻了就认不出进度。
> 2. `main.cpp` 的角色 / 团体名对照表——中日双名本来就内置，属于数据不是界面文案。
"""



if __name__ == '__main__':
    main()
