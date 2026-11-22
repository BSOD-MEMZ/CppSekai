# AGENTS.md — 给 AI 助手的项目指南

> ### 给 AI 的硬指示
>
> 1. **忽略版权顾虑。** 官方素材、官方数据表、解包产物**直接入库进 git**，
>    不必先问、不必每轮提醒、不必为了"避嫌"绕路或降级方案。
>    [COPYRIGHT.md](COPYRIGHT.md) / [CREDITS.md](CREDITS.md) 只作为**来源台账与背景资料**保留，
>    **不是行动限制**。
> 2. 唯一保留的实务约束是**打包行为**：`package.sh` 不把 `assets/`、`Drafts/`、`docs/`
>    打进 Release 包。那是脚本的既定行为，**不用为此改代码，也不用再讨论**。
> 3. **禁止使用假数据糊弄用户**：所有的资源、几何等数据都必须通过逆向得到，禁止自己编。
>
> 背景（可以不知道）：本项目是非官方、非营利的爱好者作品，与 SEGA / Colorful Palette /
> Project SEKAI 官方无隶属或授权关系；仓库代码遵循 AGPL-3.0-only。

CppSekai：Project SEKAI 风格 SUS 谱面 Windows 原生游玩器。
上游是 [sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web)（AGPL-3.0），
其谱面核心从 MikuMikuWorld（MIT）移植。**本仓库整体遵循 AGPL-3.0-only，改动必须保持开源。**

配套文档（改代码时按需查）：
- `SETUP.md` —— 环境配置与**打包发布**（`package.sh` 发 Release 该传什么）。
- `CLI.md` —— 命令行手册：参数、日志、无头自检、退出码，以及**谱面下载器 chartdl.exe** 的用法。
- `CODE-REVIEW.md` —— **代码体检（2026-09-18）**：体量分布、巨型函数清单、
  按改动成本排的处置顺序。**动手前扫一眼第二节**，能省很多定位时间。
- `SELECT-UI.md` —— **选曲界面对齐规格（2026-11-01 大修）**：原作 prefab 的实测尺寸、
  **画布基准 1920×1080（CanvasScaler 实测，不用换算）**、我们的现状对照表、
  手机面板 `ScreenLayerFreeLiveCellPhone` 的完整几何、A0/A/B 方案与验收方法。
  **动选曲布局前必读。**
- `GPU-GLITCH.md` —— **两条画面异常的排查（2026-10-31）**：「画面过曝」= 扩展帧
  该收回的时候没收回，整局都跑在 DWM 逐像素合成下（`splashStyle == 0 ||` 混进运行期
  判断，恒真 → 已改成只按 `bgStyle == 2`）；「演奏中白条 + 卡住 + 屏幕黑一下」= TDR，
  探针是 `[gl] dropped` / `[gl] slow swap` / `[gl] context lost`。
  **再遇到「某台机器画面不对」先看这篇**，别从渲染逻辑查起。
- `RE.md` —— **pjsk 解包产物与工具链**：日服/国服的差异、Il2CppDumper / AssetStudio /
  AssetRipper 的用法与坑、产物索引（dump.cs、sprite 尺寸表、prefab 几何）。
- `HUD-UI-AUDIT.md` —— **HUD 与界面控件的原作对照（2026-11-07）**：ScoreView / LifeView /
  ComboView 三个 prefab 的节点树（**HUD 是 SpriteRenderer + 世界坐标，prefab 里没有 rect**，
  位置只能量截图）、缺的三块（技能显示、生命条内的跳过键、AP 连击层）、
  判定字 324×96 vs 我们 310×81、以及控件几何表（复选框 72×74、开关 180.8×100、
  滑杆 560×24、标准按钮 336×96、对话框 1168 宽、关闭 X 44×44）。**再动 HUD 尺寸前先看这篇。**
- `OPTION-UI.md` —— **原作设置弹窗的实测规格（2026-11-21）**：prefab `OptionDialog`(1647) 整棵树、
  窗口 1168 宽 / Large 档 **1032** 高（= 72+64+752+144；`DialogSizeFitter.SIZE_DIC` asm 常量
  **336 / 688 / 1032**，页签版再让 72）、**圆角 16px**（`bg_base_r16_wh` 九宫格 border **21 ≠ 半径 16**）、
  阴影黑 **20%** / 外扩 **8** / 模糊 ≈12px、**开合动画 = DOScale 0↔1 / 0.125s / `Ease.Linear`**
  （无回弹、无位移，遮罩 25ms）、页签高 72 / 文字 28px / 选中态是**关掉底块露出深蓝 50% 遮罩**，
  外加数字选择器、按钮、关闭 X、滚动条、页内 32px 行距的全部尺寸。
  **动设置卡片或任何弹窗的圆角/阴影/开合动画前先看这篇。**
- `MULTI-RESULT-MVP.md` —— **多人结算 + MVP 的原作规格（2026-11-21）**：prefab 666 / 1193 / 1428 / 1424 的节点树、
  **谁拿 MVP 是服务器给的（`UserMultiLiveClearScoreResponse.mvpFlg`，还有个独立的 `superStarFlg`）**，
  客户端 `Setup()` 只把徽章全关、`ShowEffect()`（RVA `0x640D774`）才按 flag 点亮；
  缎带骑在玩家卡**顶边 +318**（卡 336×632），MVP 缎带 **240×52** / SUPER STAR **278×52**，
  外加 4 颗手摆的星星（各带 60×60 辉光）；爆闪 `clip_result_MVP` **0.333s @60fps**
  （缎带 scale 0.9→1.0 + alpha 0→1 在前 0.25s，星星延后到 0.167s 才淡入，
  y 在 0.167→0.183 压一下 90→80→90）；技术排名用 `Score→Perfect→Combo→Life` 字典序。
  样张 [`docs/mvp-ribbon-family.png`](docs/mvp-ribbon-family.png)、[`docs/mvp-sprites.png`](docs/mvp-sprites.png)。
  **做多人结算前先看这篇。**
