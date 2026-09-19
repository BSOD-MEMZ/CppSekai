#!/usr/bin/env python3
"""CppSekai 素材使用审计：assets/ 里哪些文件游戏**永远不会读**。

清单不是靠 grep 文件名猜的，而是把**加载器**抄下来：游戏能打开的每个素材都在
下面的 spec 里（对应 platform/Renderer.cpp 的 loadSplash/loadAssets/loadHud、
platform/Audio.cpp 的 loadSe/loadUiSe、game/Intro.cpp 的字体表、
game/SongSelect.cpp 的 selectTex、game/TapEffect.cpp、game/StageBackground.cpp、
main.cpp 的直连路径）。贴图/音效都是按 key 缓存加载的，没有 loader 点名 = 死重量。

用法：
  python asset_audit.py [assets_dir] [--list]      # --list 打印每个未用文件
输出：
  ① 未使用清单（按目录分组，带体积）——可以删的
  ② 缺失清单（loader 点名但磁盘上没有）——删多了会在这里露出来
  ③ 用一个假的 exe 目录跑一遍 runtime 校验（见 verify_pruned.sh）留给人做

注意：**未使用 ≠ 应该删**。[仓库里保留] 一律保留：
  - assets/mmw/overlay/**  是 overlay_opt/** 的源图（shrink_hud.cpp 生成 _opt），
    运行时读的是 _opt；删了原图就再也生成不出来了。
  - CREDITS.md/COPYRIGHT.md 里登记的素材要按那两份文档的结论处理。
"""
import fnmatch
import os
import sys

sys.stdout.reconfigure(encoding='utf-8')

# ---------------------------------------------------------------------------
# 加载器清单。r(...) 展开数字区间，{a,b} 展开并列项。
# ---------------------------------------------------------------------------


def expand(spec):
    out = []
    for item in spec:
        if '{' in item:
            head, rest = item.split('{', 1)
            body, tail = rest.split('}', 1)
            for part in body.split(','):
                out.extend(expand([head + part + tail]))
        elif 'r(' in item:
            head, rest = item.split('r(', 1)
            body, tail = rest.split(')', 1)
            lo, hi = body.split('-')
            for n in range(int(lo), int(hi) + 1):
                out.extend(expand([head + str(n) + tail]))
        elif '*' in item:
            # 通配：只看本目录（素材目录都是一层文件，不做递归）
            directory, pattern = os.path.split(item)
            try:
                names = sorted(os.listdir(directory or '.'))
            except OSError:
                names = []
            out.extend([os.path.join(directory, n).replace(os.sep, '/')
                        for n in names if fnmatch.fnmatch(n, pattern)])
        else:
            out.append(item)
    return out


# --- 直接路径（代码里写死的字符串） --------------------------------------
DIRECT = expand([
    'assets/splashscreen.png',
    'assets/ost/BGM_LIVE_RESULT_2.mp3',
    # platform/Renderer.cpp loadSplash()/loadAssets()
    'assets/mmw/background_overlay.png',
    'assets/mmw/stage.png',
    'assets/mmw/notes_01.png',
    'assets/mmw/longNoteLine_01.png',
    'assets/mmw/touchLine_eff_01.png',
    'assets/mmw/effect.png',
    'assets/mmw/ui/close.png', # loadHud: "../ui/close.png"
    # game/TapEffect.cpp（tap_tri_0.png 是**故意不加载**的，见那边的注释）
    'assets/fx/tap_ring.png',
    'assets/fx/tap_tri_1.png',
    'assets/fx/tap_tri_2.png',
])

# --- selectTex()：assets/select/<name>.png --------------------------------
SELECT = expand([
    'assets/select/{search,refresh,indicate_back_new,clear_indicate,fullcombo_indicate,'
    'songlevel,img_smartphone,shufflebutton,musicsetting,level}.png',
])

# --- AudioEngine::loadSe / loadUiSe：assets/se/<name>.mp3 -----------------
SE = expand([
    'assets/se/se_live_{perfect,critical,flick,flick_critical,trace,trace_critical,'
    'connect,connect_critical,long,long_critical}.mp3',
    'assets/se/count_down.mp3',
    'assets/se/{click,select,level_choose,window_open,window_close,start}.mp3',
])

