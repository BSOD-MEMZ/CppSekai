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

## 2. 实现（2026-11-01 已落地）

`game/Judgement.hpp`：

```cpp
struct JudgeWindow { float earlyMs; float lateMs; };   // 早 / 晚，分开
struct TypeWindows { JudgeWindow perfect, great, good, bad; };
JudgementWindows::windowsFor(WindowType)  // 11 行，按音符类型取
```

`WindowType` 11 行 = 本表里实际用得到的那些：`Normal / NormalCritical / Flick /
FlickCritical / LongEnd / LongEndCritical / LongEndFlick / Friction /
FrictionCritical / FrictionLongEnd / FrictionFlick`。行怎么选在
`windowTypeFor(kind, critical, holdTail)`：`critical` 是 flags bit0，
hold 尾判走 `LongEnd*`（因为它是按**松手**判的）。

**用户设置仍然是四个数**（Perfect/Great/Good/Bad），含义是「normal 这一行」。
其余类型 = 官方行 + 同一个偏移（`delta = 设置值 − 官方 normal 值`）。这样
宽松/标准/严格三个预设在所有类型上都整体平移，而每类的形状（不对称、friction 的
全档同宽）不会丢。

三处评级都换掉了：

| 位置 | 原来 | 现在 |
|---|---|---|
| `findCandidate` 命中评级 | `fabs(dt)` 比四个全局数 | 有符号 `delta`（正=迟），按该音符的行判 |
| `findCandidate` 候选筛选 | `dt <= badMs` | 超出该音符自己的 BAD 窗口直接不接（否则会判成 BAD 而不是 MISS） |
| 自动 MISS | 统一 `missAfterMs` | 该音符自己的 `bad.lateMs` |
| hold 尾判 | 全局四个数 | `LongEnd*` 行，按松手时刻算 |

预设与迁移（`main.cpp` 的 `kJudgePresets`、`game/SongSelect.cpp` 的 `loadUserData`）：

| | P | G | Gd | B | 说明 |
|---|---|---|---|---|---|
| 旧「标准」 | 40 | 90 | 140 | 200 | 自称官方表，**实测不符**；迟按到 200 ms 还算 BAD |
| **标准** | **42** | **83** | **108** | **125** | = `normal` 官方行 |
| 宽松 | 72 | 113 | 138 | 155 | 官方 + 30（默认） |
| 严格 | 12 | 53 | 78 | 95 | 官方 − 30 |

老存档里正好是 `40/90/140/200` 的（那是默认值，不是谁的选择）在读取时自动改成
新表，并打一行 `[settings] judgement windows migrated to the official table`。

## 3. 还没还原的

- [ ] `friction` 现在拿到的是**正确的窗口**（全档同宽），但判据还不是官方的
      「摸到就算」—— 我们仍按普通 tap 的距离判，只是四档窗口一样宽。
- [ ] `friction_flick` 在 core 里被 `isFlick()` 先截走（`mmw_preview.cpp:1225`），
      拿到的是 flick 的语义而不是 friction 的。