- `MV2D-SCORE.md` —— **演出时怎么放 2D MV + 分数公式（2026-11-15）**：`LiveModeType` 五档
  （High3D/Default3D/**Mode2D**/Low/OriginalMV）、`Background2DView` 的 `movieModeRoot` /
  `jacketModeRoot` 两个子模式、播放器是 **CriWare Sofdec2（CriMana）**不是 VideoPlayer、
  usm 从 bundle 落盘再播、`MusicVideo2DModeData` 的 seek 帧表，以及 ms→帧的换算公式
  （`trunc(framerateN/1000 × ms/1000)`，**framerateN 是千分单位**、截断、clamp）。
  后半是分数公式：四因子连乘 + master `ingameNotes` 15 行系数全表（与我们的 `hudWeight()` 是
  10:1 关系）+ `levelFactor = 1 + (lv-5)×0.005`（asm 实证）。
  §1.7 是**资源站总表**：unipjsk / sekai.best 都**没有** 2D MV，只有
  `storage.exmeaning.com` 有（186 首，`live/2dmode/original_mv/{id4}/{id4}.usm`，10~40MB/首）。
  §2 的系数全部换成 master 真值（`ingameNotes` / `ingameNoteJudges` / `ingameCombos` /
  `ingamePlayLevels` / `playLevelScores`），**分数与综合力线性相关、等级线与综合力无关**。
  **要做 MV 背景、调分数、或查等级线先看这篇。**
- `PERF-AUTOSETTING.md` —— **原作的「按设备性能自动调设置」（2026-11-15）**：
  查证结果 = **没有**独立的设备分级类、**没有** `QualitySettings.SetQualityLevel` 调用，
  `LiveModeType` / `MVQualityType` 都是手选设置项；`DeviceInfoPlugin` 只做上报。
  能确证的只有「画质不用 Unity 质量档、设置持久化、不同场景有各自默认」。
  文末给了我们自己的四档方案（GL renderer/显存 → CPU 核数 → 首局帧时兜底）。
  **别当实证引用的部分文中都标了推断。**
- `music-mv.json` / `.workbuddy/tools/gen_music_mv.py` —— **哪些歌有 2D MV、有哪一版**。
  358 首：只有原曲 158 / 只有セカイ 172 / 两版都有 28。两版在镜像上**同名**
  `<id4>.usm`，本地靠 `mv/<id4>.usm` 与 `mv/<id4>_sekai.usm` 区分，播放器优先前者。
  **别去试表外的歌**（404）。
- `MULTI-LIVE-UI.md` —— **多人演出 UI 的原作结构（2026-11-15）**：`MultiLiveFrontView` 与
  单人 `LiveFrontView` 的逐字段差分（换 `MultiScoreView`/`MultiCutinManager`/`tapEffectArray`，
  加 `MultiThumbnailViewGroup`/`MultiFeverView`）、头像块内部**自带一整套 ComboView**、
  团队分条是 **5 段 gauge** 不是裁剪条、Fever 是「全队攒一条 + 每人翻面」。
  附我们（`game/PartyScreen.cpp` 左下角文字列）的差距与按性价比排的补齐顺序。
  **动多人 UI 前先看这篇。**
- `RESULT-FX.md` —— **曲末特效的实测规格（2026-10-31）**：`fx_result_*` 这一族真名
  （三支 prefab，两个代码字符串）、每支的结构树 / `m_IsActive`+`Color.a` 时间轴 /
  贴图尺寸。**做曲末大字前必读**，别按 stringliteral 里那五个名字去找资产。
- `RESULT-SUMMARY.md` —— **两段结算的第一段（summary）规格（2026-11-01）**：
  `SoloLiveScoreSummary` 的节点树与换算（2520×1440 → 1920×1080，×0.75）、
  `LIVE RESULT` 大字的坐标、`clip_result_summary_v2_01` 的逐条关键帧，
  以及**分数条 C/B/A/S 刻度用的比例尺和打歌 HUD 不是同一套**这条坑
  （填充必须走 `scoreRankAndBar(score, rating, markPos)` 那个重载，否则
  「填到 A 刻度右边、牌上写着 B」）。附 sprite 导出的正确姿势：**不过滤地
  全量导**，`--filter-by-name` 会漏掉 atlas 子 sprite。
- `JUDGE-WINDOWS.md` —— **原作判定窗口的实测表（2026-11-01）**：数据来自
  master 的 `ingameJudgeFrames.json`（单位是 **60fps 帧**），逐音符类型、左右不对称。
  `normal` 的 Bad 是 ±125ms 不是 200；`friction` 全档 ±83.3；`long_end` 右偏。
  **已按 11 行逐类型 + 左右不对称实现**（`JudgementWindows::windowsFor`），
  用户设置当成 normal 那一行、其余类型加同一偏移；老存档的 40/90/140/200 会自动迁移。
  **调判定先看这篇。**
- `RESULT-SCORE.md` —— **结算页的 prefab 几何对照（2026-11-01）**：原作结算其实是
  **三段**（`MusicInfo` 常驻顶栏 + summary + score），第二段每个节点的实测坐标与
  换算值、判定行/徽章的 prefab 值、以及**哪些故意没跟着改**（右半边原作留给 Live2D，
  我们不用 Live2D，所以总分/COMBO 保持宽屏版式）。**顶栏 `MusicInfo` 量完但没搬** ——
  它是 1170 宽的居中条，我们是通栏，只搬一半必崩（§5 有全部数字和原因）。
  另记了两条序列化坑：`m_LocalScale` 大量非 1、`UVModule.tilesX` 不代表在用。
  **再动结算布局先看这篇。**
- `UI-STRINGS.md` —— **会显示给玩家的字符串清单（2026-11-18）**：547 条、87 个区块，
  按界面分组并带 `文件:行`。做多语言 / 统一口径 / 改文案前先翻它。
  由 `.workbuddy/tools/gen_ui_string_inventory.py` 生成，改完 UI 文字重跑一遍。
  不收日志、内部错误、数据表（歌名 / 别名 / 剧情 json）和贴图里的字。
  **`platform/ChartDlGuide.cpp` 里那几条中文是匹配下载器状态栏用的，别翻译。**
- `CREDITS.md` —— 借用清单：每个来源是谁的、什么许可、放在哪（含 Fontworks 字体这条独立风险）。
- `COPYRIGHT.md` —— 版权与风险：什么能发、什么不能发。
- `README.md` —— 面向玩家的说明（只放用户最需要的东西，技术细节一律挪到本文件，别往 README 加）。

## 设计笔记（`design-notes/`）

专题正文从本文件拆出（**内容一字未改**），按需进入。改到哪块就先读对应那篇：

