# 代码体检（CODE-REVIEW.md）

> **2026-09-18 首测，2026-09-21 复检。** 目的只有一个：**说清楚哪几个函数是
> 「一改就得翻半天」的地方**，以及按改动成本排的处置顺序。不是架构纯洁度评分，
> 也不劝长期重构——按你自己的节奏来。
>
> 所有数字都是量出来的（`wc -l` / 括号配平脚本 / `-fsyntax-only` 实测），不是印象。
> 复检手法：写个 Python 脚本扫描**同一嵌套层级内最长的花括号块**来量函数体，
> 但要人工核对——`namespace { ... }`、`if (tab == N) {`、`} else if (...) {`
> 都会被误报成巨型函数，必须按行号回查（脚本用完已删，别在仓库里留探针）。

---

## 〇、复检（2026-09-21）：三天涨了 3,393 行

| 文件 | 09-18 | 09-21 | 变化 | 最大单函数 |
|---|---|---|---|---|
| `main.cpp` | 6,443 | **7,489** | +1,046 | `main()` **6,641** |
| `game/SongSelect.cpp` | 3,856 | **4,324** | +468 | `drawSongSelect()` **~1,920** |
| `downloader/chartdl.cpp` | 2,562 | **3,414** | +852 | 205（`main`） |
| `game/Ui.cpp` | 1,310 | 1,770 | +460 | 200（`stepper`） |
| `platform/Renderer.cpp` | 1,355 | 1,385 | +30 | 67 |
| `game/Judgement.cpp` | 910 | 958 | +48 | 193（`load`） |
| `game/Intro.cpp` | 843 | 915 | +72 | 59 |
| `platform/SystemMedia.cpp` | 873 | 873 | 0 | 72 |
| `game/Result.cpp` | 887 | 859 | −28 | 58 |
| **原创合计** | 23,840 | **27,233** | **+3,393** | |

**结论没变，但差距拉大了**：`game/` `platform/` 依然健康——**这次给所有文件都算了最大
单函数，除 `main.cpp` 和 `SongSelect.cpp` 外，全项目最大的单个函数是 205 行**
（`chartdl.cpp` 的 `main`），次大 193 行（`JudgementEngine::load`）。
`chartdl.cpp` 虽然涨了 852 行，但它是**加函数**涨的，不是加在一个函数里——这是对的涨法。

**唯一在恶化的东西是 `main()` 和 `drawSongSelect()` 这两个函数本身的体量。**

---

## 一、体量分布（2026-09-21）

| 位置 | 行数 | 位置 |
|---|---|---|
| `main()` | **6,641** | `main.cpp:849-7489` |
| └ 其中 `runFrame` lambda（帧循环） | **2,731** | `main.cpp:4476-7206`（`:7206` 自带 `// end runFrame`） |
| └ 其中 `drawSettingsCard` lambda | **875** | `main.cpp:2987-3861` |
| └ 其中参数解析 + 初始化 | ~1,140 | `main.cpp:849-1942` |
| `drawSongSelect()` | **~1,920** | `SongSelect.cpp:2397-4324`（到文件尾） |

`main()` 内部的段落横幅（`// ---...---` 注释）本身是清楚的，能直接当目录用：
参数解析 → SDL+GL → 实例策略 → 多人加入 → 资源 → 谱面核心 → 音频 → 判定 →
UI 字体 → SMTC → 主循环 → party 运行时 → 转场 → 恢复倒计时 → profiles → 设置卡片 →
多开确认框 → 指针输入 → 手柄输入 → 震动 → 开场跳过 → **runFrame**。

**问题不是"乱"，是"全在一个作用域里"**：`main.cpp` 里 `state == AppState::` 出现
**27 处**，`main()` 顶层声明的 lambda **59 个**（含 `runFrame` / `drawSettingsCard`），
函数内 `static` **55 个**（09-18 是 44 个，**涨了 11**）。任何一处改动都得先确认
"这个变量在这个作用域有没有被别处用过"。

---

## 二、老问题（09-18 提的，09-21 复检状态）

### 1. `main()` = 6,641 行 ★最贵 —— **仍在恶化（+928）**

### 2. 帧循环 `runFrame` 2,731 行，三种画面共享作用域 —— **仍在恶化**
09-19 起这段正文被 `runFrame = [&]() {...}` 包住（`main.cpp:4476-7206`，拖动窗口时
要从消息钩子里重入）。**包一层 lambda 没有减少任何作用域泄漏**——它只解决了重入，
`state == AppState::` 的 27 处判断、三个画面的临时变量全共享一个作用域，原样保留。

