# CppSekai

> **把 Project SEKAI 的 SUS 谱面，做成一个双击就能玩的 Windows 原生 exe。**

没有 Electron，没有浏览器，没有 Unity，没有引擎，没有一个需要用户安装的运行库。
zig 把 C++20 静态链成一个 ~4.8 MB 的 `cppsekai.exe`，SDL2 给窗口和输入，OpenGL 3.3 core 直接上屏。

```
  谱面文件 (.sus)  →  判定事件流  →  ┐
                   →  打包 quad 流 →  ├→  一个 exe
  音频文件 (.mp3)  →  PCM 帧时钟   →  ┘
```

---

## 目录

| 演奏 | 选歌 | 暂停 |
|---|---|---|
| ![演奏](docs/preview.png) | ![选歌](docs/preview_select.png) | ![暂停](docs/preview_pause.png) |

三张图都是无头模式（`--screenshot`）真跑出来的帧，不是画的，也不是渲染图。

---

## 1. 它到底能干什么

- **SUS 谱面原生解析 + 演奏**：12 轨，tap / critical tap / flick / trace(friction) / hold 全部支持。
- **三种输入共用一条路径**：键盘 12 键、鼠标左/右键（当成两个指针）、多指触摸，全部走同一个「屏幕 → 裁剪空间 → 世界轨道坐标」的逆变换。
- **真·游戏特效**：命中特效不是自己糊的粒子，而是谱面核心自带的 pjsk 粒子系统（`effect.png` + 内嵌效果定义），帧序、时长、叠加混合与官方一致。
- **音频时钟即主时钟**：不用 wall clock 猜时间，直接从音频设备的 PCM 帧计数推 `songTime`，音画天然对齐。
- **自动吃掉官方音频的开头静音**：官服 mp3 前面有 ~9 秒填充，谱面 tick 0 在那之后。程序会自己扫出来（或读 sidecar），不需要手动对轴。
- **选歌 → 演奏 → 暂停弹窗 → 成绩记录**，一套完整流程：`确定` / `重试` / `放弃` / `继续演出`，通关与 FULL COMBO 存进 `scores.json`。
- **系统集成**：Windows 媒体浮层（SMTC）显示曲名/进度，任务栏按钮上跑进度条。
- **无头自检**：`--screenshot` 能不开窗口跑一帧存 PNG，改渲染不用靠肉眼盯屏幕。

---

## 2. 30 秒跑起来

```bash
bash setup.sh          # 拉工具链（zig 0.14.1 + SDL2 2.32.10）和贴图/音效资源
bash build.sh          # 编译，产物在 build/
cd build && ./cppsekai.exe
```

想顺便来两张测试谱面：

```bash
bash setup.sh --charts     # 额外下载 0075 / 0127 两张谱 + BGM 到 charts/
```

**不需要** Visual Studio、cmake、vcpkg、Python 环境。Windows 7 SP1 起，2008 年之后的核显就能跑。

---

## 3. 操作

| 输入 | 操作 | 说明 |
|---|---|---|
| 键盘 | `Z S X D C V G B H N J M` | 12 个键 = 12 条轨，`Z` 在最左 |
| 键盘 | `SPACE` | 暂停（开暂停弹窗） |
| 键盘 | `F` / `H` / `ESC` | 全屏切换 / 调试面板 / 返回·退出 |
| 鼠标 | 左键 / 右键 | 各算一个指针（可同时压两个轨）；按住不放 = 长条 |
| 鼠标 | 拖拽上 / 左 / 右 | Flick，方向必须与箭头一致（严格模式下） |
| 触摸 | 多指 + 上滑 | 多指同时判定，上滑 = flick |
| 鼠标/触摸 | 点 HUD、面板、弹窗 | **永远不当作击打**（先做 ImGui 占用检测 + 暂停按钮命中测试） |

---

## 4. 命令行

```
cppsekai [--sus <file.sus>] [--bgm <audio>] [--charts <dir>]
         [--offset <sec>] [--filler <sec>] [--auto] [--speed <1-12>]
         [--se-volume <0-1>] [--lead-in <sec>] [--cover <image>]
         [--screenshot <png>] [--screenshot-time <sec>] [--pjsk-font]
         [--title/--lyricist/--composer/--arranger/--vocal/--difficulty <text>]
         [--width <px>] [--height <px>] [--window borderless|windowed|fullscreen]
         [--fps <n>] [--judge-sheet] [--test-hits] [--show-pause-dialog]
```

