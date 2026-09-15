# AGENTS.md — 给 AI 助手的项目指南

CppSekai：Project SEKAI 风格 SUS 谱面 Windows 原生游玩器。
上游是 [sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web)（AGPL-3.0），
其谱面核心从 MikuMikuWorld（MIT）移植。**本仓库整体遵循 AGPL-3.0-only，改动必须保持开源。**

## 架构（改代码前先读这段）

```
core/native/src/mmw_preview.cpp   # 谱面核心（上游代码，勿改结构）
  - SusParser::parseText          # SUS 解析
  - susToScore                    # 谱面数据模型 + tick/秒换算
  - calculateDrawData/calculateHitEvents  # 绘制数据 & 判定事件生成
  - extern "C" API：loadSusTextPrecise / render(chartTimeSec) /
    getQuadBufferPointer / getHitEventBufferPointer
core/native/mmw_port/             # MikuMikuWorld 移植层（上游代码）
platform/                         # 平台层（本项目新增）
  Renderer.cpp  # OpenGL 3.3 core，消费核心输出的 packed quad（25 float/quad）
  Audio.cpp     # miniaudio；音频时钟 = 全局主时钟；音乐文件位置 = songTime + startPos + userOffset；
                # 自动检测 BGM 开头静音填充（官服 mp3 有 ~9s，musics.json 的 fillerSec）
  SystemMedia.* # SMTC（系统媒体传输控件，手写 WinRT vtable）+ ITaskbarList3 任务栏进度条
  CoreApi.cpp   # core_api.hpp 的 C++ 包装 + #WAVEOFFSET 文本扫描
game/Judgement.*  # 判定引擎（本项目新增，判定逻辑都在这）
                  # hold 尾判：谱面核心在长条结束时间会额外发一个普通 tap/flick/trace 事件
                  # （kind 0/1/2/3，即 SUS 的 NoteType::HoldEnd），load() 里把它按「同时间同轨道
                  # 对上 kind 5 标记的 endTimeSec」打上 holdTail 标记，之后只由松手判定：
                  # 结束前 ≤perfect/great/good 松手给 Perfect/Great/Good，一直按到底也是 Perfect；
                  # 提前松手（超过 180ms）才算断连。holdTail 事件不走 findCandidate / 自动 miss。
                  # 分数/血量：分数用上游 TEAM_POWER/weightedCount/comboFactor 公式
                  # （kTeamPower 等常量见 Judgement.hpp），血量 1000 起，整音 MISS -80、长条中断 -40。
game/Ui.*         # pjsk 风格弹窗组件库：beginCard（缩放入/出场动画 + 标题栏拖动）、
                  # tabBar、slider（深色±按钮+薄荷轨道）、infoRows、capsuleButton、
                  # cardTitle、checkBox、stepper、messageDialog（-3=动画中 -2=关闭完成）、
                  # combo（= BeginCombo + 淡入 + 自绘旋转箭头；最后一个参数 scaleHint 用来
                  # 适配调用方自己的 px-per-unit，比如选曲界面的 k）。动画统一走文件顶部的
                  # animValue / animToggle（按 ImGuiID 存一个"指数逼近"值：步长由 DeltaTime
                  # 推出、帧率无关、不会过冲）+ mixColor 颜色插值 —— 页签上滑、胶囊/stepper
                  # 悬停放大与按压回弹、对勾从中心长出、滑杆把手放大、combo 箭头 180° 翻转。
                  # `ui::anim(id, target, rate)` / `ui::mix(from, to, t)` 把同一套缓动对外
                  # 暴露，给屏幕自绘的部件用：选曲列表行、分组标题条、跳转面板字母、右下角
                  # 圆形按钮（随机/设置）的悬停淡入都走它。id 空间是本模块私有的常量，
                  # `0x4a55x000u + index` 这种写法就行，不必去凑 ImGui 的 ID 栈。
game/Result.*     # 结算画面（PRESENT/RESULT）：参考原版截图 1:1 复刻，全部画在 ImGui
                  # background draw list 上的 1920x1080 虚拟画布（和 HUD 同一套 px/py/ps 变换）。
                  # 左半边（RESULT 水印、曲目卡、得分、判定行）用参考截图的绝对 x；
                  # 右半边（进度条、SCORERANK 牌、继续按钮）挂在上方面板右缘上——手机版那块
                  # 是留给 live2d 的，16:9 里没有角色，所以面板直接铺到右边。
                  # 数字全部用游戏自带精灵（score/digit/*、combo/p*），不是字体。
                  # 详细测量笔记见下面「结算画面」一节。
game/Intro.*      # ImGui 卡片/UI；字体跟随系统（注册表找字体文件 + CJK 字形探测，Yu Gothic UI
                  # 是 CFF 轮廓 stb_truetype 渲染不了，会自动落到 Microsoft YaHei UI；--pjsk-font 回退）
main.cpp          # SDL2 窗口、事件循环、输入映射、ImGui HUD、截图模式
```

数据流：`loadSusTextPrecise → render(t) → packedQuads → Renderer::renderFrame`；
判定侧：`getHitEventBuffer → JudgementEngine::load → tap/flick/update`。

### 关键数据格式

- packed quad（25 floats）：4×(x,y,reciprocalW) + 4×(u,v) + rgba + textureId。
  textureId 0=notes 1=longNoteLine 2=touchLine（世界坐标，需 worldToClip 变换）；
  ≥3 = effect.png（已是裁剪空间坐标，id==4 为加法混合）。
- packed HitEvent（7 floats）：timeSec, center(轨道坐标), width, kind, flags, endTimeSec, volume。
  kind：0=tap 1=critical tap 2=flick 3=trace 4=hold tick(自动) 5=hold 标记(endTimeSec 有效)。

## 工具

