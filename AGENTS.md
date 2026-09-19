# AGENTS.md — 给 AI 助手的项目指南

CppSekai：Project SEKAI 风格 SUS 谱面 Windows 原生游玩器。
上游是 [sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web)（AGPL-3.0），
其谱面核心从 MikuMikuWorld（MIT）移植。**本仓库整体遵循 AGPL-3.0-only，改动必须保持开源。**

配套文档（改代码时按需查）：
- `CODE-REVIEW.md` —— **代码体检（2026-09-18）**：体量分布、巨型函数清单、
  按改动成本排的处置顺序。**动手前扫一眼第二节**，能省很多定位时间。
- `CREDITS.md` —— 借用清单：每个来源是谁的、什么许可、放在哪（含 Fontworks 字体这条独立风险）。
- `COPYRIGHT.md` —— 版权与风险：什么能发、什么不能发。

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
                  # 判定窗口全部走 `JudgementWindows`：perfect/great/good 之外还有 badMs
                  # （迟按还能算 BAD 的边界）和 missAfterMs（没人碰的音符自动 MISS 的时刻），
                  # 设置 → 判定 里两个都能量；勾上「Bad 与 Miss 同步」时 missAfterMs = badMs
                  # （老行为），不勾就各管各的。启动打一行
                  # `[settings] windows perfect=.. bad=.. missAfter=.. holdTail=.. linked=..`，
                  # 判定手感不对先看它。
                  # 分数/血量：分数用上游 TEAM_POWER/weightedCount/comboFactor 公式
                  # （kTeamPower 等常量见 Judgement.hpp），血量 1000 起，整音 MISS -80、长条中断 -40。
game/Ui.*         # pjsk 风格弹窗组件库：beginCard（缩放入/出场动画 + 标题栏拖动）、
                  # tabBar、slider（深色±按钮+薄荷轨道）、infoRows、capsuleButton、
                  # cardTitle、checkBox、stepper、messageDialog（-3=动画中 -2=关闭完成）、
                  # eulaDialog（关于本软件的首次启动弹窗，见下面「ELUA」一节）、
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
                  # 详细测量笔记见下面「结算画面」一节。
game/Intro.*      # ImGui 卡片/UI；字体**只走系统**（注册表找字体文件 + 按文件名兜底 +
                  # 日文/简中字形探测。Yu Gothic UI 是 CFF 轮廓，stb_truetype 渲染不了，
                  # 会自动落到 Microsoft YaHei）。2026-09-19 删掉 assets/mmw/font 后
                  # --pjsk-font 也没了，候选表与排错见「约定与坑」里那两条字体说明）
game/SongSelect.* # 选曲界面 + userdata.json 读写（settings / scores / account 三段）+ 等级曲线。
                  # 账户 / 等级 / 资料卡见下面「账户 / 等级」一节。
platform/Party.*  # 多人游玩（同机多窗口联机）的共享内存总线：命名文件映射 + 每实例一个座位，
                  # 主机选举、心跳、锁曲、难度、确定、绝对起奏时刻、暂停广播、实时分数、
                  # 本局曲长（成员靠它和房主同时切结算）。
                  # 无 socket / 无管道 / 无序列化：一次状态变更就是往共享页写一个 LONG。
game/PartyScreen.* # 多人游玩的浮层：选曲界面左下角的房间条（谁在房里）与演奏中的队友分数条。
                  # 房间自己没有页面——它就在选曲界面上（见「多人游玩」一节）。
                  # 难度名/序号（`difficultyIndex` / `difficultyName`）也在这，房间协议与手机面板共用。
