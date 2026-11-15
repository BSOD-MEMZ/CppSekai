#!/usr/bin/env bash
# 造一份只认 usm + mpeg1 的 ffmpeg 静态库，给 CppSekai 的 2D MV 播放用。
#
# 产物：third_party/ffmpeg-min/{include,lib}
#   libavformat.a ~1.8 MB / libavcodec.a ~3.5 MB / libswscale.a ~2.9 MB / libavutil.a ~3.2 MB
#   链接进 exe 之后只涨 ~1.8 MB（实测 5.65 MB → 7.41 MB）。
#
# 为什么是自己编而不是下预编译：
#   * 官方 build 全是全量，102 MB 起步；
#   * 我们只要 usm demuxer + mpeg1video 解码 + swscale 的 YUV→RGBA，其余全关。
#
# 许可：configure 默认 --disable-gpl，产物是 **LGPL v2.1 or later**。
#   本项目整体 AGPL-3.0 且开源，静态链接 LGPL 合规（满足"可重新链接"义务：
#   源码与构建脚本都在，见本文件）。
#
# 用法：bash .workbuddy/tools/build_ffmpeg_min.sh
#   需要的只有：zig（仓库 toolchain/ 里自带）、curl、tar、python（解压 xz）。
#   **不需要** MSVC / MinGW / MSYS2 —— zig cc 同时充当 C 编译器和 ar。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/build/dl"
SRC="$ROOT/build/src"
OUT="$ROOT/third_party/ffmpeg-min"
ZIG="$ROOT/toolchain/zig014/zig-x86_64-windows-0.14.1/zig.exe"
FFMPEG_VER=7.1
MAKE_VER=4.4.1

mkdir -p "$DL" "$SRC"

# ---- 1. GNU make（Windows 上没有，ezwinports 的单 zip 解压即用）--------------
if [ ! -x "$ROOT/build/make/bin/make.exe" ]; then
    echo "[1/4] fetch make"
    curl -sL -o "$DL/make.zip" \
        "https://downloads.sourceforge.net/project/ezwinports/make-${MAKE_VER}-without-guile-w32-bin.zip"
    python -c "import zipfile;zipfile.ZipFile(r'$DL/make.zip').extractall(r'$ROOT/build/make')"
fi
export PATH="$ROOT/build/make/bin:$PATH"

# ---- 2. ffmpeg 源码（tar 不认 xz，用 python 解）-----------------------------
if [ ! -d "$SRC/ffmpeg-$FFMPEG_VER" ]; then
    echo "[2/4] fetch ffmpeg $FFMPEG_VER"
    curl -sL -o "$DL/ffmpeg.tar.xz" "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VER}.tar.xz"
    python - "$DL/ffmpeg.tar.xz" "$SRC" <<'PY'
import lzma, sys, tarfile
with lzma.open(sys.argv[1]) as f, tarfile.open(fileobj=f) as t:
    t.extractall(sys.argv[2])
PY
fi

# ---- 3. configure -----------------------------------------------------------
# ⚠ TMPDIR 必须是 Windows 路径：configure 用 mktemp 在 /tmp 里建测试文件，
#   而 zig 是原生程序、不认识 MSYS 的 /tmp，会报
#   "unable to open output directory '/tmp/ffconf.XXXX': FileNotFound"。
# ⚠ --ar 必须显式给：Windows 上没有 ar，用 `zig ar`。
# ⚠ --disable-asm：免装 nasm/yasm（mpeg1 解码的 C 实现完全够用）。
echo "[3/4] configure"
cd "$SRC/ffmpeg-$FFMPEG_VER"
mkdir -p "$ROOT/build/tmp"
export TMPDIR="$(cygpath -m "$ROOT/build/tmp")"

./configure \
    --prefix="$(cygpath -m "$OUT")" \
    --target-os=mingw32 --arch=x86_64 \
    --cc="$ZIG cc -target x86_64-windows-gnu" \
    --ar="$ZIG ar" --ranlib=false \
    --disable-everything --disable-programs --disable-doc --disable-network \
    --disable-autodetect --disable-asm --enable-static --disable-shared \
    --enable-demuxer=usm \
    --enable-decoder=mpeg1video \
    --enable-parser=mpegvideo \
    --enable-protocol=file,pipe \
    --enable-filter=scale,format,null,anull \
    --enable-swscale --enable-avcodec --enable-avformat --enable-avutil

# ---- 4. build ---------------------------------------------------------------
echo "[4/4] make -j8"
make -j8
make install

echo
echo "done -> $OUT"
ls -la "$OUT/lib" 2>/dev/null || ls -la "$OUT"/lib/*.a
