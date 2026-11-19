# 开场卡片的跳过键

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 开场卡片的跳过键（2026-09-21，`game/Intro.cpp`）

- **形状**：照 pjsk 的圆形返回键做的——白圆盘 + **柔和的投影**（不是描边圈）+ 深藏青图标。
  尺寸和位置**跟着演出中的暂停键走**：中心取 `lifePauseRect()` 的中心，圆盘半径 40（= 生命条
  贴图里那个键的圆盘半径，2026-09-21 从 1280x720 截图量出来的），所以开场结束的瞬间这个键
  原位变成暂停键。几何常量在 `Intro.cpp` 的匿名 namespace 里（`SKIP_BTN_RADIUS_PX` /
  `SKIP_BTN_ICON_W_PX` / `skipButtonCenter()`），`drawIntro()` 和 `introSkipHitTest()`
  **共用同一份**；命中测试是**圆形**（半径 + 6px 余量），不是矩形。
- **阴影怎么画的**：这版 ImGui（1.92.5）没有阴影图元，`ui::dropShadow()` 是三层同心形状
  叠出来的（越外面越大越淡，只露边缘），见 `game/Ui.*`。
- **图标是 `assets/mmw/ui/skip.png`**：44x28 的**纯白 ">>"**（白色 + alpha，没有别的颜色）。
  `Renderer::loadHud` 注册成 `ui_skip`，绘制时用 `AddImage` 的 tint 染成藏青
  （`61,60,92` 是从 pjsk 参考截图里取样的均值）—— **所以不需要拿 PS 换色，也不要往这个
  PNG 里烤颜色**（染不了的话就失去 hover 提亮的能力）。同一张图在 `combo`、别处若要用，
  自己 tint 就行。
- **只画不判**：点击走 `main.cpp` 的事件层（鼠标 / 触摸 / 手柄 A 三条路），
  `drawIntro()` 只负责画 + 悬停放大/按压回弹（`ui::anim`，id 常量 `0x4a5530xx`）。
- **位置与 HUD 暂停键不冲突**：暂停键在右上角，但开场卡片期间 `openingPlayfieldVisibility()`
  是 0，`drawHud()` 根本不画、`hudPausePress()` 也主动吞掉点击。
- **截图**：卡片只活在谱面时间 0 之前（`outputTime = songTime + leadIn`，而 `leadIn >= 5.8s`），
  普通 `--screenshot` 抓不到。用 `--intro-preview [sec]`（默认 1.5）把输出时钟钉住再截。

