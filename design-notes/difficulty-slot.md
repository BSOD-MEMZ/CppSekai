# 难度槽位解析失败

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 难度槽位解析失败会让歌**整个不可选** 2026-09-18（"多人有时候连不上"的真正根因）

- 症状：房间组起来了（seat 0 host / seat 1 member 都在），谱面也 `song locked` 了，
  **但房主从此一行日志都不再打**，这一局永远开不起来。看着像 IPC 挂了。
- 根因在选曲界面，不在 `Party`：`drawSongSelect()` 每帧用
  `selected = groups[groupIndex].idx[diffIndex]` 反推选中项，`diffIndex` 默认 3（EXPERT）。
  `buildGroups()` 只把谱面放进**它自己的难度槽**（`diffIndexOf(item.difficulty)`）。
  于是 `#DIFFICULTY` 缺失 / 写成 `0` / 是表里不认识的字符串时（**unipjsk 的谱面一律如此**），
  `idx[全部] == -1` → `selected = -1` 每帧被写回，**把调用方开局的合法 `selected = 0` 冲掉**。
  列表看着有一首歌在屏幕上，实际"什么都没选中"：单机按确定没反应，
  房间里 `publishHostSong()` 因为 `selected < 0` 直接 return，房主静默，全房干等。
- 修法两处（`game/SongSelect.cpp`）：
  1. `buildGroups()` 里 `d < 0` 时**把这个 entry 塞进最低的空槽**——行变得可选，
     而 `difficultyIndex()` 仍然如实报 -1，不假装知道它是哪档。
  2. `selected` 赋值后补一层兜底：扫一遍该 group 的槽位，取第一个非负的。
- **踩坑教训（重要）**：这次差点被自己的诊断工具带偏。我给帧循环加的 `[alive]` 心跳显示
  `uiClock` 一路涨到 29s、帧率 33.3ms 稳定，于是先入为主认定"帧循环没停，是别的地方卡住"，
  转头去查 `Party::init()` 的 CAS 竞态。真正的线索是那行被我**加了 8 帧上限**的条件探针
  ——它打印的 `sel=-1 entries=1` 才是答案。**探针的上限别设太小，它会掩盖"条件一直不满足"
  和"条件满足了但分支没走"的区别。**
- 顺带修掉的两个真竞态（`platform/Party.cpp`，都不是本次根因但确实存在）：
  - `init()` 原来是先 `InterlockedCompareExchange(&seats[i].alive, 1, 0)` **发布 alive**、
    之后才写 `heartbeat` 等字段。窗口期内别的进程会看到 `alive==1` + 上一个宿主的陈旧心跳
    （可能已超 `kStaleMs`）立刻回收它。现在改成**先写全部字段、`release` 栅栏之后再用 CAS 发布 alive**。
  - `update()` 里"发现自己被回收就无条件抢回 seat"改为 CAS：若已被第三个进程合法拿走，
    就**放弃这个 seat 重新 `init()` 入房**，而不是两个进程都自认拥有同一个座位、逐帧互相覆盖。

