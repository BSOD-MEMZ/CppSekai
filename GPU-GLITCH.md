# GPU-GLITCH.md —— 两条「画面异常」的排查记录

> 2026-10-31。涉及两台机器：NUC7（Intel HD 620，老驱动）与开发机
> （Intel Iris Xe，驱动 32.0.101.7088）。**两台都是 Intel 集显**，所以
> 「NVIDIA 和 Intel 的区别」这条一开始就能排掉。

两条现象看起来是一回事，其实是两套完全不同的机制：

| 现象 | 机制 | 状态 |
|---|---|---|
| 教程/选曲画面整帧泛白（过曝） | 该收回的扩展帧没收回，整局走 DWM 逐像素合成 | **定位到开关逻辑里的真 bug，已修**；现场没留下截图 |
| 演奏中白色条纹/色块 → 卡住 → 屏幕黑一下 | 显卡驱动超时重置（TDR） | 机制已确认，**触发源待取证** |

> ⚠ 先纠正一件事：`veil_1.png` ~ `veil_4.png` 这四张**都是正常的**（差别只是模态
> 遮罩淡入到第几帧）。出问题的那一刻从来没被截下来。所以下面关于「过曝」的结论不是
> 从图上量出来的，是从**代码里的开关时序**推出来的 —— 好在这次推出来的是一个能直接
> 读出来的逻辑错误，不依赖复现。

---

## 一、教程画面过曝

### 结论

不是渲染逻辑画错了，是**窗口在不该透明的时候一直是透明的**。

- DWM 的逐像素合成只在 `DwmExtendFrameIntoClientArea` 把边距设成 -1 时成立。
  开着它的时候，GL 缓冲里任何 alpha < 1 的像素都会和**桌面**混合；桌面是浅色的，
  混合结果就是整帧发白、对比度被拉低。
- 代码里开它的地方只有一处：图片 splash（PNG 要透出桌面）和「玻璃背景」。
  但**启动时开了之后，只要 `splashStyle == 0`（图片 splash，这是默认值），就再也
  没关过** —— 于是整局游戏，包括打歌和选曲，全都跑在逐像素合成下。
- 更糟的是设置卡那条：切背景时用的是
  `requestGlassWindowMode(splashStyle == 0 || bgStyle == 2)`。`splashStyle == 0`
  在运行期恒为真，所以**每次切背景都会重新把透明打开**，连「从玻璃切回不玻璃」也
  会把它打开 —— 这是最容易撞上的路径：动一下背景设置，画面当场开始泛白。

- **这个问题截图看不出来**：游戏自己的 `--screenshot` 读的是 GL buffer，
  alpha 通道不进 PNG，只有 DWM 合成之后的屏幕才看得见。所以拿截图对比永远对不上
  —— 这也正是它能在一台机器上「消失」而没人说得清原因的地方。

### 为什么这台机器不复现（实测证据）

今天在 `[gl]` 启动日志里加了一行，直接量出来：

```
[gl] 3.3 | version='3.3.0 - Build 32.0.101.7088' renderer='Intel(R) Iris(R) Xe Graphics'
[gl] framebuffer alpha=0 (asked 8) depth=0 maxTexture=16384
```

| 机器 | GL_RENDERER | 请求 alpha | **实际 alpha** |
|---|---|---|---|
| 开发机 | Intel Iris Xe | 8 | **0** |
| NUC7 | Intel HD 620 | 8 | 待确认（预期 8） |

`SDL_GL_ALPHA_SIZE` 只是**请求**，WGL 最终给多少由驱动的像素格式决定。开发机
这条路径上一次都没成立：帧缓冲根本没有 alpha 通道，`glClearColor(...,0)` 写
进去的 0 被丢掉，DWM 拿到的是不透明画面 —— **所以过曝在物理上不可能出现，
和改了什么代码无关。**

这也解释了另一件事：**同一份代码在开发机上，「玻璃背景」大概也是无效的**
（被静默降级成不透明黑），只是平时没人去开那个设置。

