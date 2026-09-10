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
    -D_XM_NO_INTRINSICS_
    -D_CRT_SECURE_NO_WARNINGS
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
    game/SongSelect.cpp
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
"$ZIG" c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" \
    "$SDL/lib/libSDL2.dll.a" \
    -limm32 -lsetupapi -lversion -lole32 -loleaut32 -lwinmm -lgdi32 -luser32 -ladvapi32     -lshell32 \
    -lopengl32 \
    -o build/cppsekai.exe "$@"

# Runtime DLL + assets next to the exe
cp -f "$SDL/bin/SDL2.dll" build/ 2>/dev/null || true
rm -rf build/assets 2>/dev/null || true
cp -r assets build/ 2>/dev/null || true
# Official per-difficulty levels (song select pads; unipjsk charts ship without)
cp -f music-levels.json build/ 2>/dev/null || true

echo "build OK -> build/cppsekai.exe"
