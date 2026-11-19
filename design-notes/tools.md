# 工具

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

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
- `.workbuddy/tools/update_music_db.py` → **曲库总同步**（`musics.json` + `music-vocals.json`
  + `music-levels.json`，日服全量 + 国服独占曲）。曲库是**本地表**，不跑它就会永远停在抓表那天 ——
  表一旧，新歌既搜不到也下不了（GUI 列表和 `--download` 都要先从表里查到 `assetbundleName`
  才能拼 URL）。2026-09-19 实测：本地停在日服 id 804，当日日服已经到 811，缺 敗走 / ヘレディティ
  两首，而它们的资源在 unipjsk 上本来就是 200 —— 纯属表没跟上。
  **定数表同理，而且缺它更阴**：2026-09-20 本地 `music-levels.json` 停在 id 804 且**完全没有国服
  17 首**，后果是 chartdl 里那些歌 5 个难度框全灰（见「下载器」一节）。
  **拉新歌 / 换版本就重跑它**，`--check` 只报告。
  - 源：日服 `Sekai-World/sekai-master-db-diff`（719 首，最全；备选 Team-Haruki 的
    `haruki-sekai-master`）；国服 `Team-Haruki/haruki-sekai-sc-master`（实时收集；
    备选 Sekai-World 的 `sekai-master-db-cn-diff`）。每侧都配了备用源，抽风会自动往下试。
  - **每个仓库都配了 raw.githubusercontent + jsDelivr 两条地址**：国内直连 raw 会整段不通
    （2026-09-20 实测 curl 直接 000 超时），只留 raw 的话"表旧了重新同步一次"根本跑不起来。
  - 国服独占曲按 `id >= 10000` 挑（日服表最大 811）。
  - **读音得手工补**：国服表把独占曲的 `pronunciation` 填成了作曲者名（"Mitchie M"、"敌门"）。
    照抄会毁掉排序（两个界面都按读音排）和罗马音搜索 —— 脚本里 KANA_OVERRIDE 手工补：
    中文标题填**拼音**（`game/SongSelect.cpp` 有一条原文比对，输入 "yiyang" 能命中「一样」），
    日文/英文标题填**假名**（走罗马音路径）。
  - 定数表由两边的 `musicDifficulties.json` 合并：**日服那份管 id < 10000，国服那份管独占号段**，
    只保留 `musics.json` 里真有的 id（表跑到歌单前面去没意义）。
  - musics / music-vocals 是**单行紧凑 JSON**（`separators=(",",":")` 整体写回，diff 才只有一行）；
    **music-levels.json 反过来是一行一首、按 id 升序**，它有自己的 `write_levels()`，别顺手改格式。
  - 它同时吸收了原来的 `gen_music_vocals.py`（从 musicVocals + gameCharacters 生成演唱版本表）。
- `.workbuddy/tools/fetch_music_aliases.py` → **社区曲目别名表** `music-aliases.json`
  （`{musicId: [别名…]}`，703 首 / 12,896 条 / 185 KB）。数据来自 HarukiBot 的公开 API
  （`neo-api.haruki.seiunx.com/api/bot/v2/pjsk/alias/music/<id>`，社区提交 + 审核，
  后端是 Team-Haruki 的 Haruki-Cloud）。**抓一次存成本地表**：单机游玩器不该把搜索挂到
  网络上 —— 断网就废、还多一个隐私面。玩法是一首一首查、并发 8，700 多首约一分钟；
  别名是社区持续补充的，隔一阵重跑一次就好。纯数字别名会被丢掉（输入 id 本来就能搜到）。
- `.workbuddy/tools/winsend.c` → `build/winsend.exe`：按窗口标题找窗口再送假输入，
  无交互会话下驱动 UI（动作：`click x y` / `move x y` / `key <vk>` / `focus` /
  `place x y` / `rect`；见「平台 / 输入相关的坑」）。三个和键有关的动作区别很大：
  * `key <vk>` —— `PostMessage` 合成一笔按键。够用于普通键（F5 / ESC / 媒体键），
    **但对"修饰键 + 键"和系统键没用**（见下）。
  * `syskey <vk>` —— `PostMessage` 一笔 `WM_SYSKEYDOWN/UP`（Alt / F10 那类）。
    能把消息送进窗口过程，但**没人会进菜单模式**：DefWindowProc 判这个要看真实的
    修饰键状态。
  * `chord <modvk> <vk>` —— 修饰键 + 一个键（Ctrl+O 这种），合成按下/抬起。
    SDL 自己维护修饰键状态，所以 `mod` 里能带上 `KMOD_CTRL`。
  * `real <vk>` / `realalt <vk>` —— **真·键盘输入**（`SendInput`，走驱动那一层，
    `GetKeyState` 真的会变）。`realalt` = Alt 按住 → 键按一下 → Alt 放开。
    **只有这两个能测出菜单的访问键**（Alt 下划线 / Alt+字母）。
    两个细节：① 必须**拆成多次 `SendInput` 调用、中间留几十毫秒**，一整批灌进去的话
    应用取到第一条消息时异步键状态已经翻到最终值了；② 要带**扫描码**，不带的话
    Windows 把 `VK_MENU` 当右 Alt（扩展键）发出去，DefWindowProc 按 AltGr 处理。
    `real` 会打一行 `real: target=… foreground=…`，注入失败（沙箱里 `SetForegroundWindow`
    会被拒）时重试一次就好。
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

