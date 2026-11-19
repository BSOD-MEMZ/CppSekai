# 原生菜单栏

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 原生菜单栏（2026-10-06，`platform/NativeMenu.*` + main.cpp 的 subclass）

设置 > 系统 > **原生菜单栏**（`UserSettings::nativeMenuBar`，**默认关**）：给窗口挂一条
真正的 Win32 菜单 —— **文件 / 编辑 / 视图 / 帮助**：

- 文件：打开谱面…(`Ctrl+O`) / 设置… / 刷新谱面列表(`F5`) / 音乐商店… / 结束当前实例 /
  结束所有实例…
- 编辑：加入收藏夹（当前曲目已在收藏里时显示"取消收藏"）/ 删除谱面文件…。
  **没有当前曲目（非选曲界面 / 列表为空）时整条菜单置灰** —— 里面两项都是"对当前这首
  做点什么"（`State::songSelected`）。
- 视图：窗口模式 / 排序方式 / 分组依据（三组都是单选点）/ 显示帧率 / 显示播放进度条 /
  弱化打击特效。
- 帮助：关于本软件 / 许可与免责声明 / GitHub / 访问 xxtsoft。

开启时 `drawSongSelect` 收到 `nativeMenuBar=true`，顶栏那三个**窗口级**按钮
（刷新 / 音乐商店 / 设置）**和排序 / 分组两个下拉**都不画（`headerFileButtons` /
`headerSelectors`），坐标从一个游标往后排，**猜歌 / 随机 / 切换歌手保留**。
排序 / 分组是**同一个值**（`settings.sortMode` / `settings.groupMode`），菜单和下拉
读写同一份，所以不会打架。关闭时一切照旧。

- **挂载条件**（main.cpp 的 `refreshNativeMenuBar`，一帧跑完在 `runFrame` 外面调）：
  设置开着 && `windowMode == 1`（windowed）&& `noFrameMode == 0`。
  菜单栏画在**非客户区**：borderless 是 `WS_POPUP` 画不出来，fullscreen 铺满桌面，
  玻璃实现 = 自绘无框（`WM_NCCALCSIZE -> 0`）把非客户区整个交了出去。所以全屏 /
  无框时设置项留着、菜单先摘掉，回普通窗口再挂回来。注意 `glassMode 2` 只在
  背景 = 透明（Aero 玻璃）时才真生效（`bgStyle != 2` 会被强制回 0），所以
  「没开玻璃背景却挂上了菜单」不是 bug。
- **SetMenu 只动非客户区**：窗口外框不变，客户区矮一条菜单栏（实测 1000x600 →
  1000x580，20px）。SDL 会把它当 `SIZE_CHANGED` 发出来，游戏自己重排；
  设置 > 画面的"分辨率"本来就是客户区尺寸，不用补偿。
- **菜单是模态的**：一展开 Windows 就进自己的模态循环，SDL 的泵被停住 —— 和拖标题栏
  同一个病。所以 subclass 里 `WM_ENTERMENULOOP` 把 `dragging` 也置上 + 起
  `kDragTimerId`，复用拖动那套喂帧；`WM_EXITMENULOOP` 收尾。
  **`WM_TIMER` 那条"左键抬起 = 拖动结束"的兜底必须让路**（`!st->menuOpen` 才判）：
  菜单是点一下开、松开后还开着，照判会把喂帧定时器当场杀掉。
  * **每消费一条菜单命令要补一次 `EndMenu()`**：命令是从**信箱**来的，不是菜单自己发的，
    所以菜单的模态循环不会因为它退出。按 Alt 把菜单栏拉高（菜单模式，泵停在
    DefWindowProc 里）之后再来一条命令，窗口就一直卡在菜单模式里 —— 连
    `running = false` 都没用，主循环根本没在跑（实测：改之前 `send 105` 之后进程还在）。
    `EndMenu()` 结束**本线程**的活动菜单，没有菜单在跑时只是返回 FALSE。
    `menuOpen` 是 `SubclassState` 的成员、而那个结构体在 `runFrame` 之后才声明，所以
    帧内读的是镜像 `menuIsOpen`（窗口过程在 ENTER/EXITMENULOOP 时同步它）。
    ⚠ **这一句只能放在 `if (menuCommand >= 0)` 分支里面**（"确实消费了一条命令"）。
    放到分支外就是**每帧一次** —— 菜单一打开，下一帧就被自己关掉，表现成
    "鼠标点一下菜单栏、闪一下就没了"（2026-10-06 真踩到过，用户报的）。
