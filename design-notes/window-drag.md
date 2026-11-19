# 拖动窗口 / 改窗口大小

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 拖动窗口 / 改窗口大小 → 画面卡住、歌却继续跑 2026-09-19

**前提是经典说法**：Windows 在 `DefWindowProc` 里为标题栏拖动和边框缩放跑一个**自己的模态消息
循环**，从 `WM_NCLBUTTONDOWN` 到松手之间我们的消息泵（`SDL_PollEvent` → `DispatchMessage`）
整段被挂起 —— 主循环一帧不跑、画面冻住，而音频在 miniaudio 自己的线程里照放、谱面钟骑在音频
上，松手瞬间钟表往前跳、中间的音符全被判 MISS。
**但这个前提 2026-09-19 在 Win11 上实测没有成立**，见下面「实测记录」——先读那节再改代码。

**已落地（方案 a，帧体抽成 lambda + WM_TIMER 喂帧）**：`main.cpp` 里
`SDL_SetWindowsMessageHook` 加上一个 `SetTimer`（周期见下面「拖动 / 缩放到底多少帧」一节，
2026-11-01 从 16 ms 改成 8 ms）。那段模态循环里**系统照样会给窗口
过程投递 WM_TIMER**，而 SDL 的钩子在它处理任何消息之前被调用，于是拖动期间一个 tick 出一帧，
直接从那里面跑。实测拖动期间能稳定出帧（模拟测试 6 秒出 148 帧），**画面继续动、谱面钟继续走，
不再需要暂停**（上一版那个"静默暂停"已删掉 —— 它只是因为当时没法画才存在）。

实现键点（改这段之前先读完，容易踩）：

1. **帧体包成 `auto runFrame = [&]() { for (bool once = true; once; once = false) { …正文原样… } };`**，
   主循环变成 `while (running) { runFrame(); }`。外面那层**只跑一次的 for 是必须的**：
   正文里的 `continue` / `break` 在编译器眼里是"下一轮这个循环 / 离开这个循环"，
   套一层单次循环后 `continue` = 离开包装 = 一帧结束（正好等于原来对主循环 `continue` 的效果），
   `break` 同理；而三处真正要结束主循环的地方都是先 `running = false`，条件紧接着在调用后检查。
   **正文一个字都没改** —— 别"顺手重构"里面那 2500 行。
2. **重入保护是 `depth`**：喂进去的这一帧自己会泵消息（它就是正常的帧体，`SDL_PollEvent` 全在里面），
   于是它可能再派发下一个 WM_TIMER —— 那个必须丢掉，不能变成第二层嵌套帧。
3. `WM_ENTERSIZEMOVE` 开表 + 计数清零，`WM_EXITSIZEMOVE` 关表，进出都打
   `[window] WM_ENTERSIZEMOVE: … serving frames from a 16 ms timer` /
   `[window] WM_EXITSIZEMOVE: N frame(s) served during the drag` —— **N 是"拖动期间画面有没有在动"
   的直接证据**。
4. 定时器只挂在拖动/缩放期间，平时完全不存在，所以普通帧循环的行为与以前逐字节一致。

验证（`winmsg.exe` 的 `raw` verb，PostMessage 的消息**同样会走 SDL 的窗口过程**，所以钩子能被无头驱动）：

```bash
# 单机进演奏（--no-party 必须加：多人默认是开的；另外 --sus 的路径相对 CWD，
# build/charts/ 里没有 test.sus，要从仓库根跑 charts/test.sus）
build/cppsekai.exe --sus charts/test.sus --auto --no-party --lead-in 1     --screenshot build/shots/x.png --screenshot-time 6 &
build/winmsg.exe SDL_app raw 0231 --pid <pid>   # 伪造 WM_ENTERSIZEMOVE
sleep 6
build/winmsg.exe SDL_app raw 0232 --pid <pid>   # 伪造 WM_EXITSIZEMOVE
```
判别指标用「到谱面时间 6s 的墙钟耗时」：**12.97s（不拖）→ 12.69s（"拖"6 秒）**，即拖动期间
谱面钟照走（上一版静默暂停时这个差值是 +5.13s，正好等于拖动时长）。
**这个测试比真实拖动还狠**：外层循环同时也在跑，等于 148 个重入帧与外层帧并发，没崩没花。
真拖动时外层被系统挂住、只有这一条渲染路径，帧率只会更高（模拟里 148 帧/6s 是被双重渲染拖慢的）。

**边框缩放**走的同一条模态循环、同一个定时器，缩放期间窗口尺寸在变，
`SDL_PollEvent` 拿到的 `WM_SIZE` 会带着新尺寸重建 FBO。2026-11-01 真的拖过一遍，见下节。

### 拖动 / 缩放到底多少帧（2026-11-01 实测 + 调参）