### 2026-10-31 夜：现象补充，因果链重排

用户补了一句关键信息，比任何截图都值钱：

> 只在教程时屡次出现，**每次对话框更新就过曝**，等待几秒自动恢复，其它界面均未见过曝。

**这一句直接排掉了「整局都在透明合成」**（那会从头白到尾，不会自己好）。只剩一个组合
能同时满足「只有教程 / 一更新就有 / 几秒后自己好」：

```
玻璃背景（bgStyle == 2）
  → 选曲界面不画 wash：setSelectTransparentBackground(true)
  → GL 层 renderFrame 也清 alpha = 0（mTransparentBackground）
  → 整个选曲界面在 DWM 眼里是「0 透明」
  → 教程盖上一层 dim(0.62) 的全屏半透明黑
  → 结果 alpha ≈ 0.62，桌面从底下透上来 38%
  → 浅色桌面 + 62% 黑 = 灰白发雾，也就是「过曝」
```

为什么只有教程：**整套 UI 里只有教程层用「半透明黑压暗」**。打歌界面有 GL 场景（不透明），
选曲列表在玻璃模式下本来就是透的（那是用户自己选的、想看桌面）。而 dim 一撤（切到
`name` 卡片、或跳到下一步），画面就「自动恢复」—— 也就是用户说的几秒。

**待确认（决定这条成不成立）**：NUC7 是 Win7 吗？设置 → 外观 → 背景 是「Aero 玻璃」吗？
`aeroGlassAvailable()` 只在 6.0/6.1 返回 true，所以这件事**只可能发生在 Win7/Vista 上**。
如果 NUC 上是「壁纸」背景，这条就作废，得回到字体/描绘路径去查（那需要一张现场截图）。

如果成立，修法很直接：**教程期间不要用半透明黑压暗，先铺一层不透明的底**。教程本来就是
全屏叙事，透出桌面没有任何道理。

### 昨天的补丁 vs 今天的修复

`e690519` 在 `presentFrame()` 之后、`SDL_GL_SwapWindow()` 之前补了一遍
`glColorMask(0,0,0,1) + glClear(alpha=1)`。它把**症状**摁住了（整帧 alpha 强制成 1），
但：

- 在 **alpha = 0 的机器上是空操作**（没有通道可写）→ 开发机看不出任何变化；
- 在 **alpha = 8 的机器上有效**，但只在「补丁跑到了」的前提下 —— 而窗口本身仍然
  一直在透明合成，只要哪天有像素在补丁之后又被画上、或者补丁被条件跳过（`glassBackground`），
  症状就回来。

今天改的是**开关本身**（`main.cpp`，两处）：

1. splash 结束时，如果背景不是玻璃，`applyGlassWindowMode(false)` 把扩展帧收回去。
   图片 splash 是唯一需要它开着的理由，加载完就该还窗口一个不透明表面。
2. 设置卡切背景时把 `splashStyle == 0 ||` 去掉，只按 `bgStyle == 2` 决定。
   `splashStyle` 是启动期的事，不该出现在运行期判断里。

日志能直接看出结果：

```
[window] DwmExtendFrameIntoClientArea(-1 margins) -> 0x0
[window] glass mode 0 (extend frame only), frameless 0, per-pixel alpha on     ← splash
[window] DwmExtendFrameIntoClientArea(no margins) -> 0x0
[window] glass mode 0 (extend frame only), frameless 0, per-pixel alpha off    ← 加载完
```

非玻璃背景从此**整局都不透明**，alpha 这条路径不存在，也就不需要每帧去补。
（`main.cpp` 里那个补 alpha 的 `glClear` 留着没删：开启玻璃时它被 `glassBackground`
跳过，非玻璃时已经是 no-op，删不删都一样。）

### 顺带回答：Win10 会不会碰到毛玻璃代码

分两层看，结论是**「绘制不串，窗口串」**。

