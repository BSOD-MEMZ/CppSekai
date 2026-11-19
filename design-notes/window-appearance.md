# 窗口外观

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 窗口外观：整块玻璃 / 原生材质 / Vista 成本（2026-09-19 调查）

### 「整块玻璃」= MS 官方就有配方，别自己发明

现象：客户区已经是玻璃了，但窗口**自带 caption 和边框**（caption 有自己的高光/底边，外面还有
DWM 那圈 outline），玻璃像被装进了窗框。**`DwmExtendFrameIntoClientArea` 解决不了** —— 文档原话
是负 margin 产生 "sheet of glass" effect "where **the client area** is rendered as a solid surface
with no window border"，它管的是**客户区**，非客户区照旧由 DWM 画。

**正解在 `Custom Window Frame Using DWM` 这篇官方文档里**（learn.microsoft.com/windows/win32/dwm/customframe），
步骤和我们想做的完全一致 —— 就是说**这不是 hack，是微软自己教的做法**：

1. **去掉标准 frame**：处理 `WM_NCCALCSIZE`，当 `wParam == TRUE` 时**返回 0**。文档原话
   "your application uses the entire window region as the client area, removing the standard frame"。
   **不用改窗口风格、不需要 `WS_POPUP`** —— `WS_THICKFRAME` 之类留着（DWM 投影、最小/最大化动画
   都还在），只是 DWM 不再画 frame。注意创建时不会立刻生效，得补一次
   `SetWindowPos(..., SWP_FRAMECHANGED)` 逼它重算一次 `WM_NCCALCSIZE`。
2. **重新实现拖动/缩放**：文档明说"a side effect of removing the standard frame is the loss of the
   default resizing and moving behavior"，而"frame hit test messages are sent to you through the
   `WM_NCHITTEST` message, **even if** your application creates a custom frame without the standard
   frame"。**附录 C 直接给了 `HitTestNCA()` 的源码**（返回 `HTCAPTION` / `HTTOPLEFT`…），照抄即可；
   记得按 DPI 算边缘宽度（`SM_CXSIZEFRAME + SM_CXPADDEDBORDER` 的 per-DPI 版本）。
3. **caption 按钮**：`WM_NCHITTEST` 先交给 `DwmDefWindowProc`（`WM_NCHITTEST` 页面的 Vista 那段就是
   说这个），它负责 caption 按钮的 hit-test。我们要自绘全部 UI，也可以选
   `DWMWA_NCRENDERING_POLICY = DWMNCRP_DISABLED` 让 DWM 整个非客户区都不画 —— **这是最便宜的
   试探实验，先试它**（不确定是否连投影一起没掉，Win7 上实测一下就知道）。
4. 在 1~3 的基础上，`-1` margin 的玻璃就铺满整块 = 整块玻璃 ✓。系统的模态拖动循环仍然会发生，
   已经有子类化喂帧兜着（见上一节），画面照动。

**没有官方承诺的部分（别当成保证）**：
- **Aero Snap / 摇一摇 / Win11 悬停最大化键出 Snap Layouts** 这些是 shell 行为；`HTCAPTION` 在文档
  里只写了 "In a title bar"，没有任何一页承诺自定义 hit-test 区域也有这些。实测给，**但分系统验**。
  Win11 的 Snap Layouts 依赖系统画的最大化按钮，frame 去掉后得自己画 + 自己处理。
- 多人模式多窗口各透各的桌面，视觉上更乱。

**2026-09-19 更正**：本段原先写的是 "borderless (`WS_POPUP`) + `WM_NCHITTEST`" —— 那是社区常见做法，
能用但会丢掉 DWM 投影和窗口风格带来的行为。MS 官方配方走 `WM_NCCALCSIZE` 清零非客户区。

### 玻璃实现（`glassMode`，2026-09-19）

设置 → 系统 → 选曲背景选「透明（Aero 玻璃）」之后多一个「玻璃实现」下拉，**只有两档**：

| `glassMode` | 做什么 | 实测 |
|---|---|---|
| 0 `extend frame（默认）` | 只有 `DwmExtendFrameIntoClientArea(-1)` | 客户区 1280x720，透明像素 43.1% |
| 2 `自绘无框` | MS 官方 custom frame：`WM_NCCALCSIZE` 返回 0 + 自己 hit-test | 客户区 **1280x720 → 1296x760**（正好是窗框那 16x40）；透明 42.0%；Win7 上内边框消失、Aero 玻璃正常 ✓ |

