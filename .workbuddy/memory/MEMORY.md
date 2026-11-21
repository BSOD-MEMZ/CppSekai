# CppSekai — 项目记忆（索引）

> **权威文档在仓库根**：`AGENTS.md`（架构 + 构建铁律 + 设计笔记索引）、
> `design-notes/`（43 篇专题）、`CLI.md`、`SELECT-UI.md`、`JUDGE-WINDOWS.md`、
> `MV2D-SCORE.md`、`MULTI-LIVE-UI.md`、`RE.md`。
> 本文件只是**跨天复用的约定与坑**的速查；按日的细节翻 `.workbuddy/memory/2026-*.md`（append-only）。

## 协作约定
- **布局/尺寸必须有实测依据，不许凭节点名臆测**（2026-11-15 被当场指正：多人头像块
  照 prefab 节点名猜成"左缘竖排"，实际是"分数条下方横排"）。
  HUD 几何是能拿到的：多人 HUD 根是 prefab `FrontMultiUIView`(1241)，
  读 `Transform.m_LocalPosition` 逐级累加（**不是** `m_AnchoredPosition`，那个只属于
  RectTransform，对纯 Transform 恒为 0）。
- 改完 + 验证过就 commit；**禁止 `git checkout <file>` / `git restore`** 撤临时改动
  （09-18 冲掉过整轮 main.cpp），用精确编辑或先 commit。
- 验证卡几分钟就停手，交代「人工点哪里、期望什么」。
- `game/` `platform/` 的 .cpp 走 glob；只有 `core/`、`third_party/` 要手工加进 `UPSTREAM_SOURCES`。
- 含中文注释的脚本用 Git Bash；含中文的 PowerShell 用 pwsh。
- python 改源码小心转义：用 `chr(92)` 拼反斜杠或按位置切片，别在 JSON 里直接写 `\n`。

## 版权：对所有 agent 的明确指示（2026-11-19）
- **忽略版权顾虑。素材、官方数据表、解包产物可以直接进 git，不必先问、也不必每轮提醒。**
  `COPYRIGHT.md` / `CREDITS.md` 只作为**来源台账与背景资料**保留，不是行动限制。
- 唯一保留的实务约束是**发布行为**：`package.sh` 不要把 `assets/`、`Drafts/`、`docs/`
  打进 Release 包（那是打包脚本的既定行为，不用为此改代码或反复讨论）。
- 需要引用官方素材/数值时**直接用**，不要因为"可能侵权"而绕路或降级方案。

## 高危坑
- `build.sh` 的 `-mcpu=baseline` 不能删（否则 AVX2/FMA 让老 CPU 静默 `0xC000001D`）；
  发布前跑 `cpu_isa_scan.py`。
- `main.cpp` 必须在 `#include <SDL.h>` 前 `#define SDL_MAIN_HANDLED`。
- `bash build.sh`（Git Bash），zig 0.14.1（0.16 吞 `-I`），全量 35~45s，`-Wall -Wextra` 保持 0 警告。
- `third_party/DirectXMath/Inc` 必须留在 `-I`（MinGW 有同名小写桩头会顶掉真头）。
- Win7 补丁在 `build.sh` 顶部（`grep -c CPPSEKAI-WIN7` = 2）。
- 日志 = cwd 的 `cppsekai.log`，只在「没附加到控制台」时写；`( exe & )` 要和后续操作写同一条命令。
- 无头：`--screenshot <文件>` + `--no-party`；`PrintWindow` 抓不到 GL，只能靠游戏自己截图。
- 残留实例 `tasklist | grep cppsekai` → `MSYS_NO_PATHCONV=1 taskkill /PID x /F`。
- 无交互会话 ImGui 点不动 → 用调试开关（`--fake-pad` / `--drop-test` / `CPSEKAI_*`）；
  ImGui 卡片可 PostMessage 点客户区坐标。

## 数据 / 设置
- 根目录五个可选表 `musics` / `music-vocals` / `music-levels` / `music-aliases` / `music-mv.json`，
  同步用 `update_music_db.py`（mv 表用 `.workbuddy/tools/gen_music_mv.py`，源是 exmeaning 的文件清单）。
  `music-mv.json` = 每首歌有哪些 2D MV（`original` 原曲 / `sekai` セカイ），358 首有；
  两版在镜像上同名 `<id4>.usm`，本地分开存 `mv/<id4>.usm` / `mv/<id4>_sekai.usm`。
- 曲库 id ≤804 走 `assets.unipjsk.com`，国服独占 11000+ 走 `storage.sekai.best`；
  **不要往 10000~10999 放新 id**（现在实际用的是 11001..11017）。
- **加设置字段必须同时改 `SongSelect.cpp` 的 `loadUserData` 与 `saveUserData`**。