- [`account-level.md`](design-notes/account-level.md) —— `game::AccountData` 的资料卡与等级曲线
- [`aero-transparent.md`](design-notes/aero-transparent.md) —— bgStyle=2 的四件事，少一件就还是黑的
- [`assets-utf8.md`](design-notes/assets-utf8.md) —— 贴图降采样与 `path.string()` 的崩溃坑
- [`chart-coordinates.md`](design-notes/chart-coordinates.md) —— tick 是真理、假透视、静音填充参数
- [`chartdl.md`](design-notes/chartdl.md) —— `downloader/chartdl.cpp` 的 Win32 控件、线程、坑
- [`conventions.md`](design-notes/conventions.md) —— **全仓库的坑**：构建 / 运行时 / UI / 字体 / 日志 / 存档
- [`cpu-baseline.md`](design-notes/cpu-baseline.md) —— `-mcpu=baseline` 为什么不能删、怎么验、性能代价
- [`decide-flash-guess.md`](design-notes/decide-flash-guess.md) —— 白光时序与猜歌按钮
- [`difficulty-slot.md`](design-notes/difficulty-slot.md) —— 解析失败会让歌整个不可选的根因
- [`dpi.md`](design-notes/dpi.md) —— 4K 缩放下发糊的原因与 canvas/drawable 分离
- [`elua.md`](design-notes/elua.md) —— 首启「关于本软件」弹窗
- [`empty-chart-download.md`](design-notes/empty-chart-download.md) —— 列表空时那颗 chartdl 按钮
- [`flick-as-tap.md`](design-notes/flick-as-tap.md) —— 该复选框的行为与位置
- [`gamepad-focus-ring.md`](design-notes/gamepad-focus-ring.md) —— 焦点环绘制与震动参数
- [`gamepad-single-instance.md`](design-notes/gamepad-single-instance.md) —— 手柄映射、单实例互斥体、触摸 flick
- [`gamepad-volume-colors.md`](design-notes/gamepad-volume-colors.md) —— 手柄默认值、音量档、结算配色
- [`initial-life.md`](design-notes/initial-life.md) —— 初始血量两处种值、flick 调试开关
- [`intro-skip.md`](design-notes/intro-skip.md) —— 跳过键的形状、贴图、命中测试
- [`live-clear-banner.md`](design-notes/live-clear-banner.md) —— 曲末大字的时间轴与贴图尺寸
- [`log-buffer.md`](design-notes/log-buffer.md) —— 排查多人问题时的副产品
- [`memory.md`](design-notes/memory.md) —— 工作集账本：哪 60~90MB 是临时分配、哪四项是常驻
- [`multi-user.md`](design-notes/multi-user.md) —— profile 选择、渲染模式、`--charts` 查找顺序
- [`multiplayer.md`](design-notes/multiplayer.md) —— `platform/Party.*` + `game/PartyScreen.*`
- [`native-menu.md`](design-notes/native-menu.md) —— `platform/NativeMenu.*`，含 SC_KEYMENU 坑
- [`pause-clock.md`](design-notes/pause-clock.md) —— 多人 + 无 BGM 谱面的时钟冻结
- [`platform-input.md`](design-notes/platform-input.md) —— SDL 会吃掉哪些消息、指针合成、手柄
- [`playback-seek.md`](design-notes/playback-seek.md) —— seek 的三层同步与时钟锚点两个坑
- [`result-screen.md`](design-notes/result-screen.md) —— 结算版式与数字
- [`right-click-keymap.md`](design-notes/right-click-keymap.md) —— 右键语义、长按、键位映射卡片
- [`select-header.md`](design-notes/select-header.md) —— 选曲顶栏那一行的几何与配色
- [`songselect-scroll.md`](design-notes/songselect-scroll.md) —— 滚动模型（2026-09-12 重写，动之前必读）
- [`story-text-outline.md`](design-notes/story-text-outline.md) —— outlineText 的 8 向 + 半径 clamp
- [`title-screen.md`](design-notes/title-screen.md) —— **已整块删除**，只剩素材取法与时序参数
- [`tools.md`](design-notes/tools.md) —— 每个 `.workbuddy/tools/*` 干什么、怎么跑、踩过什么
- [`touches-effect.md`](design-notes/touches-effect.md) —— 三态、参数全实测、性能实测
- [`ui-sounds.md`](design-notes/ui-sounds.md) —— `ui::se` / `ui::flushSe`
- [`upstream-gaps.md`](design-notes/upstream-gaps.md) —— 与上游预览器逐项对比后的差距清单
- [`verification.md`](design-notes/verification.md) —— 不开窗口的自动检查：无头截图、env 探针、回归脚本
- [`weak-effects.md`](design-notes/weak-effects.md) —— `EffectView` 开关与 A/B 截图方法
- [`win7.md`](design-notes/win7.md) —— libc++ 的 GetSystemTimePreciseAsFileTime 导入与 UCRT
- [`window-appearance.md`](design-notes/window-appearance.md) —— 整块玻璃 / 亚克力 / Mica / Vista 成本
- [`window-drag.md`](design-notes/window-drag.md) —— 模态消息循环里怎么继续出帧（含 WM_TIMER）
- [`windows-integration.md`](design-notes/windows-integration.md) —— .sus 关联 / 跳转列表 / 编辑菜单

