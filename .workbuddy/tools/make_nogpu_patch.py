#!/usr/bin/env python3
"""Build the optional "no GPU -> software rendering" patch zip for CppSekai.

The package is meant to be published *next to* the normal release zip, not
inside it: only players whose machine has no OpenGL 3.3 driver need it.

Contents: Mesa's WGL front-end (opengl32.dll) + the gallium driver that carries
llvmpipe (libgallium_wgl.dll), a launcher .cmd, a Chinese readme and Mesa's
license summary. Deliberately *not* included: d3d10warp.dll / dxil.dll and the
rest of the mesa-dist-win x64 folder - those are Microsoft or D3D12/Vulkan-only
and would drag extra licensing along.

usage:
    python make_nogpu_patch.py [<dir holding the mesa x64 dlls>]

The dlls live in toolchain/mesa-<ver>/x64 (gitignored, like the rest of
toolchain/). Refreshing them - also the recipe when the packaged Mesa version
changes:

    curl -L -o mesa.7z https://github.com/pal1000/mesa-dist-win/releases/download/\
<ver>/mesa3d-<ver>-release-msvc.7z
    # tar.exe (bsdtar) fails with "LZMA codec is unsupported" and py7zr with
    # "BCJ2 filter is not supported", so use the standalone 7-Zip CLI:
    curl -L -o 7zr.exe https://www.7-zip.org/a/7zr.exe
    7zr.exe x -y -o<out> mesa.7z "x64/*"

The result is written to dist/CppSekai-software-gl-mesa-<ver>.zip.
"""
import hashlib
import os
import shutil
import sys
import urllib.request
import zipfile

MESA_VERSION = "26.2.4"
MESA_PKG = f"mesa3d-{MESA_VERSION}-release-msvc.7z"
UPSTREAM = "https://github.com/pal1000/mesa-dist-win"
MESA_HOME = "https://www.mesa3d.org/license.html"
LICENSE_URL = "https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/docs/license.rst"

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DLL_DIR = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    ROOT, "toolchain", f"mesa-{MESA_VERSION}", "x64")
LICENSE_CACHE = os.path.join(ROOT, "build", "_mesalic.txt")
OUT_DIR = os.path.join(ROOT, "dist")
STAGE = os.path.join(OUT_DIR, "_staging_nogpu")
ZIP_NAME = f"CppSekai-software-gl-mesa-{MESA_VERSION}.zip"

