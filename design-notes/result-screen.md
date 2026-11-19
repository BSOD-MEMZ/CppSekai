# 结算画面

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 结算画面（2026-09-13，`game/Result.cpp`）

**2026-09-19 改版（用户点名的四条）**：
1. **自绘装饰全删**：背景 wash（`AddRectFilledMultiColor`）、6 个装饰框线、两条斜带
   （`AddTriangleFilled`）。背景只剩 `renderFrame()` 画的 `background_overlay.png`
   （紫蓝渐变**带彩色图案**，别以为它是纯色）。
2. **不画舞台**：`main.cpp` 的 `AppState::Result` 分支传 `playfieldVisibility = 0.0f`，
   renderFrame 画完 background 就 return。
3. **判定计数 + COMBO 计数改用系统字体**（原来是 condensed 窄体）；`cond` 只剩 score-bar
   的 C/B/A/S 标记。`drawFontDigits` 的 `advance` 只是槽宽、字形在槽内居中，所以锚点不动。
4. **RESULT 大字只留 border**：`platform/FontOutline.cpp` 用 stb_truetype 光栅化再取
   「圆形膨胀 − 原覆盖」得到**真空心**轮廓，`Renderer::createTextureFromRgba()` 上传，
   结算时 `AddImage`（960x210，一次构建约 0.77 MB）。
   **不要改回"描边 + 背景色挖空"那一套** —— 挖空色必须精确等于背景色，而背景是带图案的
   图片，任何近似都露出色块。

**几何来自像素测量，不要凭感觉改。** 参考图是 2388x1080 的手机截图（Project SEKAI 官方
结算画面），用 `.workbuddy/tools/` 里的 python 脚本逐区域扫出来的数值，全部换算到
1920x1080 虚拟画布：

| 元素 | 参考图（2388 宽） | 虚拟画布 |
|---|---|---|
| 顶部条面板 | x 413..1973，y −45..177，圆角 50，白色 59% | 左边 413，右边贴到 1905（画布右缘 −15） |
| 曲绘 | 453..588，21..154（135×135，粉 #FF4577，内缩 11） | 同左（绝对 x） |
| 标题 | x 615，cap 40..67 | 同左 |
| EXPERT 胶囊 | 615..806（宽 191） | 同左；右侧接深色胶囊 806..996（宽 190） |
| 得分数字 | 右对齐 1233，字高 86，步进 61.6 | 同左（精灵见下） |
| 得分/最高得分标签 | 中心 (546,355) / (537,456)，字号 70 / 36 | 同左 |
| 进度条 | x 1246..1777（宽 531），y 80..104（高 24） | 右端 = 面板右缘 −196 |
| C/B/A/S 刻度 | 条内 746/990/1234/1478（**精灵自身的 1650 宽设计空间**），pin：梯形 63→82 + 竖线到 105 | 同比例 |
| SCORERANK 牌 | x 1798..1918（宽 120），y −20..175（顶部被裁） | 右端 = 面板右缘 −55 |
| 判定行 | 牌 x 450（宽 372 高 55 圆角 18），首行中心 y 656，行距 64.6；标签 x 472，数字右对齐 785 | 同左 |
| COMBO | 标签中心 951，数字右对齐 1225 | 同左 |
| 继续按钮 | 320×79，mint #77EDDD，深色字 | 右端 = 面板右缘，底 1039 |

**能复用素材就别画**（用户提的，2026-09-13 改）：原素材都在 `assets/mmw/overlay/` 里，
用上之后连形状都不用猜：

| 元素 | 素材 | 用法 |
|---|---|---|
| 评级大字（S/A/B/C/D） | `score/rank/chr/<x>.png` | HUD 早就注册成 `rank_char_<x>`，结算牌直接画 |
| SCORERANK 字样 | `score/rank/txt/jp/<x>.png` | 新注册成 `rank_jp_<x>`（HUD 分数面板用的 en 版不动） |
| PERFECT～MISS | `judge/v3/<1..5>.png` | 就是判定文字那 5 张，PERFECT 自带彩虹渐变，颜色/字形全对 |
| 结算 BGM | `assets/ost/BGM_LIVE_RESULT_2.mp3` | `AudioEngine::startResultBgm()` 流式循环 |

**这些精灵自带一圈发光边**，文件尺寸比实际字形大：所以 `drawSpriteInk()` 是**按 ink 框
定位**的（`drawSpriteInkCentered` 同理）。每个 sprite 的 ink 框都是量出来的，写死在
`judgeSprite[]` / 调用处：
`judge/v3/1..5` = ink 起点 +16～17、高 47～50；`rank/chr/a` = 224×266 里 solid 219×256
（+3+2）；`rank/txt/jp/a` = 1000×130 里 solid 952×114（+26+8）。
量法：`magick <png> -channel A -threshold 55% +channel -trim -format "%wx%h+%X+%Y" info:`

**排印上的几个坑：**

1. **字号必须乘 `c.scale`**。ImGui 的 `AddText(font, size, ...)` 的 size 是**像素**，不是
   虚拟单位。1920x1080 时 scale=1 看不出问题，1366x768 下所有文字会大 1.4 倍、评级字母
   直接冲出牌子——踩过一次，`game/Result.cpp` 里所有文字助手都在内部乘了 scale。
2. **只有 8 位总得分用记分精灵**（`score/digit/<d>.png`，并横向压 18%：`kDigitSqueeze`）。
   判定行计数和 COMBO 都是**普通字体**（原版就是 UI 字，只是大小不同），用
   `drawFontDigits()` 画，而且要走**窄体** `condensedFont()`（数字的宽高比 0.59 才对得上
   雅黑 Bold 的 0.62 偏宽）；文字标签才用 `boldFont()`（雅黑的字宽/字高 0.83 才和原版一致，
   Arial Narrow Bold 的 0.65 太窄）。字宽差一点时用 `textTracked()` 的 tracking 补。
