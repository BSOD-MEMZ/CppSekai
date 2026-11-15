# 多人演出的 UI（2026-11-15，REA 逆向）

> 免责声明同仓库其他文档：本文是**自己玩、自己看**的对照记录，素材与数据权利归原权利人，
> 不随代码分发、不对外发布。
>
> 结论先给：**多人 HUD 不是另做一套，而是把单人的 `LiveFrontView` 换掉三个零件
> （分数面板 → 团队分条、cutin → 多人 cutin、tap 特效 → 数组），再加两块新的
> （每人一个头像块 + Fever 条）。** 头像块里自带一套完整的 ComboView，
> 这才是「多人一眼看上去信息密度大」的真正来源。

---

## 1. 单人 vs 多人的根：两个 `LiveFrontView`

字段逐个对齐（都是 `LiveViewBase` 子类，prefab 里挂成一棵树）：

| 单人 `LiveFrontView`（11915） | 多人 `MultiLiveFrontView`（15384） | 说明 |
|---|---|---|
| `liveRoot` / `fade` / `deadMask` | 同 | 根、淡入淡出、死亡黑幕 |
| `laneView : LaneView` | `lane : LaneView` | 同 |
| `musicInfo : MusicInfoView` | 同 | 顶部曲名条 |
| `backgroundRenderer` | 同 | 背景 |
| `judgmentView` / `judgmentDescriptionView` | 同 | 判定字 + 判定说明 |
| `comboView` | 同 | **我方**连击（其他人的在头像块里） |
| **`scoreView : ScoreView`** | **`scoreView : MultiScoreView`** | 单人分数面板 → 团队分条 |
| `lifeView : LifeView` | 同 | 生命条（含暂停/跳过键） |
| `liveResultView` | 同 | 曲末判定/曲终演出 |
| **`cutinManager : CutinManager`** | **`cutinManager : MultiCutinManager`** | 卡面切入（多人要按成员区分） |
| `skillView : SkillView` | 同 | 技能显示 |
| `screenEffectView` / `effectRoot` / `effectCamera` | 同 | 全屏特效 |
| `noteShowRateView` | 同 | note 显示率 |
| `tapEffect : TapEffectView` | **`tapEffectArray : TapEffectView[]`** | 多人要能同时放多个 |
| `countdownView` / `autoLabel` / `consecutiveAutoLiveView` | **没有** | 单人连续 live 专用 |
| — | **`feverView : MultiFeverView`** | 团队 Fever 条 |
| — | **`multiThumbnailViewGroup : MultiThumbnailViewGroup`** | 每人一个头像块 |
| — | **`partyInfo : MultiLivePartyInfo`** | 队伍信息（成员/难度/自定义房设置） |

多人独有的运行时状态：`_tournamentScoreCalculateMultiThumbnailPosition`（比赛模式的头像位置）、
`_collaborationModeState`（协作模式）。

`MultiLiveType`（4932）只有两个值：`MultiLive = 0` / `CheerfulLive = 1`（チアフルカーニバル，
对战）。CC 走的是另一套 `CheerfulCarnival*` 类（见 §5）。

---

## 2. 每人一个头像块：`MultiThumbnailView`（prefab 1224）

### 2.1 节点树（`ms_geom.py 1224`）

```
MultiThumbnailView
├ ComboRoot
│ └ ComboView                       ← 就是一整套单人连击（下面全在）
│   └ root
│     ├ EffectNumberRoot (Number1..4)   特效层数字
│     ├ APEffectRoot    (Number1..4)    ALL PERFECT 专用数字
│     ├ NumberRoot      (Number1..4)    本体数字
│     ├ Combo                            "COMBO" 字样
│     └ APCombo                          "AP" 字样
├ ThumbnailRoot
│ └ ThumbnailTurnRoot               ← 翻转根（Fever 时整块翻面）
│   ├ dying                         「濒死」贴图
│   ├ death                         「死亡」贴图
│   ├ text_feverOK                  「FEVER OK」
│   ├ Thumbnail                     卡面缩略图
│   └ frame_player_multi            头像框
└ YouIcon                           「YOU」标记
```

