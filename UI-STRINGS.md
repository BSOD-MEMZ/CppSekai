# UI-STRINGS.md — 会显示给用户的字符串清单

> 口径：**只收会出现在屏幕上、需要翻译的字符串**（截至 2026-11-18 的源码）。
> 由 `.workbuddy/tools/gen_ui_string_inventory.py` 生成，改了 UI 文字后重跑即可：
> `python .workbuddy/tools/gen_ui_string_inventory.py`
>
> **不收**：日志（`platform::logf` / `printf` / `appendLog` …）、内部错误文本、
> 资源路径与 json 键名、贴图里画好的字（`PERFECT` / `FULL COMBO` / `LIVE RESULT`
> 都是 sprite，源码里没有对应字符串）、以及数据表内容（`musics.json` 歌名、
> `music-aliases.json` 别名、`assets/story/scripts/*.json` 剧情台词）。
>
> 共 **654** 条，分 **87** 个区块。行号会随代码漂移，区块标题比行号可靠。
>
> ⚠️ 两块**不要翻译**：
> 1. `platform/ChartDlGuide.cpp` 的「全部完成 / 完成，失败 / 下载完成 / 已经下载完整」——
>    那是用来**匹配**下载器状态栏文字的，翻了就认不出进度。
> 2. `main.cpp` 的角色 / 团体名对照表——中日双名本来就内置，属于数据不是界面文案。

### 打开谱面 · 文件对话框

| 文件:行 | 原文 |
|---|---|
| `main.cpp:118` | SUS 谱面 (*.sus) |
| `main.cpp:119` | 所有文件 (*.*) |
| `main.cpp:128` | 打开 SUS 谱面 |

### 命令行 --help（面向高级用户，一般不进翻译）

