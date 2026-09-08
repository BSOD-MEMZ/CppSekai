# AGENTS.md — 给 AI 助手的项目指南

CppSekai：Project SEKAI 风格 SUS 谱面 Windows 原生游玩器。
上游是 [sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web)（AGPL-3.0），
其谱面核心从 MikuMikuWorld（MIT）移植。**本仓库整体遵循 AGPL-3.0-only，改动必须保持开源。**

## 架构（改代码前先读这段）

```
core/native/src/mmw_preview.cpp   # 谱面核心（上游代码，勿改结构）
  - SusParser::parseText          # SUS 解析
  - susToScore                    # 谱面数据模型 + tick/秒换算
  - calculateDrawData/calculateHitEvents  # 绘制数据 & 判定事件生成
  - extern "C" API：loadSusTextPrecise / render(chartTimeSec) /
    getQuadBufferPointer / getHitEventBufferPointer
core/native/mmw_port/             # MikuMikuWorld 移植层（上游代码）
platform/                         # 平台层（本项目新增）
  Renderer.cpp  # OpenGL 3.3 core，消费核心输出的 packed quad（25 float/quad）
  Audio.cpp     # miniaudio；音频时钟 = 全局主时钟；音乐文件位置 = songTime + startPos + userOffset；
                # 自动检测 BGM 开头静音填充（官服 mp3 有 ~9s，musics.json 的 fillerSec）
  SystemMedia.* # SMTC（系统媒体传输控件，手写 WinRT vtable）+ ITaskbarList3 任务栏进度条
  CoreApi.cpp   # core_api.hpp 的 C++ 包装 + #WAVEOFFSET 文本扫描
game/Judgement.*  # 判定引擎（本项目新增，判定逻辑都在这）
game/Intro.*      # ImGui 卡片/UI；字体跟随系统（注册表找字体文件 + CJK 字形探测，Yu Gothic UI
                  # 是 CFF 轮廓 stb_truetype 渲染不了，会自动落到 Microsoft YaHei UI；--pjsk-font 回退）
main.cpp          # SDL2 窗口、事件循环、输入映射、ImGui HUD、截图模式
```

数据流：`loadSusTextPrecise → render(t) → packedQuads → Renderer::renderFrame`；
判定侧：`getHitEventBuffer → JudgementEngine::load → tap/flick/update`。

### 关键数据格式

- packed quad（25 floats）：4×(x,y,reciprocalW) + 4×(u,v) + rgba + textureId。
  textureId 0=notes 1=longNoteLine 2=touchLine（世界坐标，需 worldToClip 变换）；
  ≥3 = effect.png（已是裁剪空间坐标，id==4 为加法混合）。
- packed HitEvent（7 floats）：timeSec, center(轨道坐标), width, kind, flags, endTimeSec, volume。
  kind：0=tap 1=critical tap 2=flick 3=trace 4=hold tick(自动) 5=hold 标记(endTimeSec 有效)。

## 构建

```bash
bash build.sh          # 仅需 Git Bash；产物 build/cppsekai.exe + SDL2.dll + assets/
```

- 编译器是自带的 zig 0.14.1（`toolchain/`），**不要用 0.16**（其 c++ 驱动会吞 `-I`）。
- zig 编译缓存**必须放 C 盘**（build.sh 已设 `ZIG_GLOBAL_CACHE_DIR`）；D 盘文件系统不支持 zig 缓存所需的文件操作，会报 `CacheCheckFailed/AccessDenied`。
- MinGW 的 gl.h 只有 GL 1.1：GL 3.3 的函数指针和常量在 `platform/Renderer.cpp` 顶部的 `namespace gl` 里手工声明，加新 GL 调用时去那里补。
- SDL2 的 MinGW 导入库需要额外链接 imm32/setupapi/version/oleaut32，且要自己 stub 三个屏保符号（`ScreenSaverProc` 等，在 main.cpp 顶部）。
- **`main.cpp` 必须在 include SDL.h 之前 `#define SDL_MAIN_HANDLED`**，否则 SDL.h 把 main 重定义为 SDL_main，程序会变成"秒退且无输出"。
- miniaudio 的实现（`MINIAUDIO_IMPLEMENTATION`）只在 `platform/Audio.cpp` 里定义一次。

## 约定与坑

- `core/native/` 下的文件是上游代码：能不改就不改；确需改时在注释里标注原因，便于同步上游。
- 资源按 exe 所在目录解析（`SDL_GetBasePath()`），不按 CWD。charts/ 目录会依次尝试
  `--charts` → `exe\charts` → `exe\..\charts` → `./charts`，取第一个有谱面的。
- 音频对齐：官服 mp3 开头有静音填充（fillerSec≈9s），谱面 tick0 在静音之后。优先级：
  sidecar json `fillerSec`/`offset`(ms) > 自动静音检测 > 0；`--filler`/`--offset` 可覆盖。
- SUS 的 `#WAVEOFFSET` 单位是秒；核心 API 的 offset 参数是毫秒且只进 metadata，实际延迟由 AudioEngine 实现。
- SMTC/ITaskbarList3 是手写 WinRT/COM vtable（工具链无 Windows SDK）：IID 与方法顺序来自解析
  `C:\Windows\System32\WinMetadata\Windows.Media.winmd`，解析脚本在 `.workbuddy/tools/`；
  combase.dll 相关函数全部 LoadLibrary 动态加载，无需导入库。改接口调用前先跑脚本核对槽位。
- 窗口/帧率：`--width/--height`（默认 1280x720）、`--window borderless|windowed|fullscreen`、
  `--fps <n>`（vsync 之外的软上限，0=仅垂直同步）；调试面板（H）里可实时切换窗口模式和帧率上限。
- 触摸输入走 SDL_Finger* 事件，屏幕坐标 → 裁剪空间 → 世界轨道坐标的逆变换在 `Renderer::clipToWorldX/Y`。
- 判定窗口默认 perfect 40ms / great 90ms / good 140ms（非官方数值，做成可调的）。
- 游戏资源（assets/、charts/）来自公开渠道，仅限本地游玩，不要提交或分发。

## 验证（不开窗口的自动检查）

```bash
cd build
./cppsekai.exe --sus ../assets/test.sus --screenshot shot.png --screenshot-time 3.8
```

成功时会生成截图并写 `cppsekai.log` 后退出；失败原因也在 log 里。截图应看到
pjsk 舞台、透视轨道和下落中的 note 贴图。charts/ 里有联网下载的谱面可直接用，
BGM URL 规律：`https://assets.unipjsk.com/ondemand/music/long/se_<id>_01/se_<id>_01.mp3`（不是每首都有）。

## 系统要求

- Windows 7 SP1 及以上（miniaudio / SDL2 兼容底线）。OpenGL 3.3 core（约 2008 年后的 GPU 均可）。
- SMTC（媒体浮层/任务栏媒体控件）与任务栏进度条：SMTC 走 `RoGetActivationFactory`，**实际只在
  Windows 10+ 生效**（Win7/8 上 combase 的激活会失败，代码里已容错，只是不显示）；任务栏进度条
  ITaskbarList3 在 Win7+ 均可用。
- 不依赖任何运行库安装（zig c++ 静态链接 CRT + 自带 SDL2.dll）。

## 待办（按优先级）

1. flick 严格方向校验（当前上滑/点按都算过）
2. hold 音效循环（SeHoldLoop 未接）与 SE kind 区分（当前键盘全播一个音）
3. 输入/音频延迟校准界面
4. 结算画面、连击特效（judge v3 贴图已在 assets 里但未用）
5. 键盘 12 键布局可能不顺手，考虑做成可配置