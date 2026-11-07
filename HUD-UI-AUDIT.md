# HUD 与 UI 控件的原作对照（2026-11-07，REA 逆向）

> 免责声明同仓库其他文档：本文是**自己玩、自己看**的对照记录，素材与数据权利归原权利人，
> 不随代码分发、不对外发布。

这次盘点的是两件事：

1. **演奏 HUD**（分数 / 生命 / 连击 / 判定）和原作差在哪；
2. **界面控件**（按钮、复选框、滑杆、窗口框架）的几何有没有对上。

结论先给：HUD 的**结构**缺了三块（技能显示、生命条内的跳过键、AP 连击层），
**尺寸**上判定字比原作小一圈；控件则是**整体小了一档**（约 0.43 倍），
这不是"差几个像素"，是整套 UI 的尺度基准不同。

---

## 1. 方法与证据

| 证据 | 位置 |
|---|---|
| 官方 HUD 三个 prefab 的节点树 | `jp/hud_score.txt`、`jp/hud_life.txt`、`jp/hud_combo.txt`（由 `ms_geom.py` 导出，prefab 1219 / 1229 / 1235） |
| 官方控件几何 | `jp/` 下用 `ms_geom.py` 现场导出：153（Toggle）、231（CheckBoxMulti）、172（Slider）、243（ButtonLabelContent）、421/422（对话框正/负按钮）、423（DialogHeader）、1516（WishListConfirmDialog） |
| 官方 sprite 原始尺寸 | `jp/sprite-geometry.csv` |
| 我们的实现 | `game/Hud.cpp`、`game/Ui.cpp`（行号见各表） |

复现命令（AssetRipper 必须先起，见 `RE.md` §4.2）：

```bash
cd /d/Tools/rea/work/jp
python ms_geom.py 1219 hud_score    # 分数面板
python ms_geom.py 1229 hud_life     # 生命条
python ms_geom.py 1235 hud_combo    # 连击
python ms_geom.py 153  /tmp/toggle  # 开关
```

### ⚠️ 一个必须记住的前提

**演奏 HUD 不是 Unity UI。** `ScoreView` / `LifeView` / `ComboView` 里每个节点挂的是
`Transform + SpriteRenderer`（`ms_geom.py` 导出的 `anchoredPosition/sizeDelta`
全是 0），不是 `RectTransform`。也就是说：

- **它们的 prefab 里没有像素矩形**，只有世界坐标 `m_LocalPosition`（单位是"世界单位"，
  ScoreView 的子节点是 3.768 / -0.911 这种数量级）；
- 换算成 1920 画布像素需要**相机正交尺寸**，这次没拿到（live 场景 prefab 没拆）；
- 所以下表的"尺寸"只在 **sprite 原始像素**这一层可比（`pixels_to_units=108` 表示
  108 个 sprite 像素对应 1 个世界单位），**位置一律待测**。

要拿到位置的唯一可靠办法：**用 1920×1080 的原作 live 截图做像素测量**，
和我们的 `--screenshot` 叠图对位（项目里已有 `select_phone_overlay.py` 这套做法）。

---

## 2. HUD 结构对照

### 2.1 ScoreView（左上分数面板，prefab 1219，58 个节点）

官方节点（缩进即层级）：

```
ScoreView
├ ScoreSkill            ← SpriteRenderer，分数面板里的技能显示
└ ScoreBase
  ├ AddNumber           (+N 的 8 个数字槽 + Outline 层)
  ├ ScoreGaugeBase      ← 分数条底板
  ├ ScoreGauge          ← 分数条填充
  ├ Number / NumberRoot ← 8 位分数（本体 + 描边两层）
  ├ RankBase
  │ ├ rank              ← D/C/B/A/S 字母（score_A..S，76×94）
  │ └ ScoreRankText     ← "SCORE RANK" 牌
  ├ lineC / Line / C    ← 刻度：线 + 字母，C/B/A/S 各一组
  ├ lineB / lineA / lineS
```

| 官方节点 | 我们 | 结论 |
|---|---|---|
| `ScoreSkill` | **没有** | ❌ 缺一块显示（原作分数面板左侧的技能区） |
| `ScoreGaugeBase` + `ScoreGauge` | `score_bg` + `score_bar`（裁剪填充） | ✅ 等价 |
| `AddNumber`（8 槽 + outline） | `+N` 浮动数字（`scoreX(290)` 起，8 槽宽） | ✅ 等价 |
| `Number` + `NumberRoot`（本体 + 描边） | `digit_*` + `digit_s*` 两层 | ✅ 等价 |
| `RankBase.rank` | `rank_char_*` | ⚠ 尺寸偏小（见 2.4） |
| `RankBase.ScoreRankText` | `rank_txt_*` | ✅ |
| `lineC/B/A/S`（线 + 字母） | 刻度写在 `kHudMarks`，字母单独画 | ⚠ 位置是照 web 版量出来的，**没有和原作对过** |

