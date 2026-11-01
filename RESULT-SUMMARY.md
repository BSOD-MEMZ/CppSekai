# RESULT-SUMMARY.md —— 两段结算的第一段（summary）

> 2026-11-01。原作结算分**两段**：先 summary（LIVE RESULT + 分数条 + SCORERANK），
> 再分数明细页（评级、判定计数、徽章）。**两段现在都实现了**（第一段见 §6）。
> 数据来自 `ScreenLayerFreeLiveResult`（prefab pathID **701**，1146 个节点），
> 用 `ms_geom.py 701 result_freelive` 导出。

## 1. summary 的节点树

```
SoloLiveScoreSummary            (0,0)        100x100
  AnimationRoot                 (0,0)        100x100
    ScoreView                   (0,0)        776x146
      Rank                      (757, 11)    100x100      ← SCORERANK 竖牌
        Base                    (-55.9,-11.5)  224x375
        RankLineImage           (-55.9, 43.2)  224x266
        RankImage               (-55.9, -8.4)  224x266
        RankTextImage           (-55.9,-172.4) 168x22
      ResultImage               (-132.1, 35.6) 1220x166    ← "LIVE RESULT" 大字
        ResultImage             (0,0)          1220x166
        ResultLineImage_01      (0, 73.6)      1220x166    ← 上装饰线
        ResultLineImage_02      (0,-88.5)      1220x166    ← 下装饰线
        particle                (0,0)          100x100
      ScoreGauge                (-132.9,-168.2) 1359.7x40  ← 分数条
        ScoreGauge              (0,0)          1360x40
        frame                   (0,0)          100x100
        lineA/B/C/S             x=812/590/412/903.5        ← 档位刻度（A/S/B/C）
```

`SoloLiveScoreSummary` 的父是 `ScreenRoot → ComponentRoot → ScreenLayerFreeLiveResult`
（全程 pos 0、anchor 0.5/0.5），所以**上面这些就是相对屏幕中心的值**。

## 2. 换算到 1920×1080

`ScreenLayerFreeLiveResult` 下的 `Background` 是 **2520×1440**，这是设计基准。它和
我们的 1920×1080 不完全同比例（1.75 vs 1.778），按**高度**换算最稳：

```
scale = 1080 / 1440 = 0.75
```

| 元素 | Unity 中心 | ×0.75 后（相对屏幕中心） | 项目坐标（1920×1080，y 向下） | 显示尺寸 |
|---|---|---|---|---|
| ResultImage | (-132.1, 35.6) | (-99.1, 26.7) | **(860.9, 513.3)** | 915×124.5 |
| ScoreGauge | (-132.9, -168.2) | (-99.7, -126.2) | **(860.3, 666.2)** | 1020×30 |
| Rank（Base） | (701.1, -0.5) | (525.8, -0.4) | **(1485.8, 540.4)** | 168×281 |
| 装饰线 01 | 相对 ResultImage (0, 73.6) | — | ResultImage 上方 55.2 | 915×124.5 |
| 装饰线 02 | 相对 ResultImage (0, -88.5) | — | ResultImage 下方 66.4 | 915×124.5 |

⚠ 横向会多出 30px（2520×0.75 = 1890，比 1920 窄），居中的话左右各 15px 内缩，
不影响的元素可以忽略。

## 3. 资源（已切好、已入库）

`assets/mmw/overlay/result/` 新增四张，全部来自
`sactx-0-2048x2048-ASTC 4x4-LiveResultAtlas`：

| 文件 | 原名 | 尺寸 | 是什么 |
|---|---|---|---|
| `liveresult_wh.png` | `txt_liveresult_wh` | 1272×166 | **白色 `LIVE RESULT`**（summary 标题） |
| `liveresult_cc.png` | `tex_liveResult_cc` | 1280×150 | 彩色版 `LIVE RESULT` |
| `liveclear_cc.png` | `text_liveresult_clear` | 1077×162 | 渐变彩虹 `LIVE CLEAR` |
| `liveresult_line.png` | `txt_liveresult_line_dgr` | 1272×166 | 标题的**空心描边副本**（不是装饰线，见 §4） |

### 怎么切的（工具与坑）

**最省事的做法是不过滤地导一遍全部 sprite**（2026-11-01 更正）：

