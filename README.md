# CppSekai

Project SEKAI 风格 SUS 谱面 **Windows 原生游玩器**（非模拟器、非网页）。
由 [sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web)（AGPL-3.0）移植改造，
谱面解析/渲染核心未改动，新增平台层与判定引擎，使其成为可真正游玩的节奏游戏。

## 当前状态（骨架版）

- SUS 解析、谱面绘制数据、HUD 数据：复用原项目 wasm 核心（C++，未改动）
- 窗口/输入：SDL2（支持多点触摸 + 12 键键盘）
- 渲染：桌面 OpenGL 3.3 core，兼容 Intel 核显（与原 WebGL2 渲染管线逐 quad 对齐）
- 音频：miniaudio（WASAPI），音频时钟为主时钟，支持 SUS `#WAVEOFFSET` / `--offset`
- 判定：PERFECT/GREAT/GOOD/MISS（窗口可在 HUD 内调节）、combo、分数、
  hold 跟踪（断尾判定）、flick 手势（上滑，长按 tap 兼容）、自动 MISS
- 已知简化：flick 方向未严格校验、hold tick 自动判、无结算画面/连击特效/键盘音效映射未区分 kind

## 构建

需要 Git Bash。工具链全部自带（zig 当编译器 + SDL2 MinGW 包），无需安装任何东西：

```bash
bash build.sh          # 产物: build/cppsekai.exe（约 4.5 MB）+ SDL2.dll
```

zig 的编译缓存在 C 盘临时目录（D 盘文件系统不支持 zig 缓存所需的文件特性）。

## 运行

```bash
cd build
./cppsekai.exe --sus <谱面.sus> --bgm <音频.mp3> [--offset <秒>] [--speed 1-12] [--se-volume 0-1] [--auto]
```

- 无 `--bgm` 时用墙上时钟跑谱（预览模式）
- `--offset` 缺省自动读 SUS 的 `#WAVEOFFSET`（单位秒）
- 调试：`--screenshot <out.png> --screenshot-time <秒>` 在指定谱面时间截帧后退出（输出会写进 `cppsekai.log`）

### 操作

| 输入 | 映射 |
|---|---|
| 触摸屏 | 多点触摸按轨道区域，上滑 = flick，按住 = hold |
| 键盘 | `Z S X D C V G B H N J M` = 12 个半轨（自左向右） |
| SPACE | 暂停/继续 |
| F | 全屏 |
| ESC | 退出 |

## 目录结构

```
core/native/        # 谱面核心（复制自 sekai-mmw-preview-web，未改动）
platform/           # 平台层：Renderer(OpenGL) / Audio(miniaudio) / CoreApi
game/Judgement.*    # 判定引擎（新写）
main.cpp            # SDL2 窗口、主循环、输入、ImGui HUD
assets/             # 贴图与判定音效（复制自原项目 public/assets）
toolchain/          # zig + SDL2（可整体删除重新下载）
third_party/        # miniaudio / stb / DirectXMath / ImGui / nlohmann
```

## 许可证

继承上游 AGPL-3.0-only（sekai-mmw-preview-web 引入 AGPL 代码）。
谱面核心源自 MikuMikuWorld（MIT）。
