#!/usr/bin/env python3
"""同步 musics.json / music-vocals.json（日服全量 + 国服独占曲）。

为什么需要它
------------
chartdl 的曲库来自本地这两个表。它们不是运行时从网上拉的，所以**表一旧就下不了新歌**：
GUI 列表和 `--download <id>` 都要先在表里查到 `assetbundleName` 才能拼出 URL。
实测（2026-09-19）本地日服表停在 id 804，而当日日服已经到 811 —— 缺 敗走 / ヘレディティ，
两首的资源在 unipjsk 上都是 200，纯属表没跟上。

数据源（都公开）
----------------
  日服  Sekai-World/sekai-master-db-diff        （官方 master DB 镜像，719 首）
        备选 Team-Haruki/haruki-sekai-master    （实时收集，717 首，少两首老歌）
  国服  Team-Haruki/haruki-sekai-sc-master      （实时收集，含 17 首独占曲 11001+）
        备选 Sekai-World/sekai-master-db-cn-diff
  资源  unipjsk 走 assets.unipjsk.com；国服独占曲走 storage.sekai.best/sekai-cn-assets
        （见 downloader/chartdl.cpp 的 URL 函数）

两个坑
------
1. 国服表的 `pronunciation` 对独占曲填的是**作曲者名**（"Mitchie M"、"敌门"），
   官方这批曲子没给读音数据。照抄会毁掉排序（两个界面都按读音排）和罗马音搜索，
   所以下面手工补 KANA_OVERRIDE：中文标题填**拼音**、日文/英文标题填**假名**。
2. 这两个 json 是**单行紧凑格式**，用 json.dump 带缩进重写会得到整文件 diff。
   这里统一按 `separators=(",", ":")` 写，和原文件一致。

用法
----
    python .workbuddy/tools/update_music_db.py            # 同步
    python .workbuddy/tools/update_music_db.py --check    # 只报告差集
"""

import argparse
import json
import pathlib
import sys
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[2]

# 日服曲库：id < CN_ID_MIN。排在前面的源优先，拉失败就换下一个。
JP_SOURCES = [
    ("Sekai-World/sekai-master-db-diff",
     "https://raw.githubusercontent.com/Sekai-World/sekai-master-db-diff/main"),
    ("Team-Haruki/haruki-sekai-master",
     "https://raw.githubusercontent.com/Team-Haruki/haruki-sekai-master/main/master"),
]
# 国服曲库：只取 id >= CN_ID_MIN 的独占曲。
CN_SOURCES = [
    ("Team-Haruki/haruki-sekai-sc-master",
     "https://raw.githubusercontent.com/Team-Haruki/haruki-sekai-sc-master/main/master"),
    ("Sekai-World/sekai-master-db-cn-diff",
     "https://raw.githubusercontent.com/Sekai-World/sekai-master-db-cn-diff/main"),
]

# 国服独占曲的号段。日服表最大 811（2026-09），四位数留给两边共有的曲目；
# chartdl 也拿这个常数分派 CDN，所以新曲 id 别往 10000 以上放。
CN_ID_MIN = 10000

KANA_OVERRIDE = {
    11001: "ぴっくみーあっぷ",
    11002: "まいすてぃーじうぃずゆー",
    11003: "tabuchufa",              # 踏步、出发
    11004: "chulan",                 # 初岚
    11005: "うぃーりーゔざわーるどとぅげざー",
    11006: "jingzhongshaonv",        # 镜中少女
    11007: "mengsexingqiu",          # 梦色星球
    11008: "yiyang",                 # 一样
    11009: "dicaidujishi",           # 低彩度记事
    11010: "yueguohaixianxian",      # 越过海岸线
    11011: "はお",                    # ハオ
    11012: "まえのはなし",            # 前ノハナシ
    11013: "ひまんひだいしょうそうきょく",  # ヒマン=ヒダイ焦燥曲
    11014: "yuexijiangchunlei",      # 月西江·春雷
    11015: "まいまいまい",
    11016: "まじっく",                # Mag1c
    11017: "はいぷだいぶ",            # Hype Dive
}

# 演唱版本名统一用日文：表里另外几百首都这么写，混排时不能两套语言。
VOCAL_CAPTION = {
    "virtual_singer": "バーチャル・シンガーver.",
    "sekai": "セカイver.",
    "another_vocal": "アナザーボーカルver.",
}
# UI 里的显示顺序：先原曲/虚拟歌手，再 セカイ，最后其他。
TYPE_ORDER = {
    "original_song": 0,
    "virtual_singer": 1,
    "sekai": 2,
    "another_vocal": 3,
    "instrumental": 4,
    "streaming_live": 5,
}

# 日服条目的字段顺序，写出去的形状照它。
KEY_ORDER = [
    "id", "seq", "releaseConditionId", "title", "pronunciation", "creatorArtistId",
    "lyricist", "composer", "arranger", "dancerCount", "selfDancerPosition",
    "assetbundleName", "liveTalkBackgroundAssetbundleName", "publishedAt", "releasedAt",
    "liveStageId", "secForMusicScoreMaker", "fillerSec", "isNewlyWrittenMusic",
    "isFullLength", "isAvailableForMusicScoreMaker",
]
# 国服表没有这几个（日服表有），chartdl 也不读它们，补默认值只为两边形状一致。
CN_FALLBACKS = {
    "liveTalkBackgroundAssetbundleName": "bg_livetalk_default_002",
    "liveStageId": 1,
    "secForMusicScoreMaker": 0,
    "isAvailableForMusicScoreMaker": False,
}