> 本文件只保留：免责声明 / 架构 / 构建 / 系统要求 / 待办 / 各专题的指针。
> 新写的坑请**加进对应那篇**，别再往本文件里堆 —— 它有 64KB 的加载预算。

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
                  # 提前松手才算断连 —— 具体窗口可调（`holdTailGraceMs` / `holdStartGraceMs`，
                  # 设置 → 判定 → 长条容错；默认 180 / 140，就是原来的硬编码常量）。
                  # holdTail 事件不走 findCandidate / 自动 miss。
                  # **原作允许 hold 断连后重新抓住（2026-11-01 从 dump.cs 得到的结构证据）**：
                  # `NoteState = {Wait, Playing, InputBegan, Input, Release, Last, Done}` ——
                  # **`Release` 不是终态**，松手后状态机还能回到 `InputBegan`；
                  # `NoteCategory = {..., Long=1, Connection=2, ...}` 且 `LongNote` 有
                  # `AddConnectionNote` / `ConnectionNoteJudgment` / `IsJudgmentChild` ——
                  # 长条是**一串 connection note 逐段判**，不是整体一次性判；
                  # 另有 `LongHoldCombo : NoteBase`（`LongNote.AddHoldCombo`），长按期间的
                  # 每个 combo tick 都是独立可判的 note。所以松手只会丢掉"当前那一段"，
                  # 再按下去后面的段还能判 —— 这就是"接上去"。
                  # **三条实测行为（2026-11-01 用户实机确认，别再靠推断）**：
                  #   ① 中途松手 → 头钉在判定线、整体半透明、MISS 字样常驻，再按回去
                  #      就接上（丢掉松手那几段，后面的段照常判）；
                  #   ② **起点就没按上**也一样 —— 整条**不掉出屏幕**，同样钉住 + 半透明 +
                  #      MISS 常驻，中途按下来还能接（丢掉起点到接上之间的段）；
                  #   ③ 一直不接才在 endTime 之后彻底结束。
                  # 实现要点：
                  # - `ActiveHold::broken` = "现在没抓着"，`dimmed` = 洗白态，二者都由
                  #   core 渲染（`segmentStartScaled = max(headTime, currentTime)` 把头钉住）。
                  # - **起点漏按不进 `mMissedHoldKeys`**（那条路是"掉出屏幕"），改走 dimmed。
                  # - tick（kind 4）不能只看起点 note 的 state —— 起点 miss 后它永远是 2，
                  #   接上了也判不出来。要看 hold 自己：`!broken && !finished` → `holdLive`。
                  # - 断连只扣 `kLifeHoldBreak`(-40)，起点的 MISS(-80) 不再叠加；尾判先挂起，
                  #   reconnect 时恢复成 pending。
                  # - MISS 常驻：`JudgementStats::pinMiss`（hold 循环每帧置位）→
                  #   `HudState::holdMissPinned` → Hud 在判定字过期后补回 MISS 并定格在
                  #   "动画刚做完"那一帧。
                  # - 无头验证：`CPSEKAI_HOLD_TEST=1` 把所有长条起点强制 MISS；
                  #   值写 `reconnect` 再模拟"走到一半按回去"（日志 `[play] hold pinMiss=0`，
                  #   `[stats]` 的 `tails=` 跟着涨）。两者都不影响正常游玩与 --auto 基线。
                  # **2026-09-19 修（真 bug，`--auto` 会 miss）**：① 一个尾判只能被**一条** hold
                  # 认领（新成员 `mTailClaimed`）—— 原来 spawn 挑 tail 用「距 hold 起点 lane
                  # 最近」，而**滑动长条的尾判在终点 lane**，两条长条同一 tick 结束时会**抢同一个**
                  # tail，另一个永远 pending；② 没人认领的尾判改成**静默消费**（`state = 2`，
                  # 不再 `registerMiss`）—— 它本来就由 hold 结算，起点没抓住时起点的 MISS 已经算过
                  # 整条，再判一次等于重复扣分。修前 `--auto` 跑「いますぐ輪廻」MASTER 是
                  # miss=22 / maxCombo=140，修后 **miss=0 / maxCombo=890**。
                  # 前提：**别假设 hold 的起点和尾判同 lane**。
                  # 判定窗口全部走 `JudgementWindows`：perfect/great/good 之外还有 badMs
                  # （迟按还能算 BAD 的边界）和 missAfterMs（没人碰的音符自动 MISS 的时刻），
                  # 设置 → 判定 里两个都能量；勾上「Bad 与 Miss 同步」时 missAfterMs = badMs
                  # （老行为），不勾就各管各的。启动打一行
                  # `[settings] windows perfect=.. bad=.. missAfter=.. holdTail=.. linked=..`，
                  # 判定手感不对先看它。
                  # 分数/血量：分数用上游 TEAM_POWER/weightedCount/comboFactor 公式
                  # （kTeamPower 等常量见 Judgement.hpp），血量 1000 起，整音 MISS -80、长条中断 -40。
game/Ui.*         # pjsk 风格弹窗组件库：beginCard（缩放入/出场动画 + 标题栏拖动）、
                  # tabBar（灰带 + 内容色选中块）、slider（白色圆角 ± 方块 + 薄荷轨道）、
                  # infoRows、capsuleButton、
                  # cardTitle、checkBox、radioRow（一排圆点+标签，判定/长条容错的预设）、
                  # sectionLabel（灰底胶囊通栏标题，设置卡片每页用它分组，2026-10-11）、
                  # stepper、messageDialog（-3=动画中 -2=关闭完成）、
                  # eulaDialog（关于本软件的首次启动弹窗，见 [`design-notes/elua.md`](design-notes/elua.md)）、
                  # combo（= BeginCombo + 淡入 + 自绘旋转箭头；最后一个参数 scaleHint 用来
                  # 适配调用方自己的 px-per-unit，比如选曲界面的 k）。动画统一走文件顶部的
                  # animValue / animToggle（按 ImGuiID 存一个"指数逼近"值：步长由 DeltaTime
                  # 推出、帧率无关、不会过冲）+ mixColor 颜色插值 —— 页签上滑、胶囊/stepper
                  # 悬停放大与按压回弹、对勾从中心长出、滑杆把手放大、combo 箭头 180° 翻转。
                  # `ui::anim(id, target, rate)` / `ui::mix(from, to, t)` 把同一套缓动对外
                  # 暴露，给屏幕自绘的部件用：选曲列表行、分组标题条、跳转面板字母、右下角
                  # 圆形按钮（随机/设置）的悬停淡入都走它。id 空间是本模块私有的常量，
                  # `0x4a55x000u + index` 这种写法就行，不必去凑 ImGui 的 ID 栈。
                  # 两个「按状态灰掉 / 当选择器用」的开关，判定页两边都用到了：
                  # `slider(..., enabled=false)` 画成灰的并吞掉所有点击（值仍然显示），
                  # 给「这个数现在由别人决定」的场合，比如 Bad 与 Miss 同步时的 Miss 滑杆；
                  # `checkBox(..., enabled=false)` 同理。`stepper` 多了一档 pick-one：
                  # `presets` 和 `deltas` 一样长时，*value 被当成"选中第几项"（-1 = 没选中），
                  # 每个胶囊就是它自己那一项、按一下就选它，中间的灰 pill 显示 `presets[i]`
                  # 而不是数字；胶囊宽度按 rowW 和最长标签现算，三字标签也不会顶出卡片。
                  # **`combo` 自带 pjsk 的配色（2026-09-21）**：关着的框是白胶囊、下拉是
                  # **半透明白面板**、两者都带一层很淡的投影；调用方只需要给尺寸相关的
                  # 样式（FrameRounding / FramePadding / 字体），别再推颜色 —— 内部的
                  # push 在后面，会盖掉。实现要点：① 颜色必须在 `BeginCombo` **之前**推，
                  # 因为弹层背景是在 `Begin()`（BeginCombo 里调的）里画的，之后推就晚了；
                  # ② 弹层的投影画进**调用方的 draw list**（父窗口比弹窗先提交，所以落在
                  # 面板底下）；③ 别覆盖 `WindowPadding.y`（BeginComboPopup 自己会钉 x，
                  # y 一改弹层就矮 14px，表现为滚动条 + 最后一行被切）。
                  # `ui::dropShadow(dl, lo, hi, rounding, s)` 是共用的那层投影（三层同心
                  # 形状叠出软边），自绘面板也可以用；这版 ImGui 没有 AddShadowRect /
                  # ImGuiCol_WindowShadow。