**这一档只在 Vista / 7 上出现**（2026-09-19）：非 Aero 系统（Win8+）直接不画「透明（Aero 玻璃）」
和「玻璃实现」——`aeroGlassAvailable()`（`main.cpp`）用 **`RtlGetVersion`**，**不能用 `GetVersionEx`**
（没有 manifest 时 Win8.1+ 一律谎报 6.2，Win10 会被判成 Win8）。档案里带 `bgStyle=2` 时启动静默
回退成壁纸（**不回写档案**，同一份档案拿到 Win7 上设置还在）；并且 `glassMode` 只在 `bgStyle==2`
时生效，其它情况清成 0 —— 否则"档案里存着 2、背景却不是玻璃"会让窗口白变无框（启动那张图片画面
本来就要求透明窗口，`applyGlassWindowMode` 的 `enable` 恒为真，这个漏洞真会发生）。

**中间那档（`DWMWA_NCRENDERING_POLICY = DWMNCRP_DISABLED`）试过，已删**：想法是"请 DWM 别画非
客户区"，在 Win7 上它会让 DWM **回退到 Basic 窗框**（整窗连 Aero 都没了），比它本来要去掉的那个
框还丑。`SongSelect.cpp` 里加载时把旧值 1 归零，代码里留了一行注释说明，别再捡回来。

实现要点（都在 `main.cpp`，验过再改）：

- 公共入口 `applyGlassWindowMode(enable)`：算 `noFrameMode` → `applyWindowTransparency` →
  `SetWindowPos(SWP_FRAMECHANGED)`。`SWP_FRAMECHANGED` **不能省**：子类化是在窗口创建很久之后才
  装上的，创建时那次 `WM_NCCALCSIZE` 早跑完了、之后没人重算（少了它设置看着"完全没反应"）。
- **窗口结构变更必须投递到帧边界执行，不能在设置卡片里直接做**：卡片是在 ImGui 帧的中途画的，
  而 `SetWindowPos(SWP_FRAMECHANGED)` 会**同步**打来一串消息（实测 Win11 上是
  `WM_WINDOWPOSCHANGED` + `WM_MOVE` + `WM_SIZE`；Win7 同样会发）。所以卡片只置
  `requestGlassWindowMode()` 的请求，帧体开头（`ImGui::NewFrame` 之前）才真正应用。
- 模式 2 的 hit-test 在子类里（`WM_NCHITTEST`）：边缘宽度用 `SM_CXSIZEFRAME + SM_CXPADDEDBORDER`
  （Windows 自己用的数，手感才一致），顶部 `max(22, SM_CYCAPTION)` px 返回 `HTCAPTION`，其余
  `HTCLIENT`。`lParam` 两个坐标**必须按 16 位有符号解**（多显示器是负坐标），工具链没有
  `windowsx.h`，所以手写 `(short)LOWORD/HIWORD`。
- **最大化 / 全屏不参与**：`IsZoomed || IsIconic` 时把 `WM_NCCALCSIZE` 转回 SDL 的过程，否则客户端
  铺满屏幕会盖住任务栏；`windowMode == 2` 在 `noFrameMode` 里直接排除。

#### 喂帧的教训：`WM_MOVE` / `WM_SIZE` 绝不能触发一帧（2026-09-19 的崩溃根因）

症状：Win7 上在设置里切到「自绘无框」，效果出现约一秒后进程 AV 崩掉（`c0000005`）；**重启后带着
这个设置进来却一切正常** —— 只有"运行时切换"崩，说明 bug 在切换路径上。

根因：拖动喂帧那套（见上一节）最初把 `WM_MOVE` / `WM_SIZE` 也当成"拖动消息"来喂帧：

```cpp
} else if (message == WM_MOVING || message == WM_MOVE)  { counter = &st->fromMoving; }
  else if (message == WM_SIZING || message == WM_SIZE)  { counter = &st->fromSizing; }  // ← 祸根
```

而 `depth` 只挡得住"我们喂出来的帧"里再嵌套，**普通主循环里 `depth` 就是 0** —— 于是那条路变成：
设置卡片改设置 → `SetWindowPos(SWP_FRAMECHANGED)` → 客户区尺寸变了 → `WM_SIZE` 同步到达子类 →
**从帧体内部又跑了一整遍帧体**（含 `ImGui::NewFrame()`）。ImGui 不可重入，Win11 上侥幸活下来
（实测嵌套了 2~3 帧没死），Win7 上直接 AV。这也解释了为什么只有「自绘无框」会崩：只有它**改变
客户区尺寸**，切 0↔1 尺寸不变、根本不发 `WM_SIZE`。