```bash
ASMod='D:/Tools/rea/work/tools/asmod/net8/AssetStudioModCLI_net8_portable'
dotnet "$ASMod/AssetStudioModCLI.dll" "D:/Tools/rea/work/jp/GameData" \
  -m export -t sprite -o "D:/Tools/rea/work/jp/tex_all_sprite" --unity-version 2022.3.62f2
# → 2808 个 PNG，文件名就是 sprite 名，atlas 子 sprite 也已经切好
```

`tex_all_sprite/txt_liveresult_wh.png` 和「导整张 ASTC atlas 再按 m_Rect 手切」的结果
**逐像素一致**。这段文档之前写「按 sprite 名导会失败」是**错的**：失败的是
`--filter-by-name`（它会漏掉 atlas 内的子 sprite），不是 atlas 本身。
**「导不出来」不等于「不存在」，先跑全量导出再下结论。**

手工切的备用路线（`cut_sprites.py`，需要 PIL）仍然留着，遇到整张 atlas 才用：

```bash
dotnet "$ASMod/AssetStudioModCLI.dll" "D:/Tools/rea/work/jp/GameData" \
  -m export -t tex2d --filter-by-name "LiveResultAtlas" \
  -o "D:/Tools/rea/work/jp/tex_atlas" --unity-version 2022.3.62f2
python cut_sprites.py <搜索结果.html> <atlas.png> <outdir> txt_liveresult_wh ...
```

- Unity 的 `m_Rect` 是**从底部**算的，PIL 从顶部算 → `top = atlasH - (y + h)`。
- 这条路线也验证过（用 `txt_liveclear_wh` 对项目 10-27 就有的 `liveclear_wh.png`，
  逐像素一致）。

**找图不用猜名字**：全量导出后建一张「名字 → 尺寸」表（`sprite_sizes.json`），
拿 prefab 里量到的 `sizeDelta` 反查，结算页这批图全是这么命中的
（`gauge_bar_result_freeLive` = 1360×40 就是这么找到的）。

## 4. 动画：`clip_result_summary_v2_01`（1.0 秒）

summary 的入场全在这一条 clip 里。**`_00` 是静态初始姿态**（11 条曲线各只有一个
关键帧，别拿它当动画），真正播的是 `_01`。`_00_rank` / `_01_rank` 是带排名横幅的
变体，`_multi` / `_cheerful` 是多人 / Cheerful 分组，单人免费 live 用 `_01`。

| 目标 | 属性 | 关键帧 |
|---|---|---|
| ResultImage | `m_Alpha` | 0 → 0.878 @0.4167 → 1 @0.8667 |
| ResultLineImage_01 | `y` | 0 → 16.875 @0.1667 → 48.339 @0.4167 → 73.6 @1.0 |
| ResultLineImage_02 | `y` | 0 → -17.345 @0.1667 → -53.489 @0.4167 → -88.5 @1.0 |
| Rank | `m_Alpha` | 0（@0.1667 前）→ 1 @0.4167 |
| Rank/RankLineImage | `y` | -9 → -7.8 @0.1667 → 43.2 @1.0 |
| ScoreGauge | `m_Alpha` | 0 → 1 @0.1333 |

**所有关键帧的 `inSlope` / `outSlope` 都是 0**，也就是每段都是缓入缓出。
`game/Result.cpp` 的 `curveAt()` 用 smoothstep 复现，不用自己拟合贝塞尔。

**两条 `ResultLineImage` 是同一句话的描边副本**（`txt_liveresult_line_dgr`，
1272×166 的空心字），起始都叠在标题正中，然后一上一下滑开 —— 观感上就是标题
上下各浮一层描边。之前"只画一次、当成装饰线"是误读：数据里它们确实各自有
独立的 y 曲线，最终停在 +73.6 / -88.5。

⚠ 这两层必须和标题**用同一套等比 ink 缩放**（`drawSpriteInkCentered`）。按
1220×166 的盒子硬塞会把它们纵向拉伸 4%（1272:166 ≠ 1220:166），描边和实字对不上。

## 5. 分数条与 SCORERANK 牌的贴图

`ScoreView/ScoreGauge` 的四个子件全在首包里，按尺寸一搜就有：

