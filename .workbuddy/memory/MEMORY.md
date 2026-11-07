# CppSekai — 项目记忆（索引）

> 权威文档在仓库：`AGENTS.md`（架构 + 全部坑，改代码前按章节查）、`CLI.md`、`SELECT-UI.md`、
> `JUDGE-WINDOWS.md`、`RESULT-*.md`、`GPU-GLITCH.md`、`RE.md`、`COPYRIGHT.md`。
> 细节翻 `.workbuddy/memory/2026-*.md`（append-only）。

## 协作约定
- 改完 + 验证过就 commit；**禁止 `git checkout <file>` / `git restore`** 撤临时改动（09-18 冲掉过整轮 main.cpp），用精确编辑或先 commit。
- 验证卡几分钟就停手，交代「人工点哪里、期望什么」。
- `game/` `platform/` 的 .cpp 走 glob；只有 `core/`、`third_party/` 要手工加进 `UPSTREAM_SOURCES`。
- 含中文注释的脚本用 Git Bash；含中文的 PowerShell 用 pwsh。
- python 改源码小心 `\n`（Bash 是 JSON）：用 `chr(92)+'n'` 或按位置切片。

## 高危坑
- `build.sh` 的 `-mcpu=baseline` 不能删（否则 AVX2/FMA 让老 CPU 静默 0xC000001D）；发布前跑 `cpu_isa_scan.py`。
- main.cpp 必须在 `#include <SDL.h>` 前 `#define SDL_MAIN_HANDLED`。
- `bash build.sh`（Git Bash），zig 0.14.1（0.16 吞 `-I`），全量 35~45s，`-Wall -Wextra` 保持 0 警告。
- `third_party/DirectXMath/Inc` 必须留在 `-I`（MinGW 有同名小写桩头）。
- Win7 补丁在 build.sh 顶部（`grep -c CPPSEKAI-WIN7` = 2）。
- 日志 = cwd 的 `cppsekai.log`，只在「没附加到控制台」时写；`( exe & )` 要和后续操作写同一条命令。
- 无头：`--screenshot <文件>` + `--no-party`；`PrintWindow` 抓不到 GL，只能靠游戏自己截图。
- 残留实例 `tasklist | grep cppsekai` → `MSYS_NO_PATHCONV=1 taskkill /PID x /F`。
- 无交互会话 ImGui 点不动 → 用调试开关（`--fake-pad` / `--drop-test` / `CPSEKAI_*`）；ImGui 卡片可 PostMessage 点客户区坐标。

## 数据 / 设置
- 根目录四个可选表 musics / music-vocals / music-levels / music-aliases.json，同步用 `update_music_db.py`。
- 曲库 id ≤804 走 assets.unipjsk.com，国服独占 11000+ 走 storage.sekai.best，别往 10000 以上放新 id。
- **加设置字段必须同时改 `SongSelect.cpp` 的 loadUserData 与 saveUserData**。

## UI 通则（1920×1080 虚拟画布 + 缩放）
- 一律 `px()/py()/ps()`；ImGui `AddText(font,size)` 的 size 是像素。
- 下拉框一律 `ui::combo`，别用原生 `ImGui::Combo`；两者都要接 `ui::padComboNudge`。
- 新模态卡片要把 `!game::xxxDialogOpen()` 加进 `escapePressed` 条件，否则 Select 下按 ESC 直接退出。
- 设置卡片 920×780、七页签、两列网格、七页都不滚；加行前用 `CPSEKAI_UI_TRACE=1 --settings --settings-tab N` 量。
- 观感基准 sekai-stories.pages.dev：文字色只有 `#444466`，阴影一律 `ui::dropShadow`，圆角 `14*s`，按下是反白。

## 窗口 / Win32 接缝
- 菜单栏在非客户区，`--screenshot` 拍不到 → `.workbuddy/tools/menu_probe.py`。
- SDL 会吃掉 `WM_CLOSE`（直接销毁窗口）、`WM_SYSKEY*` / `WM_SYSCHAR` / `WM_MENU*` 和 `SC_KEYMENU`。
- 手写 COM vtable 按位置寻址，GUID 对着 toolchain 头文件核。
- 命令共用挂起通道，每消费一条补 `EndMenu()`；「灰掉」只是 UI，处理端要再挡一道。

## 版权口径
官方 2026-04-27：确认使用外部非公式应用 → 禁参加官方大会/活动（资格罚）。风险：资格罚 > 下架 > 诉讼，没走 DMCA。「repo 404」不能归因。

## 最近工作（细节见日志）
- 11-07 演奏中顶部进度条可拖动 seek（音频/判定/渲染三层同步，score/combo 归零、跳过的音符不判 MISS）；
  产出 `HUD-UI-AUDIT.md`（HUD 是 SpriteRenderer + 世界坐标，prefab 无 rect，位置只能量截图）
  与 `HAIKU-PORT.md`（miniaudio 无 Haiku 后端 / llvmpipe + 无 swap interval 是两大障碍）。
- 11-06 flick 判定反汇编完成（`JUDGE-WINDOWS.md` §4）：门槛=单帧瞬时位移；方向错上限 Great；独立 flick 由 Began→下一帧 Moved 定案；自动 MISS 141.7ms。**尚未落进 Judgement.cpp**。
- 11-01 选曲几何大修（`SELECT-UI.md`）：基准 1920×1080 不换算；手机面板 704 prefab 几何；A0/A1/B 待拍板。
- 11-01 判定窗口按 master 表逐类型 + 左右不对称实现（`JUDGE-WINDOWS.md`）。
- 10-29 删除标题画面；教学描边 8 步 ≤2.5px；点击特效收敛。
- 10-25 触摸特效 `game/TapEffect`（参数来自 `ScreenLayerTouchEffect`，0.03ms）。
- 10-18 新手教学落地（`game/Story*`）；10-17 多人收尾；10-06 原生菜单栏 + Win 融合。
