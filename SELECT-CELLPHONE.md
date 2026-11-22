# SELECT-CELLPHONE.md — 手机面板（704）的实测规格与贴图清单

> 2026-11-22。上一轮（[SELECT-UI.md](SELECT-UI.md) §3.6）只量了**矩形**，
> 这一轮把**每个零件的贴图名 / 颜色 / 字号**也挖出来了（`phone_style.tsv`，
> 317 个节点 / 260 条带样式的组件），并把手机面板里「我们自己画的」换成官方素材。
> 产物：`D:\Tools\rea\work\jp\phone_style.tsv`、`assets/select/` 里新增的贴图。

## 0. 一句话

原作确实有一台**倾斜的手机**，它就是 `ScreenLayerFreeLiveCellPhone`（prefab **704**）：
**754.3 × 1416.7**，中心 **(1522, 742)**（= 画布中心 + (562, +202)），
**−3.00°（Unity 逆时针 = 右端压低，也就是「向右倾」）**，左缘 1144.8、顶 33.6、
右缘 1899.1、底 1450.3（**底部切掉 370.4**，手机比屏幕高 31%）。
里面的零件**全部走官方 sprite**——没有一个是"手画的圆角矩形"。

## 1. 容器（铁数字）

| 项 | 值 |
|---|---|
| 尺寸 | 754.3 × 1416.7（占屏宽 39.3%、屏高 131.2%）|
| `anchoredPosition` | (562, −202) 相对屏幕中心，pivot/anchor 都是 (0.5, 0.5) |
| 旋转 | **`m_LocalRotation.z = −0.026176924, w = 0.99965733` → `2·atan2(z,w) = −3.00°`**（Unity 逆时针为正，负号 = 顺时针 = **右端压低/向右倾**）。⚠ 同一节点上的 `m_LocalEulerAnglesHint.z = +2.9999971` 是**过时的缓存值**，跟四元数矛盾，Unity 实际按四元数渲染 —— 别抄那个 hint。我们代码 `kTiltDeg = +3`（屏幕 y 向下，符号相反） |
| 屏内容宽 | **700**（`JacketContent` sizeDelta.x = −54.2 → 左右各内缩 27.1，即边框 3.6%/边）|
| 手机边框贴图 | `img_smartphone.png`（1034×1942，缩放 0.7295 = 754.3/1034）|

## 2. 零件表（位置是手机本地坐标，左上角原点；尺寸单位 = 754.3 宽的画布 px）

