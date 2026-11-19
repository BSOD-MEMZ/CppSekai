# 约定与坑

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

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
- **两个全屏转场（都在 `main.cpp` 末尾，画在 foreground draw list 上）**：
  - **确定 → 白光**：点「确定」**不立刻** `startSession`，而是先起白光（
    `confirmFlashOrigin` 由 `drawSongSelect` 的 `confirmCenter` 出参给出，是**倾斜后**的
    按钮中心），0.28s 扩散 + 0.16s 全白 + 0.55s 褪去。`startSession` 在白光铺满那一刻才调，
    于是**加载谱面 / 音频 / 生成舞台底板（实测 ~1.0s）全部发生在白屏后面**。时间轴用
    `min(frameDelta, 0.05)` 累加，否则卡顿那一帧的 delta 会把褪色直接跳过去。
  - **曲末 → 渐暗**：`songEndBlackout` 在 `songTime` 进入 `effectiveEnd - 1.2s` 时升到 1
    （`effectiveEnd` 就是结算触发用的那个时刻，`--result-at` 调试覆盖也算），结算画面里再用
    0.6s 把它降回 0。实测：t=7.4s 亮度 33.8 → 7.95s 2.9（纯黑）→ 8.6s 80.4（结算页）。
- **初始血量**（`UserSettings::initialLife`，100..1000）：`JudgementEngine::setInitialLife()` 存一份，
  **`reset()` 和 `load()` 两处都要种一次** —— 两者都会 `mStats = JudgementStats{}`，而一次开局
  reset() 之后紧跟 load()，只改 reset() 的话会被 load() 覆盖回 1000（这个坑实测过一次）。
- **env 诊断**：`CPSEKAI_UI_TRACE=1` 打 `[ui] tab N view=… used=… scrollMax=…`（设置页签高度，
  用来判断"内容挤不下"是没撑开还是被裁掉）和 `[ui] confirm button at x,y`（无头点确定用的坐标，
  1920x1080 下是 1623,783）。注意 **`--screenshot` 会把 stdout 重定向到 `cppsekai.log`**，
  所以这类日志要从日志文件里读。
- **`cppsekai.log` 只能被 open 一次（2026-10-10 修）**：双击运行（无控制台）和 `--screenshot`
  两条路都要把 stdout/stderr 送进日志文件，而**两次 `freopen(..., "w")` 是错的** ——
  两个 FILE 各有自己的文件位置，于是按字节互相覆盖，日志读起来像被谁啃过。用户报上来的那份里
  `[dpi] scaling on (per-monitor v2, SDL` 整段没了、只剩 ` uses DPI scaled points)`，
  就是一条更长的 stderr 警告从偏移 0 写在了它上面；`--screenshot` 那条路还会再 `freopen("w")`
  一次，把前面已经写好的行整段截掉。
  现在统一走 `redirectStdioToLog()`：**只 open 一次**，再用
  `_dup2(_fileno(stdout), _fileno(stderr))` 让 stderr 共用 stdout 的文件描述符
  （同一个位置、按打印顺序落盘），并用 `gStdioRedirected` 拦住第二条路重复打开。
  **别退回两个 freopen，也别在别处再 open 这个文件。**
- **设置卡片的内容是可滚动的**（画面页实测 `used=866 > view=642`）：child 里去掉了
  `ImGuiWindowFlags_NoScrollbar`，首行用 `SetCursorPos`（**窗口内坐标**）而不是
  `SetCursorScreenPos`，否则滚动时第一行会被钉住不动。
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
  - ⚠ 它打印的是 **metadata 原始声明列表，可能含重复行**：`Windows.Storage.Streams.IDataWriter`
    会多出一行 `WriteBuffer`（27 行 / 26 个真实方法）。照抄就会让该行之后**所有槽位后移一位**
    （`StoreAsync` 落到 `FlushAsync` 上 —— 照样返回一个状态正常的 async op，看起来没事 ——
    而 `DetachStream` 落到 vtable 之外，调用即段错误、无栈可查）。用前先对着官方文档数一遍
    方法个数（属性 get/set 分开算）。
  - `winmd_guid.py` 取 IID，短名也认（它内部是 "命名空间.类型" 的表，早先只支持全名，
    查短名一律 NOT FOUND）。**纯 COM 接口（`IBufferByteAccess` 这种）winmd 里根本没有**，
    只能查官方头文件/文档。
