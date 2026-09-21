# CppSekai — 项目长期记忆（索引版）

> **权威文档在仓库里，不在这个文件**：改代码前读 `AGENTS.md`（架构 + 全部坑 + 各功能一节），
> 体量看 `CODE-REVIEW.md`，命令行 `CLI.md`，谱面 `CHARTS.md`，能发什么 `COPYRIGHT.md`。
> 这里只放跨会话必须记住的**约定与索引**；细节翻 `.workbuddy/memory/2026-09-*.md`（append-only）。

## 协作约定
- **改完 + 验证过就 commit**（用户明确要求），别攒着。
- **禁止 `git checkout <file>` / `git restore` 撤临时改动**：2026-09-18 为撤一个调试探针 checkout
  了 main.cpp，把同文件一大轮未提交的改动全冲掉。撤动用精确编辑或先 commit。用户原话：
  「不要乱 checkout，有什么问题我们手动改」。
- 加新 .cpp 到 `game/` / `platform/` 必须同时加进 `build.sh` 的 `SOURCES`（否则链接期才报错）。
- 中文注释的脚本（build.sh 等）用 Git Bash；含中文的 PowerShell 脚本用 pwsh。
- **验证卡住几分钟就先停手**，把「需要人工点哪里、期望什么」交代清楚交给他。

## 构建 / 运行硬性坑
- `bash build.sh`（Git Bash）。zig **0.14.1**（`toolchain/` 不入库），别换 0.16（吞 `-I`）；
  zig 缓存必须在 C 盘（build.sh 已设 `ZIG_GLOBAL_CACHE_DIR`）。全量编译 35~45s。
- `main.cpp` 必须在 `#include <SDL.h>` 前 `#define SDL_MAIN_HANDLED`，否则"秒退无输出"。
- exe 是 Windows 子系统；日志去 **cwd 的 `cppsekai.log`**；`--screenshot` 的参数是**文件路径**
  （给目录会静默失败），父目录必须已存在。
- **无头跑看日志别重定向 stdout**：`( exe >/dev/null 2>&1 & )` 会让它继续写 stdout，于是
  `cppsekai.log` 根本不生成（main.cpp 开头那段按"stdout 是否已重定向"决定）。要 `( exe & )`
  或 `( exe & wait )`，或加 `--screenshot` 让它自己退。
- **Win7 补丁在 build.sh 顶部**（2026-09-19）：zig 的 libc++ chrono.cpp 在 `_WIN32_WINNT>=0x0602`
  静态导入 `GetSystemTimePreciseAsFileTime`（Win8+），Win7 启动即报"无法定位程序输入点"。
  build.sh 用幂等 sed 强制走运行时探测（`grep -c CPPSEKAI-WIN7 = 2` 断言）。**toolchain 重解压
  会自动重打，别删**。复查用 `.workbuddy/tools/pe_imports.py`（**别用 strings|grep**，字面量还在）。
  Win7 还缺 UCRT，处置待拍板 —— 见 AGENTS.md「Windows 7 兼容」。

## 发布 / 打包
- `bash package.sh [版本]`（内部先跑 build.sh）→ `dist/CppSekai-<日期>/` + zip。默认带 assets，
  `--no-assets` 出精简包；charts / toolchain 一律不发 → 目录 13MB / zip 6.8MB。
- 图标：`app.rc`（`zig rc`，id 1）+ `SDL_SetWindowIcon(icon.png)`；改 id 要同步改 chartdl 的
  `LoadImageW(MAKEINTRESOURCE(1))`。
- **`json.hpp` 在仓库里有两份、md5 相同**（`third_party/nlohmann/` 与 `core/native/vendor/nlohmann/`，
  都是 nlohmann 3.12.0）。**看到重复别直接删**：`mmw_preview.cpp:28` 用相对路径
  `"../vendor/nlohmann/json.hpp"` 钉住 vendor 那份（上游代码，AGENTS.md 说不改结构）。
  真风险是**跨 TU 的静默 ODR**（只升一份就 UB 且不报错）→ 正解是把 vendor 那份换成一行转发头。
- `build.sh:16` 的 `CXXFLAGS` **没有任何警告开关**。实测 `-Wall -Wextra -fsyntax-only`
  15 个文件 0 警告（含 7,489 行的 main.cpp），只有 chartdl 23 个 / SongSelect 4 个 →
  加 `-Wall -Wextra` 成本几乎为零。

