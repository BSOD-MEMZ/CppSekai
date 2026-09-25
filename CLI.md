# CppSekai 命令行手册

> ### ⚠️ 免责声明
>
> CppSekai 是**非官方、非营利的爱好者作品**，与 SEGA、Colorful Palette 及
> 「プロジェクトセカイ カラフルステージ！ feat. 初音ミク」（Project SEKAI）官方
> **没有任何隶属、赞助、授权或认可关系**，也**未获其许可**。
> 本文里的 `chartdl` 只是**替你从第三方镜像站取文件**的工具，它不托管、不背书、
> 也不授予任何权利。取到的素材**权利全部归原权利人**，**仅限本机个人游玩与学习**，
> **禁止二次分发、公开发布与任何商业使用**。按「现状」提供，使用后果由使用者自负。
> 详见 [COPYRIGHT.md](COPYRIGHT.md)。

讲清楚**怎么从命令行把游戏跑起来**：参数、返回值、日志去哪、无头自检怎么用。
选曲界面的交互、设置面板的各项说明在 [README.md](README.md)，谱面怎么下在 [CHARTS.md](CHARTS.md)。

---

## 一、前提

```
build/cppsekai.exe      ← 程序本体（Windows 子系统，双击不会弹 cmd 窗口）
build/SDL2.dll          ← 运行时依赖
build/assets/           ← 贴图 / 音效 / 字体（按 exe 所在目录找，不看 CWD）
charts/                 ← 谱面（自己下，见 CHARTS.md）
music-levels.json       ← 难度定数表（缺失时选曲界面的等级显示 "-"）
```

编译：`bash build.sh`（Git Bash）。所有资源路径都是**相对 exe 所在目录**解析的，
所以在哪个目录敲命令都一样，`cd build && ./cppsekai.exe` 和 `build/cppsekai.exe` 等价。

---

## 二、两个入口

```bash
# 1) 选曲界面：不给 --sus
./build/cppsekai.exe

# 2) 直接打某张谱：给 --sus
./build/cppsekai.exe --sus "charts/0628_master.sus" --bgm "charts/0628.mp3" --auto
```

不给 `--sus` 时，程序按下面的顺序找谱面目录，取**第一个真的有 .sus 的**：

1. `--charts <dir>`（给了就只用它）
2. `<exe>/charts`
3. `<exe>/../charts`（build/ 布局时的仓库根，平时就是这个）
4. `./charts`

一个都没有就进选曲界面并提示「没有找到谱面」，日志里会列出找过的目录。
F5 可以在不重启的情况下重新扫描（新丢进去的 `.sus` 立刻出现）。

---

## 三、日志去哪了

exe 是 **Windows 子系统**（不是控制台程序），所以：

| 启动方式 | 输出位置 |
|---|---|
| 双击 / 资源管理器 | 不开 cmd 窗口，日志写 `cppsekai.log`（工作目录下，`.gitignore` 里） |
| cmd / PowerShell 里运行 | 附着到那个控制台，日志直接打在终端上 |
| `> out.txt` / `\| more` 等重定向 | 尊重重定向，写到你给的地方 |
| `--screenshot` | 一定写 `cppsekai.log`（无头自检用，方便事后翻） |

也就是说：**从 cmd 敲 `cppsekai.exe --help > help.txt` 永远有东西**，双击则去看 `cppsekai.log`。
`--screenshot` 模式打开时会把 stdout/stderr 强制重定向到 `cppsekai.log`。

---

## 四、参数总表