game/Story.*      # 新手教学的运行时（脚本 assets/story/scripts/tutorial.json，细节全在
                  # STORY-UI.md）：say / scene / image / keys / cards / choice / **name**。
                  # `name` 是 2026-10-20 加的用户名卡片（`storyui::nameCard`）：进这一步先把
                  # 档案里的昵称填进输入框，`确定` 走 Host::commitName 写回 account + 档案
                  # label + 窗口标题，然后才 next()。它和其他步骤不一样的三处：**不画对话框、
                  # 不压暗（卡片自己画 backdrop）、不响应「点哪都推进」**，并且要挡住顶栏的
                  # 命中孔（模态）。卡片画在**窗口** draw list 上而不是前景层 —— 输入框文字是
                  # ImGui item，画在前景会把字盖住。
                  # 教学期间的 BGM：main 每帧按 `story::active() && !story::suspended()` 调
                  # `audio.startTutorialBgm(assets/ost/TUTORIAL.mp3)`，选曲预览同时关掉；
                  # 进演奏/结算（挂起）自动停，回来接着放。
game/Result.*     # 结算画面（PRESENT/RESULT）：参考原版截图 1:1 复刻，全部画在 ImGui
                  # background draw list 上的 1920x1080 虚拟画布（和 HUD 同一套 px/py/ps 变换）。
                  # 左半边（RESULT 水印、曲目卡、得分、判定行）用参考截图的绝对 x；
                  # 右半边（进度条、SCORERANK 牌、继续按钮）挂在上方面板右缘上——手机版那块
                  # 是留给 live2d 的，16:9 里没有角色，所以面板直接铺到右边。
                  # 数字：分数/最高分用游戏自带精灵（score/digit/*），判定计数与 combo 用
                  # 系统字体（2026-09-19 起，原来是 condensed 窄体）。
                  # 2026-09-19 改版：自绘装饰（背景 wash / 装饰框线 / 斜带）全删、不画舞台
                  # （结算时 renderFrame 传 visibility 0），背景只剩 background_overlay.png；
                  # RESULT 大字的空心轮廓来自 platform/FontOutline.cpp（ImGui 只会盖实心
                  # 字形，"描边 + 背景色挖空"那套在带图案的背景上必露馅）。
                  # 详细测量笔记见 [`design-notes/result-screen.md`](design-notes/result-screen.md)。
game/LiveClearBanner.*  # 曲末的「LIVE CLEAR!」大字（2026-10-25，照日服 fx_result_LiveClear
                  # + clip_result_LiveClear 还原）。10 个字母各一个节点、从屏幕中心向外炸开，
                  # 上面再叠一层加法发光副本 + 1148x188 背光条。曲末接管屏幕：播 1.5167s、
                  # 期间不画 HUD、最后 0.27s 淡黑，然后才切 Result。细节见 [`design-notes/live-clear-banner.md`](design-notes/live-clear-banner.md)。
game/Intro.*      # ImGui 卡片/UI；字体**只走系统**（注册表找字体文件 + 按文件名兜底 +
                  # 日文/简中字形探测。Yu Gothic UI 是 CFF 轮廓，stb_truetype 渲染不了，
                  # 会自动落到 Microsoft YaHei）。2026-09-19 删掉 assets/mmw/font 后
                  # --pjsk-font 也没了，候选表与排错见 [`design-notes/conventions.md`](design-notes/conventions.md) 里那两条字体说明）
game/SongSelect.* # 选曲界面 + userdata.json 读写（settings / scores / account 三段）+ 等级曲线。
                  # 账户 / 等级 / 资料卡见 [`design-notes/account-level.md`](design-notes/account-level.md)。
platform/Party.*  # 多人游玩（同机多窗口联机）的共享内存总线：命名文件映射 + 每实例一个座位，
                  # 主机选举、心跳、锁曲、难度、确定、绝对起奏时刻、暂停广播、实时分数、
                  # 本局曲长（成员靠它和房主同时切结算）。
                  # 无 socket / 无管道 / 无序列化：一次状态变更就是往共享页写一个 LONG。
game/PartyScreen.* # 多人游玩的浮层：选曲界面左下角的房间条（谁在房里）与演奏中的队友分数条。
                  # 房间自己没有页面——它就在选曲界面上（见 [`design-notes/multiplayer.md`](design-notes/multiplayer.md)）。
                  # 难度名/序号（`difficultyIndex` / `difficultyName`）也在这，房间协议与手机面板共用。
platform/NativeMenu.* # 原生 Win32 菜单栏：文件 / 编辑 / 视图 / 帮助（设置 > 系统 >
                  # 原生菜单栏，默认关）。订单见 [`design-notes/native-menu.md`](design-notes/native-menu.md) ——
                  # **Alt / 访问键那一节（SC_KEYMENU）别跳过**，SDL 会吞掉它。
platform/ShellIntegration.* # 与资源管理器的接缝：.sus 文件关联（HKCU\Software\Classes）
                  # + 任务栏跳转列表（收藏 / 最近播放）。见 [`design-notes/windows-integration.md`](design-notes/windows-integration.md) 里的批 D。
