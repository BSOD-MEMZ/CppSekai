# ELUA

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## ELUA（首次启动的「关于本软件」弹窗）2026-09-18

- `ui::eulaDialog`（`game/Ui.*`）就是 `messageDialog` 的加强版：可带一段说明文字 +
  一个勾选框（"以后不再显示"）+ 一行按钮。**内容按 UTF-8 码点边界贪心折行**
  （`CalcTextSizeA(fontSize, FLT_MAX, 0, candidate)` 逐段量、取最长可容纳前缀）——
  用字节下标 `substr` 折行会把多字节序列切断，量出来的宽度是错的，中文文案必现溢出。
- 卡片宽度 = `max(660*s, buttonsW+56*s)`，高度由折行数推出；`beginCard(..., false, true, ...)`
  = 没有右上角关闭 X（必须点按钮才能走）。
- 开关存在 **profile 里**：`UserSettings::eulaAccepted`（`userdata.json` 的 `eulaAccepted`）。
  主界面首帧弹出（`main.cpp` 的 `AppState::Select` 分支之后），
  关闭时 `userSettings.eulaAccepted = eulaNoShow; persistUserData();`，日志 `[eula] dismissed (accepted=%d)`。
- 文案四段：① 免费开源非营利（AGPL-3.0）、与 SEGA / Colorful Palette / 《世界计划》官方无关；
  ② 不含官方代码/音频/曲绘/谱面，素材自行下载仅供本地学习；③ 勿商用、勿传播无权素材；
  ④ 按现状提供无担保，本机数据只存本地不上传。

