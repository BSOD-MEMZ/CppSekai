# 触摸特效（TapEffect）

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 触摸特效（TapEffect）2026-10-25 —— 三种状态，参数全实测

`game/TapEffect.*`。**pjsk 的"点一下 / 按住 / 按住拖动"是三套不同的 fx**，
`ScreenLayerTouchEffect` prefab 里分别叫 `fx_Tap_single` / `Follow`(`fx_Tap_loop`) /
`fx_Tap_flick`，参数差得很远。别再把其中一套的数拿来当另一套（踩过两次）。

### 先分级，再读参数

prefab 里有 75 个粒子系统，分属 `fx_Tap_single` / `Follow` / `fx_Tap_flick` /
`TouchEffect (1)~(4)`。**分组方法**：prefab View 页面的 `GameObjects[]` 是
**深度优先**顺序，按各 fx 节点的下标切区间即可（`D:\Tools
ea\work\jp\jp_fx_group.py`）。
（`grouped.json` 就是结果，改之前先看它。）

| | 单击 `fx_Tap_single` | 按住 `Follow` | 拖动 `fx_Tap_flick` |
|---|---|---|---|
| 环 `PT_01` | **160**，life 0.5，单发 | **330**，life 0.45，循环 | 无 |
| 柔光 `PT_02` | — | **650**，4/s | 无 |
| `particle3` | **15.8~235.5**，speed 35~100 | 42~147，speed 0~0.45，**6/s** | 0.03~0.05（不可见） |
| `particle2` | 18.2~20.5 | 10.5~21，6/s | 不可见 |
| `particle2_1` | 3.0~14.1 | — | — |
| `particle1` | 1.6~7.9 | 2.1~10.5 | 不可见 |
| 粒子 scale | **157** | **210** | 1 / 0.1 / 0.1 |
| scalingMode | 0 Hierarchy | 0 Hierarchy | 1 Local |

`fx_Tap_flick` 的三个粒子节点 scale 是 1 / 0.1 / 0.1，算出来只有 0.005~0.05px，
**在正式包里等于不可见**（多半是半成品）。所以代码只做两个状态：
**Tap（一次性 burst）** 和 **Hold（跟随指针的持续效果）**，拖动 = Hold 换位置。

**Hold 没有环**：`Follow` 里的 `PT_01`（330）`rateOverTime = 0`，只能靠代码 `Play()`
触发，而游戏里没播它。实际按住时只有 **650px 白色光晕 + 环绕的三角形**。
（实测确认过：加上环反而不对。）

### 数据换算

- 世界尺寸 = `startSize × RectTransform.localScale`（scalingMode Hierarchy）。
- **`startSize` 的两种模式别混**：
  - `minMaxState: 2`（Random Between Two Curves）——`scalar` 是**两条曲线共用的乘数**
    （Unity 的 `m_CurveMultiplier`），`minScalar` 在这里**不参与**；
  - `minMaxState: 3`（Two Constants）——`scalar` 是上限、`minScalar` 是下限。
  （2026-10-25 第二次核对才发现：早先把 Two Curves 也乘了 `minScalar`，把 particle3
  的下限算成 2.5px、跨度算成 90 倍；真值是 **15.8~235.5，跨度 15 倍**。）
- **绝对像素不可求证**：四个三角节点都挂在 `particle1` 下，而 `particle1` 自己也是
  scale 157，缩放会连乘；所以能信的只有**互相之间的比例**（三角 : 环），别拿 px 当真理。
- **`rateOverTime` + `lengthInSec` + `looping` 决定"一次发几个"**：single 组三个
  系统都是 1/s + 0.5s + 不循环 → **一次点击每个系统最多一个粒子，都在 t=0**。
- **`startSpeed` 沿圆盘法线（+Z，屏幕外）**，正交 2D 看不到位移；代码里
  `kRadialGain` 把它折算成径向漂移（0 = 严格照原样）。
- **三角形是 3D 的**：`startRotation` 是 Vector3（三轴各自 ±2π），
  `RotationOverLifetime` 的 `separateAxes = 0` 所以**只有 Z 在转**（π rad/s），
  X/Y 是每颗粒子固定的倾角。渲染要做真 3D 投影（绕 X→Y→Z 后正交投影），
  倾斜的 quad 会被压扁成瘦长条 —— 这才是"翻滚的玻璃片"，纯 2D 旋转做不出来。
  **但 X/Y 倾角别照抄 ±2π**：真 mesh 有厚度和明暗，侧对时仍有体积感；平 quad 侧对
  就是一条线，表现成"三角忽大忽小"。代码里用 `kShardTilt = 1.15`（≈±66°）夹住。

### 代码里的可调旋钮

- **`kRingScale = 0.45` / `kShardScale = kRingScale`**（`TapEffect.cpp` 顶部）：非实测的
  全局缩放，1.0 = 字面值。三角的绝对 world size 本来就不可求证（四个三角节点都挂在
  `particle1` 下，而 `particle1` 自己也 scale 157，缩放会连乘），能信的只有**互相之间的比例**。
