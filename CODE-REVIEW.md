# 代码体检（CODE-REVIEW.md）

> ### ⚠️ 免责声明
>
> CppSekai 是**非官方、非营利的爱好者作品**，与 SEGA、Colorful Palette 及
> 「プロジェクトセカイ カラフルステージ！ feat. 初音ミク」（Project SEKAI）官方
> **没有任何隶属、赞助、授权或认可关系**。本文只谈**代码结构**，不涉及素材；
> 但仓库里的美术 / 音频 / 谱面 / 数据表**权利全部归原权利人**，
> **仅限本机个人游玩与学习**，**禁止分发、公开传播与商业使用**。
> 详见 [COPYRIGHT.md](COPYRIGHT.md)。

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

### 12. 编译没有 `-Wall` —— **已开，结果完全推翻了我第一次的结论** ★

`build.sh:16` 的 `CXXFLAGS` 原本只有 `-std=c++20 -O2 -s`，**一个警告开关都没有**。

**先说一个测量教训**（我自己踩的，值得记住）：我第一轮体检是**逐文件**跑
`-fsyntax-only` 然后 `grep -c 'warning:'`，报出「15 个文件 0 警告，含 main.cpp」。
**这个结论是错的。** 因为我只数了 `warning:` 行、**没看有没有 `error:`** ——
几个 TU 其实是编译提前中止（缺头文件）了，于是 `warning` 计数为 0，被我读成"干净"。
**开 `-Wall` 之后跑一次完整 `build.sh`** 才是真的：

| | 第一次（逐文件，方法有缺陷） | 第二次（完整构建，可信） |
|---|---|---|
| 第三方 + 上游（`core/native/**`、`imgui`、`DirectXMath`） | — | **174** |
| **我们自己的代码** | 报 16 | **16**（数值巧合，见下条） |

所以「这份代码本来就干净」这个判断**方向是对的，但当时没有证据**。
真正的证据是下面这场修完之后的结果：**完整构建 0 error / 0 warning**。

### 13. `-Wall` 一开就抓到 **16 条真问题**，其中 1 条是真 bug ★★

这是整次体检里最值钱的产出——**开关装上第一天就回本了**：

| 文件 | 警告 | 性质 |
|---|---|---|
| `game/SongSelect.cpp:4257,4259` | `-Wself-assign-overloaded` | **真 bug**：`label = label;` —— 见下 |
| `game/SongSelect.cpp:319` | `-Wunused-function` | `difficultyBadgeColor()` 只是 `difficultyColor()` 的空壳，零调用 |
| `game/SongSelect.cpp:3692` | `-Wunused-variable` | `scrY1` |
| `main.cpp:302` / `2406` / `2433` / `3004` / `4302` | `-Wunused-variable` ×5 | `JUDGE_LINE_Y` / `mpStartCounter` / `partyAutoReady` / `display` / `fakePadHeld` |
| `platform/SystemMedia.cpp:48,51` | `-Wunused-const-variable` ×2 | `IID_IDisplayUpdater` / `IID_IMusicDisplayProperties` |
| `platform/SystemMedia.cpp:354-359` | `-Wcast-function-type-mismatch` ×5 | WinRT 入口点的 `GetProcAddress`（同 `chartdl` 的写法） |
| `downloader/chartdl.cpp:1260,1261,1262,1364,1934` | `-Wunused-*` ×4 + `-Wcast-*` ×11 | 一处被放弃的托盘气泡功能（**含一个声明了却从未上锁的 mutex**）+ 4 处死声明 |

**那条真 bug**（`SongSelect.cpp:4256-4260`，猜歌卡片）：

```cpp
std::string label = std::to_string(i + 1) + ". " + titleFor(gGuess.options[i]);
if (answered && i == gGuess.answer) {
    label = label;            // ← 自己赋值给自己，什么都不做
} else if (answered && i == gGuess.picked) {
    label = label;            // ← 同上
}
ImU32 fill = ui::kWhiteBtn;
if (answered && i == gGuess.answer) {   // ← 下面这段才是真正表达"对错"的地方
    fill = ui::kPrimary;
} else if (answered && i == gGuess.picked) {
    fill = IM_COL32(255, 138, 150, 255);
}
```

