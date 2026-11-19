# 标题画面

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 标题画面「TAP TO START」（2026-10-28 做过，**2026-10-29 按要求整块删除，代码和素材都不在了**）

> **现状：功能与素材全部移除。** `game/TitleScreen.*`、开屏 SEGA / Colorful Palette 卡、
> `UserSettings::showTitleScreen`、main.cpp 里的全部接线，以及 `assets/title/`（20 个文件）
> 都已删除；`grep -i "TitleScreen|titlePending|showTitleScreen|assets/title"` 现在零命中。
> 启动直接进选曲。下面这段只是**留档**：万一日后想做，素材取法和实测数值都还成立，
> 但代码得重写。

- 素材来自日服 7.1.0 包（sprite 用 AssetStudioModCLI 按名字 dump）：
  `tex_title_logo_mult_01.png` 1968×416（logo 整图）、`txt_taptostart.png` 560×48、
  `tex_title_copy_01.png` 734×46（版权行）、`tex_title_jasrac.png` 698×64、
  `tex_title_soft.png` 268×80（提示后面的柔光）。
  ⚠ **开屏那几张不是 sprite，是 `globalgamemanagers` 里的 Texture2D**，按 sprite 名单
  （`as_out`）一辈子也搜不到：`logo_sega`(766×256) / `logo_colorfulpalette`(1080×151) /
  `Splash Screen Unity Logo`(2048×1024) / `splash_attention`(1504×808)。
  取法：`AssetStudioModCLI <GameData>/assets/bin/Data/globalgamemanagers -m export -t tex2d -o ...`
  （单文件输入，866 个资产，几秒出完）。
- **开屏时序**在 `TitleController` 里（dump.cs :322615 附近）：
  `SEGA_LOGO_DISPLAY_TIME = 1`、`CP_LOGO_DISPLAY_TIME = 2`、`ANIMATION_TIME = 1`、
  `MIKU_CHARACTER_ID = 21`（标题背景那个初音是 Live2D，运行时下载，包里没有）。
- **原作背景不在包里**（运行时下载的角色/房间动画），当年只能"跟随桌面壁纸"——
  复用 `refreshSelectBackdrop()` 那份纹理，cover-fit + 压暗。
- 布局用 1920×1080 虚拟画布（和 HUD/结算同一套 `Canvas`），每个元素只写**目标宽度**、
  高度按源图比例算 —— 改一个数不会把美术拉变形。
- 交互的关键点只有一个：`titlePending` 期间把 `io.MousePos` 挪到屏幕外。
  选曲列表是读**原始 `io.MousePos`** 做命中测试的，这是唯一的输入闸门；
  另外 `escapePressed` 那条「选曲按 ESC 退出游戏」的分支得加 `!titlePending`。
- 「点击开始」这类**中文文字**必须用 `game::bodyFont()`，不能用 `ImGui::GetFont()`
  —— 前景 draw list 当时的字体是 ImGui 内置**ASCII 位图字**，中文会整行变成 `????`（真踩过）。
- 国服开屏是几张静态图（没有 Live2D）。**国服的包无法逆向**，素材只能从
  [资产浏览器 | Moesekai](https://pjsk.moe/zh-cn/asset-viewer/?server=cn) 的 CDN 取：
  `GET https://storage.pjsk.moe/api/assets/bundles?server=cn&prefix=title_screen/<dir>&limit=200`
  给出 bundle 的完整 `path`，再
  `GET /api/assets/bundle-files?server=cn&path=<bundlePath>&limit=200` 拿到 `url`，
  本体在 `https://storage.pjsk.moe<url>`。`curl.exe` 打这个站要 `--ssl-no-revoke`。
  当年用的两张：`title_background.png`(2338×1440 粉青斜带底图)、
  `tex_projectsekai_front.png`(512×128 logo)，底图 cover-fit 铺满。