- **`TriSpec` 的 `sizeMin/sizeMax`**：每颗三角的直径（乘 `kShardScale` 得画布 px）。
  **2026-10-29 收敛了两轮**（用户："大的太大、小的小到看不见"）：

  | system | 实测 | 现在 | 画布 px |
  |---|---|---|---|
  | tap hero（particle3） | 15.8~235.5 | 80~130，bias 1.15 | 36~58 |
  | tap particle2 | 18.2~20.5 | 30~38 | 13~17 |
  | tap particle2_1 | 3.0~14.1 | 24~34 | 11~15 |
  | tap particle1（×2 颗） | 1.6~7.9 | 20~30 | 9~14 |
  | hold hero | 42~147 | 85~120，bias 1.10 | 38~54 |
  | hold particle2 | 10.5~21 | 30~40 | 14~18 |
  | hold particle1 | 2.1~10.5 | 24~34 | 11~15 |

  **次级那几档是被抬上来的**：按字面值乘 0.45 之后只有 0.7~3.6 px，屏幕上就是看不见的
  灰点 —— 用户报的"小到看不到"其实是它们，不是 hero。抬到 ~10 px 下限之后，一次点击读起来
  是"一圈 + 几片大小相近的玻璃"，系统之间的先后顺序仍然是实测那个。
- **`TriSpec::sizeBias`**：把单位的随机取幂再映射到 band。1.0 = uniform（Unity 原样）；
  >1 把分布压向 `sizeMin`。**别删**：hero band 一旦放回实测跨度，uniform 的平均值会翻到
  ~106 px，每一下都糊一大块。
- **`kShardTilt = 1.15`**：三角的 X/Y 倾角上限（弧度）。0 = 纯平面，3.14159 = 实测的
  ±2π。调大全飘成细线，调小失去 3D 感。侧对时看着像一条细线是**正常的**（真 mesh 有厚度，
  平 quad 没有），别为了这个把 tilt 拧小。
- `kRadialGain`：0 = 粒子不动（字面读法），1.0 = 按 startSpeed 向外飘。

### 性能（2026-10-25 实测，别凭感觉优化）

`CPSEKAI_FRAME_STATS=1` 会每 120 帧打一行 `[framestats] avg/peak`（默认静默）。
在 `--fps 2000`（关 vsync + 关 60 上限）下：

| | avg | peak |
|---|---|---|
| 无特效 | 0.51 ms | 0.75 ms |
| 3 个 burst 常驻（3 环 + 15 三角） | 0.54 ms | 1.10 ms |

→ **整个特效约 0.03 ms**，占 60fps 预算（16.7 ms）的 0.2%。所谓"帧率不高"来自美术：
主环是 **16 帧 / 0.5s ≈ 32fps 的序列帧**（Unity 的 `fps: 30` 也是这样），60Hz 下每格
停约 1.9 帧，看着就是一顿一顿的。想要更顺只能给相邻两格做交叉淡入（会略糊），
不是优化代码能解决的。

### 其它踩过的坑

- **ColorOverLifetime 的 Gradient**：只有前 `m_NumColorKeys` 个 key 的 RGB 是颜色
  （ctime 轴），前 `m_NumAlphaKeys` 个的 A 是 alpha（走**独立的 atime 轴**），
  其余 key 的 rgb 是 `(0,0,0)` 占位。当成颜色关键帧会得出"三角中途变纯黑"的假结论。
  颜色还是**线性空间**，要过 `srgbOf()`。
- **`tex_sekai_01_2` 每格三角形外面有 alpha=255 的底色**，官方 mesh 是三角形所以
  从不采样；用正方形 quad 会画成方块 → `strip_tri_background.py` 抹掉。
- **混合**：环和柔光 additive（`AddCallback` 切 `glBlendFunc(SRC_ALPHA, ONE)`，
  **画完必须恢复** `ONE_MINUS_SRC_ALPHA`）；三角 alpha blend（白描边 + 渐变填充 =
  贴纸不是光）。三角的混合模式是从美术特征推断的，shader `sekaieffect` 没源码。
- 随机数用 **xorshift32**：LCG 前几个值挤在 0 附近，每次测试都抽到最小尺寸。
- 无头验证：`--tap-fx-test [秒]` 三个冻结的 burst，`--tap-fx-hold [秒]` 冻结的按住
  状态（走 `debugHoldAt`，确定性重放发射）。**采样时刻别取太晚**：粒子 life 只有
  0.1~0.7s，age 超过 0.3 时大半已经死了，看起来像"没画"。
- 截图用 `.workbuddy/tools/shot.sh`：`--screenshot` 偶尔抓到没画完的帧（纯白、~35KB），
  脚本会检查大小并重试。
- 编译报 `lld-link: failed to write output` = 有残留 cppsekai 进程占着 exe。