| 项目文件 | 原名 | 尺寸 | 用在哪 |
|---|---|---|---|
| `liveresult_bar.png` | `gauge_bar_result_freeLive` | 1360×40 | 分数条填充（**彩虹渐变**，`m_Type 3` 水平填充） |
| `rank_tick_line.png` | `gauge_line_result` | 4×12 | 刻度竖线（纵向拉伸，两端带羽化） |
| `rank_tick_tri.png` | `icon_triangle_wh` | 28×22 | 刻度上方的宽缺口 |
| `rank_tick_{c,b,a,s}.png` | `icon_scoreRank_s_*` | 68×76 | 刻度字母（自带颜色：C 青 / B 蓝 / A 紫 / S 粉） |
| `rank_{s,a,b,c,d}.png` | `txt_scoreRank_*` | 224×266 | 牌上的大字母（实心） |
| `rank_line_{...}.png` | `txt_scoreRank_line_*` | 224×266 | 大字母的描边副本（上滑淡入） |
| `rank_label.png` | `txt_scoreRank_wh` | 168×22 | `SCORERANK` 字标，`m_Color` 粉 **#FF8EC6** |
| `livefailed_line.png` | `txt_livefailed_line_dgr` | 1352×166 | 失败局的**空心描边副本**（和 `liveresult_line` 不是同一张，宽 1352 不是 1272） |

**分数条底槽没有贴图**：`ScoreView/ScoreGauge` 的 `frame` 子节点 `m_Sprite` 是
`{0,0}`，颜色 `m_Color = (50,50,76)` = **#32324C**，`m_Type 1`（九宫格）。所以底槽
就是一块圆角药丸，`game/Result.cpp` 直接用 `AddRectFilled` 画，不吃贴图。
SCORERANK 的 `Base`（224×375）同理 —— 颜色一样是 #32324C，尺寸在首包里没有
对得上的 sprite（搜遍 2808 张），也按圆角矩形画。**这两处是唯一"画出来"而不是
"贴出来"的**；将来若找到原图，替换 `kGaugeTrack` / `kRankPlateFill` 两处即可。

### 刻度位置和 HUD 不一样（重要）

刻度在 `ScoreGauge` 局部 x = **412 / 590 / 812 / 903.5**（全宽 1359.7）：

```
C 0.303   B 0.434   A 0.597   S 0.664
```

而在打歌 HUD 里，`scoreRankAndBar()` 用的是 `746/990/1234/1478 ÷ 1650`
（0.452 / 0.600 / 0.748 / 0.896）。**两套不是同一个比例尺** —— 结算条覆盖的
分数区间和 HUD 条不一样。所以填充比例必须用**结算条自己这套**刻度反算：

```cpp
ScoreRank scoreRankAndBar(score, rating, kRankMarkPos);  // 第二阶重载
```

否则会出现"填到 A 刻度右边，牌上却写着 B"这种自相矛盾。实测
`--result-preview`（score 940021 / rating 23）填充停在 **0.616**，牌上是 **A**，
和 A 刻度 0.597 对得上。

## 6. 已实现

`game/Result.cpp` 的 `drawSummary()`，`drawResult()` 按 `kSummarySec = 2.6` 分两段：

- 标题（`result_liveresult` / `result_livefailed`）+ 两条描边副本按曲线滑开
- 分数条：底槽药丸 + 彩虹填充（`uv1 = (ratio, 1)` 裁切）+ C/B/A/S 三件套刻度
- SCORERANK 牌：圆角底 + 大字母 + 描边副本上滑 + 粉色字标
- 失败局不画 SCORERANK 牌（原作失败时没有排名），标题和描边一起换成
  `result_livefailed` / `result_livefailed_line`

`main.cpp` 的结算截图：`--screenshot-time` 优先，默认 5.2（2.6 摘要 + 2.6 分数页）。

## 7. 没做的

- [ ] `particle`（标题后面的粒子，节点存在但没解发射参数）
- [ ] `clip_result_summary_v2_00_rank` / `_multi` / `_cheerful` 三个变体
      （排名横幅 `1位~5位` 那支要用 `_rank`）
- [ ] **第二段（分数页）的几何对照** —— 现在那套是照截图摆的，和 prefab 的
      `SoloLiveScoreResult` 没做过映射。这是目前最大的一块"靠猜"。
- [ ] summary 的点击跳过（原作点一下直接进分数页）
