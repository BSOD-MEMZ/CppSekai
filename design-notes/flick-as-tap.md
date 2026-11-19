# 「Flick 视作 Tap」

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 「Flick 视作 Tap」（2026-09-19，`settings > 判定`，紧跟在「严格 Flick 方向」后面）

给"上滑很难触发"的触摸屏用：勾上之后**所有 flick 音符都变成 tap**——
`findCandidate()` 里 `flickAsTap` 同时跳过"种类不符就拒绝"和"方向不符就拒绝"两层，
所以任意一次按在窗口内的点按/滑动都能清掉它（方向不再有意义）。
`--flick-as-tap` 是本次运行等价的调试开关（和 `--auto` 一样**不落盘**）。

**hold 尾接 flick 例外，而且不能反过来变成 tap**：玩家那时手还按在轨道上，
要求他再点一下或者滑一下都是这个设置本来要消灭的事。所以**那个尾巴不再是音符**：
`update()` 的 hold 尾判分支在 `mFlickAsTap` 时直接 `judgeHoldTail(Perfect)`
（原来只有 `mAutoPlay` 走这条），按住不放、提前松手都算过，不扣分不断连。
- 注意**不能**让它走 tap 尾那条路（按松手时刻评 Perfect/Great/Good/Bad）——
  那还是在惩罚一个"本该不存在"的音符。
- 也不用担心它会漏判成 MISS：`findCandidate` 里 hold tail 只可能被 flick 手势碰到
  （`note.holdTail && !(wantFlick && kind == 2)`），所以尾巴只能由 hold 追踪器收尾。

实测（`0001_master`：17 个 flick，其中 9 个是 hold 尾；把 `--test-hits` 的 flick 分支
临时改成 `tap()` 模拟"只会点不会滑"）：关 = perfect 597 / miss 25 / tails 41；
  开 = perfect 615 / miss 7 / tails 51。差额正好是那 17 个 flick（尾巴 +10 走的是新分支）。