上面 5 行的注释还写着「The fill carries the verdict after a pick」——**判定根本就是由
颜色表达的**，那两个 `label = label` 分支是更早一版「在文字上加记号」的设计留下的尸体，
而且和下面的 `if/else if` 条件一模一样（重复的条件对）。**已删 5 行，行为不变。**

### 14. ⚠ 这套 zig 下 `-isystem` 会踩到一个极隐蔽的坑 ★★★

为了让「第三方头的警告不盖住自己的」，我把 `-Ithird_party` 改成了 `-isystem third_party`。
**结果整个构建挂了**，报一堆：

```
core/native/mmw_port/Rendering/Camera.h:9:12: error: no type named 'XMVECTOR' in namespace 'DirectX'
```

根因（`-v` 打搜索顺序才看出来的）：

```
#include <...> search starts here:
 toolchain/…/lib/libcxx/include
 toolchain/…/lib/libc/include/any-windows-any     ← ★ 这里有 directxmath.h
 third_party/DirectXMath/Inc                       ← 我们的真头排在它后面
```

zig 自带的 MinGW 头目录里有一个**小写的 `directxmath.h` 桩头**（只有 `namespace DirectX`，
没有任何 `XMVECTOR` / `XMMATRIX`）。**Windows 文件系统大小写不敏感**，
所以它在语义上就是 `DirectXMath.h`；而 clang 的搜索顺序里 **`-isystem` 排在 zig 的
builtin 目录之后**，桩头于是把真头顶掉了。`-I` 的优先级在 builtin **之前**，
所以只有 `-I` 能钉住我们要的那份。

**结论：这套工具链下，`third_party/DirectXMath/Inc` 必须留在 `-I`，不能改成 `-isystem`。**
（`build.sh` 里已写了同样内容的警告注释。）

### 15. 最终方案：上游/第三方单独编 `.o`，只给自己人开 `-Wall`

知道了 `-isystem` 不能乱用之后，剩下的正解是**把两次编译分开**：

```bash
SOURCES=( main.cpp platform/*.cpp game/*.cpp )          # ← 只有这些开 -Wall

# core/native/**（上游 AGPL，AGENTS.md 说不改结构）与 vendored imgui 单独编成 .o，
# 并且用 -w 关掉它们的警告
for src in "${UPSTREAM_SOURCES[@]}"; do
    "$ZIG" c++ "${CXXFLAGS[@]}" -w -c "$src" -o "build/obj/$(basename "${src%.cpp}").o"
done
"$ZIG" c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" "${UPSTREAM_OBJS[@]}" … -o build/cppsekai.exe
```

这样**第三方和上游的 174 条噪音从流程上消失，而不是从开关上消失**：
下面那次编译里出现的每一条警告，都必然是我们自己写出来的。代价只有 build.sh 多 10 行
（增量编译的缓存照旧生效，全量时间没变）。

