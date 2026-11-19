# 平台 / 输入相关的坑

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 平台 / 输入相关的坑（2026-09-12）

- **HUD 暂停按钮只有一条判定路径**（2026-09-13 整理）：`isPauseButton()`（虚拟坐标命中）之上
  包了个 `hudPausePress(x, y)`，鼠标分支、`SDL_TOUCH_MOUSEID` 的合成鼠标分支、`SDL_FINGERDOWN`
  分支都调它，命中即 `break`（不再当击打）。之前三处各写一份、条件还不一样：
  - 它**故意排在**「paused / autoPlay 就不收输入」和 `WantCaptureMouse` 检查**之前**——
    暂停键不是击打输入，HUD 画在哪它就该在哪能用（自动预览 `--auto` 也照样能暂停，HUD 和
    真局是一模一样的）。设置卡片在左上、暂停区在右上，永远不会重叠，所以可以绕过 ImGui 的
    捕获检查。`SPACE` 同理。
  - `SDL_TOUCH_MOUSEID` 的合成鼠标事件以前被无条件丢掉（指望 finger 分支处理），但有些触摸栈
    只发这一个事件 → 按钮全死。现在这条分支也会喂给 `hudPausePress()`。
  - 被忽略时打一行 `[pause] press ignored (...)`（开场卡期间 HUD 根本没画，也会说一句），
    否则「按钮没反应」在日志里完全隐形。
- 想验证这类"点了没反应"，用 `.workbuddy/tools/winsend.c`（`zig cc` 编译成 `build/winsend.exe`）：
  它按窗口标题找 HWND 并 PostMessage 真鼠标消息，`winsend CppSekai move X Y` + `click X Y`
  （**客户区**坐标）。配合 `--screenshot` 跑一局、中途在另一个 shell 里点一下，就能在没有交互
  会话的情况下测真实输入。2026-09-13 就是用这套确认暂停键本身没问题（1280x720 下中心 ≈
  `1222,41`）。
- **exe 是 Windows 子系统**（`build.sh` 里的 `-Wl,--subsystem,windows`）：双击不出 cmd 窗口。
  `main()` 开头用 `AttachConsole(ATTACH_PARENT_PROCESS)` 接管父控制台，但只在
  `GetStdHandle(STD_OUTPUT_HANDLE)` 无效时才 `freopen("CONOUT$")` —— 否则会把 mintty / 管道的
  输出抢走（`./cppsekai.exe --help | head` 会变成空输出）。两者都没有就写 `cppsekai.log`。
  改这段前先想清楚 stdout 的三种来源（真控制台 / 管道重定向 / 无）。
- **flick 的方向判定必须用屏幕像素除以秒**（`flickDirFrom(upSpeed, sideSpeed, travelUp, travelSide,
  isTouch, heightScale)`）：曾经拿 `worldY`（1 单位≈0.84×窗高）和 `lane` 单位（1 单位≈0.077×窗宽）
  直接比大小，两者尺度差 ~6 倍，等于要求上滑比水平方向竖直 3.6 倍才算 flick —— 触摸屏上基本
  刷不出来。阈值按 `windowH/1080` 缩放，别写死像素。触摸还多两条：上滑锥角放宽到 ~63°，
  并要求**同方向累计位移 > 14px**（`travelUp/travelSide` 每次反向就重新计数，用来滤掉静止手指
  的抖动）。  `SDL_FINGERUP` 用 `peakUp/peakSide`（手势期间的峰值速度）而不是最后一帧速度，
  短促 flick 常常在最后一个 motion 之前就结束了。
- **flick 手势喂进判定引擎有三条硬规矩**（2026-09-13 修，改 `main.cpp::movePointer` 前必读，
  症状都是"hold 尾 flick 在触摸屏上刷不出来 / 必须松手再滑")：
  1. **轨道要有兜底**：`flickJudge()` 先用 `track.lanePos`（按下时算的轨道）调
     `judgement.flick()`，返回 `None` 再用手指的**停留轨道** `track.restLanePos`
     （低于 `kFlickRestSpeed` 时更新，所以跟走位会刷新、上滑过程中不会被透视拉偏）重试。
     根因：**走位 hold 的尾判事件在终点轨道上**（kind 5 标记带终点时间但 center 是起点轨道，
     kind 2 尾判另发在终点轨道）——只按按下轨道滑，`laneCovers` 永远落空。"松手再滑"之所以
     能过，是因为新触点从当前手指位置重算了轨道。
  2. **`dt` 上限钳到 30ms**（`kFlickMaxSampleSec`）：触摸屏手指静止时**一个 motion 事件都不发**，
     flick 第一帧的 `now - lastMoveTimeSec` 可能是几百 ms，不钳的话 800px/s 被算成 ~100px/s。
     空档 >50ms（`kFlickIdleGapSec`）还要清掉速度滤波，免得把陈旧速度混进去。
  3. **不要用一次性闩锁**：旧的 `track.flicked` 让整次触点只 fire 一次，hold 途中任何提前/误触的
     滑动都会把这次机会用掉。现在 fire 后归零 `travelUp/Side`（同一次连续滑动不会每帧都触发）
     + 60ms 冷却（`kFlickRefireSec`），`SDL_FINGERUP` 的 last-chance 也不再跳过。
  另外 `track.isTouch` 必须显式记录（`beginPointer(..., bool isTouch)`），别再用 `fingerId > 0`
  猜 —— 猜错的话阈值从触摸的 500px/s 变成鼠标的 900px/s，触摸屏就废了。