- `downloader/chartdl.cpp` → `build/chartdl.exe`：**独立的谱面下载器**（不算游戏的一部分，
  build.sh 里单独编一次）。界面是**纯 Win32 通用控件**（ListView 带 checkbox + Edit +
  ProgressBar + ListBox，全中文，不共享 ImGui/SDL），链接 `-Wl,--subsystem,windows`，
  命令行模式自己 `AttachConsole(ATTACH_PARENT_PROCESS)` 把父进程控制台借回来（已经拿到
  管道句柄时不能抢，否则 `chartdl.exe --list | head` 输出会跑去 console）。
  HTTP 走 `winhttp.dll` **运行时 LoadLibrary**（工具链只有 winhttp.def，
  没有导入库——和 SystemMedia 用 combase 的办法一样）。下载在 `std::thread` 里跑，进度用
  `gJobMutex` 保护；**别在 worker 里持有 `gJobs[i]` 的引用**（GUI 线程还会 push_back，会悬空）。
  踩坑：文件里 `namespace http { std::wstring widen(...) }`，UI 段要用 `using http::widen;`；
  全局 `gLog` 是日志字符串数组，控件句柄得另起名（`gLogList`）。
  `--list` / `--download` 是给脚本和回归用的无界面模式。日志同时进 stdout 和 `chartdl.log`
  （GUI 从资源管理器启动时 stdout 是黑洞）。
  **界面绑 Common Controls v6**：`app.manifest` 声明 `Microsoft.Windows.Common-Controls
  6.0.0.0` 依赖，`app.rc` 以 RT_MANIFEST（24）id 1 嵌进 `build/app.res`（游戏 exe 共用同一份）。
  没有它进程会绑到 System32 那套 5.82 兼容实现，ListView / 按钮 / 进度条全是 Win95 平面样式。
  验证：起一个实例后看它加载的 `comctl32.dll` 路径是不是
  `C:\Windows\WinSxS\amd64_microsoft.windows.common-controls_*_6.0.*\COMCTL32.dll`
  （**别信 FileVersion**，那里显示 5.82 是 MUI 资源的旧版本号，路径才是判据）。
  **歌曲表按 id 去重**：下载路径里的 `0374_normal.sus` 全由 id 拼出来，所以同 id 出现两条
  记录既会重复列一行、又会让两个任务抢同一个输出路径。`loadData()` 用 set 丢掉后来的重复
  id，`rebuildList()` 再兜一层，丢掉的条数写进日志（`dropped N duplicate song id(s)`）。
  **列与排序**：九列（ID / 曲名 / 读音 / EASY..MASTER / 演唱版本），点表头排序、再点一次反向。
  排序标记写在**表头控件**上（`Header_SetItem` + `HDF_SORTUP/DOWN`）：ListView 自己存了一份列文本，
  v6 下改 ListView 的列**不会**同步到表头（只写 ListView 的话屏幕上看不到任何变化，但
  `LVM_GETCOLUMN` 读回来是对的 —— 很容易误判成"没重绘"）。
  **字体 / DPI**：`chartdl.manifest` 声明了系统 DPI 感知（**游戏那份故意没有**，见
  SDL_HINT_WINDOWS_DPI_AWARENESS 的默认值是不改感知），布局里所有硬编码数字都过 `dp()`
  （96dpi 单位 → 实际像素），字体走 `createUiFont()`（依次试 Microsoft YaHei UI → Microsoft
  YaHei → Segoe UI → Tahoma，用 `GetTextFaceW` 反查确认没被 GDI 悄悄替换）。启动打一行
  `[ui] dpi=… client=… screen=… font=…`，DPI 出问题先看它。`--dpi <n>` 强制缩放值，是 100% 机器上
  唯一能看高 DPI 布局的手段。
  **资源分家**：图标 + 版本信息在 `resources.rc`，`app.rc` / `chartdl.rc` 各自 `#include` 它再加
  自己的清单 —— 下载器要 DPI 感知、游戏不要，共用一份 `.res` 做不到。
  `--screenshot` 抓图前会先 `RedrawWindow(…RDW_ALLCHILDREN)` 强制重绘，并且
  **`PrintWindow` 必须带 `PW_CLIENTONLY`** —— 位图是按客户区大小开的，不带它的话 PrintWindow
  会把整窗（含标题栏）渲染进去，于是顶部 ~30px 变成标题栏、客户区底部被截掉，**图上每个 y 都偏了
  约 30px**（我就是拿偏移后的坐标去对表头，白绕了两轮）。
  **文本一律走 `toUtf8()` / `windowTextW()`，路径一律留 `std::wstring`**（2026-09-14 修崩溃）：
  之前 `windowText()` 把用户输的 UTF-8 塞进 `fs::path` 再 `.string()` 取回来，而 Windows 上
  `fs::path` 内部是宽字符、窄的那一端是**本地 ANSI 代码页**（日文机 Shift-JIS / 中文机 cp936）。
  在搜索框里打一个假名必崩：假名 UTF-8 是 `E3 81 82`，前两字节正好是合法 SJIS 双字节字、
  第三字节是孤立引导字节 → libc++ 抛 `filesystem_error: __char_to_wide: Illegal byte sequence`
  → 没人接 → `std::terminate` → `abort()`。WER 里长这样：`chartdl.exe` / `ucrtbase.dll` /
  `0xc0000409` / 异常数据 `7`（FAST_FAIL_FATAL_APP_EXIT）/ `EventType=BEX64`。
  同样的坑：`path.string()`、`fs::path(<UTF-8 窄串>)`。**别用** `.string()`，要文本用
  `pathText()`（= `toUtf8(path.native())`）。**游戏本体还没清干净**：`game/SongSelect.cpp` 仍在用
  `path.string()` / `fs::path(窄串)` 来回倒 `susPath`，谱面文件名里一旦有 ACP 表示不了的字符
  （中文/emoji 的自制谱、非日文区路径），扫描时会抛同一个 `filesystem_error` → 启动即崩。
  要修得先决定 `ChartEntry::susPath` 的编码约定（现在是"扫描时 ACP 窄串、用时再 ACP 转回去"）。
- `.workbuddy/tools/winmsg.c` → `build/winmsg.exe`：按窗口类名（+ `--pid` 指定实例）给控件
  发消息的无头驱动小工具，也是上面那个崩溃的复现器。用法：
  `winmsg.exe <class> list|alive|gettext <id>|settext <id> <utf8>|char <id> <hex>|click <x> <y> [--pid N]`。
  编译（不进 build.sh）：
  `zig c++ -x c++ -std=c++20 -O2 -s .workbuddy/tools/winmsg.c -luser32 -limm32 -o build/winmsg.exe`。
  注意 `GetWindowText` **读不到别的进程的控件文本**（跨进程只拿得到窗口标题），别拿 `gettext`
  当功能验证；`char` 走的是 WM_CHAR，ImGui 那类读 SDL 事件的界面要用 `click`。
- `.workbuddy/tools/gen_music_vocals.py` → `music-vocals.json`：从官方的 musicVocals +
  gameCharacters 表生成演唱版本表（`asset` 就是 unipjsk 的音频目录名）。
- `.workbuddy/tools/winsend.c` → `build/winsend.exe`：按窗口标题 PostMessage 真鼠标消息，
  无交互会话下驱动 UI（见「平台 / 输入相关的坑」）。

## 构建

```bash
bash build.sh          # 仅需 Git Bash；产物 build/cppsekai.exe + SDL2.dll + assets/
```

- 编译器是自带的 zig 0.14.1（`toolchain/`），**不要用 0.16**（其 c++ 驱动会吞 `-I`）。
- zig 编译缓存**必须放 C 盘**（build.sh 已设 `ZIG_GLOBAL_CACHE_DIR`）；D 盘文件系统不支持 zig 缓存所需的文件操作，会报 `CacheCheckFailed/AccessDenied`。
- MinGW 的 gl.h 只有 GL 1.1：GL 3.3 的函数指针和常量在 `platform/Renderer.cpp` 顶部的 `namespace gl` 里手工声明，加新 GL 调用时去那里补。
- SDL2 的 MinGW 导入库需要额外链接 imm32/setupapi/version/oleaut32，且要自己 stub 三个屏保符号（`ScreenSaverProc` 等，在 main.cpp 顶部）。
- **`main.cpp` 必须在 include SDL.h 之前 `#define SDL_MAIN_HANDLED`**，否则 SDL.h 把 main 重定义为 SDL_main，程序会变成"秒退且无输出"。
- miniaudio 的实现（`MINIAUDIO_IMPLEMENTATION`）只在 `platform/Audio.cpp` 里定义一次。

## 约定与坑

- `core/native/` 下的文件是上游代码：能不改就不改；确需改时在注释里标注原因，便于同步上游。
- 资源按 exe 所在目录解析（`SDL_GetBasePath()`），不按 CWD。charts/ 目录会依次尝试
  `--charts` → `exe\charts` → `exe\..\charts` → `./charts`，取第一个有谱面的。
