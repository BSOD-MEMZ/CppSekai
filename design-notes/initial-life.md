# 初始血量 / Flick 调试日志

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 初始血量 / Flick 调试日志（2026-09-16 晚）

- **初始血量上限 5000（`kMaxInitialLife`）**：`settings > 判定 > 初始血量` 的滑杆和
  `loadUserData` 的 clamp 都是 100..5000。**bar 按「本局开局的血量池」归一化**
  （`JudgementEngine::lifeRatio() = life / max(1, mInitialLife)`），所以不管设多少，
  演出界面开局都画在 100%（以前 488 就从 48.8% 开始、5000 会直接画出面板右边）。
  `lifeCeiling() = max(kMaxLife, mInitialLife)` 是血量**数值**的上限，三个扣血点
  （`registerMiss` / BAD / 长条中断）都必须用它 clamp —— 用 `kMaxLife` 会把 5000 的池子
  在第一次扣血时直接压回 1000。日志里的 `[score] life=X/Y` 里 Y 也用 `initialLife()`，
  `[stats]` 的百分比走 `lifeRatio()`。
- **面板上的数字是真实血量，胶囊是百分比**（2026-09-19 修）：`HudState` 有两个字段——
  `lifeRatio`（0..1，喂血条）和 `lifeValue`（`JudgementStats::life` 原值，喂 `life/v3/digit/*`
  那几位数）。**以前数字是 `1000 * ratio` 写死的**，所以初始血量设成 5000 时，
  数字从一开始就写 1000、每次 MISS 掉 16（看起来"扣血变慢"），而血条其实一直是对的。
  实测（0001_master，autoplay 8 miss、池子 5000）：数字 `4360`、血条 87.2%，
  与 `[stats] life=4360 (87.2%)` 一致。
- **Flick / 触摸调试日志**（`settings > 判定 > 触摸轨迹日志`，勾选即生效并写进档案；
  命令行等价物 `--flick-log`，不落盘设置、只给支持/回归用）。开启时先把
  `flick_debug.log`（exe 工作目录）清空再逐行 flush，内容：
  - `[traj] down/move/up`（2026-11-02 加）：**手势里每一个 SDL 手指事件**，写在
    "这个触点算不算打击区"的过滤**之前**（`beginPointer` 会把落点在地平线以下的
    触点直接丢掉、一行不留），所以能区分"这块屏根本不发 motion"和"滑得太短/起点太低"。
    up 行汇总 `samples` / `held` / 总 `dx dy` / `peak` px/s / `maxGap`——采样只有 2~3 个、
    或者中间有 40ms 空洞，一眼就能看出来。move 行带本帧 gap。
  - `[touch] down` / `[touch] move` / 每次 flick 判定尝试的 `[flick] fire/move|fire/up`；
    move 行有**每个原始采样**：屏幕坐标、位移、`dt`（以及未 clamp 的 rawDt）、低通后的
    vel、travel、当前分类出的 dir、lane。这些只覆盖**被判为打击**的手指（落点在打击区内）。
  - 没打中时 `logFlickOutcome` 会调 `JudgementEngine::debugFlickNotesNear(t, 0.6s)`
    把附近的 flick 音符按时间距离列出来（`dt` / laneDelta / halfWidth / wantDir / state），
    这是判断"方向错 vs 轨道错 vs 根本没到判定窗口"的关键。
  - 所以用户报"触摸 flick 老 MISS"，**让他开着这个开关打一遍再把 flick_debug.log 发来**
    就行（日志很密，一首歌几千行；`[traj]` 每个采样一行，长滑一次几十行）。
  - 实现注意：日志结构体 `FlickDebugLog` 在 main.cpp 匿名命名空间（文件级 `gFlickLog`），
    `setEnabled()` 里会 truncate；**变量别叫 `near`**（windef.h 把它定义成空宏，
    声明会被吃掉，直接编译不过）。轨迹状态 `trajTouches` 在事件循环之前声明，
    `trajRaw` 由 `while (SDL_PollEvent)` 上方那一段按事件类型调用——**不要在
    `case SDL_FINGERDOWN` 里调**，那里已经被 `state != Play` 之类的分支切碎，漏事件。

