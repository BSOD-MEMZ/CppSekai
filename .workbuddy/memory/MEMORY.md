# CppSekai — 项目长期记忆（索引）

> **权威文档在仓库里**，不是这个文件：`AGENTS.md`（架构 / 全部坑 / 各功能一节，改代码前按章节查）、
> `CLI.md`（命令行 + 无头自检）、`CHARTS.md`、`CODE-REVIEW.md`（体量）、`COPYRIGHT.md`。
> 这里只留跨会话必须记住的**约定**和**入口索引**；细节翻 `.workbuddy/memory/2026-*.md`（append-only）。

## 协作约定（用户明确要求）
- **改完 + 验证过就 commit**，别攒着。**禁止 `git checkout <file>` / `git restore` 撤临时改动**
  （09-18 冲掉过整轮 main.cpp 改动）；撤动用精确编辑或先 commit。
- **验证卡住超过几分钟就停手**，把「人工点哪里、期望什么」交代给他。
- `game/` 与 `platform/` 的 .cpp 走 **glob**（`build.sh` 的 `SOURCES`），新加文件不用改脚本；
  只有 `core/`、`third_party/` 要手工加进 `UPSTREAM_SOURCES`。
- 中文注释脚本用 Git Bash；含中文的 PowerShell 用 pwsh。

## 高危坑
- `build.sh` 的 `-mcpu=baseline` **不能删**（zig 不给 `-mcpu` 默认 native → 产物带 AVX2/FMA，
  老 CPU 静默消失 0xC000001D）。发布前跑 `.workbuddy/tools/cpu_isa_scan.py`（package.sh 已接闸）。
- `main.cpp` 必须在 `#include <SDL.h>` 前 `#define SDL_MAIN_HANDLED`。
- `bash build.sh`（Git Bash），zig 0.14.1（别换 0.16，吞 `-I`），全量 35~45s，
  `-Wall -Wextra` **保持 0 警告**；只能跑完整 build.sh 数 error/warning。
- ⚠ `third_party/DirectXMath/Inc` 必须留在 `-I`（不能 `-isystem`）：MinGW 有同名小写桩头。
- **Win7 补丁在 build.sh 顶部**（`grep -c CPPSEKAI-WIN7` = 2 断言），toolchain 重解压会自动重打；
  复查用 `pe_imports.py`。
- 日志 = cwd 的 `cppsekai.log`，**只在"没附加到控制台"时才写**（`run_in_background` 会附加）；
  `( exe & )` 必须和后续操作写在**同一条命令**里（跨调用子进程会被带走）。
- 无头：`--screenshot <文件路径>`（给目录静默失败，父目录要先存在）；`--no-party` 别忘。
- 残留实例：`tasklist | grep cppsekai`（Get-Process 看不到）→ `MSYS_NO_PATHCONV=1 taskkill /PID x /F`。
- 无交互会话下 PostMessage 点不动 ImGui → 用调试开关（`--fake-pad` / `--drop-test` /
  `--chartdl-test`）；ImGui 卡片可以直接 PostMessage 点在**客户区坐标**上（实测有效）。
- 拖动窗口会挂住消息泵 → 已用 `SDL_SetWindowsMessageHook` 变成静默暂停。

## 目录 / 数据 / 设置
- 根目录四个**可选**表：`musics.json` / `music-vocals.json` / `music-levels.json` / `music-aliases.json`。
  同步用 `update_music_db.py`（`--check` 只报告）。曲库按 id 分派：≤804 走 assets.unipjsk.com，
  国服独占 11000+ 走 storage.sekai.best。**别往 10000 以上放新 id**。
- `.gitignore` 忽略 assets/ charts/ build/ toolchain/ userdata.json profiles/ chartdl.json；
  **新增 assets/ 子目录要 `git add -f`**。`.workbuddy/` 入库。
- **加设置字段必须同时改 `SongSelect.cpp` 的 `loadUserData` 与 `saveUserData`**，漏一处静默丢设置。

## UI 通则（1920x1080 虚拟画布 + 缩放）
- HUD / 结算 / 选曲都走 `px()/py()/ps()`；ImGui `AddText(font,size)` 的 size 是像素，虚拟单位自己乘 scale。
- 自绘控件（`ui::slider/checkBox/radioRow/stepper/capsuleButton`）键盘焦点够不着 → 手柄靠
  `ui::PadScope + padNav`；新增原生 `ImGui::Combo` 要接 `ui::padComboNudge`。
- **模态卡片要和主循环 ESC 抢优先级**：新加卡片要把 `!game::xxxDialogOpen()` 加进 `escapePressed`
  分支条件，否则 Select 下按 ESC 直接退出游戏。卡片里键盘用 `ImGui::IsKeyPressed(k, false)`。
- 设置卡片 380x800，页签余量很小；加行先 `CPSEKAI_UI_TRACE=1` 看 `used`。
- 观感基准 = sekai-stories.pages.dev：文字色只有 `#444466`；**浮起阴影一律 `ui::dropShadow`**
  （零偏移晕，衰减曲线实测过别改参数）；卡片圆角 `14*s`；**按钮按下是反白不是变深**。

## 窗口外观 / 与 Windows 的接缝（细节 → AGENTS.md 对应节 + 2026-10-06 日志）
- 窗口是标准带边框窗口；`applyWindowTransparency` 的 `DwmExtendFrameIntoClientArea(-1,-1,-1,-1)`
  顺带吃掉 Win10 的 1px 黑边。NC 区仍在（左右下各 8px、上 32px），截图会露透明。
- 量矩形 `.workbuddy/tools/win_window.py rect`；菜单栏/缩略图按钮在**非客户区**、`--screenshot`
  拍不到 → `.workbuddy/tools/menu_probe.py`（rect/dump/send/menuloop）。
- 模块：`platform/NativeMenu.*` 原生菜单栏、`platform/ShellIntegration.*` .sus 关联 + 跳转列表、
  `SystemMedia.cpp` 手写 vtable 的 SMTC / 任务栏进度条 / 缩略图按钮。
- **SDL 会把 `WM_CLOSE` 交给 `DefWindowProc`、窗口当场销毁** → 想"先问一句"必须在窗口过程里吞掉，
  用 `SDL_RegisterEvents` 自定义事件通知主循环。手写 COM vtable **按位置寻址**，要用第 N 个方法
  就得把前面全声明出来；GUID 对着 `toolchain/.../any-windows-any` 头文件核，别凭记忆写。
- 菜单/缩略图命令共用一条挂起通道（`WM_COMMAND` → 主循环帧内消费）。**「灰掉」只是 UI，
  处理端要另挡一道**。跳转列表要尊重 `BeginList` 的 removed 数组，`CommitList` 失败要 `AbortList`。

## ⚠ 版权口径（2026-09-24 更新，别再引旧文档）
- 官方 2026-04-27 公告：SNS 上被确认使用「外部非公式应用」→ **禁参加官方大会/活动（资格罚）**。
  风险优先级：**资格罚 > 平台下架 > 诉讼**；相关关键词在 github/dmca 里 0 条 → 没走 DMCA。
  **「repo 404」不能归因**，别拿别人 repo 的状态当决策依据。

## 最近工作
- 10-06：原生菜单栏 + 「与 Windows 融合」四批（防休眠/拖放/媒体键、窗口位置/标题/关窗确认、
  缩略图按钮、.sus 关联/Jump List）。10-04：模态卡片 ESC 优先级。10-03：观感对齐 sekai-stories。
- 09-24：设置卡片一批。09-20：chartdl 数据源 + 猜歌 + 手柄焦点环。09-19：多人、结算改版、Win7 三连修。
