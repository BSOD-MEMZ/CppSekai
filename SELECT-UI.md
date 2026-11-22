# SELECT-UI.md — 选曲界面对齐规格（2026-10-25，2026-11-01 大修）

> 这份文档是「照着 pjsk 原作把选曲界面对齐」的**唯一依据**：原作实测数据、换算基准、
> 我们的现状、差异、可选方案、验收方法。
> 数据来自日服 7.1.0 解包（工具与产物路径见 [RE.md](RE.md)），不是目测。
>
> **⚠ 2026-11-01 更正**：下面第二、三节原来按「画布 1334×750、换算 ×1.44」写的，
> 那个基准是**错的**。原作 `CanvasScaler` 实测就是 **1920×1080**（见 §2），
> prefab 里的数字**本来就是 1920 画布的像素**，不需要任何换算。
> §3 表里「→ ×1.44」那一列、§4 表里「原作折算」那一列**全部作废**，以左列原值 +
> 新增的 §3.6（手机面板）为准。

## 0. 一句话结论

**零件之间的比例基本对，但整体小了一半左右，而且右半屏是空的**：
列表宽 450 vs 原作 912（**0.49×**）、行高 84 vs 144（0.58×）、等级球 48 vs 96（0.5×）、
通关指示 18 vs 38（0.47×）；手机面板 562×1056 vs 原作 754×1417（**0.745×**），
且原作的手机**左边缘只离列表 21px**，我们离了 728px。

> 2026-11-01 曾按方案 B 做到 1:1 并提交（`93f76b3`），**当天用户要求整体回滚**
> （只保留了"曲绘模糊用屏幕遮罩裁切"那一处）。那版的全部数值、以及"顶栏没地方放"
> 这个绕不开的坑，都留在 §6.5 —— 下次要再做，直接照抄，别重新推导。

版式上那句「原作详情在背景层 + 底部难度页签、我们右侧手机面板」的差异**不成立** ——
原作本来就有这台手机（`ScreenLayerFreeLiveCellPhone`），我们一直照它做的。

## 1. 原作是怎么搭起来的

选曲**不是一个 prefab**，是下面这组（全在 `Assets/Resources/screen/prefabs/`）：

| prefab | PathID | 职责 | 我们有吗 |
|---|---|---|---|
| `ScreenLayerMusicSelectBackground` | 651 | 背景层：`Background` 1820×980、`Difficulty` 难度色带、`UIPartsBlurMusicJacket` 虚化曲绘 | 用自己的背景图 |
| `ScreenLayerFreeLiveMusicSelect` | 702 | 列表层，94 个节点（单人自由演奏） | 有 |
| `ScreenLayerMultiLiveMusicSelect` | 668 | 多人版，同一套零件 | 有（派生态） |
| `ScreenLayerFreeLiveCellPhone` | **704** | **手机面板**：曲绘 + 曲名 + 难度 + 开始 + 评级，317 节点（§3.6） | **有**（就是我们的倾斜手机） |
| `MusicInfoView` | 1223 | 进 live 前的过渡详情（曲绘 400×400 + 标题/歌手/作曲/谱面作者），**不是**选曲界面那块 | 无 |
| `MusicSelectSingleListCell` | 1121 | 列表行（144 高） | 有（84 高） |
| `MusicSelectSimpleListCell` / `…GridCell` | 1122 / 1123 | 列表的另一套行 / 宫格单元 | 无（我们是同一套行） |
| `MusicSelectDifficultyButton` | 1126 | **宽屏版**的 5 个难度页签（120×124）；手机里用的是箭头 + 单块（§3.6） | 有（圆片，形态对，尺寸 0.63×） |
| `MusicSelectChangeViewButton` | 1130 | 列表 ↔ 宫格切换（200×72） | **无** |
| `MusicSelectUnitTab(List)` | 1117 / 1118 | 左侧组合页签（136 宽） | **无**（我们没有"组合"概念） |
| `MusicSelectSortDialog` / `FilterDialog` | 1731 / 1732 | 排序 / 筛选弹窗 | 有（下拉 + 胶囊） |

列表层内部的层级（实测）：

```
ScreenLayerFreeLiveMusicSelect
  ComponentRoot
    TournamentRoot            锦标赛开关（我们不需要）
    MusicSelectList
      Left                    → MusicSelectUnitTabList（136 宽，x=44）
      ComponentRoot
        UIPartsSearchField    760.8 × 72（图标 34 / 清除 44）
        MusicSelectGridView   1080 宽（宫格视图）
        ChangeViewButton      200 × 72
        NotFilterResult       912 × 0（"无结果"占位）
      MusicSelectListContent  912 宽（列表视图）
        MusicSelectListView → Viewport → Content（行依次排）
        GuideLine             4 高的分隔线
      TopRIght                415 × 56（排序下拉 280×56 + 筛选按钮 72×72）
```

## 2. 换算基准：原作就是 1920×1080（2026-11-01 实测，推翻了 1334×750）

**证据（一手，别再用推断）**：`CanvasRoot` prefab（PathID 426）根节点上挂着
`ScreenCanvasScaler`（MonoBehaviour，PathID 45），YAML 原文：

```yaml
m_UiScaleMode: 1                 # ScaleWithScreenSize
m_ReferenceResolution: {x: 1920, y: 1080}
m_ScreenMatchMode: 0             # MatchWidthOrHeight
m_MatchWidthOrHeight: 0          # 按宽度匹配（等比，无留边）
m_ReferencePixelsPerUnit: 1
```

三条互相印证的旁证：

1. `ScreenLayerMusicSelectBackground` 里的 `Background` sizeDelta 是 **1820×980**，
   但它的父节点 `GameObject` 是 **100×100** 的占位（anchor 铺满）→ 实际 **1920×1080**，
   正好满屏。`Difficulty` / `UIPartsBlurMusicJacket` 的 1820×4880 → **1920×4880**，色带铺满屏宽。
2. 列表 `MusicSelectListContent` 左边缘 **212**、宽 **912** → 右边缘 1124；
   手机 `CellPhone` 左边缘 **1144.85**。**间隙 21px**，两个块严丝合缝排在一起。
   按 1334 算的话两者会重叠 262px，不可能。
3. 按 1920 算，列表 912 + 手机 754 = 1666 < 1920，右侧还留 21 边距，版式自洽。

**为什么之前会错**：`bg1` / `bg2` 两张 sprite 资产尺寸是 1334×750，被当成了画布。
那是**贴图分辨率**，不是显示尺寸 —— 它们被拉大到 1920×1080 铺满（×1.44 放大）。
`SELECT-UI.md` 早先的「×1.44」就是从这个误会来的，连带 §3/§4 的折算列一起作废。

**对我们的意义**：项目自己的虚拟布局基准也是 1920×1080（`SongSelect.cpp` 的
`kBase = h/1080`，1080p 窗口下 `k = 1`），**prefab 数值可以直接搬，一比一，不用换算。**

**可信度说明（没变）**

