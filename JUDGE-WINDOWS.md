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
- [ ] **flick 的手势逻辑一篇都没实现**（位移门槛 / 方向点积 / JustPerfect /
      方向错降 Great+FlickMiss / 先记录后定案）→ 见 §4。

## 4. flick 手势判定（2026-11-06，反汇编 libil2cpp.so）

> 来源：日服 7.1.0 `lib/arm64-v8a/libil2cpp.so`（199 MB），配合 `dump.cs` 的
> RVA 定位方法体。用的是 `Sekai.Live.FlickNote` / `NoteUtility` / `LiveConfig`。
> 反汇编脚本：`.workbuddy/tools/il2cpp_disasm.py`、反查方法名：`il2cpp_where.py`。

### 4.1 谁判的

| 情形 | 方法 | JudgeFrameType |
|---|---|---|
| 独立 flick（`parentNote == null`） | `FlickNote.Judgment` → `NoteUtility.CalculateFlickNoteResult` | `Flick(2)` / `Flick_Critical(7)` |
| 长条结尾 flick（`parentNote != null`） | 同前 → `CalculateLongEndFlickNoteResult` | `Long_End_Flick(5)` / `_Critical(10)` |

两者共用 `FlickNote.UpdateDelta` / `AddInputResult` / `JudgmentFlick`，只在
**入口门槛**和**评级表**上分叉。

### 4.2 手势算不算 flick（`UpdateDelta`）

```csharp
// 输入是 touch.delta（这一帧的位移，像素）
float t   = Time.deltaTime / LiveUtility.OneFrameTime;   // = 这一帧相当于几个 1/60s
float vx  = t > 0 ? delta.x / t : delta.x;              // 归一化成「60fps 下这一帧的位移」
float vy  = t > 0 ? delta.y / t : delta.y;
if (vx*vx + vy*vy <= flickDistance*flickDistance) return;  // 这一帧不算「甩了」
float dot = normalize(v) · normalize(dir);
isTrueDirection = dot >= 0 || Direction == Default;
```

要点：

- **门槛是「瞬时速度」不是「累计滑动距离」**。慢慢拖过去永远不会成为 flick，
  必须某一帧内的位移够大。按 `deltaTime` 归一 ⇒ 低帧率不会更难甩。
- **方向判据只有一个符号点积**，没有角度容差：`dot >= 0` 就算对（< 90°）。
- `dir` 按 `NoteDirection` 取 `LiveUtility.Vector2Left` = (-1,0) /
  `Vector2Right` = (1,0) / 默认 (0,1)。
- **`Direction == Default` 的 flick 反着甩也算方向对** —— 无箭头 flick 不看方向。
- `flickDistance` 字段在 `FlickNote` 的 0xF8，运行时由
  `GenerateNote()` 从 `LiveBundleBuildData.FlickDistance`（SerializeField，0x40）
  **原值直传**，单位就是「像素 / 每 1/60 秒」。
  （`LiveUtility.ScreenDpiToInch(x) = x * Screen.dpi / 2.54` 只在谱面编辑器的
  `ConvertFlickNote` 里调，而且那边传的是 **0** ⇒ 编辑器里门槛恒 0。）
  ⚠ **这个 ScriptableObject 资源不在我们的解包里，具体像素值未知。**
  要抄的话建议自己定（原值量级 = 一两根手指快速划过的单帧位移）。

### 4.3 时序 / 状态机

`IsJudgment` 门槛（每帧、每个 touch 问一次）：

1. `State == Done` → 否
2. `LiveUtility.IsJudgmentTiming(frameType, noteTime - touchTime)` → 必须落在**本类型自己的
   Bad 窗口**内，否则根本不接（所以「超出 Bad 窗口的触摸」不会把音符判成 BAD，只会等它 MISS）
3. **独立 flick 且 `State == Playing`：只接 `phase == Began` 的 touch**
   （`TouchPhase` 是 `UnityEngine.InputSystem.TouchPhase`，Began=1）

`Judgment` 里的状态迁移（`FlickNote.State` 是**重写过的**属性）：

- `Playing(1)` → `Input(3)`，然后必须 `State == Input` 才继续
- **`phase == Began` ⇒ 只调 `AddInputResult` 记一笔，`return false`，不判**；
  真正定案发生在后续 `phase == Moved` 的帧上 —— 也就是「先按下去，再甩」两段式
- `inputResults` 是**定长 8** 的 `(int id, bool distance, bool trueDirection)` 数组
  （最多 8 指，按 `touchId` 复用槽位），`distance` 是「某一帧过过 4.2 的门槛」