| 元素 | 位置 / 尺寸 | 贴图（官方名） | 颜色 |
|---|---|---|---|
| 曲绘 `Jacket` | (186.5, 92.5) 376×376 | `JacketImage`（RawImage + `JacketEdgeTransparency` 材质）| — |
| 曲绘遮罩 `JacketMask` | (24.5, 42) 700×700 | **`bg_base_r20_wh`**（50×50，`m_Type: 0` = Simple 拉伸）| 白 |
| 虚化拖尾 `UIPartsBlurMusicJacket` | 700×574，挂在遮罩顶边 | 同一张曲绘，模糊后拉伸填满 | 白 **alpha 76/255** |
| 曲名 `MusicTitle` | (81.7, 488.9) 440.2×50 | 文本 | 白 `#FFFFFF` |
| 歌手 `ArtistName` | (82.0, 543.9) 440.2×28 | 文本 | 白 |
| 音源 `VocalName` | (80.9, 580.9) 440.2×28 | 文本 | 白 |
| 定数标签 `UIPartsMusicDifficultyLabelWithIcon` | (303.0, 637.3) 144×40（图标 32 + 文字 74.5×24）| `bg_base_r12_wh`？ | 白 |
| 难度行 `Difficulty` | (89.2, 662.2) 576×133.38，5 × **96**，步距 **120**（间隔 24），**盘顶对齐**（`m_ChildAlignment = 0`），所以盘心 y = **710.2**（不是容器中心的 728.8）| — | — |
| ↳ 圆盘 `DifficultyImage` | 96×96 | **`btn_difficulty_{easy,normal,hard,expert,master}_{off,on}`**（112×112，外圈带光晕）| 白（贴图自带色）|
| ↳ 选中环 `Selected` | 136×136 | **`eff_btn_difficulty_selected_wh`**（136×136）| 白，`CanvasGroup.alpha` 0↔1 动画 |
| ↳ 高光 `FrontGlowCover` | 104×104 | **`eff_btn_circle_h80_a70_wh`** | 逐难度：EASY `#11DD77` / NORMAL `#33CCFF` / HARD `#FFCC00` / EXPERT `#FF4477` / MASTER `#CC33FF`，默认 alpha 0 |
| ↳ 禁用遮罩 `Cover` | 112×112 | 同一张难度图 | `(0,0,34,128)` 深藏青 50% |
| ↳ 锁 | 40×46 | **`icon_lock_wh`**（80×92）| 白 |
| ↳ 左右箭头 | 40×48 @ ±318 | `btn_arrow_triangle_gr` + `eff_arrow_triangle_pk`（高光）| 白 |
| 难度名 `DifficultyText` | **圆盘外·下方**：`anchoredPosition (-1.63, −66.77)`，182.3×40.27 的居中框 | 文本 fontSize **24** | 白（选中态也是白，`DifficultyLevel` 才是 46 号在盘内）|
| 定数 `DifficultyLevel` | **圆盘内**：`anchoredPosition (0, 0)`，200×50 居中框 | 文本 fontSize **46** | 白 |
| 主按钮 `PositiveButton` | (245.1, 807.4) **258×96** | 见 §3 | — |
| 次按钮 `NegativeButton` | (99.9, 797.1) 258×96 | `DecideButton` = **`bg_base_round_h80_wh`** 241×80 | `(0,0,34,128)` |
| 底部按钮条 `CommonButtonContent` | (24.5, 898.6) 702×100，按钮 80×80、**步距 188（80+108）**，`m_HorizontalLayoutGroup` spacing 108、`m_ChildAlignment = 4`（居中）；**但选曲时设置钮是 `SetActive(false)`，只剩两个 → 居中对排列，槽位 x = 281.45 / 469.45**，y = 948.55（不是 231 步距）| `btn_settingLive_lgr` 72×72 / **`btn_random_lgr` 60×52（按钮 `m_LocalScale` 1.2 → 实画 72×62.4）** / **`btn_vocalChange_lgr` 68×68** | 白（只有图标，没有底盘）|
| 评级 `RankBase` | 中心 (613.8, 555.2)，112×112 | **`btn_circle_h96_a20_wh`**（112×112，白 20% 烤进图里）+ `Rank` = **`icon_scoreRank_s_{a,b,c,d,s}`** 68×76（按评级预着色：A 紫 / S 粉）| 白 |
| 最高分徽章 `UIPartsHighScoreBadge` | 216.2×88 | `badge_h72_pk` + `badge_direction_h46_pk` | 白 |
| 换页按钮 `UIPartsTransitionButton` | 72×72 | `btn_circle56_wh` + `icon_arrow_dbl` | 白 |
| 换谱气球 `ChangeMusicScoreBalloon` | — | `bg_balloon_circle_h46_wh` + `badge_h46_gn` / `badge_direction_h46_gn` | `#444466` 底 + 难度色 |

**已知"别照着抄"的两处**（prefab 里是编辑器占位）：

1. 5 个难度盘在 prefab 里**全都在同一个位置**（`pos (0,0)`，尺寸 96），横向排布是代码做的
   —— 只能信 96 / 24 间隔 / 步距 120 这几个数。
2. 定数标签的 `pos.y = +747.4` 比手机上边缘还高 39，平时被 `cellphone_mask` 裁掉；
   别为它挪布局。

## 3. `PositiveButton`（确定）的真实结构

prefab 原文（`MainButtonContent > PositiveButton`，258×96）：

```
GlowCover    eff_btn_round_h80_a70_wh   112×104（96 高 + 5px 光晕边，全伸展 + sizeDelta 10）
ButtonImage  btn_round_line_h80_wh      106×96，m_Border X=52 → 胶囊
             m_Color (119,238,221) = #77EEDD
Text         fontSize 32，m_fontColor (0.4667,0.9333,0.8667) = **#77EEDD**（跟描边同色，不是白！）
Cover        bg_base_round_h80_wh       88×80，m_SizeDelta (-16,-16) → 内缩 8
             m_Color (0,0,34,128)
```

