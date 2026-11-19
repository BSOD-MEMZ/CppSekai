# 暂停必须冻住主机时钟

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 暂停必须冻住"主机发布的时钟"2026-09-18（多人 + 无 BGM 谱面）

- 症状：房主按暂停，**成员画面继续走**（实测 2 秒内漂 2022ms），房主自己也还在往前推。
- 根因不在 `Party`，在时钟来源：`resolveSongClock()` 的非 follower 分支是
  `audio.hasMusic() ? audio.songTime() : wallSongTime()`。`AudioEngine::songTime()` 认 `mPaused`
  并返回冻结的 `mPauseSongTime`，**但 `wallSongTime()` 是纯 QPC 差值，完全不知道暂停这回事**。
  于是任何没有 BGM 的谱面（或还在 lead-in 阶段的）一暂停，房主的 `frameSongTime` 照涨，
  `publishHostClock()` 就把这个"还在动的瞬间"发给全房，成员跟着一起跑。
- 修法：`main.cpp` 里加了主机侧冻结 `mpHostFreezeValid` / `mpHostFreezeTime`——
  仅当 `paused && !audio.hasMusic()` 时锁住当前值，`!paused` 时解锁。
  **别删**：没有它，"无 BGM 的谱面 + 暂停"就是必然的前后场不同步。
- 回归：`mp_verify.sh` 的 round 2 断言「host paused 期间成员 chart clock 位移 < 50ms」，现在是 0ms。