用户问「为什么拖窗口、拉边框会瞬间掉到 30 帧以下」。用 `.workbuddy/tools/drag_probe.py`
（真实 `mouse_event` 按住标题栏 / 右下角边框走一圈）量出来：

| | 拖动 | 缩放 |
|---|---|---|
| 旧（16 ms 定时器 + 30 ms 下限） | 1.90 s / 44 帧 = **23 fps** | — |
| 新（8 ms 定时器 + 16 ms 下限） | 1.88 s / 96 帧 = **51 fps** | 1.47 s / 81 帧 = **55 fps** |

掉到 30 以下是两层叠出来的：`kDragMinGapMs = 30` 自己就封顶 33 fps；而 `SetTimer` 的实际
粒度是 **15.6 ms**，30 ms 的下限被量化成 32/48 ms 两档 —— 平均 43 ms，也就是 23 fps。
**定时器周期必须比下限细一档，下限才真的生效**，所以两个要一起改（8 / 16）。代价是每帧之后
留给模态循环跟光标的时间变短（一帧渲染约 13 ms @960x540）：想更跟手就把 `kDragMinGapMs`
往回加（20 → 约 41 fps），想画面更顺就往下压。

**探针用法**：`python .workbuddy/tools/drag_probe.py [move|resize]`（程序要先起好）。
必须用 `mouse_event` **真的按下左键** —— 只发 `WM_NCLBUTTONDOWN` 不够：模态循环看的是真实
按键状态，没有按下的左键它立刻退出，窗口动都不动（第一次测就栽在这，"拖动没发生"却看不出来）。

**顺带修的 IO**：`applyRenderMode()` 原来每次调用都打一行 `[window] window=WxH …` 并 fflush，
而缩放期间这条路每帧被 `SIZE_CHANGED` 拽进来一次 —— 一次 1.5 s 的缩放写 24 行。现在有
250 ms 时间闸（光比数值没用：拖动时尺寸每帧都在变，等于没降频）。

### 实测记录与真正的修法 2026-09-19（Windows 10 22H2 + Windows 11）

用户报"拖动窗口画面不会刷新"。为了不再靠猜，加了两个工具和一个常驻探针，结论如下。

**1. 拖动确实把消息泵挂住了 —— 我中途读错过一次数据。** 常驻探针（每秒一行，只在有
变化/长帧时打印）拖动期间给出：

```
[frame] geometry changed: 56 frame(s)/s, longest frame 2264 ms, window moved 392 px
```

`longest frame ≈ 拖动时长`（2.1s 拖动 → 2264ms）就是"整整一帧被卡在拖动里"的签名。
**教训**：`[sync]` 那类"每秒一行"的日志**看 qpc 跳变，不要看行数** —— 我当时因为 t 值
连续、行数没少，误判成"泵没被挂住、只是帧率掉"，白绕了一圈。

**2. `SDL_SetWindowsMessageHook` 在这件事上没用。** 查 SDL2 源码
（`src/video/windows/SDL_windowsevents.c`）：那个钩子是在 **`WIN_PumpEvents`（SDL 自己的
事件泵）** 里调的，**不在窗口过程里**。模态循环期间我们的泵不跑，钩子就永远不会被调用 ——
实测全程 24 条消息，没有 `WM_ENTERSIZEMOVE`、没有 `WM_MOVING`。
**这条推翻了我之前"钩子能在模态循环里喂帧"的设计**（那版代码只在伪造消息的合成测试里跑通，
真实拖动一次都没触发）。
**但伪造消息的测试仍然有用**：它能证明"子类化后的窗口过程确实进了消息链"（30 秒验证）。

**3. 现在走窗口子类化**（`main.cpp`，`runFrame` 定义之后那一块）：

- `SetWindowLongPtrW(hwnd, GWLP_WNDPROC, ...)` 换成我们的过程，**原过程（SDL 的）存在
  `WNDPROC chain` 里，每条消息末尾 `CallWindowProcW` 转回去**，转发行为不变。
  **状态放在窗口属性里**（`SetPropW(hwnd, L"CppSekaiSubclassState", ...)`，SDL 自己也是用
  窗口属性存窗口数据的），所以没有新增模块级可变全局，也不会出现"钩子比窗口活得久"。
- 触发：`WM_ENTERSIZEMOVE` 开一个 16ms 定时器；`WM_MOVING`/`WM_MOVE`、`WM_SIZING`/`WM_SIZE`、
  我们的 `WM_TIMER` 各自喂一帧；`WM_EXITSIZEMOVE` 关表并把帧数打进日志。
- 两个保护：`depth`（喂进去的那一帧自己会泵消息，下一个 `WM_TIMER` 会落在它中间 —— 只能有一层
  嵌套），以及 8ms 的时间下限（否则每个鼠标消息都要一帧，窗口会以 16ms 为步长跟着光标走）。
