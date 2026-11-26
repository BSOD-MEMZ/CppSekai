# 选曲列表的滚动模型

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 选曲列表的滚动模型（2026-09-12 重写，动之前先读）

`game/SongSelect.cpp` 的列表**不是 ImGui 的滚动控件**，是自己写的状态机，因为官方 UI 的行为
（没有滚动条、滚不到底、滚动时不选中、停手后中间那首被选中）ImGui 给不了：

- 唯一的状态是 `scroll`：**位于列表视口垂直中线的那个内容坐标**。行位置 =
  `viewCenterY + (i * pitch - scroll)`，`pitch` 固定 `104*k`（**等间距**，选中卡片 128 高、
  普通行 84 高都画在自己的 slot 里，别再改回"选中行更高所以把后面的行往下推"——
  那样 `center(i)` 会随选中行变化，吸附目标会跳 22px）。
- 滚轮 / 拖动 / 惯性（`flingVel`，指数衰减）/ 吸附（`scrollTarget`，`1-exp(-16dt)` 逼近）
  全都只改 `scroll`；`scrolling=true` 期间 **`cardIndex = -1`，任何行都不高亮**，
  手机面板也保持旧曲目。停手 0.20s（或惯性衰减完）后把 `lround(scroll/pitch)` 那行提交为
  `groupIndex`（这才是真正的选中），再滑到正中间。选择只在"停手"时发生，这就是用户要的语义。
- 两端各允许 `0.9*pitch` 的橡皮筋过冲，回弹靠逼近 `scrollTarget` 完成（"滚不到底"）。
  → **2026-09-12 晚改成真循环**：列表是**闭环**的，滚过最后一行接的是第一行，没有端点也没有
  橡皮筋（用户说的"滚不到底"是这个意思）。实现上一个 `slot` 是无限行序列里的位置，取行内容
  一律 `rows[((slot % rowCount) + rowCount) % rowCount]`。因此**不要再给 `scroll` 加 clamp**，
  惯性只会因为指数衰减停下（`< 100px/s` 归零）。
- 行现在来自 `buildRows()`（`ListRow`：`header` 或 `song`）：排序 + 可选分组都在那里做，
  结果按 `listSignature`（搜索词|排序|分组|难度|谱面数）缓存，别每帧重建。
  分组开着时**顺序由分组决定**（`order` 会被覆盖），否则段标题会把同一组切成好几段。
- 循环 + 分组下"中间那行"可能是段标题，所以提交选中时要 `nearestSongSlot()` 跳过标题；
  `slotOfGroup()` 取离当前 `scroll` 最近的那一份拷贝（否则 shuffle 会绕整圈转）。
  高亮卡片只画 `cardSlot` 那一处，同一个组在屏幕上出现两次时别画两张卡。
- 鼠标和触摸走同一条路：SDL 的 touch→mouse 合成（`SDL_HINT_TOUCH_MOUSE_EVENTS=1`）会把单指
  拖拽变成 `ImGui` 眼中的鼠标拖动，所以只需要读 `io.MousePos` / `IsMouseDown`。
- **点击必须在松手时判定**，且位移 < 8px 才算点击（拖拽永远不选中）。不要再用
  `InvisibleButton` + `IsItemClicked`：那个在**按下**时就触发，拖拽起手会误选。
- 行的命中测试全部靠 `slotAtY(y)` 算，没有 ImGui item；列表用 `BeginChild` 只是为了拿裁剪矩形
  （`NoScrollbar | NoScrollWithMouse`）。
- 前导等级圆显示的是**当前选中难度**的定数（`levelForDifficulty()`）：该难度没有谱面文件时
  回落到官方 `music-levels.json` 表，所以切难度时整列数字会一起变，颜色也跟着变
  （`kDiffColors[diffIndex]`）。手机面板里未选中的难度是**空心圆**（无底色填充）。