- 音频对齐：官服 mp3 开头有静音填充（fillerSec≈9s），谱面 tick0 在静音之后。优先级：
  sidecar json `fillerSec`/`offset`(ms) > 自动静音检测 > 0；`--filler`/`--offset` 可覆盖。
- **界面缩放（`UserSettings::uiScale`）只作用于选曲和结算**：这两个画面都画在 1080p 虚拟画布上，
  演奏界面和 HUD **故意不吃这个值**（打歌时放大/缩小 UI 比 UI 偏小更糟）。
  - 选曲：`k = kBase * scale`，其中 `kBase` 仍是"窗口有多大"那份；手机面板是唯一由窗口尺寸算出
    的部件，所以它额外乘 `scale` —— 不乘的话列表会变大而面板纹丝不动。
  - 结算：`makeCanvas()` 的 `scale` 乘上它。**`resultContinueHitTest()` 必须收到同一个 uiScale**，
    否则「继续」按钮的判定框会和画面对不上（触摸/鼠标点不中）。
  - `--ui-scale` / 设置卡片「画面」页滑杆，范围 0.7~1.5，存 `userdata.json` 的 `uiScale`。
- **`app.manifest` 里没有 `dpiAware`，游戏是 DPI unaware**：SDL2 的 `SDL_HINT_WINDOWS_DPI_AWARENESS`
  默认是空串（"不改变 DPI 感知"，见 SDL_hints.h），main.cpp 也没设它 —— 所以在缩放显示器的
  系统会把窗口位图拉伸（糊），同时 `SDL_WINDOW_ALLOW_HIGHDPI` 因为没有感知而没有实际作用。
  要改就得同时动 manifest 和 SDL hint，并且窗口的物理尺寸语义会跟着变（高 DPI 下窗口变小），
  所以一直是"记录在案、没动"。
- **`AudioEngine::loadMusic()` 必须先 `ma_sound_uninit` 掉上一首**：miniaudio 的
  `ma_sound_init_from_file` 内部会 `MA_ZERO_OBJECT(pSound)`，对已经初始化的 ma_sound 再 init
  会把它在引擎资源表里的节点丢掉，之后引擎遍历到坏节点直接假死。表现就是「打到一半点放弃、
  再选新曲 → 卡死」，重试（retry）同理。回归用例见 `--test-restart`。
- 输入法（搜索框）：SDL2 的 `SDL_HINT_IME_SHOW_UI` 默认是 "0"（不显示候选窗），而 ImGui 的
  SDL2 后端是在 `SDL_CreateWindow` **之后** 才设这个 hint，对主窗口无效。必须在 `SDL_Init`
  之后、建窗之前自己 `SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1")`，否则输入法面板根本不出现。
- SUS 的 `#WAVEOFFSET` 单位是秒；核心 API 的 offset 参数是毫秒且只进 metadata，实际延迟由 AudioEngine 实现。
- SMTC/ITaskbarList3 是手写 WinRT/COM vtable（工具链无 Windows SDK）：IID 与方法顺序来自解析
  `C:\Windows\System32\WinMetadata\Windows.Media.winmd`，解析脚本在 `.workbuddy/tools/`；
  combase.dll 相关函数全部 LoadLibrary 动态加载，无需导入库。改接口调用前先跑脚本核对槽位。
  - `winmd_dump.py` 打印**接口方法声明顺序 = ABI 槽位**；`"~name"` 是子串匹配（versioned
    接口名如 `ISystemMediaTransportControls2` 不好猜）。**枚举数值它不导出**（winmd 的
    Constant 表列顺序和 ECMA 不一致，读出来会错配到相邻成员）→ 枚举值查官方文档。
  - `winmd_guid.py` 取 IID，短名也认（它内部是 "命名空间.类型" 的表，早先只支持全名，
    查短名一律 NOT FOUND）。
- **SMTC 封面（put_Thumbnail）必须走 Uri，别走 StorageFile**：`StorageFile.GetFileFromPathAsync`
  是 async 工厂，本线程是 STA，完成回调被投递到 apartment 队列 —— 实测轮询 `IAsyncInfo::get_Status`
  （带不带消息泵都一样）永远停在 Started，1 秒超时后拿不到 StorageFile。现在用
  `Windows.Foundation.Uri`（`IUriRuntimeClassFactory::CreateUri`，IID
  {44A9796F-723E-4FDF-A218-033E75B0C084}）+ `RandomAccessStreamReference::CreateFromUri`
  （IID {857309DC-3FBF-4E7D-986F-EF3B1A07A964}，statics 槽 1），全程同步。路径要先
  `weakly_canonical` 绝对化（谱面扫描可能给相对路径）再逐字节百分号编码，保留 `file:///`
  与盘符冒号；日志里会回读 `get_AbsoluteUri` 确认 shell 看到的是什么。
- 窗口/帧率：`--width/--height`（默认 1280x720）、`--window borderless|windowed|fullscreen`、
  `--fps <n>`（vsync 之外的软上限，0=仅垂直同步）；调试面板（H）里可实时切换窗口模式和帧率上限。
- 输入：触摸（SDL_Finger*）与鼠标（左/右键 = 两个指针，合成负 id）共用 main.cpp 里的
  `beginPointer/movePointer/endPointer` 一条路径；屏幕坐标 → 裁剪空间 → 世界轨道坐标的逆变换在
  `Renderer::clipToWorldX/Y`。鼠标按下前要排除 ImGui 占用（`io.WantCaptureMouse`，设置面板/暂停
  弹窗）和 HUD 暂停按钮（`game::lifePauseRect()` 在 1920x1080 虚拟坐标做命中测试，事件层命中后
  置 `pauseClickRequested`，渲染层统一处理），否则点 UI 会被当成击打。
- 启动顺序（黑屏优化）：窗口 + GL 上下文就绪后先 `glClear` 换一帧；**ImGui 在这里就初始化**，
  加载的各阶段之间由 `drawSplash()` 画一帧 splash（暗底 + 标题 + 进度条，ImGui 默认字体仅 ASCII，
  期间垂直同步临时关闭）。`Renderer::loadSplash()` 只加载 background/stage（约 0.6s），之后才加载
  HUD 贴图和 CJK 字体图集。**`loadIntroFonts()` 之后必须 `ImGui_ImplOpenGL3_DestroyDeviceObjects()`**，
  否则 GL 后端还持有 splash 用的默认字体纹理，字形 UV 错位、全部 UI 文字花屏。各阶段耗时用
  `[boot]` 日志查看；贴图级耗时设 `CPSEKAI_ASSET_TIMING=1`。
- **贴图尺寸策略（2026-09-14 傍晚已回滚）**：一度用 `loadTextureFromFile(path, err, maxDim,
  cropHeight)` 把官方素材按整二次幂缩 / 裁行（`kStageKeepRows` / `kBackgroundMaxDim` /
  `kEffectMaxDim` / `kGradientMaxDim` / `kLifeDigitMaxDim` 这些常量还留在 `Renderer.cpp` 顶部），
  但缩/裁之后的纹理尺寸和调用方自己维护的 sprite 矩形对不上，**画面上精灵整体错位**。现在
  `keepRawTextures()` 默认返回 true，所有贴图按解码尺寸上传（日志 `[tex] N MB uploaded (full size)`，
  53.2MB）；**只有** `CPSEKAI_TEX_RAW=0` 才重新启用旧限制（仅用于量内存）。素材文件一如既往不动。
  - 自动化对照：**两次独立运行的截图本身就有噪声**（音符区 raw-vs-raw 平均差 41、raw-vs-opt 54，
    全图 20.4 vs 18.8），所以别拿单帧 diff 当回归标准；看 HUD / LIFE / COMBO 这类确定性区域
    （实测逐像素一致）就够了。