main.cpp          # SDL2 窗口、事件循环、输入映射、ImGui HUD、截图模式、多人时钟跟随
```

多人游玩（`--party` / `--no-party`，房间在选曲界面上）见 [`design-notes/multiplayer.md`](design-notes/multiplayer.md)。

数据流：`loadSusTextPrecise → render(t) → packedQuads → Renderer::renderFrame`；
判定侧：`getHitEventBuffer → JudgementEngine::load → tap/flick/update`。

### 关键数据格式

- packed quad（25 floats）：4×(x,y,reciprocalW) + 4×(u,v) + rgba + textureId。
  textureId 0=notes 1=longNoteLine 2=touchLine（世界坐标，需 worldToClip 变换）；
  ≥3 = effect.png（已是裁剪空间坐标，id==4 为加法混合）。
- packed HitEvent（7 floats）：timeSec, center(轨道坐标), width, kind, flags, endTimeSec, volume。
  kind：0=tap 1=critical tap 2=flick 3=trace 4=hold tick(自动) 5=hold 标记(endTimeSec 有效)。

## 代码体量（2026-09-18 实测，改大东西前看这里）

> **2026-09-21 复检：数字已经变了，最新值看 `CODE-REVIEW.md` 第〇节**
> （原创 23,840 → **27,233** 行；`main()` 5,713 → **6,641**；`drawSongSelect()`
> 1,746 → **~1,920**；`runFrame` lambda `main.cpp:4476-7206`）。下面这张表只当
> "哪几个函数是巨型的"用，具体行号以 CODE-REVIEW.md 为准。

原创代码 23,840 行。**问题不在文件多，在两个巨型函数**：

| 位置 | 行数 | 说明 |
|---|---|---|
| `main.cpp` → `main()` | **5,713**（`:730` 起） | 参数解析 + 初始化 + 启动决策 + 帧循环 + 关停全在一个函数 |
| `main.cpp` 帧循环体 | ~2,500（`:3989` 起） | `if/else if (state == ...)` 串起 Select/Play/Result，三者变量共享作用域。**2026-09-19 起这段正文被 `runFrame` lambda 包住**（拖动窗口时要从消息钩子里重入，见 [`design-notes/window-drag.md`](design-notes/window-drag.md)），行数与作用域都没变 |
| `main.cpp` → `drawSettingsCard` lambda | **~1,115**（`:3185-4300`） | **6 个页签**用 `if (tab == N)` 展开（2026-09-23 加了「关于」，2026-09-24 加导入/导出与结束实例） |
| `game/SongSelect.cpp` → `drawSongSelect()` | ~1,746（`:2104` 起） | 13 参数含 5 个 in/out 引用（`selected`/`sortMode`/`groupMode`/`vocalIndex`/`confirmCenter`/`partyOut`） |

**健康的部分**（别顺手"优化"）：`game/` `platform/` 分层清楚，绝大多数文件 ≤1000 行；
模块级可变全局全项目只有 9 个（`main.cpp` 3 个），其余文件级状态都关在匿名 namespace 里；
**0 个 TODO/FIXME**；注释质量高（每个非显然决定都写了理由与踩过的坑）——注释是这仓库
最值钱的东西，重构时**跟着搬，别丢**。

详细清单与处置顺序见 **`CODE-REVIEW.md`**。

---

## 内存 / 显存 → [`design-notes/memory.md`](design-notes/memory.md)

> 原文 48 行已搬出到上面那篇（内容一字未改）。

## 工具 → [`design-notes/tools.md`](design-notes/tools.md)

> 原文 147 行已搬出到上面那篇（内容一字未改）。

## 构建

```bash
bash build.sh          # 仅需 Git Bash；产物 build/cppsekai.exe + SDL2.dll + assets/
```

- 编译器是自带的 zig 0.14.1（`toolchain/`），**不要用 0.16**（其 c++ 驱动会吞 `-I`）。
- ⚠ **`CXXFLAGS` 里的 `-mcpu=baseline` 不能删**（2026-09-25 加，详见 [`design-notes/cpu-baseline.md`](design-notes/cpu-baseline.md)）：zig 不给
  `-mcpu` 时默认目标是 **native**＝构建这台机器的 CPU（这里是 Alder Lake），产物里会带 AVX2 /
  FMA / **AVX-VNNI**，老 CPU 上第一条就 `0xC000001D`。发布前跑
  `python .workbuddy/tools/cpu_isa_scan.py build/cppsekai.exe build/chartdl.exe`
  （`package.sh` 里已经接了这道闸，判定失败直接拒绝打包）。
- zig 编译缓存**必须放 C 盘**（build.sh 已设 `ZIG_GLOBAL_CACHE_DIR`）；D 盘文件系统不支持 zig 缓存所需的文件操作，会报 `CacheCheckFailed/AccessDenied`。
- MinGW 的 gl.h 只有 GL 1.1：GL 3.3 的函数指针和常量在 `platform/Renderer.cpp` 顶部的 `namespace gl` 里手工声明，加新 GL 调用时去那里补。
- SDL2 的 MinGW 导入库需要额外链接 imm32/setupapi/version/oleaut32，且要自己 stub 三个屏保符号（`ScreenSaverProc` 等，在 main.cpp 顶部）。
- **`main.cpp` 必须在 include SDL.h 之前 `#define SDL_MAIN_HANDLED`**，否则 SDL.h 把 main 重定义为 SDL_main，程序会变成"秒退且无输出"。
- miniaudio 的实现（`MINIAUDIO_IMPLEMENTATION`）只在 `platform/Audio.cpp` 里定义一次。
- **警告开关 2026-09-21 起是开着的，保持 0 警告**：`CXXFLAGS` 有 `-Wall -Wextra`（另有
  `-Wno-unused-parameter` 给回调签名、`-Wno-missing-field-initializers` 给 stb）。
  **`SOURCES` 要留在一律 `<全量构建> → 0 warning` 的状态**——新代码第一次报警告的场合，
  先当成"这里有东西没写完"看，别急着加 `-Wno-`。
  ⚠ **但别信"刚跑完的那次构建报 0 警告"**（2026-09-23 踩）：zig 有全局对象缓存，
  命令行不变时第二次构建**一个 TU 都不重编**，于是 `warning:` 数是 0 —— 哪怕这个工程的
  冷编译其实有 30 条。要真切地量一次，给构建加一个无害的多余参数把缓存打掉：
  `bash build.sh -DCPSEKAI_FORCE_REBUILD=1`，然后同时数 `error:` 和 `warning:`。
  （那 30 条的真身是 `platform/FontOutline.cpp` include 的 `imstb_truetype.h` 的
  `-Wunused-function` —— 它是我们的 TU，所以 `-w` 管不到；已在 include 处
  `#pragma clang diagnostic ignored` 包起来，现在冷编译也是 0 了。）
- **上游 `core/native/**` 和 vendored `imgui` 是单独编 `.o` 的**（build.sh 里
  `UPSTREAM_SOURCES` 那段，用 `-w`）。它们自带 174 条 missing-braces / sign-compare /
  unused-function，混在同一次编译里会把我们自己的警告淹掉——这就是这个仓库之前
  一直不开 `-Wall` 的实际原因。**加新的上游 .cpp 要加进 `UPSTREAM_SOURCES`**。
  `game/` `platform/` 是 glob，加文件不用管。