```
cppsekai [--sus <file.sus>] [--bgm <audio>] [--charts <dir>] [--cover <image>]
         [--offset <sec>] [--filler <sec>] [--auto] [--speed <1-12>]
         [--se-volume <0-1>] [--lead-in <sec>]
         [--title <text>] [--lyricist <text>] [--composer <text>]
         [--arranger <text>] [--vocal <text>] [--difficulty <text>]
         [--width <px>] [--height <px>] [--window borderless|windowed|fullscreen]
         [--fps <n>] [--screenshot <png>] [--screenshot-time <sec>]
         [--judge-sheet] [--judge-frame <n>] [--test-hits] [--flick-as-tap]
         [--show-pause-dialog] [--settings] [--settings-tab <0-5>]
         [--select-id <musicId>] [--select-vocal <n>] [--dump-events <n>]
         [--test-restart] [--restart-at <sec>]
         [--result-preview] [--result-at <sec>] [--confirm-flash [<sec>]]
         [--profile] [--guess] [--singer-panel] [--player <昵称[:组织]>]
         [--player-rank <n>] [--help]
```

### 4.1 内容 / 对齐

| 参数 | 说明 |
|---|---|
| `--sus <file.sus>` | 指定谱面。给了就直接进演奏，不给就进选曲界面。**脚本 / 无头运行时给绝对路径最稳**：Git Bash 的 MSYS 会改写以 `..` 开头的相对参数，`../charts/x.sus` 会被改到别的地方去 |
| `--bgm <audio>` | BGM 文件。不给时用谱面旁边的 sidecar（同目录同名音频） |
| `--charts <dir>` | 谱面目录，同时决定 F5 重扫的目录 |
| `--cover <image>` | 曲绘，不给时用 sidecar / 同目录的 jacket |
| `--filler <sec>` | BGM 开头静音秒数（谱面 tick 0 在静音之后）。不给则自动扫静音，再不行按 0 |
| `--offset <sec>` | 手感微调，正数 = 音乐更晚出。和 `--filler` 是两回事：`filler` 是文件本身的头部留白，`offset` 是你个人的延迟 |
| `--title` `--lyricist` `--composer` `--arranger` `--vocal` `--difficulty` | 开场卡片的文字（SUS 里没有这些字段）。UTF-8 中文/日文都没问题 |

优先级：**命令行 > `userdata.json` > 内置默认值**。
`--offset` 这种会写回 `userdata.json` 的项，给了命令行就按命令行的算。

### 4.2 玩法

| 参数 | 说明 |
|---|---|
| `--auto` | 自动演示：全 PERFECT 跑一遍、不接输入、**不写成绩**。**只对本次运行生效**，不会改存档里的 AUTOPLAY 开关 |
| `--speed <1-12>` | 音符速度，等同设置面板里的滑块 |
| `--se-volume <0-1>` | 打击音音量 |

### 4.3 窗口 / 性能

| 参数 | 说明 |
|---|---|
| `--width <px>` `--height <px>` | 窗口尺寸，默认 1366×768 |
| `--window borderless\|windowed\|fullscreen` | 默认 borderless（无边框铺满）；`fullscreen` 用桌面分辨率 |
| `--fps <n>` | 在垂直同步之外再加帧率上限，`0` = 只靠垂直同步。超过显示器刷新率时会自动关掉垂直同步 |

### 4.4 无头自检 / 调试