⚠️ **几何全是 0**：这一棵和 `ScoreView`/`LifeView`/`ComboView` 一样是
`Transform + SpriteRenderer`（世界坐标），prefab 里没有 rect（见 `HUD-UI-AUDIT.md` §1 的前提）。
位置只能靠截图量，或者从 `MultiThumbnailViewGroup` 的代码排版反推。
**尺寸只能拿 sprite 原始像素**：`frame_player_multi`、`text_feverOK`、`thumbnail` 都在
`jp/sprite-geometry.csv` 里。

### 2.2 字段与语义（15357）

```csharp
Transform thumbnailRoot, thumbnailTurnRoot;
SpriteRenderer death, dying, frame, thumbnail, youIcon;
GameObject     feverReady;
ComboView      comboView;
SpriteRenderer[] spriteRenderers;      // 整块一起淡入淡出用
int   prevCombo;
float currentProgress, requireFeverProgress, requireSuperFeverProgress;
bool  isFeverTime;
MultiThumbnailView.State state;
string UserId;
```

方法：

| 方法 | 做什么 |
|---|---|
| `Setup(MultiLivePartyMember)` | 用队伍成员数据初始化（卡面、框色、训练态） |
| `Setup(userId, cardId, Color frameColor, bool isSpecialTraining, bool viewSubInfo)` | 低层重载 |
| `UpdateScore(ref LiveScore, MultiLiveType, bool useJump)` | 刷新这个人的分/combo/状态 |
| `UpdateRankLiveScore(in RankLiveScoreInfo)` | 比赛模式的排名分 |
| `StartMultiLiveFeverTime()` / `TurnFever(v)` / `TurnSuperFever(v)` / `ReturnFever(v)` | **Fever 时翻面动画** |
| `PlayPraiseAction()` | 「点赞」动作（别人发了表情） |
| `JumpAction(float)` | 分数增长时跳一下 |
| `SetAlphaToAllView(float)` | 整块透明度（掉线/退出用） |
| `UpdateFeverStatus(LiveScore)` | 内部：根据分数算自己够不够 FEVER / SUPER FEVER |

### 2.3 一块头像块承载的信息

对照 `MultiLivePartyMember`（11219），一块里其实塞了：

- 卡面（`CardId`）+ 是否特训（`IsTraining`）
- 头像框（`PlayerFrameId` + `PlayerFramePartsLayout`）
- 名字（`UserName`）—— 注意：**字段里有名字，但 `MultiThumbnailView` 的节点树里没有 Text**
  （只有 `text_feverOK` 这张固定字，内容是 **"OK!"**），说明**演奏中不显示玩家名**
- 「YOU」标记（自己那一块）
- 独立 combo（`ComboView`）+ 独立分数
- 存活状态：`dying`（血量低）/ `death`（血空，且 `deadMask` 会压整屏）
- Fever 状态：`feverReady`（够了）+ 翻面（进入 FEVER）

**这块是我们差得最远的地方**：我们连头像都没有。

---

## 3. 团队总分：`MultiScoreView`（prefab 1226）

节点树（`ms_geom.py 1226`，尺寸同样全是 0）：

```
MultiScoreView
├ ScoreSkill
└ ScoreBase
  ├ Gauges
  │ ├ ScoreGauge1 .. ScoreGauge5     ← 五段！不是一个裁剪条
  ├ lineA / lineB / lineC / lineS（各带 Line + 字母）
  └ Number → NumberRoot → Number1..8 （8 位数字）
```

**这是多人分条和单人的本质差别**：单人 `ScoreView` 是「一条 `ScoreGauge` + 裁剪填充」，
多人是**五段独立伸缩的 gauge**（`scoreGaugeSpriteRenderers[]` 就是这五段）。
五段对应 S/A/B/C 之间的区间，`CalculateMultiGaugeProgress(LiveScore[])` 一次算出五个比例。
我们 `Hud.cpp` 用的是单条裁剪 —— 单机够用，但要做多人分条得改成段式。

字段（15353）与单人 `ScoreView`（1219）几乎逐项对应：