- **Alt / 访问键（mnemonic）必须在窗口过程里放行给 DefWindowProc**（2026-10-06 修，
  用户报的"按 Alt 不出下划线、Alt+字母没反应"就是这个）。**SDL 的窗口过程把这一族
  消息吃了**：它把 `WM_SYSKEYDOWN` 当普通按键喂 `SDL_KEYDOWN`，于是 DefWindowProc
  永远收不到，"进菜单模式 / 显示访问键下划线 / Alt+字母打开对应菜单"全都不发生。
  菜单挂着时（`platform::menu::attached(hwnd)`）把这些**原样转给 `DefWindowProcW`**：
  `WM_SYSKEYDOWN` / `WM_SYSKEYUP` / `WM_SYSCHAR` / `WM_MENUCHAR` / `WM_INITMENU` /
  `WM_INITMENUPOPUP` / `WM_UNINITMENUPOPUP` / `WM_MENUSELECT`，**外加
  `WM_SYSCOMMAND` 里的 `SC_KEYMENU`**。
  * `SC_KEYMENU` 是**最后一道门**，漏了它前面全白做：DefWindowProc 收到 Alt 之后会
    把"请你把菜单打开"发回来 —— 实测 `Alt 按一下 → 0x0112 wParam=0xF100 lParam=0`
    （拉高菜单栏）、`Alt+V → 0x0112 wParam=0xF100 lParam=118`（118 = 'v'）。
    这条也是 SDL 吞掉的。
  * **只转 `SC_KEYMENU`**，`WM_SYSCOMMAND` 的其余项（`SC_SCREENSAVE` /
    `SC_MONITORPOWER`）要留给 SDL 挡 —— DefWindowProc 对那两个是**真的会**起屏保 /
    关屏的。
  * 回归：`.workbuddy/tools/winsend.exe CppSekai real 18`（真·Alt，见下面「工具」一节）
    + `CPSEKAI_MSG_LOG=1`，日志里要出现 `0x0211`(WM_ENTERMENULOOP)；`realalt 86` 之后
    再出现 `0x0117 lParam=2`（视图 那个 popup 被初始化）。
- **命令不在窗口过程里执行**：`WM_COMMAND` 只把 id 记进 `menuCommand` 信箱
  （`HIWORD(wParam)==0` 且落在 `CmdSettings..CmdXxtsoft` 区间才算菜单命令），
  主循环在帧内消费。喂帧是递归的（WM_TIMER → 帧 → 泵 → WM_COMMAND），这块既可能
  在某次喂帧里跑到、也可能在正常帧里跑到，两边都得成立。
- 勾选/单选/置灰由 `platform::menu::sync()` 打：窗口模式 / 排序方式 / 分组依据三组都是
  `CheckMenuRadioItem`（**逐个 CheckMenuItem 会画出好几个勾**）。sync 会重画菜单栏，
  所以只在值变化时调。演奏中把 刷新谱面列表 / 音乐商店 / 打开谱面 置灰（重扫会卡那一帧，
  下载器和"换一首打"更不该从演出中间弹出来）。
  * 两个**踩过的坑**：① `GetSubMenu(h, n)` 的 n 是"第 n 个**项**"，**分隔线也占位置**
    （碰到分隔线返回 nullptr）—— 视图 里 排序方式/分组依据 是 2 和 3，写成 1 和 2 的话
    两个子菜单一个勾都打不上（dump 出来全是空的）。② `ModifyMenuW`（改收藏项文案）会把
    那一项**整个重新加回去**，顺手把 enabled 状态刷成可用 —— 所以**先改文案、后置灰**，
    反过来的话空列表下"加入收藏夹"还是亮的。
- **演奏中打开菜单自动暂停**（用户选的行为）：`WM_ENTERMENULOOP` 置
  `menuPauseWanted`，帧内看到且 `state == Play && session.active` 就调
  `requestPause()`（多人里非房主会被现有的规则挡掉）。选曲/结算不暂停。
- 结束所有实例 走 **`MessageBoxW` 是/否确认**（菜单栏本来就是原生外观，没套游戏卡片）；
  它是在某次喂帧里弹的模态框，嵌套帧被 `st->depth` 挡住，不会有第二帧挤进来。
- 验证用 `.workbuddy/tools/menu_probe.py`（rect / dump / send <id> / menuloop enter|exit）：
  菜单栏在非客户区，`--screenshot`（只拍 GL 帧）看不见它，只能从外面用 user32 查。
  `menuloop enter` 是**合成**的 WM_ENTERMENULOOP，用来在无头会话里驱动喂帧分支 ——
  实测 enter 后 `[window] drag frames: 30 (… timer=30)`，exit 后干净收尾。