- **SMTC 封面（put_Thumbnail）只能用内存流，三条路里只有一条通**（2026-09-23 重写）：
  - `StorageFile.GetFileFromPathAsync`：async 工厂，本线程是 STA，完成回调投递到 apartment
    队列，实测轮询 `IAsyncInfo::get_Status`（带不带消息泵都一样）永远停在 Started。
  - `RandomAccessStreamReference::CreateFromUri`：**文档只接受 ms-appx / ms-appdata / http /
    https，file:// 不在其中**。偏偏它对 file:// 也返回 S_OK（它压根不碰文件），于是
    `put_Thumbnail hr=0x00000000` 一直是**假的成功** —— 媒体浮层从头到尾空白，而日志一切正常。
    这就是"改了还是没封面"的原因。旧的 `fileUriFromPath` + `IUriRuntimeClassFactory` 那套已删。
  - **实际可用的**：`InMemoryRandomAccessStream`（`RoActivateInstance`，再 QI 到
    `IRandomAccessStream`）→ `Buffer`（`IBufferFactory::Create`，IID {71AF914D-C10F-484B-BC50-14BC623B3A27}）
    → 字节用 `IBufferByteAccess` 的 `Buffer()` 取裸指针 memcpy 进去（**纯 COM 接口，不在 winmd 里**，
    IID **905A0FEF**-BC53-11DF-8C49-001E4FC686DA，注意和 IBuffer 的 905A0FE0 只差一位）
    → `GetOutputStreamAt(0)` + `IOutputStream::WriteAsync` → `Seek(0)` 校验 `get_Size()`
    → `CreateFromStream`。Chromium 用的也是这套。
  - ⚠ `WriteAsync` 返回的 `IAsyncOperation` **永远停在 Started**（消息泵、
    `CoWaitForMultipleHandles` 都推不动，status 读出来就是 0），而且**碰不得**：`Cancel()` 和
    `Release()` 各崩一次段错误。但数据是**同步写进流的** —— 拿流自己的 `get_Size()` 判成败就行，
    op 指针故意丢掉不管（每次换歌一个对象，几 KB，不折腾）。
  - ⚠ 别想着用 `DataWriter` 绕（`WriteBytes` + `StoreAsync`）：它的 `StoreAsync` 确实会完成，
    但 `DataWriter` **持有流**、`Release` 会把流关掉，而 `DetachStream` 拿回来的是一个"归属引用"，
    两种收尾都实测出过 stream 被提前销毁的崩溃（`vt<...>(stream)` 读 vtable 直接段错误）。
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
- 判定窗口的三档预设（`main.cpp` 的 `kJudgePresets`，**2026-11-01 已按官方表重算**）：

  | 档 | perfect | great | good | bad | miss |
  |---|---|---|---|---|---|
  | 宽松（出厂） | 72 | 113 | 138 | 155 | 155 |
  | 标准（官方表） | 42 | 83 | 108 | 125 | 125 |
  | 严格 | 12 | 53 | 78 | 95 | 95 |

  设置 → 判定 顶部一排 radio 就是这三档；拖任何一根滑杆都会把选中态退到"没选中"（-1）。
  **改预设表时三档必须满足 perfect<great<good<bad 且差值 ≥10**（滑杆下方那段
  `std::max` 阶梯会硬拉，拉完就和预设对不上了）。
  老存档里正好是旧默认值 `40/90/140/200` 的，`loadUserData` 时自动迁移到 `42/83/108/125`
  （逐音符类型的完整窗口表见 [`JUDGE-WINDOWS.md`](../JUDGE-WINDOWS.md)）。
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
  `setup.sh` 可从官方 `musicDifficulties` 表重建，**平时重跑 `.workbuddy/tools/update_music_db.py`
  就一起刷**——国服独占曲的定数只有国服的 master DB 有）。曲目 id 从文件名取（`0075_master.sus` → 75），
  `game/SongSelect.cpp` 按 id 分组，所以同一首歌的不同难度会并成一条、缺 sidecar 也不会散开。
  谱面同级可放 `<musicId>.json`（如 `charts/0075.json`）作为全难度共用的元数据；`<难度>.json`
  优先于它。**表里没有这首歌只是"定数显示 `-`"，不是"没有这个难度"** —— 表旧了别慌，
  游戏这边只会少显示数字。
- 选曲界面（`game/SongSelect.cpp`）：列表行没有底色，只用一条半透明白线分隔；选中项是
  半透明白圆角矩形。行首定数指示（圆/「歌曲等级」标签 + 数字）的颜色跟当前选中的难度走
  （`kDiffColors[diffIndex]`），不是固定粉色。五个难度格子是 `assets/select/indicate_back_new.png`
  并排（原图不染色，没有的难度调 84 透明度压暗），再按该难度唱片记录把 `clear_indicate.png`
  （金）/ `fullcombo_indicate.png`（粉）盖在同一个矩形上——三张图都是 38x38，直接同尺寸叠加。
  **尺寸对齐的权威依据是 [`SELECT-UI.md`](SELECT-UI.md)**（原作实测：行 912×144、等级球/曲绘
  各 96、5 个通关指示 38 方块横排）。**原作画布就是 1920×1080**（CanvasScaler 实测），
  别再乘 1.44 —— 早先那个 ×1.44 是拿 bg sprite 的贴图分辨率当画布了。
  我们目前整体约 **0.49~0.63×**（列表 450/912、行 84/144）；2026-11-01 做过的 1:1 版本
  **已按用户要求回滚**，数值表和"顶栏没地方放"这个坑都留在 SELECT-UI.md §6.5。