也就是说 7.1.0 的「确定」是**薄荷色描边胶囊 + 外发光**，不是实心胶囊：
`btn_round_line_h80_wh` 中心 alpha 只有 **26/255**（10% 淡色），描边才是实心的。
2022 年的老截图里那颗是实心薄荷胶囊 —— 那是旧版 UI，别拿它当依据。

## 4. 我们改了什么（2026-11-22）

| 位置 | 之前 | 现在 |
|---|---|---|
| 确定按钮 | `ui::capsuleDraw` 手绘**实心**薄荷胶囊 | 官方 `eff_btn_round_h80_a70_wh`（光晕）+ `btn_round_line_h80_wh`（描边，染 #77EEDD）+ 按压时 `bg_base_round_h80_wh`（深藏青 50%）|
| 评级徽章 | 手绘 32% 白圆 + HUD 的 `rank_char_*` | 官方 `btn_circle_h96_a20_wh`（20% 白）+ `icon_scoreRank_s_{x}` 68×76 |
| 难度高光 | 无 | 官方 `eff_btn_circle_h80_a70_wh`，只亮当前难度，颜色用官方逐难度色 |
| 难度配色 | 目测（75,207,138 / 90,140,255 / 242,150,77 / 239,90,102 / 181,91,255） | prefab 里的官方值（见 §2 表）|
| 难度名 | 挪到盘内（数字上方、名字下方）—— **错了** | 按 prefab 放回**盘外下方 66.77px**，fontSize 24、182.3 宽的居中框（这轮改回来）|
| 难度行 y | 728.8（容器竖直中心，猜的） | **710.2**（`m_ChildAlignment=0` 顶对齐 → 容器顶 662.2 + 48）|
| 底部两钮 x | 231 步距居中（拿容器宽除以 3 猜的），后来又改成"占 2、3 号槽"375.5 / 563.5 | **281.45 / 469.45**（**居中对两个**：设置钮在选曲屏是 `SetActive(false)`，HorizontalLayoutGroup 跳过 inactive 子物体，于是剩下两颗重新居中；见 §7）|
| 曲名/歌手/音源 | 34 / 26 / 26 px，且把 TMP 盒顶当成基线 | **36 / 28 / 28 px**，且 y 用 TMP 盒的**竖直中心**（513.9 / 557.0 / 594.9）|
| 确定按钮文字 | 白 34px | **#77EEDD 32px**（prefab 的 m_fontColor）|
| 手机倾斜 | `kTiltDeg = −3`（右端上翘，抄了过时的 Euler hint） | **`kTiltDeg = +3`（右端压低 = 向右倾）**，从四元数 `2·atan2(-0.026176924, 0.99965733)` 推的 |
| 曲绘 | r12 + 2px 白描边（手绘猜测），后来又改成 r20 | **方角，连描边都没有**：`Jacket`(299) 376² 是 `JacketMask` 的**兄弟**不是子物体，r20 遮罩裁不到它；它上面只有 `JacketEdgeTransparency` 材质（`Sekai/UI/EdgeTransparency`，`_EdgeWidth 0.005` / `_EdgeAlpha 0.1`）= 宽度的 ~0.5%（376px 上约 1.9px）淡到 10% 的**边缘渐隐**，不是圆角（见 §7）|
| 底部图标 | `assets/select/shufflebutton.png` / `singer.png` | 官方名 `btn_random_lgr`（60×52，按钮 scale 1.2 → **72×62.4**）/ `btn_vocalChange_lgr`（**68×68**）|
| 虚化拖尾 | 三张同心缩放假装模糊（60/100/130 三档 alpha） | **一张 700×574**，alpha **76/255**，竖直方向按 RectMask2D 的 `m_Softness (0,389)` 做上下渐隐（见 §7）|

**没动**（已经对得上）：容器 754.3×1416.7 / 中心 (1522,742)、曲绘 376×376 @(186.5,92.5)、
曲名/歌手/音源 440.2 宽 @81.7、难度行 96+24 @89.2/662.2、确定 258×96 @(245.1,807.4)、
评级 112 @613.8/555.2。

