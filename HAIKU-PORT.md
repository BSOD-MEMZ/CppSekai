# 移植到 HaikuOS 的可行性评估（2026-11-07）

> 结论：**能做，但别指望"改个 build 脚本就能跑"**。
> 渲染 / 谱面核心 / ImGui / 全部游戏逻辑基本是现成的；真正的工作量在
> **音频后端、平台集成、构建系统**这三块，以及一个绕不开的性能问题（Haiku 只有软渲染）。
> 一个人的话：**能玩的单机版约 5~7 天**，完整（含多人、菜单、文件关联）**2~3 周**。

---

## 1. 代码分层与可移植性

| 层 | 体量 | 现状 | 移植难度 |
|---|---|---|---|
| `core/native`（谱面核心 + MMW 移植） | ~1.5 万行 | 纯 C++20 + `DirectXMath`（头文件，SSE2） | ✅ 直接编 |
| `game/`（判定 / HUD / 选曲 / 结算 / 故事 / 特效） | ~2.7 万行 | 纯逻辑 + ImGui 绘制；只有 `Intro.cpp` 引了 `windows.h`（且已 `#ifdef _WIN32`） | ✅ 几乎不用改 |
| `platform/Renderer.cpp` | 1591 行 | SDL2 + OpenGL 3.3 core（`<GL/gl.h>` + `SDL_GL_GetProcAddress`） | ⚠ 能编，但 GL 版本和 vsync 要看 Haiku |
| `platform/Audio.cpp` | 679 行 | miniaudio（`ma_engine` / `ma_sound`），**音频时钟 = 全局主时钟** | ❌ 后端不存在，要自己接 |
| `platform/Party.cpp` | 720 行 | `CreateFileMapping` + `CreateEvent` 的共享内存/事件（多开联动） | ❌ 要重写成 POSIX shm + sem |
| `platform/SystemMedia.cpp` | 1004 行 | 手写 WinRT vtable 的 SMTC + `ITaskbarList3` 进度条 | ❌ 整块删（无对应物） |
| `platform/NativeMenu.cpp` | 223 行 | Win32 菜单栏（Alt 访问键那一堆坑） | ⚠ 换 `BMenuBar`，逻辑可丢一半 |
| `platform/ShellIntegration.cpp` | 435 行 | `.sus` 关联 / 跳转列表 / 注册表 | ⚠ `BNodeInfo` + `BFilePanel` 替一半，跳转列表删 |
| `platform/ChartDlGuide.cpp` | 439 行 | 找 chartdl 窗口、读控件矩形、画置顶穿透浮层 | ❌ 整块禁用（Haiku 没有可比的控件内省） |
| `main.cpp` | 11837 行 | **45 处 `#ifdef _WIN32`**，但 Windows 专属块很多 | ⚠ 工作量集中在这里 |
| `downloader/chartdl.cpp` | 1 个文件 | Win32 GUI（ListView / 进度条 / `comctl32` v6） | ❌ 重写成 `BApplication` 或砍掉 GUI |
| 资源（assets / charts） | 299 文件 | PNG / MP3，用 stb_image + miniaudio 解码 | ✅ 直接用（UTF-8 路径在 Haiku 是原生） |

---

## 2. 四个真障碍

### 2.1 音频后端不存在（P0）

`third_party/miniaudio.h` 里定义了 `MA_HAIKU` 却**没有任何地方用它**——只有
`MA_BEOS` 拿来排除了一个 pthread 分支。miniaudio 的后端清单里没有 Haiku
（WASAPI / DSound / WinMM / CoreAudio / ALSA / PulseAudio / JACK / sndio / audio(4) /
OSS / AAudio / OpenSL / WebAudio / 自定义）。

三条路，按推荐度排：