```
SpriteRenderer[] scoreGaugeSpriteRenderers    ← 注意是数组（多人条是分段拼的）
SpriteRenderer   rankSpriteRenderer, rankTextSpriteRenderer
Sprite[]         rankSprites
Transform[]      gaugeRankSprites             ← 刻度字母
NumberView       number, outlineNumber
Transform        addScoreRoot; NumberView addScoreNumber, addScoreOutlineNumber
List<Tween>      tweens
ScoreGaugeCalculator scoreGaugeCalculator
```

方法：

| 方法 | 做什么 |
|---|---|
| `Setup(MultiLiveBootData)` | 用 `MultiLiveScoreData`（S/A/B/C 四档）建 calculator |
| `ChangeScoreCalculator(ScoreGaugeCalculator)` | 换比例尺（**这个就是我们 `RESULT-SUMMARY.md` 里踩的坑**：条的比例尺要换） |
| `UpdateScore(ref LiveScore[] multiScores, ScoreRank, int addScore)` | 传进来的是**全队分数数组**，它自己合计 |
| `SetupGaugeRankPosition(Transform, float)` | 按比例摆刻度字母 |

`MultiLiveScoreData`（5388）里就四个 int：`S / A / B / C`，`MAX_SCORE_FACTOR = 5`
（段位/倍率上限）。

对照 `CheerfulCarnivalScoreView`（15338）—— 对战用的**两条**条：

```
SpriteRenderer selfTeamScoreGauge, opponentTeamScoreGauge, rankSpriteRenderer;
Sprite[] rankSprites;
NumberView number, outlineNumber;
Transform addScoreRoot; NumberView addScoreNumber, addScoreOutlineNumber;
float GAUGE_ANIMATION_DURATION = 0.15;   // 条增长动画 0.15s
static readonly float ScoreGaugeRankPositionLeft/Right;
static readonly Vector2 BaseScoreGaugeSize;
CalcTotalLiveScore(ref LiveScore[] multiScores)   // 队内合计
```

---

## 4. Fever：`MultiFeverView`（15351）

多人独有的一块，单人没有。

```csharp
static readonly float FeverRate, SuperFeverRate;   // 阈值比例（两档）
Transform feverChanceRoot;
SpriteRenderer feverLaneText, superFeverLaneText;
SpriteRenderer[] feverChanceFrames;                // 待机灯的格子
Renderer[] feverGaugeRenderers, feverGaugeFlashRenderers;
MaterialST[] feverMaterialSTs;                     // UV 滚动（材质用 ST）
GameObject feverChanceCutin, feverCutin, superFeverCutin;
GameObject feverFrame, superFeverFrame, feverFrameEffect;
GameObject feverGauge, feverGaugeFlash;
float requireFeverProgress, requireSuperFeverProgress, FrontCameraSize;
```

方法：`StartProgress()` → `UpdateProgress(progress, FeverMode)` → `StartFeverMode(FeverMode)` /
`EndFeverMode()`，中途 `UpdateFeverChanceFrame()` / `FadeInFeverLineText()`。

`FeverMode`（15313）：`None = 0` / `Fever = 1` / `SuperFever = 2`。

两条通道会同时被驱动：
- **团队层面**：`MultiLiveFrontView.StartMultiLiveFeverCharge()` /
  `UpdateMultiLiveFeverCharge(progress, mode)` / `StartMultiLiveFeverMode(mode)` / `EndMultiLiveFeverMode()`
- **个人层面**：`MultiThumbnailView.StartMultiLiveFeverTime()` + `TurnFever/TurnSuperFever`

也就是说**Fever 是「全队攒一条、每个头像块各自翻面表示自己也进了」**，不是各自独立攒。

---

## 5. 另外三块

### 5.1 技能文本：`multiSkillTextViewGroup`（prefab 1225）

单人对应 `skillTextViewGroup`（1216）。多人版本按成员分组：
`MultiSkillTextViewGroup` + `SkillTextViewThumbnail`（带头像的技能条）+ `SkillTextViewAnimation`。

### 5.2 点赞：`MultiPraiseTextView`（15352）

```csharp
SkillTextViewThumbnail thumbnail;
SkillTextViewAnimation animation;
MultiLivePartyMember member;
```

配合 `MultiThumbnailView.PlayPraiseAction()` —— 点赞是「头像块动 + 一条带卡面的文字」，
共用一个 `SkillTextViewAnimation`，不是独立特效系统。

### 5.3 多人 cutin：`MultiCutinManager`

