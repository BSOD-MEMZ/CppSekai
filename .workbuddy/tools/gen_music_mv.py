"""生成 music-mv.json：每首歌有哪些 2D MV 版本。

数据源是 exmeaning 镜像的文件清单（D:/Dev/pjskassets/_files_jp.json）：
  live/2dmode/original_mv/<id4>   -> 原曲 MV（游戏里的 Original，对应 MusicCategory.original）
  live/2dmode/sekai_mv/<id4>      -> セカイ MV（游戏默认那版）

输出直接放在仓库根，chartdl.exe 和游戏都读它（和 music-levels.json 同级）。
格式：{"0001": ["sekai"], "0036": ["original", "sekai"], ...}

usage: python .workbuddy/tools/gen_music_mv.py
"""
import collections
import io
import json
import os
import re

FILES = r'D:/Dev/pjskassets/_files_jp.json'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'music-mv.json')


def main():
    data = json.load(io.open(FILES, encoding='utf-8'))
    mv = collections.defaultdict(set)
    for key in data:
        m = re.match(r'^live/2dmode/(original_mv|sekai_mv)/(\d+)$', key)
        if not m:
            continue
        kind = 'original' if m.group(1) == 'original_mv' else 'sekai'
        mv[m.group(2)].add(kind)

    out = {}
    for song_id in sorted(mv):
        # 固定顺序，diff 稳定
        out[song_id] = [k for k in ('original', 'sekai') if k in mv[song_id]]

    target = os.path.normpath(OUT)
    io.open(target, 'w', encoding='utf-8').write(
        json.dumps(out, ensure_ascii=False, indent=1) + '\n')

    only_original = sum(1 for v in out.values() if v == ['original'])
    only_sekai = sum(1 for v in out.values() if v == ['sekai'])
    both = sum(1 for v in out.values() if len(v) == 2)
    print('songs with MV:', len(out))
    print('  original only:', only_original)
    print('  sekai only   :', only_sekai)
    print('  both         :', both)
    print('->', target)


if __name__ == '__main__':
    main()
