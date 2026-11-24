#!/usr/bin/env python3
"""同步 music-categories.json（master 的 musicCategories 表）。

原作选曲界面的「絞り込み」弹窗里，MV 那一段（3DMVあり / 2DMVあり / 原曲MVあり /
MVなし）判的就是这张表：`musicCategories` 每首歌若干行，`musicCategoryName` ∈
`{mv, mv_2d, original, image}`（`mv` = 3D MV）。我们只保留前三类，`image`
（只有曲绘）和其它值一律丢掉 —— "MVなし" 就是三类都没有。

输出形状（和 music-mv.json 一样按 4 位 id ？不，这里用原始 id）：

    {"1":["mv"],"2":["mv"],"3":["mv","mv_2d"], ...}

一行紧凑、按 id 升序 —— 仓库里 musics.json / music-mv.json 都是这个写法。

用法
----
    python .workbuddy/tools/gen_music_categories.py           # 同步
    python .workbuddy/tools/gen_music_categories.py --check   # 只报告差集
"""

import argparse
import json
import pathlib
import sys
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[2]
OUT = REPO / "music-categories.json"

# 和 update_music_db.py 同一组源：GitHub 原始地址 + jsDelivr 镜像 + gh-proxy。
# raw.githubusercontent.com 在国内经常整段连不上（RE.md 记过），所以代理排最后。
SOURCES = [
    "https://raw.githubusercontent.com/Sekai-World/sekai-master-db-diff/main/musicCategories.json",
    "https://cdn.jsdelivr.net/gh/Sekai-World/sekai-master-db-diff@main/musicCategories.json",
    "https://gh-proxy.com/https://raw.githubusercontent.com/Sekai-World/sekai-master-db-diff/main/musicCategories.json",
]

# 只收这三种；`image` 是"只有曲绘"，正是 MV なし 的判定依据（丢掉 = 没有 MV）。
KEEP = ("mv", "mv_2d", "original")
ORDER = {name: i for i, name in enumerate(KEEP)}


def fetch():
    last = None
    for url in SOURCES:
        try:
            print(f"[fetch] {url}")
            with urllib.request.urlopen(url, timeout=90) as resp:
                return json.load(resp)
        except Exception as exc:  # noqa: BLE001 - 换下一个源
            print(f"  failed: {exc}")
            last = exc
    raise SystemExit(f"all sources failed: {last}")


def build(rows):
    out = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        music_id = row.get("musicId")
        name = row.get("musicCategoryName")
        if not isinstance(music_id, int) or music_id <= 0 or name not in ORDER:
            continue
        out.setdefault(music_id, set()).add(name)
    return {str(k): sorted(v, key=lambda n: ORDER[n])
            for k, v in sorted(out.items())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只报告与现有文件的差集")
    args = ap.parse_args()

    rows = fetch()
    print(f"[master] {len(rows)} rows")
    data = build(rows)
    print(f"[master] {len(data)} songs, "
          f"3D={sum('mv' in v for v in data.values())} "
          f"2D={sum('mv_2d' in v for v in data.values())} "
          f"original={sum('original' in v for v in data.values())}")

    if args.check:
        old = json.loads(OUT.read_text(encoding="utf-8")) if OUT.exists() else {}
        added = sorted((int(k) for k in data if k not in old))
        removed = sorted((int(k) for k in old if k not in data))
        changed = sorted(int(k) for k in data if k in old and data[k] != old[k])
        print(f"[check] new={len(added)} gone={len(removed)} changed={len(changed)}")
        for tag, ids in (("new", added), ("gone", removed), ("changed", changed)):
            if ids:
                print(f"  {tag}: {ids[:20]}{' ...' if len(ids) > 20 else ''}")
        return

    text = json.dumps(data, ensure_ascii=False, separators=(",", ":")) + "\n"
    OUT.write_text(text, encoding="utf-8")
    print(f"[write] {OUT}  ({len(text)} bytes)")


if __name__ == "__main__":
    sys.exit(main())