- **确定按钮的爆闪**（2026-11-01，`game/DecideBurst.*`）：原版
  `ScreenLayerFreeLiveCellPhone.PlayButtonEffect()` → `SoloLiveTransitionEffectContent`
  底下的三个 prefab（`fx_live_transition_v2_02` / `_botton_v2_01` / `_v2_01`），
  实测是「大光晕 `light` 1532×2357 + 圆环 `line_circle` + 只有三角带初速」。
  我们做成 0.9 s 的三层：两层 additive 光晕（宽 + 核）、两道扩散圆环、12 个
  从按钮边缘起飞并自转的三角（贴图复用 `tap_flare.png` / `tap_tri.png`，不增加包体）。
  **白屏（confirmFlash）要让它先亮 0.22 s**（`kDecideLead`）：两者同时开始的话
  0.28 s 白屏铺满，三角还没飞出去就被糊掉。无头验证用
  `CPSEKAI_DECIDE_ONLY=1 --confirm-flash 1.4 --screenshot-time 1.6`（这个开关
  关掉白屏，只放爆闪）。
- 右侧手机整体倾斜：先按正放坐标画完整块（手机框、封面、文字、难度圆、按钮），再用
  `dl->VtxBuffer` 把这一段的顶点统一绕手机中心旋转 **+5°**（2026-09-22 用户明确要"向右倾"，
  即顶边左高右低）。
  **原作不是这个方向**：`CellPhone.m_LocalEulerAnglesHint.z = 3`（Unity 逆时针 = 右端上翘），
  在 y 向下的绘制坐标里对应 `kTiltDeg = -3.0f`。2026-11-01 按原作改过一版，用户要求回滚，
  所以这里保持 +5° —— **别再"顺手改回原作"，那是他的偏好不是遗漏**。
  命中框不能旋转，所以难度圆 / 图标按钮 / 确定按钮的 `InvisibleButton` 用
  `tiltedItemPos()` 放到旋转后的中心。
  验证"倒向哪边"别靠肉眼：取手机框顶边在左右两个 x 上的 y
  （`.workbuddy/tools/pngcrop.py` + 扫列找框的浅紫 `6c6ca6`）。
- HUD 分数与血量是真的：分数 = 上游 overlay 的公式（`(kTeamPower / Σ权重) * 4 * 权重 *
  levelFactor * comboFactor`，权重表见 `JudgementEngine::hudWeight`，levelFactor 用该谱面难度定数，
  combo 每 100 连击 +1%，上限 1.1），再乘判定系数（Perfect 1.0 / Great 0.7 / Good 0.5，MISS 不加分
  且把 comboFactor 打回 1.0）。左上角还画段位字母和分数条（`game::scoreRankAndBar()`，阈值随定数走）。
  血量 1000 起，MISS -80、长条中途断 -40，HUD 血量 = `judgement.lifeRatio()`。
  分数前言零用 `score/digit/n.png`（它本身就是个浅色 0，8 位补足是上游行为）。
- UI 缩放：`ui::scale()` 以 860p 为基准（720p 窗口下 ≈0.84）；titlebar 高 `kHeaderH=44` 设计像素。
  2026-09-19 细节调整：页签高 50→**42**、页签圆角 14→**10**；`ui::stepper` 胶囊高 62→**50**。
  2026-09-24：判定页那两排预设从 pick-one `stepper` 换成 `ui::radioRow`（圆点 + 标签，
  一行平分 `rowWidth`，选中态是粉圈 + 粉点，走和 checkbox 同一套缓动 / 焦点环）；
  `ui::stepper` 的 pick-one 模式因此暂时没有调用者了（组件保留着）。都在 `game/Ui.cpp`
  顶部/各自函数里，改数值即可。
  设置卡片 400x500、暂停弹窗 600x250（设计像素）；滑块行高 80。改卡片尺寸时先量内容高度
  （临时 printf `GetCursorScreenPos().y` 对比 cardBottom），别让底部按钮压住内容。
- UI 组件坑：ImGui::Text 新行会把光标 x 归零（窗口 padding=0），绝对定位内容每行前要
  SetCursorScreenPos；卡片/组件内部不要用 Dummy 预留后重置光标到 (0,0)；零 item 的
  BeginGroup/EndGroup 会触发 ImGui 断言；`Ui.cpp` 的 `withAlpha(col, a)` 是**缩放** col 自身的
  alpha（曾经是"替换"，把 kBackdrop 的 84 变成 255，暂停遮罩变成全黑——改语义时留意）。
- **`AddRectFilled` 的圆角会被钳到"短边的一半"**（2026-09-19 修)：`playerLevelChip` / `expBar`
  的经验条 fill 是"窄矩形 + 圆角 = h/2"，而 ImGui 只给 `ImDrawFlags_RoundCornersAll` 的情况留
  `min(w,h)*0.5` 的钳制额度 —— 于是 fill 宽度小于胶囊半径时，它的左端比胶囊本身"方"，绿色从胶囊
  左帽的**两个角戳了出去**。修法：**fill 按完整胶囊画，再用 `PushClipRect` 裁到 ratio 宽度**
  （裁剪出来必定贴合胶囊；也不用再写"窄了会变气泡、宽了补方块"那两套分支）。
  同理，想让某个角保持大圆角而另一头是直角，用 `ImDrawFlags_RoundCornersLeft/Right`（那种 flag
  的钳制额度是 `min(w,h)*1.0`，不会被砍）。
