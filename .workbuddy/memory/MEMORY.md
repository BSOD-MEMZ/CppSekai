# CppSekai — 项目长期记忆（索引版）

> **权威文档在仓库里，不在这个文件**：`AGENTS.md`（架构 + 全部坑 + 各功能一节）、
> `CODE-REVIEW.md`（体量）、`CLI.md`（命令行）、`CHARTS.md`（谱面）、`COPYRIGHT.md`（能发什么）。
> 这里只放跨会话必须记住的**约定与索引**；细节翻 `.workbuddy/memory/2026-09-*.md`（append-only）。

## 协作约定
- **改完 + 验证过就 commit**（用户明确要求），别攒着。
- **禁止 `git checkout <file>` / `git restore` 撤临时改动**（09-18 把 main.cpp 一整轮未提交改动冲掉过）。
  撤动用精确编辑或先 commit。用户原话：「不要乱 checkout，有什么问题我们手动改」。
- 加新 .cpp 到 `game/` / `platform/` 必须同时加进 `build.sh` 的 `SOURCES`。
- 中文注释的脚本（build.sh 等）用 Git Bash；含中文的 PowerShell 脚本用 pwsh。
- **验证卡住几分钟就先停手**，把「需要人工点哪里、期望什么」交代清楚交给他。

## 构建 / 运行硬性坑
- `bash build.sh`（Git Bash）。zig **0.14.1**（`toolchain/` 不入库），别换 0.16（吞 `-I`）；
  zig 缓存必须在 C 盘。全量编译 35~45s。游戏链接行有 `-lcomdlg32`（账户页导入/导出）。
- `main.cpp` 必须在 `#include <SDL.h>` 前 `#define SDL_MAIN_HANDLED`，否则"秒退无输出"。
- exe 是 Windows 子系统；日志去 **cwd 的 `cppsekai.log`**；`--screenshot` 参数是**文件路径**
  （给目录静默失败），父目录必须已存在。
- **无头跑看日志别重定向 stdout**：`( exe >/dev/null 2>&1 & )` 会让 `cppsekai.log` 不生成。
  要 `( exe & )` / `( exe & wait )`，或加 `--screenshot` 让它自己退。
- **Win7 补丁在 build.sh 顶部**（09-19）：强制 libc++ chrono 走运行时探测，否则 Win7 报
  "无法定位程序输入点"。**toolchain 重解压会自动重打，别删**（`grep -c CPPSEKAI-WIN7 = 2` 断言）。
  复查用 `.workbuddy/tools/pe_imports.py`（别用 strings|grep）。Win7 还缺 UCRT，待拍板。
- **警告开关 09-21 起开着，保持 0 警告**（`-Wall -Wextra`）。做法：上游/第三方单独编 `.o` + `-w`
  （`UPSTREAM_SOURCES` 段），新上游 .cpp 要手工加进去。**判断有无警告只能跑完整 build.sh
  同时数 `error:` 和 `warning:`**（逐文件 `-fsyntax-only` 会把"提前中止"读成"0 警告"）。
- ⚠ **`third_party/DirectXMath/Inc` 必须留在 `-I`，不能改 `-isystem`**：toolchain 里有个 MinGW
  小写 `directxmath.h` 桩头会顶掉真头。排错用 `zig c++ -E -v`。

## 发布 / 打包
- `bash package.sh [版本]`（内部先跑 build.sh）→ `dist/CppSekai-<日期>/` + zip。默认带 assets，
  `--no-assets` 出精简包；charts / toolchain 一律不发。
- 图标：`app.rc`（`zig rc`，id 1）+ `SDL_SetWindowIcon(icon.png)`；改 id 要同步改 chartdl 的
  `LoadImageW(MAKEINTRESOURCE(1))`。
- **发二进制要带 SDL2 的 zlib 许可文本**（SDL2.dll 随包发）。

## 资源 / git / 工具
- `.gitignore` 忽略 `assets/`、`charts/`、`build/`、`toolchain/`、`userdata.json`、`profiles/`、
  `chartdl.json`。**新加的 `assets/` 子目录要 `git add -f`**。`.workbuddy/` 入库存工具。
- 工具在 `.workbuddy/tools/`：`png_color_probe.js`（无 Pillow 时的 PNG 颜色探针）、`shot_probe.py`、
  `pngcrop.py`、`pe_imports.py`、`mem_sample.py`、`update_music_db.py`、`fetch_music_aliases.py`、
  `mp_verify.sh`、`asset_audit.py`、`shrink_assets.py`、`chartdl_detail_check.py`。
- **精灵图集不许缩**：`notes*` / `effect.png` / `longNoteLine*` / `touchLine*`（矩形像素坐标写死在
  `core/native/generated/generated_resources.h`）。`assets/se/**` 是白名单，不许删。
- 字体只用系统字体（`assets/mmw/font` 已删，`--pjsk-font` 已去掉）；`CPSEKAI_FONT_FILE=<路径>` 可强制指定。