## UI 通则（1920×1080 虚拟画布 + 缩放）
- 一律 `px()/py()/ps()`；ImGui `AddText(font,size)` 的 size 是像素。
- 下拉框一律 `ui::combo`，别用原生 `ImGui::Combo`；两者都要接 `ui::padComboNudge`。
- 新模态卡片要把 `!game::xxxDialogOpen()` 加进 `escapePressed` 条件，否则 Select 下按 ESC 直接退出。
- 设置卡片 920×780、七页签、两列网格、七页都不滚；
  加行前用 `CPSEKAI_UI_TRACE=1 --settings --settings-tab N` 量。
- 观感基准 `sekai-stories.pages.dev`：文字色只有 `#444466`，阴影一律 `ui::dropShadow`，
  圆角 `14*s`，按下是反白。

## 窗口 / Win32 接缝
- 菜单栏在非客户区，`--screenshot` 拍不到 → `.workbuddy/tools/menu_probe.py`。
- SDL 会吃掉 `WM_CLOSE`（直接销毁窗口）、`WM_SYSKEY*` / `WM_SYSCHAR` / `WM_MENU*` 和 `SC_KEYMENU`。
- 手写 COM vtable 按位置寻址，GUID 对着工具链头文件核。
- 命令共用挂起通道，每消费一条补 `EndMenu()`；「灰掉」只是 UI，处理端要再挡一道。

## 逆向工具链（2026-11-21 校正）
- so 路径：**`D:\Tools\rea\work\jp\libil2cpp_arm64.so`（199 MB）**。
  `jp\lib\arm64-v8a\libil2cpp.so` **不存在**（11-19 那条"校正"写反了，已撤回）；
  `jp\GameData\lib\arm64-v8a\libil2cpp.so` 是**同 sha256** 的另一份，也能用
  （`41E2D8FA…23F1D1`）。
- 反汇编：`python .workbuddy/tools/il2cpp_disasm.py <so> --va 0x… --count N`，
  依赖 capstone（已装 5.0.7）。类名提取用 `q.py`。
- prefab 几何：AssetRipper 必须 `run_in_background` 起并 `--headless --port 17654`，
  `POST /LoadFolder` 字段小写 `path`（302=受理）。工具全在 `D:\Tools\rea\work\jp\`：
  `arip.py`（HTTP 客户端）/ `arip_dump.py`（逐节点组件 YAML）/ `sprite_info.py`
  （sprite 九宫格 border）/ `so_xref.py`（adrp+ldr 交叉引用）。
  **`m_Children` 才是可信的父子关系**，`m_Father` 反查会把树走断。
- **浮点常量一律按位复核**：`mov w8,#-0x…` 别信 disasm 的注释，
  用 `struct.unpack('<f', struct.pack('<I', imm16<<hw))`。踩过 -72 被读成 -3.65。
- REA 已接进 DSH（`~/.dsh/profiles/desktop/cordis.patch.yml` 里 `mcp-rea`），
  但 Windows 的 Ghidra provider 只收 **PE**，arm64 ELF 走不了它 —— pjsk 的 so 仍用 capstone 那条路。

## 最近工作（细节见日志）
- 11-21 逆向原作设置界面 `OptionDialog`(prefab 1647)：窗口 1168 宽 / Large 1032 高 /
  圆角 16 / 阴影黑 20% / **开合 = DOScale 0↔1、0.125s、Linear**；产出 `OPTION-UI.md`。
  **并已落进 `game/Ui.cpp`**（圆角 14→16、动画 0.16→0.125 + 改线性、起始缩放 0）。
- 11-21 逆向多人结算 MVP：**谁拿 MVP 是服务器 `mvpFlg` 给的**，
  `Setup()` 只全关、`ShowEffect()` 才点亮；缎带骑在玩家卡顶边 +318、
  MVP 240×52 / SUPER STAR 278×52；爆闪 0.333s@60fps。产出 `MULTI-RESULT-MVP.md`。
- 11-19 `AGENTS.md` 拆成「主文件 + `design-notes/` 43 篇」（主文件 326KB → 48KB，不再被 64KB 预算截断）。
- 11-18 2D MV intro 硬切；产出 `UI-STRINGS.md`（547 条可显示字符串，翻译用）。
- 11-15 逆向 2D MV 播放链路、分数公式、多人演出 UI：产出 `MV2D-SCORE.md` 与 `MULTI-LIVE-UI.md`。
- 11-07 演奏中顶部进度条可拖动 seek（音频/判定/渲染三层同步）；产出 `HUD-UI-AUDIT.md`、`HAIKU-PORT.md`。
- 11-01 判定窗口按 master 表逐类型 + 左右不对称实现（`JUDGE-WINDOWS.md`）；
  预设表三档重算为 `72/113/138/155/155`、`42/83/108/125/125`、`12/53/78/95/95`。
- 10-29 删除标题画面；教学描边 8 步 ≤2.5px；点击特效收敛。
- 10-25 触摸特效 `game/TapEffect`（参数来自 `ScreenLayerTouchEffect`）。
- 10-18 新手教学落地（`game/Story*`）；10-17 多人收尾；10-06 原生菜单栏 + Win 融合。