### 2.2 LifeView（右上生命条，prefab 1229，21 个节点）

```
LifeView
├ LifeBackground
├ PauseButton / PauseButtonDisable   ← 两个态：可点 / 置灰
├ SkipButton                         ← 生命条内的跳过键
├ LifeBaseDefault / lifeGauge
├ LifeBaseDamage  / lifeGauge       ← 掉血层（独立一张 gauge）
├ LifeSkill                          ← 技能显示
├ Icon                               ← 头像/能量图标
└ NumberRoot / NumberOutlineRoot     ← 4 位血量（本体 + 描边）
```

| 官方节点 | 我们 | 结论 |
|---|---|---|
| `LifeBackground` / `lifeGauge` | `life_bg` + `life_fill`（444×104 @ 1442,11） | ✅ |
| `LifeBaseDamage.lifeGauge` | `life_danger` 覆盖层 | ✅ |
| `PauseButton` | `lifePauseRect()` 命中区 + 自绘暂停图标 | ✅（但图标是自绘，不是 sprite） |
| `PauseButtonDisable` | **没有** | ❌ 暂停中/不可暂停时原作换成灰态图，我们只是不画或仍画 |
| `SkipButton` | 开场跳过按钮画在**右上独立位置**（`introSkipHitTest`），不在生命条里 | ⚠ 位置不同 |
| `LifeSkill` | **没有** | ❌ 缺（技能显示） |
| `Icon` | **没有** | ⚠ 待确认是哪个图标（生命条左端） |
| `NumberRoot` / `NumberOutlineRoot` | 4 位数字 + 描边 | ✅ |

### 2.3 ComboView（连击，prefab 1235，18 个节点）

```
ComboView
├ Combo               ← "COMBO" 字样（bg_combo_pl 144×30 / eff_combo 246×110）
├ NumberRoot          ← 连击数本体
├ EffectNumberRoot    ← 连击数的特效层（叠在本体后面）
└ APEffectRoot + APCombo ← ALL PERFECT 专用：另一套数字 + "AP" 字样
```

| 官方节点 | 我们 | 结论 |
|---|---|---|
| `Combo` 字样 | `combo` 贴图（kTagW 127×42 / glow 197×79×0.67） | ✅ |
| `NumberRoot` | 数字槽步距 92、基比 0.85、组中心 (1634,478) | ✅ |
| `EffectNumberRoot` | 部分是 pulse 环（`kApPulseAngular`）+ 发光 tag | ⚠ 不等价，特效层缺失 |
| `APCombo` / `APEffectRoot` | 只有 AP 脉冲环，没有 AP 专用字样/数字层 | ❌ 缺 |

### 2.4 尺寸能对上的部分（sprite 原始像素）

| 元素 | 官方 sprite | 我们 | 差 |
|---|---|---|---|
| 判定字 | `judge_perfect` 等 **324×96**（`pixels_to_units=108`） | `assets/mmw/overlay/judge` 的 **310×81**，绘制高 81（`kJudgeBaseH`） | 高 **84.4%**、宽 95.7%，且**不是同一张 art** |
| 评级字母 | `score_S` **76×94** | `rank_char_*` 49×58 × `SCORE_ROOT_SCALE`1.5 = **73.5×87** | 92.6% |
| 评分条 art | `score_bar` 1650×76（我们的资源同宽） | 444×96 面板内画 354×16（×1.5） | 面板 666×144，刻度 `746/990/1234/1478 /1650` |
| 生命条 | `life_*` 2560×600 合成图 | 444×104 @ (1442,11)，暂停区取右 600/2560 | ✅ 同源 |
| 连击字 | `eff_combo` 246×110、`bg_combo_pl` 144×30 | tag 127×42 + glow 131.99×52.93 | 比例接近，位置未核 |

**判定字是这次最实的一个偏差**：原作 324×96，我们 310×81。判定字是演奏中抬头率最高的
元素，小 15% 一眼就能看出来。

---

## 3. UI 控件对照（这些 prefab 有真正的 RectTransform，数字可信）

### 3.1 复选框 / 开关

| 控件 | 官方 | 我们（`game/Ui.cpp`） | 差 |
|---|---|---|---|
| `UIPartsCheckBoxMulti`（231） | 根 0×0；`Base` **72×74**，`Check` **56×42**，偏移 (8.8, -15.8) | `checkBox()`：`boxSize = 24`、圆角 0.26×、勾 3 点自绘 | 框只有 **1/3** 大，且原作是"勾图 56×42"不是描线勾 |
| `UIPartsToggle`（153，开关式） | 根 **180.8×100**；`Bg` **152×56**、`Toggle` 旋钮 **72×72** @ x -48、`TextOff` **70×50** @ x 25.2 | **没有这个形态**（只有方框勾选） | 缺整套开关控件 |