- **Aero 玻璃的绘制**不会串：`aeroGlassAvailable()` 用 `RtlGetVersion` 严格判
  `major == 6 && minor <= 1`（Vista / 7）。Win10 返回 false，于是
  - 设置卡里的背景列表只剩 2 项（玻璃那一项根本不出现，`bgItems`/`bgCount` 跟着走）；
  - 存档若带着 `bgStyle == 2`（从 Win7 机器同步过来），启动时被改写成 1 并打
    `[bg] Aero glass is not available on this system, using the wallpaper`；
  - `glassMode` 在 `bgStyle != 2` 时被钉成 0。
- **窗口那一套确实会串**：`DwmExtendFrameIntoClientArea` 是 Win7+ 通用 API，Win10 上照样
  生效、照样把一个普通窗口变成逐像素合成。原来那段「splash 开场 → 之后不收回」的代码
  在 Win10 上一样跑 —— 这就是今天修掉的部分。所以「Win10 电脑碰到了毛玻璃相关代码」
  这个感觉是对的，只是它碰到的不是玻璃的**画法**，是玻璃的**窗口前提**。

### 还剩一件可做（没做）

**把 alpha 位数当能力位**：启动时读一次 `GL_ALPHA_BITS`，为 0 就说明这台机器上
玻璃背景根本无效，设置里可以直接置灰，别让人白点。

---

## 二、演奏中白色条纹/色块 → 卡住 → 屏幕黑一下

### 这是什么

「整块屏幕黑一下再恢复」在 Windows 上基本只有一个解释：**TDR
（Timeout Detection and Recovery）**。GPU 有一条命令超过阈值（默认 2 秒）没
完成，驱动就判定设备挂死、复位、恢复。复位瞬间会闪黑，随后画面回来 —— 这和
「只有游戏窗口黑」是两码事。

要坐实，在**出问题的那台机器**上跑（Win7/Win10 都一样）：

```
wevtutil qe System /q:"*[System[(EventID=4101 or EventID=4102)]]" /c:10 /rd:true /f:text
```

能看到「显示驱动程序 igfx... 已停止响应，并且已成功恢复」就是它。没有 4101
的话，再查 1000/1001（`dwm.exe` 或 `explorer.exe` 崩溃重启也会黑一下，那是
另一条线）。

### 「白色条纹」为什么是 TDR 的伴生症状

最常见的成因是**一个横穿屏幕的巨大三角形**：某个顶点的 x/y 变成了 NaN、无穷，
或者一个荒谬的大数（透视除法除以了接近 0 的 w、谱面里有越界的轨道……），
光栅化出来就是一个盖住半屏的三角形。它的 UV 会被同样拉爆，于是
`CLAMP_TO_EDGE` 把纹理**边缘的一个像素**拉满整个屏幕 —— 那就是「白色条纹/
色块」。而每帧填一个满屏三角形，在垃圾集显上足以把帧预算打穿，驱动等不到
完成就复位。

### 我查了什么、排除了什么

给 `Renderer::renderFrame()` 加了一层**提交前体检**（`platform/Renderer.cpp`，
见下一节），判定条件是「顶点在*真实* clip 空间里超过 1e5」或「非有限值」。
拿它跑实测：

| 场景 | 丢弃的 quad |
|---|---|
| `--auto` 自动演奏 30 秒（222 命中） | **0** |
| 选曲界面 | **0** |
| 结算界面 | **0** |

**结论：正常演奏的数据是干净的，没有顶点爆炸。** 也就是说白条不是「每局都
埋着的雷」，而是在特定机器/特定时刻才出现的东西。

途中踩过一次自己的坑，记下来免得以后重犯：第一版阈值取了 256，结果每帧误杀
2~8 个特效 quad。原因是**特效 quad 的 x/y 是除以 w 之后的 clip 坐标，1/w 单独
存着**（shader 里再乘回去），摄像机后方的顶点会合法地打包成 `x=430, 1/w=-33`。
所以判据必须先把 `x / (1/(1/w))` 还原、再看量级，并且**不能把负的 1/w 当错误**。

### 现在能拿到的证据（今天加的）

启动日志（无条件，一行）：

