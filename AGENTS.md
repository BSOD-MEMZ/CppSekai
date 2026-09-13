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
                  # cardTitle、checkBox、stepper、messageDialog（-3=动画中 -2=关闭完成）
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
- 游戏资源（assets/、charts/）来自公开渠道，仅限本地游玩，不要提交或分发。

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
- **排序 / 分组**（搜索框右边的两个 combobox）：排序有「按名称」「按难度」，分组有「关闭」
  「按难度段（1-5 / 6-10 / … / 36+）」「按标题（あ/か/さ…/A-Z 0-9/その他）」。
  名称排序和标题分组用的是**官方读音**（`musics.json` 的 `pronunciation`，main.cpp 里
  `loadMusicPronunciations()` 载入，`foldForSort()` 把片假名折成平假名、ASCII 转小写），
  所以「ウミユリ海底譚」落在 あ 行、片假名标题也能正确排序；没有读音的（自制谱）退回用标题
  本身当 key（汉字会被排到所有假名之后 → 落进「その他」）。
  注意 `#TITLE` 里写的是难度名（有些 unipjsk 导出写 `#TITLE "master"`）时要当空处理，
  否则列表里会出现一堆叫 "master" 的歌。

## 平台 / 输入相关的坑（2026-09-12）

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
3. 结算画面、连击特效（judge v3 的 1~5 已用于判定文字，6=AUTO 仍未用）
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
> HUD 真实分数与血量、放弃后重选曲卡死。