3. RESULT 水印是**空心描边**（白 22% 描边 3px + 内部填背景色），不是实心灰字：
   `textOutlined()` 就是干这个的。

**「继续」按钮走事件层命中测试**（`resultContinueHitTest()`，和 HUD 暂停按钮、开场跳过同一套）：
结算画面不是 ImGui 窗口，触摸事件不带可用鼠标坐标，所以鼠标分支和 `SDL_FINGERDOWN`
分支各测一次（又是"触摸屏点不到"那个经典坑）。`drawResult()` 只负责画 + 悬停高亮。

**流程**：曲末（`trackDurationSec − 0.15`）切进 `AppState::Result`，停音乐、清触点，用
`judgement.stats()` 组 `ResultData`；成绩记录就在这个切换块里写盘（**不要**提前到
`trackDuration − 0.25` 那种更早的位置：曲子尾部还剩没判完的 note，自动 MISS 的窗口会
越过谱面末尾，那时读到的血量偏高，摔死的曲子会被记成 CLEAR）。`resultPreviousBest`
（= `ScoreRecord::bestScore`，新加的字段）必须在 merge **之前**取，否则永远不是新纪录。
- **CLEAR 的判定 = 打完时血量 > 0**（`st.life > 0`）：血量归零 = 失败，只留最高分、不打
  CLEAR。FULL COMBO 还要额外要求 `cleared`——长条中途断扣血但不计 MISS，`miss == 0` 也可能
  掉到 0 血。`--result-at`（调试，中途切结算）不写成绩。
点「继续」（或 ESC）回选曲。调试：`--result-preview`（启动即进，用参考图的样例数字）、
`--result-at <sec>`（跑到指定秒数切）。

### 结算页的 FULL COMBO / ALL PERFECT 与 LIVE FAILED / FINISH（2026-10-27，10-28 更正）

- ⚠ **10-27 那条"原作没有 LIVE FINISH"是错的**：当时只翻了 sprite 表（那里确实没有
  `finish` 字样的图），但**曲末特效是 prefab 不是 sprite**。`stringliteral.json` 里这一族是：
  `fx_result_finish` / `fx_result_clear_v2` / `fx_result_failed_v2` /
  `fx_result_full_combo_v2` / `fx_result_all_perfect_v2`。
  **`fx_result_finish` = 血掉完、曲子结束时播的那个大字**（用户实测确认）。
- 徽章（`badge_result_fullcombo` / `badge_result_allperfect`，358×76）**是结算页的**，
  和曲末特效是两回事：曲末另有 `fx_result_full_combo_v2` / `fx_result_all_perfect_v2`。
- 结算页的 `txt_liveclear_wh`(1220×166) / `txt_livefailed_wh`(1352×166) /
  `txt_liveresult_wh`(1272×166) 属于 summary 页（`LiveResultClear="liveclear"` /
  `LiveResultFailed="livefailed"`）。
- **第一段 summary 已实现（2026-11-01）**：`Result.cpp` 的 `drawSummary()`，`drawResult()`
  按 `kSummarySec = 2.6` 分两段（摘要 → 分数页，分数页的入场动画整体后移 2.6s）。
  内容是：标题 + **两条空心描边副本**（起始叠在标题正中、按曲线一上一下滑开；
  以前当成"装饰线只画一次"是误读）+ 彩虹分数条 + C/B/A/S 刻度 + SCORERANK 牌。
  ⚠ **刻度的比例尺和打歌 HUD 不是同一套**（结算条 C/B/A/S = 0.303/0.434/0.597/0.664，
  HUD = 0.452/0.600/0.748/0.896），填充必须走
  `scoreRankAndBar(score, rating, markPos)` 那个重载，否则「填到 A 刻度右边、牌上写 B」。
  几何 / 曲线 / 贴图来源全在 `RESULT-SUMMARY.md`。
- 我们现在的状态：曲末只有 `LiveClearBanner`（= clear 那支）；**finish / failed / FC / AP
  四支曲末特效都还没做**。结算页的徽章 + LIVE FAILED 已按 `ResultData::failed/fullCombo/
  allPerfect` 接上（见下）。
- **已提取（2026-10-31）**：五支里其实只有三支是 prefab —— `fx_result_LiveClear` /
  `fx_result_FullCombo` / `fx_result_AllPerfect`（`finish` / `failed_v2` 在资产里
  **不存在**，只是代码字符串）。结构、时间轴、贴图尺寸全部实测在 **`RESULT-FX.md`**；
  `fullcombo.png` / `allperfect.png` 已放进 `assets/mmw/overlay/result/`。

- 结算分数页带两张徽章 `badge_result_fullcombo` / `badge_result_allperfect`（358×76，
  ResultAtlas），由 `clip_result_score_{fullcombo,allperfect}_v2_01` 播 **2s**：
  **只动 `anchoredPosition.x` 4.2→44.2 与 `Color.a` 0→1**，没有 scale、没有弹跳。
- 我们的落地：`ResultData::failed / fullCombo / allPerfect`（`main.cpp` 的 `buildResultData`
  从 `JudgementStats` 推：`failed = life<=0`、`fullCombo = !failed && miss==0`、
  `allPerfect = 全 PERFECT`；和写成绩用的是同一套规则，preview 路径也一致）；
  `Result.cpp` 在 COMBO 右边的空带画徽章（滑动 40px + 淡入，2s），失败局在同一位置画
  `LIVE FAILED`（两者互斥）。⚠ 徽章锚点（`kBadgeCenterX/Y`）是**临时值** —— prefab 的
  RectTransform 这次没量到，等有人截一张官方结算图再校准。

