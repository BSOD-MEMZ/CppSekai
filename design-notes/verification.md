# 验证

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 验证（不开窗口的自动检查）

```bash
cd build
./cppsekai.exe --sus ../assets/test.sus --screenshot shot.png --screenshot-time 3.8
```

成功时会生成截图并写 `cppsekai.log` 后退出；失败原因也在 log 里。截图应看到
pjsk 舞台、透视轨道和下落中的 note 贴图。charts/ 里有联网下载的谱面可直接用，
BGM URL 规律：`https://assets.unipjsk.com/ondemand/music/long/se_<id>_01/se_<id>_01.mp3`（不是每首都有）。

**选曲界面**也能无头截（不给 `--sus`，uiClock ~1.2s 时自动抓当前列表 + 手机面板；
给 `--screenshot-time` 可以推迟抓图时刻，用来等滚动/动画停稳）：

```bash
./cppsekai.exe --screenshot select.png --width 1920 --height 1080
```

想看"切换难度后等级/颜色是否正确"，用 `--charts <只含一首多难度谱的目录>` 跑一次，
再 `python .workbuddy/tools/pngcrop.py` 放大手机面板和等级圆核对。

**量一层半透明覆盖物（阴影/渐隐/遮罩）的形状**：它自己不好看，就用两张图的比值反算
alpha —— 同一时刻跑两次（`--screenshot` 是确定性的，只有 1 帧级噪声），
对每个像素算 `alpha = 255*(1 - 有覆盖/无覆盖)`（取两图都够亮的通道，避免除零），
把结果当灰度图放大 3~4 倍存出来，覆盖物的轮廓一眼就看见了。2026-09-19 用这招量出
失血阴影"上下边缘正中 880px 没被压暗"（见 main.cpp 里的说明与 `CPSEKAI_VIGNETTE` 探针）。

**要验证需要输入才出现的状态**（列表滚动、拖拽、悬停、点了某个按钮），无头模式可以先往
ImGui 的事件队列里塞合成输入，再按 `--screenshot-time` 抓图 —— 这是唯一能自动跑交互的招，
实测可行（2026-09-12 用它对过滚动/吸附）：

```cpp
// main.cpp 的 Select 分支里，drawSongSelect 之前（临时加，验完删掉）
ImGuiIO& tio = ImGui::GetIO();
tio.AddMousePosEvent(300.0f, 420.0f);          // 窗口像素坐标
tio.AddMouseButtonEvent(0, true);              // / false = 松手
tio.AddMouseWheelEvent(0.0f, -2.0f);           // 滚轮两格
```

要点：事件**下一帧**才生效；`AddMouseButtonEvent(0,false)` 之后列表还要走完惯性 + 0.20s
静默才提交选中，所以 `--screenshot-time` 要给足（~2.0s 比较稳）；想直接读内部状态就在
`drawSongSelect` 里临时加一行 `getenv("CPSEKAI_SCROLL_DEBUG")` 门控的 printf（日志走
`cppsekai.log`），比盯着截图猜快得多。

判定/特效的无头自检（都不需要真的操作）：

```bash
./cppsekai.exe --sus ../charts/0127_master.sus --bgm ../charts/0127.mp3 \
    --test-hits --screenshot hit.png --screenshot-time 12.0   # 走判定引擎打谱面，看特效+判定文字
./cppsekai.exe --sus ../charts/0127_master.sus --auto --screenshot auto.png --screenshot-time 12.0
./cppsekai.exe --judge-frame 3 --screenshot f3.png --screenshot-time 8.0   # 冻结判定文字第 3 帧
./cppsekai.exe --sus ../charts/0127_master.sus --bgm ../charts/0127.mp3 \
    --test-restart --restart-at 8 --screenshot restart.png --screenshot-time 12.0
```

`--test-hits` 除了逐音符调用 tap/flick，还会替长条把对应轨道一直按着（`simHolds`），
所以能无头验证 hold 尾判；截图前会打一行 `[stats]`，用它核对判定分布、尾判数、分数和血量：

```
[stats] perfect=283 great=0 good=0 miss=0 combo=283 maxCombo=283 tails=8 breaks=0 score=279556 life=1000 (100.0%)
```

（`tails` 是被判定的长条尾数，`breaks` 是中途松手断连数；完全不输入时 miss 应为音符数、
`breaks` 为 0、血量掉到 0。若长条开始没打上，整条只算开始那一个 MISS。）

