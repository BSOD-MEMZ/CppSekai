# RESULT-SUMMARY.md —— 两段结算的第一段（summary）

> 2026-11-01。原作结算分**两段**：先 summary（LIVE RESULT + 分数条 + SCORERANK），
> 再分数明细页（评级、判定计数、徽章）。我们目前只有第二段。
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
| `liveresult_line.png` | `txt_liveresult_line_dgr` | 1272×166 | 标题上下的装饰线 |

### 怎么切的（工具与坑）

pjsk 把结算页的美术全打进一张 **ASTC 4x4 压缩的 SpriteAtlas**。按 sprite 名直接导
**会失败**（`AssetStudioModCLI -t sprite --filter-by-name txt_liveresult_wh` 只吐出
同批的 `_line_dgr`），但**导整张 atlas 是成功的**：

```bash
ASMod='D:/Tools/rea/work/tools/asmod/net8/AssetStudioModCLI_net8_portable'
dotnet "$ASMod/AssetStudioModCLI.dll" "D:/Tools/rea/work/jp/GameData" \
  -m export -t tex2d --filter-by-name "LiveResultAtlas" \
  -o "D:/Tools/rea/work/jp/tex_atlas" --unity-version 2022.3.62f2
# → sactx-0-2048x2048-ASTC 4x4-LiveResultAtlas-38a5d3ab.png (2048x2048)
```

然后按每个 Sprite 的 `m_Rect` 自己切（`cut_sprites.py`，需要 PIL）：

```bash
python cut_sprites.py <搜索结果.html> <atlas.png> <outdir> txt_liveresult_wh ...
```

- Unity 的 `m_Rect` 是**从底部**算的，PIL 从顶部算 → `top = atlasH - (y + h)`。
- **验证过**：用同一套方法切 `txt_liveclear_wh`，和项目里 10-27 就有的
  `liveclear_wh.png` 逐像素一致（`ImageChops.difference` 的 bbox 是 `None`）。
  也就是说项目那批结算图本来就是这条链路来的。

## 4. 实现方案（未做）

`game/Result.cpp` 目前 `drawResult()` 一进来就画分数明细页。加第一段的两种做法：

**A. 同文件加阶段（改动小）**
`ResultData` 加 `bool summary`／或传一个 `phase`，`drawResult` 按 phase 分支；
`elapsedSec` 继续做各自的入场动画。main.cpp 里用计时器在 2.5s 后切 phase，
点按可以提前切。

**B. 拆成两个绘制函数（更清楚）**
`drawSummary()` / `drawScore()`,共用 `Canvas` + 那几个文字/数字工具函数。
`Result.cpp` 已经按这个粒度拆了工具（`drawSpriteInk` / `drawDigitRun` /
`textCentered`…），B 的边际成本很低。

我倾向 **B**：两段的元素几乎没有重叠（summary 是"大标题 + 进度条 + 排名"，
score 是"评级 + 判定行 + 分数"），塞进一个函数会让已经 900 行的 `drawResult` 更难读。

**还要定的三件事**：
1. summary 停多久（原作约 2.5s，且点击可跳过）
2. 分数条要不要播"从 0 涨到实际分数"（`clip_result_summary_v2_00` 里有曲线，还没解）
3. 失败局（LIVE FAILED）走不走 summary —— 原作是走的，只是标题换成 `livefailed_wh`

## 5. 没做的

- [ ] `clip_result_summary_v2_00` / `_01` 的动画曲线（进度条增长、标题滑入）
- [ ] SCORERANK 牌的三个子图（`score/rank/` 里现有的是分数页那套，summary 的
      `RankLineImage` / `RankImage` 是另一套，还没切）
- [ ] `particle`（标题后面的粒子）
- [ ] 第二段（分数页）的几何对照 —— 现在那套是照截图摆的，和 prefab 的
      `SoloLiveScoreResult` 没做过映射