| 数据 | 可信度 | 原因 |
|---|---|---|
| 尺寸（width/height） | ✅ 高 | 美术在 prefab 里摆的就是最终尺寸 |
| 间距、内部偏移 | ✅ 高 | 行/页签内的相对位置是静态的 |
| 层级（谁是谁的子节点） | ✅ 高 | RectTransform 的 `m_Father` |
| 静态容器的 `anchoredPosition` | ✅ 较高 | 手机层这类"摆好就不动"的容器可信（§3.6 用它算位置） |
| 内容文本的 `anchoredPosition` | ⚠️ 低 | 曲名/歌手/难度这些是**代码运行时排版**，prefab 值是编辑器占位 |
| 字号 | ⚠️ 未取到 | 文本组件的 TMP 字段没导出，只能按矩形高度估 |

## 3. 原作实测尺寸（**左列就是 1920 画布的实际像素，直接用**）

> 下表「→ ×1.44」那一列是旧的错误换算，**作废**，留着只为对照历史。
> 我们的 1080p 窗口下 `k = 1`，所以左列的数字可以直接写进 `SongSelect.cpp`。

### 3.1 列表行 `MusicSelectSingleListCell`：912 × 144

| 元素 | 原作 | → ×1.44 | 备注 |
|---|---|---|---|
| 行 | 912 × 144 | 1313 × **207** | 锚点在左上，行高固定 |
| 等级球 `PlayLv` | 96 × 96，x=64 | 138，x=92 | 居中放定数 |
| 曲绘缩略 `Left/JacketThumbnail` | 96 × 96，x=176 | 138，x=253 | 96 高 = 行高的 2/3 |
| 标题 `Title` | 617 × 56，x≈238 | 888 × 81 | |
| 作曲 `Creator` | 617 × 50，y=-95 | 888 × 72 | 在标题下方 |
| 歌手 `Vocal` | 617 × 50，y=-135 | 888 × 72 | 最下面一行 |
| 5 个通关指示 `ClearStatus` | 230 × 39，格子 38×38，间距 39 | 331，格子 **55**，间距 56 | **按难度横向并排**，位于行左下 |
| 合作曲缎带 `UIPartsMusicCollaborationRibbon` | 856 × 40 | 1233 × 58 | 我们没有 |

要点：**等级球和曲绘一样大（96）**，都是行高的 2/3；5 个指示是 38 的方块横排，
不是文字下的小点。

### 3.2 难度页签 `MusicSelectDifficultyButton`：根 740 × 740

| | 原作 | → ×1.44 |
|---|---|---|
| 单个页签 | 120 × 124（Master/Normal 宽 126） | **173 × 178** |
| y | 74 | 107 |
| x | 95 / 235 / 374 / 514 / 652 | 137 / 338 / 538 / 740 / 939 |
| 间距 | 139.5 | **201**（页签 173 + gap 28） |
| 内部 | `Cover` 同尺寸 + `XxxText` 32 高 + `XxxSelect` 选中态 + `Lock` 44×51 | 文字居中偏上 y=+3 |

### 3.3 顶部零件

| 元素 | 原作 | → ×1.44 |
|---|---|---|
| 搜索框 `UIPartsSearchField` | 760.8 × 72 | 1095 × **104** |
| 搜索图标 / 清除按钮 | 34 / 44 | 49 / 63 |
| 排序下拉 `UIPartsDropdown` | 280 × 56 | 403 × 81 |
| 排序下拉弹层 `Template` | 高 300，行 `Item` 128×64 | 高 432，行 184×92 |
| 筛选按钮 `FilterButton` | 72 × 72 | 104 × 104 |
| 列表/宫格切换 | 200 × 72 | 288 × 104 |
| 左侧组合页签条 | 136 宽，x=44 | 196 宽，x=63 |
| 引导线 `GuideLine` | 912 × 4（左段 848 右段 865） | 1313 × 6 |

### 3.4 详情面板 `MusicInfoView`

| 元素 | 原作 | → ×1.44 |
|---|---|---|
| 曲绘 `Jacket` | 400 × 400 | **576 × 576** |
| 难度角标 `DifficultyJacket` | 400 × 400（和曲绘同框） | 576 |
| 角标容器 `DiffParent` / 图标 | 100 × 42 / 42 × 42 | 144 × 60 / 60 |
| 标题 `Title` | 1240 × 40（在曲绘右侧） | 1786 × 58 |
| 歌手 `Singer` / 作曲 `Creator` | 1240 × 28 | 1786 × 40 |
| 谱面 `ScoreTitle` / `ScoreCreator` | 1240 × 40 / 1240 × 28 | 同上 |
| 分隔线 `Line` | 1274 × 2 | 1834 × 3 |

（1240 宽是"跨面板"的文本宽度，不是面板自身宽度 —— 别拿它当面板尺寸。）

### 3.5 背景层

`Background` sizeDelta 1820×980，父占位 100×100 → **实际 1920×1080 满屏**；
`UpperDot` 1820×138 → 1920×138；`Difficulty` / `UIPartsBlurMusicJacket` 1820×4880 → **1920×4880**
（难度色带 / 虚化曲绘，竖向拼接滚动）。
（旧文档写的「四周各留 50」是错觉 —— 那 100 是父节点的占位尺寸，不是边距。）

### 3.6 手机面板 `ScreenLayerFreeLiveCellPhone`（PathID 704，2026-11-01 实测）

这就是我们那个「倾斜手机」的祖宗，**原作确实有这台手机**，不是我们发明的。
它就是 `img_smartphone.png` 的出处：贴图 1034×1942，RectTransform 754.3×1416.7，
缩放 **0.7295**（1034/754.3 与 1942/1416.7 都是这个数，所以我们的 `phoneAspect` 是对的）。

**容器本身（铁数字，静态摆放，可信）**

| | 值 | 说明 |
|---|---|---|
| 尺寸 | **754.3 × 1416.7** | 占屏宽 39.3%、屏高 **131.2%** |
| `anchoredPosition` | **(562, -202)** | 相对**屏幕中心**，Unity y 向上 |
| pivot / anchor | (0.5,0.5) / (0.5,0.5) | 中心对齐 |
| `m_LocalEulerAnglesHint.z` | **+3°** | Unity 逆时针 → **右端上翘**（我们代码里是 +5° 顺时针，方向反了） |
| `m_LocalScale` | 1 | 没有额外缩放 |

换成我们的坐标（1920×1080、y 向下）：

```
中心      (1522.0,  742.0)          # 屏幕中心 (960,540) + (562, +202)
外框      x 1144.8 .. 1899.2        # 右边缘离屏幕右边界 20.8
          y   33.6 .. 1450.3        # 顶端离屏幕顶 33.6，底部切掉 370.4
可见比例  宽 100% / 高 74%（1046.4 / 1416.7）
```

**关键结论（对着用户那个疑问）**：原作的手机**不是**"只露一半"——
宽度方向是全露的（右边缘 1899 < 1920），只是**底部被切掉 26%**，
顶端只留 33.6 的空隙。真正"露一半"的感觉来自它的**尺寸**：手机比屏幕还高 31%，
所以只有上半截和中间一截出现在画面里。
我们现在是 562×1056、顶端留 90、底切 66 → **又小又摆得太靠右**，
所以看起来"整个都在屏里"。