单人 `CutinManager` 的多人版，差异在卡面来源（`MultiLiveFormationData` 里的
`CardId/CardMasterRank/HairCostumeId/BodyCostumeId/AccessoryCostumeId`）。
`CutinView` 本身是共用的：`defaultCutinRight/Left` + `yellowCutinRight/Left` 四套，
`LoadCardData(CutinCardData)` 换卡面。

---

## 6. 我们现在是什么

`game/PartyScreen.cpp`，`drawPartyScores()`：

```
左下角一列自绘卡片（430 宽、48 高一格、圆角 10）
每格：名字（ellipsize 到 190px）+ 分数（右对齐，24px）+ combo（18px，"N combo"）
没有：头像、头像框、YOU 标记、独立 combo 数字、dying/death、Fever、点赞、团队分条
```

`platform/Party.cpp` 是自研的 Win32 IPC 房主/成员模型（`PartyPlayer{name, score, combo, slot, host}`）。

**结构上的差距**：

| 原作 | 我们 | 影响 |
|---|---|---|
| 头像块放**右上/中上**一片，每人一个卡面 | 左下角文字列表 | 观感差距最大的一条 |
| 每人块内自带 ComboView（大字连击） | 一格小字 "N combo" | 多人演奏时根本看不清 |
| 团队分条 `MultiScoreView` | 无（只有自己那块 ScoreView） | 原作顶部分数是**全队合计** |
| Fever 条 + 全员翻面 | 无 | 多人最大的节奏点缺失 |
| dying / death + 全屏 deadMask | 无 | 队友血空没有任何提示 |
| 点赞 | 无 | 社交反馈缺失 |

**但也要承认**：这四块（头像/Fever/团队条/点赞）在单机自娱场景里价值不高，
而且体积不小（`MultiThumbnailView` 一个 block 就是 3 个 SpriteRenderer + 一整套 ComboView）。

---

## 7. 如果要补，按性价比排

1. **头像块（最值）**：哪怕是纯 ImGui 自绘（圆角卡面 + 边框 + 名字缩写），
   **接在左上分数面板下面横着排一排**（2026-11-15 按实机观感定的，见 §10），
   每人一格、带独立 combo 大字。数据 `PartyPlayer` 已经全有了
   （name/score/combo），只差一个卡面 id。
2. **团队分条**：把 `ScoreView` 的数字换成 Σ 全队，条的比例尺按
   `ScoreGaugeCalculator.Create(s,a,b,c)` 那套重算（别复用单人刻度）。
3. **dying/death**：血空/接近空时给那格一个红色遮罩 + 全屏 `deadMask` 压一下。成本极低。
4. **Fever**：需要协议层加一条「全队进度」同步消息，成本最高，最后做。

---

## 8. 复现

```bash
cd /d/Tools/rea/work/jp
# AssetRipper 要先起（run_in_background）+ LoadFolder（见 RE.md §4.2）
python arip_search.py MultiThumbnailView      # → PrefabHierarchyObject 1224
python ms_geom.py 1224 ml_thumb               # 树有了，尺寸全是 0
python ms_geom.py 1226 ml_score
python ms_geom.py 1225 ml_skilltext
python q.py MultiLiveFrontView LiveFrontView MultiThumbnailView MultiScoreView MultiFeverView
```

| 产物 | 位置 |
|---|---|
| 头像块树 | `jp/ml_thumb.txt` / `.json` |
| 团队分条树 | `jp/ml_score.txt`（本次已导） |
| 字段原文 | `python q.py -f <类名>` |

---

## 9. 素材已经 dump 好了（2026-11-15）

`build/multi_ui/`（**build/ 已 gitignore，官方素材绝不入库**）：