- **选曲背景漂浮形状**（2026-09-14）：移植 pjsk.moe 网页背景（BackgroundPattern 组件，chunk
  `1lvqppbmv_p-9.js`）：3 层共 34 个形状（80% 三角形 / 20% 圆），mulberry32(0x9e3779b9) 生成、
  每次运行完全一致；颜色为 miku 青 / cyan / pink / yellow / 白，三角形点位 `10,0 0,100 100,85`
  叠 `scale()` `skewX()` `rotate()`，large-faint(60~95px, a=.08~.13) 占 2/3、small-bold
  (22~38px, a=.30~.48) 占 1/3。滚动列表时按层系数 **-0.30 / -0.16 / -0.07** 做视差
  （`drawBgShapes()`，`game/SongSelect.cpp`，位移对齐到进屏时的 scroll 并 clamp ±900px）。
- **图片开屏（`splashStyle==0`）期间绝对不能是全屏窗口**（2026-09-13 修）：透明底靠
  `SDL_GL_ALPHA_SIZE=8` + `glClearColor(0,0,0,0)` + `DwmExtendFrameIntoClientArea(-1,-1,-1,-1)`
  （SDL2 没有 `SDL_WINDOW_TRANSPARENT`，那是 SDL3）。但**覆盖整个桌面的窗口会被 Windows 的
  fullscreen optimizations 接管、DWM 合成被绕过 → 逐像素透明失效 → 透明底变成不透明黑**
  （症状："黑底 + 一张启动图"）。所以 `windowMode==2 && splashStyle==0` 时**创建窗口时不要进全屏**，
  等加载完（boot 末尾 `splashShown` 那块）再 `SDL_SetWindowFullscreen`；而且开屏窗口尺寸要限制在
  `SDL_GetDisplayUsableBounds - 16px` 内（万一存档分辨率正好等于显示器尺寸，也会被 FSO 抓走）——
  开屏窗口除了居中那张图整体都透明，缩几像素肉眼看不出来。经典开屏（深色底）保持创建即全屏。
  全屏切换靠 `SDL_WINDOWEVENT_SIZE_CHANGED` 把 `windowW/H` + `renderer.resize` +
  `core_api::resize` 一起更新，所以延后切换不会留下错尺寸的视口。
- HUD 预缩图：`loadHud()` 优先读 `assets/mmw/overlay_opt/`（存在则用，否则回退原图，删掉该目录即恢复）。
  原图很多是超大的（life 数字 1000x1333，实际只画 ~50px），解码很慢。用
  `zig c++ -O2 -Ithird_party -Ithird_party/mmw_preview/vendor .workbuddy/tools/shrink_hud.cpp -o build/shrink_hud.exe`
  编译后跑 `build/shrink_hud.exe assets/mmw/overlay assets/mmw/overlay_opt 512 start_grad.png`
  生成（整数倍 alpha 加权 box 缩小到 max dim 512；start_grad 是 1:1 全屏渐变，跳过）。
  这套把 HUD 加载从 ~1.0s 降到 ~0.3s。build.sh 会连 assets 一起拷到 build/。
- 判定窗口默认 perfect 40ms / great 90ms / good 140ms（非官方数值，做成可调的）。
- **trace（kind 3，绿色竹节/滑条）按"覆盖"判定，不是按"点"**（2026-09-12 修）：
  官方规则是手指按住那条轨道就判 PERFECT、没有尾判、头也不用重新点，所以
  `JudgementEngine::update()` 里 kind 3 的分支会用 `mHoldLanes` 做覆盖检测（和"无 marker 的
  guide tick"同一套），命中即 PERFECT；准点按下仍然走 `findCandidate()` 拿分级判定。
  **修之前只有"按一下"能清 trace**，于是「tap 打头 + 4 个 trace 组成竹节」的谱（例如
  0628 HARD 副歌）按住不放就会全 MISS —— 这是用户报的"竹节音符总是 miss"的真凶。
  同一个谱面里 guide hold 本身**不发任何判定事件**（无头无尾无 tick，只有一个 markerless
  mid tick），tap 头是独立的一条 1x 音符。
- 自动演示必须零 miss：flick 尾（kind 2 的 hold 尾）在 `mActiveHolds` 里本来是故意留白、
  等玩家滑动来清的，可在 `--auto` 下没人滑 → 每根都会 MISS（0628 HARD 有 13 根，
  直接把血打空）。所以那条分支里 `if (mAutoPlay) judgeHoldTail(hold, Perfect)` 兜一下。
- **flick 严格方向校验已实现并默认开启**（`JudgementEngine::mStrictFlick = true`，设置面板里
  是「严格 Flick 方向」复选框）。规则：严格模式下**点按永远清不掉 flick，滑动也永远清不掉 tap**；
  up/default（以及 SUS 没给方向的 legacy `FlickNone`）接受「上滑」或「无方向」，left/right
  必须给对对应方向（`findCandidate` 里比对 `noteFlickDir(note)` 与手势方向）。
  flick 方向来自 SUS 的 `#xxx15` 通道（`type` 1=Default/上、3=Left、4=Right），核心把它写进
  HitEvent 的 `flags` bits 1-2，`flags` bit0 才是 critical。
  注意：**键盘没有滑动方向**（`main.cpp` 的按键路径只发 `FlickUp` 兜底），所以 left/right flick
  只能用鼠标拖拽或触摸打；关掉严格模式会退回「任何手势都能清任何东西」的骨架行为。
- 判定特效**不在 HUD 里画**：命中时由 `core_api::triggerNoteEffect()` 交给谱面核心自己的
  粒子系统（`assets/mmw/effect.png` + `generated_resources.h` 里的 pjsk 特效定义）播放，
  和自动播放走的是同一条时间线，这是原作 1:1。`main.cpp` 里 `setEffectAutoplay(autoPlay)`：
  自动播放用谱面时间线触发，玩家模式改由判定引擎触发。判定文字（PERFECT/GREAT/…）在
  `game/Hud.cpp`，公式逐行对齐上游 `mmw_overlay_player.cpp`（310x81 基准、中心 960,667.5、
  前 2 帧不可见、第 2~5 帧四次方缓出到 scale 1、0.24s 窗口）。
- 难度定数：unipjsk 导出的 SUS 把 `#TITLE`/`#PLAYLEVEL` 清空了（`#DIFFICULTY 0`），所以定数
  来自仓库根的 `music-levels.json`（`{"<musicId>":[easy,normal,hard,expert,master]}`，
  `setup.sh` 可从官方 `musicDifficulties` 表重建）。曲目 id 从文件名取（`0075_master.sus` → 75），
  `game/SongSelect.cpp` 按 id 分组，所以同一首歌的不同难度会并成一条、缺 sidecar 也不会散开。
  谱面同级可放 `<musicId>.json`（如 `charts/0075.json`）作为全难度共用的元数据；`<难度>.json`
  优先于它。
