# 右键 / 触摸长按 / 自定义键位

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 右键 / 触摸长按 / 自定义键位 / 帧率显示（2026-09-26）

一批输入与列表交互的补强，几处都不是"加个开关"那么简单：

**判定时刻改用事件时间戳（"键盘不够灵敏"的正解）**：判定原来用 `songClock()`，也就是
"主循环轮到这一帧的时刻"。按键先排进 SDL 的事件队列，下一次 `PollEvent` 才被看见 ——
低帧率下这一等就是十几到几十毫秒，全被算成"你按晚了"。现在走 `songTimeAtEvent(eventMs)`：
`songClock() - clamp(SDL_GetTicks() - eventMs, 0, 0.25)`（两者同一基准），键盘
（`event.key.timestamp`）、指针按下（`beginPointer` 早就把 `eventMs` 传进来了）、flick
移动三处一起改。**判定路径里别再直接用 `songClock()`。**

**右键 / 长按 = 右键**：列表行右键弹收藏菜单，搜索框右键弹 复制 / 剪切 / 粘贴 / 清空
（ImGui 不公开 `InputTextState`，所以用 `ImGuiInputTextFlags_CallbackAlways` 把
`SelectionStart/End` 抄进一个 static 快照）。触摸屏上 SDL 不会替我们合成右键，main.cpp
自己计时：`SDL_FINGERDOWN` 记起点和时间，按住 550 ms（`kLongPressMs`）且位移 < 14 px 就往
ImGui 的输入队列注入 `AddMousePosEvent` + 右键 down/up —— **注入要放在事件循环之后**，
ImGui 要下一帧 `NewFrame` 才消费，弹出的菜单正好落在手指位置。手指一滑超过 slop 立刻取消，
不然跟列表的拖动滚动打架。

**收藏夹 + 「按收藏」分组**：收藏按 **musicId** 记（同一首曲子的四个难度共享），存
`settings.favoriteMusicIds`，`applyFavorites()` 填 `ChartEntry::favorite`，右键菜单切换。
分组多一档 `kGroupFavorite = 4`；`buildRows()` 里收藏段整体排前、段内照旧排序。
⚠️ **`loadUserData()` 里那句 `std::clamp(settings.groupMode, 0, 3)` 是写死的** —— 加第 5 档
之后它会把 4 咬回 3（表现是"设了按收藏，下拉却显示按首字"）。已改成 `kSelectGroupCount - 1`，
**这个上限现在只有一个出处（`SongSelect.hpp`）**，以后再加档改那里。
另外 `entries` 的每曲状态在多条路径上会被重贴（首次扫描 / F5 重扫 / 换 profile / 结算），
所以成绩和收藏统一走 `refreshEntryFlags()`，别再单点调 `applyScores()`。

**自定义键位**：`settings.laneKeys[12]`（SDL_Keycode 的数值，空数组 = 用 `kLaneKeys` 的默认
z s x d c v g b h n j m），`applyLaneKeys()` 在启动和切 profile 时刷新 `laneKeys[]`。
UI 在 **设置 → 演奏 → 按键映射**（3 列 × 4 行，点一格再按一个键）；新键已被别的 lane 占用
就**直接对调**，不用先清空，ESC 取消。等键状态是 main() 作用域的 `rebindLane`，`SDL_KEYDOWN`
开头就把它吞掉 —— 改键位的时候不该顺手打出一个 note。
（演奏页因此变成要滚动：`[ui] tab 0 used=1027 view=778 scrollMax=245`。）

**左下角帧率**：`settings.showFps` → `game::drawHud(..., float fps)` 最后那个参数（0 = 不画），
画在 1920x1080 虚拟画布的 `(24, 1080-26)` —— 多人分数条是从 `1080-24` 往上堆的，底下这 24px
正好空着。数值每 0.25 s 采一次瞬时帧率（逐帧刷新数字跳得读不出来）。截图自检会打一行
`[fps] 60.0 (frame 16.7 ms)`，**别靠读图判断**：10 px 字号的 "60.0" 在图上跟 "0.0" 长得一样，
我第一次就被坑了。