- **卡片入场动画只做两件事，而且都在 `endCard()` 一处完成**（2026-09-19 修）：把这一段
  顶点**往卡片中心缩** `k`、**把 alpha 乘** `k`（`k = 0.34 + 0.66 * smoothstep(t)`，0.16s）。
  两个坑：
  - **任何画在卡片里的东西都不许自己乘 `k`**（原来卡体/遮罩/关闭 X 各自 `withAlpha(col, k)`，
    而标题、按钮、复选框没有——于是框淡入时内容是"啪"一下满不透明度闪现的）。全都交给那一趟顶点。
  - **子窗口有自己的 ImDrawList**，它的顶点不在卡片的 range 里 → 设置卡片的页签内容
    （`BeginChild`）原来既不缩也不淡。`BeginChild` 之后必须 `ui::cardSubList(ImGui::GetWindowDrawList())`
    把那个 list 交给卡片，`endCard()` 会用同一套参数处理它（注册是每帧一次，`beginCard` 会清）。
  想看动画中间某一帧：`CPSEKAI_CARD_T=<0..1>` 冻结（0.16s 的动画截图时机根本追不上）。
- **对话框的开关动画归调用方管，而且必须"画到 -2 为止"**（2026-09-19 修）：`messageDialog` /
  `eulaDialog` 返回 `-3`（正在关）/ `>= 0`（按了某个按钮，内部已 `requestClose`）/ `-2`（关闭动画
  结束）。**点完按钮当帧就把标志置 false 是错的**：关闭动画一帧都播不出来，而且卡片的动画状态被
  留在半路（`open=false` 但 `t≈1`），下次弹出就"全尺寸闪现 → 缩小 → 再放大"弹一下（多开确认框
  就是这么坏的）。正确写法（暂停框那份是模板）：

  ```cpp
  static bool alive = false;
  if (wantOpen) { alive = true; }
  if (alive) {
      const int action = ui::messageDialog(...);
      if (action == -2) { alive = false; }          // 动画结束才撤卡
      else if (action >= 0) { /* 记录选择，不撤卡 */ }
  }
  ```
  勾选框同理：「以后不再显示」在按下时就写档案，但卡片留到 `-2` 再撤。
- **组件侧对"调用方中途撤卡"免疫**：`messageDialog` / `eulaDialog` 开头的状态判断走
  `cardRaisedFresh(st)`（`CardState.lastFrame != GetFrameCount()-1` ⇒ 上一帧没画过 ⇒ 这次是重新
  弹出），命中就 `t=0 / open=true / soundOpen=false` 重新入场。没有它，任何"中途撤走再弹出"都会弹
  一下。**暂停框（`##pauseDialog`）还是老写法**（每个分支立刻 `pauseDialogAlive=false`）：它的绘制
  点在状态分支里，要改成画到 -2 得先把绘制点从状态分支里提出来，还没做。
- 无头看对话框：【`--settings --settings-tab <0-6>`】打开设置卡（页签 0 演奏 / 1 画面 / 2 外观 /
  3 判定 / 4 系统 / 5 账户 / 6 关于）；`CPSEKAI_MULTIASK=1` 启动即弹「开启多开？」确认框，
  `CPSEKAI_IMPORTASK=1` 弹「导入用户数据？」确认框，`CPSEKAI_KEYMAP=1` 弹「按键映射」卡片
  （点 combo、选文件、点编辑是唯一其它入口，
  而 PostMessage 假点击进不了 ImGui 按钮）。配合 `CPSEKAI_CARD_T` 抓入场中间帧。
  ⚠ **首启那张 ELUA 会盖住设置卡**（两张卡都是独立窗口，ELUA 后画、在**正中间**），
  要看设置卡下半页就把窗口开**宽**：`ui::scale()` 只看高度（`h/860`），所以
  `--width 2600 --height 900` 会让 ELUA 跑到屏幕中间偏右、设置卡留在左上角，两者不再重叠。