**手机内部零件（尺寸可信，位置多为运行时排版，只当参考）**

| 元素 | 尺寸 | 备注 |
|---|---|---|
| 屏内容宽 | **700** | `JacketContent` sizeDelta x = -54.2（负值 = 左右各内缩 27.15），边框占 3.6%/边 |
| 曲绘 `Jacket` | **376 × 376** | 占屏宽 53.7%；水平居中（中心 x 偏 -2.6），上边缘距手机顶 **229** |
| 曲绘遮罩 `JacketMask` | 700 × 700 | 圆角裁切框，比曲绘大一圈 |
| 曲名 `MusicTitle` | **440.2 × 50** | 文本块左边缘 = 手机内 x -295.4（左边距 81.6），右侧留白给评级 |
| 歌手 `ArtistName` / 作曲 `VocalName` | 440.2 × 28 | 同上，行距 44 / 37（占位值） |
| 难度圆 | **5 × 96** | 容器 `Difficulty` 576 × 133.4，`Easy/Normal/Hard/Expert/Master` 全是 96 方块、`pos (0,0)`（**位置由代码排**，5 个塞进 576 → 步距约 120）；选中态 `Selected` 136、`Cover` 112、`Lock` 40×46 |
| 难度左右箭头 | 40 × 48 | 在难度条两端（±318），不在我们现在的方案里 |
| 开始按钮 `PositiveButton` | **258 × 96** | `NegativeButton` 同尺寸（换歌手/其他） |
| 底部按钮条 | 702 × 100，按钮 **80×80** | 换歌手 / 设置 / 随机三个 80 方块（`Cover` 68、`ShuffleButton` 图标 60×52） |
| 评级 `RankBase` | **112 × 112** | 右侧，徽章 `UIPartsHighScoreBadge` 216.2 × 88 |
| 定数标签 `UIPartsMusicDifficultyLabelWithIcon` | 144 × 40 | 图标 32，文字 74.5×24 |
| 谱面标签 `CustomMusicScoreLabel` | 520 × 32 | 图标 32 |

> 位置（`pos`）里唯一能信的是**尺寸和左对齐边距**：曲名/歌手那块在 prefab 里
> y 方向和曲绘是重叠的（194.5 vs 曲绘 103~479），明显是编辑器占位，
> 真值是代码排的。别照着 y 抄。

**原作的坐标里还有一处诡异**：定数标签 `pos.y = +747.4`，比手机上边缘（+708.35）还高 39 ——
平时应该是隐藏/被 `cellphone_mask` 裁掉的，只在某些形态下才露。别为它调整布局。

## 4. 我们现在的数值（`game/SongSelect.cpp`）

> 「原作」列是 §3 的**原值**（1920 画布），不需要再乘任何系数。
> 比例 = 我们 / 原作。1080p 窗口、UI 缩放 1.0 时 `k = 1`。
> **这张表就是现状** —— 2026-11-01 试过全部换成原作原值，已回滚（见 §6.5）。

| 项目 | 现值 | 位置 | 原作 | 比例 |
|---|---|---|---|---|
| 列表起点 x | 150 | `:2896 listX` | 212 | — |
| 列表宽 | 450 | `:2898 listW` | **912** | **0.49×** |
| 搜索框高 | 33 | `:2912 searchH`（`headerRowH` 同值 `:3072`） | 72 | 0.46× |
| 紧凑行高 | 84 | `:3237 compactH` | **144** | **0.58×** |
| 选中卡高 | 128 | `:3238 cardH` | （原作只有一套 144 的行，选中是另一层高亮） | — |
| 行内等级球 | 直径 48（cx=p0.x+30, r=24） | `:3791` | 96 | 0.50× |
| 行内曲绘 | 60（x=p0.x+62） | `:3794` | 96 | 0.63× |
| 行内标题 | 字号 20，起点 x=p0.x+136 | `:3800` | 617×56 → 大概 32 号 | — |
| 行内通关指示 | 18 方格 ×5（`indicate_back_new` 38px 图缩到 18） | `:3803` | 38 方格 ×5 | 0.47× |
| 选中卡：等级标签 | tagW 62（ch 48/128）、圆 r=27 | `:3720-3755` | — | — |
| 难度选择 | 圆片直径 60、间距 9（步距 69） | `:4209 dcD` / `:4210 dcGap` | **96、步距 115.2** | 0.63× / 0.60× |
| 确定按钮 | 高 56、宽 = 面板 58% | `:4276 okW/okH` | 258 × 96 | 0.58× 高 |
| **右侧手机面板** | **562 × 1056**，中心 **(1609, 618)** | `:4203-4214 phoneAspect/phoneH/phoneX/phoneY` | **754.3 × 1416.7**，中心 **(1522, 742)** | **0.745×** |
| 手机倾斜 | **+5°（顺时针，左端上翘）** | `:4223 kTiltDeg` | **3° 逆时针（右端上翘）** | **方向反了** |
| 手机可见 | 顶留 90、底切 66、右侧留 30 | 同上 | 顶留 33.6、底切 370.4、右侧留 20.8 | — |

现有结构（要改之前先记住）：

- 左侧列表 = 搜索框（33 高）+ 排序/分组胶囊 + 滚动行（84 / 选中 128）。
- 右侧 = **倾斜 +5° 的"手机面板"**：曲绘、标题、难度圆片、确定按钮全在里面，
  顶点统一绕手机中心旋转（见 [design-notes/songselect-scroll.md](design-notes/songselect-scroll.md)）。
- 行内 5 个指示是 `indicate_back_new.png`（38×38 原图）缩到 18 贴，
  上面再叠 `clear_indicate.png`（金）/ `fullcombo_indicate.png`（粉）。

## 5. 差异清单

| # | 差异 | 影响 | 处理 |
|---|---|---|---|
| 1 | 行/零件整体 0.49~0.63× | 观感"小一号"，字挤、指示看不清 | 方案 A/B |
| 2 | 列表占屏宽 23% vs 原作 47.5% | 手机和列表之间空 728px（原作只空 21px） | 方案 B 才根治 |
| 3 | 通关指示挤在文字下（18px） | 原作是 38px 方块横排在行左下 | 顺带改布局 |
| 4 | 难度选择是"圆片 + 下方文字" | 原作手机里就是 96 的圆片（跟我们的形态一致，只是我们 60） | 放大即可 |
| 5 | 无合作曲缎带 | 合作曲看不出来 | C |
| 6 | 无难度角标叠曲绘 | 原作曲绘上压难度图标 | C |
| 7 | 无左侧组合页签 / 宫格视图 / 筛选弹窗 | 功能差异（我们没有组合概念） | 不做（说明理由即可） |
| 8 | 背景是静态图，不是"虚化曲绘 + 难度色带" | 原作选中歌时背景会变 | C（`UIPartsBlurMusicJacket` 用现有 GL 就能做） |
| 9 | **手机面板 0.745×、倾斜方向反了、底部切得太少** | 手机"整个都在屏里"，和原作不像 | **方案 A0（见 §6.0）** |

