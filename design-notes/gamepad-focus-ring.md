# 手柄焦点环 / 震动

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 手柄焦点环 / 震动（2026-09-20）

**要解决的问题**：手柄原来只能"按键 → 键盘脉冲"，够得着的只有 ImGui 原生项和自绘列表导航。
设置卡片里的滑块 / 复选框 / stepper / 下拉框全是 `InvisibleButton` 自绘，**键盘焦点根本到不了**
（LB/RB 切了页签也动不了里面任何东西）。现在补第二条路，但**没有另开一套输入**：

- **`ui::PadScope` / `padNav` / `padWidget`**（`game/Ui.cpp`）：控件绘制时把自己的矩形登记进
  本帧列表；`main.cpp` 把手柄边沿翻成 `ui::padNav(Up/Down/Left/Right/Accept)`。
  - **只有 `PadScope` 里的控件参与**。选曲界面的排序/分组 combo 与设置卡片同帧绘制，
    不挡掉就会混进焦点表，实测焦点会"row 6 → row 15 → row 8"乱跳。`main.cpp` 里
    `drawSettingsCard` 开头一句 `ui::PadScope padScope(showDebug);` 就是全部接线。
  - 导航读**上一帧**的矩形表（布局那时已定），动作则由**本帧**登记的指针执行，
    所以值指针永远最新；焦点控件这一帧没画（换页/换卡）→ `padFrame()` 自动清焦点，
    上一层没人消费的 pending 也一起丢掉。
- **设置卡片独占方向键**：`showDebug` 时 D-pad / 左摇杆不发 UP/DOWN/LEFT/RIGHT 脉冲，
  改喂 `padNav`；`A` 改喂 `PadAccept`。**其余屏幕一个字没改**。
- **能操作的控件**：`slider`（左右 = 一次按钮步长，和深色 -/+ 同样 clamp）、`checkBox`（A 翻转）、
  `stepper`（pick-one 走相邻预设，A 也前进一格）、`capsuleButton`（A 按下；返回值就是
  `clicked || pad.pressed`）、`ui::combo`（左右循环选项）。
  **原生 `ImGui::Combo` 要手动接**：它没有绘制回调可挂，调用点后面加一句
  `xxx |= ui::padComboNudge(&idx, count);`（设置里 7 处：分辨率 / 渲染模式 / 窗口模式 / 背景样式 /
  玻璃实现 / 多开 / 用户档案）。**以后新增原生 combo 别忘这句。**
- **两张自绘卡片另外走 `forcedChoice`**（和暂停对话框同一套）：多开确认框
  （`multiAskPadChoice`）与首启 ELUA（`eulaPadChoice`）—— A/START = 主按钮，B/X = 取消（ELUA
  只有一个按钮，B/X 也是关掉它）。它们**优先于其它一切按键**（`padModalCard`），否则漏过去的
  START 会在卡片背后再开一张设置卡。`eulaAlive` / `eulaDismissedThisRun` 原来埋在帧体的
  function-local static 里，手柄块在帧首读不到 —— 已提到 main 作用域（同 `settingsTab`）。
- **焦点环**：`(46,186,164)` 2px 圆角描边 + 薄荷淡填充，`animToggle` 双向缓动，画在
  `GetWindowDrawList()`。checkBox 的矩形要外扩 6~7px，否则环被粉色填充盖住看不见。
- **无头验证**：`--settings --settings-tab N --fake-pad DOWN,LEFT --screenshot`，看 `cppsekai.log`
  的 `[pad] focus row i / N`（焦点移动）与 `[pad] focus action a -> widget kind k`
  （kind 0 slider / 1 checkBox / 2 stepper / 3 button / 4 combo；action 0 A / 1 Up / 2 Down /
  3 Left / 4 Right）。**环有没有真画出来**用 `node .workbuddy/tools/png_color_probe.js <png>
  30-95 165-210 140-190 55 8 20,200,340,600` 数像素（最后一个参数是裁剪框）：
  同一区域有焦点实测 596 px、无焦点 0 px。这个纯 Node 探针是 Pillow 装不上时的兜底
  （venv 里的 pip 连不上源，`pip install pillow` 直接 "No matching distribution"）。

**震动**（`UserSettings::padRumble`，设置→演奏「手柄震动」，0 = 关；存 userdata.json）：