| 参数 | 作用 |
|---|---|
| `--sus` | 指定谱面；**不给**就进选歌界面 |
| `--charts` | 谱面目录（默认依次试 `exe/charts` → `exe/../charts` → `./charts`，取第一个非空的） |
| `--filler` | BGM 开头静音秒数；不给则自动检测 |
| `--offset` | 手感微调（秒，正 = 音乐更晚出） |
| `--speed` | 音符速度 1–12（设置面板里也能拖） |
| `--lead-in` | 开场卡片 + 淡入的提前量，默认 6.0s，下限 5.8s |
| `--auto` | 自动演示：特效由谱面时间轴触发，不接受输入 |
| `--screenshot` | 无头跑一帧存 PNG 后退出，日志写 `cppsekai.log` |
| `--test-hits` | 不按键，按时间轴把每个音符自动喂给判定引擎（查特效链用） |
| `--window/--width/--height/--fps` | 窗口模式与帧率上限（0 = 只靠垂直同步） |
| `--pjsk-font` | 用自带的 pjsk 字体，默认跟随系统 UI 字体 |

---

## 5. 架构：三层，边界非常硬

```
┌──────────────────────────────────────────────────────────────────────┐
│  game/          玩法与界面（本项目新增）                              │
│    Judgement.*  判定引擎：事件流 → Perfect/Great/Good/Miss、combo、分 │
│    Hud.*        分数 / 生命 / 连击 / 判定字（1920x1080 虚拟坐标）      │
│    Intro.*      开场卡片：曲绘 + 作词作曲编曲 + 1.8s 舞台淡入          │
│    SongSelect.* 选歌：扫谱面、配 sidecar、分组、成绩、详情面板         │
│    Ui.*         pjsk 弹窗组件库：卡片/页签/滑条/信息行/胶囊钮/开关      │
├──────────────────────────────────────────────────────────────────────┤
│  platform/      平台层（本项目新增）                                   │
│    Renderer.*   OpenGL 3.3 core，消费核心吐出的 packed quad           │
│    Audio.*      miniaudio：BGM + 10 种判定音 × 4 声部池 + hold 循环音  │
│    CoreApi.cpp  C 接口包装 + #WAVEOFFSET 文本扫描                     │
│    SystemMedia.* 手写 WinRT vtable：SMTC 媒体浮层 + 任务栏进度条       │
│    main.cpp     SDL2 窗口/事件循环/输入映射/ImGui/截图模式             │
├──────────────────────────────────────────────────────────────────────┤
│  core/native/   谱面核心（上游代码，结构不动）                         │
│    mmw_preview.cpp    SUS 解析 → 数据模型 → 绘制数据 → 事件流          │
│    mmw_port/          MikuMikuWorld 移植层（音符/特效/粒子/相机）      │
└──────────────────────────────────────────────────────────────────────┘
```

**数据流一条线：**

```
loadSusTextPrecise ──► 核心全局状态
        │
        ├─ getHitEventBuffer() ─► JudgementEngine::load ─► 每帧 update(songTime)
        │                                                    └─► tap/flick 输入
        │
        └─ render(songTime) ─► 25 floats/quad ─► Renderer::renderFrame ─► glDrawArrays

AudioEngine::songTime()  ← 唯一时钟源 →  判定、渲染、HUD、SMTC 全部读它
```

核心**完全不碰 OpenGL**。它只往两个 buffer 里塞数：一个是「要画什么」（quad），一个是「什么时候该响」（hit event）。
平台层想画在浏览器里就画在浏览器里（上游就是 WebGL），想画在原生窗口里就画在原生窗口里——这就是这套东西能被搬下浏览器的原因。

---

## 6. 原理详解

### 6.1 时间：tick 是唯一真理，秒是算出来的

谱面时间是 **tick**（`TICKS_PER_BEAT = 480`），不是秒。因为 BPM 会变、`#SPEED` 会变，秒和 tick 从来不是线性关系。核心维护三条互相换算的轴：

| 函数 | 输入 → 输出 | 用途 |
|---|---|---|
| `accumulateTicks` | 秒 → tick | 把播放器的当前时间拉回谱面坐标系 |
| `accumulateDuration` | tick → 秒 | 判定、音效、事件的真实时间 |
| `accumulateScaledDuration` | tick → 视觉秒 | **受 hiSpeed 影响**，决定音符画在哪 |