- 选曲界面（`game/SongSelect.cpp`）：列表行没有底色，只用一条半透明白线分隔；选中项是
  半透明白圆角矩形。行首定数指示（圆/「歌曲等级」标签 + 数字）的颜色跟当前选中的难度走
  （`kDiffColors[diffIndex]`），不是固定粉色。五个难度格子是 `assets/select/indicate_back_new.png`
  并排（原图不染色，没有的难度调 84 透明度压暗），再按该难度唱片记录把 `clear_indicate.png`
  （金）/ `fullcombo_indicate.png`（粉）盖在同一个矩形上——三张图都是 38x38，直接同尺寸叠加。
- 右侧手机整体倾斜：先按正放坐标画完整块（手机框、封面、文字、难度圆、按钮），再用
  `dl->VtxBuffer` 把这一段的顶点统一绕手机中心旋转 -5°。命中框不能旋转，所以难度圆 /
  图标按钮 / 确定按钮的 `InvisibleButton` 用 `tiltedItemPos()` 放到旋转后的中心；
  「确定」原来用 `ui::capsuleButton`，为了跟着倾斜改成手绘圆角矩形（同色 `ui::kPrimary`）。
- HUD 分数与血量是真的：分数 = 上游 overlay 的公式（`(kTeamPower / Σ权重) * 4 * 权重 *
  levelFactor * comboFactor`，权重表见 `JudgementEngine::hudWeight`，levelFactor 用该谱面难度定数，
  combo 每 100 连击 +1%，上限 1.1），再乘判定系数（Perfect 1.0 / Great 0.7 / Good 0.5，MISS 不加分
  且把 comboFactor 打回 1.0）。左上角还画段位字母和分数条（`game::scoreRankAndBar()`，阈值随定数走）。
  血量 1000 起，MISS -80、长条中途断 -40，HUD 血量 = `judgement.lifeRatio()`。
  分数前言零用 `score/digit/n.png`（它本身就是个浅色 0，8 位补足是上游行为）。
- UI 缩放：`ui::scale()` 以 860p 为基准（720p 窗口下 ≈0.84）；titlebar 高 `kHeaderH=44` 设计像素。
  设置卡片 400x500、暂停弹窗 600x250（设计像素）；滑块行高 80。改卡片尺寸时先量内容高度
  （临时 printf `GetCursorScreenPos().y` 对比 cardBottom），别让底部按钮压住内容。
- UI 组件坑：ImGui::Text 新行会把光标 x 归零（窗口 padding=0），绝对定位内容每行前要
  SetCursorScreenPos；卡片/组件内部不要用 Dummy 预留后重置光标到 (0,0)；零 item 的
  BeginGroup/EndGroup 会触发 ImGui 断言；`Ui.cpp` 的 `withAlpha(col, a)` 是**缩放** col 自身的
  alpha（曾经是"替换"，把 kBackdrop 的 84 变成 255，暂停遮罩变成全黑——改语义时留意）。
- 设置卡片 360x640、四个页签（演奏 / 画面 / 判定 / 系统）；`--settings` + `--settings-tab <0-3>`
  无头打开（按键没法送进无头运行），配合 `--screenshot` 截图。
  **页签内容放在一个裁剪用的 `BeginChild` 里**：「画面」页比卡片高，多出来的行会钻到「关闭」
  按钮底下（按钮后提交，把点击全吃掉）。这个 child 的末尾**必须补一句 `ImGui::Dummy`**——
  `ui::checkBox()` 最后一条是裸的 `SetCursorScreenPos`，child 作为当帧最后一个窗口时
  `EndChild()` 会弹 "SetCursorPos ... to extend window/parent boundaries" 断言。
- 系统页签两项：`autoPauseOnBlur`（失焦自动暂停，关掉 = 切出去歌继续跑）、`reportSmtc`
  （是否汇报 SMTC）。关 SMTC 走 `systemMedia.setReporting(false)`，把媒体会话整个摘掉
  （`put_PlaybackStatus(Stopped)` + `put_IsEnabled(0)`），**不是**只停推送——否则系统浮层
  会一直挂着我们最后一首旧歌。启动时若已关闭，`init()` 之后立刻 `setReporting(false)`。
  任务栏进度条（`ITaskbarList3`）不归这个开关管，始终在跑。
- 游戏资源（assets/、charts/）来自公开渠道，仅限本地游玩，不要提交或分发。

## 与上游还没对齐的地方（2026-09-13 盘点）

对着上游 [sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web) 逐个查过的结论，按「值不值得做」排：

1. ~~**舞台背景生成**~~：**已做**（2026-09-13，2026-09-14 补齐两处）。`game/StageBackground.cpp`
   是上游 `src/lib/overlayBackgroundGen.ts` 的 C++ 移植：单应矩阵把曲绘投进舞台侧屏/中间屏
   **以及它们在下半部分屏里的暗倒影**（`kSideLeftMirror` / `kSideRightMirror` / `kCenterMirror`，
   对应上游 `MORPH_*_MIRROR`），再按 base/windows/bottom 顺序合成底板，最后由
   `toSquareBackground()` 铺成 2048x2048；由 `Renderer::setSongBackground()` 换掉默认背景贴图，
   开歌时按曲绘生成一次（约 1.1s）。
   - **铺方形那步不能省**（2026-09-14 修）：世界坐标的背景四边形在屏幕上是**正方形**
     （`worldBackgroundWidth/Height` 都是 backgroundSize 虚拟像素），默认板
     `background_overlay.png` 本来就是 2048x2048；bggen 板只有 2048x1168（1.75:1），
     直接当纹理上传会被纵向拉伸 1.75 倍——症状就是"打歌背景变形"。上游 native 的
     `composeOverlayBackgroundV3()` 末尾同样调 `renderToSquareBackground()`。
   - **mirror 那组不是"另一种布局"**（2026-09-14 修）：早先注释写"mirror 是镜像布局用的、
     我们没有"是误判。`center_mask` / `side_mask` 在 y≈680..1040 是有 alpha 的
     （191 / 128），也就是说这些倒影屏本来就会合上去，只是亮度低；不做的话画面下半部分
     那几块屏是空的（截图 diff 集中在 y≈660..1020）。
   - 想缩短生成时间的话，瓶颈是整块底板的 overlay 遍数（可按四边形包围盒裁）。
2. **长条 / guide 浓度没做成设置项**：上游有 `holdAlpha`（默认 1.0）/ `guideAlpha`（0.8），
   我们虽然把参数传了（guide 0.6）但没进设置面板。顺带一提上游后来把"长条只在头判激活后
   才显示"（`segmentActivated`）做进了核心，我们用的是另一套（`markNoteHit` /
   `setMissedHolds` / `setDimmedHolds`）。
3. **核心落后上游一点**：`core/native/src/mmw_preview.cpp` 与上游现版差 24 行上游独有内容
   （长条绘制重构、`hitEvents` 按 (time, center) `stable_sort`、trace 头显示修复）。同步会跟我们
   的 hit 驱动改造撞车，要动就单独开一轮。
4. **音效增益**：上游按 kind 分档（perfect/criticalTap .75、flickCritical .8、trace .82、
   tick .92、holdLoop .7），我们只按判定档位（Good/Bad 减半）。