DLLS = ["opengl32.dll", "libgallium_wgl.dll"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def mesa_license_text():
    """Upstream license summary, cached in build/ (gitignored)."""
    if os.path.isfile(LICENSE_CACHE):
        with open(LICENSE_CACHE, "r", encoding="utf-8") as fh:
            return fh.read()
    try:
        with urllib.request.urlopen(LICENSE_URL, timeout=30) as resp:
            text = resp.read().decode("utf-8")
    except Exception as exc:  # offline: ship a pointer instead of guessing the text
        print(f"warning: could not fetch {LICENSE_URL} ({exc})")
        return (f"Could not fetch Mesa's license summary at build time.\n"
                f"See {MESA_HOME} and {LICENSE_URL}\n")
    os.makedirs(os.path.dirname(LICENSE_CACHE), exist_ok=True)
    with open(LICENSE_CACHE, "w", encoding="utf-8") as fh:
        fh.write(text)
    return text


def main():
    for name in DLLS:
        src = os.path.join(DLL_DIR, name)
        if not os.path.isfile(src):
            raise SystemExit(
                f"missing {src}\n"
                f"pass the directory holding Mesa's x64 dlls, see this file's docstring")


    if os.path.isdir(STAGE):
        shutil.rmtree(STAGE)
    os.makedirs(STAGE)

    hashes = {}
    for name in DLLS:
        shutil.copy2(os.path.join(DLL_DIR, name), os.path.join(STAGE, name))
        hashes[name] = sha256(os.path.join(STAGE, name))

    # ---- launcher (ASCII, CRLF: cmd.exe is not fond of LF-only batch files)
    cmd = (
        "@echo off\r\n"
        "rem CppSekai - start with the software OpenGL shipped next to this file.\r\n"
        "rem Setting GALLIUM_DRIVER makes Mesa pick llvmpipe directly instead of\r\n"
        "rem probing d3d12/vulkan first.\r\n"
        "setlocal\r\n"
        'cd /d "%~dp0"\r\n'
        "set GALLIUM_DRIVER=llvmpipe\r\n"
        "set LIBGL_ALWAYS_SOFTWARE=1\r\n"
        'start "" "%~dp0cppsekai.exe" %*\r\n'
    )
    with open(os.path.join(STAGE, "run-no-gpu.cmd"), "wb") as fh:
        fh.write(cmd.encode("ascii"))

    # ---- Mesa license summary, verbatim from upstream
    license_text = mesa_license_text()
    header = (
        "Mesa 3D license summary\n"
        f"  source : https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/docs/license.rst\n"
        f"  version: Mesa {MESA_VERSION} (packaged by {UPSTREAM} as {MESA_PKG})\n"
        f"  page   : {MESA_HOME}\n"
        "  The files in this package (opengl32.dll, libgallium_wgl.dll) are Mesa's\n"
        "  own; the core Mesa library is licensed under the MIT license.\n"
        "----------------------------------------------------------------------\n\n"
    )
    with open(os.path.join(STAGE, "LICENSE-Mesa.txt"), "w", encoding="utf-8", newline="\r\n") as fh:
        fh.write(header + license_text)

    readme = f"""CppSekai - 无显卡（软件渲染）补丁
==================================
Mesa {MESA_VERSION} / llvmpipe

这个包解决什么
--------------
CppSekai 需要 OpenGL 3.3。如果机器只有「Microsoft 基本显示适配器」、没有 3D 驱动的
云主机 / 远程桌面 / 虚拟机，或者驱动只到 OpenGL 1.1，启动时会出现

    Failed to initialize OpenGL loader!

或者干脆双击没有任何反应。这个包把 OpenGL 换成 Mesa 的 llvmpipe —— 一个纯 CPU 的
软件光栅化器，于是不依赖任何显卡驱动也能跑。

怎么装
------
1. 解开这个压缩包。
2. 把下面两个文件复制到 **cppsekai.exe 所在的那个目录**（也就是和 SDL2.dll 同级）：

       opengl32.dll
       libgallium_wgl.dll

3. 双击 run-no-gpu.cmd 启动游戏（它会先设好 GALLIUM_DRIVER=llvmpipe 再启动）。
   直接双击 cppsekai.exe 一般也能用，用这个 .cmd 更保险。

解压到别处、或者放到 cppsekai.exe 的上一层目录，都是没用的 —— 只认游戏目录。

怎么确认生效
------------
能进到选曲界面就成了。想看细节：游戏目录下的 cppsekai.log 里不再出现
"Failed to initialize OpenGL loader!" 这行。

代价（先看这段再决定要不要用）
------------------------------
* 画面完全由 CPU 算。实测 i7-1260P（16 线程）1366x768、关垂直同步：
    选曲界面 55-66 fps，演奏中 83-112 fps
  同一台机器用真显卡是 214-219 fps。也就是慢 2-3 倍。核心数少的机器
  （云主机常见）会明显更差。
* 建议把渲染尺寸调小，并关掉特效：
      cppsekai.exe --render-size 960x540
  或在「设置 -> 画面」里把特效调低。
* 节奏游戏对延迟很敏感（Perfect 判定窗口只有几十毫秒），再叠上云桌面本身的
  网络 / 编解码延迟，帧率够也未必能正常打。**能进去不等于能玩。**

有些安全软件会拦程序旁边陌生名字的 opengl32.dll；被拦时表现是游戏起不来，
把游戏目录加进白名单即可。

不想要了怎么恢复
----------------
删掉 opengl32.dll 和 libgallium_wgl.dll 两个文件
（run-no-gpu.cmd / 这个说明 / LICENSE-Mesa.txt 留着不影响），
程序就回到用系统自带的 OpenGL。

包里是什么
----------
opengl32.dll / libgallium_wgl.dll 来自 Mesa 3D {MESA_VERSION}（MIT 许可），
取自 {UPSTREAM} 的 {MESA_PKG}，只挑了 OpenGL 需要的这两个文件
（d3d12 / vulkan / opencl / 微软的 d3d10warp 都没有带）。
许可证摘要见同目录 LICENSE-Mesa.txt，上游许可证页：{MESA_HOME}

    SHA256  opengl32.dll        {hashes['opengl32.dll']}
    SHA256  libgallium_wgl.dll  {hashes['libgallium_wgl.dll']}

打包日期 {__import__('datetime').date.today().isoformat()} / 对应 CppSekai 的 2026-10-10 构建
"""
    with open(os.path.join(STAGE, "README-无显卡.txt"), "w", encoding="utf-8-sig", newline="\r\n") as fh:
        fh.write(readme)

    zip_path = os.path.join(OUT_DIR, ZIP_NAME)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for name in sorted(os.listdir(STAGE)):
            zf.write(os.path.join(STAGE, name), name)

    print("staged:", sorted(os.listdir(STAGE)))
    for name, digest in hashes.items():
        print(f"  {name:22s} {digest}")
    print(f"zip    : {zip_path}")
    print(f"size   : {os.path.getsize(zip_path) / 1e6:.1f} MB")
    print(f"sha256 : {sha256(zip_path)}")


if __name__ == "__main__":
    main()