关键点：**判定用真实秒，下落位置用视觉秒**。所以改音符速度只让音符跑得更快，不会让歌对不上。每帧 `render()` 入口就是这几行换算。

### 6.2 空间：没有 3D，只有一个假透视

轨道坐标 `x` 在高度 `y` 上，被画到世界坐标 `(x·y, y)`：

```cpp
// core/native/src/mmw_preview.cpp
QuadPoints perspectiveQuadvPos(float left, float right, float top, float bottom) {
    return {{ {right*top, top}, {right*bottom, bottom}, {left*bottom, bottom}, {left*top, top} }};
}
```

- `y = 1` 是判定线（最宽、最靠下），`y → 0` 是消失点（最窄、最靠上）
- 12 轨的轨道坐标是 `lane - 6 + width/2`，即左半 −6…0，右半 0…6

好处是判定只要一次除法就能把屏幕点还原成轨道坐标，不需要射线求交：

```
窗口像素 → 裁剪空间(clipX, clipY) → worldX / worldY
         → lanePos = worldX / worldY      // 撤销假透视
```

`Renderer::clipToWorldX/Y` 就是这条逆变换（外加 letterbox 的宽高比修正），输入命中和悬停高亮全靠它。

### 6.3 渲染：核心吐数据，平台层画

每个 quad 被打包成 **25 个 float**：

```
[0..11]  4 个顶点 × (x, y, 1/w)      ← 1/w 让透视插值走硬件
[12..19] 4 个顶点 × (u, v)           ← 纹理坐标
[20..23] r, g, b, a
[24]     textureId
```

`textureId` 决定走哪条路：

| id | 贴图 | 坐标空间 | 混合 |
|---|---|---|---|
| 0 | `notes_01.png` | 世界轨道坐标（要 `worldToClip`） | 普通 alpha |
| 1 | `longNoteLine_01.png` | 同上 | 普通 |
| 2 | `touchLine_eff_01.png` | 同上 | 普通 |
| 3 | `effect.png` | **已经是裁剪空间**（核心算好了） | 普通 |
| 4 | `effect.png` | 裁剪空间 | **加法混合** |

顶点着色器里 `gl_Position = vec4(aPos * w, 0, w)`——手动把透视除法交回硬件，这样 4 个顶点可以各带深度，做出真正的梯形收束。

渲染顺序由核心的 `zIndex` 决定（`stable_sort`），平台层只做一件事：**把相邻且贴图相同的 quad 攒成一批再 `glDrawArrays`**，减少状态切换。

### 6.4 判定：事件流 + 时间窗 + 轨道覆盖

SUS 解析完，核心顺带生成 `HitEvent` 流（7 floats/条，按时间排序）：

```
[0] timeSec  [1] center  [2] width  [3] kind  [4] flags  [5] endTimeSec  [6] volume
kind : 0=tap  1=critical tap  2=flick  3=trace  4=hold tick  5=hold 标记
flags: bit0 = critical, bits1-2 = flick 方向 (0 无 / 1 上 / 2 左 / 3 右)
```

判定引擎就是一个游标 + 滑动窗口：

```
输入的那一帧：
  lanePos = 屏幕点逆变换得到轨道坐标
  在 [cursor, cursor+256) 里找 state==0、|dt| 最小、
  且 (note.center ± width/2 ± margin) 盖住 lanePos 的那个音符
  dt ≤ 40ms → PERFECT    dt ≤ 90ms → GREAT    dt ≤ 140ms → GOOD
```

- **超时自动 MISS**：`update()` 每帧扫过 `timeSec < songTime - missAfterMs` 的音符，记 MISS 并把 combo 归零。
- **Flick 严格方向**：flick 音符只在滑动方向匹配时才算过（上滑音符接受「上 / 无方向」，左右滑必须对应方向）；严格模式下点按永远不会清掉 flick，反之亦然。可在设置面板里关。
- **Hold**：kind 5 是 hold 标记，配合同轨同刻的 tap 判定决定「起手是否成功」；之后每帧检查该轨是否仍被按住，掉了且离结束还早 → 断连。hold 期间的 kind 4 tick 自动完美通过。
- **分数**：PERFECT 1000（critical 1500）/ GREAT 800 / GOOD 500，combo 与 maxCombo 另算。
- **特效联动**：每次成功命中，把该音符的 `center/width/kind/flickDir/critical/friction` 回灌给核心的 `triggerNoteEffect()`，让核心的粒子系统在正确的轨、正确的时刻放正确的特效。