| 内容 | 文件 |
|---|---|
| 32 张 sprite | 头像框 `frame_player_multi` / `thumb_mask` / `label_color_thumbnail` / `text_you_gn` / `text_feverOK` / `effect_playerScoreDanger_multi` / `frame_multi_black`；COMBO `bg_combo_pl` / `eff_combo` / `number_combo_154px_0..9_pr`；分条 `gauge_bar_ingame_multiLive_s_wh` / `bar_scoreGauge_multi` / `bar_scoreGauge_mask` / `bg_scoreGauge 1` / `ScoreRank_Mark_S|D`；Fever `gage_fever_lane` / `line_(super)fever` / `text_line(super)Fever` / `itemBox_superFever(_open)` |
| prefab 树 | `ml_thumb.txt`（1224）/ `ml_score.txt`（1226）/ `ml_skilltext.txt`（1225）/ `ml_fever.txt`（1232） |
| 索引 | `INDEX.md`（每张的用途 + 像素尺寸） |
| 一览图 | `contact_sheet.png` |

重新生成：

```bash
python .workbuddy/tools/dump_multi_ui.py            # 默认读 jp/tex_all_sprite
```

脚本只做「按名字复制 + 拼 contact sheet + 写索引」，源是 `tex_all_sprite`（2808 张全量导出）。
缺的两张：`dying` / `death` 节点存在但贴图名不同（不在 sprite 全量里，可能是运行时从 atlas 取），
索引里标了 `**缺**`。

**尺寸还是拿不到精确值**：`INDEX.md` 给的是贴图原始像素，prefab 里的 `sizeDelta` 全是 0。
位置/缩放只能量 1920×1080 截图（`HUD-UI-AUDIT.md` §1 的前提同款问题）。

### 能直接用的几个数（贴图像素，非显示像素）

| 元素 | 贴图尺寸 | 说明 |
|---|---|---|
| `frame_multi_black` | **156×156** | 头像块死亡态底图 → 头像块边长大概 156 |
| `thumb_mask` | 24×26 | 小，是 9-slice 边距不是整块 |
| `label_color_thumbnail` | 40×40 | 队色小块 |
| `text_you_gn` | 78×40 | "YOU" 字样 |
| `text_feverOK` | 64×30 | 贴图内容是 **"OK!"** |
| `number_combo_154px_*` | 78~117 × 144~151 | 名字里就写了 154px 字号 |
| `bg_combo_pl` / `eff_combo` | 144×30 / 197×79 | 与我们单人 HUD 同源 |
| `bar_scoreGauge_multi` | 609×24 | 团队分条底 |
| `gauge_bar_ingame_multiLive_s_wh` | 532×24 | 填充（未满时占 532/609 ≈ 87%） |
| `bar_scoreGauge_mask` | 610×24 | 遮罩 |
| `ScoreRank_Mark_S/D` | 120×132 | 评级（注意这是 `Mark` 系列，不是 HUD 的 `score_S` 76×94） |
| `gage_fever_lane` | 1196×1316 | Fever 整条轨道，斜着穿的 |
| `line_fever` / `line_superFever` | 66×20 / 50×18 | 阈值线 |
| `text_lineFever` / `text_lineSuperFever` | 177×46 / 252×40 | 阈值文字 |
| `itemBox_superFever(_open)` | 118×116 / 115×119 | SuperFever 图标 |

看图时注意两点（`contact_sheet.png` 里一眼能看出）：
- `ScoreRank_Mark_S` / `_D` 是**粉色徽章 + 缎带，右下角带一个「仮」字** —— 这版美术里它还是
  临时占位图，别当成最终观感去抄配色。
- 块内连击数字是**紫色发光款**（`number_combo_154px_*_pr`），和单人 HUD 的数字**不是同一套** ——
  这是「别人的连击」和「我的连击」在视觉上区分开的手段。

### ⚠️ 一半素材是「纯白遮罩」，必须染色（2026-11-15 实测）

这批图看着"怪"就是因为这个：**它们不是最终观感，是给 SpriteRenderer 的 `m_Color` 染色的底图**。
对每张 PNG 统计「不透明像素是否全白」得到：

| 类型 | 文件 |
|---|---|
| **纯白 mask（必须 tint 才有颜色）** | `frame_player_multi`（20×20 头像框）、`gauge_bar_ingame_multiLive_s_wh`（分条填充）、`bar_scoreGauge_multi`（条底）、`bar_scoreGauge_mask`、`text_feverOK`（"OK!"）、`text_lineFever`、`text_lineSuperFever`、`label_color_thumbnail`、`line_fever`、`line_superFever` |
| **自带颜色（可直接用）** | `number_combo_154px_*`（紫）、`bg_combo_pl`、`eff_combo`、`ScoreRank_Mark_S/D`（粉徽章）、`itemBox_superFever(_open)`、`text_you_gn`（青）、`frame_multi_black`、`effect_playerScoreDanger_multi`、`thumb_mask` |

