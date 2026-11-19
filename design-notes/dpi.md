# DPI / 高 DPI 显示器

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## DPI / 高 DPI 显示器：4K 缩放下「极糊」（2026-09-29，论坛报告驱动）

论坛（智教联盟 3037）两条反馈：4K + 300% 缩放时「选了窗口化却铺满全屏、元素显示不全」、
「4K 下极糊，改游戏内分辨率或缩窗口都无效」。**同一个根因：进程是 DPI-unaware。**

- `app.manifest` 从没有 `dpiAware` 节点，SDL 也默认不动（`SDL_HINT_WINDOWS_DPI_AWARENESS`
  默认 `unaware`）→ Windows 把**整个后缓冲位图拉伸**到物理像素。缩放发生在我们的最后一个像素
  交出去之后，游戏里怎么调都没用 —— 这也正好解释「缩窗口也无效」。
- 300% 下**虚拟桌面只有 1280x720**（3840/300%），所以「1280x720 窗口」必然铺满整屏；
  选更大的分辨率时窗口比虚拟桌面还大，右侧/下方就被切掉（“元素显示不全”）。
  **这不是「选了窗口化却强制全屏」，而是 unaware 进程的窗口尺寸语义被换了。**

修法：**在 `SDL_Init` 之前** `SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1")`
（`SDL_HINT_WINDOWS_DPI_AWARENESS` 要单独设，但它给的是 1:1 像素）。这个 hint 会：
① 声明 per-monitor-v2 DPI awareness（SDL 源码：`WIN_InitDPIScaling()` → `WIN_DeclareDPIAwarePerMonitorV2()`，
   失败逐级降级 PM → system）；② 把 SDL 坐标换成 **DPI 缩放点**，于是「1280x720 窗口」在
   150% 下是 1920x1080 真实像素，语义和 100% 屏一致。
- **`[dpi]` 那行**记录开关与原因；`CPSEKAI_DPI_OFF=1` 强制关掉（A/B 用）。
- 只在 `SetProcessDpiAwarenessContext` 存在时开（Win10 1607+）。**Win7/8.x 保持原样**：
  那里 SDL 会退化成 system awareness 且没有 GetDpiForMonitor 可算缩放，等于把 UI 按缩放比例
  缩小，是「能发但不能测」平台上白担的风险。

### canvas / drawable 必须分开（`platform/Renderer.*`）

开了缩放以后 `SDL_GetWindowSize`（逻辑点）≠ `SDL_GL_GetDrawableSize`（像素），
两个都有人要，混了就是"画面画在左下角一小块"：

| 字段 | 单位 | 谁用 |
|---|---|---|
| `mWindowW/H` | 像素 | `glViewport`（`renderFrame` 直画窗口时）、`presentFrame` 的 glViewport、截图尺寸 |
| `mCanvasW/H` | 逻辑点 | 布局、SDL 事件、`outputRect()/outputScale()`（指针映射要它）、窗口尺寸钳制 |
| `mWidth/mHeight` | 合成尺寸 | 窗口模式 = `mCanvasW/H`；固定渲染模式 = 钉住的 `resW/resH`（FBO 大小） |

`setRenderTargetSize(0,0)` 回到窗口模式时必须还原成 `mCanvasW/H`（**不是** `mWindowW/H`），
否则 canvas 会变成像素尺寸、整套布局按像素算。
`SDL_WINDOWEVENT_SIZE_CHANGED` 里**两个尺寸都要重查**：SDL 在只有 DPI 变化时也会发这个事件
（点数不变、像素数变），事件 payload 是点数，别拿它当像素。

- ImGui 侧不用改：`io.DisplayFramebufferScale` = 像素/点（后端自己算的），
  ImGui 1.92 会拿它当字体光栅密度（`imgui.cpp` 里 `g.FontRasterizerDensity = viewport->FramebufferScale.x`）
  → **文字是原生分辨率，不会被放大糊掉**。固定渲染模式下 main 里已经强制 `FramebufferScale = 1.0`
  （画面先进 FBO 再整体缩放），那条是对的，别删。
- 窗口尺寸钳制走 `fitWindowToDesktop(w,h,margin,usableArea)`：**只钳窗口，不钳 `resW/resH`**
  （渲染分辨率允许比窗口大 —— 固定渲染模式就是超采样）。启动与设置里改分辨率时都过一遍。
  **只钳"比屏幕还大"的**（`1920x1080` 窗口在 1080p 桌面上原样保留，那是有头无头都在用的配置），
  且**命令行显式给了 `--width/--height` 就不钳**（`--width 2600` 那种故意开超宽窗口的验证手法要用）。

### 没有 4K 屏怎么验

缩放因子 >100% 就会出现，所以**1080p 屏设成 150% 就是等价复现**（1280x720 恰好等于虚拟桌面）：

1. 显示设置 → 缩放 150% → **注销重登一次**（unaware 进程的虚拟化 DPI 在登录时才定，这步不能省）。
2. 跑 exe，看 `[window] window=AxB points=CxD ... dpi=1.50`：`points` 应约等于 `window/1.5`，
   画面清晰、窗口不再铺满。`CPSEKAI_DPI_OFF=1` 再跑一次就是修复前的样子（糊、铺满）。
3. 100% 缩放下 `dpi=1.00` 且 `window == points` —— 这是"没改坏"的判据，
   常规无头自检（`--screenshot` 的 PNG 尺寸、`[stats]`）都应当与修复前完全一致。

