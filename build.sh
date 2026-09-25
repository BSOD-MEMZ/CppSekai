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
    # ⚠ 必须钉住基线 CPU（2026-09-25 加）。不给 -mcpu 时 zig 的默认目标是 **native**，
    # 也就是"构建这台机器的 CPU"（实测 -### 里是 -target-cpu alderlake），于是产物里
    # 会出现 AVX2 / FMA / BMI2 / **AVX-VNNI** 这些只有 Intel 12 代（2021）之后才有的
    # 指令。旧 CPU 上第一条这样的指令就 0xC000001D（STATUS_ILLEGAL_INSTRUCTION），
    # 表现是"看到启动画面就静默消失"——Windows 子系统程序连控制台都没有。
    # 线上实例：rva 0x2113A1 处是 `C4 E2 59 52` = VPDPWSSD（VEX 编码，AVX-VNNI），
    # 一条 128 位 int16 点积；AVX2 时代的 Haswell 都认不了它。
    # 本项目声称支持 Win7 SP1+，所以取 x86-64 基线（SSE2）：Core 2 / Athlon 64 都能跑。
    # 想换性能档位就改这里（x86_64_v2 = Nehalem 2008+，x86_64_v3 = Haswell 2013+），
    # 但别删掉这一行 —— 删了就是静默回到 native，只有老机器的用户会发现。
    # 复查：python .workbuddy/tools/cpu_isa_scan.py build/cppsekai.exe（package.sh 也会跑）。
    -mcpu=baseline
    -s
    # Windows-subsystem binary: no cmd window when the game is launched by
    # double click. main() still runs (the MinGW startup object calls it either
    # way) and main.cpp re-attaches to the parent console when there is one, so
    # running it from a terminal keeps printing the log there.
    -Wl,--subsystem,windows
    # 警告开关：2026-09-21 之前一个都没开。实测全项目只有第三方头（stb / imgui）会报警，
    # 自己的代码是干净的（含 7,489 行的 main.cpp），所以这几行等于白捡一个回归哨兵。
    # -Wno-unused-parameter：回调签名（WndProc / ImGui / SDL）用不上的参数很常见，噪音。
    -Wall
    -Wextra
    -Wno-unused-parameter
    # stb_image_write.h 自己带 8 个 missing-field-initializers（它给 stbi__write_context
    # 做聚合初始化时故意漏掉 context）。这是第三方噪音，定向关掉这一个。
    -Wno-missing-field-initializers
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
    # ⚠ DirectXMath 必须走 -I，**不能**改成 -isystem（2026-09-21 试过，编不过）：
    # 这套 zig 的 builtin 搜索目录排在所有 -isystem 之前，而
    # toolchain/.../libc/include/any-windows-any/ 里有个 MinGW 版的小写
    # `directxmath.h` 桩头（只有 namespace DirectX，没有 XMVECTOR / XMMATRIX）。
    # Windows 文件系统大小写不敏感，桩头会把真头挡在外面，报一堆
    # "no type named 'XMVECTOR' in namespace 'DirectX'"。-I 的优先级在 builtin 之前，
    # 只有 -I 能钉住我们要的那份。
    -Ithird_party/DirectXMath/Inc
    -Ithird_party/imgui
    -I"$SDL/include/SDL2"
)

# 这里原来有个 `CPSEKAI_DEBUG_SYMBOLS=1` 的「留符号表」开关，**2026-09-25 实测没用，已删**：
# 这套 zig（lld / windows-gnu）的产物无论加不加 `-s` / `-g` 都**不写 COFF 符号表**
# （实测 `PointerToSymbolTable = 0`）、也不生成 `.debug$*` 节，`-Wl,-Map=` 被 zig 直接拒掉
# （unsupported linker arg），`--export-all-symbols` / `--print-map` 被静默吞掉。
# 所以崩溃日志里的 RVA 只能靠 `.workbuddy/tools/pe_rva_dump.py`（看那个地址上是什么字节）
# + `cppsekai.log` 最后一行的 `[boot] ...`（死在哪一步）。`-s` 已经写在 CXXFLAGS 里了。


# 我们自己的代码 —— 只有这些文件开 -Wall。
SOURCES=(
    main.cpp
    # game/ 和 platform/ 用 glob：新加一个 .cpp 忘了写进列表，要到**链接期**才报
    # undefined symbol（2026-09-13 加 game/Result.cpp 时踩过）。
    platform/*.cpp
    game/*.cpp
)

# ---------------------------------------------------------------------------
# 上游代码 + 第三方库：单独编成 .o，并且用 -w 关掉它们的警告。
#
# core/native/** 是上游 AGPL 代码（AGENTS.md 说不改结构），vendored 的 imgui 同理：
# 它们自带一堆 missing-braces / sign-compare / unused-function（实测 174 条），
# 全不是我们的问题。但它们要是混在同一次编译里，就会把我们自己代码的警告淹掉 ——
# 这正是这个仓库之前一直不开 -Wall 的实际原因。分成两次编译之后，下面那次
# "SOURCES" 编译里出现的每一条警告都必然是我们自己写出来的。
UPSTREAM_SOURCES=(
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
    third_party/imgui/*.cpp
)

mkdir -p build/obj
UPSTREAM_OBJS=()
for src in "${UPSTREAM_SOURCES[@]}"; do
    obj="build/obj/$(basename "${src%.cpp}").o"
    "$ZIG" c++ "${CXXFLAGS[@]}" -w -c "$src" -o "$obj"
    UPSTREAM_OBJS+=("$obj")
done

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
# Output path, overridable: Windows refuses to overwrite a *running* exe
# ("failed to write output ... Permission denied"), and losing half an hour to
# that has happened more than once. `OUT=build/probe.exe bash build.sh` builds
# the same thing under another name so it can be run (with `--instance multi`)
# while the game is still open.
GAME_OUT="${OUT:-build/cppsekai.exe}"
"$ZIG" c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" "${UPSTREAM_OBJS[@]}" build/app.res \
    "$SDL/lib/libSDL2.dll.a" \
    -limm32 -lsetupapi -lversion -lole32 -loleaut32 -lwinmm -lgdi32 -luser32 -ladvapi32     -lshell32 \
    -lcomdlg32 \
    -lopengl32 \
    -o "$GAME_OUT" "$@"

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

echo "build OK -> $GAME_OUT"