1. **SDL 音频 + miniaudio 解码器**（推荐）：解码照旧用 `ma_decoder`，
   输出换成 `SDL_OpenAudioDevice` + 自己写一个小的软件混音（BGM 1 路 + SE 4 路 +
   UI SE 3 路 + 预览/BGM 各 1 路，最多 10 路，代码量不大）。
   代价：`Audio.cpp` 的播放部分要重写（约 300 行），`ma_sound` 的 pitch/volume 要自己实现。
2. **miniaudio 自定义后端**（`ma_backend_callbacks`）：写一个 Media Kit 的
   `BBufferConsumer`，把 `ma_device_read` 接到 BBuffer 上。接口对得上，但
   Media Kit 的 buffer 协商、超时、格式转换很啰嗦，估计 400~600 行，且很难调。
3. 先接 `MA_BACKEND_NULL`（静音）跑通画面，音频留到最后——但**音频是主时钟**，
   没有音频就没有正确的判定时钟，只能先用墙钟凑合（游戏本身就支持无 BGM 走墙钟）。

⚠ **关键风险**：这个项目的 `songTime()` 直接读音频引擎的 PCM 帧计数
（`ma_engine_get_time_in_pcm_frames`）。换后端后时钟精度/延迟特性会变，
801 做的 offset 校准（`--offset` / 用户偏移）要重新对一遍。

### 2.2 GL 3.3 + 没有 vsync（P0）

- Haiku 的 OpenGL 是 **Mesa 软件渲染（llvmpipe）**，社区口径是"没有硬件加速，
  但能跑 OpenGL/Vulkan 应用"。GL 版本取决于 Haiku 自带的 Mesa 版本，
  **3.3 core 是否能拿到要实测**（本项目的 Renderer 用了 VAO/VBO/GLSL 330）。
- SDL2 的 Haiku 视频驱动是 `BGLView`，**`HAIKU_GL_SetSwapInterval` 直接返回
  `SDL_Unsupported()`**（SDL 源码里写着 "TODO: Implement this"）。
  也就是说 `SDL_GL_SetSwapInterval(1)` 无效——项目的帧节奏、`--fps` 节流、
  「拖动窗口挂住消息泵」那套补偿逻辑全都建立在"有 vsync 或有节流"上。
  移植后必须自己用 `SDL_Delay` 定帧（项目里已有 `--fps` 那条路径可以复用）。
- **性能是最大的不确定项**：1080p、几千个 quad、每帧多次 draw，纯 CPU 光栅化。
  实机上大概率要降到 720p 或砍特效（`EffectView` 的弱化开关正好能用）。

### 2.3 多人游玩的 IPC（P1）

`platform/Party.cpp` 用命名共享内存 + 命名事件做跨进程时钟同步和房间状态。
Haiku 有 POSIX `shm_open` / `mmap` / `sem_open`，语义能对上，但要重写这 700 行，
而且 `Party` 的"谁创建、谁清理"逻辑依赖 Win32 的句柄语义（进程崩溃时内核自动释放），
POSIX 下要自己加清理（否则 `/ramfs` 里留垃圾）。**这一块可以直接砍掉**——
单机版不依赖它。

### 2.4 构建系统（P1）

`build.sh` 是 **zig 交叉编译到 `x86_64-windows-gnu`**，链接的是随包带的 MinGW
SDL2 导入库，还带 Win7 补丁和 `-mcpu=baseline` 那套。Haiku 上：

- 编译器用 HaikuPorts 的 clang/gcc（原生），**不再需要 zig**；
- SDL2 用 HaikuPorts 的 `libsdl2` 包（Haiku 上 SDL2 是标准包）；
- GL 用系统的 `libGL`，`-lGL`；
- `-Wl,--subsystem,windows`、Win7 补丁、MinGW SDL2 路径全部去掉；
- `third_party/DirectXMath` 可以留着（纯头文件，SSE2 在 x86_64 Haiku 上有）。

也就是**再写一个 `build-haiku.sh`**，`build.sh` 不动。

---

## 3. 其余需要处理的小事

