# 「透明（Aero 玻璃）」选曲背景

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 「透明（Aero 玻璃）」选曲背景 2026-09-19

`设置 → 系统 → 选曲背景` 的第三项 `bgStyle = 2`：**完全不填充背景**，窗口自己的像素保持透明，
于是透出桌面 —— Win7 Aero 下就是原生毛玻璃，其它系统是纯透明。选曲界面那层漂浮的三角形/圆形
（`BgShape`）保留，这是这个选项的重点（"关掉填充但留住装饰"）。

不填 = 四件事一起做，少一件都还是黑的：

1. `Renderer::setTransparentBackground(true)`（`main.cpp` 启动时设一次，设置里切换时再设）：
   `renderFrame()` 清屏 alpha 设 0、`drawStaticScene()` 跳过 `background_overlay.png` 那块
   背景板（**舞台/判定区照旧**，不然音符会飘在桌面上）、`presentFrame()` 的黑边也变透明。
2. **选曲界面自己的 ImGui 窗口**也要透明：`ImGuiCol_WindowBg` 默认是 0x06/0.94，铺满全屏，
   是最后一块不透明的东西 —— `setSelectTransparentBackground(true)` 时 push 成全透明，
   卡片/按钮/列表行各自保留底色。梯度与壁纸（`drawFill`）同理跳过。
3. **窗口**：`SDL_GL_ALPHA_SIZE 8`（默认 framebuffer 得带 alpha）+ DWM
   `DwmExtendFrameIntoClientArea(hwnd, {-1,-1,-1,-1})`。这两件本来只有图片启动画面
   （`splashStyle == 0`）需要，现在 `splashStyle == 0 || glassBackground` 都要，
   `main.cpp` 里抽成了 `applyWindowTransparency(window, bool)`（关掉时传 false = 零 margin，
   把不透明的客户区还回来）。
4. **别让窗口铺满显示器**：Windows 会把覆盖整个显示器的窗口提升成 "fullscreen optimized"
   展示、绕过 DWM 合成，透明背景直接变不透明。所以 `windowMode == 2` 时窗口缩 16px
   （原来只有图片启动画面这么做）。**全屏下玻璃失效是系统的锅**，设置里那句灰字就是解释这个。

`bgStyle` 的取值范围随之变成 0..2（`SongSelect.cpp` 的 clamp 要跟着改），
`profiles/*.json` 里存的就是这个数。

**顺带修的一个老 bug**：选曲态那句 `renderer.renderFrame(nullptr, 0, 0.85f)` 没给
`playfieldVisibility`，用的是默认值 1 —— 于是**选曲界面底下一直在画整个舞台/判定区**。
以前看不出来（选曲界面自己不透明的背景盖住了），玻璃模式一开就露馅，看着像"歌单后面摆了个
舞台"。现在传 0（只留背景板），选曲界面玻璃模式下 alpha==0 的像素从 23.8% 涨到 43.1%。
**注意 `--screenshot` 的噪声基线极小（同版本两次运行只差 6 个像素）**，所以"改前改后截图
逐像素比"能当回归用：这次普通模式下有 4903 个像素变化（0.5%），就是那层舞台从半透明 UI
底下消失造成的。

