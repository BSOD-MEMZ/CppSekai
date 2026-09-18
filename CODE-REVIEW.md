# 代码体检（CODE-REVIEW.md）

> 2026-09-18 实测。目的只有一个：**说清楚哪几个文件是「一改就得翻半天」的地方**，
> 以及按改动成本排的处置顺序。不是架构纯洁度评分，也不劝长期重构——
> 按你自己说的节奏来。
>
> 所有数字都是量出来的（`wc -l` / 括号配平脚本），不是印象。

---

## 一、体量分布

本项目原创代码（排除 `third_party/`、`core/native/` 移植层）：

| 文件 | 行数 | 其中最大的单个函数 |
|---|---|---|
| `main.cpp` | **6,443** | `main()` **5,713 行**（第 730 行起） |
| `game/SongSelect.cpp` | 3,856 | `drawSongSelect()` **~1,746 行**（第 2104 行起） |
| `downloader/chartdl.cpp` | 2,562 | — |
| `platform/Renderer.cpp` | 1,355 | — |
| `game/Ui.cpp` | 1,310 | — |
| `game/Judgement.cpp` | 910 | — |
| `game/Result.cpp` | 887 | — |
| `platform/SystemMedia.cpp` | 873 | — |
| `game/Intro.cpp` | 843 | — |
| 其余 `game/` `platform/` | ~4,800 | 都在 700 行内 |
| **合计** | **23,840** | |

判断：**`game/` 和 `platform/` 的拆分是健康的**——判定、UI、HUD、结算、特效、舞台背景
各自成文件，基本都在 1000 行内，职责清楚。问题**全部集中在 `main.cpp` 和
`SongSelect.cpp` 的两个巨型函数上**。

---

## 二、主要问题（按「一改要翻多久」排序）

### 1. `main()` = 5,713 行 ★最贵

`main.cpp:730` 到文件结束几乎全在一个函数里。它一次干完：

```
参数解析（~300 行）  →  单实例 / 多开 / profile 选择决策（~200 行）
  →  SDL + GL + ImGui 初始化（~250 行）
  →  资源加载：字体 / HUD 贴图 / 舞台背景 / 谱面核心 / 音频（~400 行）
  →  帧循环（~2,500 行）
  →  关停 + 存档（~60 行）
```

代价：任何一个改动（比如我这次改失血阴影）都得在 5,700 行里先定位。
`main` 的顶层作用域里有 **283 条声明**（含其中定义的 lambda），全部互相可见
——没有编译期约束能拦住"改错一个变量"。

### 2. 帧循环体 ~2,500 行，三种画面共享一个作用域

`main.cpp:3989` 的 `while (running)` 里用

```cpp
if (state == AppState::Select) { ... }
else if (state == AppState::Play) { ... }
else if (state == AppState::Result) { ... }
```

串起三个画面，全文 **27 处** `state == AppState::` 判断散落在循环各处（不只是这三个分支）。
三个画面的临时变量（`songTime`、`resultElapsed`、`confirmFlashActive`、`hudState`……）
全在同一个作用域里，命名上只能靠前缀区分。

### 3. `drawSongSelect()` ≈ 1,746 行，13 个参数里有 5 个 in/out 引用

```cpp
int drawSongSelect(Renderer&, const std::vector<ChartEntry>&, int& selected,
    int windowW, int windowH, float timeSec, int& sortMode, int& groupMode, int& vocalIndex,
    float uiScale, ImVec2* confirmCenter, const AccountData*, const SelectPartyInfo*,
    SelectPartyResult*);
```

`selected` / `sortMode` / `groupMode` / `vocalIndex` / `confirmCenter` / `partyOut`
都是调用方（`main`）的状态，靠引用"远程改写"。**函数签名本身就是应用状态**。
（难度槽位那个「连不上」的 bug 就出在这个函数每帧反推 `selected` —— 见 AGENTS.md。）

### 4. `drawSettingsCard` lambda ≈ 769 行

`main.cpp:2684-3453`。4 个页签靠 `if (tab == N)` 展开，每个页签 100~300 行。

### 5. 设置页的 static「影子变量」——平行状态要手工同步

`main.cpp:3021-3029`：

```cpp
static float perfect = 40.0f, great = 90.0f, good = 140.0f;
static float bad = 200.0f, miss = 200.0f;
static float holdTail = 180.0f, holdStart = 140.0f, holdPreset = 0.0f;
static bool  linkBadMiss = true;
if (settingsJustOpened) { /* 逐字段从 judgement.windows() 抄一遍 */ }
```

同一份数据存两处，靠 `settingsJustOpened` 手工对齐。加一个判定参数要改三处
（`JudgementWindows`、这里的 static、那一段抄写）。