- 设置卡片 **920x780**（= 官方 1168x990 的同比例）、七个页签（演奏 / 画面 / 外观 / 判定 /
  系统 / 账户 / 关于）。2026-10-11 重排 + 改版：
  - 页内用 `ui::sectionLabel` 灰条分组，组内是 **两列网格**（`twoCols` / `colSet` 辅助）。
    卡片拉宽是为了「一行放更多」，**不是把单个控件拉成一长条** —— 官方就是网格。
  - **每组标题只占自己那一栏**（2026-11-01 七页统一）：`sectionLabel` 的第二个参数是
    行宽，**传列宽 `w`，别再传 `interior`** —— 通栏灰条在 920 宽的卡上会一路撑到 864px，
    视觉上把两栏又粘回一整行。一栏放一组（`twoCols` 左右各一个 sectionLabel），
    从上往下读就是一张网格；条件渲染的组（Aero 玻璃、背景模糊）留空即可，不要为了
    对称硬塞内容。**唯一例外是「关于」页的正文**，它本来就是阅读流。
  - 七页都不滚（2026-11-01 重排后最高的是演奏页 694，view 811；原来最高判定页 768）。
    加行前先量：`CPSEKAI_UI_TRACE=1 --settings --settings-tab N` 看 `[ui] tab N used=..`。
  - **标签页行就是标题栏**（没有「设置」标题）。`beginCard` 的 `tabBandH > 0` 会画那条
    灰带（`kTabIdle` = #bbbcd0）**并且画在关闭 X 之前** —— 让 tabBar 自己画会把 X 整个盖掉
    （那是「右上角关闭键被吃了」的原因）。选中页签是**内容色的块**（#ebecf1，和下面的
    正文连成一体，不是浮在白底上的白块），块顶距卡片顶约带高的 1/5；未选中页签是白字。
  - 12 个按键映射格子搬进独立的「按键映射」卡片（演奏页只留一行摘要 + 编辑入口，
    卡片由 `CPSEKAI_KEYMAP=1` 无头打开）。
  - **默认歌手**（2026-11-01）同样是一行摘要 + 独立卡片（`CPSEKAI_VOCALPREF=1` 无头打开，
    920x700、26 个歌手按组合六列排）。三档：跟随歌曲（セカイver，默认）/ 虚拟歌手 ver /
    指定歌手；存 `UserSettings::vocalPref` + `vocalPrefSinger`（**日文原名**，和
    music-vocals.json 的 singers 逐字比），选曲那边的 `defaultVocalIndex` 读全局
    `game::setVocalPreference()` 推过去的副本。只在"这首歌真有那个版本、且音频在本地"
    时生效，否则回落 —— 别把它做成"放不出声"。
  - 卡片带阴影（`shadowLayers`，spreadScale 3.0，比控件的 1.0 远得多）。
  - **卡片高度是"按内容反推"的，必须带死区**（2026-11-02 修「设置窗口也在颤」）：
    每页把 `used + 10s - view` 加到当前高度上（`tabCardH[tab]`，第一帧 780）。问题是
    `view` 是 ImGui 的**整数**高度，等式永远差 0.5px 左右 → 高度在 783.8 ↔ 784.75 之间
    以 60Hz 来回，底边和投影每秒抖 60 次。现在：`std::round()` 落到整像素 +
    **|差| < 1.5px 就不动**（`stored = cardH`），七页实测各自定住不动
    （784 / 804 / 699 / 707 / 652 / 765 / 521）。**别去掉这个死区**，否则极限环立刻回来。
    复查：临时在回写处打 `cardH/used/view`，同一个值应该连成一片。
  高度用 `CPSEKAI_UI_TRACE=1` 量（@1080p view = 796，最高的判定页 768，不滚）。
  `--settings` + `--settings-tab <0-6>` 无头打开（按键没法送进无头运行），配合 `--screenshot` 截图。
  **加页签要同步三处**：`kSettingsTabCount`（LB/RB 循环页签用它）、`ui::tabBar` 的标签数组、
  以及那条 `if (tab == N) ... else if ...` 链 —— **最后一个分支原本是裸 `else`**，
  往后面接新页签时得先把它改成 `else if (tab == N)`（2026-09-23 加「关于」时就这么踩了一下，
  编译报 `expected expression`）。
  各页现在的内容（2026-09-24）：演奏 = 音频偏移 / 音符速度 / BGM 音量 / 音效音量 / 手柄震动 /
  **震动（三个勾）/ 自动演出**（后两块从判定、画面搬过来）；判定 = 判定窗口预设 + 五根滑杆 +
  Bad与Miss同步 + 长条容错预设 + 松手/起按 + 严格Flick方向 / Flick视作Tap / 初始血量；
  系统 = 失焦暂停 / SMTC / 多开 + 多人演出 + **结束当前实例 / 结束所有实例**；
  账户 = 用户 / 新建 / 删除 / 昵称 / 学校 / 签名 / 等级 + **导出 / 导入用户数据**。
  **页签高度的余量很小**：演奏 515、账户 488（`view` 720p 下 519），加行先看一眼
  `CPSEKAI_UI_TRACE=1`。判定页是**故意**滚动的（635 > view）。
  **页签内容的可用宽度是卡片宽 - `padX`**（child 有卡片那么宽、文字从 `padX` 起排），
  超出的部分会被裁掉（「账户」页那两行「只存在本机 userdata…」一直都在被裁）。
  要换行的段落别只靠 `TextWrapped`（它按 child 的宽度换，也就是**卡片宽度**，会顶出卡片），
  用 `PushTextWrapPos(x + 330*s)` 显式钉住。
  **页签内容放在一个裁剪用的 `BeginChild` 里**：「画面」页比卡片高，多出来的行会钻到「关闭」
  按钮底下（按钮后提交，把点击全吃掉）。这个 child 的末尾**必须补一句 `ImGui::Dummy`**——
  `ui::checkBox()` 最后一条是裸的 `SetCursorScreenPos`，child 作为当帧最后一个窗口时
  `EndChild()` 会弹 "SetCursorPos ... to extend window/parent boundaries" 断言。