5. 小事：上游有英文 AUTO 徽章 `autolive-en.png`（它自己也没用）；`noteSpeed` 默认它 10.5 我们 8.0；
   上游还支持 MMW 谱面 Maker 的 `custom_score_json`，我们只吃 SUS。

已经补齐的：HUD 的 `+N` 加分浮动、右下角 `AUTO LIVE` 徽章、判定文字用对应档位的精灵
（上游 native 那边永远画 PERFECT，因为它是预览器）。

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
```

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

## 选曲列表的滚动模型（2026-09-12 重写，动之前先读）

`game/SongSelect.cpp` 的列表**不是 ImGui 的滚动控件**，是自己写的状态机，因为官方 UI 的行为
（没有滚动条、滚不到底、滚动时不选中、停手后中间那首被选中）ImGui 给不了：

- 唯一的状态是 `scroll`：**位于列表视口垂直中线的那个内容坐标**。行位置 =
  `viewCenterY + (i * pitch - scroll)`，`pitch` 固定 `104*k`（**等间距**，选中卡片 128 高、
  普通行 84 高都画在自己的 slot 里，别再改回"选中行更高所以把后面的行往下推"——
  那样 `center(i)` 会随选中行变化，吸附目标会跳 22px）。
- 滚轮 / 拖动 / 惯性（`flingVel`，指数衰减）/ 吸附（`scrollTarget`，`1-exp(-16dt)` 逼近）
  全都只改 `scroll`；`scrolling=true` 期间 **`cardIndex = -1`，任何行都不高亮**，
  手机面板也保持旧曲目。停手 0.20s（或惯性衰减完）后把 `lround(scroll/pitch)` 那行提交为
  `groupIndex`（这才是真正的选中），再滑到正中间。选择只在"停手"时发生，这就是用户要的语义。
- 两端各允许 `0.9*pitch` 的橡皮筋过冲，回弹靠逼近 `scrollTarget` 完成（"滚不到底"）。
  → **2026-09-12 晚改成真循环**：列表是**闭环**的，滚过最后一行接的是第一行，没有端点也没有
  橡皮筋（用户说的"滚不到底"是这个意思）。实现上一个 `slot` 是无限行序列里的位置，取行内容
  一律 `rows[((slot % rowCount) + rowCount) % rowCount]`。因此**不要再给 `scroll` 加 clamp**，
  惯性只会因为指数衰减停下（`< 100px/s` 归零）。
- 行现在来自 `buildRows()`（`ListRow`：`header` 或 `song`）：排序 + 可选分组都在那里做，
  结果按 `listSignature`（搜索词|排序|分组|难度|谱面数）缓存，别每帧重建。
  分组开着时**顺序由分组决定**（`order` 会被覆盖），否则段标题会把同一组切成好几段。
- 循环 + 分组下"中间那行"可能是段标题，所以提交选中时要 `nearestSongSlot()` 跳过标题；
  `slotOfGroup()` 取离当前 `scroll` 最近的那一份拷贝（否则 shuffle 会绕整圈转）。
  高亮卡片只画 `cardSlot` 那一处，同一个组在屏幕上出现两次时别画两张卡。
- 鼠标和触摸走同一条路：SDL 的 touch→mouse 合成（`SDL_HINT_TOUCH_MOUSE_EVENTS=1`）会把单指
  拖拽变成 `ImGui` 眼中的鼠标拖动，所以只需要读 `io.MousePos` / `IsMouseDown`。
- **点击必须在松手时判定**，且位移 < 8px 才算点击（拖拽永远不选中）。不要再用
  `InvisibleButton` + `IsItemClicked`：那个在**按下**时就触发，拖拽起手会误选。
- 行的命中测试全部靠 `slotAtY(y)` 算，没有 ImGui item；列表用 `BeginChild` 只是为了拿裁剪矩形
  （`NoScrollbar | NoScrollWithMouse`）。
- 前导等级圆显示的是**当前选中难度**的定数（`levelForDifficulty()`）：该难度没有谱面文件时
  回落到官方 `music-levels.json` 表，所以切难度时整列数字会一起变，颜色也跟着变
  （`kDiffColors[diffIndex]`）。手机面板里未选中的难度是**空心圆**（无底色填充）。
- **手机面板的元数据块是左对齐的**（2026-09-14 按官方截图改）：曲名 / 歌手 / `Vo.` 三行
  统一从内容框左缘 `contentL = cx - panelW/2` 起排，和下面那排难度圆的左缘对齐
  （`addTextLeft()`；原来是 `addTextCentered()`）。右侧放**最好成绩的评级徽章**
  （`drawBestScoreBadge()`）：半透明白圆盘（实测原版约 32% 白）+ 游戏自带的
  `score/rank/chr/<x>.png` 字母（224x266，字形实心 185x242 在 +20+16，
  原版把字形画到徽章直径的 0.686）。直径 `64*k`（≈ 难度圆的 1.06 倍，量出来的），
  右缘贴内容框右缘、竖直居中于元数据块——原版就是压着歌手/`Vo.` 两行中间，
  而按整块居中算出来的落点和它只差几像素。**没打过**（`bestScore <= 0`）时画暗一档的圆盘 + `--`。
  徽章取的是**当前选中难度**那条谱面的最好成绩，`ChartEntry::bestScore` 由 `applyScores()`
  从 `userdata.json` 灌进来（`cleared`/`fullCombo` 之外新增的第三个字段）；
  等级用 `chartRatingFor()`，取值顺序**故意和 main.cpp 的 `setChartRating()` 一致**
  （官方等级表 → 谱面自带 level → 26），这样徽章和演奏中 HUD 的 SCORE RANK 永远一致。
  三行文字都用 `ellipsize()` 截到 `textMaxW = 内容框宽 - 徽章直径 - 12k`，绝不会钻到徽章底下。
- **刷新按钮**（2026-09-14）：列表头部、两个 combobox 右边那颗深色胶囊（环形箭头 + “刷新”，
  图标是 `dl` 手画的弧 + 三角箭头，不需要素材），点了返回 `SelectRescan`，和 F5 走同一条路。
  悬停有 tooltip 写着 F5。注意**重扫之后 main.cpp 会把选中项重置成第一首**（F5 一直是这行为），
  要改成保留当前曲目得动 `main.cpp` 那处 `selected = entries.empty() ? -1 : 0`。
- **排序 / 分组**（搜索框右边的两个 combobox）：排序有「按名称」「按难度」，分组有「关闭」
  「按难度段（1-5 / 6-10 / … / 36+）」「按读音（あ/か/さ…/A-Z 逐字母/#）」「按首字
  （A-Z / 0-9 / あ い う…，用 initialLabel()）」。
  名称排序和读音分组用的是**官方读音**（`musics.json` 的 `pronunciation`，main.cpp 里
  `loadMusicMaster()` 载入，`foldForSort()` 把片假名折成平假名、ASCII 转小写），
  所以「ウミユリ海底譚」落在 あ 行、片假名标题也能正确排序；官方 715 首的读音**全是假名**
  （`Tell Your World` = てるゆあわーるど → た 行），所以逐字母段只对**没有读音的自制谱**
  生效（那时退回用标题当 key）。汉字开头且没有读音的会落进「その他」（按字节序排在最后）。
  `kanaRowLabel()` 是「行」级的标签（あ か さ た な は ま や ら わ + 逐字母 + `#`），
  `initialLabel()` 是「首字」级的（逐假名 + 逐字母）。
  注意 `#TITLE` 里写的是难度名（有些 unipjsk 导出写 `#TITLE "master"`）时要当空处理，
  否则列表里会出现一堆叫 "master" 的歌。