- **手机面板的元数据块是左对齐的**（2026-09-14 按官方截图改）：曲名 / 歌手 / `Vo.` 三行
  统一从内容框左缘 `contentL = cx - panelW/2` 起排，和下面那排难度圆的左缘对齐
  （`addTextLeft()`；原来是 `addTextCentered()`）。右侧放**最好成绩的评级徽章**
  （`drawBestScoreBadge()`）：半透明白圆盘（实测原版约 32% 白）+ 游戏自带的
  `score/rank/chr/<x>.png` 字母（224x266，字形实心 185x242 在 +20+16，
  原版把字形画到徽章直径的 0.686）。直径 `64*k`（≈ 难度圆的 1.06 倍，量出来的），
  右缘贴内容框右缘、竖直居中于元数据块——原版就是压着歌手/`Vo.` 两行中间，
  而按整块居中算出来的落点和它只差几像素。**没打过**（`bestScore <= 0`）时画暗一档的圆盘 + `--`。
  徽章取的是**当前选中难度**那条谱面的最好成绩，`ChartEntry::bestScore` 由 `applyScores()`
  从 `userdata.json` 灌进来（`cleared`/`fullCombo` 之外新增的第三个字段）；
  等级用 `chartRatingFor()`，取值顺序**故意和 main.cpp 的 `setChartRating()` 一致**
  （官方等级表 → 谱面自带 level → 26），这样徽章和演奏中 HUD 的 SCORE RANK 永远一致。
  三行文字都用 `ellipsize()` 截到 `textMaxW = 内容框宽 - 徽章直径 - 12k`，绝不会钻到徽章底下。
  字号（2026-09-22 用户要求"歌手、作者信息字号加大点"）：曲名 `26k`、**作者 `20k`**（原 17k，
  行距 35k）、**`Vo.` 行 `18k`**（原 15k，行距 32k；超宽时仍按 `textMaxW` 先缩后截）。
- **底部两颗圆钮（随机 / 切换歌手）没有圆底**（2026-09-22 用户点名去掉）：
  `shufflebutton.png` / `singer.png` 直接画在手机面板上，悬停反馈只剩"图标放大 10% +
  白度从 226 提到 255"（多人成员那颗用 alpha 90 压暗）。别再往回加 `AddCircleFilled` 深色盘
  或薄荷描边环。
- **切换歌手面板**（2026-11-21 按 prefab 1960/1961 重做）：手机面板底部那颗圆形
  「切换歌手」按钮（`assets/select/singer.png`）打开右侧 `ChangeVocalSubWindowDialog`。
  要点：
  - 右侧子窗口 **956 宽、整窗高、贴右缘**（`vocalPW = min(956k, w*0.4979)`），
    背景 `#444466` / 80%；全屏再压一层 `#000022` / 50% 的 `Cover`。
  - **没有标题行**（prefab `header: null`），关闭 X 44×44，右上内缩 18.1/18.5。
  - 列表 828 宽（左右各 64），行 **828×164**，间距 12；卡片
    `bg_base_r20_wh` 色 **#DFDFEB**、圆角 **20**、**无描边**；
    单选盘 72、居中薄荷圆 36（`icon_radio_gn`）；名字 36px、成员 32px，
    文字左缘 = 卡左 + 102；锁定态才盖 `#000022` 50% + `icon_lock_wh`。
  - 标签仍走 `vocalVersionLabel()`；列表直接就是 `availableVocals()`，没有锁定卡。
  - 点空白处（`mouse.x < vocalPX0`）关闭。**2026-11-25 起它是独立的顶层窗口**
    （`drawSingerPanel`，在选曲窗口 `End()` 之后才开）：列表是 ImGui 子窗口，子窗口
    在父窗口之后合成，所以以前画在父窗口末尾的面板会被列表右缘的 2D/原曲徽章、
    选中行高亮盖住；独立窗口同时解决输入——ImGui 把 hover 给"先提交的那个 item"，
    以前顶栏菜单钮压着关闭 X，点 X 会同时选中第一张卡。现在 `HoveredWindow` 是面板
    自己。外面那一层的 `overlayUp` 拦截规则不变。
  - `vocalEase` / `vocalPW` / `vocalPX0` 仍在顶栏之前算；滚轮直接读
    `io.MouseWheel`，`PushClipRect` 裁剪。
  - 选择**按曲存**（`gVocalChoiceBySong`），手机 `Vo.` 行和主循环取音源读同一个
    `vocalIndex`。无头看它：`--singer-panel --select-id <id>`。
