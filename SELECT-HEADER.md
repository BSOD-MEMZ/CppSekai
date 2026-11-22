# SELECT-HEADER.md — 选曲界面的「顶栏」到底是什么（2026-11-22 实测）

> 起因：用户指出「原作在选曲界面右上角还有一个菜单按钮，它可能并不包含于顶栏内，但它确实存在」。
> 结论先说：**那个菜单钮不但存在，它和返回钮一起属于同一个全局顶栏 prefab `ScreenLayerHeader`**；
> 选曲界面自己的 prefab 里**没有**顶栏，只有搜索 / 排序 / 筛选那一排。
> 本文所有数字来自日服 7.1.0 解包（prefab 694 / 702 + `ScreenLayerData` 资产），不是目测；
> 一张图看全貌：[`docs/select-topbar-1920.png`](docs/select-topbar-1920.png)。

## 0. 一句话结论

单人（自由演出）选曲时，屏幕最上面那一条是**两个层叠在一起**：

| 层 | prefab | 这一条里的东西 |
|---|---|---|
| 全局顶栏层（`DisplayLayer: 2`） | `ScreenLayerHeader` **694** | **返回圆钮（左） + 「SOLO LIVE」标题块 + 菜单圆钮（右，带未读角标）** |
| 选曲列表层（`DisplayLayer: 1`） | `ScreenLayerFreeLiveMusicSelect` **702** | 搜索框（196..957） + 筛选圆钮（1380..1452） + 排序下拉（1476..1756） |

所以从左到右整条是：

```
[返回 96²]  [SOLO LIVE 标题 604×62]   …  [搜索框 760.8×72]   [筛选 72²]  [排序 280×56]  [菜单 96²]
 x 16       x 107                      x 196                x 1380     x 1476        x 1808
```

**菜单钮和排序下拉之间只有 51.5px**（1756.5 → 1808）。这一条 96px 高的带子（y 16..112）
就是原作的「顶栏」，我们以前只把它当成「搜索那一行」，所以才会以为菜单钮是我们自己发明的东西。

## 1. 是谁决定「这一屏显示顶栏的哪几件」

`ScreenLayerData`（ScriptableObject，每屏一份）上有 5 个开关 + 2 个字符串：

```csharp
public HeaderDisplay  DisplayHeader;        // 整个顶栏
public HeaderCategory DisplayCategory;      // 顶栏里放哪几簇（Flags）
public HeaderDisplay  DisplayPlayerInfo;    // 等级/宝石/体力那一簇
public HeaderDisplay  DisplayBackUIScreen;  // 返回钮
public bool           EnableBackUIScreen;
public HeaderDisplay  DisplayScreenName;    // 标题块
public string         ScreenName;           // 标题图名（sprite 名）
public string         ScreenSubName;        // 副标题文本（wording 键）
```

`HeaderDisplay`：`Unrelated 0 / Show 1 / ShowWithAnimation 2 / Hide 3 / HideWithAnimation 4 /
SwapWithAnimation 5 / ShowImmediate 6`。
`HeaderCategory`（Flags）：`Rank 1 | Currency 2 | LiveBoost 4 | Gacha 8 | … | Menu 0x100 |
SpecialMaterialExchange 0x200 | CurrencyPaid 0x400`，**`Default = 0x107` = Menu|Rank|Currency|LiveBoost**。

### 1.1 选曲界面的原文（`Assets/Resources/screen/sceneentry/` 那一批资产）

**`ScreenLayerFreeLiveMusicSelect`**（单人 / 自由演出选曲，`ScreenType: 15`）：

```yaml
DisplayHeader: 2              # ShowWithAnimation —— 顶栏出现
DisplayCategory: 263          # 0x107 = Menu|Rank|Currency|LiveBoost
DisplayPlayerInfo: 4          # HideWithAnimation —— 等级/宝石/体力 整簇不显示
DisplayBackUIScreen: 2        # ShowWithAnimation —— 返回钮出现
EnableBackUIScreen: 1
DisplayScreenName: 2          # ShowWithAnimation —— 标题块出现
ScreenName: title_freeLive    # 「SOLO LIVE」那张 logo 图
ScreenSubName: WORD_MUSIC_SELECT
```