`main.cpp` 里这种**函数内 `static` 共 44 个**（`settingsAlive`、`lastTab`、`tabIn`、
`customW/H`、`winMode`、`fpsLimitF`、`bgMode`、`blurPending`……）。
大部分是"懒初始化一个 UI 工作副本"，能跑，但让函数的隐式状态难以追踪。

### 6. `Ui.cpp` 的卡片状态：字符串 id + 全局表

```cpp
CardState& st = cardState(id);   // static std::unordered_map<std::string, CardState>
```

卡片状态藏在按字符串 key 索引的全局 map 里（外加一个 `gOpenCard` 指针）。
这次做整卡缩放又给它加了 4 个字段（`vtxBase` / `targetList` / `pivot` / `k`）——
一个"动画进度"结构慢慢变成了"渲染中途状态"的集合。

### 7. `build.sh` 的源文件列表手工维护

`build.sh:39-57` 的 `SOURCES=(...)`。加了新 `.cpp` 忘了加进去，要到**链接期**才报
undefined symbol（2026-09-13 加 `game/Result.cpp` 时踩过）。改成 glob
`game/*.cpp platform/*.cpp` 就能永久消除。

### 8. `roomOpen` 与 `party.active()` 语义重叠

```cpp
const bool roomOpen = platform::PartyLink::roomExists();   // main.cpp:1313 机器上有房间
party.active()                                            // 本窗口进了房间
```

两者在 `main.cpp:4217` 那种条件里并排出现（`!party.active() && !roomOpen`）。
"找到房间但没加入"是个真实状态，所以两个变量都需要——但**没有一个地方写清楚它们的区别**。

---

## 三、干净的地方（别动）

体检不是只挑毛病，这几条是实测的好的一面，改的时候别顺手"优化"掉：

- **几乎没有裸全局变量**。全项目模块级可变全局只有 9 个，其中 `main.cpp` 占 3 个
  （`gFillerSec` / `gUserOffsetSec` / `gForceFlickLog`，`:56-58`），其余 4 个在
  `SongSelect.cpp` 的选曲背景、2 个在 `chartdl.cpp`。别的文件级状态都关在
  **匿名 namespace** 里（`Ui.cpp` 的 `gSeAudio`/`gSeVolume`/`gSeRequest`、
  `Party.cpp` 的 `gBlock`），外部碰不到——这是对的写法。
  代价是 `main` 太长，但**不要用"抽成全局"来解决**，那会更糟。
- **0 个 `TODO` / `FIXME` / `HACK` 注释**。全项目搜索无命中。
- **注释质量高**：每个非显然的决定都写了理由，包括踩过的坑（`#define SDL_MAIN_HANDLED`、
  日志缓冲假死、`fs::path` 编码、难度槽位……）。这是这个仓库最值钱的部分，**比代码结构值钱**。
  重构时**注释要跟着搬**，别丢。
- **`game/` 的分层是对的**（判定 / UI / HUD / 结算 / 特效 / 舞台各自成文件）。
- `platform/` 把 Renderer / Audio / SystemMedia / Party 包在核心之上，边界清楚。

---

## 四、处置顺序（按改动成本排，不按架构纯洁度）

| # | 做什么 | 风险 | 收益 |
|---|---|---|---|
| 1 | **`build.sh` 的 `SOURCES` 改 glob** | 极低 | 永久消除"忘了加文件"这一类事故。改动 5 行 |
| 2 | **`main()` 里的成块逻辑抽成函数**：参数解析、启动决策（单实例/多开/profile）、三个画面的 draw | 低（**只搬代码不改逻辑**） | 5,713 → 每块 300~500 行，定位成本断崖式下降 |
| 3 | **`drawSongSelect` 的 in/out 引用收进一个 `SelectState` struct** | 中 | 13 参数 → 3~4 个；顺带让"每帧反推 selected"这类 bug 更难写出来 |
| 4 | **设置页 shadow 变量换成一个 struct** | 低 | 加判定参数从改 3 处变 1 处 |
| 5 | **给上游代码补许可头**（`mmw_preview.cpp`、`mmw_port/**`） | 极低 | 这是**合规**，不是重构。见 CREDITS.md 第一节末 |

**第 1、5 条是当天就能做完的小事**；第 2 条是收益最大的一步，而且因为它
"只搬代码不改逻辑"，出错概率比看起来低得多——建议一次搬一块、搬完跑
`.workbuddy/tools/mp_verify.sh` + 无头 `--screenshot` 对一遍。

---

## 五、一句话结论

**不是烂代码，是"长在一个函数里"的代码。** 拆文件这件事做过且做对了（`game/`、
`platform/` 都很健康），只是 `main.cpp` 和 `SongSelect.cpp` 的两个函数从来没拆过。
把它们拆开，这个项目的可维护性会从"改一处要翻 5000 行"变成正常水平；
不拆也能继续跑，代价是每次改动都更慢、更容易碰坏别的东西。