- **曲名回填**：unipjsk 导出的 SUS `#TITLE` 是空的，旧的回落是 `prettyFileName()`，于是整张
  列表都叫「0018 master」。现在 `musics.json` 的 `title` 也一起进主表（`storeTitle()` /
  `titleFor()`），`scanChartFolder()` 与 `resolveSidecars()` 的回落顺序是
  **SUS `#TITLE` → sidecar `title` → 官方 `title` → 文件名**。顺带选曲搜索框也能搜到真曲名了。
  这两个值**存在 settings 里**（`sortMode` / `groupMode`），`drawSongSelect` 收 `int&`，
  调用方（main.cpp）发现变了就 `persistUserData()`；分组模式会覆盖排序（见上）。
- **段标题可点 → 索引面板**（2026-09-13 加）：分组开着时点段标题把列表换成字母面板
  （`indexOpen` / `indexAnim`），点字母用 `nearestSlotOfRow()` 飞过去（落点是"该段第一首
  居中、标题在上一行"），点空白处恢复列表；面板打开时 `listHovered` 要按 `!indexOpen` 屏蔽，
  否则背后列表会跟着动，列表行还要按顶点 alpha 淡出（抓 `listVtxFirst` 之后批量缩）。分组关掉时面板自动关。
  **面板是极简的（没有底板/标题/关闭叉）**，交互全手写命中测试——这里踩过三个坑，改之前先看：
  1) **别用整块 InvisibleButton 当背景**：它在 mouse-down 就抢走 active id，之后提交的字母
     永远收不到点击。字母的命中测试要学列表行那样直接比坐标。
  2) **格子矩形必须半开区间**（`>= min && < max`）：两端都 `<=` 时正好落在格子边界上的点击会
     被相邻两个字母同时命中，一次点击跳两段。
  3) **用"按下那一刻面板是否已打开"（`indexOpenAtPress`）门控**：一次按下+抬起可能落在同一帧，
     列表的 release-commit（点标题开面板）会和面板的 press 处理同时吃这一次点击。
- **失血阴影（暗角）**（`main.cpp`，`damageVignette`/`deadVignette`）：**四条边带要内缩 +
  四个角方块用四色插值**（外角两侧是暗色、内角 0）。只内缩边带、不补角，角上就只剩横向渐变、
  没有上下方向的压暗，看上去像"阴影没绕窗口四周"（实测角区系数 0.96 = 几乎没压暗，补角后 0.73）。
  每像素只允许一层，别让边带互相重叠（会叠加两倍黑）。
- **入场/过渡动画**（2026-09-13）：`enterAnim` 靠"本函数每帧都被调用，隔 >0.5s 才又调一次
  = 刚进来"判定（不用宿主通知），手机面板的入场就是挂在既有的"手机顶点整体旋转"那趟循环里
  做的（位移 + 顶点 alpha），所以别在那里加 `continue` 之类的短路。选中卡片的高度按槽位
  （`slotHeights`，索引是 `wrapSlot(slot)`）做指数趋近——`slotHeights` 必须在 `listSignature`
  变化时 `assign(rowCount, 0)`，否则越界。
- **选曲背景可以是桌面壁纸**（2026-09-13）：`bgStyle/bgBlur/bgDim` 三个设置项，壁纸路径见
  `windowsWallpaperPath()`（SPI_GETDESKWALLPAPER → 注册表 HKCU\Control Panel\Desktop\WallPaper
  → `%APPDATA%\Microsoft\Windows\Themes\TranscodedWallpaper`，都要 `GetFileAttributesW` 验存在，
  后两个兜 slideshow/Spotlight）。解码 + 降采样 + 三次 box 模糊都在
  `Renderer::loadBackdropTexture()`（1024 上限、滑动窗口 O(1)/像素），**只在设置开着时才加载**，
  纹理由 main.cpp 持有并通过 `game::setSelectBackdrop(tex,w,h,dim)` 交给 SongSelect 画
  （cover 铺满 + dim 黑罩）。模糊只在滑条松手时重算，拖动期间别重算（CPU pass）。


## 结算画面（2026-09-13，`game/Result.cpp`）

**几何来自像素测量，不要凭感觉改。** 参考图是 2388x1080 的手机截图（Project SEKAI 官方
结算画面），用 `.workbuddy/tools/` 里的 python 脚本逐区域扫出来的数值，全部换算到
1920x1080 虚拟画布：

| 元素 | 参考图（2388 宽） | 虚拟画布 |
|---|---|---|
| 顶部条面板 | x 413..1973，y −45..177，圆角 50，白色 59% | 左边 413，右边贴到 1905（画布右缘 −15） |
| 曲绘 | 453..588，21..154（135×135，粉 #FF4577，内缩 11） | 同左（绝对 x） |
| 标题 | x 615，cap 40..67 | 同左 |
| EXPERT 胶囊 | 615..806（宽 191） | 同左；右侧接深色胶囊 806..996（宽 190） |
| 得分数字 | 右对齐 1233，字高 86，步进 61.6 | 同左（精灵见下） |
| 得分/最高得分标签 | 中心 (546,355) / (537,456)，字号 70 / 36 | 同左 |
| 进度条 | x 1246..1777（宽 531），y 80..104（高 24） | 右端 = 面板右缘 −196 |
| C/B/A/S 刻度 | 条内 746/990/1234/1478（**精灵自身的 1650 宽设计空间**），pin：梯形 63→82 + 竖线到 105 | 同比例 |
| SCORERANK 牌 | x 1798..1918（宽 120），y −20..175（顶部被裁） | 右端 = 面板右缘 −55 |
| 判定行 | 牌 x 450（宽 372 高 55 圆角 18），首行中心 y 656，行距 64.6；标签 x 472，数字右对齐 785 | 同左 |
| COMBO | 标签中心 951，数字右对齐 1225 | 同左 |
| 继续按钮 | 320×79，mint #77EDDD，深色字 | 右端 = 面板右缘，底 1039 |

**能复用素材就别画**（用户提的，2026-09-13 改）：原素材都在 `assets/mmw/overlay/` 里，
用上之后连形状都不用猜：

| 元素 | 素材 | 用法 |
|---|---|---|
| 评级大字（S/A/B/C/D） | `score/rank/chr/<x>.png` | HUD 早就注册成 `rank_char_<x>`，结算牌直接画 |
| SCORERANK 字样 | `score/rank/txt/jp/<x>.png` | 新注册成 `rank_jp_<x>`（HUD 分数面板用的 en 版不动） |
| PERFECT～MISS | `judge/v3/<1..5>.png` | 就是判定文字那 5 张，PERFECT 自带彩虹渐变，颜色/字形全对 |
| 结算 BGM | `assets/ost/BGM_LIVE_RESULT_2.mp3` | `AudioEngine::startResultBgm()` 流式循环 |