```
[gl] framebuffer alpha=0 (asked 8) depth=0 maxTexture=16384
```

演奏中（只在有事时出声）：

```
[gl] dropped N garbage quad(s) this frame (...)      # 顶点爆炸，附原始 25 个 float
[gl] slow swap 1520 ms (present waited on the GPU)   # 卡住发生在 GPU 侧
[gl] context lost - the driver reset the device (TDR)# 驱动已经复位过了
```

- `CPSEKAI_GL_TRACE=1` 时额外打印出问题那一个 quad 的原始数据，用来区分是
  音符、长条还是特效。
- `slow swap` 是**最便宜、最能定性**的探针：vsync 开着时 swap 不该超过一个
  刷新间隔，几百毫秒就说明 CPU 在等 GPU 把这一帧画完 —— 也就是提交的工作量
  远超这台机器。TDR 就是从这种状态长出来的，而且它比 Windows 的复位**早**。

### 让那位朋友做的事

1. 换上今天之后的构建（`build/cppsekai.exe` 所在的那份）。
2. 正常打一首，**复现一次**（白条 + 卡住 + 黑屏）。
3. 复现后关掉游戏，把 `cppsekai.log` 发回来。
4. 顺手跑一下上面那条 `wevtutil` 命令，一起发。

日志里只要有 `[gl] slow swap` / `[gl] dropped` / `[gl] context lost` 任意一条，
就能直接定性；一条都没有，那问题就不在这个渲染循环里（往驱动、电源、
内存不足、DWM 崩溃那几条线查）。

### 顺带发现的一个真实卡顿源（不是黑屏）

```
[stage] built plate 2048x2048 in 349 ms
[frame] 37 frame(s)/s, longest frame 403 ms (state=1)
```

开曲瞬间 `buildStageBackground()` 在 CPU 上合成一张 **2048×2048 RGBA（16 MB）**
的曲绘舞台，开发机要 349 ms，且这一帧整体 403 ms。垃圾机器上这个数会成倍放大
（少核 + 低频 + 4 GB 内存），表现为「按开始之后卡住几秒」。它不会导致黑屏，
但如果朋友说的「卡住」出现在开曲那一刻，就是这里。真要治的话方向是缓存到
磁盘 / 降到 1024，属于另一个话题。

---

## 三、今天改了什么

前两行是**诊断**（不改渲染结果），第三行是**真的修了一个 bug**：

| 文件 | 改动 |
|---|---|
| `platform/Renderer.cpp` | `quadIsGarbage()`：提交前体检，超范围/非有限的 quad 丢弃并计数；`CPSEKAI_GL_TRACE=1` 打印原始数据 |
| `main.cpp` | 启动日志加 `framebuffer alpha / asked / depth / maxTexture`；swap 计时（>150 ms 报警）+ `GL_CONTEXT_LOST` 检测 |
| `main.cpp` | **扩展帧只在需要时开着**：splash 结束（非玻璃背景）收回去；设置卡切背景只按 `bgStyle == 2` 判断 |

验收：`--auto` 演奏 / 选曲 / 结算各跑一遍，**0 次丢弃、0 条 slow swap**，
`[stats]` 与改动前一致。日志里 `per-pixel alpha` 在 splash 后从 `on` 变 `off`
（见上面那段），演奏截图正常。

## 四、待验证

- [ ] NUC7 上 `[gl] framebuffer alpha=` 是多少？—— 决定「过曝」这条到底是 alpha
      混合还是别的东西（比如驱动的色调映射）。**不用专门去复现**：下次碰那台机器
      看一眼日志就够；就算一直是 0，今天把扩展帧收回也把这类风险封死了。
- [ ] 玻璃背景（`bgStyle == 2`）在新逻辑下仍然透明 —— 需要一台 alpha=8 的机器看，
      开发机看不出来。
- [ ] 朋友的机器：GPU/驱动版本、`cppsekai.log`、事件日志 4101。
- [ ] 如果 `slow swap` 抓到了，把当时的 `[gl]` 上下文（quad 数、场景）记下来。
