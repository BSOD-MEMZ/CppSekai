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
- 启动顺序（黑屏优化）：窗口 + GL 上下文就绪后先 `glClear` 换一帧，`Renderer::loadSplash()` 只加载
  background/stage 再画一帧（约 0.6s 出画面），之后才加载 HUD 贴图和 CJK 字体图集，整备完成约 1.3s。
  各阶段耗时用 `[boot]` 日志查看；贴图级耗时设 `CPSEKAI_ASSET_TIMING=1`。
- HUD 预缩图：`loadHud()` 优先读 `assets/mmw/overlay_opt/`（存在则用，否则回退原图，删掉该目录即恢复）。
  原图很多是超大的（life 数字 1000x1333，实际只画 ~50px），解码很慢。用
  `zig c++ -O2 -Ithird_party -Ithird_party/mmw_preview/vendor .workbuddy/tools/shrink_hud.cpp -o build/shrink_hud.exe`
  编译后跑 `build/shrink_hud.exe assets/mmw/overlay assets/mmw/overlay_opt 512 start_grad.png`
  生成（整数倍 alpha 加权 box 缩小到 max dim 512；start_grad 是 1:1 全屏渐变，跳过）。
  这套把 HUD 加载从 ~1.0s 降到 ~0.3s。build.sh 会连 assets 一起拷到 build/。
- 判定窗口默认 perfect 40ms / great 90ms / good 140ms（非官方数值，做成可调的）。
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