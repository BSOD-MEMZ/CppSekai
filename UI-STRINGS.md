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
> 共 **547** 条，分 **87** 个区块。行号会随代码漂移，区块标题比行号可靠。
>
> ⚠️ 两块**不要翻译**：
> 1. `platform/ChartDlGuide.cpp` 的「全部完成 / 完成，失败 / 下载完成 / 已经下载完整」——
>    那是用来**匹配**下载器状态栏文字的，翻了就认不出进度。
> 2. `main.cpp` 的角色 / 团体名对照表——中日双名本来就内置，属于数据不是界面文案。

### 打开谱面 · 文件对话框

| 文件:行 | 原文 |
|---|---|
| `main.cpp:117` | SUS 谱面 (*.sus) |
| `main.cpp:118` | 所有文件 (*.*) |
| `main.cpp:127` | 打开 SUS 谱面 |

### 命令行 --help（面向高级用户，一般不进翻译）

| 文件:行 | 原文 |
|---|---|
| `main.cpp:609` | into the window (letterbox). Same as the 渲染模式 setting.\n |
| `main.cpp:616` | copy logs in as a different user. Same as 设置 -> 系统.\n |
| `main.cpp:617` | --party [--party-name <name>]: force 多人游玩 on for this run and join\n |
| `main.cpp:620` | host alone plays the BGM). Same as 设置 -> 系统 ->\n |
| `main.cpp:621` | 多人游玩.\n |
| `main.cpp:622` | --no-party: force 多人游玩 off for this run (plain single window),\n |
| `main.cpp:625` | 下载谱面 button from the log (a posted click cannot\n |
| `main.cpp:631` | the window: the host presses 确定 on whatever chart the\n |
| `main.cpp:633` | the phone panel and presses 确定.\n |
| `main.cpp:643` | saved 判定 > Flick 视作 Tap setting is not changed).\n |
| `main.cpp:648` | --confirm-flash [<sec>]: play the 确定 white burst in the song list (debug;\n |
| `main.cpp:651` | given page (0 演奏 / 1 画面 / 2 外观 / 3 判定 / 4 系统 / 5 账户 /\n |
| `main.cpp:652` | 6 关于).\n |
| `main.cpp:654` | --guess: open the 猜歌 alias quiz (the header button's card) at boot.\n |
| `main.cpp:655` | --singer-panel: open the 切换歌手 vocal-version panel (the phone panel's\n |
| `main.cpp:660` | produced; same switch as settings > 判定 > 触摸轨迹日志).\n |

### 启动失败 · OpenGL 版本过低

| 文件:行 | 原文 |
|---|---|
| `main.cpp:2341` | CppSekai 需要 OpenGL 3.3，而当前上下文只提供了 %d.%d。\n\n |
| `main.cpp:2342` | 最常见的原因：显卡驱动没装（Windows 只给了「Microsoft 基本显示适配器」）， |
| `main.cpp:2343` | 或者在云主机 / 远程桌面 / 没有 3D 加速的虚拟机里运行。\n\n |
| `main.cpp:2344` | 如果这台机器确实没有能用的显卡驱动，可以到发布页下载「无显卡补丁」 |
| `main.cpp:2345` | （Mesa 软件渲染），解压到本程序所在的文件夹再启动。\n |
| `main.cpp:2346` | 具体信息见程序目录下的 cppsekai.log。 |

### 启动失败 · OpenGL 初始化失败

| 文件:行 | 原文 |
|---|---|
| `main.cpp:2421` | OpenGL 初始化失败：无法加载 3.3 的函数入口点。\n\n |
| `main.cpp:2422` | 这台机器的 OpenGL 驱动不完整，多半是没装显卡驱动， |
| `main.cpp:2423` | 或者在云主机 / 远程桌面 / 没有 3D 加速的虚拟机里运行。\n\n |
| `main.cpp:2424` | 没有可用显卡驱动的机器，可以到发布页下载「无显卡补丁」 |
| `main.cpp:2425` | （Mesa 软件渲染），解压到本程序所在的文件夹再启动。 |

### 窗口标题 · 谱面下载器

| 文件:行 | 原文 |
|---|---|
| `main.cpp:3519` | CppSekai 谱面下载器 |
| `main.cpp:3521` | CppSekai 谱面下载器 |

### 多人游玩 · 暂停提示

| 文件:行 | 原文 |
|---|---|
| `main.cpp:3835` | 多人游玩中：暂停由房主控制 |

### 谱面加载失败

| 文件:行 | 原文 |
|---|---|
| `main.cpp:4070` | 谱面加载失败，文件可能损坏或不是 SUS 谱面： |

### 未设置昵称

| 文件:行 | 原文 |
|---|---|
| `main.cpp:4544` | (无昵称) |

### 歌手版本标签

| 文件:行 | 原文 |
|---|---|
| `main.cpp:4616` | 虚拟歌手 ver |
| `main.cpp:4621` | 指定歌手 |
| `main.cpp:4624` | 指定歌手 |
| `main.cpp:4626` | セカイver |

### 设置 · 页签名

| 文件:行 | 原文 |
|---|---|
| `main.cpp:4693` | 演奏 |
| `main.cpp:4693` | 画面 |
| `main.cpp:4693` | 外观 |
| `main.cpp:4693` | 判定 |
| `main.cpp:4694` | 系统 |
| `main.cpp:4694` | 账户 |
| `main.cpp:4694` | 关于 |

### 设置 · 演奏 / 画面 / 外观

| 文件:行 | 原文 |
|---|---|
| `main.cpp:4811` | 音量 |
| `main.cpp:4813` | BGM 音量 |
| `main.cpp:4824` | 音效音量 |
| `main.cpp:4837` | 手感 |
| `main.cpp:4839` | 音频偏移 |
| `main.cpp:4846` | 音符速度 |
| `main.cpp:4861` | 奖励 |
| `main.cpp:4864` | 演出能量（经验 ×%.0f） |
| `main.cpp:4875` | 玩法 |
| `main.cpp:4877` | 默认歌手 |
| `main.cpp:4892` | 自动演出 |
| `main.cpp:4906` | 无 MV |
| `main.cpp:4906` | 原曲 MV |
| `main.cpp:4921` | 输入 |
| `main.cpp:4923` | 按键映射 |
| `main.cpp:4937` | 按键映射　%s |
| `main.cpp:4955` | 手柄 |
| `main.cpp:4957` | 手柄震动 |
| `main.cpp:4968` | 震动 |
| `main.cpp:4973` | 命中 Flick 时震动 |
| `main.cpp:4980` | 命中绝赞（黄键）时震动 |
| `main.cpp:4987` | MISS 时震动 |
| `main.cpp:5034` | 界面与渲染 |
| `main.cpp:5039` | 界面缩放 |
| `main.cpp:5050` | 渲染模式 |
| `main.cpp:5056` | 跟随窗口分辨率 |
| `main.cpp:5056` | 固定分辨率 |
| `main.cpp:5071` | 性能 |
| `main.cpp:5073` | 帧率上限 |
| `main.cpp:5081` | 显示帧率 |
| `main.cpp:5087` | 实测 %.0f fps · %d Hz 关垂直同步 |
| `main.cpp:5092` | 窗口 |
| `main.cpp:5094` | 分辨率 |
| `main.cpp:5099` | 自定义… |
| `main.cpp:5133` | 窗口模式 |
| `main.cpp:5159` | 原生菜单栏 |
| `main.cpp:5175` | 界面 |
| `main.cpp:5178` | 显示播放进度条 |
| `main.cpp:5189` | 经典开屏画面 |
| `main.cpp:5197` | 打击特效 |
| `main.cpp:5207` | 弱化打击特效 |
| `main.cpp:5217` | 背景 |
| `main.cpp:5222` | 选曲背景 |
| `main.cpp:5228` | 默认渐变 |
| `main.cpp:5228` | 桌面壁纸 |
| `main.cpp:5228` | Aero 玻璃 |
| `main.cpp:5229` | 默认渐变 |
| `main.cpp:5229` | 桌面壁纸 |
| `main.cpp:5253` | 背景模糊 |
| `main.cpp:5260` | 背景模糊 |
| `main.cpp:5270` | 背景变暗 |
| `main.cpp:5273` | 背景变暗 |
| `main.cpp:5286` | 窗口玻璃 |
| `main.cpp:5289` | 玻璃实现 |
| `main.cpp:5296` | 无边框窗口 |

### 设置 · 判定 / 系统

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5368` | 判定窗口 (ms) |
| `main.cpp:5400` | Bad 与 Miss 同步 |
| `main.cpp:5421` | 长条容错 (ms) |
| `main.cpp:5435` | 松手容错 %.0f |
| `main.cpp:5437` | 起按容错 %.0f |
| `main.cpp:5450` | 严格 Flick 方向 |
| `main.cpp:5457` | Flick 视作 Tap |
| `main.cpp:5470` | 触摸轨迹日志 |
| `main.cpp:5483` | 生命 / 综合力 |
| `main.cpp:5549` | 窗口行为 |
| `main.cpp:5552` | 失焦时自动暂停 |
| `main.cpp:5559` | 最小化窗口静音 |
| `main.cpp:5570` | 系统集成 |
| `main.cpp:5573` | 启用 SMTC 汇报 |
| `main.cpp:5591` | 关联 .sus 谱面文件 |
| `main.cpp:5608` | 开始游戏时切到英文输入法 |
| `main.cpp:5618` | 隐藏系统触摸特效 |
| `main.cpp:5630` | 多实例 |
| `main.cpp:5632` | 多开 |
| `main.cpp:5636` | 只允许一个实例 |
| `main.cpp:5636` | 允许多开，新实例登录另一个用户 |
| `main.cpp:5666` | 多人演出 |
| `main.cpp:5698` | 多人游玩已开启，重开所有窗口生效。 |

### 设置 · 诊断（日志分类 / 结束实例）

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5704` | 诊断 |
| `main.cpp:5714` | 日志输出　%d/%d 类 |
| `main.cpp:5726` | 结束当前实例 |
| `main.cpp:5734` | 确认结束？ |
| `main.cpp:5746` | 结束所有实例 |

### 设置 · 账户（用户 / 导入导出）

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5818` | 当前 |
| `main.cpp:5842` | 新建用户 |
| `main.cpp:5843` | 用户 2 |
| `main.cpp:5845` | 用户 |
| `main.cpp:5877` | 确认删除？ |
| `main.cpp:5905` | 删除用户 |
| `main.cpp:5912` | 导出 |
| `main.cpp:5930` | 已导出： |
| `main.cpp:5938` | 导入 |
| `main.cpp:5950` | (无昵称) |
| `main.cpp:5956` | 不是用户数据文件： |

### 设置 · 关于

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5978` | 资料 |
| `main.cpp:5980` | 昵称 |
| `main.cpp:6008` | 学校 / 组织 |
| `main.cpp:6022` | 等级 |
| `main.cpp:6022` | 本级经验 |
| `main.cpp:6026` | 签名 |
| `main.cpp:6048` | 版本 1.0.0.7 · 许可 AGPL-3.0-only |
| `main.cpp:6054` | 策划 / 开发　xxt8582753（BSOD-MEMZ） |
| `main.cpp:6056` | 测试　Shitakara4banme |
| `main.cpp:6058` | 株洲市南方中学 · 2026 年 9 月 |
| `main.cpp:6073` | 访问 xxtsoft |
| `main.cpp:6077` | 许可与免责声明 |
| `main.cpp:6085` | 重播新手教学 |
| `main.cpp:6109` | 本项目免费、开源、非营利，与 SEGA、Colorful Palette 及《初音未来： |
| `main.cpp:6110` | 缤纷舞台》官方没有任何关系，仅供本地学习与练习使用。 |
| `main.cpp:6111` | 全部曲名、角色、美术与音频的权利归各自权利人所有。 |

### 键位设置 / 多开确认

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6160` | 确定 |
| `main.cpp:6222` | 按键映射 |
| `main.cpp:6240` | %d 按新键… |
| `main.cpp:6256` | 恢复默认键位 |
| `main.cpp:6263` | 确定 |
| `main.cpp:6309` | 默认歌手 |
| `main.cpp:6324` | セカイver |
| `main.cpp:6329` | 虚拟歌手 ver |
| `main.cpp:6334` | 指定歌手 |
| `main.cpp:6389` | 确定 |
| `main.cpp:6424` | 日志输出 |
| `main.cpp:6455` | 全部启用 |
| `main.cpp:6461` | 全部关闭 |
| `main.cpp:6467` | 确定 |
| `main.cpp:6507` | 开启多开？ |
| `main.cpp:6509` | 多人演出是实验性功能：同一台机器开几个窗口，每个窗口各登录一个用户，窗口多了可能掉帧或时钟漂移，并且存在大量已知 Bug，确定开启吗？ |
| `main.cpp:6511` | 取消 |
| `main.cpp:6511` | 确定开启 |

### 退出确认

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6557` | 要退出吗？ |
| `main.cpp:6559` | 现在退出不会保存本局的成绩。 |
| `main.cpp:6561` | 继续演出 |
| `main.cpp:6561` | 退出 |

### 删除歌曲 · 文件统计

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6697` | 将删除 |
| `main.cpp:6697` | 个文件： |
| `main.cpp:6698` | 个谱面 + |
| `main.cpp:6698` | 张曲绘 + |
| `main.cpp:6699` | 个音频 |
| `main.cpp:6701` | 个元数据 |

### 删除歌曲确认

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6746` | 删除这首歌？ |
| `main.cpp:6748` | 歌曲： |
| `main.cpp:6750` | 位置： |
| `main.cpp:6751` | 本操作不可逆！ |
| `main.cpp:6753` | 取消 |
| `main.cpp:6753` | 删除 |

### 导入用户数据

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6815` | 导入用户数据？ |
| `main.cpp:6817` | 文件： |
| `main.cpp:6818` | 用户： |
| `main.cpp:6818` | ，成绩 |
| `main.cpp:6818` | 条 |
| `main.cpp:6819` | 导入会用这份数据覆盖当前用户（ |
| `main.cpp:6819` | ）的设置、成绩和资料，无法撤销。 |
| `main.cpp:6821` | 取消 |
| `main.cpp:6821` | 覆盖导入 |
| `main.cpp:6841` | 导入失败： |
| `main.cpp:6866` | 已导入： |

### 多人游玩已开启 · 重启提示

| 文件:行 | 原文 |
|---|---|
| `main.cpp:6888` | 多人游玩已开启 |
| `main.cpp:6890` | 现在这个窗口还在单人模式里，立即重启窗口来准备多人游玩。 |
| `main.cpp:6892` | 稍后 |
| `main.cpp:6892` | 立即重启 |

### 关闭所有实例确认

| 文件:行 | 原文 |
|---|---|
| `main.cpp:8774` | 关掉本机所有 CppSekai 窗口？ |
| `main.cpp:8775` | 结束所有实例 |

### 多人 · 等待 / 选曲状态

| 文件:行 | 原文 |
|---|---|
| `main.cpp:9503` | 请选择歌曲 |
| `main.cpp:9505` | 准备好的话就开始吧 |
| `main.cpp:9507` | 即将开始 |
| `main.cpp:9509` | 等待其他玩家 |
| `main.cpp:9512` | 等待房主选曲 |
| `main.cpp:9516` | 本窗口没有 |
| `main.cpp:9517` | ，无法参加本曲 |
| `main.cpp:9519` | 请选择难度 |
| `main.cpp:9521` | 准备好的话就开始吧 |
| `main.cpp:9523` | 等待其他玩家 |

### 谱面下载器缺失

| 文件:行 | 原文 |
|---|---|
| `main.cpp:9760` | 没能打开谱面下载器：chartdl.exe 不在游戏目录里 |

### 多人 · 房主状态 / 加载失败

| 文件:行 | 原文 |
|---|---|
| `main.cpp:9841` | 房主还没有定下本窗口能打的曲子 |
| `main.cpp:9845` | 先在手机上选择难度 |
| `main.cpp:9931` | 本曲已经开始 |
| `main.cpp:9931` | 房主已开始，你未确定 |
| `main.cpp:9943` | 本窗口没有这首曲子 |
| `main.cpp:9960` | 谱面加载失败 |

### 多人 · 房主放弃

| 文件:行 | 原文 |
|---|---|
| `main.cpp:10188` | 房主已放弃本曲 |

### 多人 · 房主暂停

| 文件:行 | 原文 |
|---|---|
| `main.cpp:11032` | 房主已暂停 |

### 多人 · 继续 / 放弃 / 重试

| 文件:行 | 原文 |
|---|---|
| `main.cpp:11142` | 是否继续演出？ |
| `main.cpp:11143` | 重试 |
| `main.cpp:11143` | 放弃 |
| `main.cpp:11143` | 继续演出 |
| `main.cpp:11163` | 房主重开本曲 |

### 关于本软件

| 文件:行 | 原文 |
|---|---|
| `main.cpp:11396` | 关于本软件 |
| `main.cpp:11398` | CppSekai 是免费、开源、非营利的同人练习工具（AGPL-3.0）。它与 SEGA、Colorful Palette 以及《初音未来：缤纷舞台》官方没有任何关系，仅供本地学习与练习使用。一切权利归各自权利人所有。 |
| `main.cpp:11400` | 以后不再显示 |
| `main.cpp:11400` | 知道了 |

### 选曲 · 版本标签

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:506` | 「世界」ver. |
| `game/SongSelect.cpp:509` | 虚拟歌手ver. |

### 选曲 · 默认用户

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:1086` | 默认用户 |

### 选曲 · 五十音 / 分组标签

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:1900` | その他 |
| `game/SongSelect.cpp:1915` | その他 |
| `game/SongSelect.cpp:1928` | その他 |
| `game/SongSelect.cpp:1941` | あ |
| `game/SongSelect.cpp:1941` | か |
| `game/SongSelect.cpp:1941` | さ |
| `game/SongSelect.cpp:1941` | た |
| `game/SongSelect.cpp:1942` | な |
| `game/SongSelect.cpp:1942` | は |
| `game/SongSelect.cpp:1942` | ま |
| `game/SongSelect.cpp:1942` | や |
| `game/SongSelect.cpp:1943` | ら |
| `game/SongSelect.cpp:1943` | わ |
| `game/SongSelect.cpp:1945` | その他 |
| `game/SongSelect.cpp:1955` | あ |
| `game/SongSelect.cpp:1958` | た |
| `game/SongSelect.cpp:1961` | や |
| `game/SongSelect.cpp:1965` | その他 |

### 选曲 · 分组「其他」

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:2069` | 收藏 |
| `game/SongSelect.cpp:2069` | 其他 |

### 选曲 · 版本分类

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:2920` | セカイ |
| `game/SongSelect.cpp:2923` | バーチャル |
| `game/SongSelect.cpp:2931` | 他 |

### 选曲 · 搜索框 / 右键菜单

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:3600` | 搜索 |
| `game/SongSelect.cpp:3629` | 剪切 |
| `game/SongSelect.cpp:3634` | 复制 |
| `game/SongSelect.cpp:3637` | 粘贴 |
| `game/SongSelect.cpp:3650` | 清空 |

### 选曲 · 排序 / 分组下拉

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:3688` | 按名称 |
| `game/SongSelect.cpp:3688` | 按难度 |
| `game/SongSelect.cpp:3689` | 关闭 |
| `game/SongSelect.cpp:3689` | 按难度段 |
| `game/SongSelect.cpp:3689` | 按读音 |
| `game/SongSelect.cpp:3689` | 按首字 |
| `game/SongSelect.cpp:3689` | 按收藏 |
| `game/SongSelect.cpp:3690` | 排序： |
| `game/SongSelect.cpp:3691` | 分组： |

### 选曲 · 顶栏按钮

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:3789` | 刷新 |
| `game/SongSelect.cpp:3793` | 音乐商店 |
| `game/SongSelect.cpp:3798` | 猜歌 |
| `game/SongSelect.cpp:3813` | 设置 |

### 选曲 · 收藏 / 删除谱面

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:4123` | 取消收藏 |
| `game/SongSelect.cpp:4123` | 加入收藏夹 |
| `game/SongSelect.cpp:4137` | 删除谱面文件… |

### 选曲 · 空列表 / 下载谱面

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:4270` | 没有找到谱面。把 .sus 放到 charts/ 目录下，再按 F5 重新扫描 |
| `game/SongSelect.cpp:4278` | 下载谱面 |
| `game/SongSelect.cpp:4286` | 这会跳转到音乐商店。 |

### 选曲 · 切换歌手

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:5344` | 切换歌手 |

### 选曲 · 无其他演唱版本

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:5512` | 暂无其他演唱版本 |

### 选曲 · 个人资料卡

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:5567` | 个人资料 |
| `game/SongSelect.cpp:5573` | 昵称 |
| `game/SongSelect.cpp:5576` | （未设置） |
| `game/SongSelect.cpp:5578` | 学校 / 组织 |
| `game/SongSelect.cpp:5581` | （未设置） |
| `game/SongSelect.cpp:5584` | 签名 |
| `game/SongSelect.cpp:5596` | %d 次   平均 %.0f |
| `game/SongSelect.cpp:5600` | 等级 |
| `game/SongSelect.cpp:5601` | 游玩 / 平均分 |
| `game/SongSelect.cpp:5617` | 到下一级 %d / %d EXP |

### 选曲 · 猜歌

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:5670` | 猜歌 |
| `game/SongSelect.cpp:5678` | 正在准备题库… |
| `game/SongSelect.cpp:5678` | 没有可用的题库 |
| `game/SongSelect.cpp:5682` | 首次加载请耐心等待。 |
| `game/SongSelect.cpp:5683` | 找不到 music-aliases.json 或别名表过小。 |
| `game/SongSelect.cpp:5700` | 关闭 |
| `game/SongSelect.cpp:5706` | 这个别名指的是哪首歌？ |
| `game/SongSelect.cpp:5797` | 答对了！ |
| `game/SongSelect.cpp:5798` | 答错了，正确答案是「 |
| `game/SongSelect.cpp:5801` | 答对 %d / %d    连对 %d（最佳 %d） |
| `game/SongSelect.cpp:5814` | 下一题 |
| `game/SongSelect.cpp:5814` | 跳过 |
| `game/SongSelect.cpp:5818` | 关闭 |

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

### 多人 · 玩家名条

| 文件:行 | 原文 |
|---|---|
| `game/PartyScreen.cpp:170` | 多人演出 |
| `game/PartyScreen.cpp:178` | (房主) |
| `game/PartyScreen.cpp:181` | (你) |

### 结算 · 分数 / 最高分

| 文件:行 | 原文 |
|---|---|
| `game/Result.cpp:1081` | 得分 |
| `game/Result.cpp:1087` | 新纪录! |
| `game/Result.cpp:1099` | 最高得分 |

### 结算 · 歌曲等级

| 文件:行 | 原文 |
|---|---|
| `game/Result.cpp:996` | 歌曲等级 |

### 结算 · 升级 / 继续

| 文件:行 | 原文 |
|---|---|
| `game/Result.cpp:1227` | 等级提升!  +%d EXP |
| `game/Result.cpp:1265` | 继续 |

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

### 通用控件 · 等级

| 文件:行 | 原文 |
|---|---|
| `game/Ui.cpp:2015` | 等级 |

### 选曲 · 曲目信息

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:4410` | 原曲 |
| `game/SongSelect.cpp:4457` | 歌曲等级 |

### 选曲 · 确定

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:5114` | 确定 |
| `game/SongSelect.cpp:5120` | 确定 |

### 选曲 · 关闭

| 文件:行 | 原文 |
|---|---|
| `game/SongSelect.cpp:5626` | 关闭 |

### 导入 · 用户

| 文件:行 | 原文 |
|---|---|
| `main.cpp:1931` | 用户 |

### 导入 · 玩家

| 文件:行 | 原文 |
|---|---|
| `main.cpp:1990` | 玩家 |

### 选曲 · 收藏 / 最近播放

| 文件:行 | 原文 |
|---|---|
| `main.cpp:3082` | 收藏 |
| `main.cpp:3085` | 最近播放 |

### 设置 · 判定预设名

| 文件:行 | 原文 |
|---|---|
| `main.cpp:4272` | 宽松 |
| `main.cpp:4272` | 标准 |
| `main.cpp:4272` | 严格 |
| `main.cpp:4280` | 宽容 |
| `main.cpp:4280` | 较紧 |
| `main.cpp:4280` | 严格 |

### 打开用户数据 · 文件对话框

| 文件:行 | 原文 |
|---|---|
| `main.cpp:4380` | 用户数据 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0 |

### 角色 / 团体名对照表（数据，中日双名已内置，不需翻译）

| 文件:行 | 原文 |
|---|---|
| `main.cpp:4569` | 星乃一歌 |
| `main.cpp:4569` | 星乃一歌 |
| `main.cpp:4569` | 天馬咲希 |
| `main.cpp:4569` | 天马咲希 |
| `main.cpp:4570` | 望月穂波 |
| `main.cpp:4570` | 望月穗波 |
| `main.cpp:4570` | 日野森志歩 |
| `main.cpp:4570` | 日野森志步 |
| `main.cpp:4571` | 花里みのり |
| `main.cpp:4571` | 花里实乃里 |
| `main.cpp:4571` | 桐谷遥 |
| `main.cpp:4571` | 桐谷遥 |
| `main.cpp:4572` | 桃井愛莉 |
| `main.cpp:4572` | 桃井爱莉 |
| `main.cpp:4572` | 日野森雫 |
| `main.cpp:4572` | 日野森雫 |
| `main.cpp:4573` | 小豆沢こはね |
| `main.cpp:4573` | 小豆泽心羽 |
| `main.cpp:4573` | 白石杏 |
| `main.cpp:4573` | 白石杏 |
| `main.cpp:4574` | 東雲彰人 |
| `main.cpp:4574` | 东云彰人 |
| `main.cpp:4574` | 青柳冬弥 |
| `main.cpp:4574` | 青柳冬弥 |
| `main.cpp:4575` | 天馬司 |
| `main.cpp:4575` | 天马司 |
| `main.cpp:4575` | 鳳えむ |
| `main.cpp:4575` | 凤笑梦 |
| `main.cpp:4576` | 草薙寧々 |
| `main.cpp:4576` | 草薙宁宁 |
| `main.cpp:4576` | 神代類 |
| `main.cpp:4576` | 神代类 |
| `main.cpp:4577` | 宵崎奏 |
| `main.cpp:4577` | 宵崎奏 |
| `main.cpp:4577` | 朝比奈まふゆ |
| `main.cpp:4577` | 朝比奈真冬 |
| `main.cpp:4578` | 東雲絵名 |
| `main.cpp:4578` | 东云绘名 |
| `main.cpp:4578` | 暁山瑞希 |
| `main.cpp:4578` | 晓山瑞希 |
| `main.cpp:4579` | 初音ミク |
| `main.cpp:4579` | 初音未来 |
| `main.cpp:4579` | 鏡音リン |
| `main.cpp:4579` | 镜音铃 |
| `main.cpp:4580` | 鏡音レン |
| `main.cpp:4580` | 镜音连 |
| `main.cpp:4580` | 巡音ルカ |
| `main.cpp:4580` | 巡音流歌 |
| `main.cpp:4585` | Wonderlands×Showtime |
| `main.cpp:4585` | 25時、ナイトコードで。 |
| `main.cpp:4585` | 虚拟歌手 |
| `main.cpp:4593` | 系统（启动/窗口/菜单） |
| `main.cpp:4594` | 选曲（界面/下载/猜歌） |
| `main.cpp:4595` | 剧情（教学/脚本） |
| `main.cpp:4596` | 演奏（判定/暂停/结算） |
| `main.cpp:4597` | 音频（播放/音效） |
| `main.cpp:4598` | 输入（手柄/键盘/触摸） |
| `main.cpp:4599` | 渲染（OpenGL/帧统计） |
| `main.cpp:4600` | 多人游玩（房间同步） |

### 设置 · 账户 / 新建用户

| 文件:行 | 原文 |
|---|---|
| `main.cpp:5781` | 用户 |

### 多人 · 准备中 / 加载中

| 文件:行 | 原文 |
|---|---|
| `main.cpp:9533` | 准备中… |
| `main.cpp:9533` | 谱面加载中… |
| `main.cpp:9534` | 即将开始 |
| `main.cpp:9534` | 谱面加载中… |

### 多人 · 状态提示

| 文件:行 | 原文 |
|---|---|
| `main.cpp:10119` | 等待其他玩家 |
| `main.cpp:10119` | 准备好的话就开始吧 |
| `main.cpp:10121` | 等待其他玩家 |
| `main.cpp:10123` | 请选择难度 |

### 多人 · 玩家

| 文件:行 | 原文 |
|---|---|
| `platform/Party.cpp:668` | 玩家 |

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
| `downloader/chartdl.cpp:1217` | [--delete-selected]       press 删除文件 on that row, with no\n |

### 下载器 · 列表列名

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:1482` | 曲名 |
| `downloader/chartdl.cpp:1482` | 读音 |
| `downloader/chartdl.cpp:1482` | 演唱版本 |
| `downloader/chartdl.cpp:1483` | 已下载 |

### 下载器 · 窗口标题

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2190` | CppSekai 谱面下载器 |

### 下载器 · 详情面板

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:3181` | （在左边选一首歌） |
| `downloader/chartdl.cpp:3294` | 曲绘 |
| `downloader/chartdl.cpp:3297` | 元数据 (sidecar json) |

### 下载器 · 窗口标题

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:3521` | CppSekai 谱面下载器 |

### 下载器 · 剩余时间

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:1183` | 不到 1 秒 |
| `downloader/chartdl.cpp:1188` | %d 分 %d 秒 |
| `downloader/chartdl.cpp:1190` | %d 秒 |

### 下载器 · 列表列

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:1721` | 版本 |
| `downloader/chartdl.cpp:1729` | 已下载 |
| `downloader/chartdl.cpp:1731` | 缺 MV |
| `downloader/chartdl.cpp:1733` | 部分 |

### 下载器 · 删除结果

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:1909` | 个文件（ |
| `downloader/chartdl.cpp:1912` | ，失败 |
| `downloader/chartdl.cpp:1912` | 个（可能被占用） |

### 下载器 · 设置页 / 致谢

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2264` | 关闭窗口时 |
| `downloader/chartdl.cpp:2265` | 退出程序 |
| `downloader/chartdl.cpp:2267` | 隐藏到托盘（后台继续下载） |
| `downloader/chartdl.cpp:2269` | 提醒 |
| `downloader/chartdl.cpp:2270` | 下载完成后弹出气泡提醒 |
| `downloader/chartdl.cpp:2272` | 最小化时收进托盘 |
| `downloader/chartdl.cpp:2283` | 数据来源与致谢 |
| `downloader/chartdl.cpp:2284` | 谱面 / BGM / 曲绘：unipjsk（assets.unipjsk.com） |
| `downloader/chartdl.cpp:2285` | 国服独占曲目：sekai.best（storage.sekai.best） |
| `downloader/chartdl.cpp:2286` | 2D MV（.usm）：exmeaning（storage.exmeaning.com） |
| `downloader/chartdl.cpp:2287` | 感谢以上镜像站，以及 sekai-world 的 master 镜像。 |
| `downloader/chartdl.cpp:2289` | 确定 |
| `downloader/chartdl.cpp:2290` | 取消 |

### 下载器 · 主窗口控件

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2373` | 下载器设置 |
| `downloader/chartdl.cpp:2430` | 输出目录 |
| `downloader/chartdl.cpp:2432` | 浏览… |
| `downloader/chartdl.cpp:2433` | 打开目录 |
| `downloader/chartdl.cpp:2435` | 搜索 |
| `downloader/chartdl.cpp:2437` | 下载勾选的歌曲 |
| `downloader/chartdl.cpp:2438` | 全选 / 全不选 |
| `downloader/chartdl.cpp:2439` | 取消 |
| `downloader/chartdl.cpp:2442` | 删除文件 |
| `downloader/chartdl.cpp:2450` | 不含 MV |
| `downloader/chartdl.cpp:2451` | 原曲 MV |
| `downloader/chartdl.cpp:2452` | セカイ MV |
| `downloader/chartdl.cpp:2453` | 两个都要 |
| `downloader/chartdl.cpp:2455` | 设置… |
| `downloader/chartdl.cpp:2477` | 下载内容 |
| `downloader/chartdl.cpp:2506` | （在左边选一首歌） |
| `downloader/chartdl.cpp:2529` | 曲绘 |
| `downloader/chartdl.cpp:2530` | 元数据 (sidecar json) |
| `downloader/chartdl.cpp:2539` | 别名 |
| `downloader/chartdl.cpp:2552` | 别名 |
| `downloader/chartdl.cpp:2561` | 就绪 |

### 下载器 · 托盘 / 气泡

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2646` | 显示主窗口 |
| `downloader/chartdl.cpp:2648` | 退出 |
| `downloader/chartdl.cpp:2677` | CppSekai 谱面下载器 |
| `downloader/chartdl.cpp:2678` | 已隐藏到托盘，下载会继续。双击托盘图标可以恢复窗口。 |
| `downloader/chartdl.cpp:2692` | 选择谱面输出目录 |

### 下载器 · 删除确认 / 提示

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2745` | 正在取消… |
| `downloader/chartdl.cpp:2758` | 删除《 |
| `downloader/chartdl.cpp:2759` | 》在本地已下载的文件？\n\n |
| `downloader/chartdl.cpp:2760` | 谱面 / 音频 / 曲绘 / 2D MV / 元数据都会删掉，游戏里这首歌会回到未下载状态。\n |
| `downloader/chartdl.cpp:2761` | 目录： |
| `downloader/chartdl.cpp:2762` | CppSekai 谱面下载器 |
| `downloader/chartdl.cpp:2764` | 已取消删除 |
| `downloader/chartdl.cpp:2798` | 右边至少要勾一样东西（难度 / 演唱版本 / 曲绘 / 元数据）。 |
| `downloader/chartdl.cpp:2799` | CppSekai 谱面下载器 |

### 下载器 · 下载队列状态

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2842` | 勾选的歌曲都已经下载过了，没有需要下载的文件。 |
| `downloader/chartdl.cpp:2843` | 没有勾选任何歌曲。 |
| `downloader/chartdl.cpp:2844` | CppSekai 谱面下载器 |
| `downloader/chartdl.cpp:2851` | 开始下载… |
| `downloader/chartdl.cpp:2852` | 首歌曲，共 |
| `downloader/chartdl.cpp:2853` | 个文件 === |
| `downloader/chartdl.cpp:2855` | 首已下载完整，已跳过 |

### 下载器 · 已下载完整

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:2941` | 这首歌已经下载完整了 |

### 下载器 · 进度 / 完成

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:3030` | 剩余 |
| `downloader/chartdl.cpp:3033` | 下载中 |
| `downloader/chartdl.cpp:3034` | 个文件 |
| `downloader/chartdl.cpp:3040` | 全部完成 |
| `downloader/chartdl.cpp:3040` | 完成，失败 |
| `downloader/chartdl.cpp:3040` | 个 |
| `downloader/chartdl.cpp:3061` | 成功 %zu，跳过 %zu，失败 %zu |
| `downloader/chartdl.cpp:3065` | 下载完成 |
| `downloader/chartdl.cpp:3065` | 下载完成（有失败） |

### 下载器 · 详情面板

| 文件:行 | 原文 |
|---|---|
| `downloader/chartdl.cpp:3320` | （无） |
| `downloader/chartdl.cpp:3323` | 已下载 |
| `downloader/chartdl.cpp:3353` | 已下载 |
| `downloader/chartdl.cpp:3366` | （没有演唱版本数据） |
| `downloader/chartdl.cpp:3384` | 曲绘  ✓已下载 |
| `downloader/chartdl.cpp:3384` | 曲绘 |
| `downloader/chartdl.cpp:3390` | 元数据 (sidecar json)  ✓已下载 |
| `downloader/chartdl.cpp:3390` | 元数据 (sidecar json) |