### 3. `drawSongSelect()` ~1,920 行 —— **仍在恶化（+174）**
签名 13 个参数，其中 `selected` / `sortMode` / `groupMode` / `vocalIndex` /
`confirmCenter` / `partyOut` 是调用方的 in/out 引用，靠引用"远程改写"。
**函数签名本身就是应用状态。**

### 4. `drawSettingsCard` lambda 875 行 —— **仍在恶化（+107）**
`main.cpp:2987-3861`。4 个页签靠 `if (tab == N)` 展开。

### 5. 设置页的 static「影子变量」—— **仍在（`main.cpp:3420`）**
```cpp
static float perfect = 40.0f;      // main.cpp:3420
if (settingsJustOpened) { ... }    // main.cpp:2995 / 3429
```
同一份数据存两处，靠 `settingsJustOpened` 手工对齐。**加一个判定参数要改三处**
（`JudgementWindows` 定义、这里的 static、那段抄写）。

### 6. `main.cpp` 函数内 `static` 55 个，`getenv` 9 个 —— **在涨**

### 7. `build.sh` 的 `SOURCES` 手工维护 —— **没做（还欠着）**
`build.sh:39` 起 28 条。加新 `.cpp` 忘加进去要到**链接期**才报 undefined symbol。
改成 glob 就永久消除。**这条 09-18 就排在第 1 位，一直没动。**

### 8. `roomOpen` 与 `party.active()` 语义重叠 —— **仍在**

---

## 三、本次新发现（09-21）

### 9. 同一份 `json.hpp` 存了两份 → **静默 ODR 风险** ★不是「删掉就行」

```
third_party/nlohmann/json.hpp        25,830 行  ← 本项目自己的代码走 -Ithird_party
core/native/vendor/nlohmann/json.hpp 25,830 行  ← 上游 mmw_preview.cpp 相对路径钉住
md5 完全相同：83e2e643e7ef52e95511044d07110148（都是 nlohmann/json 3.12.0）
```

两份**逐字节相同**的第三方头，各被不同的翻译单元包含：

```cpp
game/SongSelect.cpp:30          #include <nlohmann/json.hpp>        → third_party 那份
core/native/mmw_port/JsonIO.h:4 #include <nlohmann/json.hpp>        → third_party 那份
core/native/src/mmw_preview.cpp:28  #include "../vendor/nlohmann/json.hpp"  → vendor 那份
```

**为什么不能简单删一个**：上游那句是**相对路径**，删掉 `vendor/` 就编不过；
改它又要动上游文件（AGENTS.md：「勿改结构」）。

**真正的风险**是 ODR：`json.hpp` 是 header-only，类定义和 inline 函数会进每个 TU。
两份拷贝今天一模一样所以没事，但**只要将来只升其中一份**，两个 TU 里就会出现
不同的 `nlohmann::json` 定义，链到一起是**静默的未定义行为**——不是编译错误。
nlohmann 自带的版本检查（`json.hpp:60-66`）只在本 TU 内生效，
**跨 TU 完全不报**：

```cpp
#if NLOHMANN_JSON_VERSION_MAJOR != 3 || ... != 3.12.0
    #warning "Already included a different version of the library!"   // ← 只是 warning，且仅限单 TU
#endif
```

**最小修法**：把 `core/native/vendor/nlohmann/json.hpp` 的内容换成就一行
`#include <nlohmann/json.hpp>` 的转发头。上游那句相对路径照样能编过，
仓库里只剩一份实体，ODR 隐患根除。改动 1 个文件、1 行，不碰任何逻辑。

### 10. 「三级候选路径探测」手写了 7 遍 ★收益/成本比最高

`baseDir → .. → cwd` 这个**约定写在 AGENTS.md 里，实现靠手抄**：

```cpp
// main.cpp:2107（music-levels.json）/ 2122（musics.json）/
// 2136（music-vocals.json）/ 2151（music-aliases.json）—— 四段完全同构
for (const std::string& candidate :
    {baseDir + "musics.json", baseDir + "..\\musics.json", std::string("musics.json")}) {
    std::ifstream probe(candidate, std::ios::binary);
    if (probe.good()) { probe.close(); game::loadMusicMaster(candidate); break; }
}
```
同样形状还出现在 `:1676`（`icon.png`）、`:1874`（`charts` 目录候选）、`:2476`（`chartdl.exe`）。
**7 处 × 每处 5~8 行 = ~45 行可以压成 1 个 `std::optional<std::string> findDataFile(name)`**
加 7 行调用。纯机械改动，零逻辑风险，而且顺手把「三层候选」这个约定固化进代码。