## 6. 方案

### A0. 只把手机摆回原作的位置和大小（最小改动，10 行以内）

`SongSelect.cpp:4203-4223` 三个数 + 倾斜方向：

| 变量 | 现值 | 改成 | 依据 |
|---|---|---|---|
| `phoneH` | `(h - 24*kBase) * scale` | **`h * 1.3118f * scale`** | 1416.7 / 1080 |
| `phoneX` | `w - phoneW - 30*k` | **`w - phoneW - 20.8f * k`** | 右边缘 1899.15 |
| `phoneY` | `(h - phoneH)*0.5 + 78*k` | **`33.6f * k`** | 上边缘 33.65 |
| `kTiltDeg` | `+5.0f`（顺时针） | **`-3.0f`**（y-down 里取负 = 逆时针，右端上翘） | `m_LocalEulerAnglesHint.z = 3` |
| 内部布局 | `scrX0/scrY0` 等按 phoneW/H 的百分比 | 曲绘 `jw = phoneW * 0.4987`（= 376/754.3）、难度圆 `phoneW * 0.1273`（= 96/754.3） | §3.6 |

**注意副作用**：手机放大到 754 宽后，左边缘到 1145，而我们列表右边缘只有 600 ——
中间会空出 **545px** 的空白（原作只有 21px）。所以 A0 单独做会显得很怪，
**它必须和 A 或 B 一起做**。

### A. 等比放大列表（和 A0 配套，半天级）

目标是让"列表宽 : 手机宽"这个比例对上，同时所有零件统一放大到 0.75×：

| 项目 | 现值 | A1（0.75×，配 A0） | 原作 |
|---|---|---|---|
| 列表宽 | 450 | **684** | 912 |
| 紧凑行高 | 84 | **108** | 144 |
| 曲绘 | 60 | **72** | 96 |
| 等级球 | 48 | **72** | 96 |
| 通关指示 | 18 | **28** | 38 |
| 行内标题 | 20 | **24** | ~32 |
| 搜索框高 | 33 | **54** | 72 |
| 选中卡高 | 128 | 160 | （原作 144 + 高亮层） |
| 难度圆 | 60 | **72**（步距 90） | 96（步距 ~120） |

A1 的判据：改完填进 §4，**每一项 / 原作都落在 0.72~0.78**，且手机也是 0.745 —— 全部对齐。
不追求 1.0，因为列表在我们的版式里是侧栏（原作是等宽两块）。

### B. 照抄原作数值（1:1）—— **2026-11-01 试做过并回滚，数值见 §6.5**

- 列表左边缘 **212**、宽 **912**、行高 **144**（`MusicSelectListContent` 原值）；
- 搜索框 **760.8 × 72** @ (196, -28)，排序下拉 280×56 + 筛选 72×72 @ 右上；
- 手机 **754.3 × 1416.7** @ 中心 (1522, 742)、3° 逆时针（§3.6）；
- 难度圆 96、步距 115.2，开始按钮 258×96，曲绘 376。

**代价**：改完就是原作的样子，我们现在那套"手机贴右、列表留白"的观感会消失；
`tiltedItemPos()` / 命中框、试听按钮落点都要跟着重算。
**建议**：先 A0 + A1 看效果，B 由用户拍板。

### 6.5 试做记录（方案 B，2026-11-01 —— **已回滚**）

当天做完整套改动（commit `93f76b3`）并截图验过，用户看完要求回滚。
**代码回到改动前，只留下遮罩那处修复。** 这张表是当时的改法，下次要再做直接照抄：

| 项 | 旧 | 新 |
|---|---|---|
| `listX` / `listW` | 150 / 450 | **212 / 912** |
| `pitch` / `compactH` / `cardH` | 104 / 84 / 128 | **152 / 144 / 208** |
| `searchW` / `searchH` | 450 / 33 | **760.8 / 72**（字号 17→26） |
| 行内等级球 | r24 @ x+30 | **r48 @ x+112** |
| 行内曲绘 | 60 @ x+62 | **96 @ x+176** |
| 行内标题 / 通关指示 | 20 号 / 18 方块 | **36 号 / 38 方块步距 39** |
| 选中卡 | tagW62 / 圆 r27 / 文字 x+96 | **tagW96 / r48 / x+168** |
| 分组标题 | 22 号 | **34 号** |
| `phoneH` / 位置 | `h-24` / 右下角 30k | **1416.7k / 中心 (w/2+562k, h/2+202k)** |
| 倾斜 | +5°（顺时针） | **-3°（逆时针，右端上翘）** |
| 手机曲绘 | `sw*0.62` | **376/754.3**，距手机顶 229 |
| 难度圆 | 60 / 间距 9 | **96 / 步距 120** |
| 确定按钮 | 高 56、宽 58% 面板 | **258×96** |
| 底部两圆 | 64 | **96** |
| 模糊层 | 1.05×phoneW 正方形 | **700×574 盒 + `mask_smartphone` 遮罩** |

**顶栏那块绕不开（下次要重排这是第一道关）**：手机从 x 1145 开始，搜索框（196..957）
右边只剩 188px，原来那条「下拉×2 + 四个按钮」的横排会**整个压在手机下面**（第一版截图里
直接看不见）。当时的解法是整条搬到**左侧竖列** `x 44 / 宽 160`，正好是原作的
`MusicSelectUnitTabList` 位置 —— 原作那儿是"组合页签"，我们没有组合概念。
等级 chip 落在搜索框和手机之间的窄条里。这套解法本身是可行的，回滚是因为用户不喜欢整体观感。

**曲绘模糊的溢出（用户报的 bug）—— 这处改动保留了下来**：原实现是一个 1.05×phoneW 的
正方形 quad，`AddImageRounded` 的圆角挡不住手机边框和刘海。现在：
1. `blurredJacketTex()` 多收四个参数（quad 在手机内的归一化矩形），生成纹理时
   先做 24×24 的模糊，再**双线性放大到 96×96**，同时逐像素采样
   `assets/select/mask_smartphone.png` 的 alpha 相乘 —— 刘海和圆角就切出来了。
   24×24 直接乘遮罩不行：刘海只有 3 个 texel，边缘全是锯齿。
2. 归一化的基准是**屏幕矩形**（`scrX0..scrX1 / scrY0..scrY1`），不是整块手机 ——
   按整块手机映射时裁切线落在边框内侧，模糊还是会糊到顶边。
3. **遮罩的刘海要填平**：`img_smartphone.png` 自己的顶部就有一条刘海凹槽，
   再叠一个带刘海的遮罩会在顶上挖出个悬空缺口（2026-11-01 截图里就是这样）。
   现在 `assets/select/mask_smartphone.png` 是"填平刘海 + 顶部 5% 渐隐"的版本，
   由 `Drafts/mask_smartphone.webp` 生成。