| 参数 | 说明 |
|---|---|
| `--screenshot <png>` | 无头跑一帧存 PNG 然后退出。选曲界面默认在启动后 1.2s 抓，演奏中在 `--screenshot-time` 抓。**参数是文件路径**（`shots/a.png`），给一个目录会静默写失败；父目录必须已存在 |
| `--screenshot-time <sec>` | 演奏模式抓图的时间点（谱面时间，默认 4.0）。**选曲界面也给这个参数时**按它抓（最少 0.5s），用来等滚动/动画停稳 |
| `--judge-sheet` | 把 6 张判定文字贴图并排画在屏幕下方（对判定文字动画/UV 用） |
| `--judge-frame <n>` | 把判定文字**冻结**在第 n 帧（60fps 计），用来逐帧核对动画 |
| `--test-hits` | 不按键，按时间轴把音符逐个喂给判定引擎（查特效链路） |
| `--show-pause-dialog` | 演奏 0.5s 后强制打开暂停弹窗（截弹窗用的） |
| `--intro-preview [sec]` | 把开头卡片的输出时钟钉在 `sec` 秒（默认 1.5 = 卡片全亮）不往前走，用来截开头卡片 / 右上角跳过键。**必要**：`outputTime = songTime + leadIn` 而 `leadIn` 至少 5.8s，卡片只活在谱面时间 0 之前，而 `--screenshot` 最早只能在谱面 0.5s 抓图 |
| `--settings` `--settings-tab <0-5>` | 启动即打开设置卡片，并指定分页（0 演奏 / 1 画面 / 2 判定 / 3 系统 / 4 账户 / 5 关于），配合 `--screenshot` 截设置面板——按键没法在无头运行里送进去 |
| `--select-id <musicId>` | 启动就停在选曲列表里这首歌上（截图 / 下载器交接用），首帧定位不会被列表初始布局覆盖 |
| `--ui-scale <n>` | 选曲 / 结算画面的界面缩放（`1.0` = 正好填满窗口，可用范围 0.7~1.5）。**只影响这两个画面**，演奏界面和 HUD 不受影响；设置卡片的「画面」页有同一个滑杆，值存进 `userdata.json` |
| `--dump-stage-bg <png>` | 把当前歌曲生成的舞台底板写成 PNG（排查舞台背景合成用），2048x2048（上游 renderToSquareBackground 的方形结果） |
| `--select-vocal <n>` | 预选第 n 个演唱版本（`availableVocals()` 的下标），用来截"切了版本"的界面 / 验证预览音频跟着走 |
| `--dump-events <n>` | 谱面加载后打印前 n 个 packed HitEvent（time / center / width / kind / flags / endTime / volume）。谱面"看起来不对"时先看这个：分出是解析的问题还是渲染的问题 |
| `--test-vocal-switch <sec>` | 选曲界面到点自动切到下一个演唱版本（回归测"切版本预览接着播"） |
| `--test-restart` `--restart-at <sec>` | 走到指定秒数执行「放弃 → 换一首」——回归测「打到一半重选曲卡死」那个 bug |
| `--result-at <sec>` | 谱面走到指定秒数就切到**结算画面**（用真实判定数据），不用等整首歌放完 |
| `--result-preview` | 启动即进结算画面，且用参考截图的样例数字（940021 / PERFECT 634 …），专门用来跟原版截图做像素对比 |
| `--flick-as-tap` | 本次运行把 **flick 音符当 tap 判**（任意手势都能清），给上滑很难触发的触摸屏用；等同设置里「判定 > Flick 视作 Tap」，但**不写档案** |
| `--flick-log` | 开触摸 flick 调试日志（等同设置里「判定 > Flick 调试日志」，但不写档案）。写 `flick_debug.log`：每个触摸采样（坐标/位移/dt/vel/travel/分类出的方向）+ 每次 flick 判定的结果，**没打中时还会列出附近 flick 音符的 dt / 轨道偏差 / 需要的方向**——触摸 flick 老 MISS 就靠它定位 |
| `--confirm-flash [<sec>]` | 在选曲界面单独放一次「确定」的白色爆发光效（默认 1.0s 处，**不加载歌曲**），配合 `--screenshot` 抓爆发过程 |
| `--profile` | 启动即打开选曲界面的**个人资料卡**（平时要点右上角的等级牌才出来），配合 `--screenshot` 截它 |
| `--guess` | 启动即打开**猜歌**卡片（平时要点选曲界面头排的「猜歌」按钮），配合 `--screenshot` 截它 |
| `--singer-panel` | 启动即打开**切换歌手**面板（平时要点手机面板底部那颗麦克风按钮），配合 `--select-id <音乐 id>` + `--screenshot` 截它 |
| `--player <昵称[:组织]>` | 无头检查用：把账户的昵称 / 学校塞进内存（**不读也不写 userdata.json**），让资料卡和设置「账户」页有东西可看 |
| `--player-rank <n>` | 同上，直接把等级设成 n（本级经验清零），用来对比不同等级下的等级牌 |
| `--player-exp <0..1>` | 同上，把本级经验设成「升到下一级所需经验的这个比例」——等级牌左端那截绿色进度条就是它，`0.35` = 35% |
| `--party` | 本次运行加入**多人游玩**房间（同机多窗口一起打，第一个窗口是房主，BGM 只房主放；等价于设置里的「多人游玩」，但不写档案，并且会顺带允许多开） |
| `--party-name <name>` | 房间里显示的名字（默认取账户昵称），同时进窗口标题：`CppSekai - <name>`，多个窗口才分得清 / 脚本才点得准 |
| `--party-auto [<难度 0-6>]` | 无头跑一整轮多人：房主自动在当前这首按「确定」，每个窗口自动选该难度并准备。配合 `CPSEKAI_MP_TRACE=1`（每秒一行 `[sync] qpc/t/offset` + 队友分数）就能断言时钟同步。回归脚本见 `.workbuddy/tools/mp_verify.sh` |
| `--help` / `-h` | 打印用法并退出 |