对照 `ScreenLayerHome`（首页）：`DisplayPlayerInfo: 2`（显示）、`DisplayBackUIScreen: 4`（藏）、
`DisplayScreenName: 4`（藏）—— 首页那一条是「等级 + 宝石 + 体力 + 菜单」，
**选曲那一条反过来是「返回 + 标题 + 菜单」，没有等级/宝石/体力**。这也解释了为什么选曲这条
看起来「空」：中间那一大段本来就是留给搜索框的。

**`ScreenLayerMultiLiveMusicSelect`**（多人选曲，`ScreenType: 39`）：

```yaml
DisplayHeader: 4              # HideWithAnimation —— 整个顶栏不显示
DisplayCategory: 0
EnableBackUIScreen: 0
DisplayBackUIScreen: 0        # Unrelated
ScreenName: title_multiLive
```

也就是说**多人选曲根本没有顶栏**（房间状态由房间 UI 接管）。要做多人版时别把这套搬过去。

## 2. `ScreenLayerHeader`（prefab 694，230 个节点）里跟这条带子有关的节点

```
ScreenLayerHeader
  ComponentRoot
    LeftTop                        (0,1)-(0,1)  pivot(0,1)  124×124
      UIPartsBackButton            96×96 @(16,16)  pivot(0,1)
        EffectBase / Cover
        Icon                       42×42 @中心  icon_back_wh  #444466
      GameObject                   100×100（排版占位）
        ScreenNamePanel            604×62 @(107,61)  pivot(0,0)  CanvasGroup(alpha 0)
          ScreenName               350×60          ← 运行时 SetScreenName() 换成 title_freeLive
          ScreenSubNameTriangle    24×28  icon_triangle_pk（粉色小三角）
          ScreenSubName            32px 文本       ← WORD_MUSIC_SELECT（「楽曲選択」）
          FadeRoot
            CircleFriendButton / IdCopyButton     ← 只有个人资料类页面用
    UIPartsMenuButton (1)          96×96 @(1808,16)  pivot(1,1)  btn_circle80_wh
      MenuIcon                     40×34   icon_menu_wh   #444466
      badge                        32×32 @(1872,16)  badge_attention_rd
      Cover
    MenuLayout                     (1,1) 96 高，pivot(1,0.5)，右缘 1904
      PlayerInfo                   ← 这一屏 DisplayPlayerInfo=4，整簇关掉
      MenuButtonPadding            96×96（HorizontalLayoutGroup 给菜单钮留的位）
    BlockListButton / CommunityReportButton   ← 举报/拉黑，选曲不用
```

菜单钮和返回钮**是同一个 prefab 里的两个 `CustomButton`**（`menuButton` / `backUIScreenButton`
两个 `[SerializeField]` 字段），所以「菜单钮算不算顶栏」这个问题没有歧义：**算**。
`MenuLayout` 只是个 `HorizontalLayoutGroup`：`PlayerInfo` 和 `MenuButtonPadding` 并排，
`PlayerInfo` 一关，菜单钮就顶到 1904（右缘留 16）。`MenuButtonPadding` 就是给它占的那 96px。

## 3. 几何表（1920×1080 画布，y 从屏幕上沿往下）

### 3.1 顶栏层（prefab 694）

| 元素 | x | y | w | h | 贴图 / 颜色 |
|---|---|---|---|---|---|
| 返回钮 `UIPartsBackButton` | 16 | 16 | 96 | 96 | `btn_circle80_wh`（96² 白圆，Simple 不是九宫格）|
| ↳ 图标 `Icon` | 43 | 43 | 42 | 42 | `icon_back_wh`，tint **#444466**，preserveAspect |
| 标题块 `ScreenNamePanel` | 107 | 61 | 604 | 62 | `bg_contentsName`（九宫格白胶囊；切片图未导出）|
| ↳ 标题图 `ScreenName` | 146 | 占位 | 350 | 60 | 运行时 = `title_freeLive`（369×59，`SetNativeSize`）|
| ↳ 副标题三角 | 158 | 占位 | 24 | 28 | `icon_triangle_pk`（粉）|
| ↳ 副标题文字 | 182 | 占位 | →711 | 56 | 32px 文本，`WORD_MUSIC_SELECT` |
| 菜单钮 `UIPartsMenuButton` | 1808 | 16 | 96 | 96 | `btn_circle80_wh` |
| ↳ 汉堡图标 `MenuIcon` | 1836 | 47 | 40 | 34 | `icon_menu_wh`，tint **#444466** |
| ↳ 角标 `badge` | 1872 | 16 | 32 | 32 | `badge_attention_rd`（平时隐藏）|

