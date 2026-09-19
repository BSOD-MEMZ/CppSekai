# CppSekai — 项目长期记忆（精简版）

> **权威文档在仓库里，不在这个文件**：改代码/加功能前读 `AGENTS.md`（架构 + 全部坑），
> 命令行看 `CLI.md`，谱面数据看 `CHARTS.md`。这里只放跨会话必须记住的**索引与约定**，
> 详细技术笔记一律写进 `AGENTS.md`（那边随代码一起 commit）。历史细节翻
> `.workbuddy/memory/2026-09-*.md`（按日期，append-only）。

## 协作约定
- **改完 + 验证过就 commit**（用户明确要求），别攒着。
- **别用 `git checkout <file>` / `git restore` 撤临时改动**：2026-09-18 我为撤一个
  调试探针 checkout 了 main.cpp，把同一文件里**一大轮还没 commit 的需求改动全冲掉了**。
  要撤探针就用 Edit 精确回退，或者先 commit 再动。用户原话：「不要乱 checkout，
  有什么问题我们手动改」。
- 加新 .cpp 到 `game/` 或 `platform/` 时**必须同时加进 `build.sh` 的 SOURCES**，否则
  链接期才报 undefined symbol（2026-09-13 加 `game/Result.cpp` 时踩过）。
- 中文注释的脚本（build.sh 等）用 Git Bash 跑；涉及中文的 PowerShell 脚本用 pwsh。

## 构建 / 运行硬性坑
- `bash build.sh`（Git Bash）。zig **0.14.1**（`toolchain/`），**别换 0.16**（吞 `-I`）；
  zig 缓存必须放 C 盘（build.sh 已设 `ZIG_GLOBAL_CACHE_DIR`），D 盘文件系统不支持。
- `main.cpp` 必须在 `#include <SDL.h>` 前 `#define SDL_MAIN_HANDLED`，否则"秒退无输出"。
- exe 是 Windows 子系统；日志去 `cppsekai.log`（`--screenshot` 一定写文件）。
- `--screenshot` 给的目录**必须已存在**，否则进程静默挂住不退出。
- 无头自检：`--screenshot` + `--screenshot-time`，断言看日志 `[stats]`/`[score]`/`[result]` 行。

## 发布 / 打包
- `bash package.sh [版本]` → `dist/CppSekai-<日期>/` + zip（~2.7MB）。
  **默认带 assets**（2026-09-15 起，解压即玩，66MB / zip 51MB）；`--no-assets` 出旧的
  精简包（exe + SDL2.dll + icon.png + 三张官方事实数据表 + 文档 + LICENSE + setup.sh），
  用户侧 `bash setup.sh --assets-only` 拉素材 → chartdl.exe 下谱面。charts / toolchain 一律不发。
  详见 README「7.4 发布 / 打包」。
- 图标：`app.rc`（`zig rc` 编译资源，id 1）+ 运行时 `SDL_SetWindowIcon(icon.png)`；
  改 id 要同步改 chartdl 的 `LoadImageW(MAKEINTRESOURCE(1))`。

## 资源 / git 的坑
- `.gitignore` 忽略整个 `assets/`（还有 `charts/`、`build/`、`toolchain/`、`userdata.json`、
  2026-09-16 起还有 `profiles/`、`chartdl.json`）。
  `assets/mmw/**` 在仓库里只是"先 commit 后加规则"；**任何新加的 `assets/` 子目录都要
  `git add -f`**，否则换机器就丢（`assets/select/**` 就是这样丢过的）。
- **不要删 `.workbuddy/`**（项目记忆 + 工具）。工具：`.workbuddy/tools/`
  （`winmd_*.py` 查 WinRT 接口槽位、`pngcrop.py`、`shrink_hud.cpp`、`shot_probe.py`
  像素探针——对齐截图用，**带 Pillow 回退**所以不需要 ImageMagick）。
- `build/winsend.exe` / `build/winmsg.exe` 不在版本库里，要自己编（见 AGENTS.md 工具一节）。

## 素材现状（2026-09-19 大清理后）
- `assets/` **只剩 9.9 MB / 248 个文件**（清理前 56.1 MB）。清点工具
  `.workbuddy/tools/asset_audit.py`，压缩工具 `.workbuddy/tools/shrink_assets.py`。