`JudgmentFlick` 挑哪个记录来定案：

1. 有 `distance && trueDirection` 的条目 → 直接用它（正常路径）
2. 否则退一步找**只有 `distance`** 的条目 → 用它的 `trueDirection` 判（甩反了也判，降级）
3. 都没有 → 不判，等下一帧

长条尾 flick 的额外门槛（`parentNote != null` 分支）：

- **不走 phase 判断**
- 要求 `noteTime - touchTime <= -1/120`，也就是**必须比音符时间晚 ≥ 8.33 ms** ——
  长条尾 flick 结构上不可能提前判
- 要求 `isTrueDirection` 为真，**否则直接 `return false`（不判、保持 pending）**
  —— 长条尾甩反了不会降级，是压根不认

### 4.4 评级（`NoteUtility.GetJudgeResultInternal`）

`offset = noteTime - touchTime`，**正 = 早**。窗口 = `[-*After, +*Before]`，
和 §1 表的 Before/After 读法一致（反汇编验证过，§1 的换算没问题）。

```
if (isDirection) {
    if (|offset| <= 1/120s)  -> JustPerfect(8)          // LiveConfig.JustPerfectJudgeTimeHalf
    if (在 Perfect 窗口内)   -> Perfect(7)
} else {
    // 没有 JustPerfect 这一档
    if (在 Perfect 窗口内)   -> (Great(6), Description = FlickMiss(4))
}
if (在 Great 窗口内) -> Great(6)
if (在 Good  窗口内) -> Good(5)
if (在 Bad   窗口内) -> Bad(3)
否则 -> Miss
```

⇒ **甩反方向的唯一后果是「上限 Great」**，不是 MISS；而且只有正好落在 Perfect 窗口内
时才会打上 `FlickMiss` 这个描述（表现层用来显示特殊字样）。时间本来就差到 Great/Good
档的，描述仍是正常的 Fast/Late。

`LiveConfig` 里的常量（`.cctor` 反出来的字面量）：

| 常量 | 值 |
|---|---|
| `JustPerfectJudgeTime` | `1/60 = 16.667 ms` |
| **`JustPerfectJudgeTimeHalf`** | **`1/120 = 8.333 ms`** ← JustPerfect 用的就是它 |
| `JudgeTime` | `0.125 s` |
| `Life` / `MaxSkillCount` | `1000` / `6` |

描述映射 `GetNoteResultDescription(offset)`：`|offset| <= 8.33ms` → `Just(3)`；
早 → `Fast(1)`；晚 → `Late(2)`。

### 4.5 自动 MISS

`NoteBase.Excute`：`OffsetJudgeTime = now - noteTime`（**正 = 晚**），
`> LiveConfig.noteTypeJudgeData.JudgeTimeAfter` ⇒ `(Miss, None)` + `State = Done`。

`JudgeTimeAfter` 是 **`UpdateJudgeTimeBeforeAndAfter` 对全部类型取 max(BadAfter)**，
**不是逐类型**：当前表里最大值是 8.5 帧 = **141.7 ms**（flick 那一行给的）。
所以 `normal` 音符其实也会一直活到 141.7 ms 才 MISS —— 只是 125 ms 之后已经没有
触摸能接住它了（`IsJudgmentTiming` 挡住）。

`FlickNote.Excute` 另有一条兜底：`State == Input` 且距离 `lastInputFrame` 已过 ≥1 帧时，
还会再主动调一次 `JudgmentFlick(currentFrameInfo.time)`。

### 4.6 我们现在的实现 vs 原作

| 项 | 原作 | 我们（`main.cpp` 的 `flickDirFrom` + `game/Judgement.cpp`） |
|---|---|---|
| 手势判据 | 单一位移向量长度 `|v| > flickDistance`（px/帧@60fps） | 双轴分离：`upSpeed > 380/900 px/s`、`sideSpeed > 450/900`，或行程 `> 56/150 px`；另有 `minTravel 12px` |
| 方向 | `dot(normalize(v), dir) >= 0`（≈±90°） | **已对齐（11-06）**：`flickVector()` 收滑动向量，左/右音符只判 `vx` 的符号，Default 恒真 |
| 帧率归一 | 显式 ÷ `deltaTime/(1/60)` | 用 px/s 速度（天然帧率无关），但采样窗口 `min(rawDt, 30ms)` |
| 重复触发 | 无锁，每帧都试 | `kFlickRefireSec = 60ms` 锁存 + 行程清零 |
| 方向错 | 上限 Great；Perfect 窗口内 → **(Great, FlickMiss)** | **已对齐（11-06）**：仍进候选、上限 Great（Perfect→Great）；有方向对的候选时优先选它。`FlickMiss` 字样未做 |
| JustPerfect | ±8.33 ms，**仅方向对时** | 无这一档 |
| 触发时机 | `Began` 只记录 → 下一帧 `Moved` 定案 | 每个 motion 事件都可直接定案（含 lift-off） |
| 长条尾 flick | 必须晚 ≥8.33 ms；方向错**不判** | **时间下限已对齐**（`1/120`）；方向错这里仍会判中（上限 Great），比原作宽松 |
| 自动 MISS | 全局 `max(BadAfter)` = 141.7 ms | `mWindows.missAfterMs` 也是全局（§2 表里写的「逐类型 bad.lateMs」与代码不符，见下） |
| 同时手指数 | 8（`inputResults` 定长 8） | — |