修法（两道，都在子类里）：

1. **门闩 `dragging`**：只在 `WM_ENTERSIZEMOVE` ~ `WM_EXITSIZEMOVE`（或按键松开的看门狗）之间才算
   "正在拖动"，其余时间任何消息都不喂帧。同时把 **`WM_MOVE` / `WM_SIZE` 从喂帧名单里删掉** ——
   它们不是模态循环发的（`SetWindowPos`、`SDL_SetWindowSize` 都会产生），只留
   `WM_MOVING` / `WM_SIZING` / 自家 `WM_TIMER`。
2. 结构变更投递到帧边界（上一段）。

回归实测（Win11，`dragwin` 真输入）：改完后真拖动 54 帧被喂进模态循环（`moving=35 timer=19`）、
窗口精确位移 240px；真缩放 34 帧（`sizing=12`）、宽度精确 +160px —— **喂帧功能没被门闩挡掉**。
切换探针（下面那个）连跑多个来回不再崩、也没有再出现嵌套帧。

日志里现在会打这两行（**下次 Win7 再崩，先看它们**）：

```
[window] WM_SIZE outside a drag (frameless=1) - not served
[window] WM_WINDOWPOSCHANGED outside a drag (frameless=1) - not served
```

它们说明"机器确实会发这些消息"（Win7 会、Win11 也会），而 `- not served` 说明它们**没有**再变成
嵌套帧。

#### 崩溃现场怎么拿到：`cppsekai-crash.log`

exe 是 Windows 子系统程序，崩了什么都不留（只有 Windows 错误对话框里那个「异常偏移」）。现在
`main()` 开头装了 `SetUnhandledExceptionFilter`（`cppsekaiCrashFilter`），崩了会往 exe 旁边写
`cppsekai-crash.log`：异常码、**出错地址按 RVA 记**（和错误对话框里那个数一致）、当时的
`state/glassMode/frameless/frames`，以及 `RtlCaptureStackBackTrace` 抓的 32 层返回地址（同样记 RVA）。
写好就 `EXCEPTION_EXECUTE_HANDLER` 直接结束，不再弹系统对话框。

**这个项目没法把 RVA 反查成函数名**：zig 的 lld 生成的 exe 里**没有 COFF 符号表**
（`ptrsym=0 nsyms=0`，`-g`、`--export-all-symbols` 都没用，`-Wl,-Map`/`/MAP` 一律被 zig 拒绝），
所以"拿偏移查函数"这条路走不通，别浪费时间再试（`.workbuddy/tools/pe_symbols.py` 已删）。
能用的是两样：`.workbuddy/tools/pe_rva_dump.py <exe> <rva>` 把那个地址上的字节/指令打出来
（2026-09-25 的 `0xC000001D` 就是这么认出 `C4 E2 59 52` = VPDPWSSD 的），以及
`cppsekai.log` 最后一行的 `[boot] ...` / 第一行的 `[settings] ...`（说清死在哪个启动阶段）。
定位靠**崩溃日志里的状态 + 上面的步进日志 + 探针复现**。

#### 复现用的探针

`CPSEKAI_GLASS_TOGGLE=<秒>`：到点自动把 `glassMode` 在 0/2 之间翻一次，走的就是设置卡片那条投递
路径 —— 这样"切一下就崩"不用手点也能复现：

```bash
cd build && CPSEKAI_GLASS_TOGGLE=5 ./cppsekai.exe --no-party --screenshot shots/x.png --screenshot-time 12
```

（`--screenshot` 会强制把日志写进 `cppsekai.log`；从 Git Bash 直接跑的话 stdout 被父进程接走、
**不生成日志文件**，这一点坑过一次。）

**待用户在 Win7 上复测**：切「自绘无框」→ 应该不再崩；万一还崩，把 `cppsekai-crash.log` 和
`cppsekai.log` 末尾发我。

### 原生材质（Win10 亚克力 / Win11 Mica）：一条官方、一条野生

我们现在做的"透明 + DWM 玻璃"只在 Win7 Aero 上有模糊；Win8+ 是纯透（没有模糊）。

