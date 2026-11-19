# 下载器

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

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
- **搜索是大小写不敏感的**（2026-09-20 修）：`matchesFilter` 以前是裸字节比较，`hype` 搜不到
  「Hype Dive」，只有照抄表里的大小写才行。现在查询和标题一起过 `foldCase()`（只折 ASCII；
  假名/汉字无大小写，原样比较），别名表加载也走同一个函数。
- **命令行参数按 UTF-8 重新读一遍**（2026-09-20）：GUI 子系统的 `argv` 是 **ANSI 代码页**解出来的，
  `chartdl.exe --list 镜中少女` 到手就是乱码、永远匹配不上。`main()` 开头用
  `utf8Args()`（`CommandLineToArgvW` + `toUtf8`）重取，解析循环读的是 `args` 不是 `argv`。
- **难度框的可用性不能只看定数表**（2026-09-20 修的 bug，症状很有欺骗性）：
  `updateDetailPanel` 原来用 `available = level > 0` 判断"这首有没有这一档"，
  而定数表 `music-levels.json` 一旦没有这首歌的行，五档全是 0 → **5 个难度框全部灰掉、取消勾选**，
  点「下载勾选的歌曲」只能下到曲绘 + BGM，谱面一个都不下（2026-09-20 用户报的正是这个：
  国服 17 首独占曲在表里一行都没有）。现在 `Song::levelsKnown` 记录"表里到底有没有这首歌"：
  没有 → 一律当成"有"，定数显示仍走 0（界面显示 `-`）；有 → 才用 `level > 0` 判断。
  启动日志会打 `[data] N song(s) have no levels in music-levels.json ...`，**别把它当成正常输出**，
  那是表旧了（跑 `.workbuddy/tools/update_music_db.py`）。
- **`--list` 之外没有别的搜索入口**，所以改 `matchesFilter` 要顺手跑一遍：
  `--list hype` / `--list 镜中少女` / `--list はいぷ` / `--list imasugurinne`（罗马音）/
  `--list tyw`（别名）—— 五条覆盖五个匹配分支。
- **一个文件只排一个任务**：`queueSong` 的 `add()` 会先在 `gJobs` 里查同路径，重复的直接丢。
  起因是「下载内容」面板曾经给同一个演唱版本建出**两个勾选框**（同一个 `kIdVocalBase + v` 的
  id 在同一个父窗口下出现两次，`GetDlgItem` 只能拿到一个，而排队读的是 `gVocalChecks` 全部），
  同一个 BGM 被下两遍。面板那边加了 `GetDlgItem` 守卫挡住重复行，队列这边兜底。
  （2026-09-20 的 09-20 打包版里两个「バーチャル・シンガーver.」行就是这个，
  **重复行为什么会产生没查出根因**：探针显示一次 `updateDetailPanel` 里难度循环跑了两遍、
  演唱版本循环也跑了两遍，但函数入口探针只打了一次 —— 行为像"从中间又跑了一遍"。
  现在两处都不再依赖这个成因成立与否。）