### 11. 调试探针转正后散在生产路径里（16 处 `getenv`）

| 变量 | 位置 | 性质 |
|---|---|---|
| `CPSEKAI_UI_TRACE` | `main.cpp` **5 处**（`:3842/5018/5641/...`） | 无头验证用，转正了 |
| `CPSEKAI_MP_TRACE` | `main.cpp:4569/4622` | 同上 |
| `CPSEKAI_MSG_LOG` | `main.cpp:7267` | 同上 |
| `CPSEKAI_GLASS_TOGGLE` | `main.cpp:1610` | Win7 Aero 玻璃实验开关 |
| `CPSEKAI_VIGNETTE` | `main.cpp:6608` | 冻结暗角 |
| `CPSEKAI_MULTIASK` | `main.cpp:2983` | 无头开卡片 |
| `CPSEKAI_DEBUG_SYMBOLS` | `main.cpp:741` | 崩溃栈解析 |

外加 `--fake-pad`、`--chartdl-test` 两个命令行调试开关。
**这些本身是有价值的**（无头验证就靠它们，见 `.workbuddy/skills/win32-gui-headless-verify`），
问题是它们**以裸 `getenv()` 的形式嵌在 6,641 行的 `main()` 里**——每个都是一处要读过去的
分支。建议：集中到一个 `DebugSwitches` struct，在参数解析段一次读完，之后只读字段。
`getenv` 全局分布：`main.cpp` 9 / `SongSelect.cpp` 2 / `Renderer.cpp` 2 / `Intro.cpp` 1 /
`Ui.cpp` 1 / `Party.cpp` 1。

### 12. 编译没有 `-Wall`，但**代码其实是干净的** ★白捡

`build.sh:16` 的 `CXXFLAGS` 只有 `-std=c++20 -O2 -s`，**一个警告开关都没有**。
用 `-Wall -Wextra` 实测（`zig c++ -fsyntax-only`，逐文件，`-Wno-unused-parameter`）：

| 文件 | 警告数 |
|---|---|
| **`main.cpp`（7,489 行）** | **0** |
| `game/Judgement.cpp` `game/Ui.cpp` `game/Result.cpp` | **0** |
| `game/Hud.cpp` `game/Intro.cpp` `game/PartyScreen.cpp` | **0** |
| `game/StageBackground.cpp` `game/TapEffect.cpp` | **0** |
| `platform/Audio.cpp` `CoreApi.cpp` `FontOutline.cpp` | **0** |
| `platform/Renderer.cpp` `SystemMedia.cpp` `Party.cpp` | **0** |
| `game/SongSelect.cpp` | 4 |
| `downloader/chartdl.cpp` | **23** |

**15 个文件 0 警告**，其中包含 7,489 行的 `main.cpp` 和 1,385 行的 `Renderer.cpp`
——说明这份代码本来就干净，只是从来没开过警报器。
往 `CXXFLAGS` 加 `-Wall -Wextra` 成本是 0，收益是**以后的新问题当场暴露**。
**唯一需要先处理的是 `chartdl.cpp`**（见下条）。

### 13. `chartdl.cpp` 的 23 个警告里，藏着 **4 处死声明** ★

11 个 `-Wcast-function-type-mismatch`（`GetProcAddress` → 函数指针）和
8 个 `-Wmissing-field-initializers` 是 **Win32 惯用写法**，噪音，加个局部 `#pragma`
压掉即可。**但剩下 4 个是真的**：

| 位置 | 声明 | 情况 |
|---|---|---|
| `:1261` | `std::string gPendingBalloon;` | **全项目仅此一处出现**——从未读写 |
| `:1262` | `std::mutex gPendingMutex;` | 同上，**声明了却从未加锁** |
| `:1260` | `bool gNeedClearTicks = false;` | 从未读写 |
| `:1364` | `double gGuiStartSec = 0.0;` | 注释写着 `for --screenshot-time`，但从未读写——chartdl 的截图计时路径要么被删了、要么没写完 |

前两条合起来是**一个被放弃的功能**：托盘气泡通知（pending balloon）连互斥量一起
声明好了就没再碰。**「声明了却从没上锁的 mutex」是最容易骗人的东西**——读代码的人
会以为 `gPendingBalloon` 是线程安全的。**这 4 行建议直接删**（零引用，删了必编过）。

---

## 四、干净的地方（别动）

体检不是只挑毛病。这几条 09-18 实测过、09-21 复检仍然成立：

