#!/usr/bin/env bash
# CppSekai build script (bash / Git Bash)
# Uses the bundled zig toolchain as the C++ compiler and the bundled SDL2
# MinGW import libraries. No system-wide dependencies.
set -e
cd "$(dirname "$0")"
# zig's compilation cache needs filesystem features the D: drive lacks;
# keep it on C:.
export ZIG_GLOBAL_CACHE_DIR="$LOCALAPPDATA/Temp/cppsekai-zig-cache"
export ZIG_LOCAL_CACHE_DIR="$ZIG_GLOBAL_CACHE_DIR/local"

ZIG="toolchain/zig014/zig-x86_64-windows-0.14.1/zig.exe"
TOOLCHAIN_LIB="$(dirname "$ZIG")/lib"
SDL="toolchain/SDL2-2.32.10/x86_64-w64-mingw32"

CXXFLAGS=(
    -std=c++20
    -O2
    -s
    # Windows-subsystem binary: no cmd window when the game is launched by
    # double click. main() still runs (the MinGW startup object calls it either
    # way) and main.cpp re-attaches to the parent console when there is one, so
    # running it from a terminal keeps printing the log there.
    -Wl,--subsystem,windows
    -D_XM_NO_INTRINSICS_
    -D_CRT_SECURE_NO_WARNINGS
    # stb_image must read UTF-8 paths (it opens files with _wfopen when this is
    # set); everything the game hands it is UTF-8, see path_utf8.hpp.
    -DSTBI_WINDOWS_UTF8
    -Icore/native
    -Icore/native/src
    -Icore/native/generated
    -Ithird_party
    -I.
    -Ithird_party/DirectXMath/Inc
    -Ithird_party/imgui
    -I"$SDL/include/SDL2"
)


SOURCES=(
    main.cpp
    platform/CoreApi.cpp
    platform/Renderer.cpp
    platform/FontOutline.cpp
    platform/Audio.cpp
    platform/Party.cpp
    platform/SystemMedia.cpp
    game/Judgement.cpp
    game/Hud.cpp
    game/Ui.cpp
    game/Intro.cpp
    game/PartyScreen.cpp
    game/Result.cpp
    game/SongSelect.cpp
    game/TapEffect.cpp
    game/StageBackground.cpp
    core/native/src/mmw_preview.cpp
    core/native/mmw_port/Math.cpp
    core/native/mmw_port/MinMax.cpp
    core/native/mmw_port/Note.cpp
    core/native/mmw_port/Score.cpp
    core/native/mmw_port/Tempo.cpp
    core/native/mmw_port/Utilities.cpp
    core/native/mmw_port/EffectView.cpp
    core/native/mmw_port/Particle.cpp
    core/native/mmw_port/ResourceManager.cpp
    core/native/mmw_port/Rendering/Camera.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_impl_sdl2.cpp
    third_party/imgui/imgui_impl_opengl3.cpp
)

mkdir -p build
# ---------------------------------------------------------------------------
# Win7 兼容补丁（幂等，每次构建都检查一遍）。
#
# toolchain/ 不入库，换台机器重新解压 zig 就回到未打补丁的状态，所以补丁写在构建脚本里。
#
# 病根：zig 自带 libc++（lib/libcxx/src/chrono.cpp）编译时 _WIN32_WINNT=0x0a00，
# 于是 std::chrono::system_clock::now() 走了 "Windows 8+" 分支，**静态导入**
# GetSystemTimePreciseAsFileTime。这个函数 Win8 才有，Win7 SP1 上进程加载阶段就弹
# "无法定位程序输入点 GetSystemTimePreciseAsFileTime 于动态链接库 KERNEL32.dll 上"，
# 一行业务代码都不会执行。注意：它连 <iostream> 这种无关头文件都躲不开（libc++ 内部
# 引用），所以没法在业务代码里绕过去。
#
# 修法：强制走 libc++ 自带的「运行时探测」分支 —— GetProcAddress 找得到就用精确时钟
# （Win8+ 行为完全不变，实测精度仍是微秒级），找不到退回 GetSystemTimeAsFileTime
# （Win7，15ms 粒度）。只影响 std::chrono::system_clock，游戏计时用的是 QPC。
CHRONO_CPP="$TOOLCHAIN_LIB/libcxx/src/chrono.cpp"
if ! grep -q CPPSEKAI-WIN7 "$CHRONO_CPP" 2>/dev/null; then
    sed -i \
        -e 's@^#  if _WIN32_WINNT < _WIN32_WINNT_WIN8$@#  if 1 /* CPPSEKAI-WIN7 */@' \
        -e 's@^#  if (_WIN32_WINNT >= _WIN32_WINNT_WIN8 .*$@#  if 0 /* CPPSEKAI-WIN7 */@' \
        -e '/^      (_WIN32_WINNT >= _WIN32_WINNT_WIN10)/d' \
        "$CHRONO_CPP"
    # 补丁打不上就当场报错，别默默产出一个 Win7 打不开的 exe
    [ "$(grep -c CPPSEKAI-WIN7 "$CHRONO_CPP")" = 2 ] || {
        echo "!! build.sh: Win7 兼容补丁打不上（$CHRONO_CPP 内容变了），请手工检查" >&2
        exit 1
    }
fi
# Windows resources: the exe icon + version info. zig ships its own resource
# compiler, so no windres / Windows SDK is needed. Two .rc files: the icon and
# the version block are shared (resources.rc), the manifests are not - the
# downloader declares DPI awareness, the game does not.
"$ZIG" rc app.rc build/app.res
"$ZIG" rc chartdl.rc build/chartdl.res
"$ZIG" c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" build/app.res \
    "$SDL/lib/libSDL2.dll.a" \
    -limm32 -lsetupapi -lversion -lole32 -loleaut32 -lwinmm -lgdi32 -luser32 -ladvapi32     -lshell32 \
    -lopengl32 \
    -o build/cppsekai.exe "$@"

# ---------------------------------------------------------------------------
# Chart downloader (a separate, standalone tool: same libraries, no game code).
# Shares the vendored ImGui + SDL2 + nlohmann/json, so it builds in a second.
# ---------------------------------------------------------------------------
DL_SOURCES=(
    downloader/chartdl.cpp
)
# GUI subsystem: double-clicking it must not flash a console. The command line
# modes attach to the parent console themselves (see main()).
"$ZIG" c++ "${CXXFLAGS[@]}" -Wl,--subsystem,windows "${DL_SOURCES[@]}" build/chartdl.res     -lole32 -loleaut32 -lgdi32 -luser32 -ladvapi32 -lshell32 -lcomctl32 -lcomdlg32     -o build/chartdl.exe "$@"

# Runtime DLL + assets next to the exe
cp -f "$SDL/bin/SDL2.dll" build/ 2>/dev/null || true
rm -rf build/assets 2>/dev/null || true
cp -r assets build/ 2>/dev/null || true
# Official per-difficulty levels (song select pads; unipjsk charts ship without)
cp -f music-levels.json build/ 2>/dev/null || true
# Window icon (the exe's own icon is embedded from app.rc)
cp -f icon.png build/ 2>/dev/null || true
# Official song readings (song select "sort by name" + aiueo grouping)
cp -f musics.json build/ 2>/dev/null || true

echo "build OK -> build/cppsekai.exe"