### 3.2 选曲列表层（prefab 702）同一行的控件

| 元素 | x | y | w | h | 贴图 |
|---|---|---|---|---|---|
| 搜索框 `UIPartsSearchField` | 196 | 28 | **760.8** | 72 | `btn_round_r16_wh`（r16 九宫格）；放大镜 `icon_search_h34_wh`（34²）在**右端** x 898 |
| 筛选 `FilterButton` | 1380.3 | 27 | 72 | 72 | `btn_circle56_wh` + `icon_refine_dbl` 29×28 |
| 排序 `UIPartsDropdown` | 1476.5 | 36 | 280 | 56 | `btn_round_h56_wh`；下拉面板 `bg_base_half_r20_shadow_lgr` |

三者**不是一样高**（72 / 72 / 56），但中心都在 y≈64；这一条里菜单钮 96 高（16..112），
返回钮同理。**别把它们拉齐** —— 原作就是这样。

### 3.3 一句话记住四个边距

- 左：返回钮左缘 **16**；标题块左缘 **107**（= 返回钮右缘 112 − 5）。
- 右：菜单钮右缘 **1904**（= 1920 − 16）；角标右缘也正好 1904。
- 菜单钮 ↔ 排序下拉：**51.5**（1808 − 1756.5）。
- 筛选 ↔ 排序下拉：**24.2**（1476.5 − 1452.3）。

## 4. 还没量准的一处：标题块 vs 搜索框的静态矩形是重叠的

按 prefab 的静态值，标题块 **107..711 / 61..123**，搜索框 **196..957 / 28..100** ——
**两者的 x 和 y 都重叠**。但标题块在 prefab 里没有留任何脚本引用（`ScreenLayerHeader` 只有
`screenNameImage` / `screenNameCanvasGroup` 两个字段，没有 panel 的 RectTransform），
`SetScreenName()` / `ShowScreenName()` 的汇编也只改 sprite 和 alpha，**不会挪位置**。
所以这一处仍然存疑，两条可能：

1. 搜索框（`UIPartsSearchField` 的脚本，MonoScript 9971）在运行时按某种规则重新排版（很常见）；
2. 标题块被搜索框整片盖住，实际看不到（pill 左缘 107 与返回钮右缘 112 只差 5px，等于看不见）。

标题块**子节点**的 `anchoredPosition` 是编辑器占位（比如 `ScreenName` 自己算出来会跑到
`ScreenNamePanel` 上面去），真实排布只能以游戏内截图为准：老版本截图里
「SOLO LIVE」在上、粉三角 + 「楽曲選択」在它正下方，整块紧贴返回钮右侧 ——
所以图上那一块是按「logo + 副标题」画的，**没有**把 `ScreenNamePanel` 的 604×62 当成可视胶囊。

> 以后再拿到一张 7.x 的选曲截图，先量这一处：标题块到底在 x 107 还是右移了。

## 5. 我们（`game/SongSelect.cpp`）现在的实现

**2026-11-22 已落地**：菜单钮按原作摆回最右（96²、图标 40×34、染 #444466）。
**返回钮和标题块是「确定不做」** —— 用户明确要求左边那一格空着、不画 SOLO LIVE 标题块，
所以搜索框保持原作的 x=196，这一条中间不会被顶栏挤掉；真要做，数字在 §3 里现成。

**2026-11-22 第二轮**：菜单钮现在真的打开侧边栏了 —— 对应原作的
`MenuDialog`（prefab 1775），不是以前那颗四行 `Popup`。几何（904 宽 / 满高 /
#444466 / 大格 392×184 / 小格 192×152 / 关闭钮 44²）与实现细节见
[`SELECT-UI.md`](SELECT-UI.md)「2026-11-22 第二轮」§3。菜单钮在抽屉打开时收起
（原作 `HideMenuButton`），ESC 只关抽屉（`main.cpp` 的 escape 链加了
`game::menuDrawerOpen()` 判断）。

