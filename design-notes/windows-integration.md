# 与 Windows 的接缝

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 与 Windows 的接缝（2026-10-06 一批）

一夜之间补的四件事，都是"和桌面融合"的基础设施，各自都有坑：

- **`WM_CLOSE` 必须被窗口过程吞掉**（重要）。SDL 收到它会交给 `DefWindowProc`，
  **窗口当场被销毁**（实测 `0x0010` 后面紧跟 WM_DESTROY / WM_NCDESTROY），进程随后
  就走不下去了 —— 所以"演奏中点 X 先问一句"（`closeAsk`）在 SDL 那层做不了。
  做法：窗口过程拦下 WM_CLOSE → `SDL_PushEvent` 一个**自己登记的事件类型**
  （`closeRequestEventType = SDL_RegisterEvents(1)`）→ 主循环里
  `requestWindowClose()` 一处决策（演奏中就弹卡，其余直接 `running = false`）。
  窗口不销毁就一直是活的，用户选「继续演出」之后一切照旧。
  `SDL_WINDOWEVENT_CLOSE` 那条分支留着只当兜底（Windows 上一般走不到）。
- **UI 卡片只能被按钮关掉**：`game/Ui.cpp` 的 `eulaDialog` / `messageDialog` 出场动画是
  卡片自己的内部状态（`st.open`），调用方把标志置回 false **不会**关掉它（下一帧照样画）。
  想让 ESC 关掉一张卡，要么像猜歌那样在卡片里自己读键，要么走 `forcedChoice`
  替用户"按"某颗按钮 —— `closeAsk` 用的是后者（ESC → `closeAskPadChoice = 0`）。
  注意垫片那个每帧都跑的模态块会把 forcedChoice 覆盖成 -1，要加
  `if (xxxPadChoice < 0)` 让路。
- **`结束所有实例` 用的是"标记 + WM_CLOSE"两条消息**：`postCloseToAllInstances` 先发
  `RegisterWindowMessageW(L"CppSekaiForceQuit")`、再发 WM_CLOSE；收到标记的窗口
  （`subclass.forceQuit`）跳过演奏中那张确认卡。不加标记的话，一次"结束所有实例"会在
  每个正在演奏的窗口里各弹一张卡。`PostMessage` 有序，标记一定先到。
- **窗口位置**：`windowX` / `windowY`（-1 = 没意见 = 居中）。启动时**必须校验它落在
  某块当前接着的显示器上**（`SDL_GetDisplayUsableBounds` 取并集，只看左上角），
  否则拔了扩展坞就会开出一个看不见的窗口 —— 那天回落到居中并打一行日志。
  记录跟着 `persistUserData()` 走（**不要在 SDL_WINDOWEVENT_MOVED 里记**，那会每拖
  一下写一次盘）；全屏时不记（坐标是桌面的，跟"用户把窗口摆在哪"无关）。
- **`SetCurrentProcessExplicitAppUserModelID`**（`main.cpp` 顶部手写声明，
  shobjidl.h 里那个签名；shell32 已在链接行上）。**必须在建窗口之前调**。
  副作用：多开时所有窗口会**并到一个任务栏按钮**里 —— 多人游玩同时看几个窗口时
  靠 Alt+Tab / 缩略图切，不再是并排几个按钮。不想要就注释掉那一句。
- 窗口标题现在跟着**曲目**走（演奏中是 `CppSekai · 曲名 · 难度`），每帧调一次
  `syncWindowTitle()` —— 它看到标题没变就直接返回，几纳秒的事。
- 防屏保 / 防休眠：窗口没最小化就 `SetThreadExecutionState(ES_CONTINUOUS |
  ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED)`，最小化放行，状态变了才调。
- 拖放：`SDL_DROPFILE` 收 `.sus`，只在**选曲界面**生效（演奏/结算中间换曲会把那一局的
  收尾流程绕过去）。`--drop-test <path> [sec]` 能合成这个事件，真实拖放没法从脚本驱动。
- 媒体键：`SDLK_AUDIOPLAY` 演奏中 = 开关暂停卡片、选曲 = 回车；`AUDIONEXT/PREV` =
  上下方向键；`AUDIOSTOP` = ESC。**SDL2 没有 AUDIOPAUSE / TOGGLEPLAYPAUSE**，
  键盘上那颗播放/暂停合键落到 AUDIOPLAY。