**这轮对几何的复验**（2026-11-22 第二遍，全部按 `ms_phone.rects.json` 重算）：
用无头 1920×1080 截图量了 5 个难度盘的中心 —— EASY (1276,727) / NORMAL (1395,733.5) /
HARD (1514,740) / EXPERT (1633,746) / MASTER (1753,752)，行斜率 **+3.01°**、步距 119.3px、
盘径 95px（=96），和 prefab 的 96 / 120 / +3° 全部对上；曲绘卡片量到 391×391（旋转后
应 395.2）中心 (1534,313) vs 期望 (1541.8,314.6)。
（2026-11-22 第三遍：那次写的"含 20px 圆角吃掉四角"是错的 —— 曲绘**没有圆角**，
上面那个 391 是旋转后 bbox 算法本身的误差，不是圆角。）

## 5. 还差什么（没做，写下来免得下次重新推）

- **定数标签**（`UIPartsMusicDifficultyLabelWithIcon` 144×40）我们没画 —— 手机盘里
  难度已经由 5 个盘表达了，原作这块是给"当前难度 Lv."用的。
- **左右箭头**（40×48 @±318）没画：我们直接点盘。
- **最高分徽章**（`badge_h72_pk`）没画：我们只用评级圆盘。

## 6. 复现

```bash
cd /d/Tools/rea/work/jp
# 组件级样式表（贴图名 / 颜色 / 字号）—— 需要 AssetRipper 常驻 + GameData 已加载
python prefab_style.py 704 phone_style.tsv
# 矩形（手机本地坐标）
python ms_geom.py 704 ms_phone && python hdr_rects.py ms_phone.json
# 树 + 布局字段（spacing / 对齐 / sprite / 字号 / RectMask2D softness）—— §7.4 的表就是这么来的
python /d/Dev/CppSekai/.workbuddy/tools/prefab_layout.py cell1129.json
# 切片 border（判断圆角用）
python -c "import json;d=json.load(open('as_out/btn_round_r16_wh.txt'));print(d['m_Border'])"
```

`prefab_style.py` 已随仓库放在 `.workbuddy/tools/`。

样张：[`docs/select-cellphone-1920.png`](docs/select-cellphone-1920.png)
（无头 1920×1080 截图，`.\build\cppsekai.exe --no-party --screenshot docs\select-cellphone-1920.png --screenshot-time 2.0 --width 1920 --height 1080`）。

## 7. 第三遍复验：四个"到底是不是这样"的问题（2026-11-22）

用户问的四件事，逐条给证据。prefab 704 / 1129，工具见 §6；RVA 来自
`dump/dump.cs`，反汇编用 `.workbuddy/tools/il2cpp_disasm.py`（arm64 libil2cpp）。

### 7.1 专辑封面有没有圆角 → **没有**

节点树（`JacketContent` 下）：

```
JacketContent
  Positions
  JacketMask (262)   700x700 @ (24.5, 42)   Mask(m_ShowMaskGraphic=0) + bg_base_r20_wh
    Jacket (73)      700x574  (anchors 0,1-1,1)  RectMask2D softness (0,389)
      UIPartsBlurMusicJacket (231)
        JacketThumbnail (190)  CustomRawImage  m_Color a=0.29803923 → 模糊层
  Jacket (299)       376x376 @ (186.55, 92.45)  ← 清晰曲绘，是 JacketMask 的**兄弟**
```

`Jacket(299)` 不在 `JacketMask(262)` 里面，所以那个 `bg_base_r20_wh` 遮罩**裁不到它**。
清晰曲绘自己身上的只有 `JacketImage(184)`：

| 字段 | 值 |
|---|---|
| `m_Script` | 9310 = `CustomRawImage` |
| `m_Material` | `{m_FileID: 53, m_PathID: 1}` = 材质 **`JacketEdgeTransparency`** |
| shader | `Sekai/UI/EdgeTransparency`（`uiedgetransparency`，collection 6874）|
| `_EdgeWidth` / `_EdgeAlpha` | **0.005 / 0.1** |

即"宽度 0.5%（376px 上约 **1.9px**）线性淡到 10% alpha"的**边**渐隐，不是圆角。
我们的代码现在 `addJacket(..., 0.0f)` = 方角。

顺带：`JacketMask` 的 `bg_base_r20_wh` 是 `m_Type: 0`（**Simple，不是九宫格**），
50×50 拉到 700×700 后它自己的圆角弧会跟着放大到 **~259px**（实测 sprite 弧半径 ≈18.5px，
×14）。它只作用于模糊层，而模糊层本身 alpha 只有 76/255，所以这件事对观感影响很小 ——
我们**没有**在模糊 quad 上加圆角（加了就是猜），只按下面的 softness 做竖直渐隐。