```cpp
const float searchH    = 72.0f * k;                       // ✅ 原作
const float headerRowY = 28.0f * k;                       // ✅ 原作
const float menuBtnD   = 96.0f * k;                       // ✅ 原作 96²
const float menuBtnX   = w - 16.0f * k - menuBtnD;         // ✅ 1808 @1920（和等级牌一样按 w 贴右）
const float menuBtnY   = 16.0f * k;                       // ✅ 原作 16
```

| 项 | 原作 | 我们 | 状态 |
|---|---|---|---|
| 菜单钮位置 | 1808..1904（右缘 16 边距） | `w - 16k - 96k`，1080p 下就是 1808..1904 | ✅ 已对齐 |
| 菜单钮尺寸 | 96² 白圆 + `icon_menu_wh` 40×34 #444466 | 96² 圆 + 40×34，tint `(68,68,102)` | ✅ 已对齐 |
| 未读角标 | 32² @(1872,16)，`badge_attention_rd` | 无 | ⏸ 故意不做：没有真实「有未读」信号，画一个常亮的就是假数据 |
| 返回钮 | 96² 白圆 @(16,16)，图标 42² #444466 | **不做**（该区域留空） | ❌ 用户 2026-11-22 决定 |
| 标题块 | SOLO LIVE + ▶楽曲選択 | **不做** | ❌ 同上 |
| 等级牌 | 这一屏 `DisplayPlayerInfo=Hide` | 已隐藏（`kShowLevelChip=false`）| ✅ 和原作一致；**菜单钮占了这个角，以后要恢复得先决定谁让位** |
| 搜索 / 筛选 / 排序 | 196 / 1380.3 / 1476.5 | 同（`SELECT-UI.md` §顶栏一比一）| ✅ |

改的时候注意：**菜单钮右移之后，`ui::setAnchor("rescan"/"store"/"guess"/"abssettings")` 那四个
教学锚点也跟着右移**（`SongSelect.cpp` 里 `menuBtnMin/Max`，原来写死 `filterBtnD`），
剧情里指过去的手势要靠它，别拆。

## 6. 复现命令（日服 7.1.0，产物路径见 [RE.md](RE.md)）

下面用到的 4 个新脚本已随仓库放在 `.workbuddy/tools/`（`ms_geom.py` / `arip_search.py`
原本就在 `jp/` 与仓库里各有一份）。在 `jp/` 里跑，或者把 `.workbuddy/tools/` 加进
`PYTHONPATH` 都行。

```bash
cd /d/Tools/rea/work/jp
# 1) 起 AssetRipper（后台常驻）并加载合并后的 GameData
/d/Tools/rea/work/tools/arip/app/AssetRipper.GUI.Free.exe --headless --port 17654 &
curl.exe --noproxy '*' -X POST "http://127.0.0.1:17654/LoadFolder" \
     --data-urlencode "path=D:/Tools/rea/work/jp/GameData"

# 2) 顶栏 prefab 694 的整棵树（含 anchor / pivot / sizeDelta）
python ms_geom.py 694 hdr
python hdr_rects.py hdr.json          # 解成 1920×1080 绝对矩形

# 3) 每一屏的顶栏开关（ScreenLayerData，一个屏一个 bundle）
python entry_resolve.py 3085          # EntryScreenLayersOutGame -> bundle 索引
#   ScreenLayerFreeLiveMusicSelect 在 I:3487 / ScreenLayerMultiLiveMusicSelect 在 I:3453
curl.exe --noproxy '*' "http://127.0.0.1:17654/Assets/Yaml?Path=%7B%22C%22%3A%7B%22B%22%3A%7B%22P%22%3A%5B%5D%7D%2C%22I%22%3A3487%7D%2C%22D%22%3A1%7D"

# 4) 单个节点的组件原文（spriteName / m_Color / m_IsActive）
python go_components.py 32 694        # 返回钮
python go_components.py 162 694       # 菜单钮

# 5) 重画示意图
python render_topbar.py
```

## 7. 验收

无头截图对比（不用开游戏）：

```bash
./build/cppsekai.exe --no-party --screenshot shots/a.png --screenshot-time 2.0
```

对着 `docs/select-topbar-1920.png` 量三件事：返回钮左缘 16、菜单钮右缘 1904、
排序下拉右缘 1756。三个数对上，这一条就对齐了。
