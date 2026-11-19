# UI 音效

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## UI 音效（2026-09-15，`ui::se` / `ui::flushSe`）

- 素材在 `assets/se/`：`click.mp3`（任意组件按下）、`select.mp3`（选曲列表每动一格 /
  手柄焦点环移动）、`slide.mp3`（**设置卡里滑杆每动一格**，`SeSlide`，2026-09-22 加）、
  `level_choose.mp3`（难度按钮）、`window_open.mp3` / `window_close.mp3`（卡片 / 弹窗）、
  `start.mp3`（点「确定」起白光的那一下，`SeStart`，优先级最高）。
  `AudioEngine::loadUiSe` 在 `loadSe` 末尾加载（每种 3 个声部），**缺文件只打印一行日志**，
  不报错——所以精简包 / 无素材时界面照样能跑，只是没声音。
  加一种新音效要**同步改三处**：`ui::SeKind`、`platform::AudioEngine::UiSe`（**同样的顺序**，
  `flushSe` 直接 `static_cast`）、`loadUiSe` 里的 `kFiles[]`（还有 `Ui.cpp` 里的
  `kSeRequestCount` static_assert 和 `CPSEKAI_SE_TRACE` 用的名字表）。
- **`SeSlide` 故意排在 `SeSelect` 后面（= 优先级更高）**：滑杆动一格和焦点环挪一下可能落在
  **同一帧**（手柄摇杆一次压过两个方向，`padNav` 是按方向逐个调的），那时该听见的是"格"，
  不是环。调试开关：`CPSEKAI_SE_TRACE=1` 会在帧尾打一行 `[se] <名字>`，
  无头跑（`--settings --fake-pad DOWN,RIGHT`）就能断言滑杆确实响了——没有这张表就只能靠猜。
- 滑杆的"一格"是**跨过 `step` 的边界**，不是"值变了"（拖动时值每帧都在变）：
  `slider()` 进来时记 `stepSlot(value)`，出去时比一次，只有槽位变了才 `se(SeSlide)`。
  于是拖拽 / 深色 ± / 手柄左右三条路都正好一次一格（± 在两端被 clamp 掉时不响）。
- 播放**不在按下瞬间**：组件只 `ui::se(...)` 记一个请求，主循环帧尾调一次 `ui::flushSe()`
  （`main.cpp` 里紧挨 `ImGui::Render()` 之前），由它挑**本帧最高优先级**的那一个播。
  `ui::SeKind` 的顺序 = 优先级（click < select < slide < level_choose < window_open < window_close），
  必须和 `platform::AudioEngine::UiSe` 一一对应（目前直接 static_cast）。
- 为什么要这么绕：**`window_open.mp3` 里本身就混了 click 声**。按下的那一帧弹窗同时开，
  如果 click 也播就成了双击；同一帧里更高的那个（open/close）把 click 顶掉，正好对上官方手感。
  所以"按下就开窗 / 关窗"的地方**只报 click 就够了**，不用额外屏蔽。
- 开 / 关音的触发点在 `beginCard` 里（按 `CardState::soundOpen` 每局只响一次）；关音走
  `requestClose(st)`，它顺手清 `soundOpen`，这样点 X / 点按钮的那一帧就响 close、下一帧不会
  再响一次。X 的 `*closeClicked` 语义没变。
- 接好的地方：`game/Ui.cpp` 全部组件（滑杆、勾选框、stepper、combo、卡片 X）、
  `main.cpp` 的 HUD 暂停键、`game/SongSelect.cpp` 的滚轮 / 方向键 / 拖拽落点 / 分区字母 /
  难度按钮 / 确定 / 随机 / 切换歌手（+ 面板里的卡片与 X） / 重扫。**拖拽连续滚动只在落点响**（按格响只给
  滚轮和键盘，否则 fling 会连成一片噪音）。