### 7.2 模糊填充多大 → **700×574，alpha 76/255，竖直渐隐**

| 项 | 值 | 来源 |
|---|---|---|
| quad | **700 × 574**，挂在 `JacketMask` 顶边 → 手机本地 **x 24.5..724.5, y 42..616** | `Jacket(73)`：anchors (0,1)-(1,1)、sizeDelta (0,574)、pivot (0.5,1) |
| alpha | **0.29803923 = 76/255** | `JacketThumbnail(190)` 的 `m_Color.a` |
| 只有一张 | 是 | `Blur()`（RVA **0x4D1AB88**）：`new RenderTexture(源图宽, 源图高)` → `new Material(uigaussianblur)`（`Sekai/UI/UIGaussianBlur`，`_SamplingDistance = 2.8`）→ `RawImage.set_texture`，即把**同一张曲绘模糊后拉伸填满**这个 quad（长宽比故意变形）|
| 竖直渐隐 | `RectMask2D` 的 `m_Softness = (0, **389**)`、`m_Padding = 0` | `Jacket(73)` 上的 script 2292 |
| 横向 | softness 0 → 左右是**硬边** | 同上 |

`389 > 574/2`，上下两个斜坡重叠，所以 alpha 沿竖直方向是**三角**形：两边 0，
中间峰值 `(287/389) × 76 ≈ **56/255**`。曲绘上缘（距顶 50.5px）只有 ~10/255，
曲绘正中最亮，下缘外 147px 处还有 ~29/255 —— 这就是"光晕"的真实分布。

实现：`AddImage` 只能给一个颜色，所以我把 700×574 拆成上下两半，**改它刚压进去的
4 个顶点 alpha**（TL/TR/BR/BL 顺序），得到上 0→56、下 56→0 的线性斜坡。
旧的"三张同心缩放 60/100/130"是编的，已删。

### 7.3 随机 / 切换歌手要不要相对居中 → **要，居中成一对**

`CommonButtonContent.SetupButton`（RVA **0x62459D4**）逐条：

```
randomButton.SetActive(info.ShouldShowRandomButton);       // EnableButtonInfo +0x13
randomButton.enabled      = info.EnableRandomButton;       // +0x10
changeVocalButton.enabled = info.EnableChangeVocalButton;  // +0x11
settingButton.SetActive(info.EnableSettingButton);         // +0x12
```

（`0x50E1548` = `CustomButton.SetActive` → 内部 `gameObject.SetActive`；
`0xAD619F0` = `Behaviour.set_enabled`。）

`SetupCommonButtonContent`（RVA **0x655574**）把自己的 bool 参数原样当
`EnableSettingButton` 传下去，而**两个进入选曲屏的入口都传 false**：

| 调用点 | 汇编 | 结果 |
|---|---|---|
| `SetupMusicSelect` (0x6555034) @ 0x65551AC | `mov w2, wzr` | enableSetting = **false** |
| `SetMusicData` (0x6555914) @ 0x655559BC | `mov w2, wzr` | enableSetting = **false** |
| `SetupFinalConfirm` (0x6555620) @ 0x655572C | `mov w2, #1` | true（只有最终确认页）|

设置钮的 GameObject 被关掉 → `HorizontalLayoutGroup` 建 `m_RectChildren` 时**跳过
inactive 子物体** → 剩下 随机 / 切换歌手 两个**重新居中**：

| 情况 | 3 颗（最终确认页） | **2 颗（选曲屏）** |
|---|---|---|
| 总宽 | 3×80 + 2×108 = 456 | 2×80 + 108 = **268** |
| 起点 | (702−456)/2 = 123 | (702−268)/2 = **217** |
| 条内中心 | 163 / 351 / 539 | **257 / 445** |
| 手机本地 | 187.45 / 375.45 / 563.45 | **281.45 / 469.45**（步距 188）|

交叉验证：`SettingButton` 里存的 `anchoredPosition.x = **163**`，正好等于 3 颗时的第 1 槽
中心 —— 说明这组数字的模型是对的。**不要**留空第 1 槽。

图标本身：`ShuffleButton` 的 `m_LocalScale = 1.2`，它下面的 `Base` 是 60×52 →
实画 **72×62.4**；`ChangeVocalButton` 的 `Base` 是 68×68。

### 7.4 选曲列表的几何 → prefab **1129**（不是 1121）