main.cpp          # SDL2 窗口、事件循环、输入映射、ImGui HUD、截图模式、多人时钟跟随
```

多人游玩（`--party` / `--no-party`，房间在选曲界面上）见下面「多人游玩」一节。

数据流：`loadSusTextPrecise → render(t) → packedQuads → Renderer::renderFrame`；
判定侧：`getHitEventBuffer → JudgementEngine::load → tap/flick/update`。

### 关键数据格式

- packed quad（25 floats）：4×(x,y,reciprocalW) + 4×(u,v) + rgba + textureId。
  textureId 0=notes 1=longNoteLine 2=touchLine（世界坐标，需 worldToClip 变换）；
  ≥3 = effect.png（已是裁剪空间坐标，id==4 为加法混合）。
- packed HitEvent（7 floats）：timeSec, center(轨道坐标), width, kind, flags, endTimeSec, volume。
  kind：0=tap 1=critical tap 2=flick 3=trace 4=hold tick(自动) 5=hold 标记(endTimeSec 有效)。

## 代码体量（2026-09-18 实测，改大东西前看这里）

原创代码 23,840 行。**问题不在文件多，在两个巨型函数**：

| 位置 | 行数 | 说明 |
|---|---|---|
| `main.cpp` → `main()` | **5,713**（`:730` 起） | 参数解析 + 初始化 + 启动决策 + 帧循环 + 关停全在一个函数 |
| `main.cpp` 帧循环体 | ~2,500（`:3989` 起） | `if/else if (state == ...)` 串起 Select/Play/Result，三者变量共享作用域。**2026-09-19 起这段正文被 `runFrame` lambda 包住**（拖动窗口时要从消息钩子里重入，见「拖动窗口」一节），行数与作用域都没变 |
| `main.cpp` → `drawSettingsCard` lambda | ~769（`:2684-3453`） | 4 个页签用 `if (tab == N)` 展开 |
| `game/SongSelect.cpp` → `drawSongSelect()` | ~1,746（`:2104` 起） | 13 参数含 5 个 in/out 引用（`selected`/`sortMode`/`groupMode`/`vocalIndex`/`confirmCenter`/`partyOut`） |

**健康的部分**（别顺手"优化"）：`game/` `platform/` 分层清楚，绝大多数文件 ≤1000 行；
模块级可变全局全项目只有 9 个（`main.cpp` 3 个），其余文件级状态都关在匿名 namespace 里；
**0 个 TODO/FIXME**；注释质量高（每个非显然决定都写了理由与踩过的坑）——注释是这仓库
最值钱的东西，重构时**跟着搬，别丢**。

详细清单与处置顺序见 **`CODE-REVIEW.md`**。

---

## 工具

- `downloader/chartdl.cpp` → `build/chartdl.exe`：**独立的谱面下载器**（不算游戏的一部分，
  build.sh 里单独编一次）。界面是**纯 Win32 通用控件**（ListView 带 checkbox + Edit +
  ProgressBar + ListBox，全中文，不共享 ImGui/SDL），链接 `-Wl,--subsystem,windows`，
  命令行模式自己 `AttachConsole(ATTACH_PARENT_PROCESS)` 把父进程控制台借回来（已经拿到
  管道句柄时不能抢，否则 `chartdl.exe --list | head` 输出会跑去 console）。
  HTTP 走 `winhttp.dll` **运行时 LoadLibrary**（工具链只有 winhttp.def，
  没有导入库——和 SystemMedia 用 combase 的办法一样）。下载在 `std::thread` 里跑，进度用
  `gJobMutex` 保护；**别在 worker 里持有 `gJobs[i]` 的引用**（GUI 线程还会 push_back，会悬空）。
  **2026-09-19 起是 4 个 worker**（`kDownloadThreads`）。worker 本来就是「取一个 Waiting job
  就下」的模式，但两处必须配套：① `gRunning` 只能在**最后一个** worker 退出时置 false
  （`gActiveWorkers.fetch_sub(1) <= 1`），否则先下完的线程会提前报"全部完成"；
  ② `http::get()` 把 **WinHTTP session + connection 缓存在 `thread_local`** 里复用，
  不再每个文件 `WinHttpOpen` + 重新握手（连接被服务器半关时 worker 重试一次：丢缓存连接 +
  截断 `.part` 重下；`HTTP xxx` 是最终答案，不重试）。实测 31 文件 / 63.9 MB：**19.0s → 9.8s**。
  **进度条按「文件数」算，不是字节**：入队时每个 job 的 Content-Length 还是 0（要等响应头），
  按字节算会让进度条在还没开始下载时跑到 100%。剩余时间用同一个比例推，两者同源不会打架。
  点「开始下载」会**清空 gJobs**（原来只追加，旧条目 Done 了还在参与 `xx/xx` 的统计）。
  **窗口坑（真 bug，已修）**：设置窗口是 `WS_OVERLAPPED` **顶层**窗口 + owner，而
  `GetParent()` 对 owner 返回 **0** → `WM_DESTROY` 里的 `EnableWindow(主窗口, TRUE)`
  从来没执行过，关掉设置后整个下载器点不动（只有系统提示音）。改用
  `GetWindow(hwnd, GW_OWNER)`。ListView 那几处 `GetParent(gList)` 是对的，别跟着改。
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
  `winmsg.exe <class> list|alive|gettext <id>|settext <id> <utf8>|char <id> <hex>|click <x> <y>|raw <hex 消息> [wparam] [lparam] [--pid N]`。
  编译（不进 build.sh）：
  `zig c++ -x c++ -std=c++20 -O2 -s .workbuddy/tools/winmsg.c -luser32 -limm32 -o build/winmsg.exe`。
  注意 `GetWindowText` **读不到别的进程的控件文本**（跨进程只拿得到窗口标题），别拿 `gettext`
  当功能验证；`char` 走的是 WM_CHAR，ImGui 那类读 SDL 事件的界面要用 `click`。
  `raw` 是给「没有控件对应、只能靠窗口消息驱动」的状态用的，SDL 的窗口类名是 `SDL_app`：
  `winmsg.exe SDL_app raw 0231 --pid <pid>` = 伪造 WM_ENTERSIZEMOVE，用来测拖动窗口那条暂停逻辑
  （见「拖动窗口 / 改窗口大小」一节）。**PostMessage 的消息同样会经过 SDL 的窗口过程**，
  所以消息钩子照样会被调用，能无头验证。
- `.workbuddy/tools/gen_music_vocals.py` → `music-vocals.json`：从官方的 musicVocals +
  gameCharacters 表生成演唱版本表（`asset` 就是 unipjsk 的音频目录名）。
- `.workbuddy/tools/winsend.c` → `build/winsend.exe`：按窗口标题找窗口再送假输入，
  无交互会话下驱动 UI（动作：`click x y` / `move x y` / `key <vk>` / `focus` /
  `place x y` / `rect`；见「平台 / 输入相关的坑」）。
- `.workbuddy/tools/asset_audit.py` → **素材清点**：把各 loader 里点名的路径当成清单，
  列出 assets/ 里游戏永远不会读的文件（`--list` 打全表，`--paths` 只打路径给脚本用）。
  文件顶部的 `KEEP` 是"手放进来、暂时没接线但有意留着"的白名单（整个 `assets/se/`
  都在里面），加进去的东西永远不会出现在"可以删"清单里。
  配套 `.workbuddy/tools/asset_prune_verify.sh`：在 build/_prune/ 造一份副本、按清单删干净，
  再跑选曲/演奏/结算/暂停四个模式检查日志有没有加载失败（**不碰仓库里的 assets/**）。
  **2026-09-19 已经真删过了**：461 个 / 43.3 MB（19.8 MB 未使用 + 23.6 MB 字体目录），
  assets/ 从 56.1 MB 降到 11 MB。删完四个模式零加载失败。
- `.workbuddy/tools/shrink_assets.py` → **素材压缩**（`--apply` 才写，原图先备份到
  `.workbuddy/backup/assets-<日期>/`）。两趟，区别就是全部意义：
  `RESIZE` 只碰"绘制尺寸由 C++ 写死、且按整图/分数 UV 采样"的文件（现在只有
  `overlay_opt/life/v3/digit/*.png`：333x444 的图、屏幕上按 34px 画，烘到 128）；
  `LOSSLESS` 是对其余**所有** loader 会读的 PNG 做 `optimize=True` 重编码
  （顺带丢掉全不透明的 alpha 通道），像素逐位相同所以渲染不可能变。
  `NEVER` 里的精灵图集（`notes*` / `effect.png` / `longNoteLine*` / `touchLine*`）
  两趟都不碰——它们的精灵矩形是**像素坐标**写死在
  `core/native/generated/generated_resources.h`，缩放会让所有音符错位。
  2026-09-19 实测：223 个文件 9.70 → 6.77 MB（-30%）。
  验证方法是逐像素比对同一帧（**注意：一批里的第一次运行会明显不同**，那是
  舞台背景/着色器冷启动，别误判成改动导致的差异；同一版本连跑两次应当 0 差异）。
- `.workbuddy/tools/mp_verify.sh` → 多人游玩的端到端回归：开两个窗口（`--party-auto`），
  断言同一 `start counter`、BGM 只在主机、时钟偏差、实时分数过进程、房主暂停后成员画面钉住、
  **打到结算画面（两边同一 chart time）并回到选曲、房间重新武装**。
  纯文本 PASS/FAIL，不需要看截图（三轮约 4 分钟）。

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
  2026-09-19 细节调整：页签高 50→**42**、页签圆角 14→**10**；`ui::stepper` 胶囊高 62→**50**
  （判定页「长条容错」那一排原来像三块厚板子）。都在 `game/Ui.cpp` 顶部/各自函数里，改数值即可。
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
- 无头看对话框：【`--settings --settings-tab <0-4>`】打开设置卡（页签 0 演奏 / 1 画面 / 2 判定 /
  3 系统 / 4 账户）；`CPSEKAI_MULTIASK=1` 启动即弹「开启多开？」确认框（点 combo 是唯一其它入口，
  而 PostMessage 假点击进不了 ImGui 按钮）。配合 `CPSEKAI_CARD_T` 抓入场中间帧。
- 设置卡片 360x640、四个页签（演奏 / 画面 / 判定 / 系统）；`--settings` + `--settings-tab <0-3>`
  无头打开（按键没法送进无头运行），配合 `--screenshot` 截图。
  **页签内容放在一个裁剪用的 `BeginChild` 里**：「画面」页比卡片高，多出来的行会钻到「关闭」
  按钮底下（按钮后提交，把点击全吃掉）。这个 child 的末尾**必须补一句 `ImGui::Dummy`**——
  `ui::checkBox()` 最后一条是裸的 `SetCursorScreenPos`，child 作为当帧最后一个窗口时
  `EndChild()` 会弹 "SetCursorPos ... to extend window/parent boundaries" 断言。
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

**2026-09-19 改版（用户点名的四条）**：
1. **自绘装饰全删**：背景 wash（`AddRectFilledMultiColor`）、6 个装饰框线、两条斜带
   （`AddTriangleFilled`）。背景只剩 `renderFrame()` 画的 `background_overlay.png`
   （紫蓝渐变**带彩色图案**，别以为它是纯色）。
2. **不画舞台**：`main.cpp` 的 `AppState::Result` 分支传 `playfieldVisibility = 0.0f`，
   renderFrame 画完 background 就 return。
3. **判定计数 + COMBO 计数改用系统字体**（原来是 condensed 窄体）；`cond` 只剩 score-bar
   的 C/B/A/S 标记。`drawFontDigits` 的 `advance` 只是槽宽、字形在槽内居中，所以锚点不动。
4. **RESULT 大字只留 border**：`platform/FontOutline.cpp` 用 stb_truetype 光栅化再取
   「圆形膨胀 − 原覆盖」得到**真空心**轮廓，`Renderer::createTextureFromRgba()` 上传，
   结算时 `AddImage`（960x210，一次构建约 0.77 MB）。
   **不要改回"描边 + 背景色挖空"那一套** —— 挖空色必须精确等于背景色，而背景是带图案的
   图片，任何近似都露出色块。

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
- **Win7 上还需要 UCRT**（`api-ms-win-crt-*.dll` + `ucrtbase.dll`）：Win8 起系统自带，Win7 SP1
  得装一次 KB2999226 / VC++ 2015-2022 运行库，或者随包带一份 app-local 的 UCRT（见下节）。
  Win10+ 什么都不用装，exe + SDL2.dll 就能跑。
- **玩家数据只有一个文件 `userdata.json`**（`game::userDataPath()`）：优先放
  `<exe>\..\userdata.json`——也就是有 `charts\` 的那一层（build/ 布局下 = 仓库根），
  这样 `rm -rf build` 不会丢、换机器把这份文件拷到 `charts/` 旁边成绩就回来了；
  没找到 `charts\` 才落到 `<exe>\userdata.json`。内容 `{settings, scores}`：
  settings 是 noteSpeed/seVolume/offsetSec/leadInSec/windowMode/fpsLimit/判定三窗/strictFlick，
  scores 按**谱面文件名**做 key（与绝对路径无关，所以重下同样的谱成绩能对上）。
  命令行参数 > userdata.json > 内置默认（`*Given` 标志记录哪些来自命令行）。
  **它被 .gitignore 忽略**（个人成绩，不是源码）。`--screenshot` 模式不会写这个文件。

## Windows 7 兼容（2026-09-19 实测）

Win7 SP1 上启动直接弹 **「无法定位程序输入点 GetSystemTimePreciseAsFileTime 于动态链接库
KERNEL32.dll 上」**，一行业务代码都不执行。根因不在业务代码，在工具链：

- zig 自带 libc++（`toolchain/…/lib/libcxx/src/chrono.cpp`）编译时 `_WIN32_WINNT=0x0a00`，
  于是 `std::chrono::system_clock::now()` 走 `#if _WIN32_WINNT >= _WIN32_WINNT_WIN8` 分支，
  **静态导入** `GetSystemTimePreciseAsFileTime`（Win8+ 才导出）。这个导入是 libc++ 内部的，
  连只 `#include <iostream>` 的空程序都会中招 —— 所以**没法在业务代码里绕开**（实测：
  项目里一处 `system_clock` 都没用）。
- `-D_WIN32_WINNT=0x0601` 没用：它只影响我们自己的 TU，libc++ 是 zig 按自己的规则预编译的。
- 同名强符号顶掉导入也不行：zig 的 windows-gnu 链接是「直接拿 DLL 当输入」，
  会报 `duplicate symbol: GetSystemTimePreciseAsFileTime ... defined at KERNEL32.dll`，
  而且 zig 不认 `--allow-multiple-definition`。

**修法（已落地，在 `build.sh` 顶部）**：构建前幂等地给那份 `chrono.cpp` 打两处小补丁，强制走
libc++ 自带的「运行时探测」分支（`GetProcAddress` 找得到就用精确时钟，找不到退回
`GetSystemTimeAsFileTime`）。Win8+ 行为完全不变（实测精度仍是微秒级，`gap_us=0`），Win7 退到
15ms 粒度 —— 游戏计时走 QPC，只影响 `std::chrono::system_clock`。补丁后有
`grep -c CPPSEKAI-WIN7 = 2` 的断言，打不上就 `exit 1`，不会静默产出 Win7 打不开的 exe。
`toolchain/` 不入库，所以补丁必须留在 build.sh 里，重新解压 zig 也能自愈。

**复查手法**（`.workbuddy/tools/pe_imports.py`，纯 stdlib 的 PE 导入表解析）：

```bash
python .workbuddy/tools/pe_imports.py build/cppsekai.exe build/chartdl.exe build/SDL2.dll
# 期望：三个都是「未发现已知 Win8+ 独占导入」
```

注意别用 `strings | grep` 判断：libc++ 的运行时探测分支里**还留着那个名字的字面量**（喂给
`GetProcAddress`），会永远命中；`grep -c` 判断是否为静态导入必须是**看导入表**。
`GetFileInformationByHandleEx` / `SetFileInformationByHandle` 是 **Vista** 就有的，不是坑。

已经查过、没问题的：`SDL2.dll` 导入表干净（msvcrt + Vista 级 API），其余 94 个 KERNEL32 导入
（SRWLock / FlsAlloc / InitOnceExecuteOnce / GetTickCount64 / CreateSymbolicLinkW / RtlVirtualUnwind…）
全是 Vista 基线。SMTC 那套本来就有 Win10 门的容错。

### 还没解决的：Win7 缺 UCRT

exe 静态导入 `api-ms-win-crt-{runtime,stdio,string,math,heap,locale,convert,time,environment,
multibyte,utility,private}-l1-1-0.dll`。Win8 起由系统 API set 解析到 `ucrtbase.dll`（所以
System32 里根本看不到这些文件，Win10 上一直无事），**Win7 上没有这套 API set**，必须真文件在。
三条路，任选：

1. 让用户装一次 VC++ 2015-2022 运行库 / KB2999226（最省事，但 README 那句「不需要安装运行库」
   在 Win7 上就不成立了）；
2. 随包带 app-local UCRT：`ucrtbase.dll` + 12 个 `api-ms-win-crt-*.dll` 放 exe 旁边
   （MS 支持的部署方式，来源是 Windows SDK 的 `Redist\ucrt\DLLs\x64`，约 1.5MB，对 66MB 的包
   不痛不痒）；`package.sh` 加一段拷贝即可；
3. 放弃 Win7（要动 README / COPYRIGHT 里的最低系统要求）。

**待用户拍板**，暂时只在文档里写明前置条件。

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

## 拖动窗口 / 改窗口大小 → 画面卡住、歌却继续跑 2026-09-19

**前提是经典说法**：Windows 在 `DefWindowProc` 里为标题栏拖动和边框缩放跑一个**自己的模态消息
循环**，从 `WM_NCLBUTTONDOWN` 到松手之间我们的消息泵（`SDL_PollEvent` → `DispatchMessage`）
整段被挂起 —— 主循环一帧不跑、画面冻住，而音频在 miniaudio 自己的线程里照放、谱面钟骑在音频
上，松手瞬间钟表往前跳、中间的音符全被判 MISS。
**但这个前提 2026-09-19 在 Win11 上实测没有成立**，见下面「实测记录」——先读那节再改代码。

**已落地（方案 a，帧体抽成 lambda + WM_TIMER 喂帧）**：`main.cpp` 里
`SDL_SetWindowsMessageHook` 加上一个 16ms 的 `SetTimer`。那段模态循环里**系统照样会给窗口
过程投递 WM_TIMER**，而 SDL 的钩子在它处理任何消息之前被调用，于是拖动期间一个 tick 出一帧，
直接从那里面跑。实测拖动期间能稳定出帧（模拟测试 6 秒出 148 帧），**画面继续动、谱面钟继续走，
不再需要暂停**（上一版那个"静默暂停"已删掉 —— 它只是因为当时没法画才存在）。

实现键点（改这段之前先读完，容易踩）：

1. **帧体包成 `auto runFrame = [&]() { for (bool once = true; once; once = false) { …正文原样… } };`**，
   主循环变成 `while (running) { runFrame(); }`。外面那层**只跑一次的 for 是必须的**：
   正文里的 `continue` / `break` 在编译器眼里是"下一轮这个循环 / 离开这个循环"，
   套一层单次循环后 `continue` = 离开包装 = 一帧结束（正好等于原来对主循环 `continue` 的效果），
   `break` 同理；而三处真正要结束主循环的地方都是先 `running = false`，条件紧接着在调用后检查。
   **正文一个字都没改** —— 别"顺手重构"里面那 2500 行。
2. **重入保护是 `depth`**：喂进去的这一帧自己会泵消息（它就是正常的帧体，`SDL_PollEvent` 全在里面），
   于是它可能再派发下一个 WM_TIMER —— 那个必须丢掉，不能变成第二层嵌套帧。
3. `WM_ENTERSIZEMOVE` 开表 + 计数清零，`WM_EXITSIZEMOVE` 关表，进出都打
   `[window] WM_ENTERSIZEMOVE: … serving frames from a 16 ms timer` /
   `[window] WM_EXITSIZEMOVE: N frame(s) served during the drag` —— **N 是"拖动期间画面有没有在动"
   的直接证据**。
4. 定时器只挂在拖动/缩放期间，平时完全不存在，所以普通帧循环的行为与以前逐字节一致。

验证（`winmsg.exe` 的 `raw` verb，PostMessage 的消息**同样会走 SDL 的窗口过程**，所以钩子能被无头驱动）：

```bash
# 单机进演奏（--no-party 必须加：多人默认是开的；另外 --sus 的路径相对 CWD，
# build/charts/ 里没有 test.sus，要从仓库根跑 charts/test.sus）
build/cppsekai.exe --sus charts/test.sus --auto --no-party --lead-in 1     --screenshot build/shots/x.png --screenshot-time 6 &
build/winmsg.exe SDL_app raw 0231 --pid <pid>   # 伪造 WM_ENTERSIZEMOVE
sleep 6
build/winmsg.exe SDL_app raw 0232 --pid <pid>   # 伪造 WM_EXITSIZEMOVE
```
判别指标用「到谱面时间 6s 的墙钟耗时」：**12.97s（不拖）→ 12.69s（"拖"6 秒）**，即拖动期间
谱面钟照走（上一版静默暂停时这个差值是 +5.13s，正好等于拖动时长）。
**这个测试比真实拖动还狠**：外层循环同时也在跑，等于 148 个重入帧与外层帧并发，没崩没花。
真拖动时外层被系统挂住、只有这一条渲染路径，帧率只会更高（模拟里 148 帧/6s 是被双重渲染拖慢的）。

**还没做**：拖**边框缩放**走的同一条模态循环、同一个定时器，但缩放期间窗口尺寸在变，
`SDL_PollEvent` 拿到的 `WM_SIZE` 会带着新尺寸重建 FBO —— 这条路径没人真的拖过边框验证。

### 实测记录与真正的修法 2026-09-19（Windows 10 22H2 + Windows 11）

用户报"拖动窗口画面不会刷新"。为了不再靠猜，加了两个工具和一个常驻探针，结论如下。

**1. 拖动确实把消息泵挂住了 —— 我中途读错过一次数据。** 常驻探针（每秒一行，只在有
变化/长帧时打印）拖动期间给出：

```
[frame] geometry changed: 56 frame(s)/s, longest frame 2264 ms, window moved 392 px
```

`longest frame ≈ 拖动时长`（2.1s 拖动 → 2264ms）就是"整整一帧被卡在拖动里"的签名。
**教训**：`[sync]` 那类"每秒一行"的日志**看 qpc 跳变，不要看行数** —— 我当时因为 t 值
连续、行数没少，误判成"泵没被挂住、只是帧率掉"，白绕了一圈。

**2. `SDL_SetWindowsMessageHook` 在这件事上没用。** 查 SDL2 源码
（`src/video/windows/SDL_windowsevents.c`）：那个钩子是在 **`WIN_PumpEvents`（SDL 自己的
事件泵）** 里调的，**不在窗口过程里**。模态循环期间我们的泵不跑，钩子就永远不会被调用 ——
实测全程 24 条消息，没有 `WM_ENTERSIZEMOVE`、没有 `WM_MOVING`。
**这条推翻了我之前"钩子能在模态循环里喂帧"的设计**（那版代码只在伪造消息的合成测试里跑通，
真实拖动一次都没触发）。
**但伪造消息的测试仍然有用**：它能证明"子类化后的窗口过程确实进了消息链"（30 秒验证）。

**3. 现在走窗口子类化**（`main.cpp`，`runFrame` 定义之后那一块）：

- `SetWindowLongPtrW(hwnd, GWLP_WNDPROC, ...)` 换成我们的过程，**原过程（SDL 的）存在
  `WNDPROC chain` 里，每条消息末尾 `CallWindowProcW` 转回去**，转发行为不变。
  **状态放在窗口属性里**（`SetPropW(hwnd, L"CppSekaiSubclassState", ...)`，SDL 自己也是用
  窗口属性存窗口数据的），所以没有新增模块级可变全局，也不会出现"钩子比窗口活得久"。
- 触发：`WM_ENTERSIZEMOVE` 开一个 16ms 定时器；`WM_MOVING`/`WM_MOVE`、`WM_SIZING`/`WM_SIZE`、
  我们的 `WM_TIMER` 各自喂一帧；`WM_EXITSIZEMOVE` 关表并把帧数打进日志。
- 两个保护：`depth`（喂进去的那一帧自己会泵消息，下一个 `WM_TIMER` 会落在它中间 —— 只能有一层
  嵌套），以及 8ms 的时间下限（否则每个鼠标消息都要一帧，窗口会以 16ms 为步长跟着光标走）。
- `CPSEKAI_MSG_LOG=1` 打印子类化过程看到的每条消息。

**实测（Win11，SendInput 拖 2.1 秒、280px）**：`WM_EXITSIZEMOVE: 127 frame(s) served during
the drag (moving=126 sizing=0 timer=1)` —— 拖动期间约 60fps（受 vsync 限），同时
`[frame] geometry changed: 59 frame(s)/s, longest frame 29 ms, window moved 50 px`，
最长帧从 **2264ms 掉到 19~29ms** ✓。Win10 22H2 那边等用户复测（模态循环更经典，消息一定会
送到窗口过程）。

**3b. 拖动的卡顿（2026-09-19 修）**：喂进去的每一帧都会把模态循环堵住整帧时间，所以
最初"每个 WM_MOVING 一帧 + vsync 开着"等于 97% 的时间在阻塞循环 —— 用户的原话是"拖动和 resize
会很卡"。三个改动：① 拖动期间**关掉 vsync**（`dragFramePacing`，由子类在 ENTERSIZEMOVE/
EXITSIZEMOVE 置位，帧体的 vsync 决策读它），每帧只花渲染本身的时间；② 喂帧下限从 8ms 提到
**30ms**（约 33fps），给循环留出时间跟手；③ `WM_TIMER` 里查 `GetAsyncKeyState(VK_LBUTTON)`，
**按键一松就 KillTimer + 恢复 vsync** —— 模态循环有时不给我们 `WM_EXITSIZEMOVE`（松手那条消息
会落在我们服务的那一帧里、被 SDL 的泵吃掉），没有这个收尾会留下"定时器永远在跑 + vsync 一直关"
的坑。改完实测：拖 2.1s / 280px → **63 帧（约 30fps）、`WM_EXITSIZEMOVE` 正常到达**，
探针里最长帧 50ms 上下（≈喂帧间隔，不再是整段拖动）。

**4. 常驻探针**（`main.cpp` 帧体开头，默认安静，不需要环境变量）：那一秒里窗口几何动过就打印
`[frame] geometry changed: N frame(s)/s, longest frame M ms, window moved P px`，
否则只在出现 >60ms 长帧时打印。**判据**：`M` ≈ 拖动时长 ⇒ 泵被挂住且没人喂帧；`M` 只有
几十毫秒 ⇒ 帧一直在出。这条日志是为了让"拖动时到底刷不刷新"以后能自证，不用再问用户开开关。

**5. 工具与踩过的坑**：

- `.workbuddy/tools/dragwin.c` → `build/dragwin.exe`
  （verb：`rect` 打印外框+前台窗口、`focus` 用 `AttachThreadInput` 强行抢前台 —— 后台进程直接
  调 `SetForegroundWindow` 会被系统拒绝，抢不到焦点时"拖动"拖的是压在上面的别的窗口）：**SendInput 真输入**拖窗口
  （`PostMessage` 伪造按钮状态骗不过系统，它看物理按键状态）。用法
  `dragwin.exe [rect|resize] [steps] [stepPx] [sleepMs]`、`dragwin.exe rect` 只看窗口位置。
  **两个坑**：① 后台进程的 `SetForegroundWindow` 会被系统拒 → 拖到的是压在上面的别的窗口，
  所以现在先 `SetWindowPos(HWND_TOPMOST)` 置顶、再点客户区抢焦点、**打印
  `foreground before drag: ours/NOT ours` 并回读 `GetWindowRect` 确认位移**，否则测试白做；
  ② 编译 `zig cc -x c -std=c11 -O2 -s .workbuddy/tools/dragwin.c -luser32 -o build/dragwin.exe`。
- `.workbuddy/tools/capwin.c` → `build/capwin.exe`：想抓"屏幕上的客户区"，**方法无效** ——
  `GetDC(NULL)` + `BitBlt` 抓 GPU 合成的窗口会得到**纯白**（"拖动中 0 像素差异"是假象）。
  要真抓得用 `PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT)`。**先做对照组**（不拖动时两张
  截图也该不同）—— 这一条当场拦住了一次错误结论，留着当反例。



验证（不用眼睛也能看）：`--screenshot` 写的是 RGBA PNG（`glReadPixels(..., GL_RGBA, ...)` +
`stbi_write_png(..., 4, ...)`），所以**直接量 alpha** 就知道透明生效没有 ——
选曲界面角落的 `ImGuiCol_WindowBg` 当初就是这么抓出来的（`alpha==0` 占比 0% → 24%）。

```bash
# 临时把档案切到玻璃模式再截图，量 alpha（Pillow 在隔离 venv 里）
python -c "..."                      # 改 profiles/default.json 的 bgStyle=2
./cppsekai.exe --screenshot shots/g.png --screenshot-time 4
# 注意 --screenshot 的参数是**文件路径**（不是目录！给目录会静默写失败）
```

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

**降 GL 版本不会有性能收益**（2026-09-19 实测，用户问 "GL3.1 性能会不会比 3.3 好"）：GL 版本号
是**能力集**不是性能档，同一驱动的光栅化路径一样。我们本来就已经在 **3.3 core profile** 上
（`SDL_GL_CONTEXT_PROFILE_CORE`，main.cpp:1108 起），core 才是这附近唯一真正影响性能的开关
（免掉兼容 profile 的旧状态校验）。实测把垂直同步关掉、上限抬到 240：**1280x720 → 214 fps、
1920x1080 → 219 fps** —— 像素翻倍帧率不动，说明瓶颈在 CPU 侧（每帧的提交/绘制调用），
**根本不在填充率或 GL 特性**上。降到 3.1 只会丢功能（sampler object、显式 attrib location…
顺带一提我们的 shader 其实没用 `layout(location=)`，ImGui 后端也自带 pre-3.3 分支），
外加为此改一遍代码，换 0 fps。

## UI 音效（2026-09-15，`ui::se` / `ui::flushSe`）

- 素材在 `assets/se/`：`click.mp3`（任意组件按下）、`select.mp3`（选曲列表每动一格）、
  `level_choose.mp3`（难度按钮）、`window_open.mp3` / `window_close.mp3`（卡片 / 弹窗）、
  `start.mp3`（点「确定」起白光的那一下，`SeStart`，优先级最高）。
  `AudioEngine::loadUiSe` 在 `loadSe` 末尾加载（每种 3 个声部），**缺文件只打印一行日志**，
  不报错——所以精简包 / 无素材时界面照样能跑，只是没声音。
  加一种新音效要**同步改三处**：`ui::SeKind`（末尾追加 = 优先级最高）、
  `platform::AudioEngine::UiSe`（同样的顺序，`flushSe` 直接 `static_cast`）、
  `loadUiSe` 里的 `kFiles[]`，以及 `Ui.cpp` 顶部的 `kSeKindCount`。
- 播放**不在按下瞬间**：组件只 `ui::se(...)` 记一个请求，主循环帧尾调一次 `ui::flushSe()`
  （`main.cpp` 里紧挨 `ImGui::Render()` 之前），由它挑**本帧最高优先级**的那一个播。
  `ui::SeKind` 的顺序 = 优先级（click < select < level_choose < window_open < window_close），
  必须和 `platform::AudioEngine::UiSe` 一一对应（目前直接 static_cast）。
- 为什么要这么绕：**`window_open.mp3` 里本身就混了 click 声**。按下的那一帧弹窗同时开，
  如果 click 也播就成了双击；同一帧里更高的那个（open/close）把 click 顶掉，正好对上官方手感。
  所以"按下就开窗 / 关窗"的地方**只报 click 就够了**，不用额外屏蔽。
- 开 / 关音的触发点在 `beginCard` 里（按 `CardState::soundOpen` 每局只响一次）；关音走
  `requestClose(st)`，它顺手清 `soundOpen`，这样点 X / 点按钮的那一帧就响 close、下一帧不会
  再响一次。X 的 `*closeClicked` 语义没变。
- 接好的地方：`game/Ui.cpp` 全部组件（滑杆、勾选框、stepper、combo、卡片 X）、
  `main.cpp` 的 HUD 暂停键、`game/SongSelect.cpp` 的滚轮 / 方向键 / 拖拽落点 / 分区字母 /
  演唱版本 chip / 难度按钮 / 确定 / 随机 / 设置 / 重扫。**拖拽连续滚动只在落点响**（按格响只给
  滚轮和键盘，否则 fling 会连成一片噪音）。

## 手柄 / 音量 / 结算配色（2026-09-15）

- **XBOX 手柄**（`main.cpp` 主循环前的 `openPad` + 帧首的映射块）：只做菜单，
  **不参与打歌**（12 轨的东西手柄打不了）。实现方式是**把手柄按键翻译成键盘按键**，
  用 `SDL_PushEvent` 塞回 SDL 队列（`SDL_INIT_GAMECONTROLLER` 已加进 `SDL_Init`）——
  于是选曲/设置/弹窗**原本就有的方向键 / Enter / Escape 逻辑**直接生效，不用给每个
  画面再写一条输入路径。要点：
  - 一次按压 = **一个脉冲**：本帧压 KEYDOWN，下一帧补 KEYUP（`padReleaseQueue`）。
    ImGui 对同帧 down+up 的识别不可靠，拆两帧最稳；方向键按住时按 400/110ms 自动重复。
  - 映射：方向/左摇杆 = 方向键（左摇杆死区 12000），A = Enter（确定 / 开始 / 结算继续），
    START = 选曲里开设置卡（H）、演奏中开暂停弹窗、结算页继续，Y = F5 重扫，
    B / BACK = 关弹窗（B 只在有卡/弹窗时发 Escape —— 选曲界面按 Escape 会退出游戏）。
  - 热插拔：`SDL_CONTROLLERDEVICEADDED/REMOVED` 里开关（注意 ADDED 给的是 device index、
    REMOVED 给的是 instance id，两者不能混用）。
  - 已验证：合成按键这条路是通的（`SDL_PushEvent(KEYDOWN)` 能把设置卡打开，截图确认）。
    真手柄的按键映射还需要人手试一遍。
- **音量**（设置 → 演奏页签）：`BGM 音量` = `UserSettings::bgmVolume`（存 userdata.json），
  走 `AudioEngine::setBgmVolume`，它是引擎侧的主音量，**同时**缩放谱面音轨 / 选曲试听
  （0.85 基准）/ 结算 BGM（0.85 基准），所以一定要在第一次 `loadMusic` **之前**设一次；
  `音效音量` = 老 `seVolume`，判定音本来就按它播，UI 音是 `ui::bindSe(&audio, 0.8f * seVolume)`
  —— 改滑杆时要**重新 bind**，否则 UI 音量不跟。
- **结算画面难度配色**：`difficultyColor()`（`SongSelect.hpp` 公开，选曲徽章和结算共用）
  现在同时给**难度胶囊**和**曲绘边框**上色。原来是写死的 `kPink`，因为参考截图是 EXPERT 的，
  于是打 EASY 也是红的。
- **确定键光效**（`main.cpp` 的 confirmFlash 块，2026-09-16 重做）：原来是一圈**平的 50% 白三角**
  （硬边、没有衰减）+ 两层满 alpha 的实心圆，而且**铺不满屏**——圆半径按"按钮到最远角"给，
  16 层同心圆每层恒定 0.18 alpha 叠起来，最远那个角只叠到 ~29%，加载那 1 秒能看见屏幕。
  现在：
  - **包络**加了一段 attack（`kConfirmAttack = 0.09s`，smoothstep 进），旧版第 0 帧就是全亮，
    那是"硬"的一半；
  - 半径改成两个值：`rOuter = want * (0.85 + 0.90*cover)`、`rInner = 0.62 * rOuter`。
    `want` = 按钮到最远角的距离；**关键是 `rInner`（满亮度核心）必须越过 `want`**，
    cover=1 时 rInner = 1.085 want，于是 `startSession` 那一下整屏都是峰值亮度；
  - 40 层圆的 alpha 是**解出来的**，不是常数：第 i 层的目标合成率 = smoothstep(径向位置)，
    而叠加是 `1-prod(1-a_i)`，所以 `a_i = (target - comp) / (1 - comp)`。
    这样出来的是真径向渐变（`target-alpha` 只出现在最外圈，等于 0），外缘没有硬边。
  - 峰值仍是 `kConfirmPeak = 0.88`（纯白帧看着像切一刀而不是光）。
  - 实测（1280x720，按钮中心 1083,521）：t=0.10 时最远角 156 / 近角 241（有渐变、软）；
    t=0.32（正白的时刻）四角 233~241、中心 238 —— **整屏铺满**。
  - 调试：`--confirm-flash [<sec>]` 单独放一次这个特效（不加载歌曲），配 `--screenshot` 看帧。
    无头断言用像素探针量四角 / 中心亮度（见下「验证」）。
- **`ui::combo` 的弹窗动画曾经把第一行吃掉**（2026-09-16 修，症状：分组下拉里「关闭」没了）。
  两个坑叠在一起，改 `Ui.cpp` 的 combo 前必读：
  1. **`ImGui::IsPopupOpen("##combo")` 永远是 false**。它比的是弹窗**窗口自身**的 ID，
     而 combo 的弹窗窗口名是 `"##Combo_%02d"`（按嵌套深度回收），跟 combo 的 ID 无关。
     所以 `animValue` 的 t 恒为 0 → 弹窗被永久上移 14px、箭头也从来没转过。
     现在用一个 `static std::unordered_map<ImGuiID,bool> comboOpen` 记住**上一帧**的
     `BeginCombo` 返回值来驱动动画（晚一帧正好是缓动要的"开门那帧 t≈0"）。
  2. **绝对不要 `SetWindowPos()` 挪弹窗**。ImGui 在 `Begin()` 里就把弹窗的裁剪矩形
     (`ClipRect` / `InnerRect`) 按当时的位置算好了，帧中途挪窗口只改 `Pos` 和内容游标
     （`SetWindowPos` 会把 `CursorStartPos` 一起偏移），**内容就跑到裁剪矩形外面被裁掉**——
     上移 14px 正好裁掉第一行。要动只能动内容，而内容一样会出裁剪框。
     所以入场动画改成**淡入弹窗内容**（`ImGuiStyleVar_Alpha` 只 push 在 popup 内部，
     不能包住 `BeginCombo`，否则关着的 combo 也一起变透明）。
  3. 另外 `ImGuiComboFlags_HeightSmall` 把弹窗高度卡在 4 行，而它算行高时**没算调用方的
     `FramePadding`**（`Selectable` 的 bb 高是 `label_size.y + FramePadding.y*2`），
     选曲界面那套 padding 下 4 行会多出 ~4px，最后一行被削掉。
     `ui::combo` 内部把它换成 `HeightRegular`（弹窗是 `AlwaysAutoResize`，仍然贴着内容）。
  无头验证：跑起来后用 `winsend.exe` 点下拉框（1920x1080 时分组框中心 ≈ `882,50`），
  截图后数弹窗区域里有几行文字（原来是 3 行，现在 4 行）。

## 账户 / 等级（2026-09-15，`game::AccountData`）

- **数据**（`game/SongSelect.hpp` 的 `AccountData`，存在 `userdata.json` 的 `account` 段）：
  `name` / `org` / `note` / `rank` / `exp`（**指向下一级**的存量，`addPlayerExp()` 已经把超出
  的部分滚过去了）/ `plays` / `totalScore`。老存档没有 `account` 段就是默认账户，不报错。
  `loadUserData` / `saveUserData` 多带一个 `AccountData&` 参数（只有 main.cpp 两个调用点）。
- **等级曲线**（`expToNextRank`，1:1 照 Sekaipedia 的官方表）：1 级 10、2 级 8010、
  3~12 级 `8000+500*(r-2)`、13~15 级 `13000+1000*(r-12)`、16 级起 `16000+480*(r-15)`，
  上限 `kMaxPlayerRank = 900`。一局的经验 = **分数评级倍率**（`scoreRankExp`：
  d 20 / c 200 / b 240 / a 280 / s 320）—— 官方是 `m_score × m_bonus`，我们**没有加成系统**，
  所以恒等于 `m_score`。评级用 `game::scoreRankAndBar()`，和结算画面牌子同一个调用，不会打架。
- **经验只在完整跑完时结算**：挂在 `main.cpp` 记成绩那块（`!autoPlay && !session.scoreRecorded
  && songTime >= trackDurationSec - 0.25`）里，所以 **autoplay 预览不加经验**，中途放弃也不加。
  **`--result-at` 提前切结算不会记成绩**（songTime 还没到片尾），所以也拿不到经验——想在无头
  环境验证就得真放完整首歌（`--screenshot-time` 给够）。
  日志：`[rank] <评级> +N exp -> rank R (x/need)`（有 ` (rank up)` 就是升级了）。
- **等级牌**（`ui::playerLevelChip`，`game/Ui.hpp`）：圆角深灰药丸，**左边那截绿色是经验条
  不是图标底板**——宽度 = `expRatio` × 药丸宽（左端跟着药丸的圆角、右端切平），
  `assets/select/level.png` 直接盖在它上面（金色音符 + 透明底，压在绿/深灰上都看得见）。
  药丸高度写死 `33*unit`，就是同 unit 下 `ui::combo` 的框高（17u 字 + 8u 上下 padding），
  所以选曲界面里它和「排序 / 分组」两个下拉框一样高、一样平（`headerRowY`）。
  宽度 158u 固定（等级数字在槽里右对齐），几何全部按 `unit`（= 调用方的 px/1080p 单位）缩放。
  贴图走 `ui::setLevelIconTexture()`（main.cpp 启动时
  `loadUiTexture(baseDir + "assets\\select\\level.png")` 注册），**丢了会退化成手画的八分音符**。
  两处调用：选曲右上角（`w - 26*k` / `headerRowY`，`alignRight`）、结算面板右下角
  （`kCanvasW - kPlateRightInset`，和 SCORERANK 牌子右缘对齐）——结算界面**不再另画经验条**，
  同一件事只画一遍。
  结算那块的淡入是**重写这一段顶点 alpha**（芯片自己没有 alpha 参数），和手机面板那套一样。
  独立的长条经验条 `ui::expBar` 现在只有个人资料卡在用（那儿地方宽，值得画一整条）。
- **个人资料卡**（`drawSongSelect` 末尾、`ImGui::End()` 之后）：`ui::beginCard` + `infoRows`，
  点选曲右上角的等级牌打开（卡片是带 dim 的模态，关窗靠 X / 关闭按钮——**别指望 Escape，
  选曲界面按 Escape 是退出游戏**）。`gProfileOpen` 是**模块级**变量（不是函数 static），
  这样 `--profile` 能在无头运行里直接把它顶开。
- **平时不显示**：昵称 / 学校 / 签名只有资料卡和设置「账户」页会画，演奏、HUD、结算都只有等级。
- 无头检查：`--profile`（开资料卡）、`--player 昵称:组织`、`--player-rank N`、`--player-exp <0..1>`
  （本级经验的比例，用来截等级牌那截绿色进度）。后三个**只改内存**（在 `loadUserData`
  之后套用），不会往存档里写测试数据。
- **坑**：`--sus` 的相对路径在 Git Bash 下不可靠（MSYS 会改写 `../x` 这类参数，
  实测 `--sus ../charts/x.sus` 和 `--sus charts/x.sus` 都读不到，绝对路径正常）。
  脚本里一律给绝对路径。另外 `--screenshot` 收的是**文件路径**不是目录，指到目录上会静默不写。

## 多用户 / 渲染模式 / 谱面目录（2026-09-16）

- **谱面目录一律是「exe 同级的 charts\」**（`downloader/chartdl.cpp` 的 `defaultChartsDir()`），
  不再是 `..\charts`。打包版解压出来是 `CppSekai-<日期>\`，`..\charts` 会落到游戏文件夹**外面**。
  游戏侧**两个目录都扫并且合并**（`main.cpp` 扫 `chartCandidates` 全部候选、按 .sus 文件名去重，
  先到的目录赢），所以老的仓库根 `charts\` 和新的 `build\charts\` 同时可见，不会“谱面消失”。
  `chartsDir`（`--select-id` 交接、空列表提示用）仍取**第一个非空**候选。
- **多用户**（`game/SongSelect.cpp` 的 `loadProfiles` / `saveProfiles` / `profileDataPath`）：
  `<dataDir>\profiles\<id>.json` 一人一份 `{settings, scores, account}`，
  `<dataDir>\profiles\index.json` 记 `{active, users:[{id,name}]}`。
  首次运行**把老的 `userdata.json` 复制**成 `profiles/default.json`（复制不是搬，老版本回滚照样能跑）。
  切换走 `main.cpp` 的 `activateProfile()`：存旧的 → 读新的 → **把 live 镜像全部从新档案重新推一遍**
  （`resW/resH` / `fpsLimitLive` / `noteSpeed` / `leadIn` / `gUserOffsetSec` / `windowMode` …）。
  **坑**：`persistUserData()` 是「把 live 值抄进 userSettings 再写盘」的方向，不重推的话切换用户会把
  上一个用户的窗口尺寸/帧率写进新用户的档里（实测 `default` 被 `second` 的 800x450/144 覆盖）。
  命令行给过的项（`--speed` 等）保持优先，和启动时同一套 `if (!xxxGiven)` 判断。
  账户页的 `nameBuf/orgBuf/noteBuf` 是函数 static，用 `bufProfile` 记住它是哪个用户填的，
  切换后必须重填，否则下一次击键会把上一个用户的名字写进新用户。
  切换的窗口模式 / 分辨率 / 开屏样式**下次启动才生效**（写在界面上），其余当场生效。
  删除用户**只从 index 里拿掉，文件留在 `profiles\`**（不删数据）。无头检查：`--activate-profile <id>`。
- **渲染模式**（`UserSettings::renderScale`，0=窗口多大渲染多大 / 1=固定分辨率）：
  1 的时候 `Renderer::setRenderTargetSize()` 开一个 `resW x resH` 的 FBO，场景和 ImGui 都画进去，
  每帧末尾 `presentFrame()` 再按**等比 + 黑边**贴到窗口。这样拖动窗口只缩放画面、不改排版。
  关键是三件事同时成立，少一件画面就错位：
  1. `windowW/windowH`（游戏内的一切坐标）= **渲染尺寸**，`winPixelW/H` = 真实窗口（SDL 事件 / 视口）；
  2. **在 SDL 事件进 switch 之前把指针坐标改写成渲染坐标**（`mapPointerEvent`，在
     `ImGui_ImplSDL2_ProcessEvent` 之前调用），这样游戏、ImGui、触摸三套代码一行都不用改；
     `SDL_GetMouseState` 那种直接轮询的要单独过 `toGamePoint()`；
  3. `ImGui_ImplSDL2_NewFrame()` **之后**把 `io.DisplaySize` 设成渲染尺寸、`DisplayFramebufferScale`
     设成 1（后者让字体图集也按渲染分辨率栅格化，这正是「只渲染多大分辨率」的含义）。
  `--screenshot` 读的是**真实帧缓冲**（`winPixelW/H`，带黑边），读 `windowW/H` 只会拿到画面一角。
  无头检查：`--render-size <w>x<h>`（配 `--width/--height` 就能造出窗口≠渲染尺寸）。
  验证方法：`--result-preview` + `winsend.exe CppSekai click <x> <y>` 打「继续」按钮，
  窗口模式和固定模式下**同一个窗口像素**都要命中（`[result] continue -> song select`）。
  **winsend 的 click 会先发 WM_MOUSEMOVE**，SDL 的按键事件是用最后一次移动的位置，
  所以只发 click 不先 move 的话点击会落在 (0,0)。
- **分辨率**：预设从 640x360 起（`kResW/kResH`），还有「自定义…」两个输入框
  （**提交才应用**，`IsItemDeactivatedAfterEdit`，否则输入 "1280" 会中途 resize 四次窗口）。
- **`.workbuddy/tools/shot_probe.py` 现在带 Pillow 回退**（`magick` 不在 PATH 时用 PIL 解码），
  没有 ImageMagick 的机器也能用像素探针。

## 下载器（`downloader/chartdl.cpp`）2026-09-16

- 输出目录默认 = **exe 同级 `charts\`**，记忆在 `chartdl.json`（`closeAction` / `notifyOnDone` /
  `minimizeToTray` / `outDir`，同目录）。
- **已下载状态**：`scanDownloaded()` 扫一遍输出目录（文件名全是 ASCII 派生，比较用 UTF-16 原生名，
  **不要走 `fs::path` 的窄端**）。整首齐了（该有的难度 + 曲绘 + sidecar；**演唱版本不算**，
  unipjsk 本来就缺）→ 表格行灰显 + 勾选被 `LVN_ITEMCHANGED` 里顶回去 + 全选/排队跳过；
  只有部分 → 右侧「下载内容」里已存在的那几项打勾禁用、标注 `✓已下载`。
  输出目录改了（浏览 / 编辑框失焦）会重扫。日志 `[scan] N complete, M partial` 是无头断言点。
- **队列跑完自动清勾选**（`gRunning` 由真变假那个沿，`gSawRunning` 记状态）并重扫，
  同时按 `notifyOnDone` 弹气球（`Shell_NotifyIconW` + `NIF_INFO`）。
- **设置窗口**是独立顶层窗口（`CppSekaiChartDlSettings`，父窗口 `EnableWindow(FALSE)` 做模态），
  **WM_CREATE 里用 `AdjustWindowRectEx` 重算窗口大小**——只给个猜测尺寸会让底下的按钮跑到标题栏外面。
  `--open-settings` 无头打开它并**截它而不是主窗口**。
- **分割手柄**：三块之间那条 8px 的缝是**父窗口自己的客户区**（子控件会吞消息），
  所以拖动靠父窗口的 WM_LBUTTONDOWN/MOUSEMOVE + `SetCapture`，外观靠 `WM_PAINT` + `drawSplitterBar`
  （不画的话缝就是灰底，看不出来能拖）。位置存 `gListWidth` / `gBottomHeight`（真实像素，-1 = 还没排过）。
- 关窗行为选「隐藏到托盘」时，**先 `addTrayIcon` 并确认成功再隐藏**，否则会留下一个再也叫不回来的进程。
- `loadData()` 现在**幂等**（开头 clear）：main 和 GUI 各调一次，不定稿的话歌表翻倍（715 → 1430），
  所有按索引进 `gSongs` 的东西（含已下载扫描）都会做两遍。

## 手柄 / 单实例 / 触摸 flick（2026-09-16 晚）

- **手柄绑定**（`main.cpp` 的 pad 块，仍是"手柄→键盘脉冲"这一套）：
  A = 确定 / 开始 / **跳过开场卡片** / 结算继续；START = 演奏中暂停、其他地方开设置；
  **X / Y = 暂停对话框的 重试 / 放弃**；LB / RB = **设置卡片切页签**；B / BACK = 返回。
  - **演奏中 BACK 改成暂停**（原来发 Escape，而 Play + Escape = 放弃并回选曲，误触代价太大）。
  - **暂停对话框是自绘的**（`ui::messageDialog` 的胶囊按钮是 InvisibleButton，ImGui 自己的焦点
    导航够不着），所以给 `messageDialog` 加了 `forcedChoice` 参数：手柄的选择按"点了一下那个
    按钮"处理（含关闭动画），调用方的 action 分支一行都不用加。
  - **同一帧里 `pressed(btn, prev)` 只能调一次**：第二次算出来一定是 false（prev 已经置真），
    所以 A/START 在"暂停对话框"和"跳过开场"两处都要用，就必须先 `const bool padA = pressed(...)`
    存下来。这是很容易踩的坑（写了两次，A 确认暂停对话框就永远不生效）。
  - 设置卡片的页签变量从 lambda 里的 `static int tab` **提到 main 作用域**（`settingsTab`），
    否则手柄改不到它。
- **无头验证手柄**：`--fake-pad <A|B|X|Y|LB|RB|START|BACK>[,...]` 让这些键以 30 帧压 / 30 帧松
  的节奏循环（多个键各错开半周期，否则 START 会抢掉 X 的那一帧），默认 6 个周期后停
  （不停的话会一直重开对话框，跑不到截图）。SDL 的按钮名是 `leftshoulder`/`dpup` 这种，
  短名在 `padDown()` 里做别名映射。**注意控制台输出不进管道**（GUI 子系统），要看
  `cppsekai.log`，而且每个进程启动时会**截断**这个日志，多开时只能看到最后一个。
- **单实例 / 多开**（`UserSettings::instanceMode`，设置→系统 或 `--instance single|multi`）：
  - single（默认）：`Local\CppSekai.SingleInstance` 命名互斥体已存在 → `EnumWindows` 找标题
    `CppSekai` 的窗口，`ShowWindow(SW_RESTORE)` + `SetForegroundWindow`，然后自己退出。
  - multi：再占一个 `Local\CppSekai.Profile.<id>` 互斥体。**每个实例都要占自己那份**，
    否则第二个窗口会照样挑 `default` 然后两边同时写同一个存档。已有用户都被占了就自动建
    `用户N`（在 设置→账户 里能看到，可改名）。
  - 互斥体靠进程结束由系统释放，崩溃也不会留下死锁。
- **多开的内存**：实测三个实例 207 / 299 / 333 MB（工作集），**线性增长，没有共享**。
  跨进程共享不了 GL 纹理和解码缓冲，想省只能是（a）单进程多窗口，或（b）给非首个实例开
  `CPSEKAI_TEX_RAW=0` 那套贴图缩小策略（`loadTextureFromFile` 的 maxDim/cropHeight 还编在里面，
  现在只是没人用）。**这轮没有实现，只测了数**。
- **窗口不能拖边框缩放（2026-09-16 修）**：开屏样式是图片时窗口被强制建为 `SDL_WINDOW_BORDERLESS`，
  加载完再 `SDL_SetWindowBordered(window, SDL_TRUE)`。**这个调用只把标题栏加回来，不会恢复
  `WS_THICKFRAME`**，于是窗口看起来正常但拖边缘毫无反应（`WS_MAXIMIZEBOX` 也一起没了）。
  修法：恢复边框之后补一次 `SDL_SetWindowResizable(window, SDL_TRUE)`（SDL 内部就是加
  `WS_THICKFRAME|WS_MAXIMIZEBOX`）。退出全屏（F 键、设置里换窗口模式）两处也补了。
  判断依据别靠肉眼：`.workbuddy/tools/winstyle.c` → `build/winstyle.exe <窗口标题>` 直接打样式位
  （PowerShell 的 `Add-Type` 在本沙箱被禁，所以用 C 小工具）。
- **触摸 flick 的修复（2026-09-16 晚，已改）**，四处：
  1. `movePointer` / `beginPointer` 现在收 `event.*.timestamp`，不再用处理事件时的 `SDL_GetTicks()`；
  2. 同一毫秒的采样**累加**（`pendingDx/pendingDy`）而不是 `dt==0` 直接丢掉，抬手时也把在攒的位移并进去；
  3. 新增"位移兜底"：触摸划出 56px（1080p 当量，鼠标 150px）就算 flick，不管秒速度；
     触摸阈值 500→380、600→450 px/s；
  4. 方向改成**按位移主轴判**（侧向在 0.85 倍内算侧向），并且**判失败不再吃掉手势**
     （只有打中才清零位移 + 上 60ms 的锁）。数值模型：温和 343px/s 上传（原来永不触发）、
     1440p 窗口、一半采样同毫秒丢样本这三种情况现在都能判出 UP，45° 斜划判 RIGHT（原来判 UP）。

## 贴图瘦身 / UTF-8 路径 / 扫描开销（2026-09-16 晚）

- **贴图瘦身是改文件，不是改加载策略**。`loadTextureFromFile` 那套 maxDim/cropHeight
  策略（`keepRawTextures()`）**仍然默认关闭**——缩/裁之后的纹理尺寸和调用方自己维护的
  sprite 矩形对不上（2026-09-14 踩过，画面整体错位）。所以瘦身只动**消费方与纹理尺寸无关**
  的那几张，`.workbuddy/tools/shrink_assets.py` 里就是这份白名单（原图备份在
  `.workbuddy/backup/assets-20260916/`），跑 `--apply` 生效：

  | 文件 | 改动 | 解码内存 |
  |---|---|---|
  | `assets/mmw/stage.png` | 2048x2840 → **裁到 2048x1176**（下面 1664 行没有任何四边形采样） | 23.3 → 9.6 MB |
  | `assets/mmw/background_overlay.png` | 2048 → 1024（整张 UV，房间底板本来就是糊的） | 16.8 → 4.2 MB |
  | `assets/select/img_smartphone.png` | 1034x1942 → 517x971（画进一个矩形，内部布局全是比例） | 8.0 → 2.0 MB |

  合计 `[tex] 53.2 MB uploaded` → **28.2 MB**，启动 `stage textures` 484ms → ~100ms。
  A/B 验证：只换这三张、同一时刻截图，全图差异和"同一套素材跑两次"的噪声完全一致
  （mean 3.53 / >12 的像素 2.63%）→ 视觉无损。
  `buildStaticVertices()` 的舞台 sprite 现在写 `min(2048, mStage.width) x min(1176, mStage.height)`：
  裁过的文件和重新下载的原始文件都会映射到**同一片像素**，谁都不会被拉变形。
  **别动**：`notes*.png`（音符图集）、`effect.png`（判定特效）、`longNoteLine*`、`touchLine*`、
  `assets/mmw/overlay/**`（HUD 精灵，全都有写死的 sprite 矩形）——HUD 那套的瘦身走
  `overlay_opt/` + `shrink_hud.exe`（整张等比缩，UV 仍然对得上）。
- **路径一律 UTF-8**（`path_utf8.hpp`，2026-09-16 修启动崩溃）。Windows 上 `fs::path` 的窄端
  是**本地 ANSI 代码页**，`fs::path(std::string)` / `path.string()` 碰到代码页表示不了的字符会
  **抛 `filesystem_error`**，没人接 → `std::terminate`。实测：谱面目录里放一个
  `テスト曲_初音ミク_🎵_master.sus`，进程在 `[boot] chart scan` 之前就死了（cp936 机器上
  一串 UTF-8 字节不是合法 GBK 双字节序列）。所以：
  - `ChartEntry::susPath` / `bgmPath` / `coverPath`、`userDataPath()` 系列、`--sus` 的参数
    **全是 UTF-8**（和 `SDL_GetBasePath()`、`utf8Argv`、nlohmann::json 一致，也是 miniaudio
    和开了 `STBI_WINDOWS_UTF8` 的 stb_image 要的编码）；
  - 过 `fs::path` 边界只走 `path_utf8::toPath()` / `path_utf8::fromPath()`，
    不许再出现 `fs::path(窄串)` 和 `.string()`（`game/SongSelect.cpp` 里已经清干净，
    调用方想省事就照它那两行包装 `toFsPath` / `fromFsPath`）；
  - `build.sh` 加了 `-DSTBI_WINDOWS_UTF8`（stb 才会用 `_wfopen`）；
    miniaudio 自己就把 UTF-8 转宽字符，不用管。
  回归：`build/cppsekai.exe --charts <含 emoji 文件名的目录>` 必须能扫出 1 首并出图。
  顺带修好的：成绩 key（`scoreKey()` = 谱面文件名）以前是 ACP 窄串写进 JSON 的，
  非 ASCII 名会存成乱码，现在和 ASCII 名一样（ASCII 下 ACP == UTF-8，老档兼容）。
- **`findSidecar` 的目录兜底以前是 O(n²)**（2026-09-16 修）。关键字兜底每次都
  `directory_iterator` 重走一遍整个谱面目录，而没有 `<stem>.png` 的谱面（很常见）每首都会走到
  这条分支。实测 **700 首 = 扫描 3.2s**（70 首 192ms，10 倍数量 17 倍耗时）。
  现在 `folderListing()` 按目录缓存一份排序后的 `(小写文件名, 完整路径)`，关键字兜底和
  **精确名探测**都改成查内存（后者以前每首约 12 次 stat）；`scanChartFolder()` 开头
  `clearFolderListingCache()`，所以 F5 / 刷新按钮照样能看到新丢进来的谱面。
  **这条缓存只在扫描期有效，不要提到扫描外面用**（否则会藏住新文件）。
  实测：700 首 3.25s → **0.25s**（热）。
- **`scanAllChartDirs` 曾经把每个候选目录扫两遍**（2026-09-16 修）：`chartsDir`
  （第一个非空候选）原来是靠再调一次 `scanChartFolder` 判断"空不空"找出来的，结果整个扫描
  白跑一遍。现在在合并那一趟里顺手记下来。**别再为了"探测有没有谱面"调 `scanChartFolder`**，
  它是全量扫描 + 每首开文件。
- `CPSEKAI_SCAN_TIMING=1` 打 `[scan] N chart(s): header X ms, sidecar Y ms`（开文件读 SUS 头
  和各 sidecar 的耗时分解）。剩下的时间在 `entries.push_back`（每条 15+ 个 string）和
  `std::sort` 上，700 首约 0.15s，不值得再动。
- 无头量性能：`[boot] chart scan` 行给的是**累计到那一刻**的耗时（前面的行一减就是这一段）；
  造大库就用脚本把一首谱复制 N 份到 `build/charts_bench/`，再 `--charts charts_bench`。
  注意 `--charts` 收的是**能被 Windows 打开的路径**：Git Bash 的 `$(pwd)` 会给出
  `/d/...` 这种 MSYS 路径，`--charts "$(pwd)/charts_bench"` 会扫出 0 首（用相对路径或 `D:/...`）。
- 扫描本身的固定开销还有：每首 `readSusHeader`（开一个文件读 40 行）+ sidecar 的存在性检查。
  这是 715 首那套的真实成本（约 0.4s），暂时够用；要再快只能并行，不是必须。

## 初始血量 / Flick 调试日志（2026-09-16 晚）

- **初始血量上限 5000（`kMaxInitialLife`）**：`settings > 判定 > 初始血量` 的滑杆和
  `loadUserData` 的 clamp 都是 100..5000。**bar 按「本局开局的血量池」归一化**
  （`JudgementEngine::lifeRatio() = life / max(1, mInitialLife)`），所以不管设多少，
  演出界面开局都画在 100%（以前 488 就从 48.8% 开始、5000 会直接画出面板右边）。
  `lifeCeiling() = max(kMaxLife, mInitialLife)` 是血量**数值**的上限，三个扣血点
  （`registerMiss` / BAD / 长条中断）都必须用它 clamp —— 用 `kMaxLife` 会把 5000 的池子
  在第一次扣血时直接压回 1000。日志里的 `[score] life=X/Y` 里 Y 也用 `initialLife()`，
  `[stats]` 的百分比走 `lifeRatio()`。
- **面板上的数字是真实血量，胶囊是百分比**（2026-09-19 修）：`HudState` 有两个字段——
  `lifeRatio`（0..1，喂血条）和 `lifeValue`（`JudgementStats::life` 原值，喂 `life/v3/digit/*`
  那几位数）。**以前数字是 `1000 * ratio` 写死的**，所以初始血量设成 5000 时，
  数字从一开始就写 1000、每次 MISS 掉 16（看起来"扣血变慢"），而血条其实一直是对的。
  实测（0001_master，autoplay 8 miss、池子 5000）：数字 `4360`、血条 87.2%，
  与 `[stats] life=4360 (87.2%)` 一致。
- **Flick 调试日志**（`settings > 判定 > Flick 调试日志`，勾选即生效并写进档案；
  命令行等价物 `--flick-log`，不落盘设置、只给支持/回归用）。开启时先把
  `flick_debug.log`（exe 工作目录）清空再逐行 flush，内容：
  - `[touch] down` / `[touch] move` / 每次 flick 判定尝试的 `[flick] fire/move|fire/up`；
    move 行有**每个原始采样**：屏幕坐标、位移、`dt`（以及未 clamp 的 rawDt）、低通后的
    vel、travel、当前分类出的 dir、lane。
  - 没打中时 `logFlickOutcome` 会调 `JudgementEngine::debugFlickNotesNear(t, 0.6s)`
    把附近的 flick 音符按时间距离列出来（`dt` / laneDelta / halfWidth / wantDir / state），
    这是判断"方向错 vs 轨道错 vs 根本没到判定窗口"的关键。
  - 所以用户报"触摸 flick 老 MISS"，**让他开着这个开关打一遍再把 flick_debug.log 发来**
    就行（日志很密，一首歌几千行）。
  - 实现注意：日志结构体 `FlickDebugLog` 在 main.cpp 匿名命名空间（文件级 `gFlickLog`），
    `setEnabled()` 里会 truncate；**变量别叫 `near`**（windef.h 把它定义成空宏，
    声明会被吃掉，直接编译不过）。

## 「Flick 视作 Tap」（2026-09-19，`settings > 判定`，紧跟在「严格 Flick 方向」后面）

给"上滑很难触发"的触摸屏用：勾上之后**所有 flick 音符都变成 tap**——
`findCandidate()` 里 `flickAsTap` 同时跳过"种类不符就拒绝"和"方向不符就拒绝"两层，
所以任意一次按在窗口内的点按/滑动都能清掉它（方向不再有意义）。
`--flick-as-tap` 是本次运行等价的调试开关（和 `--auto` 一样**不落盘**）。

**hold 尾接 flick 例外，而且不能反过来变成 tap**：玩家那时手还按在轨道上，
要求他再点一下或者滑一下都是这个设置本来要消灭的事。所以**那个尾巴不再是音符**：
`update()` 的 hold 尾判分支在 `mFlickAsTap` 时直接 `judgeHoldTail(Perfect)`
（原来只有 `mAutoPlay` 走这条），按住不放、提前松手都算过，不扣分不断连。
- 注意**不能**让它走 tap 尾那条路（按松手时刻评 Perfect/Great/Good/Bad）——
  那还是在惩罚一个"本该不存在"的音符。
- 也不用担心它会漏判成 MISS：`findCandidate` 里 hold tail 只可能被 flick 手势碰到
  （`note.holdTail && !(wantFlick && kind == 2)`），所以尾巴只能由 hold 追踪器收尾。

实测（`0001_master`：17 个 flick，其中 9 个是 hold 尾；把 `--test-hits` 的 flick 分支
临时改成 `tap()` 模拟"只会点不会滑"）：关 = perfect 597 / miss 25 / tails 41；
开 = perfect 615 / miss 7 / tails 51。差额正好是那 17 个 flick（尾巴 +10 走的是新分支）。

## 多人游玩（`platform/Party.*` + `game/PartyScreen.*`）

同一台机器开多个窗口一起打。房间**没有自己的页面**：它就在选曲界面上。

**房里只有一个人时不显示任何多人 UI**（2026-09-19）：左下角的房间胶囊
（`drawPartyBadge`）和手机面板状态行里那些"其他玩家…"的话都只在
`party.playerCount() > 1` 时才出。房间本身照旧活着（下一个窗口开起来就加入），
只是不拿一排并不存在的队友去烦人。**演算里的"加载中/即将开始"和错误行
（谱面加载失败 / 本窗口没有这首曲子）照常显示**——那是本窗口自己的状态，不是多人话术。

流程（2026-09-18 定稿，之前的"房间页 + 准备 + 10s 倒计时"已删）：
**第一个窗口是房主**，选曲列表归它（它的光标停在哪个曲目，就立刻广播哪个曲目）；
其他窗口的列表是**只读**的（灰罩 + 「只读·由房主选曲」+ 一律禁用的搜索/排序/刷新 + 灰掉的随机按钮），
它们的**手机面板显示房主的曲目**，难度就在那里点；**所有人的「确定」= 已经决定的信号**，
房主也按确定，最后一个按下去就**立刻**开打（只有 lead-in，没有额外等待）。

- **谁按了什么**（`mpConfirmed` / `mpSpectating` / `mpPublished`，都在 main.cpp）：
  - 房主：光标换曲 → `publishHostSong()`（`lockSong`，epoch++，成员据此清空自己的难度/确定）；
    按确定 → 自己 ready + 记住 `mpConfirmedEpoch`；再按一次且没全 ready → **强制开始**。
  - 成员：手机里点难度 → `party.setDifficulty`；按确定 → ready。换曲/回到大厅时自己的答案作废。
  - 旁观：banner 右侧的胶囊把座位退回 `PartySeatLobby`，**这样它就不会卡住房主的开局**
    （`allReady()` 只看非 Lobby 座位；`inRoom >= 1`，所以房里只剩房主时它自己确定就开）。
  - 进房间时座位直接是 `PartySeatChoosing`：刚加入的窗口也算"在局里"，房主要等它按确定
    （否则房主会在新窗口还没看清楚前就开局）。
- **加载宽限没了**：老代码是 `4s 宽限 + lead-in`（默认 10s），现在只有 lead-in（默认 6s）。
  各窗口在 `PartyCharging` 那一帧加载自己的谱面（**白色确定闪光就是盖这次加载的**，
  在 charge 里手动点亮 `confirmFlashActive`，不要设 `confirmStartPending`——这里没有异步加载），
  然后等共享 QPC 时刻。
- **开局没有倒计时（2026-09-19 改）**：charge 分两段，靠 `startCounter == 0` 区分：
  1) **加载段**：房主 `beginLoading()`（epoch++，phase=Charging，**不写 startCounter**），
     每个窗口加载完自己的谱面就 `setSeat(PartySeatLoaded)`（**新加的座位状态**，不是 Ready）；
  2) **武装段**：房主看到 `allLoaded()`（非 Lobby 座位全是 Loaded）就把起奏时刻写下去
     （`armStart(now + 0.8s)`，**不动 epoch**，否则成员会以为换了一轮）。
  于是等待 = 最慢那个窗口的加载（实测 0.2~0.3s）+ 0.8s 引信，**不再是固定的 5.8s**。
  实测日志：`[party] start armed: ... (every chart loaded; the room waited 0.24s for the loads)`。
  坑：
  - `armStart` 只在 `phase == PartyCharging` 时才写，否则会把已经释放的房间"复活"；
    房主侧用 `mpArmedEpoch` 保证一轮只武装一次（重复武装会把时刻一直往后推）。
  - 房主的**强制开始**（第二次按确定）也走 `beginLoading()`：没按确定的窗口会立刻
    `setSeat(PartySeatLobby)` 让开，所以不会卡住武装。
  - 成员第一次看到 charge 时的 **`late` 判定必须带 `startCounter != 0`**：QPC 是个大正数，
    裸写 `now >= startCounter` 会在加载段就把所有窗口判成"错过开局"。
  - **看门狗** `kPartyLoadTimeoutSec = 6.0`：某个窗口加载失败/卡住时房主照常武装
    （日志写 `watchdog: somebody never loaded`），否则整房永远停在加载段。
    迟到的窗口仍然会**正确入局**——时钟锚在共享 QPC 上，它只是少看一段开幕卡。
  - 房主自己的加载失败时 `releaseSong()` 回大厅，成员按"房主已放弃本曲"回列表。
- **结算画面进不去了（2026-09-18 修）**：多人路径从来没调 `announceTrack()`，
  `trackDurationSec` 一直是 0，于是 `resultDue` 恒假 —— 歌放完就停在演奏画面。
  现在 charge 加载成功后两边都调 `announceTrack()`，并且**房主把 `trackDurationSec`
  也写进共享页**（`publishTrackEnd` / `readTrackEnd`，共享块多了一个 `trackEndMs`，
  映射名升到 `Local\CppSekai.Party.v2`；v1 的旧窗口不会读错偏移）。
  成员每帧读它——成员没有 BGM，自己算的话会拿谱面最后一个音符当曲终，比房主早好几秒切结算。
- **传输**：一条命名文件映射（`platform/Party.cpp`），
  8 个座位 + 一个控制块。没有 socket、没有管道、没有序列化，也不 flush：**一次状态变更
  就是往共享页写一个 LONG**，别的窗口下一帧就读到了——同机上这是延迟的物理下限
  （只剩读者自己的一帧）。控制块用 seqlock（写者 `InterlockedIncrement(&seq)` 包住），
  座位里每个字段只有主人写、天然无冲突。
- **座位/主机**：进房间时 CAS 抢最低空位；`hostSlot` 谁都不是有效活人时，最低活位接管。
  每帧写 `heartbeat`（GetTickCount64 低 32 位），超过 6 秒没心跳的座位被判死——但
  **`PartyCharging` 阶段不回收**（那时所有窗口都在加载谱面，谁都不心跳，回收会把房间打散）；
  被误判死掉的窗口会自己把座位抢回来（`update()` 开头）。
- **起奏同步**：`startCounter` 是一个 **QPC 值**（不是相对时间）。QPC 全机唯一，所以
  "T 时刻开始"对所有窗口意义相同。各窗口到点各自 `beginSessionClockAt(startCounter)`
  （`perfStart = startCounter`），之后再走房主的 lead-in（默认 6s，就是开幕卡 + 淡入）。
- **时钟跟随**（`resolveSongClock` / `songClock`）：房主每帧把自己的 `songTime` **连同采样时的
  QPC** 发到共享页（`publishHostClock`）；成员的本底时钟是 QPC（它没有 BGM，走 `wallSongTime`），
  用包里的 QPC 差把房主时钟外推到"现在"，再把差值当作**偏移量**缓变跟上：
  `t = 本底 + offset`。三点要注意：
  - **房主未发布之前读到的包是 0**，直接拿去算会得到几个小时量级的误差（第一次实测就是：
    成员时钟瞬间跳到 +3993s，整首歌全 MISS）。所以控制块里有 `hostClockValid`，
    没发布就返回 false；另外 `|error| > 5s` 的包一律丢弃（`[party] clock sample rejected`）。
  - **起奏前允许自由对齐**（`local < 0.5` 直接 snap）：那时只有开幕卡在看时钟，
    跳一下没人看得出来，但可以把对齐误差压到噪声级——慢慢挪的话第一串音符会差几十毫秒。
  - **起奏后 3 秒内用 50% 速率修正，之后 3%**。因为房主的音频时钟在**音乐真正开始**的那一刻
    会被重新锚定（`AudioEngine::update` 里 `mMusicStartFrames = 现在`），一下子跳最多一帧
    （30fps 下 25ms），而那一刻正是第一批音符要判的时候。3% 要好几秒才吃完这个台阶。
  实测（`--party-auto` 两个窗口，`CPSEKAI_MP_TRACE=1` 对 QPC 轴）：**中位 3.4ms / 最差 6.7ms**。
- **BGM 只有房主播**：成员的 `startSession(..., allowMusic=false)` —— 不 `loadMusic`、不检测
  头部静音，`audio.hasMusic()` 为假，于是时钟自然落回 wall clock（正好被上面的跟随覆盖）。
  选曲界面的试听也只有房主放（`playPreviewHere`）。同一份 mp3 在两个窗口差几毫秒放出来就是回声。
- **暂停 / 放弃 / 重试**（2026-09-18 补）：房主暂停时 `setHostPaused(true)`，成员看到后
  **把画面钉在房主报的那个 songTime 上**（不外推，否则暂停期间会越跑越远）；恢复瞬间靠
  `|error| > 0.2` 的 snap 重新对齐。**成员按不动暂停**（`requestPause()` 直接拒绝并往
  `mpStatus` 写一行提示）——时钟是房主的，本地暂停只会让自己脱节。多人模式还会**强制关掉
  "失焦自动暂停"**（多个窗口并排，几乎总有一个不在前台）。
  - 房主在暂停对话框选**放弃 / 重试**：`releaseSong()` 把房间丢回大厅（epoch++），
    **成员在演奏态每帧看一眼房间**（`phase == PartyLobby` 且 epoch 变了）就 `leaveLiveForRoom()`
    回选曲——否则它会继续打一首已经没人计时的歌，而且再也回不到列表。
  - **重试在多人里 = 重开本轮**（回大厅 → 房主重新广播同一首 → 全员再按确定），
    不是原地重启：共享一局里的时钟/谱面/起奏时刻都是房间的，一个窗口自己重启必然失步。
- **选歌试听与打歌 BGM 重叠（2026-09-18 修）**：房主进入 charge 后**还停在选曲界面**
  等整个 lead-in，而这一帧的 BGM 已经加载好了——试听每帧还会重启，于是"选歌的曲子"和
  "正在打的曲子"叠在一起（而且看起来是概率触发，取决于 charge 落在哪一帧）。
  修法：`playPreviewHere` 里加上 `!mpStartPending`（charge 一武装就停试听）。
- **charge 阶段的数字倒计时已删**（2026-09-19）：原来在选曲界面上盖一层 + 中央大数字
  （"即将开始 5.8"），因为那时 charge 是**固定的 5.8s**，不盖点什么就像"按了确定又回到选歌"。
  改成"加载完就开"之后整段只剩约 1 秒，数字只会闪一下，于是：
  遮罩和大数字全删（`state == AppState::Select && phase == PartyCharging` 那个块没了），
  只在手机面板状态行写 `谱面加载中…` → `即将开始`（**不带数字**，
  `startCounter != 0` 区分两段）。这段等待由房主那边点亮的白色确定闪光自解释。
- **"失焦"在多人里一律忽略**（2026-09-18）：不只 `party.active()`，只要**本机有房间**
  （`roomOpen`，启动时 `roomExists()` 的结果）就跳过所有失焦处理——包括演奏态失焦直接
  回选曲那条老路径（lead-in 期间失焦会 `state = Select`，在两个窗口互相点击时特别容易触发）。
- **房主中途关窗**：成员发现 `hostAlive()` 为假就放弃跟随、退回自己的 QPC 时钟
  （`[party] host gone: running on the local clock`），不会因为跟着一份冻结的包而卡死。
- **SMTC / 任务栏进度只有房主汇报**（`smtcHere`），否则两个窗口抢同一个媒体控件。
- **换歌靠文件名的**：房主广播的是**谱面文件名**（`0001_expert.sus`）+ `musicId`，
  成员在自己的谱面表里找同 musicId 的同难度（`findPartyEntry` / `buildPartyOptions`）。
  两台窗口读的是同一个 `charts\`，所以一定找得到；成员没有的难度不会出现在它的难度条里。
- **多人模式下窗口标题带玩家名**（`CppSekai - <名字>`）：否则任务栏里几个窗口分不出来，
  无头脚本也没法指定要点哪个（winsend 按标题子串找窗口）。
- **开关**：`设置 -> 系统 -> 多人游玩`（`UserSettings::multiplayer`，写进档案）或命令行
  `--party`（本次运行有效，不落盘）。打开它会**顺带把多开策略设成"允许多开"**——多人本来
  就需要多个窗口，而多开时每个窗口登录不同用户（成绩各自记账）。
- **只有一个人的房间**：房主自己按确定就开局（`allReady()` 的 `inRoom >= 1`），
  日志写 `[party] all ready -> charging (...) [solo in the room]`。中途加入的窗口会落在
  **charge 已经过了**的那一侧，就本曲不参加（`[party] charge skipped (...)`），
  不会半路掉进已经开始的谱面。
- **回归脚本**：`.workbuddy/tools/mp_verify.sh`（三轮全自动：一轮完整对局 + 暂停广播 +
  **打到结算画面并回选曲**，全 PASS/FAIL 输出）。`--party-auto <难度>` 现在也能当单机用：
  没有房间时它就是在 1.8s 按一下确定（配 `--no-party` 可以测纯单机流程）。
- **房间状态在选曲界面上**（2026-09-18）：`game::SelectPartyInfo/Result` 是选曲界面
  与房间之间的接口（`drawSongSelect` 最后两个参数，不传就是纯单机）。
  `--screenshot-time` 在选曲态就能拍到 banner / 灰罩 / 手机面板（不再需要房间页）。
- **偏差断言要绕开"成员追赶"那一下**：mp_verify 里插值时如果成员这段 offset 变了 >20ms
  就单独统计（`member clock corrections seen`），不算进 skew——那是它按设计跳到房主时钟上，
  插值会把一个 350ms 的假偏差报出来（真机上两窗口始终是同一条时间轴）。

### 设置 / 加入房间的几个坑（2026-09-17 补，都是用户实际踩到的）

- **多人游玩开关是"按档案"存的，房间是"按机器"的**。多开时第二个窗口会**换成另一个档案**
  （`[instance] multi-open -> profile 'other'`），而这次换档案发生在实例策略判定**之后**、
  于是它读到的 `multiplayer` 是**那个档案自己的**（默认 false）→ 于是第二个窗口不进房间、
  房主永远只有自己一个人 → 表现就是"开了多人游玩却打不起来"。
  修法：进房间前先 `PartyLink::roomExists()`（`OpenFileMappingW` 试探），
  **本机已经有别人开好的房间就无条件加入**（日志会写
  `[joined a room this profile has switched off]`）。档案之间不再需要各自打开一次。
- 房间没起来时启动日志会直说：`[party] off (设置 -> 系统 -> 多人游玩 没开，本机也没有别人开好的房间)`
  / `[party] ready: seat N ...` / 单人开局时 `[party] solo start (1 player in the room...)`。
  **"多人没生效"先看这三行**。
- 设置卡片（系统页）：**"多人游玩"勾选框在"只允许一个实例"时是灰的且点不动**（`ui::checkBox`
  加了 `enabled` 参数，`BeginDisabled` + 灰化；灰着的时候**不回写**，否则打开一次设置页
  就把开关悄悄关掉了）。反过来，选"只允许一个实例"会**自动关掉**多人游玩。
  档案加载时也补了一条：`multiplayer == true` 就把 `instanceMode` 抬成 1，保证文件自洽。
- **昵称和"用户"下拉框是两个东西**：下拉框列的是 `profiles/index.json` 里的档案标签，
  昵称在档案数据里（`account.name`）。以前改昵称不更新标签，于是设置页显示"默认用户"。
  现在：改昵称即同步标签（空昵称不动，避免把标签清成空）；启动时也会把不一致的标签
  自动纠正（`[profile] label of 'default' updated to 'xxx'`），多开换档案、切用户同理。
- **窗口标题带玩家名**（`CppSekai - <名字>`）：多开时任务栏/无头脚本都靠它区分窗口；
  注意昵称改了要重开窗口才更新标题（房间里的名字是即时更新的）。

### 触摸屏多窗口（2026-09-17 补）

- 同机两个窗口并排打时，**SDL 默认会吞掉"点击激活窗口"的那一次按下**
  （`SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH` 默认 0：只激活、不给事件），于是后台窗口的
  第一次触摸直接消失 = 一个音符判废。**多人模式启动时已把它设成 "1"**（房间起来之后才设，
  单机手感不变），激活和事件一起给。
- 触摸本身不会"抢键盘焦点导致另一边判不了"：Windows 把每个触点按坐标投给**它下面的那个窗口**
  （WM_POINTER/WM_TOUCH），SDL 把它变成该窗口的 `SDL_FINGER*`；游戏判定走 finger 路径，
  触摸顺带产生的合成鼠标事件带 `SDL_TOUCH_MOUSEID`，在鼠标分支里被过滤掉。
- 真正会烦人的是**层级**：触摸会把那个窗口抬到最前，两个窗口重叠时会被对方盖住。
  解法是并排放（不重叠），或者给从属窗口点 `WS_EX_NOACTIVATE`（还没做，需要时再加开关）。
- 验收办法：两个窗口都开 `--flick-log`（各自 cwd 下的 `flick_debug.log`，**要放不同目录**
  否则互相覆盖），或者直接看 HUD 左下角队友分数条——某个窗口如果摸不到，它的分数就一直是 0。

## ELUA（首次启动的「关于本软件」弹窗）2026-09-18

- `ui::eulaDialog`（`game/Ui.*`）就是 `messageDialog` 的加强版：可带一段说明文字 +
  一个勾选框（"以后不再显示"）+ 一行按钮。**内容按 UTF-8 码点边界贪心折行**
  （`CalcTextSizeA(fontSize, FLT_MAX, 0, candidate)` 逐段量、取最长可容纳前缀）——
  用字节下标 `substr` 折行会把多字节序列切断，量出来的宽度是错的，中文文案必现溢出。
- 卡片宽度 = `max(660*s, buttonsW+56*s)`，高度由折行数推出；`beginCard(..., false, true, ...)`
  = 没有右上角关闭 X（必须点按钮才能走）。
- 开关存在 **profile 里**：`UserSettings::eulaAccepted`（`userdata.json` 的 `eulaAccepted`）。
  主界面首帧弹出（`main.cpp` 的 `AppState::Select` 分支之后），
  关闭时 `userSettings.eulaAccepted = eulaNoShow; persistUserData();`，日志 `[eula] dismissed (accepted=%d)`。
- 文案四段：① 免费开源非营利（AGPL-3.0）、与 SEGA / Colorful Palette / 《世界计划》官方无关；
  ② 不含官方代码/音频/曲绘/谱面，素材自行下载仅供本地学习；③ 勿商用、勿传播无权素材；
  ④ 按现状提供无担保，本机数据只存本地不上传。

## 空谱面时的「下载谱面（chartdl）」按钮 2026-09-18

- 列表为空时（`rowCount == 0`）在选曲界面画一句提示 + 一个
  `ui::capsuleButton("下载谱面（chartdl）")`，返回 `SelectAction::SelectDownload(-5)`。
  **`emptyAction` 优先于列表本身产出的 action 返回**（列表为空，本来就产不出别的）。
- `main.cpp` 收到后 `launchChartDownloader()`：先 `FindWindowW(L"#32770", L"CppSekai 谱面下载器")`
  找已开的窗口（有就 `SetForegroundWindow`），否则依次试 `baseDir + "chartdl.exe"` /
  `baseDir + "build\\chartdl.exe"` / `baseDir + "..\\build\\chartdl.exe"`，
  `CreateProcessW` 起独立进程（工作目录 = `baseDir`，所以它写到 `<baseDir>\charts`）。
- 每帧 `pollChartDownloader()` 轮询进程句柄，退出后 **自动重扫谱面**——这就是这个按钮的
  全部意义（下完回来列表里就该有东西）。`wantRescan` 的三个来源：
  `rescanRequested || action == SelectRescan || chartDlFinished`。
- 调试：`--chartdl-test [<sec>]`（默认 1.5s）在列表为空时自动按一次这个按钮。
  **为什么需要它**：`winsend.exe` 的 PostMessage 点击到不了这个界面（无交互会话下 SDL 不收注入输入），
  所以验证只能从代码里按。

## 暂停必须冻住"主机发布的时钟"2026-09-18（多人 + 无 BGM 谱面）

- 症状：房主按暂停，**成员画面继续走**（实测 2 秒内漂 2022ms），房主自己也还在往前推。
- 根因不在 `Party`，在时钟来源：`resolveSongClock()` 的非 follower 分支是
  `audio.hasMusic() ? audio.songTime() : wallSongTime()`。`AudioEngine::songTime()` 认 `mPaused`
  并返回冻结的 `mPauseSongTime`，**但 `wallSongTime()` 是纯 QPC 差值，完全不知道暂停这回事**。
  于是任何没有 BGM 的谱面（或还在 lead-in 阶段的）一暂停，房主的 `frameSongTime` 照涨，
  `publishHostClock()` 就把这个"还在动的瞬间"发给全房，成员跟着一起跑。
- 修法：`main.cpp` 里加了主机侧冻结 `mpHostFreezeValid` / `mpHostFreezeTime`——
  仅当 `paused && !audio.hasMusic()` 时锁住当前值，`!paused` 时解锁。
  **别删**：没有它，"无 BGM 的谱面 + 暂停"就是必然的前后场不同步。
- 回归：`mp_verify.sh` 的 round 2 断言「host paused 期间成员 chart clock 位移 < 50ms」，现在是 0ms。

## 难度槽位解析失败会让歌**整个不可选** 2026-09-18（"多人有时候连不上"的真正根因）

- 症状：房间组起来了（seat 0 host / seat 1 member 都在），谱面也 `song locked` 了，
  **但房主从此一行日志都不再打**，这一局永远开不起来。看着像 IPC 挂了。
- 根因在选曲界面，不在 `Party`：`drawSongSelect()` 每帧用
  `selected = groups[groupIndex].idx[diffIndex]` 反推选中项，`diffIndex` 默认 3（EXPERT）。
  `buildGroups()` 只把谱面放进**它自己的难度槽**（`diffIndexOf(item.difficulty)`）。
  于是 `#DIFFICULTY` 缺失 / 写成 `0` / 是表里不认识的字符串时（**unipjsk 的谱面一律如此**），
  `idx[全部] == -1` → `selected = -1` 每帧被写回，**把调用方开局的合法 `selected = 0` 冲掉**。
  列表看着有一首歌在屏幕上，实际"什么都没选中"：单机按确定没反应，
  房间里 `publishHostSong()` 因为 `selected < 0` 直接 return，房主静默，全房干等。
- 修法两处（`game/SongSelect.cpp`）：
  1. `buildGroups()` 里 `d < 0` 时**把这个 entry 塞进最低的空槽**——行变得可选，
     而 `difficultyIndex()` 仍然如实报 -1，不假装知道它是哪档。
  2. `selected` 赋值后补一层兜底：扫一遍该 group 的槽位，取第一个非负的。
- **踩坑教训（重要）**：这次差点被自己的诊断工具带偏。我给帧循环加的 `[alive]` 心跳显示
  `uiClock` 一路涨到 29s、帧率 33.3ms 稳定，于是先入为主认定"帧循环没停，是别的地方卡住"，
  转头去查 `Party::init()` 的 CAS 竞态。真正的线索是那行被我**加了 8 帧上限**的条件探针
  ——它打印的 `sel=-1 entries=1` 才是答案。**探针的上限别设太小，它会掩盖"条件一直不满足"
  和"条件满足了但分支没走"的区别。**
- 顺带修掉的两个真竞态（`platform/Party.cpp`，都不是本次根因但确实存在）：
  - `init()` 原来是先 `InterlockedCompareExchange(&seats[i].alive, 1, 0)` **发布 alive**、
    之后才写 `heartbeat` 等字段。窗口期内别的进程会看到 `alive==1` + 上一个宿主的陈旧心跳
    （可能已超 `kStaleMs`）立刻回收它。现在改成**先写全部字段、`release` 栅栏之后再用 CAS 发布 alive**。
  - `update()` 里"发现自己被回收就无条件抢回 seat"改为 CAS：若已被第三个进程合法拿走，
    就**放弃这个 seat 重新 `init()` 入房**，而不是两个进程都自认拥有同一个座位、逐帧互相覆盖。

## 日志缓冲（2026-09-18，排查多人问题时的副产品）

- `main.cpp` 启动早期现在无条件 `setvbuf(stdout/stderr, _IONBF)`。
- 原因：这个 exe 是 **Windows 子系统**，`AttachConsole(ATTACH_PARENT_PROCESS)` 成功后会
  `freopen("CONOUT$", ...)`，**而那一步会把 stdout 打回 msvcrt 的默认（行/全）缓冲**。
  从 cmd / PowerShell / Git Bash 里 `> run.txt` 时它同时拥有控制台和被重定向的 stdout，
  日志只在缓冲填满（4KB）时才落到文件——**表现就是"跑着跑着不打了，像卡死"**，
  实际最后几分钟的输出还躺在用户态缓冲里，进程被 kill 就永远丢了。
  排查多人问题花的时间有一半耗在这上面。

## 待办（按优先级）

0. **Win7 的 UCRT 怎么给**：随包带 app-local UCRT / 要求装运行库 / 干脆放弃 Win7 三选一，
   见「Windows 7 兼容」最后一节（`GetSystemTimePreciseAsFileTime` 那条已经修好了）。
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