- `CPSEKAI_MSG_LOG=1` 打印子类化过程看到的每条消息。

**实测（Win11，SendInput 拖 2.1 秒、280px）**：`WM_EXITSIZEMOVE: 127 frame(s) served during
the drag (moving=126 sizing=0 timer=1)` —— 拖动期间约 60fps（受 vsync 限），同时
`[frame] geometry changed: 59 frame(s)/s, longest frame 29 ms, window moved 50 px`，
最长帧从 **2264ms 掉到 19~29ms** ✓。Win10 22H2 那边等用户复测（模态循环更经典，消息一定会
送到窗口过程）。

**3b. 拖动的卡顿（2026-09-19 修）**：喂进去的每一帧都会把模态循环堵住整帧时间，所以
最初"每个 WM_MOVING 一帧 + vsync 开着"等于 97% 的时间在阻塞循环 —— 用户的原话是"拖动和 resize
会很卡"。三个改动：① 拖动期间**关掉 vsync**（`dragFramePacing`，由子类在 ENTERSIZEMOVE/
EXITSIZEMOVE 置位，帧体的 vsync 决策读它），每帧只花渲染本身的时间；② 喂帧下限从 8ms 提到
**30ms**（约 33fps），给循环留出时间跟手；③ `WM_TIMER` 里查 `GetAsyncKeyState(VK_LBUTTON)`，
**按键一松就 KillTimer + 恢复 vsync** —— 模态循环有时不给我们 `WM_EXITSIZEMOVE`（松手那条消息
会落在我们服务的那一帧里、被 SDL 的泵吃掉），没有这个收尾会留下"定时器永远在跑 + vsync 一直关"
的坑。改完实测：拖 2.1s / 280px → **63 帧（约 30fps）、`WM_EXITSIZEMOVE` 正常到达**，
探针里最长帧 50ms 上下（≈喂帧间隔，不再是整段拖动）。

**4. 常驻探针**（`main.cpp` 帧体开头，默认安静，不需要环境变量）：那一秒里窗口几何动过就打印
`[frame] geometry changed: N frame(s)/s, longest frame M ms, window moved P px`，
否则只在出现 >60ms 长帧时打印。**判据**：`M` ≈ 拖动时长 ⇒ 泵被挂住且没人喂帧；`M` 只有
几十毫秒 ⇒ 帧一直在出。这条日志是为了让"拖动时到底刷不刷新"以后能自证，不用再问用户开开关。

**5. 工具与踩过的坑**：

- `.workbuddy/tools/dragwin.c` → `build/dragwin.exe`
  （verb：`rect` 打印外框+前台窗口、`focus` 用 `AttachThreadInput` 强行抢前台 —— 后台进程直接
  调 `SetForegroundWindow` 会被系统拒绝，抢不到焦点时"拖动"拖的是压在上面的别的窗口）：**SendInput 真输入**拖窗口
  （`PostMessage` 伪造按钮状态骗不过系统，它看物理按键状态）。用法
  `dragwin.exe [rect|resize] [steps] [stepPx] [sleepMs]`、`dragwin.exe rect` 只看窗口位置。
  **两个坑**：① 后台进程的 `SetForegroundWindow` 会被系统拒 → 拖到的是压在上面的别的窗口，
  所以现在先 `SetWindowPos(HWND_TOPMOST)` 置顶、再点客户区抢焦点、**打印
  `foreground before drag: ours/NOT ours` 并回读 `GetWindowRect` 确认位移**，否则测试白做；
  ② 编译 `zig cc -x c -std=c11 -O2 -s .workbuddy/tools/dragwin.c -luser32 -o build/dragwin.exe`。
- `.workbuddy/tools/capwin.c` → `build/capwin.exe`：想抓"屏幕上的客户区"，**方法无效** ——
  `GetDC(NULL)` + `BitBlt` 抓 GPU 合成的窗口会得到**纯白**（"拖动中 0 像素差异"是假象）。
  要真抓得用 `PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT)`。**先做对照组**（不拖动时两张
  截图也该不同）—— 这一条当场拦住了一次错误结论，留着当反例。



验证（不用眼睛也能看）：`--screenshot` 写的是 RGBA PNG（`glReadPixels(..., GL_RGBA, ...)` +
`stbi_write_png(..., 4, ...)`），所以**直接量 alpha** 就知道透明生效没有 ——
选曲界面角落的 `ImGuiCol_WindowBg` 当初就是这么抓出来的（`alpha==0` 占比 0% → 24%）。

```bash
# 临时把档案切到玻璃模式再截图，量 alpha（Pillow 在隔离 venv 里）
python -c "..."                      # 改 profiles/default.json 的 bgStyle=2
./cppsekai.exe --screenshot shots/g.png --screenshot-time 4
# 注意 --screenshot 的参数是**文件路径**（不是目录！给目录会静默写失败）
```

