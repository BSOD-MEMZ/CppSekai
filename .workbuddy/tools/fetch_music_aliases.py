#!/usr/bin/env python3
"""把 HarukiBot 的社区曲目别名表导出成离线文件 music-aliases.json。

为什么是离线表
--------------
别名在 Team-Haruki 的数据库里（Haruki-Cloud 的 `database/pjsk/alias`，用户提交 +
审核），通过公开 API 暴露：

    GET https://neo-api.haruki.seiunx.com/api/bot/v2/pjsk/alias/music/{musicId}
    -> {"data": {"aliases": ["idol", "mmj", "偶像新锐队", "摸啊摸啊", ...]}, "status": 200}

CppSekai 是单机游玩器，**不能把搜索挂到网络上**（断网就废、还有隐私问题），所以
抓一次存成本地表，和 musics.json 一个待遇。

表里有什么
----------
社区梗和译名占多数，正好补上官方表没有的维度：
    "idol"     -> 57  アイドル新鋭隊      "tyw" -> 1  Tell Your World
    "偶像新锐队" -> 57                    "即刻轮回" -> 733 いますぐ輪廻
    "mmj"      -> 57                      "梦开始的地方" -> 1
这些是罗马音折叠（romaji_search.hpp）和拼音都覆盖不到的。

用法
----
    python .workbuddy/tools/fetch_music_aliases.py             # 抓取并写入
    python .workbuddy/tools/fetch_music_aliases.py --check     # 只报告

注意：别名是社区持续补充的，隔一阵重跑一次就好。API 是别人的公共服务，
并发压到 8、失败重试一次，别把人家的站打挂。
"""

import argparse
import concurrent.futures
import json
import pathlib
import sys
import time
import urllib.error
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[2]
API_BASE = "https://neo-api.haruki.seiunx.com/api/bot/v2/pjsk"
OUT_PATH = REPO / "music-aliases.json"

# 并发别开太大：这是别人的公开 API，不是自家的机器。
WORKERS = 8
RETRIES = 1


def fetch_aliases(music_id):
    """返回这一首的别名列表；没有别名（404）时返回空列表。"""
    url = f"{API_BASE}/alias/music/{music_id}"
    for attempt in range(RETRIES + 1):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "CppSekai/1.0"})
            with urllib.request.urlopen(req, timeout=25) as resp:
                payload = json.load(resp)
            data = payload.get("data") or {}
            names = data.get("aliases") or []
            return music_id, [n for n in names if isinstance(n, str) and n.strip()]
        except urllib.error.HTTPError as exc:
            if exc.code == 404:          # 这首没人加过别名，正常
                return music_id, []
            if attempt == RETRIES:
                return music_id, None    # None = 拉取失败，别当成"没有"
            time.sleep(0.4)
        except Exception:  # noqa: BLE001 - 网络抖动是预期内的
            if attempt == RETRIES:
                return music_id, None
            time.sleep(0.4)
    return music_id, None


def worth_keeping(alias, music_id):
    """纯数字别名没意义 —— 输入 id 本来就能搜到，留着白占体积。"""
    stripped = alias.strip()
    if not stripped or stripped.isdigit():
        return False
    return stripped != str(music_id)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只报告，不写文件")
    args = ap.parse_args()

    songs_path = REPO / "musics.json"
    ids = [s["id"] for s in json.loads(songs_path.read_text(encoding="utf-8"))]
    print(f"[info] 曲库 {len(ids)} 首，逐首查询别名（并发 {WORKERS}）…")

    table = {}
    failed = []
    with concurrent.futures.ThreadPoolExecutor(WORKERS) as pool:
        for music_id, aliases in pool.map(fetch_aliases, ids):
            if aliases is None:
                failed.append(music_id)
                continue
            kept = [a for a in aliases if worth_keeping(a, music_id)]
            if kept:
                table[str(music_id)] = sorted(set(kept))

    print(f"[info] 命中 {len(table)} 首，共 {sum(len(v) for v in table.values())} 个别名")
    if failed:
        print(f"[warn] {len(failed)} 首查询失败（会被漏掉，可重跑）：{failed[:12]}")

    old = {}
    if OUT_PATH.exists():
        try:
            old = json.loads(OUT_PATH.read_text(encoding="utf-8"))
        except Exception:  # noqa: BLE001
            old = {}
    added = sorted(set(table) - set(old))
    removed = sorted(set(old) - set(table))
    print(f"[info] 与本地相比：新增 {len(added)} 首、消失 {len(removed)} 首")
    if added[:10]:
        for key in added[:10]:
            print(f"  + {key:<6} {', '.join(table[key][:5])}")

    if args.check:
        print("\n--check：没有写文件。")
        return 0

    OUT_PATH.write_text(json.dumps(table, ensure_ascii=False, separators=(",", ":")) + "\n",
                        encoding="utf-8")
    size_kb = OUT_PATH.stat().st_size / 1024
    print(f"\n[ok] 已写入 {OUT_PATH.name}（{size_kb:.0f} KB）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
