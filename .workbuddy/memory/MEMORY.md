# CppSekai — 项目长期记忆（索引版）

> **权威文档在仓库里，不在这个文件**：改代码前读 `AGENTS.md`（架构 + 全部坑 + 各功能一节），
> 体量/巨型函数看 `CODE-REVIEW.md`，命令行 `CLI.md`，谱面数据 `CHARTS.md`，能发什么 `COPYRIGHT.md`。
> 这里只放跨会话必须记住的**约定与索引**；历史细节翻 `.workbuddy/memory/2026-09-*.md`（append-only）。

## 协作约定
- **改完 + 验证过就 commit**（用户明确要求），别攒着。
- **禁止 `git checkout <file>` / `git restore` 撤临时改动**：2026-09-18 为撤一个调试探针 checkout
  了 main.cpp，把同文件一大轮未提交的改动全冲掉了。撤动用精确编辑，或先 commit。用户原话：
  「不要乱 checkout，有什么问题我们手动改」。
- 加新 .cpp 到 `game/` / `platform/` 必须同时加进 `build.sh` 的 `SOURCES`，否则链接期才报错。
- 中文注释的脚本（build.sh 等）用 Git Bash 跑；含中文的 PowerShell 脚本用 pwsh。
- **验证卡住几分钟就先停手**，把「需要人工点哪里、期望什么」交代清楚交给他，别硬磕自动化。

## 构建 / 运行硬性坑
- `bash build.sh`（Git Bash）。zig **0.14.1**（`toolchain/`，不入库），别换 0.16（吞 `-I`）；
  zig 缓存必须在 C 盘（build.sh 已设 `ZIG_GLOBAL_CACHE_DIR`，D 盘文件系统不支持）。
- `main.cpp` 必须在 `#include <SDL.h>` 前 `#define SDL_MAIN_HANDLED`，否则"秒退无输出"。
- exe 是 Windows 子系统；日志去 `cppsekai.log`（`--screenshot` 一定写文件）；
  `--screenshot` 的参数是**文件路径**（给目录会静默失败），父目录必须已存在。
  screenshot 是 RGBA PNG → **量 alpha 就能验透明**（Pillow 在 venv
  `~/.workbuddy/binaries/python/envs/default`）。
- **Win7 兼容补丁在 build.sh 顶部**（2026-09-19）：zig 自带 libc++ 的 chrono.cpp 在
  `_WIN32_WINNT>=0x0602` 时静态导入 `GetSystemTimePreciseAsFileTime`（Win8+），Win7 加载即报
  "无法定位程序输入点"。build.sh 幂等 sed 强制走运行时探测分支（`grep -c CPPSEKAI-WIN7 = 2`
  断言，打不上就 exit 1）。**toolchain 重新解压会自动重打，别删这段**。复查用
  `.workbuddy/tools/pe_imports.py`（看导入表；**别用 strings|grep**，函数名字面量还在）。
  Win7 还缺 UCRT（`api-ms-win-crt-*`），处置待用户拍板 —— 见 AGENTS.md「Windows 7 兼容」。

## 发布 / 打包
- `bash package.sh [版本]`（内部会先跑 build.sh）→ `dist/CppSekai-<日期>/` + zip。
  默认带 assets（解压即玩）；`--no-assets` 出精简包。charts / toolchain 一律不发。
  包内已排除 `assets/mmw/{overlay,effects,sound}`（运行时都不读）→ 目录 13MB / zip 6.8MB。
- 图标：`app.rc`（`zig rc` 编资源，id 1）+ 运行时 `SDL_SetWindowIcon(icon.png)`；
  改 id 要同步改 chartdl 的 `LoadImageW(MAKEINTRESOURCE(1))`。

## 资源 / 工具 / git
- `.gitignore` 忽略 `assets/`、`charts/`、`build/`、`toolchain/`、`userdata.json`、`profiles/`、
  `chartdl.json`。**新加的 `assets/` 子目录要 `git add -f`**（`assets/select/**` 就这样丢过）。
  `.workbuddy/` 不入 ignore（记忆 + 工具，工具是入库的）。
- **不要删 `.workbuddy/`**。工具：`winmd_*.py`（查 WinRT 槽位）、`pngcrop.py`、
  `shot_probe.py`（像素探针，带 Pillow 回退）、`asset_audit.py`、`shrink_assets.py`、
  `mp_verify.sh`（多人回归）、`pe_imports.py`（PE 导入表 → Win7 兼容检查）。
- `build/winsend.exe` / `winmsg.exe` 不在库里，要自己编（见 AGENTS.md 工具一节）。