- **任务栏缩略图工具栏按钮：做过又撤了**（2026-10-06 当天）。`ITaskbarList3` 的
  `ThumbBarAddButtons` 那套（悬停任务栏图标出 暂停/静音/返回选曲 三个小按钮）当天写完
  也验证过（`hr=0x0`，图标是 `CreateDIBSection` + `CreateIconIndirect` 现画的，颜色要按
  alpha 预乘），但用户随后决定不要，整个特性被移除 —— vtable 里为它加的
  `RegisterTab..ThumbBarUpdateButtons` 也一起删了。**留个记录是为了别再"重新发现"一遍**：
  真想加回去的话，vtable 位置寻址、`THUMBBUTTON` 的 ABI、`THB_ICON + hIcon`（省掉
  IImageList）这些结论都还成立。

### 批 D：`.sus` 文件关联 + 跳转列表（`platform/ShellIntegration.*`）

两件事装在一个模块里，都只碰 **HKCU、不要管理员**，失败一律降级成 no-op 打日志。

**`.sus` 关联**（设置 > 系统 > 关联 .sus 谱面文件，默认关；CLI `--associate-sus` /
`--unassociate-sus` 调的是同一对函数）：

- 写三处：`HKCU\Software\Classes\.sus`(默认值 → `CppSekai.Chart`)、
  `…\Classes\CppSekai.Chart\shell\open\command`(默认值 → `"<exe>" --sus "%1"`)、
  `…\Classes\CppSekai.Chart\DefaultIcon`。**exe 路径用 `GetModuleFileNameW`**，
  不要拼 baseDir（命令行给过相对路径时只有它是对的）。
- 撤销前**先读** `.sus` 现在的默认值：不是 `CppSekai.Chart` 就什么都不做（不去拆
  别人的关联），返回 false。设了之后 `.sus` 指向我们，再撤就是 `RegDeleteTreeW`
  两棵子树。
- 双击走的是已有的 `--sus <文件>`，那条路早就是"启动即演奏"。
- `HKCU\Software\Classes` 是**每用户**的：换账户、重装系统都不残留；想手动清就
  删这两棵子树。

**跳转列表**（右键任务栏图标 → 收藏 / 最近播放）：

- vtable 全部手写（`ICustomDestinationList` / `IShellLinkW` / `IPropertyStore`），
  加上自己实现的一个 `IObjectArray`（`LinkArray`，包一个 `vector<void*>`）。
  **GUID 全部对着 `toolchain/.../libc/include/any-windows-any` 的头文件核过**，
  别凭记忆写。
- 两个必须做的收尾：① `BeginList` 交出来的 **removed 数组要尊重** —— 我们是
  每次全量重建（`AppendCategory` 是追加语义，旧的不会自己清），不把它记下来又
  原样加回去的话，用户右键"从列表中删除"删一次、下次启动它又回来；比对用的是
  **参数串**（`--sus "路径"`），比抠路径稳；② `CommitList` 失败之后要
  `AbortList`，否则下一次 `BeginList` 一直报"上一次的事务没结束"。
- 显示名走 `IShellLinkW::QueryInterface(IPropertyStore)` + `PKEY_Title`。
  `PROPERTYKEY` = `{fmtid, pid}`，pid 是 **2**（不是 FLAGS 那种字段）。
- 建表时机两处：启动扫完谱面之后一次（`syncJumpList()`），以及演奏中
  `announceTrack` 记完最近播放之后一次。
- "最近播放"是 `settings.recentSusPaths`（跟 `favoriteMusicIds` 一样，
  `loadUserData` / `saveUserData` **两处都要改**，漏一处静默丢）。去重后插最前，
  留 10 条；写盘跟着 `persistUserData` 走。
- 路径已经没了的条目**跳过不加**（点了打不开比不显示更糟）——`GetFileAttributes`

### 批 E：打开谱面 / 编辑菜单 / 排序分组子菜单 / 演出能量（2026-10-06 晚）