- **触摸屏滚这个 child**（2026-09-24）：ImGui 只在**滚轮事件**上滚动，触摸板一个都没有，
  于是带滚动条的页签以前只能用指尖拖那根 4px 滚动条。现在 SDL 的 `SDL_FINGER*` 路径
  （`state != Play` 时）把竖直位移攒进 `uiTouchScrollPx`，`##tabcontent` 里
  `ImGui::SetScrollY(GetScrollY() - px)` 落地。要点：
  - **手势方向只判一次**（超过 `12*scale()` 的 slop 后取 |dy|>|dx|），竖直就是翻页、
    横向留给滑杆 —— 否则在一个滑杆上竖划会被 `IsWindowHovered()` 的 active-item 检查吃掉。
    也正因为要配合它，hover 用的是 `ImGuiHoveredFlags_AllowWhenBlockedByActiveItem`。
  - `ui::scale()` 在 scale() **之后**拿到也行，slop 用 `12*max(1,scale)`。
  - `SDL_FINGERUP` **不要**清 `uiTouchScrollPx`：快甩的手势 down/move/up 会挤在同一帧，
    清了就等于整段抹掉（踩过）。只有下一次 `SDL_FINGERDOWN` 重置。
  - 无头复现：往队列里塞 `SDL_MOUSEMOTION(which=SDL_TOUCH_MOUSEID)` 把 ImGui 的指针挪到卡片上，
    再塞 DOWN + 若干 MOTION。**每个事件都要带 `windowID`**，否则 ImGui 后端
    （`ImGui_ImplSDL2_GetViewportForWindowID`）直接丢掉，鼠标位置不更新就永远 hover 不到。
    量"到底滚了多少"别数行，直接拿两张截图做配准（`.workbuddy/tools` 那套 PNG 工具 / PIL 都行）。
- **「关于」页（页签 5，2026-09-23）**：版本 / 许可 / 制作 / 上游 / 素材版权 五段 + 两个链接
  按钮 + 免责段落 + 「许可与免责声明」按钮（把首启那张 ELUA 再拉起来）。要点：
  - 链接按钮用 `openUrl()`（= `ShellExecuteW(nullptr, L"open", url, ...)`，和 chartdl 打开
    输出目录同一个调用；**shell32 早就在链接行上**，不用动 build.sh）。
  - **两个链接并排、不能上下排**：上下排会多出一行，`used` 超 `view` 约 25s，页面就出滚动条了。
    并排之后按钮变窄（`(interior - 8s)/2`），而 `capsuleButton` 是**居中且不截断**的
    ——标签必须短（「GitHub」而不是「GitHub 仓库」，后者会顶出胶囊）。
  - 免责段落压到 3 行（去掉和 ELUA 卡重复的"一切权利归权利人所有"），再加前面那几行
    正好卡在 `scrollMax=0`。**加行之前先量**：`CPSEKAI_UI_TRACE=1` 会打
    `[ui] tab 5 view=.. used=.. scrollMax=..`，`used > view` 就是必然出滚动条。
- **选曲头部行现在有两个按钮**（2026-09-19）：「刷新」（`refresh.png`，F5 同义）和它右边的
  「下载谱面」（`store.png`，返回既有的 `game::SelectDownload`，主循环已有处理；空列表那个按钮
  用的是同一个 action）。几何在 `game/SongSelect.cpp` 头部一起算（`rescanX/W`、`storeX/W`），
  **多人 banner 的右边界要用 `storeX + storeW`**，不然会长得盖不住新按钮。
- **选曲界面两个图标来自 `assets/select/`**（`selectTex()`：静态缓存 + 缺文件静默跳过）：
  `search.png` 画在搜索框**里面**的左侧（深色十字圆环，按框高 0.44 缩放）——InputText 的
  `FramePadding.x` 就是「给图标留出的位置」，输入框宽度是整条胶囊；旧写法把框缩短、
  图标画在框外，看起来是"图标浮在框右边"。`refresh.png` 是白色圆形箭头，替掉了手绘的弧+三角。
- **弹窗不再有半透明黑遮罩**（2026-09-19）：`beginCard` 不画 backdrop 了（`kBackdrop` 删掉），
  `dimBackdrop` 现在只表示"窗口铺满全屏"= 模态：挡住底下一切点击。设置卡片是非模态，
  窗口只包住卡片本身，所以演奏时 HUD/轨道照样可点。
