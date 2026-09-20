#!/usr/bin/env python3
"""同步 musics.json / music-vocals.json / music-levels.json（日服全量 + 国服独占曲）。

为什么需要它
------------
chartdl 的曲库来自本地这几个表。它们不是运行时从网上拉的，所以**表一旧就下不了新歌**：
GUI 列表和 `--download <id>` 都要先在表里查到 `assetbundleName` 才能拼出 URL。
实测（2026-09-19）本地日服表停在 id 804，而当日日服已经到 811 —— 缺 敗走 / ヘレディティ，
两首的资源在 unipjsk 上都是 200，纯属表没跟上。

`music-levels.json`（难度定数）同样要一起同步，而且**缺它的后果比缺歌更阴**：
- 游戏侧：选曲界面那一列定数显示 `-`；
- chartdl 侧：定数的有无被当成「这首有没有这个难度」的判据，17 首国服独占曲
  在这张表里一行都没有 → 5 个难度框全被灰掉，点排队只能下到曲绘和 BGM。
  （2026-09-20 报的这个 bug。chartdl 那边已经改成「表里没这行 = 未知 = 照常可勾」，
  所以这里慢一拍不再致命，但表还是得跟上。）

数据源（都公开）
----------------
  日服  Sekai-World/sekai-master-db-diff        （官方 master DB 镜像，719 首）
        备选 Team-Haruki/haruki-sekai-master    （实时收集，717 首，少两首老歌）
  国服  Team-Haruki/haruki-sekai-sc-master      （实时收集，含 17 首独占曲 11001+）
        备选 Sekai-World/sekai-master-db-cn-diff
  定数  上面两边的 musicDifficulties.json（日服管 id < 10000，国服管独占号段）
  资源  unipjsk 走 assets.unipjsk.com；国服独占曲走 storage.sekai.best/sekai-cn-assets
        （见 downloader/chartdl.cpp 的 URL 函数）
  每个仓库都同时给 raw.githubusercontent.com 和 jsDelivr 两条地址：国内直连 raw
  经常整段不通（2026-09-20 实测超时），只留它等于没法重新同步。

三个坑
------
1. 国服表的 `pronunciation` 对独占曲填的是**作曲者名**（"Mitchie M"、"敌门"），
   官方这批曲子没给读音数据。照抄会毁掉排序（两个界面都按读音排）和罗马音搜索，
   所以下面手工补 KANA_OVERRIDE：中文标题填**拼音**、日文/英文标题填**假名**。
2. musics / music-vocals 这两个 json 是**单行紧凑格式**，用 json.dump 带缩进重写会得到
   整文件 diff。这里统一按 `separators=(",", ":")` 写，和原文件一致。
3. music-levels.json 反过来是**一行一首**的格式，且按 id 升序 —— 它也有自己的 writer
   （write_levels），别顺手改成一行。

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
#
# 每个仓库都给两条地址：GitHub 原始地址 + jsDelivr 镜像（/gh/ 前缀是同一份内容）。
# raw.githubusercontent.com 在国内经常整段连不上（2026-09-20 实测直接超时），
# 只留它的话「表旧了重新同步一次」根本跑不起来。
def _mirrors(owner_repo, subdir=""):
    """同一个仓库的两条取数地址；haruki 系列的 master 表在仓库的 master/ 子目录下。"""
    tail = f"/{subdir}" if subdir else ""
    return [
        (owner_repo, f"https://raw.githubusercontent.com/{owner_repo}/main{tail}"),
        (f"{owner_repo} (jsDelivr)", f"https://cdn.jsdelivr.net/gh/{owner_repo}@main{tail}"),
    ]


JP_SOURCES = [
    *_mirrors("Sekai-World/sekai-master-db-diff"),
    *_mirrors("Team-Haruki/haruki-sekai-master", "master"),
]
# 国服曲库：只取 id >= CN_ID_MIN 的独占曲。
CN_SOURCES = [
    *_mirrors("Team-Haruki/haruki-sekai-sc-master", "master"),
    *_mirrors("Sekai-World/sekai-master-db-cn-diff"),
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


# 难度顺序 = music-levels.json 里那个五元组的顺序，也是 chartdl 的 kDiffNames。
DIFF_ORDER = ["easy", "normal", "hard", "expert", "master"]


def levels_from(rows):
    """musicDifficulties 行 -> {musicId: [easy, normal, hard, expert, master]}。

    该难度没有定数（表里没这行 / playLevel 是 0）就留 0，和原表一致；两个界面都用
    «> 0» 判断「有没有这一档」。
    """
    out = {}
    for row in rows:
        mid = row.get("musicId", 0)
        diff = row.get("musicDifficulty", "")
        level = row.get("playLevel", 0)
        if mid <= 0 or diff not in DIFF_ORDER or not isinstance(level, int) or level <= 0:
            continue
        out.setdefault(mid, [0] * 5)[DIFF_ORDER.index(diff)] = level
    return out


def write_levels(path, levels):
    """一行一首、按 id 升序 —— 这是 music-levels.json 原文件的形状（不是单行紧凑）。

    写成 json.dump(indent=1) 之类会得到整文件 diff；这里手工拼，只让真正变化的行出现。
    """
    entries = sorted(levels.items())
    body = ",\n".join(f'"{mid}":[{",".join(str(v) for v in lv)}]' for mid, lv in entries)
    path.write_text("{\n" + body + "\n}\n", encoding="utf-8")


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
        jp_diff_raw = fetch_first(JP_SOURCES, "musicDifficulties.json")
        cn_diff_raw = fetch_first(CN_SOURCES, "musicDifficulties.json")
    except Exception as exc:  # noqa: BLE001
        print(f"[error] {exc}", file=sys.stderr)
        return 1

    songs_path = REPO / "musics.json"
    vocals_path = REPO / "music-vocals.json"
    levels_path = REPO / "music-levels.json"
    old_songs = {s["id"]: s for s in json.loads(songs_path.read_text(encoding="utf-8"))}
    old_vocals = json.loads(vocals_path.read_text(encoding="utf-8"))
    old_levels = {int(k): v for k, v in json.loads(levels_path.read_text(encoding="utf-8")).items()}

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

    # 定数表：日服那份管 id < 10000，国服那份管独占号段（两边表都含对方的曲子，
    # 所以只按号段合并，别整份覆盖）。只保留 musics.json 里真有的 id —— 表跑到
    # 歌单前面去没有意义，而且会让 chartdl 的「有没有这行」判断失去意义。
    levels = {mid: lv for mid, lv in levels_from(jp_diff_raw).items() if mid < CN_ID_MIN}
    levels.update({mid: lv for mid, lv in levels_from(cn_diff_raw).items() if mid >= CN_ID_MIN})
    known_ids = {s["id"] for s in songs}
    dropped = sorted(mid for mid in levels if mid not in known_ids)
    levels = {mid: lv for mid, lv in levels.items() if mid in known_ids}

    new_levels = sorted(set(levels) - set(old_levels))
    gone_levels = sorted(set(old_levels) - set(levels))
    changed_levels = sorted(mid for mid in old_levels
                            if mid in levels and old_levels[mid] != levels[mid])
    print(f"\n定数表 {len(levels)} 首（本地 {len(old_levels)} 首）："
          f"新增 {len(new_levels)} / 变化 {len(changed_levels)}")
    for mid in new_levels:
        title = next((s["title"] for s in songs if s["id"] == mid), "?")
        print(f"  + {mid:<6} {title:<30} {levels[mid]}")
    for mid in changed_levels[:12]:
        print(f"  ~ {mid:<6} {old_levels[mid]} -> {levels[mid]}")
    if gone_levels:
        print(f"  表里不再有（会被删掉）{len(gone_levels)} 首：{gone_levels[:12]}")
    if dropped:
        print(f"  （官方表里有、歌单里没有的 {len(dropped)} 首已丢弃：{dropped[:12]}）")

    if args.check:
        print("\n--check：没有写文件。")
        return 0

    write_compact(songs_path, songs)
    write_compact(vocals_path, vocals)
    write_levels(levels_path, levels)
    print(f"\n[ok] 已写入 {songs_path.name} / {vocals_path.name} / {levels_path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