## 资源 / 工具 / git
- `.gitignore` 忽略 `assets/`、`charts/`、`build/`、`toolchain/`、`userdata.json`、`profiles/`、
  `chartdl.json`。**新加的 `assets/` 子目录要 `git add -f`**。`.workbuddy/` 不入 ignore（工具入库）。
- **不要删 `.workbuddy/`**。工具在 `.workbuddy/tools/`：`png_color_probe.js`（纯 Node 的 PNG
  颜色探针，支持裁剪框，**Pillow 装不上时的兜底**）、`shot_probe.py`、`pngcrop.py`、`pe_imports.py`、
  `mem_sample.py`、`update_music_db.py`、`fetch_music_aliases.py`、`mp_verify.sh`、`asset_audit.py`、
  `shrink_assets.py`、`chartdl_detail_check.py`、`winmd_*.py`。
- `build/winsend.exe` / `winmsg.exe` 不在库里，要自己编（见 AGENTS.md）。

## 素材现状（2026-09-19 清理后 10MB / 248 文件）
- **字体只用系统字体**（`assets/mmw/font/` 已删，`--pjsk-font` 已去掉）。候选表三层：SPI 讯息字体
  → 固定 face 名 → **按文件名兜底**（msyh/meiryo/msgothic/simsun…，Win7 全靠这层）。字形探测是
  日文+简中混合（初/ミ/詞/设），日文字体缺 `设` 会被拒 —— 故意的。「字体变点阵 + 中文变问号」=
  候选全没过 → 看 `cppsekai.log` 的 `[intro]` 几行，`CPSEKAI_FONT_FILE=<路径>` 可强制指定。
  粗体 face 已删（`boldFont()` 返回 bodyFont）；结算 RESULT 大字走 `platform/FontOutline.cpp`
  的真空心轮廓纹理，不走字体。
- `assets/se/**` 是白名单，用户自己加的，不许删。
- **精灵图集不许缩**：`notes*` / `effect.png` / `longNoteLine*` / `touchLine*`（矩形是像素坐标写死
  在 `core/native/generated/generated_resources.h`，缩文件 = 音符错位）。

## 数据表 / 谱面目录
- 仓库根四个**可选**表（`main.cpp` 按 baseDir → .. → cwd 三级候选找，`package.sh` 拷进包）：
  `musics.json`、`music-vocals.json`、`music-levels.json`（定数）、`music-aliases.json`（社区别名）。
  缺一个只少一块功能、不会崩；别名表丢了是静默降级（启动打 `[aliases] N aliases`）。
- 曲库两源按 id 分派：日服曲（id ≤ 804）走 `assets.unipjsk.com`；**国服独占曲（id 11001+）走
  `storage.sekai.best/sekai-cn-assets`**，布局三处不同（谱面带 `.txt`、音频前缀 `vs_/se_/an_`、
  曲绘 5 位 `jacket_s_11xxx`）。判据只有 `id >= 10000` —— **别往 10000 以上放新 id**。
  总同步用 `update_music_db.py`（含定数表；`--check` 只报告；每个仓库配 raw.github + jsDelivr
  两条地址，国内直连 raw 会整段不通）。**表是本地快照会过时，日服出新曲就跑它**。
