# 演奏中的进度条拖动

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 演奏中的进度条拖动（seek，2026-11-07）

窗口最顶上那条 3px 半透明进度条（设置 → 画面「显示播放进度条」）**本身就是 seek 控件**：
鼠标悬停在顶部 16px 的命中带里它会长到 6px、冒出白色圆钮和时间（`1:24 / 3:07`）；
在带上按下开始拖动，指针离开这条带也继续跟（否则「从 20% 拖到 80%」做不到）。

一次 seek 是三层一起跳，缺一层就会出现「画面对了声音没对」或「一拖上百个 MISS」：

| 层 | 调用 | 它做的事 |
|---|---|---|
| 音频 | `AudioEngine::seekTo(chartSec)` | 文件位置 = `chart + startPos + userOffset`；时钟改成 `songTime = mMusicClipStartSec + (now - mMusicStartFrames)/rate`，seek 时 `mMusicStartFrames = now`、`mMusicClipStartSec = chart`；暂停中同样只改 `mPauseSongTime`；无 BGM 的局由 `main.cpp` 拨 `perfStart`（墙钟） |
| 判定 | `JudgementEngine::seekTo(sec)` | 落点**之前**的音符 `state = 2` 静默消费（不判 MISS、不掉血、不计分），长条/尾判/命中索引全清，`mStats` 归零、血量回初始 |
| 渲染 | `core_api::clearHitNotes()` | 核心的 `hitNoteIds` 不认时间，不清的话倒着拖回去那些音符已经"消失"了 |

- **score / combo / 判定表从 0 重新算**，`mWeightedNoteCount` **故意不重算**：跳过后半曲
  不该能打出满分（否则结算会给 S）。
- ⚠ **时钟锚点的两个坑（2026-11-07 各踩一次，症状都是"点哪儿都跳错"）**：
  1. 不能用**文件位置**的帧数当锚点——`songTime()` 是 chart 时间，chart 0 在文件里
     是 `startPos`（官曲前面约 9s 静音），拿文件帧锚会整体多走 9 秒；
  2. 也不能用 `now - chart*rate`（想说"这段在 40 秒前开始"）——帧计数是**无符号**的、
     从引擎创建起算，目标比引擎运行时长还大时直接下溢被钳到 0，时钟反而倒退。
     正解是 `mMusicClipStartSec` 这个**有符号偏移**，`resume()` / 新歌开始都要一起维护。
  验证：`CPSEKAI_SEEK_TEST=40` 之后日志里 `[seek-test] clock now 40.00s` 必须等于目标。
- **只在松手那一下真跳**（2026-11-07 改）：按下 + 松手各跳一次会听到**两声**"咔"
  （每次 seek 都是音频里的一次断点），拖动中每 90ms 跳一次更是一串断点。
  现在 press 只记录目标、拖动只更新 `scrubTargetSec`（进度条和旋钮跟着指针走），
  release 才 `seekPlayback()`；`seekPlayback` 里还有一道去重——目标与当前
  `songClock()` 差 < 50ms 就直接返回（点在自己所在位置不该出声）。
  `AudioEngine::seekTo` 跳完加 **6ms 淡入**（`ma_sound_set_fade_in_milliseconds(0→1)`），
  否则 seek 落在波形中间第一帧满幅，听感是一声爆音。
- **多人游玩直接禁用**（`party.active()` → `[seek] ignored`）：那一局的时钟归房主，
  单方面跳会让所有人的谱面散架。
- 暂停 / 倒计时 / 暂停卡 / 开屏卡期间不接（`seekBarArmed()`）。
- 无头验证：`CPSEKAI_SEEK_TEST=<秒>`，开局 1 秒后跳一次。日志应有
  `[audio] seek to … (file …)` / `[judge] seek to … (N note(s) consumed silently, cursor …)` /
  `[seek] …`，随后 `[stats]` 从 0 重新长且 **miss=0**。
- 事件层：鼠标 / 触摸两条路的 DOWN/MOTION/UP 都在 `beginPointer()` **之前**被吃掉
  （`hudSeekPress/Move/Release`），所以拖动不会被当成 lane 输入，也不会顺手点掉暂停键。