> 顺带核对出的两处 **§2 / 代码不一致**（不是 flick 专属，写在这免得下次又踩）：
> 1. §2 说自动 MISS 用「该音符自己的 `bad.lateMs`」，实际 `Judgement.cpp:648` 用的是
>    全局 `mWindows.missAfterMs`。
> 2. 原作其实是**全局 max(BadAfter)**（141.7 ms），我们两种都不完全等价；
>    差异只影响「MISS 字样什么时候跳出来」，不影响最终评级。


## 5. 舞台区 / 空间判定（2026-11-06）

### 5.1 原作怎么把手指变成 lane

`LiveTouch` 的字段（`dump.cs`，`Sekai.Live`，TypeDefIndex 11816）：

```
int fingerId; int touchId; Vector2 delta;
Vector3 worldPosition;      // 0x10
TouchPhase phase;           // 0x1C
float musicTime;            // 0x20
```

`CreateLiveTouch(touchId, musicTime, ref Touch, out LiveTouch, phase)`
（`0x545763C`）里调 **`Camera.ScreenToWorldPoint(Vector3)`** —— 也就是把屏幕点
投到**一个固定的世界平面**上。`IsJudgment(ref LiveTouch, float lane)` 的 `lane`
就是这个 `worldPosition.x`，然后拿音符自己的横向区间比：

- vtable `+0x458` 取到的值 `> lane` → 不判（手指在音符右边之外）
- vtable `+0x478` 取到的值 `< lane` → 不判（手指在音符左边之外）

⇒ 判定的横向坐标**只跟屏幕 x 有关**，跟手指在屏幕上的高度无关。
`LiveTouch.delta`（0x8）正是 flick 用的那一对，`phase`（0x1C）是 Began/Moved 的判据。

### 5.2 我们原来的映射错在哪

`worldToClip()` 是**正交投影**（`platform/Renderer.cpp:1189`）：

```
clipX = 2 * worldX * scaledWidth / fillWidth      // 只跟 worldX 有关
clipY = -2 * (worldY * scaledHeight - screenTop)  // 只跟 worldY 有关
```

透视感是渲染时人为做的（`core` 的 `scaleQuad(quad, y)` 把 x、y 都乘 y，
所以 lane 坐标 x 在高度 y 上画在 `worldX = x * y`）。

原来的触摸反解是「撤销透视」：`lanePos = clipToWorldX(clipX) / worldY`。
这让 **同一个屏幕 x 在不同高度映射到不同 lane**：

| 手指高度 | worldY | 同一个屏幕 x 映射到的 lane |
|---|---|---|
| 判定线（屏幕 79%） | 1.00 | `L`（正确） |
| 屏幕中部 | ≈0.65 | `L / 0.65` ≈ **1.54 L** |
| 屏幕上部（≈300px） | ≈0.39 | `L / 0.39` ≈ **2.6 L** |

后果：**追着音符 sprite 点，永远正中**（sprite 就画在 `lane * y`，除回去刚好
还原 lane），于是「音符还在屏幕上半部分时打」和「在判定线上打」一样准；
而真实手感应该是越往上越偏（原作的固定平面）。这就是「上半部分反而更容易判定」
的来源。

**已改**：`beginPointer` / `movePointer` 都改成 `lanePos = clipToWorldX(clipX)`，
即投到 `worldY == 1`（判定线）平面，与原作一致。世界 y 只用来拒绝天空
（`worldY < 0.06`）和屏幕外（`> 1.6`），不再参与 lane。

### 5.3 还没对齐的空间项

