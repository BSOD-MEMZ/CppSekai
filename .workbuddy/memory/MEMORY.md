# CppSekai — 项目长期记忆（索引）

> **权威文档在仓库里**，不是这个文件：`AGENTS.md`（245KB，架构 / 全部坑 / 各功能一节，改代码前按章节查）、
> `CLI.md`（命令行 + 无头自检）、`CHARTS.md`、`CODE-REVIEW.md`（体量）、`COPYRIGHT.md`。
> 这里只留跨会话必须记住的**约定**和**入口索引**；细节翻 `.workbuddy/memory/2026-*.md`（append-only）。

## 协作约定（用户明确要求）
- **改完 + 验证过就 commit**，别攒着。
- **禁止 `git checkout <file>` / `git restore` 撤临时改动**（09-18 冲掉过整轮 main.cpp 改动）。
  撤动用精确编辑或先 commit。原话：「不要乱 checkout，有问题我们手动改」。
- 加新 .cpp 到 `game/` / `platform/` 必须同步加进 `build.sh` 的 `SOURCES`。
- 中文注释脚本用 Git Bash；含中文的 PowerShell 用 pwsh。
- **验证卡住超过几分钟就停手**，把「人工点哪里、期望什么」交代给他。

## 高危坑
- `build.sh` 的 `CXXFLAGS` 里 **`-mcpu=baseline` 不能删**：zig 不给 `-mcpu` 默认 native →
  产物带 AVX2/FMA/AVX-VNNI，老 CPU「看完启动画面静默消失」(0xC000001D)。
  发布前跑 `.workbuddy/tools/cpu_isa_scan.py`（package.sh 已接闸）。
- `main.cpp` 必须在 `#include <SDL.h>` 前 `#define SDL_MAIN_HANDLED`。
- `bash build.sh`（Git Bash），zig 0.14.1（别换 0.16，吞 `-I`），全量 35~45s。
  警告开关开着（`-Wall -Wextra`）**保持 0 警告**；判断只能跑完整 build.sh 数 error/warning。
- ⚠ `third_party/DirectXMath/Inc` 必须留在 `-I`（不能 `-isystem`）：MinGW 有同名小写桩头。
  上游/第三方 .cpp 走 `UPSTREAM_SOURCES` + `-w`，新增要手工加。
- **Win7 补丁在 build.sh 顶部**（`grep -c CPPSEKAI-WIN7` = 2 断言），toolchain 重解压会自动重打，别删。
  复查用 `pe_imports.py`。
- 日志 = cwd 的 `cppsekai.log`；**只在"没附加到控制台"时才写**（`run_in_background` 会附加 → 没日志文件）。
  `( exe & )` 必须和后续操作写在**同一条命令**里（跨调用子进程会被带走）。
- 无头：`--screenshot <文件路径>`（给目录静默失败，父目录要先存在）；`--no-party` 别忘。
- 残留实例：`tasklist | grep cppsekai`（Get-Process 看不到）→ `MSYS_NO_PATHCONV=1 taskkill /PID x /F`。
- 无交互会话下 PostMessage 点不动 ImGui → 加调试开关（`--fake-pad` / `--chartdl-test`）；
  `--fake-pad NONE` = 伪造"插了个不会按的手柄"。
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
  分支条件，否则 Select 下按 ESC 直接退出游戏。
- 卡片里键盘操作用 `ImGui::IsKeyPressed(k, false)`（false 关 auto-repeat）。
- 设置卡片 380x800，页签余量很小（演奏 515 / 账户 488）；加行先 `CPSEKAI_UI_TRACE=1` 看 `used`。
- 观感基准 = sekai-stories.pages.dev：文字色只有 `#444466`；**浮起阴影一律 `ui::dropShadow`
  （零偏移晕，衰减曲线实测过别改参数）**；卡片圆角 `14*s`；**按钮按下是反白不是变深**。细则见 2026-10-03。

## 窗口外观（Win32 / DWM）
- 主窗口是标准带边框窗口；创建时因启动图临时 `SDL_WINDOW_BORDERLESS`，`splashShown` 后装回来。
- `applyWindowTransparency` 的 `DwmExtendFrameIntoClientArea(-1,-1,-1,-1)` 顺带吃掉了 Win10 的 1px 黑边
  （副作用，不是特意去边）。NC 区仍在：窗口比客户区左右下各多 8px、上 32px，截图会露透明。
- 量矩形用 `.workbuddy/tools/win_window.py rect`。

## ⚠ 版权口径（2026-09-24 更新，别再引旧文档）
- 官方 2026-04-27 公告：SNS 上被确认使用「外部非公式应用」→ **禁参加官方大会/活动（资格罚）**。
  风险优先级：**资格罚 > 平台下架 > 诉讼**；相关关键词在 github/dmca 里 0 条 → 没走 DMCA。
  **「repo 404」不能归因**，别拿别人 repo 的状态当决策依据。

## 最近工作（细节 → AGENTS.md 对应小节 + 当日日志）
- 10-04：模态卡片 ESC 优先级、UI 复核。10-03：观感对齐 sekai-stories 规格。
- 09-24：设置卡片一批（账户导入导出 / 结束实例 / 判定预设、`ui::radioRow`、震动三勾、页签拖动）。
- 09-20：chartdl 数据源 + 猜歌 + 手柄焦点环。09-19：多人、结算改版、Win7 三连修、内存审计。