- **刷新按钮**（2026-09-14）：列表头部、两个 combobox 右边那颗深色胶囊（环形箭头 + “刷新”，
  图标是 `dl` 手画的弧 + 三角箭头，不需要素材），点了返回 `SelectRescan`，和 F5 走同一条路。
  悬停有 tooltip 写着 F5。注意**重扫之后 main.cpp 会把选中项重置成第一首**（F5 一直是这行为），
  要改成保留当前曲目得动 `main.cpp` 那处 `selected = entries.empty() ? -1 : 0`。
- **搜索框匹配顺序**：标题子串 → 作者子串 → **社区别名（精确匹配）** → 读音子串（原文）
  → 读音（把输入当罗马音折成假名）。别名表是 `music-aliases.json`（HarukiBot 的公开 API
  导出，700 首 / 1.3 万条，见工具一节），所以 `tyw`、`hs`、`mmj`、`梦开始的地方`、`mmj团歌`、
  `即刻轮回` 都能直接搜到。**别名必须精确匹配**：表里全是两字母词（`hs` / `kz` / `emu`），
  当子串用会一次点亮半张列表。chartdl 的曲库列表走同一张表、同一套顺序。
- **排序 / 分组**（搜索框右边的两个 combobox）：排序有「按名称」「按难度」，分组有「关闭」
  「按难度段（1-5 / 6-10 / … / 36+）」「按读音（あ/か/さ…/A-Z 逐字母/#）」「按首字
  （A-Z / 0-9 / あ い う…，用 initialLabel()）」。
  名称排序和读音分组用的是**官方读音**（`musics.json` 的 `pronunciation`，main.cpp 里
  `loadMusicMaster()` 载入，`foldForSort()` 把片假名折成平假名、ASCII 转小写），
  所以「ウミユリ海底譚」落在 あ 行、片假名标题也能正确排序；**日服曲的读音全是假名**
  （`Tell Your World` = てるゆあわーるど → た 行），所以逐字母段只对**没有读音的自制谱**
  生效（那时退回用标题当 key）。**例外是国服独占曲的中文标题，那里填的是拼音**
  （见工具一节）—— 它们会落进逐字母段，而且只有靠原文比对才搜得到。
  汉字开头且没有读音的会落进「その他」（按字节序排在最后）。
  `kanaRowLabel()` 是「行」级的标签（あ か さ た な は ま や ら わ + 逐字母 + `#`），
  `initialLabel()` 是「首字」级的（逐假名 + 逐字母）。
  注意 `#TITLE` 里写的是难度名（有些 unipjsk 导出写 `#TITLE "master"`）时要当空处理，
  否则列表里会出现一堆叫 "master" 的歌。
- **曲名回填**：unipjsk 导出的 SUS `#TITLE` 是空的，旧的回落是 `prettyFileName()`，于是整张
  列表都叫「0018 master」。现在 `musics.json` 的 `title` 也一起进主表（`storeTitle()` /
  `titleFor()`），`scanChartFolder()` 与 `resolveSidecars()` 的回落顺序是
  **SUS `#TITLE` → sidecar `title` → 官方 `title` → 文件名**。顺带选曲搜索框也能搜到真曲名了。
  这两个值**存在 settings 里**（`sortMode` / `groupMode`），`drawSongSelect` 收 `int&`，
  调用方（main.cpp）发现变了就 `persistUserData()`；分组模式会覆盖排序（见上）。
