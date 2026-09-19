#!/usr/bin/env python3
"""把国服（简中服）独占曲同步进仓库的 musics.json / music-vocals.json。

为什么需要它
------------
chartdl 的曲库来自本地 musics.json（日服表，715 首）。日服进度快于国服，
所以日服表已经覆盖了绝大部分曲目；缺的只有国服**独占**的那几首 ——
它们的 id 走独立号段（11001 起），日服表里根本没有。

数据源（都是公开的）
--------------------
  曲库  https://raw.githubusercontent.com/Sekai-World/sekai-master-db-cn-diff/main/musics.json
  演唱  https://raw.githubusercontent.com/Sekai-World/sekai-master-db-cn-diff/main/musicVocals.json
  资源  https://storage.sekai.best/sekai-cn-assets/  (S3 bucket, 见 chartdl 的 URL 函数)

两个坑
------
1. 国服表的 `pronunciation` 对独占曲填的是**作曲者名**（"Mitchie M"、"敌门"），
   不是读音 —— 官方这批曲子就没给读音数据。直接抄进来的话排序会乱
   （chartdl 按读音排）而且罗马音搜索会失效。所以这里手工补 KANA_OVERRIDE。
2. 中文标题的曲目（初岚 / 一样 / 低彩度记事…）没有日语读音，填**拼音**：
   排序按 latin 序、搜索时拼音能直接命中（见 romaji_search.hpp 的第二个 key）。

用法
----
    python .workbuddy/tools/update_cn_music.py            # 联网拉最新表并同步
    python .workbuddy/tools/update_cn_music.py --check    # 只报告差集，不写文件
"""

import argparse
import json
import pathlib
import sys
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[2]
CN_BASE = "https://raw.githubusercontent.com/Sekai-World/sekai-master-db-cn-diff/main"
# 国服独占曲的号段：日服 id 目前最大 804，四位数留给两边共有的曲目。
CN_ID_MIN = 10000

# 官方表里 pronunciation 是作曲者名，这里补上真正的读音。
# 中文标题 → 拼音（不带声调，方便直接输入搜索）；日文/英文标题 → 假名。
KANA_OVERRIDE = {
    11001: "ぴっくみーあっぷ",
    11002: "まいすてーじうぃずゆー",
    11003: "tabuchufa",              # 踏步、出发
    11004: "chulan",                 # 初岚
    11005: "うぃーりーゔざわーるどとぅげざー",
    11006: "jingzhongshaonv",        # 镜中少女
    11007: "mengsexingqiu",          # 梦色星球
    11008: "yiyang",                 # 一样
    11009: "dicaidujishi",          # 低彩度记事
    11010: "yueguohaixianxian",      # 越过海岸线
    11011: "はお",                    # ハオ
    11012: "まえのはなし",            # 前ノハナシ
    11013: "ひまんひだいしょうそうきょく",  # ヒマン=ヒダイ焦燥曲
    11014: "yuexijiangchunlei",      # 月西江·春雷
    11015: "まいまいまい",
    11016: "まじっく",                # Mag1c
    11017: "はいぷだいぶ",            # Hype Dive
}

# 演唱版本名统一用日文（表里另外 715 首都这么写，混排时不能两套语言）。
VOCAL_CAPTION = {
    "virtual_singer": "バーチャル・シンガーver.",
    "sekai": "セカイver.",
    "another_vocal": "アナザーボーカルver.",
}

# 国服表没有这几个字段（日服表有），chartdl 也不读，给个合理默认值
# 只是为了两条 json 的形状一致。
FALLBACKS = {
    "liveTalkBackgroundAssetbundleName": "bg_livetalk_default_002",
    "liveStageId": 1,
    "secForMusicScoreMaker": 0,
    "isAvailableForMusicScoreMaker": False,
}

# 日服条目的字段顺序，新条目照排。
KEY_ORDER = [
    "id", "seq", "releaseConditionId", "title", "pronunciation", "creatorArtistId",
    "lyricist", "composer", "arranger", "dancerCount", "selfDancerPosition",
    "assetbundleName", "liveTalkBackgroundAssetbundleName", "publishedAt", "releasedAt",
    "liveStageId", "secForMusicScoreMaker", "fillerSec", "isNewlyWrittenMusic",
    "isFullLength", "isAvailableForMusicScoreMaker",
]


def fetch(name):
    url = f"{CN_BASE}/{name}"
    print(f"[fetch] {url}")
    with urllib.request.urlopen(url, timeout=60) as resp:
        return json.load(resp)