先在 prefab 702 里确认用的是哪支 cell：`MusicSelectListView.listViewItemPrefab =
{m_FileID: 21, m_PathID: 506}`；该文件第 21 个外部文件是 bundle
`db02094472f894a81a8d59032b4fe140`，`506` 在那里是 **`MusicSelectListCell` 的
MonoBehaviour**（字段类型是 `ListViewItem`，即组件引用）。所以是 **895×144 / 选中 230**
的 `MusicSelectListCell`，**不是** 912 宽的 `MusicSelectSingleListCell`。

两个内容块都是**中心锚定、比 144 的 cell 大**：`Highlight` 895×230（y_cell −43..187）、
`Neutral` 895×192（−24..168）。所以下表都换成"**距 cell 中心（y_cell 72）的偏移**"：

| 元素 | 紧凑行（Neutral） | 选中卡片（Highlight） |
|---|---|---|
| 等级盘 | **80×80** @ x 26.3..106.3（中心 66.3），TMP **36** | **96×96** @ 19.2..115.2（中心 67.2），TMP **48** |
| 等级牌 `txt_musicLv` | 无 | **102×38** @ x 16.2，y_cell 10..48（压在盘上缘）|
| 曲绘缩略 | **96×96** 中心 (170.7, −0.7)，**方角**（`JacketThumbnail` 的 RawImage 没有材质；只有它下面的 loading 占位是 r10）| 无（曲绘在手机面板里）|
| 曲名 | TMP **32** @ x **243.6**（不是 293.4，见下），y_cell 中心 47.5 | TMP **40** @ x **231.8**，y_cell 中心 0.3 |
| 频谱（曲绘槽位）| 无（这一行放曲绘）| **6 根 8×72**，x 133/147/161/175/189/203，底边 y_cell **107.7**、pivot 在底，纯色 **#444466**；`Update` 让 `scale.y = clamp(\|20·log10(level)\|/32)`，< 0.02 收起 |
| 歌手 / 音源 | **没有**（Neutral 只有 Title）| TMP **28** @ x 236.7 / 237.7，y_cell 中心 48 / 88.2 |
| 通关菱形 | **28×28**，组 230×29.1 @ x 236.4，y_cell 88.1..117.1；`spacing 0` + `padding L1` → 从 x **237.4** 起紧贴排列 | **38×38**，组 @ x 233.8，y_cell 138.7..167.7；`spacing 1` + `padding 0` → 从 x 233.8 起、步距 **39** |
| MV 角标 `MVMode` | 439.3×100，`MiddleRight`/spacing 8/反序，子 **79×48**；右缘内缩 **28.7**、中心离行底 **42.4** | 右缘内缩 **17.5**、中心离卡片底 **39.4** |
| 下划线 `UnderLine` | `base_wh` **856.64×4**，白 **20%**，x **19.2..875.8**，y_cell **142..146**（骑在 cell 底边）| 无 —— `UnderLine` 挂在 **Neutral** 下（不是 Base），所以只有紧凑行有 |
| 卡片底 | 无 | `bg_base_r20_wh` 九宫格、白 **60%**、圆角 **20**、**没有描边** |

曲名的 x 是 prefab 里**自相矛盾**的一处，单独记一下：带 RectMask2D 的显示框
Title 在 x **293.4**，它下面的 TMP 在 **243.6**（差 49.8），而 293.4+617 会顶出
895 的单元格。三种旁证都指向 243.6：兄弟单元格 MusicSelectSingleListCell(1121)
的框与 TMP 同在 238、本 prefab 的 Highlight 分支两者同在 231.8、而且 243.6 距曲绘
右缘 218.7 只有 24.9（Highlight 是 13.1）。按 293.4 画会在曲绘和曲名之间留 74.7px 的
空洞（2026-11-22 用户报的就是这个）。

两处"prefab 里是占位、只能信尺寸"：菱形和 MV 角标的 `anchoredPosition` 全是 (0,0)
（layout group 排的）。另外菱形组里第 5 个子物体是一个 8×28 的空 `Padding` 垫片
（7 个子物体里排第 5），它的 `m_IsActive` 不在序列化的组件数据里、无法从 prefab 断定
开还是关；我们只有 5 个难度，就按"等距 5 颗"画（若它开着，第 5 颗会多 8px 间隔）。