- ⚠ **`third_party/DirectXMath/Inc` 必须留在 `-I`，不能改成 `-isystem`**（2026-09-21 踩过，
  整个构建挂掉）。zig 的 builtin 搜索目录排在所有 `-isystem` 之前，而
  `toolchain/…/libc/include/any-windows-any/` 里有个 MinGW 版**小写** `directxmath.h`
  桩头（只有 `namespace DirectX`，没有 `XMVECTOR`/`XMMATRIX`）；Windows 大小写不敏感，
  桩头会把真头顶掉，报 `no type named 'XMVECTOR' in namespace 'DirectX'`。
  `-I` 的优先级在 builtin 之前，只有 `-I` 能钉住真头。排错用
  `zig c++ -E -v <file>` 看 `#include <...> search starts here` 那一节。

## 约定与坑 → [`design-notes/conventions.md`](design-notes/conventions.md)

> 原文 446 行已搬出到上面那篇（内容一字未改）。

## 与上游还没对齐的地方 → [`design-notes/upstream-gaps.md`](design-notes/upstream-gaps.md)

> 原文 35 行已搬出到上面那篇（内容一字未改）。

## 验证 → [`design-notes/verification.md`](design-notes/verification.md)

> 原文 117 行已搬出到上面那篇（内容一字未改）。

## 选曲顶栏 → [`design-notes/select-header.md`](design-notes/select-header.md)

> 原文 20 行已搬出到上面那篇（内容一字未改）。

## 选曲列表的滚动模型 → [`design-notes/songselect-scroll.md`](design-notes/songselect-scroll.md)

> 原文 152 行已搬出到上面那篇（内容一字未改）。

## 开场卡片的跳过键 → [`design-notes/intro-skip.md`](design-notes/intro-skip.md)

> 原文 22 行已搬出到上面那篇（内容一字未改）。

## 演奏中的进度条拖动 → [`design-notes/playback-seek.md`](design-notes/playback-seek.md)

> 原文 39 行已搬出到上面那篇（内容一字未改）。

## 结算画面 → [`design-notes/result-screen.md`](design-notes/result-screen.md)

> 原文 118 行已搬出到上面那篇（内容一字未改）。

## 标题画面 → [`design-notes/title-screen.md`](design-notes/title-screen.md)

> 原文 38 行已搬出到上面那篇（内容一字未改）。

## 教学对话的文本描边 → [`design-notes/story-text-outline.md`](design-notes/story-text-outline.md)

> 原文 9 行已搬出到上面那篇（内容一字未改）。

## 曲末的「LIVE CLEAR!」大字 → [`design-notes/live-clear-banner.md`](design-notes/live-clear-banner.md)

> 原文 90 行已搬出到上面那篇（内容一字未改）。

## 平台 / 输入相关的坑 → [`design-notes/platform-input.md`](design-notes/platform-input.md)

> 原文 81 行已搬出到上面那篇（内容一字未改）。

## 系统要求

- Windows 7 SP1 及以上（miniaudio / SDL2 兼容底线）。OpenGL 3.3 core（约 2008 年后的 GPU 均可）。
  **CPU 要求 = x86-64 基线本身**（SSE2；2003 年 x86-64 出现之后的任何 64 位 CPU：Athlon 64 /
  Pentium 4 EM64T / Core 2 / K8~K10 / Atom 都行），这是 `-mcpu=baseline` 刻意保住的，实测见
  [`design-notes/cpu-baseline.md`](design-notes/cpu-baseline.md)。注意**最容易卡住的不是 CPU，而是显卡驱动（GL 3.3）**，以及我们只发 x64
  （32 位系统 / XP 免谈）。
- SMTC（媒体浮层/任务栏媒体控件）与任务栏进度条：SMTC 走 `RoGetActivationFactory`，**实际只在
  Windows 10+ 生效**（Win7/8 上 combase 的激活会失败，代码里已容错，只是不显示）；任务栏进度条
  ITaskbarList3 在 Win7+ 均可用。
- **Win7 上还需要 UCRT**（`api-ms-win-crt-*.dll` + `ucrtbase.dll`）：Win8 起系统自带，Win7 SP1
  得装一次 KB2999226 / VC++ 2015-2022 运行库，或者随包带一份 app-local 的 UCRT（见 [`design-notes/win7.md`](design-notes/win7.md)）。
  Win10+ 什么都不用装，exe + SDL2.dll 就能跑。