- **字体只用系统字体**：`assets/mmw/font/` 已删，`--pjsk-font` 已从代码里去掉，
  `loadIntroFonts()` 不收参数。别再往仓库里放字体文件（商业字体分发风险那条已关闭）。
- **`assets/se/**` 是白名单**：里面是用户自己加的音效，哪怕现在没接线也不许删
  （`asset_audit.py` 的 `KEEP`）。
- **精灵图集两趟都不碰**：`notes*` / `effect.png` / `longNoteLine*` / `touchLine*`
  —— 精灵矩形是像素坐标写死在 `core/native/generated/generated_resources.h`，
  渲染端按**加载尺寸**归一化 UV，缩文件 = 所有音符错位。
- 仍可优化的（用户还没拍板）：`assets/mmw/overlay/**` 5.2 MB 运行时完全不读
  （只读 `overlay_opt/**`），发布包可排除；`stage.png` / `background_overlay.png` 可再缩但
  有可见代价。

## 谱面目录（2026-09-16 改）
- **只有 exe 同级的 `charts\`**（下载器默认输出）。游戏扫 `chartCandidates` 全部候选**合并**、
  按 .sus 文件名去重（`build\charts` 与仓库根 `charts` 同时可见）。
- 下载器输出目录/托盘行为存在 `<exe>\chartdl.json`。

## 数据文件（`userdata.json` / `profiles\`）
- 2026-09-16 起是**多用户**：`<dataDir>\profiles\<id>.json` 一人一份
  `{settings, scores, account}`，`index.json` 记 active + 名单；首次运行把 `userdata.json`
  **复制**成 `default` 用户（复制不搬）。
- `<dataDir>`：有 `<exe>\..\charts` 就用那一层（build/ 布局 = 仓库根），否则 `<exe>\`。
- scores 的 key 是**谱面文件名**，每条 `{cleared, fullCombo, bestScore}`。
- 优先级：命令行 > 档案 > 内置默认；`--screenshot` 模式**不写**成绩（但 `--activate-profile`
  会写 index.json）。
- 新增设置：`renderScale`（0=窗口多大渲染多大 / 1=固定分辨率等比缩放）、`bgStyle` 默认 1
  （桌面壁纸）。

## UI 通则（本项目所有画面都是"虚拟画布 + 缩放"）
- HUD / 结算 / 选曲都按 1920x1080 虚拟坐标设计，`px()/py()/ps()` 换算到窗口。
- **ImGui 的 `AddText(font, size, ...)` 的 size 是像素**：虚拟单位必须自己乘 scale，
  否则非 1080p 窗口下文字全部偏大（2026-09-13 结算画面踩过）。
- 数值/文字优先用游戏自带精灵（`score/digit/*`、`combo/p*`、HUD overlay），别用字体凑。

## 验证手法（2026-09-16 补）
- **看不了截图时**（模型没视觉）：日志断言 + `.workbuddy/tools/shot_probe.py` 像素探针
  + `winsend.exe` 点击。坐标映射这类要「确定性目标」：`--result-preview` + 点结算「继续」
  （`resultContinueHitTest` 的矩形写死，能算出窗口像素），两种模式下同一个像素都要命中。
- `winsend` 的 `click` 会先发 WM_MOUSEMOVE（SDL 用最后一次移动的位置）；
  `--result-preview` 会在结算出现后 ~2.6s 自动截图退出，点击要连点。
- **2026-09-18 更正：截图是能直接看的**（Read 一张 PNG 即可，模型这边有视觉），
  选曲/房间这类界面改动直接用 `--screenshot` 抓图人工确认，比像素探针快得多。
  抓图时注意：`--party-auto` 会在结算 2s 后自动按「继续」，结算态的截图触发在 2.6s——
  要拍结算就别加 `--party-auto`，或者改 `result-at` 拖时间。
- 跑游戏的日志：**stdout 重定向到文件**（`> run.txt 2>&1`）时有时会落到 `cppsekai.log`
  （取决于 AttachConsole 成不成），两个地方都看一眼，别以为日志是旧的。
- **2026-09-18：日志"跑着跑着不打了"是缓冲假死，不是真卡死**。这个 exe 是 Windows 子系统，
  `AttachConsole` 成功后 `freopen("CONOUT$")` 会把 stdout 打回 msvcrt 默认缓冲，
  shell 里 `> run.txt` 时就只在填满 4KB 才落盘。已改成无条件 `setvbuf(_IONBF)`——但**读旧日志
  时仍要记住这点**，别把"日志停了"当成进程死了。
- **探针的上限别设太小**。给临时条件探针加 `if (n < 8)` 这类计数上限，会掩盖
  "条件一直不满足"和"条件满足了但分支没走"的区别——2026-09-18 排查多人"连不上"时
  差点因此漏掉真根因（真凶是 `selected == -1`，不是 IPC）。
- 无交互会话下 `winsend.exe` 的 PostMessage **到不了某些 ImGui 界面**（SDL 收不到注入输入）。
  要验证的按钮就在代码里加个自动按的调试开关（如 `--chartdl-test`），别在输入注入上死磕。

## 最近工作
- **2026-09-19 两件**：(a) **多人开局倒计时删了**——charge 分「加载段 / 武装段」两段
  （`beginLoading()` → 各窗口 `setSeat(PartySeatLoaded)` → 房主 `allLoaded()` 后
  `armStart(now+0.8s)`），等待从固定 5.8s 变成「最慢那个窗口的加载（实测 0.24s）+ 0.8s」，
  选曲界面的遮罩与大数字全删，面板只写 `谱面加载中…` → `即将开始`；`mp_verify.sh` 三轮全绿。
  (b) **失血阴影几何重做**：原来四角方块在 16:9 上上下边缘正中留 880px 亮带 + 硬断崖，
  改成四条整边条各自垂直渐隐（角落叠两条自然最暗）；新增探针 `CPSEKAI_VIGNETTE=<0..1>`。
  两件事的坑都写进 AGENTS.md「多人游玩」与 main.cpp 注释。
- **2026-09-18 三件用户需求 + 挖出两个真 bug**（commit `3f1c2ba`）：
  (a) 空谱面时选曲界面出「下载谱面（chartdl）」按钮（`SelectDownload`，退出后自动重扫；
  `--chartdl-test` 可自动按）；(c) 首启 ELUA 弹窗（`ui::eulaDialog` + `UserSettings::eulaAccepted`）；
  (b) **多人"有时候连不上"的真根因不在 IPC，在选曲界面**——`drawSongSelect` 每帧用
  `selected = groups[gi].idx[diffIndex]` 反推选中项（`diffIndex` 默认 EXPERT），而
  `buildGroups` 只把谱面放进它自己的难度槽；unipjsk 的谱面一律不写可识别的 `#DIFFICULTY`
  → 所有槽 -1 → `selected` 每帧被写回 **-1**，把调用方开局的合法 `0` 冲掉。列表看着有歌、
  实际"什么都没选中"：单机按确定没反应，房间里房主在 `publishHostSong()` 因 `selected < 0`
  直接 return、从此静默、全房干等。**这是用户早就会遇到的 bug，跟多人无关，只是多人把它
  放大成"连不上"**。修法：解析不出难度就塞进最低空槽 + `selected` 赋值后兜一层扫槽。
  另修两个真 bug：暂停不冻主机的时钟（`wallSongTime()` 是纯 QPC 不知道暂停，无 BGM 谱面
  一暂停房主就把"还在动的瞬间"发给全房，实测 2s 漂 2022ms）；日志缓冲假死（见「验证手法」）。
  还修了 `platform/Party.cpp` 两个确实存在但不是本次根因的竞态（`init()` 先发 `alive`
  后写 `heartbeat`；`update()` 无条件抢回座位）。`mp_verify.sh` 三回合全绿
  （round2 成员暂停位移 2022ms → 0ms）。
- **2026-09-18 多人游玩流程重做**：房间搬到选曲界面（房主光标即广播；成员列表只读 + 灰罩 +
  灰掉的随机；难度在手机面板里选；「确定」= 唯一的开始按钮，最后一个按下立刻开局，
  旧的 4s 加载宽限/10s 倒计时删了）；`AppState::Party` / `drawPartyScreen` 整个删掉。
  同时修了「多人打完不进结算」（根因：多人路径没调 `announceTrack()` → `trackDurationSec`
  恒 0 → `resultDue` 永远假；现在房主把曲长写进共享页给成员）。详见 AGENTS.md「多人游玩」。
- **2026-09-17 多人游玩**（`platform/Party.*` + `game/PartyScreen.*` + `AppState::Party`）：
  同机多窗口一起打，房主选曲 / 各自选难度 / 绝对 QPC 起奏时刻 / 只有房主播 BGM。
  回归脚本 `.workbuddy/tools/mp_verify.sh`（现已扩到三轮，含结算轮）。
- 2026-09-13 结算画面 `game/Result.cpp`（参考官方截图 1:1 复刻，几何全是量出来的）,
  详见 `AGENTS.md` 的「结算画面」一节；调试入口 `--result-preview` / `--result-at`。
- 2026-09-13 设置卡片加「系统」页（失焦自动暂停 / SMTC 汇报开关），页签内容移进裁剪 child；
  选曲分组改成「按读音」（假名行级 + 英文逐字母）。多歌手切换：上游没有，官方数据
  `musicVocals.json` 有，unipjsk 只给一个版本音频——见当日日志。
- 2026-09-13 曲名从 musics.json 回填（unipjsk 的 #TITLE 是空的）；CLEAR 改成打完血量 > 0
  （记录点必须放在结算切换块里）；HUD 暂停键三路输入合并成 hudPausePress() 并放到
  paused/autoPlay 检查之前。真实输入回归用 `build/winsend.exe`（PostMessage 鼠标消息）。
- 2026-09-13 HUD 补上分数 `+N` 浮动与 `AUTO LIVE` 徽章；UI 组件尺寸收小；多歌手（演唱版本）
  切换（数据 `music-vocals.json`，音频就是 `charts/<assetbundleName>.mp3`）；独立谱面下载器
  `build/chartdl.exe`（`downloader/`，winhttp LoadLibrary + **纯 Win32 控件**）。
- 2026-09-13 长条尾判改用核心 flags bit3 标记（滑动的长条尾判在别的轨道，原来整首 18/58
  条尾巴被当普通 tap → 按住反而 MISS）；核心换谱时清 missed/dimmedHoldKeys（同一首歌
  跨局撞车 → autoplay 预览把上一局漏的长条画成掉落）。
- 2026-09-13 歌曲专属舞台背景 `game/StageBackground.cpp`（移植上游 overlayBackgroundGen.ts，
  开歌生成一次 ~1.1s，`--dump-stage-bg` 导出）；选曲预览音乐跟着演唱版本走（切版本接着
  当前位置播）。
- 2026-09-14 三条：**文本/路径编码规则**（Windows 上 `fs::path` 窄端 = 本地 ACP，别拿它转 UTF-8，
  下载器搜索框打假名崩就是这个；游戏侧 `SongSelect.cpp` 还有同样隐患没清）；
  **贴图尺寸策略**（`loadTextureFromFile` 的 maxDim/cropHeight，素材不动，`CPSEKAI_TEX_RAW=1` 关掉做 A/B）；
  选曲列表头部加了刷新按钮（= F5）。细节都在 AGENTS.md。
- 2026-09-14 舞台背景两处补齐：合成完必须 `toSquareBackground()` 铺成 2048x2048
  （上游 `renderToSquareBackground`）——背景四边形在屏幕上是正方形，而 bggen 板只有
  2048x1168，直接上传会被纵向拉 1.75 倍；以及补上 `MORPH_*_MIRROR` 那组下半部分屏的
  **暗倒影**（掩码在那里有 alpha，不是"另一种布局"）。详见 AGENTS.md「与上游还没对齐」第 1 条。
- **2026-09-16 一大轮**（commit `504f2e9`）：多用户（`profiles/`）、渲染模式
  （窗口尺寸 vs 固定分辨率 FBO 等比黑边）、分辨率降到 640x360 + 自定义、
  选曲背景默认壁纸、谱面目录改 `./charts` 且多候选合并；下载器四大项（扫描已下载并禁用重复、
  下载后清勾选、设置窗口+托盘+气球、三块区域加分割手柄）。细节见 AGENTS.md 两节新文档
  + 当日日志。