- 唯一入口是 `main.cpp` 的 `rumble(strength, ms, why)` lambda：低/高频马达分别给
  `k*24000` / `k*52000` —— **只推轻马达像"嗡"，两个一起才像"咚"** —— 并先问
  `SDL_GameControllerHasRumble`。**每次都打一行 `[pad] rumble '<why>' ... -> buzz / no pad`**：
  无头跑没有手柄，这行是唯一能验证"什么时候该震"的东西。
- **乐曲开始**：`songClock()` 从负跨过 0 的那一刻（`Audio::start` 起的是**静音** lead-in 时钟，
  真正出声就是这一刻），230ms 满力。多人各窗口共用同一套时钟，所以一屋子同时震。
- **结算数字滚动**：`drawResult` 的 `countUp` 是 `easeOutCubic(span(t, 0.85, 0.95))`，这里把同一条
  曲线**量化成 24 个 tick**（每 tick 55ms，强度随进度递减），tick 变了才震 —— 数字滚得快就震得密、
  慢下来就稀疏。实测日志 23 行、强度 0.74 → 0.30。
- **确定白屏**（2026-09-21）：三处 `ui::se(ui::SeStart)` 后面各跟一句 `rumble(0.90f, 200, "confirm")`
  —— 白屏就是"要开始读谱了"的那一刻，最长的一脚给在这里。**新增"确定"入口时三处都要记得加**
  （选曲确定 / 重试 / `--confirm-flash` 调试路径）。
- **命中口味**（2026-09-21，设置→判定「震动」三个勾选框，存 userdata.json 的
  `rumbleFlick` / `rumbleCritical` / `rumbleMiss`）：Flick、绝赞（黄键）、MISS 时各来一小脚
  （55/65/95ms，0.50/0.55/0.75）。实现要点：**不要看 `lastHitKind` / `lastJudgeCritical`**
  —— 一帧里判两个音（和弦、或者自动演奏一次跑好几个）时它们只剩最后一个音，
  绝赞会被吃掉（实测 25s 的 `--auto` 一脚都没有）。正确做法是 `JudgementStats` 里的
  `hitCount` / `criticalHitCount` / `flickHitCount` 三个**只增不减的计数**（在
  `registerJudge` 里加，那里只有真命中会经过），`main.cpp` 在 `judgement.update()` 之后
  比一次增量；同一个音既 flick 又绝赞时让绝赞优先，一帧最多一脚。
- **窗口不能拖边框缩放（2026-09-16 修）**：开屏样式是图片时窗口被强制建为 `SDL_WINDOW_BORDERLESS`，
  加载完再 `SDL_SetWindowBordered(window, SDL_TRUE)`。**这个调用只把标题栏加回来，不会恢复
  `WS_THICKFRAME`**，于是窗口看起来正常但拖边缘毫无反应（`WS_MAXIMIZEBOX` 也一起没了）。
  修法：恢复边框之后补一次 `SDL_SetWindowResizable(window, SDL_TRUE)`（SDL 内部就是加
  `WS_THICKFRAME|WS_MAXIMIZEBOX`）。退出全屏（F 键、设置里换窗口模式）两处也补了。
  判断依据别靠肉眼：`.workbuddy/tools/winstyle.c` → `build/winstyle.exe <窗口标题>` 直接打样式位
  （PowerShell 的 `Add-Type` 在本沙箱被禁，所以用 C 小工具）。
- **触摸 flick 的修复（2026-09-16 晚，已改）**，四处：
  1. `movePointer` / `beginPointer` 现在收 `event.*.timestamp`，不再用处理事件时的 `SDL_GetTicks()`；
  2. 同一毫秒的采样**累加**（`pendingDx/pendingDy`）而不是 `dt==0` 直接丢掉，抬手时也把在攒的位移并进去；
  3. 新增"位移兜底"：触摸划出 56px（1080p 当量，鼠标 150px）就算 flick，不管秒速度；
     触摸阈值 500→380、600→450 px/s；
  4. 方向改成**按位移主轴判**（侧向在 0.85 倍内算侧向），并且**判失败不再吃掉手势**
     （只有打中才清零位移 + 上 60ms 的锁）。数值模型：温和 343px/s 上传（原来永不触发）、
     1440p 窗口、一半采样同毫秒丢样本这三种情况现在都能判出 UP，45° 斜划判 RIGHT（原来判 UP）。