| 系统 | 做法 | 官方性 |
|---|---|---|
| Win7 / Vista | `DwmExtendFrameIntoClientArea(-1)` | **文档明确**：负 margin = "sheet of glass"（已实现 ✓） |
| Win8 / 8.1 | 同上，只有透明没模糊 | Aero 在 Win8 被砍 |
| Win10 1803+ | `SetWindowCompositionAttribute` + `ACCENT_ENABLE_BLURBEHIND`（轻）/ `ACCENT_ENABLE_ACRYLICBLURBEHIND`（真亚克力，吃 GPU、有历史 bug） | **未文档化**：user32 内部导出，learn 上没有页面，能用但无支持承诺。Win10 上**没有**文档化的替代品 |
| Win11 **22621+** | `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE=38, DWMSBT_MAINWINDOW=2 /*mica*/ / DWMSBT_TRANSIENTWINDOW=3 /*acrylic*/)`，配 `DWMWA_WINDOW_CORNER_PREFERENCE=33`、`DWMWA_BORDER_COLOR=34`（`DWMWA_COLOR_NONE=0xFFFFFFFE` 可去掉那条边框线） | **官方**。枚举文档写的是 **build 22621** 起；21H2/22000 上只有未文档化的 `DWMWA_MICA_EFFECT=1029`。圆角/边框色/暗色模式是 22000 起 |

官方设计文档（`/windows/apps/design/style/mica`）还写了几条**会打脸的前提**：想看见材质就得
"set the background to **transparent** for all layers where you want to see Mica"；而在系统关掉透明
效果 / 节电模式 / 低端硬件 / **窗口失去激活** / **系统版本低于 22000** 这几种情况下，Mica 会
**退化成实心底色**。另外 MS 给 Win32 的 Mica 教程走的是 **Windows App SDK 的 `MicaController`**
（要 App SDK 运行时依赖，对静态链接的 exe 太重）—— 我们直接用上面那条文档化的 attribute 即可。
顺带一提：官方那个 Win32 Mica 样例窗口本身还留着 `WS_OVERLAPPEDWINDOW`（frame 还在），
所以"整块玻璃"和"材质"这两条得我们自己拼。

全部 `GetProcAddress` 动态取（工具链没有对应导入库），一块 `platform/WindowMaterial.cpp` 按系统
分派，约 100~150 行。**Mica 我在 Win11 上能实测**（合成结果 `--screenshot` 抓不到，得人眼看），
Win10 亚克力只能他那台 22H2 验。材质只在"背景不填充"（`bgStyle == 2`）时才看得见 ——
铺满的窗口没有材质可言。

### 兼容 Vista 的成本（2026-09-19 实测）

**已经免费达标的部分**（实测，不是推测）：

- **导入表**：`pe_imports.py` 现在带一组"Win7 独占"名单（`GetLogicalProcessorInformationEx` /
  `SetThreadGroupAffinity` / `GetActiveProcessorCount` / `SetThreadErrorMode` …）。
  三个 PE（cppsekai.exe / chartdl.exe / SDL2.dll）**都没有 Win7 独占导入，也没有 Win8+ 独占导入**；
  源码里也没有直接调用 `SetThreadDescription` / `GetDpiForWindow` / `PathCchCanonicalize` 这类新 API
  （SMTC 那套本来就有 Win10 门）。
- **UCRT**：Vista SP2 **有官方包** —— `Windows6.0-KB2999226-x64.msu`（微软下载中心 id=48234，
  写明支持 Vista SP2 / Server 2008）。也就是和 Win7 一样的处理：装一次 redist，或随包带 app-local。
- **SDL2 2.32**：导入表同样干净（对新 API 一律动态加载），官方口径一直是 Vista+；
  **miniaudio** 走 WASAPI，而 WASAPI 正是 Vista 引入的。
- **Aero / DWM**：`DwmExtendFrameIntoClientArea` 在 Vista 上就有（Aero 就是 Vista 的东西）。

**真正的阻塞是 GPU 驱动，不在我们这边**：

- **Intel 集显在 Vista 上的最后一个驱动（15.22.54，2012-01）只暴露 OpenGL 3.1**（GLSL 1.40）
  —— 而 CppSekai 是 **GL 3.3 core**（shader `#version 330`、ImGui 的 GL3 后端用 `glBindSampler`）。
  Intel 集显的 Vista 机器**跑不起来**。
- 独显（NVIDIA / AMD 最后一批 Vista 驱动）到 GL 4.x，能跑 ✓。

**结论**：API 层面已经"顺带干净"，所以**继续只承诺 Win7 SP1+** 是最省事的；如果哪天想收 Vista，
先决定"要不要为 Intel 集显把渲染器降到 GL 3.1"（shader 改 1.40 + 手动 `glBindAttribLocation` +
ImGui 后端降级 + 去掉 `glBindSampler`），那是一块真活儿，而目标机器（Vista + Intel 集显）基本
只剩虚拟机。**我的判断：不值得**。要收就只承诺"独显机器可以试"，并把 UCRT 前置条件写清楚。

