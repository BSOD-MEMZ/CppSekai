# 谱面坐标与时间轴

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 谱面坐标与时间轴（README 原来的「原理详解」搬过来的）

完整的三层架构与逐模块说明见本文件开头；这里只留几张"改坐标/改时钟之前必须知道"的小表。

### 时间：tick 是唯一真理，秒是算出来的

谱面时间是 **tick**（`TICKS_PER_BEAT = 480`），不是秒 —— BPM 与 `#SPEED` 都会变，秒和 tick
从来不是线性关系。核心维护三条互相换算的轴（都在 `core/native/src/mmw_preview.cpp`）：

| 函数 | 输入 → 输出 | 用途 |
|---|---|---|
| `accumulateTicks` | 秒 → tick | 把播放器当前时间拉回谱面坐标系 |
| `accumulateDuration` | tick → 秒 | 判定、音效、事件的真实时间 |
| `accumulateScaledDuration` | tick → 视觉秒 | **受 hiSpeed 影响**，决定音符画在哪 |

关键点：**判定用真实秒、下落位置用视觉秒**。所以改音符速度只让音符跑得更快，不会让歌对不上
（`noteSpeed` 走的是 `setPreviewConfig` 的 hiSpeed 那条路）。

### 空间：没有 3D，只有一个假透视

轨道坐标 `x` 在高度 `y` 上被画到世界坐标 `(x·y, y)`：

```cpp
// core/native/src/mmw_preview.cpp
QuadPoints perspectiveQuadvPos(float left, float right, float top, float bottom)
{
    return {{ {right*top, top}, {right*bottom, bottom}, {left*bottom, bottom}, {left*top, top} }};
}
```

- `y = 1` 是判定线（最宽、最靠下），`y → 0` 是消失点（最窄、最靠上）
- 12 轨的轨道坐标是 `lane - 6 + width/2`，即左半 −6…0，右半 0…6
- 好处是判定只要一次除法就能把屏幕点还原成轨道坐标，不用射线求交：

```
窗口像素 → 裁剪空间 (clipX, clipY) → worldX / worldY
         → lanePos = worldX / worldY      // 撤销假透视
```

`Renderer::clipToWorldX/Y` 就是这条逆变换（外加 letterbox 的宽高比修正），
输入命中与悬停高亮全靠它 —— 改渲染模式 / 缩放 / 窗口比例之后"点不中音符"先查这里。

### 音频开头的静音填充：具体参数

`platform/Audio.cpp` 里那套自动检测（`sidecar` 的 `fillerSec` / `offset` 优先）：
以 44100Hz **单声道**解码开头，`kBlockFrames = 1024` 一块找第一个峰值 > `kThreshold = 184`
（≈ −45 dBFS）的采样；结果 < **0.3 秒**就当编码间隙，返回 0（不算填充）。

---