---

## 五、无头自检怎么用

核心思路：**不开窗口也要能看到画面**，改渲染/UI 不用靠肉眼。

```bash
# 选曲界面截图（1.2s 自动抓）——会得到当前列表 + 手机面板
./build/cppsekai.exe --screenshot "D:/tmp/select.png" --width 1920 --height 1080

# 演奏中第 12 秒截图（日志里的 [stats] 顺便给你判定统计）
./build/cppsekai.exe --sus charts/0628_master.sus --auto \
    --screenshot "D:/tmp/play.png" --screenshot-time 12

# 只看 BGM 对轴对不对：拿 3 秒处的画面，看音符是不是压在判定线上
./build/cppsekai.exe --sus charts/0075_master.sus --auto \
    --screenshot "D:/tmp/align.png" --screenshot-time 3 --filler 9.0

# 回归测：中途放弃换曲（复现曾经的卡死）
./build/cppsekai.exe --sus charts/0075_master.sus --auto --test-restart --restart-at 8

# 判定文字逐帧看
./build/cppsekai.exe --sus charts/0075_master.sus --auto --judge-frame 3 \
    --screenshot "D:/tmp/judge.png" --screenshot-time 6

# 结算画面：启动即进（样例数字），入场动画跑完自动抓图
./build/cppsekai.exe --sus charts/0628_expert.sus --result-preview \
    --screenshot "D:/tmp/result.png" --width 1920 --height 1080

# 结算画面：真实跑一段再切（用实际判定数据）
./build/cppsekai.exe --sus charts/0628_master.sus --auto --result-at 8 \
    --screenshot-time 30 --screenshot "D:/tmp/result_live.png"
```

> 结算画面在 `--screenshot` 模式下**不等 `--screenshot-time`**：入场动画跑完
> （2.6 s）就自动抓，所以那个参数给大一点无所谓。

要点：

- 路径给**绝对路径**（或 Windows 风格的 `D:/tmp/x.png`）最稳；相对路径是相对**当前工作目录**存的。
- 每次运行都会往日志里打 `[stats] perfect=… great=… miss=… maxCombo=… score=… life=…`，
  无头模式可以直接拿来当断言（自动演示应当全 PERFECT、0 miss）。
- `--screenshot` **完全不写 `userdata.json`**，随便跑，不会污染存档。
- 贴图加载耗时想细看：设环境变量 `CPSEKAI_ASSET_TIMING=1`，日志里会多出每张贴图的耗时。
- 看动画的某一帧：卡片（设置卡 / 各弹窗）的入场只有 0.16s，`--screenshot` 的时机根本追不上，
  所以有个 `CPSEKAI_CARD_T=<0..1>`——把**所有卡片的动画冻结在这个进度**上再抓图，
  `0.3` 左右最能看出内容是不是跟着框一起缩放/淡入。
- `--flick-as-tap`：本次运行把 flick 音符按 tap 判（等价于设置里的「Flick 视作 Tap」，**不写档案**）。
- **失血阴影**（掉血时四边变暗）：`CPSEKAI_VIGNETTE=0.85` 把阴影冻结在这个强度上，用来截无头对比图——
  掉血需要真人漏接，`--screenshot` 造不出来。`0` = 关（当对照图用）。