- **文件 > 打开谱面…（`Ctrl+O`）**：`GetOpenFileNameW`（`main.cpp` 顶部的
  `browseForChart`，**必须带 `OFN_NOCHANGEDIR`** —— 不然对话框会把进程的 cwd 改掉，
  baseDir 那套相对路径全废）。选中的路径交给 `startDroppedChart(path, "open")` ——
  **跟拖放是同一条起奏路**（日志前缀就是第二个参数，拖放传 `"drop"`）。
  `Ctrl+O` 本身由键盘处理置 `openChartRequested`，帧内跟菜单那条命令汇合；
  菜单项上那个 `	Ctrl+O` 只是**提示**（没建加速键表，SDL 的泵不调
  `TranslateAccelerator`）。演奏中这项置灰 —— 演出中间换曲会把这一局的收尾流程绕过去。
- **编辑 > 删除…（删的是"一首歌"，不是一个文件）**：**全程序里唯一会毁掉用户数据的
  动作**，没有回收站。清单由 `collectDeleteTargets()`（main.cpp）在**提出请求的那一刻**
  算好并冻住，卡片只负责显示和确认：
  * 目标 = **同一个 `musicId` 的全部难度谱面**（列表里看到的本来也是"一首歌"）+ 曲绘
    （`ChartEntry::coverPath`）+ **所有歌手的 mp3**（`game::musicVocals(id)` 里每个
    `asset`，即 `<asset>.mp3`）+ 谱面自带的那个 BGM + sidecar（`<stem>.json` 和共用的
    `<id4>.json`）。认不出 `musicId` 的（文件名开头没编号）只删它自己一个，绝不猜。
  * **共用的文件不能连带删**：一张曲绘、一个 mp3 常常被同一首歌的几个难度同时用着。
    所以先算出"**没被删的那些谱面还认得哪些文件**"（谱面 / 曲绘 / 音频 / sidecar 全算），
    拿它过一遍筛子。这样"删掉最后一个难度"和"只删其中一个难度"自动落到对的行为上，
    不用分情况写。实测：两个难度 + 共用曲绘 + 共用 json + 一个老式 mp3 + 一个歌手 mp3
    → 6 个文件全删；同目录里另一首歌的 4 个文件一个没动。
  * 卡片写清楚曲名 + "将删除 N 个文件：几个谱面 / 几张曲绘 / 几个音频" + 目录，
    **默认那颗是"取消"**（`primary` 给左边，跟「要退出吗」那张卡正好反过来）。
  * `requestDelete()` 是**唯一入口**：菜单 编辑、曲目右键（`drawSongSelect`
    的 `deleteRequest` 出参）、`CPSEKAI_DELETEASK` 都走它，三个入口不可能算出不同的清单。
  * 删之前先打一份清单（`[delete] will remove N file(s)…` + 逐条），删的时候逐条打
    `removed` / `FAILED`，最后一行 `N/M file(s) gone` —— 用户问"删了什么"、
    脚本验"该删的删没删"都看这几行。
  * 删完**自己重扫一遍列表**并把光标停在原来的位置（不走 `rescanRequested`：
    那条路会把光标打回第 0 首）。
- **视图 > 排序方式 / 分组依据**：跟选曲界面两个下拉是同一个值，菜单开着时下拉收起。
- **演出能量**（设置 > 演奏，`UserSettings::liveBonus`，0..100，默认 5）：结算经验 =
  评级经验 × `game::liveBonusExpMultiplier(bonus)`。倍率是**官方的 ライブボーナス 表**
  （4.0.0 平衡调整后）：`0→×1  1..5→×5n  6..10→×20+n`；官方能消耗的档位到 10
  （平时上限 5、活动期间 10），**11 档以上是照 6..10 那条直线外推的、本项目自己的延伸**
  —— 代码注释、设置页那一行、CLI.md 都写明了，别把它说成官方数值。
  无头验证用 `--live-bonus <0..100>`（跟 `--player-rank` 一个性质，正常退出会写回档案）。
  实测：0→20、5→500（×25）、10→600（×30）、100→2400（×120）。
- 调试开关 `CPSEKAI_DELETEASK=1`：开局就把删除卡举到列表第一首上（截图 / 无头驱动用；
  `PrintWindow` 抓不到 GL 画面，只有游戏自己的 `--screenshot` 拍得到那张卡）。

