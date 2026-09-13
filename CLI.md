# CppSekai 命令行手册

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
         [--se-volume <0-1>] [--lead-in <sec>] [--pjsk-font]
         [--title <text>] [--lyricist <text>] [--composer <text>]
         [--arranger <text>] [--vocal <text>] [--difficulty <text>]
         [--width <px>] [--height <px>] [--window borderless|windowed|fullscreen]
         [--fps <n>] [--screenshot <png>] [--screenshot-time <sec>]
         [--judge-sheet] [--judge-frame <n>] [--test-hits]
         [--show-pause-dialog] [--test-restart] [--restart-at <sec>]
         [--result-preview] [--result-at <sec>] [--help]
```

### 4.1 内容 / 对齐

| 参数 | 说明 |
|---|---|
| `--sus <file.sus>` | 指定谱面。给了就直接进演奏，不给就进选曲界面 |
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
| `--pjsk-font` | 用自带的 pjsk 字体；默认跟随系统 UI 字体（会自动挑一个带中日文字形的） |

### 4.4 无头自检 / 调试

| 参数 | 说明 |
|---|---|
| `--screenshot <png>` | 无头跑一帧存 PNG 然后退出。选曲界面默认在启动后 1.2s 抓，演奏中在 `--screenshot-time` 抓 |
| `--screenshot-time <sec>` | 演奏模式抓图的时间点（谱面时间，默认 4.0）。**选曲界面也给这个参数时**按它抓（最少 0.5s），用来等滚动/动画停稳 |
| `--judge-sheet` | 把 6 张判定文字贴图并排画在屏幕下方（对判定文字动画/UV 用） |
| `--judge-frame <n>` | 把判定文字**冻结**在第 n 帧（60fps 计），用来逐帧核对动画 |
| `--test-hits` | 不按键，按时间轴把音符逐个喂给判定引擎（查特效链路） |
| `--show-pause-dialog` | 演奏 0.5s 后强制打开暂停弹窗（截弹窗用的） |
| `--test-restart` `--restart-at <sec>` | 走到指定秒数执行「放弃 → 换一首」——回归测「打到一半重选曲卡死」那个 bug |
| `--result-at <sec>` | 谱面走到指定秒数就切到**结算画面**（用真实判定数据），不用等整首歌放完 |
| `--result-preview` | 启动即进结算画面，且用参考截图的样例数字（940021 / PERFECT 634 …），专门用来跟原版截图做像素对比 |
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

---

## 六、涉及的数据文件

| 文件 | 位置 | 说明 |
|---|---|---|
| `userdata.json` | `<exe>/../userdata.json`（有 `charts/` 时）否则 `<exe>/userdata.json` | 设置 + 成绩。成绩 key 是**谱面文件名**（`0628_master.sus`），所以重下同样的谱成绩能对上 |
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
- **不要用 `--auto` 当日常启动参数**：虽然现在不会改存档了，但自动演示不写成绩、不接输入。
- **别在 D 盘跑 zig 编译**（缓存必须放 C 盘，`build.sh` 已经处理）；这条只影响编译，不影响运行。