def to_repo_song(cn_row):
    """国服条目 -> 仓库条目（按日服表的字段顺序，缺的补默认值）。"""
    merged = dict(cn_row)
    merged.update({k: merged.get(k, v) for k, v in FALLBACKS.items()})
    merged.pop("categories", None)   # 日服表没有这个字段
    merged.pop("infos", None)
    merged["pronunciation"] = KANA_OVERRIDE.get(merged["id"], merged.get("title", ""))
    return {k: merged[k] for k in KEY_ORDER if k in merged}


def to_repo_vocals(rows):
    """国服演唱条目 -> 仓库格式 {asset, caption, id, singers, type}。"""
    out = []
    for row in rows:
        out.append({
            "asset": row.get("assetbundleName", ""),
            "caption": VOCAL_CAPTION.get(row.get("musicVocalType", ""), row.get("caption", "")),
            "id": row.get("id", 0),
            "singers": [],   # 国服只给了 characterId，拿不到具体名字，留空
            "type": row.get("musicVocalType", ""),
        })
    return out


def append_json_array(path, new_items):
    """往一个紧凑（单行）JSON 数组的末尾追加条目。

    仓库里这两个表是 nlohmann 风格的单行 JSON，用 json.dump 整体重写会得到
    "删一行加一行"的 1MB diff。这里只动收尾的 ']'，新增内容直接拼进去。
    """
    raw = path.read_text(encoding="utf-8")
    stripped = raw.rstrip()
    trailing = raw[len(stripped):]
    end = stripped.rfind("]")
    head = stripped[:end]
    payload = ",".join(json.dumps(it, ensure_ascii=False, separators=(",", ":"))
                       for it in new_items)
    path.write_text(head + "," + payload + "]" + trailing, encoding="utf-8")


def append_json_object(path, pairs):
    """同上，但对象里追加 "key":value（music-vocals.json 按 musicId 分键）。"""
    raw = path.read_text(encoding="utf-8")
    stripped = raw.rstrip()
    trailing = raw[len(stripped):]
    end = stripped.rfind("}")
    head = stripped[:end]
    payload = ",".join(
        '"%s":%s' % (key, json.dumps(value, ensure_ascii=False, separators=(",", ":")))
        for key, value in pairs)
    path.write_text(head + "," + payload + "}" + trailing, encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只报告差集，不写文件")
    args = ap.parse_args()

    try:
        cn_musics = fetch("musics.json")
        cn_vocals = fetch("musicVocals.json")
    except Exception as exc:  # noqa: BLE001 - 网络问题是预期内的失败
        print(f"[error] 拉取国服表失败：{exc}", file=sys.stderr)
        return 1

    songs_path = REPO / "musics.json"
    vocals_path = REPO / "music-vocals.json"
    songs = json.loads(songs_path.read_text(encoding="utf-8"))
    vocals = json.loads(vocals_path.read_text(encoding="utf-8"))

    have_ids = {s["id"] for s in songs}
    new_rows = [s for s in cn_musics if s["id"] >= CN_ID_MIN and s["id"] not in have_ids]

    vocal_by_id = {}
    for row in cn_vocals:
        mid = row.get("musicId", 0)
        if mid >= CN_ID_MIN:
            vocal_by_id.setdefault(mid, []).append(row)

    print(f"\n国服独占曲 {len([s for s in cn_musics if s['id'] >= CN_ID_MIN])} 首，"
          f"其中 {len(new_rows)} 首本地还没有\n")
    for row in new_rows:
        kana = KANA_OVERRIDE.get(row["id"])
        flag = "" if kana else "   <-- 读音缺失！"
        print(f"  {row['id']}  {row['title']:<28} {row.get('pronunciation', ''):<20}{flag}")

    missing_kana = [r["id"] for r in new_rows if r["id"] not in KANA_OVERRIDE]
    if missing_kana:
        print(f"\n[warn] 这些 id 没在 KANA_OVERRIDE 里，读音会退化成曲名：{missing_kana}")
        print("       国服出新曲时把它们补进脚本再跑一次。")

    if args.check:
        print("\n--check：没有写文件。")
        return 0
    if not new_rows:
        print("\n本地已是最新，无需写入。")
        return 0

    # 独占曲的 id 是 11001+，最大，所以追加到末尾天然有序，不用整表重排。
    append_json_array(songs_path, [to_repo_song(r) for r in new_rows])
    append_json_object(vocals_path, [
        (str(row["id"]), to_repo_vocals(vocal_by_id.get(row["id"], []))) for row in new_rows
    ])
    print(f"\n[ok] musics.json {len(songs) + len(new_rows)} 首、"
          f"music-vocals.json {len(vocals) + len(new_rows)} 组")
    return 0


if __name__ == "__main__":
    sys.exit(main())