## 数据表 / 谱面目录
- 仓库根四个**可选**表：`musics.json`、`music-vocals.json`、`music-levels.json`（定数）、
  `music-aliases.json`（社区别名）。缺一个只少一块功能。同步用 `update_music_db.py`（`--check` 只报告）；
  **表是本地快照会过时，日服出新曲就跑它**。
- 曲库两源按 id 分派：日服（id ≤ 804）走 `assets.unipjsk.com`；**国服独占曲（11000+）走
  `storage.sekai.best/sekai-cn-assets`**，布局三处不同。**别往 10000 以上放新 id**。
- 谱面只认 **exe 同级的 `charts\`**；多用户 `<dataDir>\profiles\<id>.json` + `index.json`，
  首次运行把 `userdata.json` 复制成 `default`。**加设置字段要同时改 `SongSelect.cpp` 的
  `loadUserData`（`s.value(...)`）与 `saveUserData`（`doc["settings"]`）**，漏一处就是静默丢设置。

## UI 通则（1920x1080 虚拟画布 + 缩放）
- HUD / 结算 / 选曲都走 `px()/py()/ps()`。**ImGui `AddText(font,size,...)` 的 size 是像素**，
  虚拟单位必须自己乘 scale。数值/文字优先用游戏自带精灵（`score/digit/*`、`combo/p*`）。
- 自绘控件（`ui::slider` / `checkBox` / `radioRow` / `stepper` / `capsuleButton`）**键盘焦点够不着** ——
  手柄靠焦点环（`ui::PadScope` + `padNav`）；**新增原生 `ImGui::Combo` 要接一句 `ui::padComboNudge`**。
- 设置卡片 **380x800**（设计像素，`ui::scale()` = 视高/860 封顶 2.0）；页签内容放裁剪 child 里，
  **余量很小**（演奏 515 / 账户 488）。加行先跑 `CPSEKAI_UI_TRACE=1` 看 `used`。

## 验证手法（精选）
- **截图能直接看**（Read 一张 PNG）。**先看 PNG 尺寸**：窗口多大截图就多大，别按 1920x1080 算裁剪框。
  `--party-auto` 会在结算 2s 后自动按「继续」，拍结算别加它。开场卡片要 `--intro-preview`。
- **别只读 `lastHitKind` / `lastJudge*`**（一帧判多个音时只剩最后一个）——统计用只增不减的
  `JudgementStats::hitCount` / `criticalHitCount` / `flickHitCount` 比增量。
- 无头自检：`--screenshot` + `--screenshot-time`，断言看 `[stats]`/`[score]`/`[result]`；
  设置卡片 `--settings --settings-tab N`。`--no-party` 别忘（多人默认开着）。
- **日志按 cwd 落盘、进程启动时截断** → **一个测试用一个独立目录**。
  **Git Bash 不等 GUI exe**，要 `( exe & wait )` 或 `exe & sleep N`。
- 无交互会话下 PostMessage 到不了多数 ImGui 界面（实测 `winmsg/winsend click` 点选曲 combo /
  设置齿轮 / 刷新全无反应，只有 SDL 层自绘热区能到）→ 在代码里加自动按的调试开关
  （`--fake-pad`、`--chartdl-test`），或临时加 env 探针 `OpenPopup`，**只开一次**，验完删干净。
- **残留实例会同时骗你两次**：`tasklist | grep cppsekai` 才看得到（`Get-Process` 看不到），
  既占着 exe 让链接报 Permission denied，又用单实例 mutex 挡掉无头跑。清理：
  `MSYS_NO_PATHCONV=1 taskkill /PID <pid> /F`。
- 拖动窗口会让 Windows 跑模态循环挂住消息泵（画面冻结、音频照跑）；已用 `SDL_SetWindowsMessageHook`
  变成静默暂停。临时条件探针**别设计数上限**。
- 内存用 `mem_sample.py`：GL 空窗口 72MB、游戏稳态 212MB；启动后 10~12s 有一次 ~46MB 延迟归还，
  别当泄漏。chartdl 设置窗口用 `GetWindow(hwnd, GW_OWNER)` 判关闭（不是 `GetParent()`）。

## 最近工作（细节看 AGENTS.md 对应小节 + 当日日志）
- **2026-09-24**：设置卡片一批（账户页导入/导出、系统页结束实例、判定预设 宽松/标准/严格
  —— 出厂**宽松** = 官方各 +30ms，新增 `ui::radioRow`；震动三勾；页签触摸拖动滚动）；
  第二批：窗口标题跟随用户、开多人弹「立即重启？」（重启前 CloseHandle 两个命名互斥体）、
  **自动演出给经验但不写谱面成绩**、README 581→203 行（**技术细节禁止再往 README 加**）。
- **2026-09-20**：chartdl 数据源体检 + 定数表纳入同步；确定闪光改全白；猜歌卡片；
  **手柄焦点环** + 开局与结算数字滚动的震动（`UserSettings::padRumble`）。
- **2026-09-19**：多人开局倒计时删掉；失血阴影重做；Win7 三连修；素材压到 10MB；
  内存审计 + 结算界面改版。更早：多人（9-17）、profiles 与下载器（9-16）、结算/设置 4 页/
  长条尾判/ELUA（9-13~9-15）。