- `--auto` **只影响本次运行**：`persistUserData()` 里会看 `autoplayGiven`，命令行给的 autoplay
  不再写回 `userdata.json`（以前跑一次预览会把 AUTOPLAY 永久打开）。`--screenshot` 模式干脆
  完全不写 `userdata.json`。
- **触摸路径要自己 hit-test 所有按钮**：触摸的合成鼠标事件带 `SDL_TOUCH_MOUSEID`，在
  `SDL_MOUSEBUTTONDOWN` 分支里被过滤掉了，所以**任何只在鼠标分支里判定的按钮，触摸屏上都点不到**。
  踩过两次：HUD 暂停按钮（已修）、开场卡片右下角的「跳过 >>」按钮（2026-09-12 修；
  2026-09-21 已改成右上角的圆形跳过键，见「开场卡片的跳过键」一节）。
  新增可点元素时，要么放进 ImGui（合成鼠标事件能到 ImGui），要么在 `SDL_FINGERDOWN` 里补一份
  同样的 hit-test（`game::introSkipHitTest` / `isPauseButton` 就是这个模式）。
- **Windows 输入法候选框要自己 `SDL_StartTextInput()`**（2026-09-26 修，症状是"全屏下打字没反应"）：
  从第一天起全仓就没有这一句，而 `SDL_HINT_IME_SHOW_UI=1`（`main.cpp` 顶部，SDL_CreateWindow
  **之前**设的）单独不起作用 —— SDL 只在「文本输入处于激活状态」时才处理 `WM_IME_SETCONTEXT`
  并把组合窗交给系统，`SDL_SetTextInputRect()` 在 `SDL_IsTextInputActive()` 为假时更是空操作。
  更坑的是 **ImGui 的 SDL2 后端自己也不调了**：`imgui_impl_sdl2.cpp`（1.92.5）的变更日志里
  2023-04-06 那行写着 "Avoid calling SDL_StartTextInput()/SDL_StopTextInput()"，现在它只在
  `ImGui_ImplSDL2_PlatformSetImeData()` 里喂一个 `SDL_SetTextInputRect()`。两头都不调，候选框就
  永远拿不到位置 —— 全屏下窗口左上角即屏幕角，组合窗落到屏幕外，看起来像"打字没反应"。
  修法：主循环 `ImGui::NewFrame()` 之后按 `io.WantTextInput` 的边沿 Start / Stop（rect 仍由
  ImGui 在 `Render()` 里交给后端），切换时打一行 `[ime] text input on`。
  **候选框本身只能真人用输入法验证**（无头会话里那个框不会出现）。
- **`pulse()` / `pressed()` 定义在 `if (pad != nullptr || !fakePad.empty())` 里面**（2026-10-06）：
  想借"合成一次按键"这条路的代码（媒体键、任何新的快捷键）**别放进那个块** ——
  没插手柄时整块不执行，表现是"键明明到了、标志也置上了、就是没人消费"（第一次做媒体键
  就栽在这：`CPSEKAI_KEY_LOG=1` 已经打出 `AudioNext` 了，`[media]` 一行都没有）。
  `pulse` 现在提到那个 if **外面**了，抬起走 `padReleaseQueue` 延迟一帧 —— 同一帧里
  down+up 会让 ImGui 的 `DownDuration` 归零，`IsKeyPressed` 直接看不到。
- **按键级诊断：`CPSEKAI_KEY_LOG=1`**（2026-10-06 加）：每笔按键打一行
  `[key] sym=0x… (名字) scancode=… mod=… repeat=…`，比 `CPSEKAI_MSG_LOG`（窗口消息级）低一层。
  查"某个键没反应"从它开始：能立刻分清是"事件没进来"还是"进来了但 keysym 不对"。
- **`winsend.exe key <vk>` 的 lParam 必须带扫描码**（2026-10-06 修）：SDL2 从 lParam 的
  16..23 位取扫描码（`scancodes_windows` 表按扫描码索引），wParam 里的虚拟键码只是参考。
  以前传 0 → SDL 翻成 `SDL_SCANCODE_UNKNOWN`、事件被丢掉：窗口过程确实收到 `WM_KEYDOWN`
  （`CPSEKAI_MSG_LOG` 看得见），游戏里一点反应没有（拿 F5 试出来的）。媒体键（0xB0..0xB7）
  还要置**扩展键位**（bit24），不然扫描码会撞上普通键（0x22 是 G）。