4. quad 仍是 1.05×phoneW 的正方形（1:1 那版的 700×574 也回滚了），
   只是被遮罩裁在屏幕里；不透明度 235 → **190**（用户要"更透"）。
   要再收窄就改 `bleedW`。

**故意没做**：列表滚动模型仍是"居中 + 无限循环"（原作是顶部对齐的普通 Viewport，
选中行位置固定），所以向上滚时顶部会露出半截行；宫格视图 / 左侧组合页签 / 筛选弹窗
仍然没有（功能差异，不是几何差异）。

### C. 补零件（可随时插入，风险低，全部照着 prefab 原值做）

1. **通关指示重排**：5 个 38 方块横排在行左下、间距 39（现在挤在标题下 18px）。
2. **难度角标叠曲绘**：`DifficultyJacket` 与曲绘同框（400×400），角标容器 100×42 + 图标 42。
3. **合作曲缎带**：`UIPartsMusicCollaborationRibbon` 856×40，横在行顶（数据：`charts/<id>.json` 里加个 flag 即可）。
4. **背景虚化曲绘 + 难度色带**：拿当前选中曲的 cover 做模糊 → 现有 GL 就能画。
5. **未解锁锁图标**：44×51（原作 `Lock`）。

## 7. 验收方法

1. **数值对照**（首选，客观）：把 §4 的表填成"我们 / 原作 = 比例"，逐项检查是否同档
   （1120-11-01 之后不再有"折算"这一步，原作列就是最终值）。
2. **几何对位图**：`python .workbuddy/tools/select_phone_overlay.py build/select_phone_overlay.png`
   —— 在 1920×1080 上叠加画出原作手机和我们的手机（含旋转、列表块），
   一眼能看出大小/位置差多少。改完重跑一次就是回归验证。
3. **无头截图**：`bash .workbuddy/tools/shot.sh shots/select_v2.png --no-party`
   （不给 `--sus` 时 uiClock≈1.2s 自动抓选曲界面；`shot.sh` 会自动重试白屏帧）。
   建议同时抓一张窄窗口（`--width 1280 --height 720`）确认缩放没塌。
4. **人工确认**（需要用户）：滚动时行高动画、文字是否被切、难度页签点击热区。
   这几样我这边点不动，只能靠他。

## 8. 未确认项

- [x] ~~**CanvasScaler 参考分辨率**~~ → **2026-11-01 已确认：1920×1080**（§2，别再折算）。
- [ ] **字号**：列表行 `TitleText` / `Creator` 的 TMP 组件没导出 `m_fontSize`，
      要换 AssetStudio 或直接读 TMP 组件的 YAML 才能拿到。
- [x] ~~**选中行怎么放大**~~ → **2026-11-21 已确认**：自由演奏列表实际用的是
      `MusicSelectListCell`（prefab **1129**，895×144），选中时 `RectTransform.sizeDelta`
      从 144 补到 **230**（`highlightSizeX/Y = 895/230`），不是 `MusicSelectSingleListCell`
      （1121，912×144 / 232，另一套未用于这条列表的 prefab）。背景是 `Highlight` 节点的一张
      `bg_base_r20_wh` 九宫格图，白色 **60%**、圆角 **20px**、**没有描边**；`Neutral` 行没有
      底色，只有 `UnderLine` 856.64×4 / 白 20%。展开是 **0.1s、Ease.Linear、无延迟** 的
      DOTween，没有回弹。详见 §12。
- [ ] 难度页签的**选中/未选中**底色（`XxxSelect` 的 sprite 与颜色）。
- [ ] **手机面板的运行时位置**：§3.6 的位置来自 prefab 静态值（`CellPhone` 的 anchor/pivot
      都是中心、scale 1，看起来就是最终摆放）。但选曲界面有入场动画，
      需要一张实机截图核对"顶端留 33.6"是否成立。

## 9. 数据与工具

- 原始测量：`D:\Tools\rea\work\jp\ms_freelive.{json,txt}`（列表层）、`ms_info.*`（详情）、
  `ms_row.*`（行）、`ms_diffbtn.*`（难度页签）、`ms_bg.*`（背景层）、
  **`ms_phone.*`（手机面板，317 节点）**、`ms_canvasroot.*`（UI 根，CanvasScaler）。
- 精灵尺寸总表：`D:\Tools\rea\work\jp\sprite-geometry.csv`（2807 个 sprite 的 w/h/pivot）。
- 提取脚本：`D:\Tools\rea\work\jp\ms_geom.py <prefabPathId> [输出前缀]`（走 AssetRipper 本地 API）。
- 按名字找 prefab：`python arip_search.py <关键词> PrefabHierarchyObject`
  （只列 `PrefabHierarchyObject`，那才是真 prefab —— 同名的 `PrefabInstance` / `GameObject` 会混进来）。
- 读组件字段（CanvasScaler 就是这么拿到的）：先 `/Assets/View` 找到 GO 的 `Components[]` PathID，
  再 `/Assets/Yaml?Path=...` 取原文。
- 对位图脚本：`.workbuddy/tools/select_phone_overlay.py`（本项目内，改了布局重跑它就能对比）。
- 逆向全过程与工具链：[RE.md](RE.md)；本轮工作日志：`.workbuddy/memory/2026-11-01.md`。

## 10. 下次开工接上（新 session 从这看）

读三份就够：本文件 → [design-notes/songselect-scroll.md](design-notes/songselect-scroll.md)（现有视觉规则）→ `RE.md`（要重新取数据时才看）。

**先确认环境**（AssetRipper 不会自己常驻，必须用后台任务起，否则进程随命令结束被带走）：

```bash
# 需要重新读 prefab 时才用；只是改代码不需要
/d/Tools/rea/work/tools/arip/app/AssetRipper.GUI.Free.exe --headless --port 17654   # run_in_background
curl --noproxy '*' -X POST "http://127.0.0.1:17654/LoadFolder" --data-urlencode "path=D:/Tools/rea/work/jp/GameData"
# 加载约 2 分钟，302 只代表受理；用 Search/View 能不能查到来判断就绪
cd /d/Tools/rea/work/jp && python arip_search.py "MusicSelect" PrefabHierarchyObject
```

**开工顺序建议**：

1. 用户在 §6 里挑 A0 / A0+A1 / B —— **这一步必须先等他拍板**，A0 单独做会让手机和列表之间空 545px。
2. 按选定的方案改 `game/SongSelect.cpp`：手机那几个数在 `:4203-4223`，
   列表/行在 `:2896`（listX/listW）、`:3237`（compactH）、`:3791`（等级球）附近。
   **行高/曲绘/等级球/指示/搜索框/listW 要一起改**，只改一个比例会歪。
3. `bash build.sh`（必须 0 警告）→ `bash .workbuddy/tools/shot.sh shots/select_a1.png --no-party`。
4. 把结果图和老图并排给用户看，让他判"够不够大"——这一步只能靠他的眼睛。
5. 通过就 commit；不通过就只调 `compactH` 与 `listW` 两个数（其余按比例跟随）。

**别做的事**：不要在没有用户确认的情况下动方案 B（照抄版式）——那会把倾斜手机面板这套
现有设计整个推翻，属于产品决策。