**这些精灵自带一圈发光边**，文件尺寸比实际字形大：所以 `drawSpriteInk()` 是**按 ink 框
定位**的（`drawSpriteInkCentered` 同理）。每个 sprite 的 ink 框都是量出来的，写死在
`judgeSprite[]` / 调用处：
`judge/v3/1..5` = ink 起点 +16～17、高 47～50；`rank/chr/a` = 224×266 里 solid 219×256
（+3+2）；`rank/txt/jp/a` = 1000×130 里 solid 952×114（+26+8）。
量法：`magick <png> -channel A -threshold 55% +channel -trim -format "%wx%h+%X+%Y" info:`

**排印上的几个坑：**

1. **字号必须乘 `c.scale`**。ImGui 的 `AddText(font, size, ...)` 的 size 是**像素**，不是
   虚拟单位。1920x1080 时 scale=1 看不出问题，1366x768 下所有文字会大 1.4 倍、评级字母
   直接冲出牌子——踩过一次，`game/Result.cpp` 里所有文字助手都在内部乘了 scale。
2. **只有 8 位总得分用记分精灵**（`score/digit/<d>.png`，并横向压 18%：`kDigitSqueeze`）。
   判定行计数和 COMBO 都是**普通字体**（原版就是 UI 字，只是大小不同），用
   `drawFontDigits()` 画，而且要走**窄体** `condensedFont()`（数字的宽高比 0.59 才对得上
   雅黑 Bold 的 0.62 偏宽）；文字标签才用 `boldFont()`（雅黑的字宽/字高 0.83 才和原版一致，
   Arial Narrow Bold 的 0.65 太窄）。字宽差一点时用 `textTracked()` 的 tracking 补。
3. RESULT 水印是**空心描边**（白 22% 描边 3px + 内部填背景色），不是实心灰字：
   `textOutlined()` 就是干这个的。

**「继续」按钮走事件层命中测试**（`resultContinueHitTest()`，和 HUD 暂停按钮、开场跳过同一套）：
结算画面不是 ImGui 窗口，触摸事件不带可用鼠标坐标，所以鼠标分支和 `SDL_FINGERDOWN`
分支各测一次（又是"触摸屏点不到"那个经典坑）。`drawResult()` 只负责画 + 悬停高亮。

**流程**：曲末（`trackDurationSec − 0.15`）切进 `AppState::Result`，停音乐、清触点，用
`judgement.stats()` 组 `ResultData`；成绩记录就在这个切换块里写盘（**不要**提前到
`trackDuration − 0.25` 那种更早的位置：曲子尾部还剩没判完的 note，自动 MISS 的窗口会
越过谱面末尾，那时读到的血量偏高，摔死的曲子会被记成 CLEAR）。`resultPreviousBest`
（= `ScoreRecord::bestScore`，新加的字段）必须在 merge **之前**取，否则永远不是新纪录。
- **CLEAR 的判定 = 打完时血量 > 0**（`st.life > 0`）：血量归零 = 失败，只留最高分、不打
  CLEAR。FULL COMBO 还要额外要求 `cleared`——长条中途断扣血但不计 MISS，`miss == 0` 也可能
  掉到 0 血。`--result-at`（调试，中途切结算）不写成绩。
点「继续」（或 ESC）回选曲。调试：`--result-preview`（启动即进，用参考图的样例数字）、
`--result-at <sec>`（跑到指定秒数切）。

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
  踩过两次：HUD 暂停按钮（已修）、开场卡片右下角的「跳过 >>」按钮（2026-09-12 修）。
  新增可点元素时，要么放进 ImGui（合成鼠标事件能到 ImGui），要么在 `SDL_FINGERDOWN` 里补一份
  同样的 hit-test（`game::introSkipHitTest` / `isPauseButton` 就是这个模式）。

## 系统要求

- Windows 7 SP1 及以上（miniaudio / SDL2 兼容底线）。OpenGL 3.3 core（约 2008 年后的 GPU 均可）。
- SMTC（媒体浮层/任务栏媒体控件）与任务栏进度条：SMTC 走 `RoGetActivationFactory`，**实际只在
  Windows 10+ 生效**（Win7/8 上 combase 的激活会失败，代码里已容错，只是不显示）；任务栏进度条
  ITaskbarList3 在 Win7+ 均可用。
- 不依赖任何运行库安装（zig c++ 静态链接 CRT + 自带 SDL2.dll）。
- **玩家数据只有一个文件 `userdata.json`**（`game::userDataPath()`）：优先放
  `<exe>\..\userdata.json`——也就是有 `charts\` 的那一层（build/ 布局下 = 仓库根），
  这样 `rm -rf build` 不会丢、换机器把这份文件拷到 `charts/` 旁边成绩就回来了；
  没找到 `charts\` 才落到 `<exe>\userdata.json`。内容 `{settings, scores}`：
  settings 是 noteSpeed/seVolume/offsetSec/leadInSec/windowMode/fpsLimit/判定三窗/strictFlick，
  scores 按**谱面文件名**做 key（与绝对路径无关，所以重下同样的谱成绩能对上）。
  命令行参数 > userdata.json > 内置默认（`*Given` 标志记录哪些来自命令行）。
  **它被 .gitignore 忽略**（个人成绩，不是源码）。`--screenshot` 模式不会写这个文件。

## 待办（按优先级）

1. hold 音效循环（SeHoldLoop 未接）与 SE kind 区分（当前键盘全播一个音）
2. 输入/音频延迟校准界面
3. 连击特效（judge v3 的 1~5 已用于判定文字，6=AUTO 仍未用）
4. 键盘 12 键布局可能不顺手，考虑做成可配置；键盘也打不了 left/right flick（只能发 FlickUp），
   要么给按键加"按住+方向键"的组合，要么引导玩家用鼠标/触摸
5. 【暂缓·长期，想清楚再做】歌手 / 音源版本选择。同一首歌的 `SEKAI ver.` / `VIRTUAL SINGER ver.` /
   `アナザーボーカル` **共用同一份谱面**，差别只在音源（以及 Vo. 署名、可能的头部静音）——
   所以**绝不复制 SUS**，只把「音源」做成可选列表：
   - `ChartEntry.bgmPath` → `std::vector<AudioVariant>{ label, path, vocal, fillerSec }` + `audioIndex`
   - 版本列表优先读 `<id4>.json` 的 `"audio": [...]`（对齐官方 `musicVocals` 表：每版本自带
     label + vocal，甚至各自的 fillerSec）；没写就扫文件名 `<id4>__<tag>.<ext>` 兜底（**双下划线**，
     跟难度用的单下划线 `_master` 区分），label 用 tag 美化；一个版本都没有时行为同现在（不显示选择器）
   - UI：右侧手机面板在难度条下方加 `‹ label ›` 左右切换器，仅当版本数 > 1 时绘制
   - 播放：`main.cpp` 用选中版本的 `path` / `fillerSec` 喂 `loadMusic` 和 `audioStartSec`，
     intro 卡的 Vo. 行也要跟着版本走（现在取的是 `entry.vocal`）
   - 动到的文件：`SongSelect.hpp` / `SongSelect.cpp`（scan + resolveSidecars + 选择器）/ `main.cpp` /
     `CHARTS.md`（sidecar 字段表补 `audio`）

> 已实现的旧待办：flick 严格方向校验（见「约定与坑」里的说明）、hold 尾判（松手判定）、
> HUD 真实分数与血量、放弃后重选曲卡死、**结算画面**（见「结算画面」一节）。