| 项 | 说明 |
|---|---|
| `main.cpp` 的 Windows 块 | 控制台 attach、`SetProcessDpiAwareness`、输入法切换到英文、单实例 mutex、`DwmExtendFrameIntoClientArea`（透明/去黑边）、拖窗暂停（`SDL_SetWindowsMessageHook`）、`restartSelf`（`CreateProcessW`）、`registerDrop`、`WM_COMMAND` 菜单通道 —— 逐块要么给 POSIX 分支，要么 `#ifdef` 掉 |
| 关窗确认 | 现在是"在 WndProc 里吞掉 `WM_CLOSE`"。Haiku 走 `QuitRequested()` / SDL_QUIT，改成 `SDL_QUIT` 拦截即可，**反而简单** |
| 触摸 / 手柄 | Haiku 基本没有触摸屏；SDL2 gamepad 在 Haiku 的支持不明确，先把键盘 + 鼠标跑通 |
| 窗口透明观感 | 没有 DWM，选曲背景那套"玻璃"会退化成纯色/半透明；`glassMode` 直接当 0 |
| 路径 | 项目到处是 `path_utf8.hpp` 的 Win32 宽字符转换；Haiku 原生 UTF-8，把 Win32 分支换成直通即可（这块是**变简单**） |
| 字体 / 文本 | stb_truetype（`platform/FontOutline.cpp` 自带一份实现）+ ImGui 内置字体 → 无依赖，可移植 |
| 媒体键 / SMTC | 无对应物。Haiku 有 `BNotification`，可以做"正在播放"提示，但没有系统级媒体控件 |
| 文件关联 | `BNodeInfo::SetType("application/x-cppsekai-sus")` + `BMimeType::SetPreferredApp`，比注册表干净 |

---

## 4. 分阶段计划（如果真要做）

| 阶段 | 内容 | 预估 |
|---|---|---|
| 0 | Haiku 实机/虚拟机起 SDL2 + OpenGL 窗口，打印 `GL_VERSION` / `GL_RENDERER`，确认 3.3 core 与帧率上限 | 0.5~1 天 |
| 1 | `build-haiku.sh` + 平台桩：`SystemMedia` / `NativeMenu` / `ShellIntegration` / `ChartDlGuide` / `Party` 全换 no-op，让 `main.cpp` 编过 | 2~3 天 |
| 2 | 音频（SDL 音频 + miniaudio 解码），先只保证 BGM 能播、时钟能走 | 2~3 天 |
| 3 | 输入 / 全屏 / 窗口事件 / 路径，跑通一局 | 1~2 天 |
| 4 | 判定 offset 重校准 + 性能调优（分辨率、特效） | 1~2 天 |

**只做"单机、键盘、能打一局"：5~7 天。** 加上多人、菜单、文件关联、chartdl 重写，
2~3 周起步，而且第二平台会永久增加维护成本。

---

## 5. 结论与建议

- **技术上没有死路**：没有一块是"Haiku 根本做不到"的，最坏的音频也能用 SDL 音频绕过去。
- **真正的阻力是性能和收益**：Haiku 只有 llvmpipe，1080p 的节奏游戏体验很难保证；
  而 Haiku 用户基数极小，这个移植更像是"能不能"的实验，不是"值不值"的产品决策。
- 如果只是想验证"这套 C++ 代码是不是真的可移植"：**阶段 0 + 阶段 1 就够了**（3~4 天），
  能编过、能出画面，剩下的就是体力活。
- 想要一个"真能玩的多平台版本"，**WebAssembly/WebGL 反而是更现实的路**：
  `core/native` 本来就是按 emscripten 目标写的（导出的 API 全是 `EMSCRIPTEN_KEEPALIVE`），
  上游 `sekai-mmw-preview-web` 就是网页版。
- **建议做成独立分支 `haiku`**，不进主线：`platform/` 的条件编译会让主线每改一处都要
  两边验证，而这项目最怕的就是"改了 A 坏了 B"。