## 11. 相关但独立的一摊事

触摸特效（`game/TapEffect.*`）同样是照原作粒子系统还原的，但**已经做完并提交**，
参数与坑记在 [design-notes/touches-effect.md](design-notes/touches-effect.md)；原理和取数方法见 [RE.md](RE.md) §4.3。

## 12. 2026-11-21 复核：选中卡 / 切换歌手 / 确认动画

这一节是这轮逆向的实测结论，数据来自 prefab 1129 / 1960 / 1961 / 2279 的
`arip_dump` 与 `AnimationClip`，不是截图目测。

### 12.1 选中卡的 border / 圆角 / 展开曲线

- **实际使用**：自由演奏列表的 `listViewItemPrefab` 是 `MusicSelectListCell`
  （prefab **1129**，根 **895×144**）。`MusicSelectListView.highlightListViewItem`
  的类型也是这个类；`MusicSelectSingleListCell`（1121）是另一套单列 prefab
  （912×144，`highlightSizeX/Y = 912/232`），**不是这条列表的选中卡**。
- **选中尺寸**：`highlightSizeX = 895`、`highlightSizeY = 230`。列表在选中时把
  `RectTransform.sizeDelta` 从 `144` 补到 `230`，宽度不变。
- **背景**：`MusicSelectListCell/Base/Highlight` 是一张 Image，sprite 为
  **`bg_base_r20_wh`**（50×50，九宫格 border 24，圆角 **20px**），`m_Type: 1`
  （Sliced），颜色 **白色 60%**（`m_Color.a = 0.6`）。**没有 Outline / 第二条
  Image / 描边框**；`Neutral` 行没有底色，只有 `UnderLine` 856.64×4，
  `base_wh`、白色 20%。
- **展开曲线**：`MusicSelectListView.HIGHLIGHT_TWEEN_SEC = 0.1`、
  `HIGHLIGHT_TWEEN_DELAY_SEC = 0`；代码用 `DOTween.To(..., duration = 0.1f)`，
  `SetDelay(0)`、`SetEase(1)`，而 `DG.Tweening.Ease.Linear = 1`。也就是说
  **0.1 s 线性、无回弹、无 Overshoot**；每一帧的 setter 调
  `UpdateHighlightTween` 重排其余行，不是指数逼近。
- 我们原来的 `AddRect` 白色描边、`radius 10`、`a=190`、`1-exp(-dt*16)` 都不对；
  2026-11-21 已改成“无描边 / 半径按 20/144 缩放 / 白色 153 / 0.1s 线性”。

### 12.2 切换歌手面板（`ChangeVocalSubWindowDialog`）

- prefab **1960** `ChangeVocalSubWindowDialog`：`RightSubWindowComponent` 从右侧
  滑入；`Bg` **956 宽、整窗高**，`bg_base_wh`，颜色 **#444466 / 80%**（深蓝）。
- 全屏 `CloseArea` 下还有一层 `Cover`：**#000022 / 50%**，所以打开时背景整体压暗
  一半；关闭按钮 44×44，位于右上 `(-18.1, -18.5)`，`btn_close_wh`。
- `ListView` 左右各内缩 64（956 − 128 = **828**），行 prefab `ChangeVocalCell`
  （1961）**828×164**。
- 每行就是一张 **`bg_base_r20_wh` 卡片**：颜色 **#DFDFEB（223,223,235）不透明**，
  圆角 **20px**，**没有描边**。左侧单选：
  `UIPartsRadioToggle` 72×72（左边距 16），白色圆 `btn_circle_h56_wh`，选中时叠
  `icon_radio_gn` 36×36 薄荷勾；未选中只有白盘。
- 文本从卡片左缘 +102 起：`VocalName` 36px、颜色 #555577；`VocalMember` 32px、
  颜色 #444466；两行 `Vo. …`（换行后第二行缩进）。`MusicShopDetailVersionInfoCell`
  负责这块，行内 `VocalName` 700×36 @ (5,-40)、`VocalMember` 705×80 @ (7,-107)
  （相对 `CellRoot` 990 宽）。
- 锁定态：整卡盖 `#000022` 50%，锁图标 80×92（`icon_lock_wh`），上面还有
  `UIPartsReleaseConditionsBalloon` 592×96、文字 28px 的解锁条件气泡。
- **prefab 里没有标题文本**（`header: null`），也没有卡片描边；它是一块纯色
  `Bg` + 828 宽的卡片列表。
- **2026-11-21 已按上面 1:1 落地**：`SongSelect.cpp` 的切换歌手面板改成
  956 宽、整窗高、#444466/80%，全屏 #000022/50% 压暗；列表 828 宽、行 164 高、
  `#DFDFEB`/圆角 20/无描边；单选 72、勾 36、文字 36/32；去掉了原来的标题行。
  资源 `icon_radio_gn.png` / `btn_close_wh.png` 已进 `assets/select/`。

### 12.3 选取确认动画（`SoloLiveTransitionEffectContent`）

- prefab **2279**，挂在 `ScreenLayerFreeLiveCellPhone` 的
  `transitionEffectContent` 上；三套 Animator：
  - `tapEffect` = `fx_live_transition_v2_02` / `anim_transition_v2_02`：
    **0.4167 s**（25 帧 @60）。`light` 的 `m_Color.a` 从 0 升到 1；`flare`、
    `back_light`、`tri_02` 在 0.0167 s 激活，`flare_white` 在 0.333 s 激活。
  - `buttonEfect` = `fx_live_transition_botton_v2_01` /
    `clip_transition_botton_v2_01`：**0.0167 s（1 帧）**，只把
    `botton_light_01/02`、`line_circle` 打开。
  - `startEffect` = `fx_live_transition_v2_01` / `anim_transition_v2_01`：
    clip 长 **4 s**；`botton_light`、`tri_botton`、`line_circle` 在 0.0167 s 激活，
    `circle` 在 0.25 s 激活。`PlayStartEffect` 在 `Animator.Play(0)` 之后
    `UniTask.Delay(TimeSpan.FromSeconds(2.0))`，也就是代码只等 **2.0 s** 就继续。
- 这些 RawImage 都挂在手机内部：`cellphone_mask` **1016.9×2221.7**、
  `light` **1532.3×2357.2**；效果被 `cellphone_mask` 裁成手机屏幕形状，
  **不是从按钮中心铺满全屏的白色圆**。`PlayButtonEffect()` 只负责把
  `buttonEfect` 打开，`PlayTapEffect()` 先 `StopAllEffect()` 再播 `tapEffect`。
- 我们的 `DecideBurst` 2026-11-21 已换成原贴图 + 真实 0.4167s tap / 2.0s start
  包络，并把 `light` 画在手机屏幕矩形里（不再整张 `tex_transition_02` 铺屏）；
  但最终 1:1 仍要等 §13 的最终确认页把手机遮罩层接上，当前是从 `确定` 直接起
  加载遮罩，所以只能看到前 0.4167s。

### 12.4 手机难度按钮 / 背景漂浮物