**`frame_player_multi` 只有 20×20**：它是九宫格边框（Unity sprite 的 border 拉伸），
不是一张 156×156 的整图 —— 所以在 contact sheet 里看着像个小圆角方块。
头像块的**实际大小参考值**是 `frame_multi_black` 的 **156×156**（死亡态的整块底）。

**`dying` / `death` 的图其实存在**（prefab 里 `SpriteRenderer` 有有效引用，只是走
`m_ExternalRefs` 外部文件，名字不叫 dying/death，全量 sprite 里也没同名项）。
从尺寸看 `death` 极可能就复用 `frame_multi_black`（156×156 纯黑块）+ 染色；
`dying` 是同尺寸的另一张遮罩。真要确认得解 prefab 的 external refs 表，成本不值。


---

## 10. 位置：实测出来了（2026-11-15 修正）

上一版我写"量不到位置"，**那是错的**。错在两点，都在我这边：

1. 找错 prefab —— 真正的多人 HUD 根叫 **`FrontMultiUIView`（prefab 1241，332 个节点）**，
   不是 `MultiLiveFrontView`（那是脚本名，不是资产名）。同族的还有 `FrontUIView`(1238)
   单人 / `FrontVRView`(1237) / `FrontRankUIView`(1239) / `FrontMusicVideoView`(1240)。
2. 读错字段 —— `ms_geom.py` 读 `m_AnchoredPosition`，那只存在于 `RectTransform`。
   HUD 节点是 `Transform`，要读 **`m_LocalPosition`**（新脚本 `ms_world.py` 干这个，
   顺带逐级累加出世界坐标）。

### 实测值（`front_multi_ui.world.txt`）

```
LeftTopAnchor              world=(-10.880,  5.000)
  MultiThumbnailViewGroup  world=( -8.732,  3.234)  scale=0.800
    MultiThumbnailView     world=( -8.732,  3.234)   ← 槽 0（自己）
    MultiThumbnailView     world=( -7.905,  3.234)
    MultiThumbnailView     world=( -7.098,  3.234)
    MultiThumbnailView     world=( -6.281,  3.234)
    MultiThumbnailView     world=( -5.468,  3.234)   ← 槽 4
MultiScoreView             world=(-10.880,  5.000)  ← 同一个锚点，分数面板
RightTopAnchor             world=( 10.880,  5.000)
  LifeView                 world=( 10.880,  5.000)
```

**五个块 y 完全相同、x 递增 0.82 单位 —— 横排，接在分数面板下面。**
槽位间距 0.82 单位，组缩放 0.8。

换到 1920×1080 画布（`pixels_to_units = 108`，锚点 ±10.88×±5 说明 prefab 是按
**19.5:9 手机**摆的，1 单位 = 108 px）：

| | 世界单位 | 像素 |
|---|---|---|
| 第 1 块中心 x（距左边） | 2.148 | **232** |
| 块中心 y（距顶） | 1.766 | **190** |
| 槽间距 | 0.82 | **88.6** |
| 槽位数 | 5 | — |

### 块内还有这些（`MultiThumbnailView` 1224）

```
ComboRoot       local y=-0.610  scale=0.20   ← 连击群在块下方，整体缩到 20%
  Combo         local y=-1.600  scale=2.00   （154px 的数字缩下来约 30px 高）
YouIcon         local y=-0.693  scale=1.25   ← "YOU" 标签，块底下方约 20px
ThumbnailRoot / ThumbnailTurnRoot / dying / death / Thumbnail / frame_player_multi  全在原点
```

注意 `frame_player_multi` 是 **20×20 的九宫格**（ImGui 没有九宫格路径，直接拉伸会糊成
一层白罩），实测过，所以现在块边框是自绘圆角描边。

### 复现

```bash
cd /d/Tools/rea/work/jp
python ms_world.py 1241 front_multi_ui     # 多人 HUD 根，含 5 个头像块
python ms_world.py 1224 ml_thumb_world     # 单个块内部
```
