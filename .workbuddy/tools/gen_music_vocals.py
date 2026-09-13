#!/usr/bin/env python3
"""把官方的 musicVocals + gameCharacters 表压成 CppSekai 用的 music-vocals.json。

    python .workbuddy/tools/gen_music_vocals.py [输出路径]

输出（键 = 曲目 id，紧凑数组，顺序就是 UI 里的显示顺序）：

    {
      "374": [
        {"id": 1051, "type": "sekai", "caption": "セカイver.",
         "asset": "se_0374_01", "singers": ["花里みのり", "鏡音レン", ...]},
        ...
      ]
    }

`asset` 就是游戏资源包名，也就是 unipjsk 的音频路径：
    https://assets.unipjsk.com/ondemand/music/long/<asset>/<asset>.mp3
（CppSekai 侧按 `charts/<asset>.mp3` 找文件。）

数据源是 Sekai-World/sekai-master-db-diff（官方 master DB 的镜像）。
"""
import json
import sys
import urllib.request

BASE = "https://raw.githubusercontent.com/Sekai-World/sekai-master-db-diff/main"
# 显示顺序：先原曲/虚拟歌手，再 セカイ，最后其他（另一人声、演唱会…）
TYPE_ORDER = {
    "original_song": 0,
    "virtual_singer": 1,
    "sekai": 2,
    "another_vocal": 3,
    "instrumental": 4,
}


def fetch(name):
    with urllib.request.urlopen(f"{BASE}/{name}", timeout=60) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "music-vocals.json"

    vocals = fetch("musicVocals.json")
    chars = {c["id"]: (c.get("firstName", "") + c.get("givenName", "")).strip()
             for c in fetch("gameCharacters.json")}

    by_song = {}
    for row in vocals:
        singers = [chars.get(c["characterId"], f"#{c['characterId']}")
                   for c in row.get("characters", [])]
        by_song.setdefault(row["musicId"], []).append({
            "id": row["id"],
            "type": row["musicVocalType"],
            "caption": row["caption"],
            "asset": row["assetbundleName"],
            "singers": singers,
        })

    for entries in by_song.values():
        entries.sort(key=lambda e: (TYPE_ORDER.get(e["type"], 9), e["id"]))

    out = {str(k): v for k, v in sorted(by_song.items())}
    with open(out_path, "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False, separators=(",", ":"))
        fh.write("\n")
    total = sum(len(v) for v in out.values())
    print(f"wrote {out_path}: {len(out)} songs, {total} vocal versions")


if __name__ == "__main__":
    main()