| 文件:行 | 原文 |
|---|---|
| `main.cpp:610` | into the window (letterbox). Same as the 渲染模式 setting.\n |
| `main.cpp:617` | copy logs in as a different user. Same as 设置 -> 系统.\n |
| `main.cpp:618` | --party [--party-name <name>]: force 多人游玩 on for this run and join\n |
| `main.cpp:621` | host alone plays the BGM). Same as 设置 -> 系统 ->\n |
| `main.cpp:622` | 多人游玩.\n |
| `main.cpp:623` | --no-party: force 多人游玩 off for this run (plain single window),\n |
| `main.cpp:626` | 下载谱面 button from the log (a posted click cannot\n |
| `main.cpp:632` | the window: the host presses 确定 on whatever chart the\n |
| `main.cpp:634` | the phone panel and presses 确定.\n |
| `main.cpp:644` | saved 判定 > Flick 视作 Tap setting is not changed).\n |
| `main.cpp:649` | --confirm-flash [<sec>]: play the 确定 white burst in the song list (debug;\n |
| `main.cpp:652` | given page (0 演奏 / 1 画面 / 2 外观 / 3 判定 / 4 系统 / 5 账户 /\n |
| `main.cpp:653` | 6 关于).\n |
| `main.cpp:654` | --select-filter: open the song-select 筛选 card (the header funnel).\n |
| `main.cpp:658` | assets (tab logos / 楽曲Lv. plate); default zh-CN.\n |
| `main.cpp:659` | --unit <0-7>: preselect a 团体 tab (0 all / 1 VS / 2 L/N / 3 MMJ / 4 VBS /\n |
| `main.cpp:662` | --guess: open the 猜歌 alias quiz (the header button's card) at boot.\n |
| `main.cpp:663` | --singer-panel: open the 切换歌手 vocal-version panel (the phone panel's\n |
| `main.cpp:668` | produced; same switch as settings > 判定 > 触摸轨迹日志).\n |

### 设置 · 演奏 / 画面 / 外观

| 文件:行 | 原文 |
|---|---|
| `main.cpp:4803` | 指定歌手 |
| `main.cpp:4806` | 指定歌手 |
| `main.cpp:4808` | セカイver |
| `main.cpp:4875` | 演奏 |
| `main.cpp:4875` | 画面 |
| `main.cpp:4875` | 外观 |
| `main.cpp:4875` | 判定 |
| `main.cpp:4876` | 系统 |
| `main.cpp:4876` | 账户 |
| `main.cpp:4876` | 关于 |
| `main.cpp:4993` | 音量 |
| `main.cpp:4995` | BGM 音量 |
| `main.cpp:5006` | 音效音量 |
| `main.cpp:5019` | 手感 |
| `main.cpp:5021` | 音频偏移 |
| `main.cpp:5028` | 音符速度 |
| `main.cpp:5043` | 奖励 |
| `main.cpp:5046` | 演出能量（经验 ×%.0f） |
| `main.cpp:5057` | 玩法 |
| `main.cpp:5059` | 默认歌手 |
| `main.cpp:5074` | 自动演出 |
| `main.cpp:5088` | 无 MV |
| `main.cpp:5088` | 原曲 MV |
| `main.cpp:5103` | 输入 |
| `main.cpp:5105` | 按键映射 |
| `main.cpp:5119` | 按键映射　%s |
| `main.cpp:5137` | 手柄 |
| `main.cpp:5139` | 手柄震动 |
| `main.cpp:5150` | 震动 |
| `main.cpp:5155` | 命中 Flick 时震动 |
| `main.cpp:5162` | 命中绝赞（黄键）时震动 |
| `main.cpp:5169` | MISS 时震动 |
| `main.cpp:5216` | 界面与渲染 |
| `main.cpp:5221` | 界面缩放 |
| `main.cpp:5236` | 选曲页最高 100%（固定两栏版式） |
| `main.cpp:5239` | 渲染模式 |
| `main.cpp:5245` | 跟随窗口分辨率 |
| `main.cpp:5245` | 固定分辨率 |
| `main.cpp:5260` | 性能 |
| `main.cpp:5262` | 帧率上限 |
| `main.cpp:5270` | 显示帧率 |
| `main.cpp:5276` | 实测 %.0f fps · %d Hz 关垂直同步 |
| `main.cpp:5281` | 窗口 |
| `main.cpp:5283` | 分辨率 |
| `main.cpp:5288` | 自定义… |

### 设置 · 判定 / 系统

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5364` | 界面 |
| `main.cpp:5367` | 显示播放进度条 |
| `main.cpp:5378` | 经典开屏画面 |
| `main.cpp:5386` | 打击特效 |
| `main.cpp:5396` | 弱化打击特效 |
| `main.cpp:5406` | 背景 |
| `main.cpp:5411` | 选曲背景 |
| `main.cpp:5417` | 默认渐变 |
| `main.cpp:5417` | 桌面壁纸 |
| `main.cpp:5417` | Aero 玻璃 |
| `main.cpp:5418` | 默认渐变 |
| `main.cpp:5418` | 桌面壁纸 |
| `main.cpp:5442` | 背景模糊 |
| `main.cpp:5449` | 背景模糊 |
| `main.cpp:5459` | 背景变暗 |
| `main.cpp:5462` | 背景变暗 |
| `main.cpp:5475` | 窗口玻璃 |
| `main.cpp:5478` | 玻璃实现 |
| `main.cpp:5485` | 无边框窗口 |
| `main.cpp:5557` | 判定窗口 (ms) |
| `main.cpp:5589` | Bad 与 Miss 同步 |
| `main.cpp:5610` | 长条容错 (ms) |
| `main.cpp:5624` | 松手容错 %.0f |
| `main.cpp:5626` | 起按容错 %.0f |
| `main.cpp:5639` | 严格 Flick 方向 |
| `main.cpp:5646` | Flick 视作 Tap |
| `main.cpp:5659` | 触摸轨迹日志 |
| `main.cpp:5672` | 生命 / 综合力 |

### 设置 · 诊断（日志分类 / 结束实例）

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5738` | 窗口行为 |
| `main.cpp:5741` | 失焦时自动暂停 |
| `main.cpp:5748` | 最小化窗口静音 |

### 设置 · 账户（用户 / 导入导出）

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5819` | 多实例 |
| `main.cpp:5821` | 多开 |
| `main.cpp:5825` | 只允许一个实例 |
| `main.cpp:5825` | 允许多开，新实例登录另一个用户 |
| `main.cpp:5855` | 多人演出 |
| `main.cpp:5887` | 多人游玩已开启，重开所有窗口生效。 |
| `main.cpp:5893` | 诊断 |
| `main.cpp:5903` | 日志输出　%d/%d 类 |
| `main.cpp:5915` | 结束当前实例 |
| `main.cpp:5923` | 确认结束？ |
| `main.cpp:5935` | 结束所有实例 |

### 设置 · 关于

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5970` | 用户 |
| `main.cpp:6007` | 当前 |
| `main.cpp:6031` | 新建用户 |
| `main.cpp:6032` | 用户 2 |
| `main.cpp:6034` | 用户 |
| `main.cpp:6066` | 确认删除？ |
| `main.cpp:6094` | 删除用户 |
| `main.cpp:6101` | 导出 |
| `main.cpp:6119` | 已导出： |

### 键位设置 / 多开确认

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6167` | 资料 |
| `main.cpp:6169` | 昵称 |
| `main.cpp:6197` | 学校 / 组织 |
| `main.cpp:6211` | 等级 |
| `main.cpp:6211` | 本级经验 |
| `main.cpp:6215` | 签名 |
| `main.cpp:6233` | 头像 |
| `main.cpp:6260` | 无 |
| `main.cpp:6271` | 选择图片… |
| `main.cpp:6278` | 清除头像 |
| `main.cpp:6283` | PNG / JPG / BMP，建议使用正方形图片。多人演出时会显示。 |
| `main.cpp:6299` | 版本 1.0.0.8 · 许可 AGPL-3.0-only |
| `main.cpp:6305` | 策划 / 开发　xxt8582753（BSOD-MEMZ） |
| `main.cpp:6307` | 测试　Shitakara4banme |
| `main.cpp:6309` | 株洲市南方中学 · 2026 年 9 月 |
| `main.cpp:6324` | 访问 xxtsoft |
| `main.cpp:6328` | 许可与免责声明 |
| `main.cpp:6336` | 重播新手教学 |
| `main.cpp:6360` | 本项目免费、开源、非营利，与 SEGA、Colorful Palette 及《初音未来： |
| `main.cpp:6361` | 缤纷舞台》官方没有任何关系，仅供本地学习与练习使用。 |
| `main.cpp:6362` | 全部曲名、角色、美术与音频的权利归各自权利人所有。 |
| `main.cpp:6411` | 确定 |
| `main.cpp:6473` | 按键映射 |
| `main.cpp:6491` | %d 按新键… |
| `main.cpp:6507` | 恢复默认键位 |
| `main.cpp:6514` | 确定 |

### 退出确认

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6560` | 默认歌手 |

### 删除歌曲 · 文件统计

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6706` | 全部启用 |
| `main.cpp:6712` | 全部关闭 |

### 删除歌曲确认

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6758` | 开启多开？ |
| `main.cpp:6760` | 多人演出是实验性功能：同一台机器开几个窗口，每个窗口各登录一个用户，窗口多了可能掉帧或时钟漂移，并且存在大量已知 Bug，确定开启吗？ |

### 导入用户数据

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6810` | 现在退出不会保存本局的成绩。 |
| `main.cpp:6812` | 继续演出 |
| `main.cpp:6812` | 退出 |

### 多人 · 房主状态 / 加载失败

| 文件:行 | 原文 |
|---|---|
| `main.cpp:9836` | 等待房主选曲 |
| `main.cpp:9841` | 本窗口没有 |
| `main.cpp:9842` | ，无法参加本曲 |
| `main.cpp:9844` | 请选择难度 |
| `main.cpp:9846` | 准备好的话就开始吧 |
| `main.cpp:9848` | 等待其他玩家 |
| `main.cpp:9855` | 即将开始 |
| `main.cpp:9855` | 谱面加载中… |

### 选曲 · 五十音 / 分组标签

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:1945` | 初音ミク |
| `game/SongSelect.cpp:1945` | 鏡音リン |
| `game/SongSelect.cpp:1945` | 鏡音レン |
| `game/SongSelect.cpp:1946` | 巡音ルカ |
| `game/SongSelect.cpp:1947` | 星乃一歌 |
| `game/SongSelect.cpp:1947` | 天馬咲希 |
| `game/SongSelect.cpp:1948` | 望月穂波 |
| `game/SongSelect.cpp:1948` | 日野森志歩 |
| `game/SongSelect.cpp:1949` | 花里みのり |
| `game/SongSelect.cpp:1949` | 桐谷遥 |
| `game/SongSelect.cpp:1950` | 桃井愛莉 |
| `game/SongSelect.cpp:1950` | 日野森雫 |
| `game/SongSelect.cpp:1951` | 小豆沢こはね |
| `game/SongSelect.cpp:1951` | 白石杏 |
| `game/SongSelect.cpp:1952` | 東雲彰人 |
| `game/SongSelect.cpp:1952` | 青柳冬弥 |
| `game/SongSelect.cpp:1953` | 天馬司 |
| `game/SongSelect.cpp:1953` | 鳳えむ |
| `game/SongSelect.cpp:1954` | 草薙寧々 |
| `game/SongSelect.cpp:1954` | 神代類 |
| `game/SongSelect.cpp:1955` | 宵崎奏 |
| `game/SongSelect.cpp:1955` | 朝比奈まふゆ |
| `game/SongSelect.cpp:1956` | 東雲絵名 |
| `game/SongSelect.cpp:1956` | 暁山瑞希 |

### 选曲 · 个人资料卡

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:5580` | 搜索 |
| `game/SongSelect.cpp:5611` | 剪切 |
| `game/SongSelect.cpp:5616` | 复制 |
| `game/SongSelect.cpp:5619` | 粘贴 |

### 原生菜单栏

| 文件:行 | 原文 |
|---|---|
| `platform/NativeMenu.cpp:39` | 打开谱面(&O)...\tCtrl+O |
| `platform/NativeMenu.cpp:41` | 设置(&S)... |
| `platform/NativeMenu.cpp:43` | 刷新谱面列表(&R)\tF5 |
| `platform/NativeMenu.cpp:44` | 音乐商店（下载谱面）(&D)... |
| `platform/NativeMenu.cpp:46` | 结束当前实例(&C) |
| `platform/NativeMenu.cpp:47` | 结束所有实例(&A)... |
| `platform/NativeMenu.cpp:48` | 文件(&F) |
| `platform/NativeMenu.cpp:54` | 加入收藏夹(&F) |
| `platform/NativeMenu.cpp:55` | 删除谱面文件(&D)... |
| `platform/NativeMenu.cpp:56` | 编辑(&E) |
| `platform/NativeMenu.cpp:64` | 窗口模式(&W) |
| `platform/NativeMenu.cpp:69` | 按名称(&N) |
| `platform/NativeMenu.cpp:70` | 按难度(&L) |
| `platform/NativeMenu.cpp:71` | 排序方式(&O) |
| `platform/NativeMenu.cpp:74` | 关闭(&O) |
| `platform/NativeMenu.cpp:75` | 按难度段(&D) |
| `platform/NativeMenu.cpp:76` | 按读音(&R) |
| `platform/NativeMenu.cpp:77` | 按首字(&I) |
| `platform/NativeMenu.cpp:78` | 按收藏(&F) |
| `platform/NativeMenu.cpp:79` | 分组依据(&P) |
| `platform/NativeMenu.cpp:82` | 显示帧率(&F) |
| `platform/NativeMenu.cpp:85` | 显示播放进度条(&B) |
| `platform/NativeMenu.cpp:86` | 弱化打击特效(&E) |
| `platform/NativeMenu.cpp:87` | 视图(&V) |
| `platform/NativeMenu.cpp:91` | 关于本软件(&A) |
| `platform/NativeMenu.cpp:92` | 许可与免责声明(&L) |
| `platform/NativeMenu.cpp:95` | 访问 xxtsoft(&X) |
| `platform/NativeMenu.cpp:96` | 帮助(&H) |
| `platform/NativeMenu.cpp:212` | 取消收藏(&F) |
| `platform/NativeMenu.cpp:212` | 加入收藏夹(&F) |

### 开头卡片 · 曲目信息

| 文件:行 | 原文 |
|---|---|
| `game/Intro.cpp:726` | 作词： |
| `game/Intro.cpp:726` | 作曲： |
| `game/Intro.cpp:726` | 编曲： |
| `game/Intro.cpp:727` | 演唱： |

### 教学 · 命名提示

| 文件:行 | 原文 |
|---|---|
| `game/Story.cpp:64` | *之后也可更改。 |
| `game/Story.cpp:65` | 「世界」的居民 |
| `game/Story.cpp:66` | 确定 |

### 教学 · 完成按钮

| 文件:行 | 原文 |
|---|---|
| `game/Story.cpp:846` | 完成 |

### 教学 · 名字长度提示

| 文件:行 | 原文 |
|---|---|
| `game/Story.cpp:917` | 名字最多可输入 |
| `game/Story.cpp:917` | 个字符。 |

### 教学 · 操作提示

| 文件:行 | 原文 |
|---|---|
| `game/StoryUi.cpp:904` | 松手就选它 |
| `game/StoryUi.cpp:904` | 点一下选这个 |

### 教学 · 图片缺失

| 文件:行 | 原文 |
|---|---|
| `game/StoryUi.cpp:963` | （图片没找到） |

### 设置 · 账户 / 新建用户

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5780` | 关联 .sus 谱面文件 |

### 系统集成 · 文件类型名

| 文件:行 | 原文 |
|---|---|
| `platform/ShellIntegration.cpp:18` | SUS 谱面 (CppSekai) |

### 下载器 · 窗口标题匹配

| 文件:行 | 原文 |
|---|---|
| `platform/ChartDlGuide.cpp:24` | CppSekai 谱面下载器 |

### 下载器 · 状态栏文字匹配（改了就认不出来，不要翻译）

| 文件:行 | 原文 |
|---|---|
| `platform/ChartDlGuide.cpp:254` | 全部完成 |
| `platform/ChartDlGuide.cpp:254` | 完成，失败 |
| `platform/ChartDlGuide.cpp:255` | 已经下载完整 |
| `platform/ChartDlGuide.cpp:255` | 下载完成 |

### 下载器 · --help

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:1233` | 不到 1 秒 |
| `downloader/chartdl.cpp:1238` | %d 分 %d 秒 |
| `downloader/chartdl.cpp:1240` | %d 秒 |

### 下载器 · 详情面板

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:3276` | （在左边选一首歌） |

### 下载器 · 主窗口控件

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2460` | 下载器设置 |
| `downloader/chartdl.cpp:2517` | 输出目录 |
| `downloader/chartdl.cpp:2519` | 浏览… |
| `downloader/chartdl.cpp:2520` | 打开目录 |
| `downloader/chartdl.cpp:2522` | 搜索 |
| `downloader/chartdl.cpp:2524` | 下载勾选的歌曲 |
| `downloader/chartdl.cpp:2525` | 全选 / 全不选 |
| `downloader/chartdl.cpp:2526` | 取消 |
| `downloader/chartdl.cpp:2529` | 删除文件 |
| `downloader/chartdl.cpp:2537` | 不含 MV |
| `downloader/chartdl.cpp:2538` | 原曲 MV |
| `downloader/chartdl.cpp:2539` | セカイ MV |
| `downloader/chartdl.cpp:2540` | 两个都要 |
| `downloader/chartdl.cpp:2542` | 设置… |
| `downloader/chartdl.cpp:2564` | 下载内容 |

### 下载器 · 托盘 / 气泡

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2648` | 就绪 |

### 下载器 · 删除确认 / 提示

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2764` | CppSekai 谱面下载器仍在运行 |
| `downloader/chartdl.cpp:2765` | 双击托盘图标可恢复窗口。 |
| `downloader/chartdl.cpp:2779` | 选择谱面输出目录 |

### 下载器 · 下载队列状态

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2845` | 删除《 |
| `downloader/chartdl.cpp:2846` | 》在本地已下载的文件？\n\n |
| `downloader/chartdl.cpp:2847` | 谱面 / 音频 / 曲绘 / 2D MV / 元数据等将被永久删除。\n |
| `downloader/chartdl.cpp:2848` | 目录： |
| `downloader/chartdl.cpp:2849` | CppSekai 谱面下载器 |
| `downloader/chartdl.cpp:2851` | 已取消删除 |

### 下载器 · 已下载完整

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2937` | 所有歌曲已下载完成。 |
| `downloader/chartdl.cpp:2938` | 没有勾选任何歌曲。 |
| `downloader/chartdl.cpp:2939` | CppSekai 谱面下载器 |
| `downloader/chartdl.cpp:2946` | 开始下载… |
| `downloader/chartdl.cpp:2947` | 首歌曲，共 |
| `downloader/chartdl.cpp:2948` | 个文件 === |
| `downloader/chartdl.cpp:2950` | 首已下载完整，已跳过 |

### 下载器 · 进度 / 完成

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:3036` | 这首歌已经下载完整了 |

### 下载器 · 详情面板

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:3389` | 曲绘 |
| `downloader/chartdl.cpp:3392` | 元数据 (sidecar json) |

### 其他（未归类，需人工确认）

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:1267` | [--delete-selected]       press 删除文件 on that row, with no\n |
| `downloader/chartdl.cpp:1535` | 曲名 |
| `downloader/chartdl.cpp:1535` | 演唱版本 |
| `downloader/chartdl.cpp:1535` | 读音 |
| `downloader/chartdl.cpp:1536` | 已下载 |
| `downloader/chartdl.cpp:1774` | 版本 |
| `downloader/chartdl.cpp:1782` | 已下载 |
| `downloader/chartdl.cpp:1784` | 缺 MV |
| `downloader/chartdl.cpp:1786` | 部分 |
| `downloader/chartdl.cpp:1962` | 个文件（ |
| `downloader/chartdl.cpp:1965` | 个（可能被占用） |
| `downloader/chartdl.cpp:1965` | ，失败 |
| `downloader/chartdl.cpp:2243` | CppSekai 谱面下载器 |
| `downloader/chartdl.cpp:2319` | 关闭窗口时 |
| `downloader/chartdl.cpp:2320` | 退出程序 |
| `downloader/chartdl.cpp:2322` | 隐藏到托盘（后台继续下载） |
| `downloader/chartdl.cpp:2324` | 下载与提醒 |
| `downloader/chartdl.cpp:2325` | 下载完成后弹出气泡提醒 |
| `downloader/chartdl.cpp:2327` | 最小化时收进托盘 |
| `downloader/chartdl.cpp:2329` | 批量下载全部演唱版本（流量大、速度慢） |
| `downloader/chartdl.cpp:2332` | 并发连接数 |
| `downloader/chartdl.cpp:2349` | 数据来源与致谢 |
| `downloader/chartdl.cpp:2350` | 谱面 / BGM / 曲绘：unipjsk（assets.unipjsk.com） |
| `downloader/chartdl.cpp:2351` | 国服独占曲目：sekai.best（storage.sekai.best） |
| `downloader/chartdl.cpp:2352` | 2D MV（.usm）：exmeaning（storage.exmeaning.com） |
| `downloader/chartdl.cpp:2353` | 感谢以上镜像站，以及 sekai-world 的 master 镜像。 |
| `downloader/chartdl.cpp:2355` | 确定 |
| `downloader/chartdl.cpp:2356` | 取消 |
| `downloader/chartdl.cpp:2593` | （在左边选一首歌） |
| `downloader/chartdl.cpp:2616` | 曲绘 |
| `downloader/chartdl.cpp:2617` | 元数据 (sidecar json) |
| `downloader/chartdl.cpp:2626` | 别名 |
| `downloader/chartdl.cpp:2639` | 别名 |
| `downloader/chartdl.cpp:2733` | 显示主窗口 |
| `downloader/chartdl.cpp:2735` | 退出 |
| `downloader/chartdl.cpp:2832` | 正在取消… |
| `downloader/chartdl.cpp:2885` | 请选择要下载的文件。 |
| `downloader/chartdl.cpp:2886` | CppSekai 谱面下载器 |
| `downloader/chartdl.cpp:3125` | 剩余 |
| `downloader/chartdl.cpp:3128` | 下载中 |
| `downloader/chartdl.cpp:3129` | 个文件 |
| `downloader/chartdl.cpp:3135` | 个 |
| `downloader/chartdl.cpp:3135` | 全部完成 |
| `downloader/chartdl.cpp:3135` | 完成，失败 |
| `downloader/chartdl.cpp:3156` | 成功 %zu，跳过 %zu，失败 %zu |
| `downloader/chartdl.cpp:3160` | 下载完成 |
| `downloader/chartdl.cpp:3160` | 下载完成（有失败） |
| `downloader/chartdl.cpp:3415` | （无） |
| `downloader/chartdl.cpp:3418` | 已下载 |
| `downloader/chartdl.cpp:3448` | 已下载 |
| `downloader/chartdl.cpp:3461` | （没有演唱版本数据） |
| `downloader/chartdl.cpp:3479` | 曲绘 |
| `downloader/chartdl.cpp:3479` | 曲绘 已下载 |
| `downloader/chartdl.cpp:3485` | 元数据 (sidecar json) |
| `downloader/chartdl.cpp:3485` | 元数据 (sidecar json) 已下载 |
| `downloader/chartdl.cpp:3617` | CppSekai 谱面下载器 |
| `game/Result.cpp:1316` | 歌曲等级 |
| `game/Result.cpp:1401` | 得分 |
| `game/Result.cpp:1407` | 新纪录! |
| `game/Result.cpp:1419` | 最高得分 |
| `game/Result.cpp:1547` | 等级提升!  +%d EXP |
| `game/Result.cpp:1585` | 继续 |
| `game/SongSelect.cpp:623` | 「世界」ver. |
| `game/SongSelect.cpp:626` | 虚拟歌手ver. |
| `game/SongSelect.cpp:1239` | 默认用户 |
| `game/SongSelect.cpp:2152` | その他 |
| `game/SongSelect.cpp:2167` | その他 |
| `game/SongSelect.cpp:2180` | その他 |
| `game/SongSelect.cpp:2193` | あ |
| `game/SongSelect.cpp:2193` | か |
| `game/SongSelect.cpp:2193` | さ |
| `game/SongSelect.cpp:2193` | た |
| `game/SongSelect.cpp:2194` | な |
| `game/SongSelect.cpp:2194` | は |
| `game/SongSelect.cpp:2194` | ま |
| `game/SongSelect.cpp:2194` | や |
| `game/SongSelect.cpp:2195` | ら |
| `game/SongSelect.cpp:2195` | わ |
| `game/SongSelect.cpp:2197` | その他 |
| `game/SongSelect.cpp:2207` | あ |
| `game/SongSelect.cpp:2210` | た |
| `game/SongSelect.cpp:2213` | や |
| `game/SongSelect.cpp:2217` | その他 |
| `game/SongSelect.cpp:2321` | 其他 |
| `game/SongSelect.cpp:2321` | 收藏 |
| `game/SongSelect.cpp:3164` | 暂无其他演唱版本 |
| `game/SongSelect.cpp:3533` | 音乐商店 |
| `game/SongSelect.cpp:3534` | 猜歌 |
| `game/SongSelect.cpp:3535` | 刷新列表 |
| `game/SongSelect.cpp:3536` | 设置 |
| `game/SongSelect.cpp:3537` | 个人资料 |
| `game/SongSelect.cpp:3538` | 随机选曲 |
| `game/SongSelect.cpp:3539` | 筛选 |
| `game/SongSelect.cpp:3540` | 排序 |
| `game/SongSelect.cpp:3570` | 排序：名称 |
| `game/SongSelect.cpp:3570` | 排序：难度 |
| `game/SongSelect.cpp:3669` | 自动编成尚未实现。 |
| `game/SongSelect.cpp:3844` | 卡组系统尚未实现，现在只有一套队伍。 |
| `game/SongSelect.cpp:3866` | 自动编成 |
| `game/SongSelect.cpp:3868` | 自动编成尚未实现。 |
| `game/SongSelect.cpp:3884` | 当前队伍 |
| `game/SongSelect.cpp:3898` | 综合力 |
| `game/SongSelect.cpp:3914` | 综合力 = 队伍里 5 张卡片的数值合计 |
| `game/SongSelect.cpp:3936` | 角色牵绊等级 |
| `game/SongSelect.cpp:3938` | 角色羁绊等级尚未实现。 |
| `game/SongSelect.cpp:3967` | 排序 |
| `game/SongSelect.cpp:3972` | 排序：按名称 |
| `game/SongSelect.cpp:3972` | 排序：按等级 |
| `game/SongSelect.cpp:4008` | 卡组编成功能尚未实现。 |
| `game/SongSelect.cpp:4257` | 无 MV |
| `game/SongSelect.cpp:4304` | 关闭 |
| `game/SongSelect.cpp:4304` | 开启 |
| `game/SongSelect.cpp:4477` | 确定 |
| `game/SongSelect.cpp:4715` | セカイ |
| `game/SongSelect.cpp:4718` | バーチャル |
| `game/SongSelect.cpp:4726` | 他 |
| `game/SongSelect.cpp:5632` | 清空 |
| `game/SongSelect.cpp:6216` | 加入收藏夹 |
| `game/SongSelect.cpp:6216` | 取消收藏 |
| `game/SongSelect.cpp:6230` | 删除谱面文件… |
| `game/SongSelect.cpp:6368` | 这个筛选条件下没有谱面。换个团体，或者点「筛选」把条件放开 |
| `game/SongSelect.cpp:6369` | 没有找到谱面。把 .sus 放到 charts/ 目录下，再按 F5 重新扫描 |
| `game/SongSelect.cpp:6374` | 清除全部筛选 |
| `game/SongSelect.cpp:6390` | 这会清掉团体页签、搜索框和「筛选」里的全部条件。 |
| `game/SongSelect.cpp:6400` | 下载谱面 |
| `game/SongSelect.cpp:6408` | 这会跳转到音乐商店。 |
| `game/SongSelect.cpp:6553` | 原曲 |
| `game/SongSelect.cpp:6616` | 楽曲Lv. |
| `game/SongSelect.cpp:6616` | 歌曲等级 |
| `game/SongSelect.cpp:7507` | 确定 |
| `game/SongSelect.cpp:7648` | 没有 |
| `game/SongSelect.cpp:7649` | 失败 |
| `game/SongSelect.cpp:7650` | 已开始 |
| `game/SongSelect.cpp:7651` | 未确定 |
| `game/SongSelect.cpp:7652` | 选择难度 |
| `game/SongSelect.cpp:7702` | 按名称 |
| `game/SongSelect.cpp:7702` | 按难度 |
| `game/SongSelect.cpp:7706` | 排序： |
| `game/SongSelect.cpp:7976` | 音乐商店 |
| `game/SongSelect.cpp:7977` | 猜歌 |
| `game/SongSelect.cpp:7978` | 刷新列表 |
| `game/SongSelect.cpp:7979` | 设置 |
| `game/SongSelect.cpp:7980` | 个人资料 |
| `game/SongSelect.cpp:7981` | 随机选曲 |
| `game/SongSelect.cpp:7982` | 筛选 |
| `game/SongSelect.cpp:7983` | 排序 |
| `game/SongSelect.cpp:8019` | 排序：名称 |
| `game/SongSelect.cpp:8019` | 排序：难度 |
| `game/SongSelect.cpp:8123` | 个人资料 |
| `game/SongSelect.cpp:8129` | 昵称 |
| `game/SongSelect.cpp:8132` | （未设置） |
| `game/SongSelect.cpp:8134` | 学校 / 组织 |
| `game/SongSelect.cpp:8137` | （未设置） |
| `game/SongSelect.cpp:8140` | 签名 |
| `game/SongSelect.cpp:8152` | %d 次   平均 %.0f |
| `game/SongSelect.cpp:8156` | 等级 |
| `game/SongSelect.cpp:8157` | 游玩 / 平均分 |
| `game/SongSelect.cpp:8173` | 到下一级 %d / %d EXP |
| `game/SongSelect.cpp:8182` | 关闭 |
| `game/SongSelect.cpp:8226` | 猜歌 |
| `game/SongSelect.cpp:8234` | 正在准备题库… |
| `game/SongSelect.cpp:8234` | 没有可用的题库 |
| `game/SongSelect.cpp:8238` | 首次加载请耐心等待。 |
| `game/SongSelect.cpp:8239` | 找不到 music-aliases.json，或者别名表过小。 |
| `game/SongSelect.cpp:8256` | 关闭 |
| `game/SongSelect.cpp:8262` | 这个别名指的是哪首歌？ |
| `game/SongSelect.cpp:8353` | 答对了！ |
| `game/SongSelect.cpp:8354` | 答错了，正确答案是「 |
| `game/SongSelect.cpp:8357` | 答对 %d / %d    连对 %d（最佳 %d） |
| `game/SongSelect.cpp:8370` | 下一题 |
| `game/SongSelect.cpp:8370` | 跳过 |
| `game/SongSelect.cpp:8374` | 关闭 |
| `game/SongSelect.cpp:8634` | クリア状況 |
| `game/SongSelect.cpp:8634` | 通关状况 |
| `game/SongSelect.cpp:8645` | すべて |
| `game/SongSelect.cpp:8645` | 全部 |
| `game/SongSelect.cpp:8646` | 未クリア |
| `game/SongSelect.cpp:8646` | 未通关 |
| `game/SongSelect.cpp:8647` | 未フルコンボ |
| `game/SongSelect.cpp:8647` | 未全连 |
| `game/SongSelect.cpp:8648` | 未AP |
| `game/SongSelect.cpp:8648` | 未AP |
| `game/SongSelect.cpp:8649` | 未S评级 |
| `game/SongSelect.cpp:8649` | 未スコアS |
| `game/SongSelect.cpp:8668` | 2DMVあり |
| `game/SongSelect.cpp:8668` | 3DMVあり |
| `game/SongSelect.cpp:8668` | MVなし |
| `game/SongSelect.cpp:8668` | 原曲MVあり |
| `game/SongSelect.cpp:8670` | 无MV |
| `game/SongSelect.cpp:8670` | 有2DMV |
| `game/SongSelect.cpp:8670` | 有3DMV |
| `game/SongSelect.cpp:8670` | 有原曲MV |
| `game/SongSelect.cpp:8685` | 曲目 |
| `game/SongSelect.cpp:8685` | 楽曲 |
| `game/SongSelect.cpp:8688` | APPENDあり |
| `game/SongSelect.cpp:8688` | すべて |
| `game/SongSelect.cpp:8688` | クリエイト譜面あり |
| `game/SongSelect.cpp:8688` | 書き下ろし楽曲 |
| `game/SongSelect.cpp:8690` | 全部 |
| `game/SongSelect.cpp:8690` | 新创作曲 |
| `game/SongSelect.cpp:8690` | 有APPEND |
| `game/SongSelect.cpp:8690` | 有自制谱面 |
| `game/SongSelect.cpp:8750` | キャンセル |
| `game/SongSelect.cpp:8750` | 取消 |
| `game/SongSelect.cpp:8753` | 决定 |
| `game/SongSelect.cpp:8753` | 決定 |
| `game/Ui.cpp:2030` | 等级 |
| `main.cpp:1969` | 用户 |
| `main.cpp:2036` | 玩家 |
| `main.cpp:2387` | CppSekai 需要 OpenGL 3.3，而当前上下文只提供了 %d.%d。\n\n |
| `main.cpp:2388` | 最常见的原因：显卡驱动没装（Windows 只给了「Microsoft 基本显示适配器」）， |
| `main.cpp:2389` | 或者在云主机 / 远程桌面 / 没有 3D 加速的虚拟机里运行。\n\n |
| `main.cpp:2390` | 如果这台机器确实没有能用的显卡驱动，可以到发布页下载「无显卡补丁」 |
| `main.cpp:2391` | （Mesa 软件渲染），解压到本程序所在的文件夹再启动。\n |
| `main.cpp:2392` | 具体信息见程序目录下的 cppsekai.log。 |
| `main.cpp:2467` | OpenGL 初始化失败：无法加载 3.3 的函数入口点。\n\n |
| `main.cpp:2468` | 这台机器的 OpenGL 驱动不完整，多半是没装显卡驱动， |
| `main.cpp:2469` | 或者在云主机 / 远程桌面 / 没有 3D 加速的虚拟机里运行。\n\n |
| `main.cpp:2470` | 没有可用显卡驱动的机器，可以到发布页下载「无显卡补丁」 |
| `main.cpp:2471` | （Mesa 软件渲染），解压到本程序所在的文件夹再启动。 |
| `main.cpp:3146` | 收藏 |
| `main.cpp:3149` | 最近播放 |
| `main.cpp:3595` | CppSekai 谱面下载器 |
| `main.cpp:3597` | CppSekai 谱面下载器 |
| `main.cpp:3911` | 多人游玩中：暂停由房主控制 |
| `main.cpp:4149` | 谱面加载失败，文件可能损坏或不是 SUS 谱面： |
| `main.cpp:4351` | 严格 |
| `main.cpp:4351` | 宽松 |
| `main.cpp:4351` | 标准 |
| `main.cpp:4359` | 严格 |
| `main.cpp:4359` | 宽容 |
| `main.cpp:4359` | 较紧 |
| `main.cpp:4459` | 用户数据 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0 |
| `main.cpp:4505` | 图片 (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga\0 |
| `main.cpp:4506` | 所有文件 (*.*)\0*.*\0 |
| `main.cpp:4518` | 选择头像图片 |
| `main.cpp:4552` | 头像保存失败，请选择另一张图片。 |
| `main.cpp:4726` | (无昵称) |
| `main.cpp:4751` | 天馬咲希 |
| `main.cpp:4751` | 天马咲希 |
| `main.cpp:4751` | 星乃一歌 |
| `main.cpp:4751` | 星乃一歌 |
| `main.cpp:4752` | 日野森志步 |
| `main.cpp:4752` | 日野森志歩 |
| `main.cpp:4752` | 望月穂波 |
| `main.cpp:4752` | 望月穗波 |
| `main.cpp:4753` | 桐谷遥 |
| `main.cpp:4753` | 桐谷遥 |
| `main.cpp:4753` | 花里みのり |
| `main.cpp:4753` | 花里实乃里 |
| `main.cpp:4754` | 日野森雫 |
| `main.cpp:4754` | 日野森雫 |
| `main.cpp:4754` | 桃井愛莉 |
| `main.cpp:4754` | 桃井爱莉 |
| `main.cpp:4755` | 小豆沢こはね |
| `main.cpp:4755` | 小豆泽心羽 |
| `main.cpp:4755` | 白石杏 |
| `main.cpp:4755` | 白石杏 |
| `main.cpp:4756` | 东云彰人 |
| `main.cpp:4756` | 東雲彰人 |
| `main.cpp:4756` | 青柳冬弥 |
| `main.cpp:4756` | 青柳冬弥 |
| `main.cpp:4757` | 凤笑梦 |
| `main.cpp:4757` | 天馬司 |
| `main.cpp:4757` | 天马司 |
| `main.cpp:4757` | 鳳えむ |
| `main.cpp:4758` | 神代类 |
| `main.cpp:4758` | 神代類 |
| `main.cpp:4758` | 草薙宁宁 |
| `main.cpp:4758` | 草薙寧々 |
| `main.cpp:4759` | 宵崎奏 |
| `main.cpp:4759` | 宵崎奏 |
| `main.cpp:4759` | 朝比奈まふゆ |
| `main.cpp:4759` | 朝比奈真冬 |
| `main.cpp:4760` | 东云绘名 |
| `main.cpp:4760` | 晓山瑞希 |
| `main.cpp:4760` | 暁山瑞希 |
| `main.cpp:4760` | 東雲絵名 |
| `main.cpp:4761` | 初音ミク |
| `main.cpp:4761` | 初音未来 |
| `main.cpp:4761` | 鏡音リン |
| `main.cpp:4761` | 镜音铃 |
| `main.cpp:4762` | 巡音ルカ |
| `main.cpp:4762` | 巡音流歌 |
| `main.cpp:4762` | 鏡音レン |
| `main.cpp:4762` | 镜音连 |
| `main.cpp:4767` | 25時、ナイトコードで。 |
| `main.cpp:4767` | Wonderlands×Showtime |
| `main.cpp:4767` | 虚拟歌手 |
| `main.cpp:4775` | 系统（启动/窗口/菜单） |
| `main.cpp:4776` | 选曲（界面/下载/猜歌） |
| `main.cpp:4777` | 剧情（教学/脚本） |
| `main.cpp:4778` | 演奏（判定/暂停/结算） |
| `main.cpp:4779` | 音频（播放/音效） |
| `main.cpp:4780` | 输入（手柄/键盘/触摸） |
| `main.cpp:4781` | 渲染（OpenGL/帧统计） |
| `main.cpp:4782` | 多人游玩（房间同步） |
| `main.cpp:4798` | 虚拟歌手 ver |
| `main.cpp:5322` | 窗口模式 |
| `main.cpp:5348` | 原生菜单栏 |
| `main.cpp:5759` | 系统集成 |
| `main.cpp:5762` | 启用 SMTC 汇报 |
| `main.cpp:5797` | 开始游戏时切到英文输入法 |
| `main.cpp:5807` | 隐藏系统触摸特效 |
| `main.cpp:6127` | 导入 |
| `main.cpp:6139` | (无昵称) |
| `main.cpp:6145` | 不是用户数据文件： |
| `main.cpp:6575` | セカイver |
| `main.cpp:6580` | 虚拟歌手 ver |
| `main.cpp:6585` | 指定歌手 |
| `main.cpp:6640` | 确定 |
| `main.cpp:6675` | 日志输出 |
| `main.cpp:6718` | 确定 |
| `main.cpp:6762` | 取消 |
| `main.cpp:6762` | 确定开启 |
| `main.cpp:6808` | 要退出吗？ |
| `main.cpp:6948` | 个文件： |
| `main.cpp:6948` | 将删除 |
| `main.cpp:6949` | 个谱面 + |
| `main.cpp:6949` | 张曲绘 + |
| `main.cpp:6950` | 个音频 |
| `main.cpp:6952` | 个元数据 |
| `main.cpp:6997` | 删除这首歌？ |
| `main.cpp:6999` | 歌曲： |
| `main.cpp:7001` | 位置： |
| `main.cpp:7002` | 删掉无法恢复；这个目录里别的歌不受影响。 |
| `main.cpp:7004` | 删除 |
| `main.cpp:7004` | 取消 |
| `main.cpp:7066` | 导入用户数据？ |
| `main.cpp:7068` | 文件： |
| `main.cpp:7069` | 条 |
| `main.cpp:7069` | 用户： |
| `main.cpp:7069` | ，成绩 |
| `main.cpp:7070` | 导入会用这份数据覆盖当前用户（ |
| `main.cpp:7070` | ）的设置、成绩和资料，无法撤销。 |
| `main.cpp:7072` | 取消 |
| `main.cpp:7072` | 覆盖导入 |
| `main.cpp:7092` | 导入失败： |
| `main.cpp:7117` | 已导入： |
| `main.cpp:7139` | 多人游玩已开启 |
| `main.cpp:7141` | 现在这个窗口还在单人模式里，立即重启窗口来准备多人游玩。 |
| `main.cpp:7143` | 稍后 |
| `main.cpp:7143` | 立即重启 |
| `main.cpp:7570` | 小豆沢こはね |
| `main.cpp:7570` | 星乃一歌 |
| `main.cpp:7570` | 花里みのり |
| `main.cpp:7571` | 天馬司 |
| `main.cpp:7571` | 日野森雫 |
| `main.cpp:7571` | 朝比奈まふゆ |
| `main.cpp:7571` | 東雲絵名 |
| `main.cpp:7571` | 桃井愛莉 |
| `main.cpp:9096` | 关掉本机所有 CppSekai 窗口？ |
| `main.cpp:9097` | 结束所有实例 |
| `main.cpp:9827` | 请选择歌曲 |
| `main.cpp:9829` | 准备好的话就开始吧 |
| `main.cpp:9831` | 即将开始 |
| `main.cpp:9833` | 等待其他玩家 |
| `main.cpp:10155` | 没能打开谱面下载器：chartdl.exe 不在游戏目录里 |
| `main.cpp:10251` | 房主还没有定下本窗口能打的曲子 |
| `main.cpp:10255` | 先在手机上选择难度 |
| `main.cpp:10343` | 房主已开始，你未确定 |
| `main.cpp:10343` | 本曲已经开始 |
| `main.cpp:10355` | 本窗口没有这首曲子 |
| `main.cpp:10373` | 谱面加载失败 |
| `main.cpp:10586` | 房主已放弃本曲 |
| `main.cpp:11454` | 房主已暂停 |
| `main.cpp:11564` | 是否继续演出？ |
| `main.cpp:11565` | 放弃 |
| `main.cpp:11565` | 继续演出 |
| `main.cpp:11565` | 重试 |
| `main.cpp:11585` | 房主重开本曲 |
| `main.cpp:11839` | 关于本软件 |
| `main.cpp:11841` | CppSekai 是免费、开源、非营利的同人练习工具（AGPL-3.0）。它与 SEGA、Colorful Palette 以及《初音未来：缤纷舞台》官方没有任何关系，仅供本地学习与练习使用。一切权利归各自权利人所有。 |
| `main.cpp:11843` | 以后不再显示 |
| `main.cpp:11843` | 知道了 |
| `platform/Party.cpp:742` | 玩家 |
| `platform/Party.cpp:1026` | 玩家 |