**驱动 UI（没有交互会话时）**：`.workbuddy/tools/winsend.c` 编译成 `build/winsend.exe`
（`zig cc -O2 .workbuddy/tools/winsend.c -o build/winsend.exe`），按标题子串找到窗口后用
`PostMessage` 送输入，所以能在 `--screenshot` 跑着的时候点按：

```bash
./build/cppsekai.exe --window windowed --width 1920 --height 1080 \
    --screenshot sel.png --screenshot-time 7 >/dev/null 2>&1 &
./build/winsend.exe CppSekai click 210 469      # 客户端坐标，点第一个段标题
./build/winsend.exe CppSekai key 27             # VK_ESC
./build/winsend.exe CppSekai move 300 500       # 只移动（测 hover）
./build/winsend.exe "CppSekai - A" place 20 20  # 两个实例并排放（按标题区分窗口）
./build/winsend.exe "CppSekai - A" rect         # 打印窗口/客户区矩形和客户区原点
```

**假输入的三个坑（2026-09-17 一次多人回归里全踩了一遍，改 `winsend.c` 时别退化）**：

1. **SDL 的按键事件要求窗口持有键盘焦点**，否则 `WM_KEYDOWN` 被静默丢弃。单纯
   `SetForegroundWindow` 在"前台窗口属于别的进程"时会失败（脚本启动时永远如此），
   要用 `AttachThreadInput(前台线程, 本线程, TRUE)` 再 `SetForegroundWindow`；
   仍失败时补发 `WM_ACTIVATE`+`WM_SETFOCUS`（SDL 就是靠这两条维护焦点状态的）。
2. **SDL 报告鼠标按键的位置取的是它自己记录的鼠标位置**（用系统光标刷新），
   不是消息里的 lParam。所以 `click` 必须先用 `ClientToScreen`+`SetCursorPos` 把真光标
   挪过去，再发消息；否则点到哪儿全看运气。
3. **`ShowWindow(SW_RESTORE)` 会触发 Windows 的还原动画**，动画期间 `ClientToScreen`
   拿到的是旧位置，点击会整体偏几十~上百像素。只在 `IsIconic()` 时还原，并且
   `place` 之后要等 ~2s 再点。
4. 键盘输入**没有坐标**，比点击可靠得多；能键盘走的路（选曲界面的方向键/回车、
   房间页的左右键）优先用键盘。
5. `CPSEKAI_UI_TRACE=1` 时会打 `[ui] mouse down at x,y`——**查"点了没反应"先看这行有没有、
   坐标对不对**，它把"事件没到"和"到了但没命中"分开。

配合 `.workbuddy/tools/pngcrop.py` 的 `read_png()` 做**像素断言**（比肉眼看图可靠，
而且这个模型看不了图）：比如背景区均值、某个 UI 色的像素计数、两次截图同一区域的哈希
是否变化。`--screenshot` 的图是 RGBA8 非交错 PNG，`read_png()` 只吃这种格式。
注意 ① `sleep` 在这个 bash 里没有，用 `python -c "import time;time.sleep(4)"`；
② 后台起进程后 `cppsekai.log` 一时删不掉（还占着句柄），先 `mv` 走再跑；
③ 壁纸/选曲这类界面动画都在 0.4s 内结束，要在中间帧抓图得临时把时长调大
（抓完记得改回来）。

`--test-restart` 在 `--restart-at` 秒走「放弃 → 载入下一首」的完整流程（第二次
`loadMusic()`），是那个「放弃后选新曲卡死」的回归用例：跑不到 12s 的截图就是卡死了。

`.workbuddy/tools/effect_sheet_usage.py` 会统计 `effect.png` 里哪些分块被内嵌粒子引用、
多少不透明像素从没被采样过（`--dump` 出对比图）；改特效贴图或粒子数据后跑一下。

**放大看截图细节**（工具链没有 Pillow / ImageMagick）：`.workbuddy/tools/pngcrop.py`
是纯 python 的 PNG 裁剪 + 最近邻放大，用来核对小 UI（等级徽章、难度圆、HUD 数字）：

```bash
python .workbuddy/tools/pngcrop.py build/sel1.png build/crop.png <x> <y> <w> <h> [zoom]
```