## 素材现状（2026-09-19 大清理后 10MB / 248 文件，清理前 56.1MB）
- **字体只用系统字体**（`assets/mmw/font/` 已删，`--pjsk-font` 已去掉），别再往仓库放字体。
  候选表三层：SPI 讯息字体 → 固定 face 名（中英两套）→ **按文件名兜底**（msyh/meiryo/msgothic/
  simsun/simhei…，Win7 全靠这层，注册表值名随语言变）。**注册表值可能是完整路径**，别无脑拼
  `\Fonts\`。字形探测是日文+简中混合（初/ミ/詞/设），日文字体会因缺 `设` 被拒 —— 故意的。
  「字体变点阵 + 中文变问号」= 所有候选都没过 → 看 `cppsekai.log` 的 `[intro]` 几行
  （候选表 + 拒绝原因），`CPSEKAI_FONT_FILE=<路径>` 可强制指定。
- `assets/se/**` 是白名单，用户自己加的，哪怕没接线也不许删。
- **精灵图集不许缩**：`notes*` / `effect.png` / `longNoteLine*` / `touchLine*`
  —— 精灵矩形是像素坐标写死在 `core/native/generated/generated_resources.h`，缩文件 = 音符错位。

## 谱面目录 / 数据文件
- 谱面只认 **exe 同级的 `charts\`**（下载器默认输出）；游戏扫 `chartCandidates` 全部候选**合并**、
  按 .sus 文件名去重。下载器设置存 `<exe>\chartdl.json`。
- 多用户：`<dataDir>\profiles\<id>.json`（`{settings, scores, account}`）+ `index.json`；
  首次运行把 `userdata.json` **复制**成 `default`。`<dataDir>`：有 `<exe>\..\charts` 就用那一层。
- 优先级：命令行 > 档案 > 内置默认；`--screenshot` 不写成绩。

## UI 通则（所有画面都是「1920x1080 虚拟画布 + 缩放」）
- HUD / 结算 / 选曲都用 `px()/py()/ps()` 换算。**ImGui `AddText(font,size,...)` 的 size 是像素**，
  虚拟单位必须自己乘 scale（非 1080p 窗口文字偏大就是漏了这步）。
- 数值/文字优先用游戏自带精灵（`score/digit/*`、`combo/p*`、HUD overlay），别用字体凑。

## 验证手法
- **截图是能直接看的**（Read 一张 PNG 即可），选曲/房间这类界面改动直接抓图确认最快。
  但 `--party-auto` 会在结算 2s 后自动按「继续」，要拍结算就别加它。
- 无头自检：`--screenshot` + `--screenshot-time`，断言看日志 `[stats]`/`[score]`/`[result]` 行。
  **截图逐像素比的噪声基线只有 6 个像素**（同版本跑两次），所以画面回归可以靠 diff；
  `winmsg.exe <class> raw <hex> [wparam] [lparam]` 能伪造任意窗口消息（PostMessage 一样走 SDL
  的窗口过程 → 消息钩子能被无头验证）。`--no-party` 别忘：多人默认开着。
- **拖动窗口会让 Windows 跑模态循环把整个消息泵挂住**（画面冻结、音频照跑）。已用
  `SDL_SetWindowsMessageHook` 把拖动变成静默暂停（`[window] WM_ENTERSIZEMOVE ...` 日志）；
  要让画面继续渲染得把 main() 的帧体抽出来 —— 大改，用户还没拍板（见 AGENTS.md）。
- 日志"跑着跑着不打了"是缓冲假死（已改无条件 `setvbuf(_IONBF)`，读旧日志仍要记住）。
- 临时条件探针**别设计数上限**（`if (n < 8)` 会掩盖"条件没满足"和"分支没走"的区别）。
- 无交互会话下 `winsend.exe` 的 PostMessage 到不了某些 ImGui 界面 → 在代码里加自动按的
  调试开关（如 `--chartdl-test`），别在输入注入上死磕。
- **Git Bash 不等 GUI 子系统 exe**：`for ...; do ./cppsekai.exe ...; done` 会让几个实例几乎同时
  启动互抢 GPU，测出过 245fps 的假基线。串行要 `exe & sleep N`。**日志落在 cwd 的 `cppsekai.log`**
  （从仓库根跑就去根目录捞），不是 build/ 那份。
- **内存怎么查**：`.workbuddy/tools/mem_sample.py`（每 150ms 打 WorkingSetSize / **峰值** / 提交，
  tasklist 4 秒粒度看不出启动期台阶）。2026-09-19 实测：GL 空窗口基线 **72MB**，游戏稳态
  **245MB**（峰值 291MB）—— 大头是**常驻的 TTF 数据 ~37MB**（msyh 19.7 + msyhbd 16.9，新版
  ImGui 要整份字体常驻，换来 atlas 只有 0.25MB），贴图 13.5MB、壁纸 2.2MB。启动后 10~12s
  有一次 ~46MB 的**延迟归还**，别当成泄漏、也别当成"切设置释放了内存"（bgStyle 0/1 都有）。

## 最近工作（细节一律看 AGENTS.md 对应小节 + 当日日志）
- **2026-09-19**：多人开局倒计时删了（改成「最慢窗口加载 + 0.8s」两段 charge）；失血阴影几何
  重做（探针 `CPSEKAI_VIGNETTE`）；**Win7 三连修**：① libc++ chrono 静态导入
  `GetSystemTimePreciseAsFileTime`（启动即失败，build.sh 打补丁）② 系统字体候选表扩到三层 +
  按文件名兜底（点阵字/中文问号）③ 新增 `bgStyle = 2`「透明（Aero 玻璃）」背景
  （不填背景、保留漂浮三角形，Renderer + SongSelect + 窗口 DWM 四处联动）；素材压到 10MB。
  另：**渲染质量档评估过，用户拍板先不做** —— 实测瓶颈不在填充率（窗口 1920x1080 + autoplay，
  渲染 1080p 与 540p 都是 ~1.0ms/帧、950+fps），只对弱 GPU/VM 有意义。已知待办：拖动窗口时
  38~51fps（见「验证手法」）。
- **2026-09-18**：空谱面「下载谱面」按钮、首启 ELUA 弹窗、**多人"连不上"真根因在选曲界面**
  （`selected` 被 `diffIndex` 反推成 -1）、暂停没冻住主机时钟、多人流程重做、多人不进结算。
- **2026-09-17**：多人游玩（`platform/Party.*` + `game/PartyScreen.*`，同机多窗口共享内存总线）。
- **2026-09-16**：多用户 `profiles/`、渲染模式（固定分辨率 FBO）、谱面目录 `./charts`、下载器大改。
- 更早（9-13~9-15）：结算画面、设置 4 页、选曲分组/多歌手、长条尾判、舞台背景、HUD 徽章、
  贴图尺寸策略与 UTF-8 路径规则。