**没 3D 驱动时要优雅拒绝，别崩（2026-10-10 修，用户实机报的）**：

`SDL_GL_CreateContext` **会成功** —— GDI Generic 会给一个能用的 **OpenGL 1.1** 上下文，所以
「上下文为 NULL 就退出」那条守卫根本不触发；接着 imgui 的 GL3 loader 因版本 < 2.0 打印
`Failed to initialize OpenGL loader!` 并返回 false，而 `io.BackendRendererUserData` 一直是 NULL，
于是第一个 `NewFrame()` 里 `bd->ShaderHandle` 读了 `[NULL+0x30]` → **0xC0000005 @ rva 0xDA3E7**，
窗口都没出来、也没有崩溃框。现在有四道：

- 拿到上下文后先 `queryGlVersion()`。**3.0 的 `GL_MAJOR_VERSION` 枚举 MinGW 的 1.1 gl.h 里没有**，
  写成字面量 `0x821B` / `0x821C`；老上下文上这两个查询会失败并留下 0，这时回退解析
  `GL_VERSION` 的 `"major.minor"` 前缀。
- 每次启动打一行 **`[gl] 3.3 | version='…' renderer='…' vendor='…'`** —— 别人机器上的显卡问题
  以后就靠这一行定性（这次排查时日志里什么 GL 信息都没有）。
- `< 3.3` 走 `reportGlRefusal()`（stderr + 原生 `MessageBoxW`）后 `return 1`。双击运行没有控制台，
  那个消息框是唯一能让用户看见的东西；**`--screenshot` 无头跑不弹框**（没人点确定，会挂住自检）。
- **`ImGui_ImplOpenGL3_Init()` 的返回值必须接**（false 就是 loader 没干活），否则又回到上面那个
  NULL 解引用。调试开关 `CPSEKAI_FAKE_GL_VERSION=2.1` 可以假装版本不够，用来在有显卡的机器上
  无头验证这条拒绝路径。
- 用户侧的解法是 Mesa 软件渲染补丁包：`dist/CppSekai-software-gl-mesa-*.zip`，
  打包脚本 `.workbuddy/tools/make_nogpu_patch.py`（Mesa 的 dll 放 `toolchain/mesa-<ver>/x64`）。
  实测 i7-1260P 1366x768 关 vsync：选曲 55–66 fps、演奏 83–112 fps，同机真显卡 214–219 fps。
  ⚠ **它解决的是「驱动层」，不是「系统层」**（2026-10-11 有人问「能不能在 XP/Vista 上软解跑」时
  查证过）：`pe_imports.py` 实测 Mesa 那两个 dll（`opengl32.dll` / `libgallium_wgl.dll`）
  **不依赖 UCRT**（只链 KERNEL32/GDI32/USER32/ADVAPI32/SHELL32/VERSION/ntdll/ole32），
  也**没有 Win8+ 的静态导入** → Win7 上加载没有障碍（**但没有在 Win7 上实测过**）。
  真正挡在前面的是我们自己的 exe：它静态依赖 11 个 `api-ms-win-crt-*`（UCRT），而 UCRT 官方
  只到 **Vista 及以上**（app-local 分发），**XP 根本不支持**；加上只发 x64、文档底线是
  Win7 SP1 —— 所以「XP 不用想，Vista 理论上有缝但不值得赌，Win7 SP1 才是有意义的下限」。
  另外 llvmpipe 吃 CPU 指令集，别拿 SSE2-only 的老 CPU 试。

**降 GL 版本不会有性能收益**（2026-09-19 实测，用户问 "GL3.1 性能会不会比 3.3 好"）：GL 版本号
是**能力集**不是性能档，同一驱动的光栅化路径一样。我们本来就已经在 **3.3 core profile** 上
（`SDL_GL_CONTEXT_PROFILE_CORE`，main.cpp:1108 起），core 才是这附近唯一真正影响性能的开关
（免掉兼容 profile 的旧状态校验）。实测把垂直同步关掉、上限抬到 240：**1280x720 → 214 fps、
1920x1080 → 219 fps** —— 像素翻倍帧率不动，说明瓶颈在 CPU 侧（每帧的提交/绘制调用），
**根本不在填充率或 GL 特性**上。降到 3.1 只会丢功能（sampler object、显式 attrib location…
顺带一提我们的 shader 其实没用 `layout(location=)`，ImGui 后端也自带 pre-3.3 分支），
外加为此改一遍代码，换 0 fps。