### 6.5 音频：音频时钟是主时钟

```
音乐文件位置 = songTime + startPos + userOffset
```

- `songTime` 由 `ma_engine_get_time_in_pcm_frames()` 推出来，**不是** `SDL_GetTicks`——用的是音频设备自己的时钟，永远不会和声音漂移。
- `startPos` 是「谱面 tick 0 对应文件第几秒」。官服 mp3 前面有 ~9 秒静音，所以这个值 ≈ 9。优先级：**sidecar `fillerSec`/`offset` > 自动检测 > 0**。
- 自动检测：用 miniaudio 以 44100Hz 单声道解码开头 20 秒，1024 帧一块找第一个峰值 > −45 dBFS（`184/32768`）的采样；小于 0.3 秒就当编码间隙，不算填充。
- SUS 的 `#WAVEOFFSET`（秒）叠加在 `startPos` 上。
- **开场前摇**：`start(leadIn)` 先把时钟锚在 `-leadIn` 秒，`songTime` 从负值爬到 0 才真正 `ma_sound_start`，所以开场卡片播完的瞬间音乐正好起。
- **暂停 / 恢复**：暂停记下 `songTime` 并停掉声音；恢复时重新 seek 到 `pauseSongTime + startPos + userOffset` 并把帧锚点补回去，时钟不会跳。
- `lead-in` 最短 5.8s = 开场卡片 4.0s + 舞台淡入 1.8s（上游时序）。

### 6.6 界面：立即模式 + 1920×1080 虚拟坐标

HUD 和弹窗全部是 ImGui 立即模式画的，统一在 **1920×1080 虚拟空间**里按留黑缩放（`scale = min(w/1920, h/1080)`）：

- HUD 走 **background draw list**（在 GL 帧之上、弹窗之下）
- 弹窗卡片走正常窗口层 → 所以暂停弹窗天然盖住 HUD，还能整屏压暗
- `ui::` 是一套按 pjsk 观感手搓的组件库：卡片缩放入/出场动画、可拖标题栏、圆角页签、深色 ± 按钮 + 薄荷轨道的滑条、灰底粉值信息行、胶囊按钮、粉色开关、数字步进器
- 所有坐标改动前先看 `ui::scale()`（以 860p 为基准）和 `kHeaderH`，否则卡片内容会压住底部按钮

### 6.7 系统集成：没有 SDK，就手写 vtable

这个工具链里没有 Windows SDK，所以 WinRT / COM 接口是**手写虚表**的：

- IID 和方法槽位顺序来自解析 `C:\Windows\System32\WinMetadata\Windows.Media.winmd`（元数据里方法的声明顺序就是 ABI 顺序）
- `combase.dll` 的函数全部 `LoadLibrary` 动态拿，不需要导入库
- SMTC 只在 Windows 10+ 真正生效；任务栏进度条（`ITaskbarList3`）Win7 就有
- 每一处失败都降级成 no-op，缺哪个都不会崩

### 6.8 启动：把黑屏压到 0.6 秒

启动是一条被精心排序的流水线，每步都有 `[boot]` 计时：

```
窗口 + GL 上下文 → glClear 立刻换一帧（窗口不再是「没画过」的黑块）
  → loadSplash()（只加载 background + stage）→ 再换一帧 → 约 0.6s 有画面
  → 其余贴图 + HUD 精灵图 → CJK 字库图集 → 整备完成（约 1.3s~3.5s，看机器）
```

细节：HUD 精灵图优先读 `assets/mmw/overlay_opt/`（离线用 `.workbuddy/tools/shrink_hud.cpp` 按整数倍 alpha 加权缩到 512px，把 HUD 加载从 ~1.0s 压到 ~0.3s）；字体先找系统字体文件，再探测是否有 CJK 字形——Yu Gothic UI 是 CFF 轮廓，stb_truetype 渲染不了，会自动落到 Microsoft YaHei UI。

---

## 7. 数据文件

### 谱面目录（`charts/`，不入库）

每首歌一组，按文件名配对：

```
charts/
  0075.mp3               BGM（官服音源，开头 ~9s 静音）
  0075.png               曲绘
  0075_master.sus        谱面
  0075_master.json       sidecar（可选，装 SUS 里没有的字段）
```