- **`game/` / `platform/` 的分层是对的**。09-21 补测：**除两个巨型函数外，
  全项目最大单函数 205 行**。这个数字比架构图更能说明问题——文件是拆过的。
- **0 个 `TODO` / `FIXME` / `HACK`**。09-21 复检仍然为 0。
- **注释质量是这仓库最值钱的部分**，而且**没有被巨型函数稀释**：
  `main.cpp` 纯 `//` 行占比 **26.4%**（7,489 行里 1,976 行是注释）。
  其余文件 3.3%~19.8%。改巨型函数时**注释要跟着搬，别丢**。
- **几乎没有裸全局变量**。模块级可变全局只有 9 个（`main.cpp` 3 个：
  `gFillerSec` / `gUserOffsetSec` / `gForceFlickLog`）。其余文件级状态关在
  **匿名 namespace** 里（`SongSelect.cpp:34/654/1297` 三处、`Ui.cpp`、`Party.cpp`），
  外部碰不到——这是对的写法。**不要用"抽成全局"来解决 `main` 太长，那会更糟。**
- **`Ui.cpp` 的卡片状态改善了**：`cardState()` 的 key 从 09-18 的
  `std::unordered_map<std::string, CardState>` **换成了 `ImGuiID`**
  （`Ui.cpp:124-126`）——每帧不再做字符串哈希。

---

## 五、处置顺序（按改动成本排，不按架构纯洁度）

| # | 做什么 | 风险 | 收益 | 状态 |
|---|---|---|---|---|
| 1 | **`build.sh` 的 `SOURCES` 改 glob** | 极低 | 永久消除"忘了加文件"事故，改 5 行 | **欠着** |
| 2 | **`CXXFLAGS` 加 `-Wall -Wextra`**（先删 `chartdl` 的 4 行死声明） | 极低 | 15 个文件实测 0 警告 → 白捡一个回归哨兵 | 新 |
| 3 | **删 `chartdl.cpp` 的 4 处死声明**（含一对从未使用的 balloon 功能 + mutex） | 极低 | 零引用，删了必编过；去一个骗人的"假线程安全" | 新 |
| 4 | **重复的 `json.hpp` 换成转发头**（`vendor/` 那份 → `#include <nlohmann/json.hpp>`） | 低 | 消除跨 TU 的静默 ODR 隐患，改 1 行 | 新 |
| 5 | **抽 `findDataFile()`，替换 7 处手抄的候选路径** | 低 | ~45 行 → ~8 行，把约定固化进代码 | 新 |
| 6 | **设置页 shadow 变量换成一个 struct** | 低 | 加判定参数从改 3 处变 1 处 | 欠着 |
| 7 | **调试开关集中进 `DebugSwitches`** | 低 | 16 处裸 `getenv` 收敛到 1 处 | 新 |
| 8 | **`main()` 里的成块逻辑抽成函数**：参数解析、启动决策、三个画面的 draw | 低（**只搬代码不改逻辑**） | 6,641 → 每块 300~500 行，定位成本断崖下降 | 欠着（**收益最大**） |
| 9 | **`drawSongSelect` 的 in/out 引用收进 `SelectState` struct** | 中 | 13 参数 → 3~4 个 | 欠着 |
| 10 | **给上游代码补许可头**（`mmw_preview.cpp`、`mmw_port/**`） | 极低 | 这是**合规**，见 CREDITS.md 第一节末 | 欠着 |

**1~7 条都是当天能做完的机械改动**，合起来能把仓库里"白白浪费的阅读成本"清掉一大半。
第 8 条是收益最大的一步，因为它"只搬代码不改逻辑"，出错概率比看起来低得多——
建议一次搬一块，搬完跑 `.workbuddy/tools/mp_verify.sh` + 无头 `--screenshot` 对一遍。

---

## 六、一句话结论

**不是烂代码，是"长在一个函数里"的代码；而且它还在长。**

拆文件这件事做过且做对了（`game/`、`platform/` 的健康度用"最大单函数 205 行"就能证明）。
问题从来没有扩散——**就是 `main()` 从 5,713 长到 6,641、`drawSongSelect()` 从 1,746
长到 1,920，两个函数，三天，+1,100 行**。其余增长（`chartdl.cpp` +852、`Ui.cpp` +460）
都是加函数，是健康的涨法。

所以真正需要的只有两个动作：**把这两个函数拆开**，以及**加上 `-Wall` +
`build.sh` glob + 把那份重复的 `json.hpp` 换成转发头**——后者加起来不到半小时，
但能让"不小心碰坏"从"上线后才发现"变成"编译期/链接期就报出来"。