**结果：`bash build.sh` → `0 error / 0 warning`。** 回归验证：
`--sus charts/0075_master.sus --auto` 跑出 `perfect=770 miss=0 breaks=0 100.0%`，
选曲界面截图正常。


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
| 1 | **`build.sh` 的 `SOURCES` 改 glob** | 极低 | 永久消除"忘了加文件"事故 | **✅ 已做** |
| 2 | **`CXXFLAGS` 加 `-Wall -Wextra`** | 极低 | 装上第一天就抓到 16 条真问题（含 1 条真 bug） | **✅ 已做** |
| 3 | **删掉全部 16 条警告对应的死代码** | 极低 | 零引用；去一个骗人的"假线程安全" mutex 和一个 `label = label` | **✅ 已做** |
| 4 | **重复的 `json.hpp` 换成转发头**（`vendor/` 那份 → `#include <nlohmann/json.hpp>`） | 低 | 消除跨 TU 的静默 ODR 隐患，仓库少 25,830 行冗余 | **✅ 已做** |
| 5 | **上游/第三方单独编 `.o` + `-w`**（见第 15 节） | 低 | 174 条第三方噪音从流程里消失，`-Wall` 只剩自己的信号 | **✅ 已做** |
| 6 | **抽 `findDataFile()`，替换 7 处手抄的候选路径** | 低 | ~45 行 → ~8 行，把约定固化进代码 | 新 |
| 7 | **设置页 shadow 变量换成一个 struct** | 低 | 加判定参数从改 3 处变 1 处 | 欠着 |
| 8 | **调试开关集中进 `DebugSwitches`** | 低 | 16 处裸 `getenv` 收敛到 1 处 | 新 |
| 9 | **`main()` 里的成块逻辑抽成函数**：参数解析、启动决策、三个画面的 draw | 低（**只搬代码不改逻辑**） | 6,641 → 每块 300~500 行，定位成本断崖下降 | 欠着（**收益最大**） |
| 10 | **`drawSongSelect` 的 in/out 引用收进 `SelectState` struct** | 中 | 13 参数 → 3~4 个 | 欠着 |
| 11 | **给上游代码补许可头**（`mmw_preview.cpp`、`mmw_port/**`） | 极低 | 这是**合规**，见 CREDITS.md 第一节末 | 欠着 |

**1~5 条 2026-09-21 已落地**，验证：`bash build.sh` → **0 error / 0 warning**；
`--sus charts/0075_master.sus --auto` → `perfect=770 miss=0 breaks=0 100.0%`；选曲截图正常。
第 6~8 条是还能当天做完的机械改动。

第 9 条是收益最大的一步，因为它"只搬代码不改逻辑"，出错概率比看起来低得多——
建议一次搬一块，搬完跑 `.workbuddy/tools/mp_verify.sh` + 无头 `--screenshot` 对一遍。

---

## 六、一句话结论

**不是烂代码，是"长在一个函数里"的代码；而且它还在长。**

拆文件这件事做过且做对了（`game/`、`platform/` 的健康度用"最大单函数 205 行"就能证明）。
问题从来没有扩散——**就是 `main()` 从 5,713 长到 6,641、`drawSongSelect()` 从 1,746
长到 1,920，两个函数，三天，+1,100 行**。其余增长（`chartdl.cpp` +852、`Ui.cpp` +460）
都是加函数，是健康的涨法。

**2026-09-21 的收获是：把警报器装上了，而且它第一天就响了。**
`-Wall` 一开就抓出 16 条真问题，其中包括 `SongSelect.cpp:4257` 那句
`label = label;`——一行已经变成空气、但还留在代码里的旧设计。这类东西
**不靠工具是看不见的**（它编译通过、运行正常、只是什么都不做），
而它旁边那 5 行注释还在向读代码的人解释「判定由颜色表达」，
让你完全不会怀疑上面那两行是废话。

剩下真正的大活只有一件：**把 `main()` 和 `drawSongSelect()` 拆开**。
它"只搬代码不改逻辑"，所以风险比体量看起来低得多；不拆也能继续跑，
代价是每次改动都更慢、更容易碰坏别的东西——而能替你发现"碰坏了"的那个哨兵，
现在总算装上了。

---

## ⚠️ 免责声明

- **非官方**：本项目与 SEGA / Colorful Palette 及 Project SEKAI 官方**没有任何关系**，
  未获授权、赞助或认可。
- **代码**：本项目代码是 AGPL-3.0-only（衍生自 `sekai-mmw-preview-web` ← `MikuMikuWorld`），
  改动必须继续开源。字体只用系统字体，仓库里一个字体文件都不带。
- **素材**：`assets/**`、`Drafts/**`、`docs/**` 与四张 JSON 表里的内容**权利全部归原权利人**，
  **仅限本机个人游玩与学习**，**禁止分发、公开传播与任何商业使用**。
- **无担保 / 责任自负**：本文所有数字都是量出来的，但不构成任何保证；使用后果自负。

完整版见 [COPYRIGHT.md](COPYRIGHT.md) 第九节，来源台账见 [CREDITS.md](CREDITS.md)。
