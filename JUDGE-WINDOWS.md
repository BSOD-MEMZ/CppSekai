# JUDGE-WINDOWS.md —— 原作判定窗口（master 数据实测）

> 2026-11-01。数据源：`Sekai-World/sekai-master-db-diff` 的
> `ingameJudgeFrames.json`（日服 master DB 的自动 diff 仓库），
> 对应 `dump.cs` 里的 `MasterIngameJudgeFrame`。
> **单位是 60fps 的帧**，换算 `ms = 帧 × 16.6667`。

## 1. 完整表（毫秒）

`Before` = 早按方向，`After` = 迟按方向。**原作是逐音符类型、且左右不对称的**。

| 类型 | Perfect | Great | Good | Bad |
|---|---|---|---|---|
| `normal` | ±41.7 | ±83.3 | ±108.3 | **±125.0** |
| `normal_critical` | ±55.0 | ±75.0 | ±108.3 | ±125.0 |
| `flick` | ±41.7 | 108.3 / 125.0 | 116.7 / 133.3 | 125.0 / **141.7** |
| `flick_critical` | ±58.3 | 108.3 / 125.0 | 116.7 / 133.3 | 125.0 / 141.7 |
| `long` | ±41.7 | ±83.3 | ±108.3 | ±125.0 |
| `long_end` | 58.3 / 66.7 | 108.3 / 133.3 | 125.0 / 141.7 | 125.0 / 141.7 |
| `long_critical` | ±55.0 | ±75.0 | ±108.3 | ±125.0 |
| **`friction`** | **±83.3** | **±83.3** | **±83.3** | **±83.3** |
| `friction_critical` | ±83.3 | ±83.3 | ±83.3 | ±83.3 |
| `friction_long` | ±83.3 | ±83.3 | ±83.3 | ±83.3 |
| `friction_long_end` | 108.3 / 133.3 | 108.3 / 133.3 | 108.3 / 133.3 | 108.3 / 133.3 |
| `friction_flick` | 108.3 / 125.0 | 108.3 / 125.0 | 108.3 / 125.0 | 108.3 / 125.0 |

读数要点：

- **`normal` 的 Bad 是 125 ms，不是 200。** 超过 125 ms 直接 MISS。
- **`normal_critical` 的 Perfect 更宽（55 vs 41.7）**，但 Great 更窄（75 vs 83.3）。
- **`flick` 整体比 `normal` 宽、而且右边更长**（迟按更宽容），这正是"甩上去容易晚"。
- **`long_end`（长条尾判）明显右偏**：Perfect 58.3/66.7 —— 早松手比晚松手严格。
- **`friction` 全档同一个窗口 ±83.3 ms**：它不看提前/滞后，只看"有没有摸到"。
- `friction_flick` / `friction_long_end` 不用这个统一窗口，各有自己的。

## 2. 我们的现状

`game/Judgement.hpp` 的 `JudgementWindows` 是**一套对称窗口**（perfect / great /
good / bad / missAfter 各一个数），**没有按音符类型分表，也没有左右不对称**。

判定预设（设置 > 判定 > 判定窗口，`main.cpp` 的 `kJudgePresets`）原来写的是：

| | P | G | Gd | B | 说明 |
|---|---|---|---|---|---|
| 旧「标准」 | 40 | 90 | 140 | 200 | 自称官方表，**实测不符** |
| **新「标准」** | **42** | **83** | **108** | **125** | = `normal` 的官方值 |
| 新「宽松」 | 72 | 113 | 138 | 155 | 官方 + 30（默认） |
| 新「严格」 | 12 | 53 | 78 | 95 | 官方 − 30 |

旧的 `标准` 在 Good 上宽了 32 ms、Bad 宽了 75 ms（200 vs 125）——**迟按到 200 ms
还算 BAD 而不是 MISS**，是从前最明显的一处偏差。已按官方表改正。

## 3. 还没还原的

- [ ] **逐类型窗口**：`flick` / `long_end` / `friction` 各用各的，我们现在全用一套。
- [ ] **左右不对称**：`Before` / `After` 分开，我们只有 ±一个数。
- [ ] `friction` 的"全档同窗"语义：现在 `friction` 走 kind 3（trace）但用普通窗口，
      官方是 ±83.3 一刀切 —— 也就是**摸到就是最高档**，判据完全不同。
- [ ] `friction_flick` 在 core 里被 `isFlick()` 先截走（`mmw_preview.cpp:1225`），
      拿到的是 flick 的语义而不是 friction 的。