sidecar JSON 支持的字段：`title` `artist` `lyricist` `composer` `arranger` `vocal` `difficulty` `level` `mv`，
以及音频对齐用的 `fillerSec`（秒）或 `offset`（毫秒，上游 Web 预览的参数名）。

### 其他数据

| 文件 | 位置 | 作用 |
|---|---|---|
| `scores.json` | exe 旁边 | 通关 / FULL COMBO 记录，按谱面文件名作 key |
| `music-levels.json` | 根目录 / exe 旁边 / 上级目录 | 官方等级表（unipjsk 导出的 SUS 被剃掉了 `#PLAYLEVEL`）。支持 `{"75":[6,13,17,23,28]}` 或游戏原版 `musicDifficulties.json` 格式 |
| `musics.json` | 根目录 | 官方曲库元数据（曲名、作词作曲、曲绘资产名），供对齐 / 参考 |

---

## 8. 构建的坑（都是血换的）

- **必须 zig 0.14.1**。0.16 的 `zig c++` 驱动会吞掉 `-I`，编译必挂。
- **zig 编译缓存必须放 C 盘**（`build.sh` 已设 `ZIG_GLOBAL_CACHE_DIR`）。D 盘文件系统不支持 zig 缓存需要的文件操作，会报 `CacheCheckFailed / AccessDenied`。
- **`main.cpp` 必须在 include `SDL.h` 之前 `#define SDL_MAIN_HANDLED`**，否则 `main` 被重定义成 `SDL_main`，程序变成「秒退且无输出」。
- **MinGW 的 `gl.h` 只有 GL 1.1**：GL 3.3 的函数指针和常量在 `platform/Renderer.cpp` 顶部的 `namespace gl` 里手工声明 + `SDL_GL_GetProcAddress` 动态加载。加新 GL 调用先去那里补。
- **SDL2 的 MinGW 导入库**需要额外链 `imm32/setupapi/version/oleaut32/winmm`，还得自己 stub 三个屏保符号（`ScreenSaverProc` 等，在 `main.cpp` 顶部）。
- **`MINIAUDIO_IMPLEMENTATION` 只在 `platform/Audio.cpp` 定义一次**，别的地方只 include 头。
- **资源按 exe 所在目录解析**（`SDL_GetBasePath()`），不按 CWD——这样双击和命令行行为一致。

---

## 9. 目录

```
CppSekai/
  main.cpp            窗口 / 事件循环 / 输入映射 / ImGui / 设置面板 / 截图模式
  core_api.hpp        C 接口的 C++ 包装
  core/native/        谱面核心 + MikuMikuWorld 移植层（上游代码，能不改就不改）
  game/               判定 / HUD / 开场 / 选歌 / pjsk 弹窗组件库
  platform/           渲染器 / 音频 / 系统媒体 / 核心包装
  third_party/        imgui, miniaudio, stb_image, DirectXMath, nlohmann-json
  assets/             贴图与判定音效（不入库，setup.sh 拉）
  charts/             谱面与音频（不入库）
  docs/               README 截图
  build.sh setup.sh   编译 / 一次性环境准备
  AGENTS.md           给 AI 助手的项目指南（改代码前先读）
```

---

## 10. 待办

1. **生命值是假的**——`hudState.lifeRatio` 目前硬编码 `1.0f`，所以实际是 no-fail 模式，需要接真实的扣血 / 回血
2. **结算画面**（当前只写 `scores.json`，不弹结算）与连击特效（`judge v3` 贴图已在 `assets/` 但没用上）
3. **输入 / 音频延迟校准界面**（现在只有手动拖 `--offset` 或设置面板滑条）
4. **键盘 12 键布局**可能不顺手，考虑做成可配置 / 支持自定义键位
5. **hold 音效循环**已接通，但键盘按下时 SE 的 kind 区分仍可细化

---

## 11. 授权与素材

- 本仓库整体遵循 **AGPL-3.0-only**，改动必须保持开源。
  - 上游：[sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web)（AGPL-3.0）——谱面核心与渲染布局来自这里
  - 再上游：MikuMikuWorld（MIT）——`core/native/mmw_port/` 的移植来源
  - 仓库里目前**还没有 `LICENSE` 文件**，建议补一份
- `assets/` 下的贴图与音效是 Project SEKAI 的官方素材，`charts/` 是官方谱面数据，**仅限本地游玩，不入库、不再分发**。
- 版权归 SEGA / Colorful Palette 所有，本项目与官方无关。