- **段标题可点 → 索引面板**（2026-09-13 加）：分组开着时点段标题把列表换成字母面板
  （`indexOpen` / `indexAnim`），点字母用 `nearestSlotOfRow()` 飞过去（落点是"该段第一首
  居中、标题在上一行"），点空白处恢复列表；面板打开时 `listHovered` 要按 `!indexOpen` 屏蔽，
  否则背后列表会跟着动，列表行还要按顶点 alpha 淡出（抓 `listVtxFirst` 之后批量缩）。分组关掉时面板自动关。
  **面板是极简的（没有底板/标题/关闭叉）**，交互全手写命中测试——这里踩过三个坑，改之前先看：
  1) **别用整块 InvisibleButton 当背景**：它在 mouse-down 就抢走 active id，之后提交的字母
     永远收不到点击。字母的命中测试要学列表行那样直接比坐标。
  2) **格子矩形必须半开区间**（`>= min && < max`）：两端都 `<=` 时正好落在格子边界上的点击会
     被相邻两个字母同时命中，一次点击跳两段。
  3) **用"按下那一刻面板是否已打开"（`indexOpenAtPress`）门控**：一次按下+抬起可能落在同一帧，
     列表的 release-commit（点标题开面板）会和面板的 press 处理同时吃这一次点击。
- **失血阴影（暗角）**（`main.cpp`，`damageVignette`/`deadVignette`）：**四条边带要内缩 +
  四个角方块用四色插值**（外角两侧是暗色、内角 0）。只内缩边带、不补角，角上就只剩横向渐变、
  没有上下方向的压暗，看上去像"阴影没绕窗口四周"（实测角区系数 0.96 = 几乎没压暗，补角后 0.73）。
  每像素只允许一层，别让边带互相重叠（会叠加两倍黑）。
- **入场/过渡动画**（2026-09-13）：`enterAnim` 靠"本函数每帧都被调用，隔 >0.5s 才又调一次
  = 刚进来"判定（不用宿主通知），手机面板的入场就是挂在既有的"手机顶点整体旋转"那趟循环里
  做的（位移 + 顶点 alpha），所以别在那里加 `continue` 之类的短路。选中卡片的高度按槽位
  （`slotHeights`）做指数趋近——**键是原始 `slot`，不是 `wrapSlot(slot)`**（2026-11-02 改）：
  列表是循环的，曲库比屏幕能放的槽位少时同一首歌会同时出现在多个槽位上，那些副本必须各自
  有独立的高度状态。以前共用 `wrapSlot` 索引，副本们每帧朝 128 / 84 互相拉扯，**选中卡片
  永远到不了 `cardH`（实测卡在 ~98/128）而且每帧在 97.88~98.33 之间来回**——这就是用户报的
  "选中选项一直在颤抖"。`slotHeights` 现在是个 `unordered_map<int,float>`，`listSignature`
  变化时 `clear()`，每帧在行循环之后把 `[firstSlot-4, lastSlot+4]` 之外的擦掉（槽位号会随着
  循环无限增长，不清就是个慢泄漏）。**别改回 `vector` + `wrapSlot`。**
- **选曲背景可以是桌面壁纸**（2026-09-13）：`bgStyle/bgBlur/bgDim` 三个设置项，壁纸路径见
  `windowsWallpaperPath()`（SPI_GETDESKWALLPAPER → 注册表 HKCU\Control Panel\Desktop\WallPaper
  → `%APPDATA%\Microsoft\Windows\Themes\TranscodedWallpaper`，都要 `GetFileAttributesW` 验存在，
  后两个兜 slideshow/Spotlight）。解码 + 降采样 + 三次 box 模糊都在
  `Renderer::loadBackdropTexture()`（1024 上限、滑动窗口 O(1)/像素），**只在设置开着时才加载**，
  纹理由 main.cpp 持有并通过 `game::setSelectBackdrop(tex,w,h,dim)` 交给 SongSelect 画
  （cover 铺满 + dim 黑罩）。模糊只在滑条松手时重算，拖动期间别重算（CPU pass）。


