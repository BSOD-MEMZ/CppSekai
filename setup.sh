#!/usr/bin/env bash
# CppSekai one-shot environment setup (Git Bash on Windows).
# Downloads the pinned toolchain (zig 0.14.1 + SDL2 2.32.10) and the game
# assets from the upstream AGPL repo. Nothing here is committed to git.
# Usage:
#   bash setup.sh            # toolchain + assets
#   bash setup.sh --charts   # also download sample charts + BGM (unipjsk)
set -e
cd "$(dirname "$0")"

ZIG_VERSION=0.14.1
ZIG_URL="https://ziglang.org/download/${ZIG_VERSION}/zig-x86_64-windows-${ZIG_VERSION}.zip"
SDL_VERSION=2.32.10
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL2-devel-${SDL_VERSION}-mingw.tar.gz"
UPSTREAM="https://github.com/watagashi-uni/sekai-mmw-preview-web"

mkdir -p toolchain assets

# --- zig (C++ compiler; 0.16+ is broken for this project, keep 0.14.1) ------
if [ ! -f "toolchain/zig014/zig-x86_64-windows-${ZIG_VERSION}/zig.exe" ]; then
    echo "[setup] downloading zig ${ZIG_VERSION}..."
    curl -sL --max-time 600 -o toolchain/zig.zip "$ZIG_URL"
    python -c "import zipfile; zipfile.ZipFile('toolchain/zig.zip').extractall('toolchain/zig014')"
    rm -f toolchain/zig.zip
else
    echo "[setup] zig already present"
fi

# --- SDL2 (MinGW development package) ---------------------------------------
if [ ! -f "toolchain/SDL2-${SDL_VERSION}/x86_64-w64-mingw32/lib/libSDL2.dll.a" ]; then
    echo "[setup] downloading SDL2 ${SDL_VERSION}..."
    curl -sL --max-time 300 -o toolchain/sdl2.tar.gz "$SDL_URL"
    tar -xzf toolchain/sdl2.tar.gz -C toolchain/
    rm -f toolchain/sdl2.tar.gz
else
    echo "[setup] SDL2 already present"
fi

# --- game assets (sprites / SE) from the upstream AGPL repo -----------------
if [ ! -f "assets/mmw/notes_01.png" ]; then
    echo "[setup] downloading game assets from upstream..."
    curl -sL --max-time 300 -o toolchain/upstream.tar.gz "${UPSTREAM}/archive/refs/heads/main.tar.gz"
    tar -xzf toolchain/upstream.tar.gz -C toolchain/
    mkdir -p assets
    cp -r "toolchain/sekai-mmw-preview-web-main/public/assets/mmw" assets/
    rm -rf toolchain/upstream.tar.gz toolchain/sekai-mmw-preview-web-main
else
    echo "[setup] assets already present"
fi

# --- official per-difficulty levels (song select pads) ----------------------
# unipjsk SUS exports strip #PLAYLEVEL, so the numbers come from the game's
# own musicDifficulties table, compacted to {"<musicId>":[e,n,h,ex,m]}.
if [ ! -f "music-levels.json" ]; then
    echo "[setup] downloading official music levels..."
    mkdir -p assets/music
    curl -sL --max-time 300 -o assets/music/musicDifficulties.json \
        "https://viewer-api.unipjsk.com/api/master/1/musicDifficulties"
    python - <<'PY'
import json, collections
order = ["easy", "normal", "hard", "expert", "master"]
rows = json.load(open("assets/music/musicDifficulties.json", encoding="utf-8"))
out = collections.defaultdict(lambda: [0] * 5)
for r in rows:
    d = r.get("musicDifficulty")
    if d in order:
        out[int(r["musicId"])][order.index(d)] = int(r.get("playLevel", 0))
body = ",\n".join('"%d":[%s]' % (k, ",".join(map(str, out[k]))) for k in sorted(out))
open("music-levels.json", "w", encoding="utf-8", newline="\n").write("{\n" + body + "\n}\n")
print("[setup] music-levels.json:", len(out), "songs")
PY
else
    echo "[setup] music-levels.json already present"
fi

# --- optional: sample charts + BGM from unipjsk ------------------------------
if [ "$1" == "--charts" ]; then
    mkdir -p charts
    # Every difficulty, so the song select shows all five pads. The score files
    # are small; the BGM/jacket are shared per song.
    for id in 0075 0127; do
        for diff in easy normal hard expert master; do
            [ -f "charts/${id}_${diff}.sus" ] || curl -s --max-time 60 -o "charts/${id}_${diff}.sus" \
                "https://assets.unipjsk.com/startapp/music/music_score/${id}_01/${diff}"
        done
        [ -f "charts/${id}.mp3" ] || curl -s --max-time 300 -o "charts/${id}.mp3" \
            "https://assets.unipjsk.com/ondemand/music/long/se_${id}_01/se_${id}_01.mp3"
    done
    echo "[setup] charts downloaded (0075 / 0127, all difficulties)"
fi

echo "[setup] done. Next: bash build.sh"