def fetch(base, name):
    url = f"{base}/{name}"
    print(f"[fetch] {url}")
    with urllib.request.urlopen(url, timeout=90) as resp:
        return json.load(resp)


def fetch_first(sources, name):
    """按顺序试每个源，第一个成功的胜出（源站偶尔抽风，别为此停下）。"""
    last = None
    for _, base in sources:
        try:
            return fetch(base, name)
        except Exception as exc:  # noqa: BLE001 - 网络失败是预期内的
            last = exc
            print(f"[warn] {base} 拉取失败：{exc}")
    raise RuntimeError(f"{name}: 所有源都失败（最后一个错误：{last}）")


def songs_from(rows, want_cn):
    """挑出属于这一侧的行，按日服的字段顺序整理。"""
    out = []
    for row in rows:
        if (row.get("id", 0) >= CN_ID_MIN) != want_cn:
            continue
        merged = dict(row)
        if want_cn:
            merged.update({k: merged.get(k, v) for k, v in CN_FALLBACKS.items()})
            merged.pop("categories", None)   # 日服表没有这两个字段
            merged.pop("infos", None)
            merged["pronunciation"] = KANA_OVERRIDE.get(merged["id"], merged.get("title", ""))
        out.append({k: merged[k] for k in KEY_ORDER if k in merged})
    return out


def vocals_for(rows, chars=None):
    """官方 musicVocals 行 -> [{id,type,caption,asset,singers}]。

    `chars` 给出 characterId -> 名字（日服有 gameCharacters 表）；
    国服只给了 characterId、拿不到名字，singers 留空。
    """
    out = []
    for row in rows:
        singers = []
        if chars is not None:
            singers = [chars.get(c.get("characterId"), f"#{c.get('characterId')}")
                       for c in row.get("characters", [])]
        out.append({
            "asset": row.get("assetbundleName", ""),
            "caption": VOCAL_CAPTION.get(row.get("musicVocalType", ""), row.get("caption", "")),
            "id": row.get("id", 0),
            "singers": singers,
            "type": row.get("musicVocalType", ""),
        })
    out.sort(key=lambda e: (TYPE_ORDER.get(e["type"], 9), e["id"]))
    return out


def write_compact(path, obj):
    """单行紧凑 JSON —— 和仓库里原文件的形状一致，diff 才不会变成整文件。"""
    path.write_text(json.dumps(obj, ensure_ascii=False, separators=(",", ":")) + "\n",
                    encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只报告差集，不写文件")
    args = ap.parse_args()

    try:
        jp_raw = fetch_first(JP_SOURCES, "musics.json")
        jp_vocals_raw = fetch_first(JP_SOURCES, "musicVocals.json")
        chars = {c["id"]: (c.get("firstName", "") + c.get("givenName", "")).strip()
                 for c in fetch_first(JP_SOURCES, "gameCharacters.json")}
        cn_raw = fetch_first(CN_SOURCES, "musics.json")
        cn_vocals_raw = fetch_first(CN_SOURCES, "musicVocals.json")
    except Exception as exc:  # noqa: BLE001
        print(f"[error] {exc}", file=sys.stderr)
        return 1

    songs_path = REPO / "musics.json"
    vocals_path = REPO / "music-vocals.json"
    old_songs = {s["id"]: s for s in json.loads(songs_path.read_text(encoding="utf-8"))}
    old_vocals = json.loads(vocals_path.read_text(encoding="utf-8"))

    jp_songs = songs_from(jp_raw, want_cn=False)
    cn_songs = songs_from(cn_raw, want_cn=True)
    songs = sorted(jp_songs + cn_songs, key=lambda s: s["id"])

    new_ids = sorted({s["id"] for s in songs} - set(old_songs))
    gone_ids = sorted(set(old_songs) - {s["id"] for s in songs})
    print(f"\n日服 {len(jp_songs)} 首 + 国服独占 {len(cn_songs)} 首 = {len(songs)} 首")
    print(f"新增 {len(new_ids)} 首：")
    for s in songs:
        if s["id"] in new_ids:
            note = ""
            if s["id"] >= CN_ID_MIN and s["id"] not in KANA_OVERRIDE:
                note = "   <-- 读音缺失，退化成曲名，请补进 KANA_OVERRIDE"
            print(f"  + {s['id']:<6} {s['title']:<30} {s['pronunciation']}{note}")
    if gone_ids:
        print(f"表里不再有（会被删掉）{len(gone_ids)} 首：{gone_ids}")

    cn_vocals = {}
    for row in cn_vocals_raw:
        mid = row.get("musicId", 0)
        if mid >= CN_ID_MIN:
            cn_vocals.setdefault(mid, []).append(row)
    vocals = {str(s["id"]): vocals_for(
        [v for v in jp_vocals_raw if v.get("musicId") == s["id"]], chars)
        for s in jp_songs}
    for mid, rows in cn_vocals.items():
        vocals[str(mid)] = vocals_for(rows)

    changed_vocals = [k for k in vocals if old_vocals.get(k) != vocals[k]]
    print(f"\n演唱版本 {len(vocals)} 组，其中 {len(changed_vocals)} 组与本地不同"
          f"（新增演唱版本也算）：{changed_vocals[:12]}")

    if args.check:
        print("\n--check：没有写文件。")
        return 0

    write_compact(songs_path, songs)
    write_compact(vocals_path, vocals)
    print(f"\n[ok] 已写入 {songs_path.name} / {vocals_path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