- **字体只用系统字体**（2026-09-19）：`assets/mmw/font/` 整个删了（两个 Fontworks 商业
  FOT-Rodin + 16.9 MB 的 Noto），`--pjsk-font` 选项一并去掉，`loadIntroFonts()` 不再收参数。
  现在是「注册表读系统 UI 字体 → 探 CJK 字形 → 不行就按下面的候选表依次试」，结果页那条
  condensed 窄体另外从 `%WINDIR%\Fonts` 按文件名取 `ARIALNB.TTF`；全失败才落 ImGui 内置
  位图字（只有 ASCII，但至少不是"一个字都没有"）。
  **2026-09-19 晚：`msyhbd.ttc`（粗体 face）也删了** —— 它原来被加载了*两遍*（一次 ja
  ranges、一次 MergeMode 简中），各 16.1 MB，实测省下 **31.9 MB** 工作集（峰值 291.6→258.1、
  稳态 245.4→212.3）。`boldFont()` 现在返回 bodyFont。代价只是「得分 / 最高得分 / COMBO」
  笔画细一档：CJK 字宽两套完全一致（全角 1 em，得分 84 / 最高得分 168 都一样），latin 差
  3-7%，且每处都是居中或左对齐的固定位置，布局不动。
  **COPYRIGHT.md 里那条"商业字体嵌入分发"的风险至此关闭**——别再往仓库里放字体文件。
  **2026-10-25 同一段代码上又砍了两笔**（内存审计，`-- 18.6 MB` 稳态）：
  - `OversampleH/V` 原来在 3 处硬写 `2`。1.92 的 auto 规则是
    `raster_size > 36 ? 1 : 2`（`imgui_draw.cpp:3407`），本项目字号 **42px → auto 本来就选 1**；
    `ImGui.h:3658` 还注明 OversampleV 在 Y 轴上没用。硬写 2 让每个字形位图的面积变成
    必要的 **4 倍**（84×84 vs 42×42）。**默认 0 才对，别再写回去。**
  - 同一个 TTF 原来调了**两次** `AddFontFromFileTTF`（一次 Japanese ranges、一次 MergeMode
    简中）→ `msyh.ttc` 那 19.7 MB 常驻两份。**1.92 没法共享 buffer**：`AddFont` 里
    `FontDataOwnedByAtlas == false` 会先 `ImMemdup` 复制一份、再把所有权设回 true
    （`imgui_draw.cpp:3059`），传 false 只会多复制一次。正解是 `mergedBodyRanges()`
    把两份 ranges 并成一个数组、只调一次 AddFont（字形集是同一个并集）。
  - 实测：`[boot] fonts` 那一步 **100 MB → 32 MB**，稳态 282.6 → 264.0（差的那部分是
    启动期临时分配，本来 10~11 秒后就会被归还，别拿峰值当泄漏）。
