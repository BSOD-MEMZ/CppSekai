#!/usr/bin/env bash
# Assembles the files a GitHub release should carry, and zips them.
#
#   bash package.sh [version]            # default version: the date
#   bash package.sh --no-assets [version]  # lean build (see below)
#
# What goes in, and what deliberately does not, is documented in README.md
# ("发布 / 打包"). Short version: the executables, SDL2.dll, the official
# *factual* data tables (titles / readings / levels / vocal versions) and the
# documents. The charts are NOT included - they are official game assets,
# and COPYRIGHT.md forbids redistributing them; the release notes tell the
# user to grab songs with chartdl.exe.
#
# assets/ IS bundled by default so the download is plug-and-play: the game
# loads its sprites, fonts, UI sounds and splash art from <exe>/assets at
# startup and will not boot without them. That is official game art/audio
# (see COPYRIGHT.md) - drop it with `--no-assets` for the lean archive, which
# needs the user to run `bash setup.sh --assets-only` once instead.
set -e
cd "$(dirname "$0")"

WITH_ASSETS=1
VERSION=""
for arg in "$@"; do
    case "$arg" in
        --no-assets) WITH_ASSETS=0 ;;
        --with-assets) WITH_ASSETS=1 ;;
        -*) echo "[package] unknown option: $arg" >&2; exit 1 ;;
        *) VERSION="$arg" ;;
    esac
done
VERSION="${VERSION:-$(date +%Y-%m-%d)}"
NAME="CppSekai-${VERSION}"
DIST="dist"
OUT="${DIST}/${NAME}"

bash build.sh
rm -rf "$OUT"
mkdir -p "$OUT/charts"

# Binaries + the single runtime dependency.
cp -f build/cppsekai.exe "$OUT/"
cp -f build/chartdl.exe "$OUT/"
cp -f build/SDL2.dll "$OUT/"
# Window icon: the exe carries its own copy, but the runtime window icon is
# loaded from this file.
cp -f icon.png "$OUT/"

# Documented data tables (see COPYRIGHT.md: factual data, rebuildable by setup.sh).
cp -f musics.json music-vocals.json music-levels.json "$OUT/"

# Sprites / UI sounds / fonts / splash art the game loads at startup. With
# these in place the package runs as-is (assets are resolved relative to the
# exe, so the folder has to keep its name).
if [ "$WITH_ASSETS" = "1" ]; then
    cp -r assets "$OUT/assets"
    rm -f "$OUT/assets/test.sus" # dev chart, not part of the game
    echo "[package] assets bundled ($(du -sh "$OUT/assets" | cut -f1)) - official game art/audio, see COPYRIGHT.md"
fi

# Documents + the asset fetcher (only useful in the lean build).
cp -f README.md SETUP.md COPYRIGHT.md LICENSE setup.sh "$OUT/"

# Keep the (empty) charts folder in the archive.
cat > "$OUT/charts/放谱面到这里.txt" <<'EOF'
这个文件夹就是游戏的谱面目录。

游戏本体解压即玩（贴图、字体、音效已随包提供），只差谱面。

两种拿谱面的办法：

1. 双击 chartdl.exe（图形界面）：左边勾歌 → 右边勾难度 / 演唱版本 / 曲绘
   → 「下载勾选的歌曲」。文件会按游戏需要的命名写进来
   （谱面 0374_master.sus、BGM se_0374_01.mp3、曲绘 0374.png、元数据 0374.json）。

2. 命令行：
     chartdl.exe --list 374
     chartdl.exe --download 374 --diffs all --vocals all --out .\charts

游戏只认 <数字>.sus / <数字>_<难度>.sus，BGM 与曲绘按上面的命名放在同一层。
EOF

# Trim the binaries zig leaves behind.
rm -f "$OUT"/*.pdb "$OUT"/*.log 2>/dev/null || true

echo
echo "[package] ${OUT}"
du -sh "$OUT"
ls -1 "$OUT"

# Zip (PowerShell is always there on Windows; `zip` usually is not).
if command -v zip >/dev/null 2>&1; then
    (cd "$DIST" && rm -f "${NAME}.zip" && zip -qr "${NAME}.zip" "${NAME}")
else
    powershell -NoProfile -Command \
        "Compress-Archive -Path '${OUT}' -DestinationPath '${DIST}/${NAME}.zip' -Force"
fi
echo
echo "[package] ${DIST}/${NAME}.zip"
du -sh "${DIST}/${NAME}.zip"