- 原作的手机难度条是 `DifficultySelectContent`（prefab 2289），不是宽屏的
  `MusicSelectDifficultyButton`（1126）：
  - 5 个 96×96 的槽位，间距 24（整行 576）；
  - 未选中圆盘 `btn_difficulty_<name>_off` 112×112；
  - **选中圆盘是实心填充的 `btn_difficulty_<name>_on` 112×112**（不是只加一圈描边），
    后面再叠 `eff_btn_difficulty_selected_wh` 136×136 白色光晕；
  - 等级数字 46px，难度名 24px（选中时名字变深色 `#444466`）；
  - 左右箭头 40×48，位置 ±318，图 `btn_arrow_triangle_gr`（hover 用
    `eff_arrow_triangle_pk`）；未解锁/无谱面用 40×46 `icon_lock_wh` + 暗罩。
    **2026-11-21 用户确认：左右箭头是给 APPEND 切换用的，我们没有 APPEND 支持，
    已从手机面板删除，不做“点了换难度”的假按钮。**
  - 2026-11-21 已把 `SongSelect.cpp` 的手机难度行改成这套（按手机宽度
    `phoneW/754.3` 缩放；当前 1080p 下约 71px 圆盘 / 429px 行宽）。
- 原作选曲背景只有 `ScreenLayerMusicSelectBackground`（prefab 651）：
  `Background` + `Difficulty` 色带 + `UpperDot`（`ptn_dot_LL_wh`，138 高、白 50%）
  + 虚化曲绘；**没有**在背景里随机漂浮的圆环/三角形。我们 2026-11-15 加的
  `drawBgShapes()` 是自己造的（连 RNG 都照 web component 抄的），2026-11-21
  已整段删除；现在背景回到 `bg_common` + 壁纸/虚化曲绘，不再有那些漂浮物。

### 12.5 取数位置

- prefab：`D:\Tools\rea\work\jp\list_cell_full.json`、`vocal_dialog_full.json`、
  `vocal_cell_full.json`、`transition_full.json`、`final_full.json`。
- 动画：`clip_anim_transition_v2_01.yml`、`clip_anim_transition_v2_02.yml`、
  `clip_clip_transition_botton_v2_01.yml`。
- 代码：`MusicSelectListView.cs`（`HIGHLIGHT_TWEEN_SEC`）、
  `MusicSelectListCell.cs`、`ChangeVocalSubWindowDialog.cs`、`ChangeVocalCell.cs`、
  `SoloLiveTransitionEffectContent.cs`（AssetRipper 导出的签名；逻辑以 so 反汇编为准）。

## 13. 最终确认页（确定 → 卡组 / 自动演出 / MV → 开始）——已落地 v1

原作的“确定”不是直接开始，而是进 `ScreenLayerFreeLiveFinalConfirmation`
（prefab **703**）：`ScreenLayerDeckEditBase` + 5 张 `DeckCard` + 自动演出开关 +
MV 模式按钮 + Live 设置 + 综合力 + `Decide`。我们的改造方案：

### 13.1 状态（实际实现）

没有新增 `AppState`：最终页放在 `SongSelect.cpp` 的模块状态
`gFinalConfirmOpen` 里，底下的列表/手机变量一个都不用跨文件搬。

- 选曲页 `确定`：保存 `gFinalEntry / gFinalDifficulty / gFinalVocal`，打开
  `gFinalConfirmOpen`，`action = SelectNone`，**不调用 `startSession`**。
- 最终页 `开始`：返回选中的 entry，main 才走现有 `startSession`。
- `Esc` / `返回`：关闭最终页，回选曲，不丢选择。
- 调试：`CPSEKAI_FINAL_CONFIRM=1` 可直接截图最终页（见
  `shots/final_confirm2.png`）。

### 13.2 版面（1920×1080 虚拟画布）

> **2026-11-21 纠错**：上一版最终页是我按目标值拼的（居中、深色渐变、自画按钮），
> 不是 prefab 绝对坐标，已废弃。现在按 prefab 703 的 RectTransform 绝对 rect 画：
> `BG x=56 y=303.8 1546.2×555.3`、`DeckBase x=70.4 y=256 1080×624`、
> 卡槽中心 y=601、`BGUnder y=707`、`TotalPower/Value x=150.2 y=285`、
> `UIPartsLiveModeButton x=57 y=962 176×72`、`AutoLiveToggle/ToggleRoot
> x=822.6 y=1014.4 152×56`、`Decide` 在 2× 的 BottomRight 容器里换算为
> `x=1724.7 y=973.5 177.3×58`。右侧白区是 prefab 的 `BG` 预留区，原作运行时
> 在这里挂 cellphone 层。2026-11-21 用户明确：**第一屏和第二屏是同一台手机**，
> 不要另造 754/1522 的几何。现已直接复用选曲页现值：**562 宽、中心
> (1609,618)、+5°**；`img_smartphone`、曲绘、标题/歌手、难度、播放键都走
> 同一套。背景也明确是**选曲背景同一个**，最终页不再画 `bg_common` 之外的
> 另一套背景。
> 704 的 754.3×1416.7 只是 prefab 静态容器，不作为第二屏的新几何。

- `BG`：1546×555，`bg_base_r20_wh` 白；`BGUnder` 152 高（`bg_base_r20u_yw`，
  这个 sprite 当前导出集里没有，先画同形状或再补导）。
- `DeckBase`：1080×624，`bg_base_three_r16_wh`，颜色 #DFDFEA / 80%。
- 5 个槽位：每个 **200×510**，间距 16，总宽 1064，水平居中于 DeckBase；
  每张用“空槽位”：
  - 卡面 `base_wh`，颜色 **#B0BDC8**（0.690, 0.741, 0.784），圆角 16；
  - 中间 `bg_base_circle_h96_wh` 72 圆、颜色 **#919FB0**
    （0.569, 0.635, 0.686）；
  - 上面 `icon_plus_h36_wh` 36。
  - 点击任意空槽位只弹一句“卡组编成功能尚未实现”，不伪造卡牌、不改综合力。
- 底部左：`OriginalMemberButton` 376.5×114（可先只画/禁用）、
  `AutoLiveToggle` 180.8×100、`UIPartsLiveModeButton` 176×72、
  `LiveSettingButton` 128×132。
- 底部右：`Decide` 354.5×116（`btn_round_h80_wh` + `eff_btn_round_h80_a70_wh`）。
- 综合力：`DeckTotalPowerView` 位置（相对 Deck 左下）显示
  `userSettings.deckPower`（设置 → 判定 → 综合力），不参与卡组计算。

### 13.3 交互

- **自动演出**：`UIPartsToggle` 开关（`bg_auto` + `Toggle` 72），状态存到
  `userSettings` 或局部；开启后 `startSession` 用 `autoPlay=true`。
- **MV**：`LiveModeButton` 一个 176×72 按钮循环可用的
  `LiveModeType`（3D/2D/原曲；按 `music-mv.json` / 本地 `mv/` 文件过滤），
  文字显示当前模式；不另做弹窗，和原作 `LiveModeButton` 的“按一下换下一个”一致。