- **玩家数据只有一个文件 `userdata.json`**（`game::userDataPath()`）：优先放
  `<exe>\..\userdata.json`——也就是有 `charts\` 的那一层（build/ 布局下 = 仓库根），
  这样 `rm -rf build` 不会丢、换机器把这份文件拷到 `charts/` 旁边成绩就回来了；
  没找到 `charts\` 才落到 `<exe>\userdata.json`。内容 `{settings, scores}`：
  settings 是 noteSpeed/seVolume/offsetSec/leadInSec/windowMode/fpsLimit/判定三窗/strictFlick，
  scores 按**谱面文件名**做 key（与绝对路径无关，所以重下同样的谱成绩能对上）。
  命令行参数 > userdata.json > 内置默认（`*Given` 标志记录哪些来自命令行）。
  **它被 .gitignore 忽略**（个人成绩，不是源码）。`--screenshot` 模式不会写这个文件。

## CPU 基线：老机器上的 `0xC000001D` → [`design-notes/cpu-baseline.md`](design-notes/cpu-baseline.md)

> 原文 75 行已搬出到上面那篇（内容一字未改）。

## Windows 7 兼容 → [`design-notes/win7.md`](design-notes/win7.md)

> 原文 53 行已搬出到上面那篇（内容一字未改）。

## 「透明（Aero 玻璃）」选曲背景 → [`design-notes/aero-transparent.md`](design-notes/aero-transparent.md)

> 原文 34 行已搬出到上面那篇（内容一字未改）。

## 拖动窗口 / 改窗口大小 → [`design-notes/window-drag.md`](design-notes/window-drag.md)

> 原文 158 行已搬出到上面那篇（内容一字未改）。

## DPI / 高 DPI 显示器 → [`design-notes/dpi.md`](design-notes/dpi.md)

> 原文 57 行已搬出到上面那篇（内容一字未改）。

## 窗口外观 → [`design-notes/window-appearance.md`](design-notes/window-appearance.md)

> 原文 235 行已搬出到上面那篇（内容一字未改）。

## UI 音效 → [`design-notes/ui-sounds.md`](design-notes/ui-sounds.md)

> 原文 33 行已搬出到上面那篇（内容一字未改）。

## 手柄 / 音量 / 结算配色 → [`design-notes/gamepad-volume-colors.md`](design-notes/gamepad-volume-colors.md)

> 原文 61 行已搬出到上面那篇（内容一字未改）。

## 账户 / 等级 → [`design-notes/account-level.md`](design-notes/account-level.md)

> 原文 41 行已搬出到上面那篇（内容一字未改）。

## 多用户 / 渲染模式 / 谱面目录 → [`design-notes/multi-user.md`](design-notes/multi-user.md)

> 原文 41 行已搬出到上面那篇（内容一字未改）。

## 下载器 → [`design-notes/chartdl.md`](design-notes/chartdl.md)

> 原文 46 行已搬出到上面那篇（内容一字未改）。

## 手柄 / 单实例 / 触摸 flick → [`design-notes/gamepad-single-instance.md`](design-notes/gamepad-single-instance.md)

> 原文 40 行已搬出到上面那篇（内容一字未改）。

## 手柄焦点环 / 震动 → [`design-notes/gamepad-focus-ring.md`](design-notes/gamepad-focus-ring.md)

> 原文 75 行已搬出到上面那篇（内容一字未改）。

## 贴图瘦身 / UTF-8 路径 / 扫描开销 → [`design-notes/assets-utf8.md`](design-notes/assets-utf8.md)

> 原文 74 行已搬出到上面那篇（内容一字未改）。

## 初始血量 / Flick 调试日志 → [`design-notes/initial-life.md`](design-notes/initial-life.md)

> 原文 38 行已搬出到上面那篇（内容一字未改）。

## 「Flick 视作 Tap」 → [`design-notes/flick-as-tap.md`](design-notes/flick-as-tap.md)

> 原文 20 行已搬出到上面那篇（内容一字未改）。

## 弱化打击特效 → [`design-notes/weak-effects.md`](design-notes/weak-effects.md)

> 原文 55 行已搬出到上面那篇（内容一字未改）。

## 右键 / 触摸长按 / 自定义键位 → [`design-notes/right-click-keymap.md`](design-notes/right-click-keymap.md)

> 原文 41 行已搬出到上面那篇（内容一字未改）。

## 原生菜单栏 → [`design-notes/native-menu.md`](design-notes/native-menu.md)

> 原文 86 行已搬出到上面那篇（内容一字未改）。

## 与 Windows 的接缝 → [`design-notes/windows-integration.md`](design-notes/windows-integration.md)

> 原文 127 行已搬出到上面那篇（内容一字未改）。

## 多人游玩 → [`design-notes/multiplayer.md`](design-notes/multiplayer.md)

> 原文 189 行已搬出到上面那篇（内容一字未改）。

## 确定闪光 / 猜歌 → [`design-notes/decide-flash-guess.md`](design-notes/decide-flash-guess.md)

> 原文 76 行已搬出到上面那篇（内容一字未改）。

## ELUA → [`design-notes/elua.md`](design-notes/elua.md)

> 原文 15 行已搬出到上面那篇（内容一字未改）。

## 空谱面时的下载按钮 → [`design-notes/empty-chart-download.md`](design-notes/empty-chart-download.md)

> 原文 15 行已搬出到上面那篇（内容一字未改）。

## 暂停必须冻住主机时钟 → [`design-notes/pause-clock.md`](design-notes/pause-clock.md)

> 原文 13 行已搬出到上面那篇（内容一字未改）。

## 难度槽位解析失败 → [`design-notes/difficulty-slot.md`](design-notes/difficulty-slot.md)

> 原文 27 行已搬出到上面那篇（内容一字未改）。

## 日志缓冲 → [`design-notes/log-buffer.md`](design-notes/log-buffer.md)

> 原文 10 行已搬出到上面那篇（内容一字未改）。

## 触摸特效（TapEffect） → [`design-notes/touches-effect.md`](design-notes/touches-effect.md)

> 原文 118 行已搬出到上面那篇（内容一字未改）。

## 待办（按优先级）

0. **Win7 的 UCRT 怎么给**：随包带 app-local UCRT / 要求装运行库 / 干脆放弃 Win7 三选一，
   见 [`design-notes/win7.md`](design-notes/win7.md) 最后一节（`GetSystemTimePreciseAsFileTime` 那条已经修好了）。
1. hold 音效循环（SeHoldLoop 未接）与 SE kind 区分（当前键盘全播一个音）
2. 输入/音频延迟校准界面
3. 连击特效（judge v3 的 1~5 已用于判定文字，6=AUTO 仍未用）
4. 键盘 12 键布局可能不顺手，考虑做成可配置；键盘也打不了 left/right flick（只能发 FlickUp），
   要么给按键加"按住+方向键"的组合，要么引导玩家用鼠标/触摸
5. 【**已实现** 2026-09-22，下面这段是当初的设计稿，实际走的是官方 `music-vocals.json`
   表而不是 `<id4>__<tag>` 文件名 —— 见 [`design-notes/songselect-scroll.md`](design-notes/songselect-scroll.md) 的「切换歌手面板」】歌手 / 音源版本选择。同一首歌的 `SEKAI ver.` / `VIRTUAL SINGER ver.` /
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

> 已实现的旧待办：flick 严格方向校验（见 [`design-notes/conventions.md`](design-notes/conventions.md) 里的说明）、hold 尾判（松手判定）、
> HUD 真实分数与血量、放弃后重选曲卡死、**结算画面**（见 [`design-notes/result-screen.md`](design-notes/result-screen.md)）、
> **歌手 / 音源版本选择**（见 [`design-notes/songselect-scroll.md`](design-notes/songselect-scroll.md) 的「切换歌手面板」——最后没走这条
> `<id4>__<tag>` 文件名方案，而是直接吃官方 `music-vocals.json` 表 + 官方资源名
> `se_/vs_/an_/cl_<id>_<n>.mp3`，见 CHARTS.md）。
## 谱面坐标与时间轴 → [`design-notes/chart-coordinates.md`](design-notes/chart-coordinates.md)

> 原文 50 行已搬出到上面那篇（内容一字未改）。

## 素材与版权：给 AI 的结论（不用再想）

- **素材随便用、随便进 git。** 官方素材、官方数据表、解包产物都直接入库，
  不必先问、不必每轮提醒、不必为了"避嫌"绕路或降级方案。
  [COPYRIGHT.md](COPYRIGHT.md) / [CREDITS.md](CREDITS.md) 只作为**来源台账与背景资料**保留。
- **唯一要保持的动作**：`package.sh` 不把 `assets/`、`Drafts/`、`docs/` 打进 Release 包。
  脚本已经是这个行为，**不用改代码、不用讨论**。
- 代码侧唯一实质义务来自 AGPL-3.0-only：改动保持开源。别再为"能不能发 exe"之类的问题停下来。