- **字体候选表（2026-09-19 晚修 Win7 时扩的）**：`systemFontCandidates()` 现在三层，
  按顺序去重后逐个试：
  1. `SPI_GETNONCLIENTMETRICS` 的 `lfMessageFont`（跟随系统，首选）；
  2. 固定 face 名，**中英两套都列**：`Microsoft YaHei UI` / `Microsoft YaHei` / `微软雅黑`、
     `Yu Gothic UI` / `Yu Gothic` / `Meiryo UI` / `Meiryo` / `MS Gothic` / `MS UI Gothic`、
     `SimSun` / `宋体` / `SimHei` / `黑体` / `Microsoft JhengHei` / `Malgun Gothic` / Noto ×2；
  3. **按文件名兜底**（绕开注册表）：`msyh.ttc` `msyh.ttf` `meiryo.ttc` `msgothic.ttc`
     `YuGothM.ttc` `msjh.ttc` `malgun.ttf` `simhei.ttf` `simsun.ttc` `mingliu.ttc`
     `arialuni.ttf`（存在才进列表）。**这一层才是 Win7 能起来的保证**：Fonts 键的
     *值名* 随语言和系统版本变（Win7 根本没有 `Microsoft YaHei UI` 这条，英文版讯息字体是
     latin-only 的 Segoe UI），而 *文件名* 从 Vista 起没变过。
  还有两个 2026-09-19 晚修的坑，改这几行时别退回去：
  - `findFontFile()` 拿到的值可能是**完整路径**（`C:\Windows\Fonts\msyh.ttc`，Win7 上很常见）
    而不是裸文件名 —— 以前无脑拼 `\Fonts\` 前缀会拼出不存在的路径，然后被静默跳过。
  - 探测字形用的是**日文 + 简中混合集**（初 `U+521D` / ミ `U+30DF` / 詞 `U+8A5E` /
    设 `U+8BBE`）：日文字体（Meiryo / MS Gothic）**会**因为缺 `设` 被拒，中文装饰字体
    （方正/汉仪）会因缺 `ミ` 被拒 —— 这是故意的，歌名是日文、游戏自带 UI 文案是简中，
    一个文件得同时盖住两边。日志会写清缺哪个码位。
- **出 \"字体变点阵 + 中文变问号\" 先看 `cppsekai.log`**：那意味着 `loadIntroFonts()` 里
  一个候选都没过，UI 落到 ImGui 内置位图字（ProggyClean，CJK 全变 `?`）。启动时会打
  `[intro] N system font candidate(s)` + 每条的 `face -> path`，然后是每个候选的拒绝原因
  （`not readable` / `rejected by the rasterizer` / `lacks U+XXXX`），最后
  `system font <face> @42px loaded` 或 `no usable system font`。**这几行就是全部答案**。
  `CPSEKAI_FONT_FILE=<路径>` 可以强制指定一个字体文件（诊断，也是自动识别失败时的逃生口）。
- 系统页签两项：`autoPauseOnBlur`（失焦自动暂停，关掉 = 切出去歌继续跑）、`reportSmtc`
  （是否汇报 SMTC）。关 SMTC 走 `systemMedia.setReporting(false)`，把媒体会话整个摘掉
  （`put_PlaybackStatus(Stopped)` + `put_IsEnabled(0)`），**不是**只停推送——否则系统浮层
  会一直挂着我们最后一首旧歌。启动时若已关闭，`init()` 之后立刻 `setReporting(false)`。
  任务栏进度条（`ITaskbarList3`）不归这个开关管，始终在跑。
- **结束实例两个按钮**（系统页签，2026-09-24）：「结束当前实例」= `running = false`（走正常收尾，
  存盘照做）；「结束所有实例」二次确认（`killAllFrames`，~3s 内再按一下）后
  `postCloseToAllInstances()`：EnumWindows 找 `GW_OWNER == nullptr` 的窗口，
  **类名必须是 `SDL_app`**（SDL2 注册的类，靠它把"Notepad 打开了个叫 CppSekai 的文件"排掉）
  且标题正好 `CppSekai` 或前缀 `CppSekai - `（多人游玩会把用户名缀在后面），然后每窗一个
  `PostMessageW(WM_CLOSE)`。**没有 kill**：WM_CLOSE → SDL 的 `SDL_WINDOWEVENT_CLOSE` →
  main 里新加的那个分支置 `running = false`，于是每个实例都自己存盘退出。
  （实测：PostMessage 之后日志出现 `[instance] window close -> exiting`，profile 完整写回。）
- **窗口标题跟着当前用户走**（2026-09-24）：`syncWindowTitle()`（main.cpp，SDL_CreateWindow
  之后定义）在每个会改名字的地方调一次 —— `activateProfile()`（切档案）与账户页改昵称。
  标题 = `CppSekai`，或**房间标签存在时** `CppSekai - <玩家名>`（`--party-name` 优先，否则
  用昵称）。单人窗口保持光秃秃的 `CppSekai`：单实例那个"把已开的窗口叫到前面来"的查找是
  **按这个名字精确匹配**的（见实例策略那段）。以前只在启动时算一次，切了用户标题就停在旧名字。
- **开多人游玩时会问「立即重启？」**（2026-09-24，`drawRestartAskDialog` + `restartSelf`）：
  房间是**启动时**加入的（`party.init()` 在 main 开头），不重开窗口就只能一直是单人，
  所以勾上「多人演出」（或第一次确认多开那张卡里点的就是多人）之后弹一张 `ui::eulaDialog`，
  选「立即重启」就走 `restartSelf()`：`persistUserData()` → **先 `CloseHandle` 两个命名互斥体**
  → `GetModuleFileNameW` + `GetCommandLineW` + `CreateProcessW`（cwd 沿用当前目录）→
  `running = false` 走正常收尾。
  ⚠ **互斥体必须先放手**：新进程一启动就 `CreateMutexW` 抢同一个名字，我们还攥着的话它读到
  "已经有一个实例在跑"，于是把老窗口叫到前台然后自己退出 —— 重启完一个窗口都不剩。
  实测过：重启后子进程日志是 `[instance] single owner of profile 'X'`（拿到的是父进程那把锁），
  且只活一个进程、不再继续重启（无头复现的临时探针已删，见当日日志）。
- **自动演出给经验、不给成绩**（2026-09-24）：设置里的「自动演出」按官方 AUTO LIVE 处理 ——
  照算经验 / 场次 / 总分，但**不写 `cleared` / `fullCombo` / `bestScore`**（AUTO 是全程
  PERFECT，写进去等于把记录本作废），结算里也强制 `newRecord = false`。
  命令行的 `--auto` 连经验也不给（那是看谱面的预览，不该动档案）。
  读旧最高分用 `scores.find(key)` 而**不是 `scores[key]`** —— 后者会**凭空建一条空记录**
  （自动演出跑完会在档案里多出一个全 false 的 key）。
- **账户页导入 / 导出**（2026-09-24）：一个 profile 文件就是全部（settings + scores + 资料），
  所以"备份"和"搬家"是同一个操作。导出 = `persistUserData()`（先把活镜像刷进 userSettings）
  + `game::saveUserData(用户挑的路径, ...)`；导入 = `game::isUserDataFile()` 先筛
  （**在弹确认框之前**，免得对一份不是用户数据的 json 说"要覆盖你的成绩"）→ 确认后
  **`std::filesystem::copy_file` 覆盖 profile 文件本身**（不 re-serialize：这样将来新版本
  加的键也一起搬过来）→ 重新 `loadUserData` + `applyProfileLive()` + `++profileDataGeneration`
  （账户页那几个 InputText 缓冲要重读；判定页的工作副本靠 `settingsWasOpen = false` 重读）。
  文件选择器是 comdlg32 的 `GetOpenFileNameW` / `GetSaveFileNameW`（**build.sh 游戏链接行
  新加了 `-lcomdlg32`**）；`OFN_NOCHANGEDIR` 是必须的，否则挑完目录后进程 cwd 被改掉，
  `charts\`、日志、相对 `--sus` 全跟着错。
- 游戏资源（assets/、charts/）来自公开渠道。**按用户 2026-11-19 的口径：可以直接进 git，不必先问**；只有 `package.sh` 打 Release 包时不带 `assets/`/`Drafts/`/`docs/`。

