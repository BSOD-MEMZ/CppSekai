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
    platform/Audio.cpp
    platform/SystemMedia.cpp
    game/Judgement.cpp
    game/Hud.cpp
    game/Ui.cpp
    game/Intro.cpp
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