- 谱面只认 **exe 同级的 `charts\`**；游戏扫 `chartCandidates` 全部候选**合并**、按 .sus 文件名去重。
- 多用户：`<dataDir>\profiles\<id>.json` + `index.json`；首次运行把 `userdata.json` **复制**成
  `default`。优先级：命令行 > 档案 > 内置默认；`--screenshot` 不写成绩。**加设置字段要同时改
  `game/SongSelect.cpp` 里 `loadUserData` 的 `s.value(...)` 与 `saveUserData` 的 `doc["settings"]`**
  （漏一处就是静默丢设置）。

## UI 通则（1920x1080 虚拟画布 + 缩放）
- HUD / 结算 / 选曲都用 `px()/py()/ps()` 换算。**ImGui `AddText(font,size,...)` 的 size 是像素**，
  虚拟单位必须自己乘 scale（非 1080p 文字偏大就是漏了这步）。
- 数值/文字优先用游戏自带精灵（`score/digit/*`、`combo/p*`），别用字体凑。
- 自绘控件（`ui::slider` / `checkBox` / `stepper` / `capsuleButton`）**键盘焦点够不着** —— 手柄靠
  焦点环驱动（`ui::PadScope` + `padNav`，见 AGENTS.md「手柄焦点环 / 震动」）；**新增原生
  `ImGui::Combo` 要接一句 `ui::padComboNudge`**。

## 验证手法（精选）
- **截图能直接看**（Read 一张 PNG）。`--party-auto` 会在结算 2s 后自动按「继续」，拍结算别加它。
  **自己拿到的 PNG 先看尺寸**：窗口多大截图就多大（1280x720 常见），别按 1920x1080 算裁剪框。
  开场卡片只活在谱面时间 0 之前，要 `--intro-preview [sec]` 才截得到。
- **"这一帧发生了什么判定"别只读 `lastHitKind` / `lastJudge*`**：一帧里判多个音时它们只剩最后一个
  （和弦、自动演奏一次跨好几个音）。要统计就用只增不减的计数（`JudgementStats::hitCount` /
  `criticalHitCount` / `flickHitCount`），在 `main.cpp` 里比增量。2026-09-21 用手柄震动实测踩到。
- 无头自检：`--screenshot` + `--screenshot-time`，断言看 `[stats]`/`[score]`/`[result]` 行；
  设置卡片用 `--settings --settings-tab N`。截图逐像素比的噪声基线只有 6 px，画面回归能靠 diff。
  `--no-party` 别忘（多人默认开着）。
- **日志按 cwd 落盘、进程启动时截断**：并发跑只能看到最后一个 —— **一个测试用一个独立目录**。
  **Git Bash 不等 GUI exe**，要 `( exe & wait )` 或 `exe & sleep N`（`for` 里直接跑会互抢 GPU，
  测过 245fps 假基线）。
- 无交互会话下 PostMessage 到不了某些 ImGui 界面 → 在代码里加自动按的调试开关（`--fake-pad`、
  `--chartdl-test`），别在输入注入上死磕。**2026-09-21 实测确认**：`winmsg click` / `winsend click`
  （后者已激活窗口 + SetCursorPos）点选曲界面的 combo / 设置齿轮 / 刷新**全都没反应**，能到的只有
  SDL 事件层的自绘热区（开场跳过键、HUD 暂停键）。要看「弹出来的东西长什么样」，临时加个 env
  探针在代码里 `OpenPopup`，**只开一次**（每帧开会让弹层永久 hidden），验完删干净。
- **残留实例会同时骗你两次**（2026-09-21）：`Get-Process cppsekai` 看不到它（`tasklist | grep`
  才看得到），一边占着 exe 让链接报 `failed to write output ... Permission denied`，一边用单实例
  mutex 把下一次无头跑挡掉（日志只有 `[instance] already running`）。清理：
  `MSYS_NO_PATHCONV=1 taskkill /PID <pid> /F`。
- 拖动窗口会让 Windows 跑模态循环把消息泵挂住（画面冻结、音频照跑）；已用
  `SDL_SetWindowsMessageHook` 变成静默暂停。
- 临时条件探针**别设计数上限**（`if (n < 8)` 会掩盖"条件没满足"和"分支没走"的区别）。
- 内存用 `mem_sample.py`：GL 空窗口 72MB、游戏稳态 212MB（删粗体后）；启动后 10~12s 有一次
  ~46MB 的延迟归还，别当泄漏。
- chartdl：设置窗口是**顶层**窗口，关闭判定用 `GetWindow(hwnd, GW_OWNER)` 而非 `GetParent()`；
  下载 4 线程 + `thread_local` 缓存 session；**测下载别拿单曲做样本**。

## 最近工作（细节看 AGENTS.md 对应小节 + 当日日志）
- **2026-09-20**：① chartdl 数据源体检 + 定数表纳入同步；② 确定闪光改全白；③ 猜歌卡片；
  ④ **手柄焦点环**（设置卡片里的滑块/复选框/stepper/下拉框都能用手柄操作）**+ 开局与结算数字
  滚动的震动**（新设置 `UserSettings::padRumble`）。
- **2026-09-19**：多人开局倒计时删掉；失血阴影重做；Win7 三连修（chrono / 字体候选表 /
  `bgStyle=2` Aero 玻璃）；素材压到 10MB；内存审计 + 结算界面改版。渲染质量档评估过、用户拍板先不做。
- 更早：多人游玩（9-17）、多用户 profiles 与下载器大改（9-16）、结算画面 / 设置 4 页 / 长条尾判 /
  ELUA 弹窗 / 选曲分组（9-13 ~ 9-15）。
