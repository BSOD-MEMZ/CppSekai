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

# --- optional: sample charts + BGM from unipjsk ------------------------------
if [ "$1" == "--charts" ]; then
    mkdir -p charts
    for id in 0075 0127; do
        [ -f "charts/${id}_master.sus" ] || curl -s --max-time 60 -o "charts/${id}_master.sus" \
            "https://assets.unipjsk.com/startapp/music/music_score/${id}_01/master"
        [ -f "charts/${id}.mp3" ] || curl -s --max-time 300 -o "charts/${id}.mp3" \
            "https://assets.unipjsk.com/ondemand/music/long/se_${id}_01/se_${id}_01.mp3"
    done
    echo "[setup] charts downloaded (0075 / 0127)"
fi

echo "[setup] done. Next: bash build.sh"
