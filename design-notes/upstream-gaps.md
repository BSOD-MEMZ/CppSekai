# 与上游还没对齐的地方

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 与上游还没对齐的地方（2026-09-13 盘点）

对着上游 [sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web) 逐个查过的结论，按「值不值得做」排：

1. ~~**舞台背景生成**~~：**已做**（2026-09-13，2026-09-14 补齐两处）。`game/StageBackground.cpp`
   是上游 `src/lib/overlayBackgroundGen.ts` 的 C++ 移植：单应矩阵把曲绘投进舞台侧屏/中间屏
   **以及它们在下半部分屏里的暗倒影**（`kSideLeftMirror` / `kSideRightMirror` / `kCenterMirror`，
   对应上游 `MORPH_*_MIRROR`），再按 base/windows/bottom 顺序合成底板，最后由
   `toSquareBackground()` 铺成 2048x2048；由 `Renderer::setSongBackground()` 换掉默认背景贴图，
   开歌时按曲绘生成一次（约 1.1s）。
   - **铺方形那步不能省**（2026-09-14 修）：世界坐标的背景四边形在屏幕上是**正方形**
     （`worldBackgroundWidth/Height` 都是 backgroundSize 虚拟像素），默认板
     `background_overlay.png` 本来就是 2048x2048；bggen 板只有 2048x1168（1.75:1），
     直接当纹理上传会被纵向拉伸 1.75 倍——症状就是"打歌背景变形"。上游 native 的
     `composeOverlayBackgroundV3()` 末尾同样调 `renderToSquareBackground()`。
   - **mirror 那组不是"另一种布局"**（2026-09-14 修）：早先注释写"mirror 是镜像布局用的、
     我们没有"是误判。`center_mask` / `side_mask` 在 y≈680..1040 是有 alpha 的
     （191 / 128），也就是说这些倒影屏本来就会合上去，只是亮度低；不做的话画面下半部分
     那几块屏是空的（截图 diff 集中在 y≈660..1020）。
   - 想缩短生成时间的话，瓶颈是整块底板的 overlay 遍数（可按四边形包围盒裁）。
2. **长条 / guide 浓度没做成设置项**：上游有 `holdAlpha`（默认 1.0）/ `guideAlpha`（0.8），
   我们虽然把参数传了（guide 0.6）但没进设置面板。顺带一提上游后来把"长条只在头判激活后
   才显示"（`segmentActivated`）做进了核心，我们用的是另一套（`markNoteHit` /
   `setMissedHolds` / `setDimmedHolds`）。
3. **核心落后上游一点**：`core/native/src/mmw_preview.cpp` 与上游现版差 24 行上游独有内容
   （长条绘制重构、`hitEvents` 按 (time, center) `stable_sort`、trace 头显示修复）。同步会跟我们
   的 hit 驱动改造撞车，要动就单独开一轮。
4. **音效增益**：上游按 kind 分档（perfect/criticalTap .75、flickCritical .8、trace .82、
   tick .92、holdLoop .7），我们只按判定档位（Good/Bad 减半）。
5. 小事：上游有英文 AUTO 徽章 `autolive-en.png`（它自己也没用）；`noteSpeed` 默认它 10.5 我们 8.0；
   上游还支持 MMW 谱面 Maker 的 `custom_score_json`，我们只吃 SUS。

已经补齐的：HUD 的 `+N` 加分浮动、右下角 `AUTO LIVE` 徽章、判定文字用对应档位的精灵
（上游 native 那边永远画 PERFECT，因为它是预览器）。