---

## 六、涉及的数据文件

| 文件 | 位置 | 说明 |
|---|---|---|
| `userdata.json` | `<exe>/../userdata.json`（有 `charts/` 时）否则 `<exe>/userdata.json` | 设置 + 成绩。成绩 key 是**谱面文件名**（`0628_master.sus`），所以重下同样的谱成绩能对上 |
| `profiles/<id>.json` | 同上一层的 `profiles/` | 多用户：每个用户一份（内容就是上面那份 JSON），`profiles/index.json` 是用户列表。设置 → 账户 里能**导出 / 导入**单份文件（原生文件选择框，导出的是完整一份档案），见 AGENTS.md |
| `music-levels.json` | exe 同一层或上一层 | `{"628":[8,13,18,26,29]}` 或官方 `musicDifficulties` 原样数组。选曲界面的难度定数来源 |
| `musics.json` | exe 同一层或上一层 | 官方曲目表（`setup.sh` 拉下来的那份）。只用里面的 `id` + `pronunciation`（读音），给「按名称排序」和「按标题分组」用；缺了就是按标题字符串排 |
| `cppsekai.log` | 工作目录 | 无控制台时的日志（含 `--screenshot`） |
| `<musicId>.json` / `<谱面名>.json` | `charts/` 里 | sidecar：曲名/艺术家/Vo./`fillerSec`/`offset`/`mv` 标签，详见 CHARTS.md |

想清掉成绩：删 `userdata.json`（设置也会一起回默认）。
想只清设置：编辑文件里的 `settings` 段。

---

## 七、常见配方

```bash
# 全屏、不锁帧（高刷屏）、音量小一点
./build/cppsekai.exe --window fullscreen --fps 0 --se-volume 0.5

# 自动演示 + 60 帧上限，挂机看谱
./build/cppsekai.exe --sus charts/0628_master.sus --auto --fps 60

# 拿一张谱当"播放器"听歌（不接输入、不写成绩）
./build/cppsekai.exe --sus charts/0075_master.sus --bgm charts/0075.mp3 --auto

# 换一个谱面目录（不动仓库里的 charts/）
./build/cppsekai.exe --charts "D:/sekai/charts" 
```

选曲界面键盘/鼠标/触摸速查（README 第 3 节有完整表）：

| 操作 | 效果 |
|---|---|
| 滚轮 / 拖拽 / 触摸滑动 | 滚列表。**滚动时不选中**，松手后停在中间的那首才是选中项；列表首尾相接（滚过最后一首接着第一首），没有滚动条 |
| 单击 / 点按某一行 | 选中它，列表滑到把该行放到中间 |
| 双击 / 双击触摸 | 直接开打 |
| 排序 / 分组下拉框 | 搜索框右边：排序「按名称 / 按难度」、分组「关闭 / 按难度段 / 按标题（あ か さ…）」 |
| `↑` `↓` | 上/下一首 |
| `Enter` | 开打当前选中 |
| `F5` | 重新扫描谱面目录 |
| `H` | 设置面板 |
| `ESC` | 退出 |
| 手柄方向键 / 左摇杆 | 上/下一首（按住会连续） |
| 手柄 `←` `→` | 换难度 |
| 手柄 `A` | 开打（等同 `Enter`） |
| 手柄 `Y` | 重新扫描（等同 `F5`） |
| 手柄 `START` | 设置面板（等同 `H`） |

> 手柄只做菜单：演奏画面**不吃手柄输入**（12 轨的东西手柄打不了）。实现上是把手柄按键
> 翻译成键盘按键塞回 SDL 队列，所以上面这些键怎么走，手柄就怎么走。详见 AGENTS.md
> 的「手柄 / 音量 / 结算配色」。