- **判定宽度**：我们 `laneCovers()` 是 `note.width/2 + margin`，而调用点传
  `margin = 0.8` lane —— 1 lane 宽的音符实际覆盖 **2.6 lane**。原作的
  `IsJudgment` 只要求 touch 落在音符自己的横向区间里（`0x458` / `0x478` 两个
  边界，无额外容差）。0.8 是历史手感补丁，会让相邻 lane 的同一时刻音符互相抢，
  在 12 lane 的密集谱面上尤其明显。要不要降到 0.3~0.5 需要真人手感确认。
- **note 横向偏移**：`LiveBundleBuildData` 有 `judgmentOffsetX/Y`、
  `normalNoteOffsetX`、`flickNoteOffsetX`、`longNoteOffsetX`
  （SerializeField 0x18/0x1C/0x34/0x38/0x3C）。这些是演出层给 note 判定区做的
  微调，我们的谱面转换没有这些字段（一般应为 0，未验证）。
- ~~**friction / trace 的触摸是持续的**~~ → **已对齐（11-06）**，见 §5.5。
- **最多 8 指**：原作 `inputResults` 定长 8；我们没有限制（SDL 也不会给超过 10）。
- **Began → Moved 两段式**：原作独立 flick 在 Began 只记录、下一帧 Moved 才定案；
  我们每个 motion 事件都能直接定案（更宽松，不改变能否判中，只影响极早的甩）。

### 5.4 怎么回归

```bash
# 判定逻辑单测（方向 / 方向错降级 / 长条尾时间下限 / strict tap / 摩擦条覆盖窗口）
zig c++ -std=c++17 -I. .workbuddy/tools/judge_flick_test.cpp \
    game/Judgement.cpp platform/Log.cpp -o /tmp/judge_flick_test.exe && /tmp/judge_flick_test.exe

# 整条判定链（自动把所有音符按正常路径打一遍，看 [stats] 有没有 MISS 爆掉）
./build/cppsekai.exe --sus charts/0075_master.sus --test-hits \
    --screenshot /tmp/hits.png --screenshot-time 20 --no-party
```

### 5.5 摩擦条（trace / kind 3）的覆盖判定 —— 11-06 对齐

`NoteBase` 的子类里**没有** `FrictionNote`：摩擦条就是 `NormalNote` 带
`NoteCategory.Friction`，走 `NormalNote.IsJudgment`（`0x507657C`）。反汇编出来是：

```
if (State == Done)                                   return false;
if (JudgeLaneStart > lane)                           return false;   // vtable +0x458
if (JudgeLaneEnd   < lane)                           return false;   // vtable +0x478
frameType = NoteUtility.GetINoteToJudgeFrameType(this);
offset    = noteTime - touch.musicTime;                             // 正 = 手指早到
if (!NoteUtility.IsJudgmentTiming(frameType, offset)) return false; // 0x4DBDEF8
// 相位闸门（某个 int 属性 == 0 时只认 Moved；== 1 时只要不是 Ended/Canceled）
```

关键在那个 `IsJudgmentTiming`（`0x4DBDEF8`）：它从 `JudgeTimeData` 里读的是
**`BadBeforeJudgeTime` / `BadAfterJudgeTime`**（结构体偏移 0x1C / 0x20）：

```
offset > -BadAfter  &&  offset < BadBefore      // 开区间
```

⇒ 摩擦条**整个 BAD 窗口内都能判**，不需要卡在 perfect 窗口，更不需要正好落在
音符时间那一帧。而 `master/ingameJudgeFrames.json` 里摩擦四行**四档同宽**
（friction 5/5 帧全档、friction_long_end 6.5/8、friction_flick 6.5/7.5），
所以窗口内任何时刻命中都是 PERFECT —— 这就是「滑过去就算」的出处。

**我们的实现**（`game/Judgement.cpp`）：

- `coverFriction()`：一次采样 = 窗口检查 + `mHoldLanes` 里有没有手指盖住 +
  记账。`mHoldLanes` 每帧由 `main.cpp` 灌入所有手指/键盘的当前 lane，所以
  「滑过去」在采样意义上就是连续的（原作是每个输入事件问一次，两者等价）。
- `sweepFrictionCoverage()`：主循环按时间序走、遇到第一个未来音符就 `break`，
  所以**早到那一侧的手指永远见不到音符**。这个扫段专门补 `offset > 0` 的一半，
  上限取摩擦四行里最宽的 `bad.earlyMs`。
- 顺手修掉一条**过宽**：原来只要盖住就判 PERFECT，晚了 200 ms 也算；现在必须
  在 BAD 窗口内，超了照常走自动 MISS。
- autoplay 不走早到那侧 —— 否则特效会比音符早 ~83 ms 冒出来。

已知残留：覆盖容差仍是 `margin = 0.5f` lane（见 §5.3 第一条），原作没有额外容差。