# --- Renderer::loadHud：overlay/ 与 overlay_opt/ 同一批相对路径 -----------
OVERLAY_REL = expand([
    'score/{bg,fg,bar}.png',
    'score/digit/{0,1,2,3,4,5,6,7,8,9,s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,plus,splus,n,sn}.png',
    'score/rank/chr/{d,c,b,a,s}.png',
    'score/rank/txt/{en,jp}/{d,c,b,a,s}.png',
    'combo/{pt,pe}.png',
    'combo/p{0,1,2,3,4,5,6,7,8,9}.png',
    'combo/b{0,1,2,3,4,5,6,7,8,9}.png',
    'life/v3/{bg,normal,danger,overflow}.png',
    'life/v3/digit/{0,1,2,3,4,5,6,7,8,9,s0,s1,s2,s3,s4,s5,s6,s7,s8,s9}.png',
    'judge/v3/{1,2,3,4,5,6}.png',
    'autolive.png',
    'start_grad.png',
    # game/StageBackground.cpp 的舞台底板
    'bggen/v3/{base,bottom,center_cover,center_mask,side_cover,side_mask,windows}.png',
])

HUD = [f'assets/mmw/overlay/{p}' for p in OVERLAY_REL] \
    + [f'assets/mmw/overlay_opt/{p}' for p in OVERLAY_REL]

# --- 手放进来、暂时还没接线的素材 -----------------------------------------
# assets/se/ 整个目录都算"留着"：里面是用户自己加的音效，其中几张（LIVE_CLEAR /
# LIVE_FINISH / touch）现在没有代码点名，但它们是有意留着的储备，不是废弃素材。
# 加到这里的东西永远不会出现在"可以删"清单里。
KEEP = expand([
    'assets/se/*.mp3',
])

USED = set(DIRECT) | set(SELECT) | set(SE) | set(HUD) | set(KEEP)

# 运行时**只读 _opt**（存在就不读原图），所以 overlay/ 里的原图算"运行时不用、
# 但仓库里要留"（_opt 是靠它们生成的）。这条只影响措辞，不影响"未使用"判定。
SOURCE_ONLY_DIRS = ('assets/mmw/overlay/',)


def walk_assets(root):
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            full = os.path.join(dirpath, fn).replace(os.sep, '/')
            yield full, os.path.getsize(os.path.join(dirpath, fn))


def main():
    root = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else 'assets'
    show_list = '--list' in sys.argv
    # --paths: 只打印未使用的路径（一行一个），给删/复制脚本用
    if '--paths' in sys.argv:
        root = root.rstrip('/')
        for p in sorted(walk_assets(root)):
            path = p[0]
            if path not in USED:
                print(path)
        return
    root = root.rstrip('/')

    present = dict(walk_assets(root))
    used_present = {p: s for p, s in present.items() if p in USED}
    unused = {p: s for p, s in present.items() if p not in USED}
    missing = sorted(p for p in USED if p not in present)

    print('=== 汇总 ===')
    print('磁盘上 %d 个文件, %.1f MB' % (len(present), sum(present.values()) / 1e6))
    print('  loader 会用     %3d 个, %.1f MB' % (len(used_present), sum(used_present.values()) / 1e6))
    print('  loader 不碰     %3d 个, %.1f MB' % (len(unused), sum(unused.values()) / 1e6))

    groups = {}
    for p, s in unused.items():
        if p.startswith('assets/mmw/overlay_opt/'):
            key = 'assets/mmw/overlay_opt/<子目录>  (源图在 overlay/，见下)'
            key = 'assets/mmw/overlay_opt/' + p[len('assets/mmw/overlay_opt/'):].split('/')[0]
        else:
            parts = p.split('/')
            key = '/'.join(parts[:3]) if len(parts) > 3 and parts[2] not in ('',) else '/'.join(parts[:-1])
        # 目录聚合：用"文件所在目录"更好读（combo/84 个 vs 每条一行）
        key = os.path.dirname(p)
        groups.setdefault(key, []).append((p, s))

    print()
    print('=== loader 不碰的（按目录） ===')
    for key in sorted(groups, key=lambda k: -sum(s for _, s in groups[k])):
        items = groups[key]
        total = sum(s for _, s in items)
        print('%-46s %3d 个  %7.1f KB' % (key, len(items), total / 1024))
        if show_list:
            for p, s in sorted(items):
                print('      %8.1f KB  %s' % (s / 1024, p))

    if missing:
        print()
        print('=== loader 点名但磁盘上没有（缺文件会静默少一块 UI） ===')
        for p in missing:
            print('  ' + p)
    else:
        print()
        print('=== loader 点名的素材全都在 ===')


if __name__ == '__main__':
    main()