### 3.2 滑杆

| | 官方 `UIPartsSlider`（172） | 我们 `slider()` | 差 |
|---|---|---|---|
| 轨道 | **560×24** | 高 **5**，宽 = 列宽 − 2×(按钮 31 + 16) | 高 1/4.8 |
| 手柄 | **64×84**（`Handle`），内部 `Dot` 高 48 | 直径 **20** | 1/4.2 |
| 行高 | — | 62（含数值文字） | — |
| 命中带 | `CollisionArea` 高 **85** | 手柄 ±thumbR×2 = 80 | 接近 ✅ |

### 3.3 按钮

| | 官方 | 我们 | 差 |
|---|---|---|---|
| 标准按钮 `UIPartsCommonButton`（1516 FooterButtons 内） | **336×96**，文字内缩 48（两侧），`Cover` 内缩 16 | `capsuleButton` 实际画 **调用尺寸 × 0.85**；对话框里 146×41 ×0.85 ≈ **124×34.8** | 宽 37%、高 **36%** |
| 对话框正/负按钮（421/422） | **408×116**，文字内缩 28/32，`Dot` 高 48 @ y -14 | 同上 | — |
| 关闭 X | `UIPartsCloseButton` **44×44** @ (-15,-15)（压在框角上） | 绘 26、命中 38（`kCloseDraw/kCloseHit`） | 59% |
| 对话框标题带 | `UIPartsDialogHeader` 高 **100**，左右图标 **36×36** @ ±46 | `beginCard` 的 `tabBandH` 由调用方给（设置卡是页签带） | 无固定规格 |

### 3.4 窗口框架

| | 官方 `WishListConfirmDialog`（1516） | 我们 | 差 |
|---|---|---|---|
| 卡片宽 | **1168**（`WindowRoot` 固定宽，高由内容撑） | `messageDialog` **500**、`eulaDialog` **660**、设置卡 **920** | 43% / 56% / 79% |
| 页面遮罩 | `UIPartsDialogFillCover`（223） | `dimBackdrop` 半透明 + 模糊 | 等价 |
| 内容内边距 | 见各 View 自身 | 设置卡两列网格 | — |

**结论**：我们的整套控件是照 **sekai-stories（网页复刻）** 的紧凑尺度做的，
原作是"1080 画布上 336×96 的按钮、1168 宽的面板"。要么接受这个尺度（网页版就是这个观感），
要么把整套 `scale()` 基准乘 ~2.3 —— 后者会把设置卡从 920 撑到 2116，屏幕放不下，
**所以这不是简单改常量能解决的，是信息密度取向的取舍**。

---

## 4. 建议的处置顺序

**能立刻做、风险低：**

1. **换判定字 art**：从日服包导 `judge_*`（324×96）替掉 overlay 的 310×81 版本，
   `kJudgeBaseH` 改 96（位置 `kJudgeCenterY` 要跟着微调，否则会往下坠）。
2. **补 `PauseButtonDisable`**：暂停/倒计时期间把暂停键画成灰态（现成的 `life_*` 表里
   可以裁，或直接用 sprite）。
3. **评级字母改 76×94**：`rank_char_*` 的绘制尺寸从 49×58 改成 `76/1.5 × 94/1.5`，
   位置要重对一次。

**要先有数据再动：**

4. **HUD 位置整体对位**：需要一张 1920×1080 原作 live 截图，按 `select_phone_overlay.py`
   的做法做叠图脚本（`hud_overlay.py`），把 score / life / combo / judge 四块的
   左/上/宽/高量出来，再统一改。
5. **`ScoreSkill` / `LifeSkill` / `Icon`**：先确认这三块在什么模式下才显示（原作可能只在
   有技能的 live 里显示），再决定要不要做。
6. **`SkipButton` 位置**：原作在生命条内，我们独立；改之前先量生命条的间隙。

**不建议动：**

7. 控件整体尺度。改一半比不改更难看，而且会连带把七个设置页的排版全部推翻。

---

## 5. 这次没拿到的

- **HUD 各块的像素矩形**：HUD 是 SpriteRenderer + 世界坐标，prefab 里没有 rect。
  要算需要 live 场景相机（正交尺寸），或者直接量官方截图。
- **sprite → 节点**的对应表：sprite 引用跨 asset collection，`ms_geom.py` 只解析了
  同 collection 的 PathID，跨包引用解析错了（打印出来是 collection hash）。要补得先
  解析 `m_Sprite` 的 `{m_FileID: >0}` 指向哪张 atlas。
- **控件九宫格边距**（sliced sprite 的 border）：YAML 里有 `m_Sprite` 的 border 字段，
  但同上，跨包引用没解。