> 「按名称」和「按标题分组」用的是官方读音（`musics.json` 的 `pronunciation`）：片假名标题
> 会折成平假名再排序，所以「ウミユリ海底譚」落在 **あ行**。没有读音的谱（自制谱）退回用标题
> 本身当排序键，汉字会排在所有假名之后（都归到「その他」）。

---

## 八、退出码

| 码 | 含义 |
|---|---|
| `0` | 正常退出（含 `--help`、`--screenshot` 完成） |
| `1` | 启动失败：`SDL_Init` / 建窗口 / GL 3.3 上下文 / 资源加载失败（原因写在日志里） |

---

## 九、坑

- **引号**：路径带空格要引起来。cmd 用双引号；PowerShell 里 `--title "砂の惑星"` 没问题，
  但 PowerShell 会吃掉 `--` 之前的东西，别用 `--%` 混着写。
- **中文 / 日文参数**：程序内部按 UTF-8 处理，命令行也是从 `GetCommandLineW()` 重新取的，
  所以 cmd（GBK 代码页）里敲日文标题也能正确显示。
- **`--charts` 给了就只用它**：不会再去 `exe/../charts` 兜底。
- **不要用 `--auto` 当日常启动参数**：它连经验都不给、也不接输入（设置里的「自动演出」是
  另一回事：给经验、不写谱面成绩，见 README 的 Q&A）。
- **别在 D 盘跑 zig 编译**（缓存必须放 C 盘，`build.sh` 已经处理）；这条只影响编译，不影响运行。

---

## 十、谱面下载器（`chartdl.exe`）

不想手抄 curl 就双击 `build/chartdl.exe`（界面见 [docs/preview_downloader.png](docs/preview_downloader.png)）：

- 左边是官方曲目表（搜索：id / 曲名 / 读音），勾选多首 → **queue checked** 批量下
- 右边是选中曲目的详情：5 个难度、**每个演唱版本**（点了就下那个版本的 BGM）、曲绘、sidecar 元数据
- 顶栏 **删除文件**：选中一首本地已经有文件的曲子时启用，删掉它的谱面 / 音频 / 曲绘 / 元数据
  （含残留的 `.part`），会先弹确认框。文件名用的是下载时那套 helper，所以不会误删别的曲子
- 下载中显示总进度条 + 当前文件大小，日志在下面；已存在的文件默认跳过
- 命令行也能用（方便脚本化）：

```bash
./build/chartdl.exe --list 374                       # 查歌
./build/chartdl.exe --download 374 --diffs all --vocals all
./build/chartdl.exe --download 75,127 --out ../charts --force
```

无头自检开关（配 `--screenshot` 用，见 `.workbuddy/tools/chartdl_*_check.py`）：
`--select <row>`、`--scroll-detail <n>`、`--open-settings`、`--dpi <96|120|144|192>`、
`--delete-selected`（按下「删除文件」但不弹确认框，**真删**，务必把 `--out` 指到临时目录）。

文件放到 `<out>/`（默认 `..\charts`），命名和游戏要求一致：谱面 `0374_master.sus`、
BGM `<assetbundleName>.mp3`（`se_0374_01.mp3` / `an_0374_02.mp3` …）、曲绘 `0374.png`、元数据 `0374.json`。

---

## ⚠️ 免责声明

- **非官方**：本项目与 SEGA / Colorful Palette 及 Project SEKAI 官方**没有任何关系**，
  未获授权、赞助或认可；`chartdl` 取的也不是官方渠道，是第三方镜像。
- **素材权利**：下载到的谱面 / BGM / 曲绘**权利全部归原权利人**，本项目不主张任何权利。
- **仅限本地个人使用**：**禁止**分发、公开传播与任何商业使用；用 `--out` 指到别的目录
  也不改变这一点。默认落在 `charts/`（已被 gitignore）是刻意的。
- **无担保 / 责任自负**：工具按 AS IS 提供，镜像站随时可能失效或改版，后果自负。

完整版见 [COPYRIGHT.md](COPYRIGHT.md) 第九节，谱面放置规则见 [CHARTS.md](CHARTS.md)。