- **Live 设置**：128×132 齿轮按钮，先映射到现有设置卡片（或只放一个“未实现”提示）。
- **开始**：播放 13.4 的确认特效，然后 `startSession`。

### 13.4 确认特效（1:1 的接法）

- 保留 `SoloTransitionEffectContent` 的 2 段：
  - `tap`：0.4167s，`light` α 0→1，`flare`/`back_light`/`tri_02` 在
    0.0167s、`flare_white` 在 0.333s；
  - `start`：clip 4s，但代码只等 **2.0s**；底部灯/三角/线环 0.0167s、
    `circle` 0.25s。
- 在最终确认页里把手机面板（或只它的屏幕矩形）作为遮罩层；`light`
  1532×2357 画在 `cellphone_mask` 1016×2221 里，按下 `Start` 时先播
  0.4167s tap，再进 2s start，最后切 live。**不要**再从 `确定` 直接铺全屏白。
- 资源已落地：
  - `assets/fx/live_circle.png`（tex_transition_03，`light`/`circle` 的软盘）
  - `assets/fx/live_flare.png`（tex_transition_01，flare/三角/播放图标的图集）
  - `assets/fx/live_glow_sheet.png`（tex_transition_02，4×4 三角光晕序列帧，不是
    `light` 本身；别把它整张贴到屏幕上）
  - `assets/fx/live_tri.png`（tex_sekai_v2_02，2×2 三角）
  - `assets/final/` 里是 DeckBase / BG / 空槽位 / 按钮 / 综合力图标等
    （`bg_base_three_r16_wh`、`bg_base_r20_wh`、`bg_base_circle_h96_wh`、
    `icon_plus_h36_wh`、`btn_round_h80_wh`、`eff_btn_round_h80_a70_wh`、
    `bg_auto`、`icon_setting_wh`、`icon_deckPower_*`、`frame_select_r0_wh` 等）。
- 仍未导出、需要补的：`bg_base_r20u_yw`、`frame_unselected_L`、
  `icon_setting`（先用 `icon_setting_wh` 代替）。

---

## 2026-11-21 落地：团体页签 + 顶栏收纳 + 1129/704 真实几何

这一轮把 §1~§3 里量到的数字真正写进了 `game/SongSelect.cpp`，并且把顶部那一排
按用户指定的交互重新分过工。**下面这一段是当前实现，不是计划。**

### 团体页签（`MusicSelectUnitTabList` 1117 / `MusicSelectUnitTab` 1118）

- 位置与尺寸照 prefab：**x=44、宽 136、上下各缩进 84**（所以 1080 高时是
  y 84..996）；底板 `bg_tab_playlist`（136×1024，两端各 68 的圆角）按**纵向
  三段**拉伸，别整张缩——整张缩会把上下两个圆角拉扁。
- 单元格 **112×100**，logo 112×112 居中，所以相邻两个 logo 会**重叠 12px**，
  这是 prefab 本来的样子，不是画错。
- 图标尺寸按原件：`すべて`/`その他` 是横条字（off 77×29 / 82×30，on 128×85 /
  128×86），六个团体是 112×112 的方 logo。**on 比 off 大**就是"选中"的官方提示，
  不要统一尺寸。
- 8 个页签的序号就是原作的 `CategoryTabType`：
  `0 全部 / 1 VS / 2 L/N / 3 MMJ / 4 VBS / 5 W×S / 6 25時 / 7 その他`。

### 团体怎么算出来的（原作没有这个 master 字段）

原作是**从歌曲演唱版本反推**的，不是曲库里的字段：

1. 取这首歌的 **セカイver**，没有就退到 original_song，再退到 virtual_singer
   （数据在 `music-vocals.json`，本来就已经加载给手机面板了）；
2. 在版本里找**第一个属于真实团体的歌手**，以它为准（"初音ミク + 星乃一歌"
   这种二重唱算 Leo/need，因为团体成员优先于 VS）；
3. 全是 VS → バーチャル・シンガー；
4. 全是外部角色（合作曲，`music-vocals.json` 里写成 `#35` 那种）→ その他。

`MasterMusicVocalCharacter.GetUnitType()`（RVA `0x6174F90` /
`0x614DB60`）就是 `characterType != 0 → none`，否则回查
`gameCharacters.unit`；上面那张 26 行歌手→团体表就是它的静态化。
实测 736 首分布：**VS 261 / L/N 108 / W×S 84 / VBS 83 / MMJ 82 / 25時 80 /
その他 38**。

### 列表行（`MusicSelectListCell` 1129）

- 列表列 **x=212、宽 912**，行 **895×144**（左右各留 8.5），选中 **895×230**。
- **行距 = 行高 = 144**，所以长大的卡片会**把上下相邻行推开** 43px
  (`(230-144)/2`)，不是压在上面。实现上 `pushRef()` 每帧从
  `slotHeights` 里取当前最高的那一行，按它的动画高度算 `extra`，于是推开
  跟卡片一起走同一条 0.1s 线性 tween，卡片缩回去时自动归零。
- 行内数字全部按 prefab（895×144 的坐标系，1:1）：
  `PlayLv` 96×96 圆心 x=48、`Jacket` 100×100 圆心 x=213.6、
  文字列 x=292.8、MV 徽章 78.98×48。
- MV 徽章的垂直位置改成**从底边量绝对值**：Neutral 距底 66.4 / 右 28.7，
  Highlight 距底 39.4 / 右 17.5。以前用"占行高百分比"，卡长到 230 之后就会漂。

### 手机面板（`ScreenLayerFreeLiveCellPhone` 704）

- **754.3 × 1416.7，中心 (1522, 742)，倾斜 -3°**（逆时针，屏幕 y 向下所以取负）。
  不再按窗口宽度塞成 46%，上下超出画布是故意的。

### 顶栏（这一块是**我们自己的**交互，不是原作）

- 一行 33k：`搜索`（占剩下的宽度）+ `排序` 下拉 + `筛选`(圆钮) + `菜单`(圆钮)。
- `刷新 / 音乐商店 / 猜歌 / 设置` 全收进**菜单**；原生菜单栏开着时前三个不重复出现。
- `分组依据` 收进 **筛选卡片**（`--select-filter` 可直接截图），卡片里另外放了
  一份**团体**单选——窄窗口下左侧页签会挤，这里能替代。
- 选曲页的**等级牌暂时隐藏**（`kShowLevelChip = false`，`--profile` 仍可打开资料卡）。
- 旧的四颗胶囊按钮被移除，但教学用的锚点 `rescan / store / guess / abssettings`
  全都改指**菜单按钮**，剧情不至于找不到东西。

### 无头验收

```
./build/cppsekai.exe --no-party --screenshot shots/a.png --screenshot-time 2.0
./build/cppsekai.exe --no-party --unit 3 --screenshot shots/b.png   # MMJ 过滤
./build/cppsekai.exe --no-party --select-filter --screenshot shots/c.png
```
`[select] list rebuilt: N visible of M (unit k)` 是过滤器自己的日志，切页签看它最直接。
