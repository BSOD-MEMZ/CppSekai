# 曲末的「LIVE CLEAR!」大字

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 曲末的「LIVE CLEAR!」大字（2026-10-25，`game/LiveClearBanner.*`）

曲末到结算之间**不是淡黑**，原作先砸一行大字。数据全部来自日服 7.1.0（取数脚本见
`RE.md` 的工具一节，产物在 `D:\Tools\rea\work\jp\`）。

**原作怎么做的**：`Assets\Resources\effects\result\prefab\fx_result_LiveClear` 是独立特效
prefab，**10 个字母各占一个 Image 节点**（不是一行文字，也不是一张整图），另有一整层
`text_ADD`（同位置、加法混合）+ `tex_back` 1148×188 背光条 + `pt`/`pt_loop`/`light` 三套粒子。
动画是 `clip_result_LiveClear`（1.5167s @60fps），只动三类属性：字母的
`m_AnchoredPosition.x`、`m_Color.a`、`m_IsActive`。**没有任何 scale 曲线**：

| 时间 | 事件 |
|---|---|
| 0.0167 | 全部激活 |
| 0.0167 → 0.1833 | 每个字母 x 从 **0** 滑到自己的槽位（L1 −446 … R +497.6、! +589.9），`text_ADD` alpha 0→1 |
| 0.05 | `pt` 喷一次 |
| 0.1833 → 0.6833 | `tex_back` alpha 0→1 |
| 0.2667 | `text_ADD` alpha 0.51 |
| 0.5167 | `pt_loop` 起 |
| 0.6833 | `text_ADD` ≈0（只剩普通层，米白色） |

所谓「炸开」就是**同时出发、同时到位、路程不同**。

**我们的实现**：素材是原作的 `tex_LiveClear.png`（1236×182，10 格单行，只在 u 方向切）
和 `tex_LiveClear_light.png`，放在 `assets/mmw/overlay/result/`，在 `Renderer.cpp` 注册成
`result_liveclear` / `result_liveclear_light`。字母表（atlas x/宽 + prefab 的 w/h/x）在
`LiveClearBanner.cpp` 顶部的 `kGlyphs`，**每个字母的宽高是 prefab 手调的，不是等比**，照抄才不歪。

**光效 / 粒子**（2026-10-25 第二轮，按 `fx_result_LiveClear` 的 ParticleSystem 实测参数）：

| 节点 | 实测 | 实现 |
|---|---|---|
| `light` | burst 1 个 @t=0，startSize **15**（×100 = 直径 1500px）、startSpeed 0、寿命 0.8s | `softBlob`：14~20 层同心圆模拟径向渐变，加法混合，半径 620 长到 750 |
| `pt` | burst 30 个 @t≈0，startSize 0.02~0.07（2~7px）、startSpeed 3（300px/s）、寿命 0.25~1s | 30 个点，纵向压扁 0.5（字是横条，圆形爆散会变光晕） |
| `pt_loop` | rateOverTime 10/s，startSize 0.03~0.08、**startSpeed 0（原地闪）**、寿命 1~2s，0.5167s 起 | 16 个静止小点，`sin` 包络闪，散布在字带内 |

尺寸换算：**粒子世界单位 ×100 = 画布像素**（fx prefab 自身 scale 是 1，但 UI 的 Canvas 是
1 单位 = 1/100 世界单位；按 startSize 0.07 推出来的，和文字高度对得上）。

**黑遮罩**（用户点名的第二处）：原作不是"字出来前白白亮着、最后一秒才黑"。`blackout()` 分两段：
0.0167→0.25 压到 **0.5 的半透明遮罩**（舞台还看得见，字亮在上面），一直保持到 1.25s，
然后从 0.5 平滑涨到 1（1.5167s 全黑）。**遮罩必须画在字下面** —— 顺序反了字会整个发灰（踩过）。

**音效**：`assets/se/LIVE_CLEAR.mp3`（用户自己录的，官方 SE 是 ACB/CriWare，没解）。挂在
`AudioEngine::UiSe` 末尾的 `UiSeLiveClear`，`loadUiSe` 的文件表最后一格；banner 启动时
`audio.playUiSe(UiSeLiveClear, 0.8f)` **直接播**，不走 `ui::se` 队列（那套是控件反馈，
同帧有控件发声会把这条顶掉）。加在枚举末尾是必须的：`ui::flushSe()` 把 `ui::SeKind` 的
下标直接 cast 进这个枚举。

流程接在 `main.cpp`：`songTime >= effectiveEnd` 时 `start()` + 播音效，然后
- **2026-11-01 起不看血量**：原版不论打成什么样，最后一个音符判完就出 LIVE CLEAR，
  失败局也一样（结算第一屏会显示 LIVE FAILED，横幅播的是 CLEAR，这是用户要的统一）；
- `resultDue` 等 **banner 自己的时钟**（`time() >= kDuration`），不是 songTime —— 音频时钟在
  曲末就停了，用 songTime 会永远等不到；
- 黑幕由 `banner.blackout()` 接管（半透明 0.5 遮罩 → 1.25s 后涨到 1）；**活着的曲子在这段
  完全不走老的 `kSongEndFadeSec` 淡黑**，否则字会出现在黑屏上；
- banner 期间不画 HUD（`drawHud` 的条件加 `!liveClearBanner.active()`），原作曲终就是只剩舞台；
- ⚠ **`songEndBlackout` 在横幅 armed 之后必须是赋值语义，不能 max 保留旧值**
  （2026-11-03 修「血量扣完横幅像没播」）：曾经有个 `else if (!clearedSoFar)` 的普通黑场
  分支，在横幅 armed **之前** 1 s 就把画面推黑，而横幅分支是 `max(blackout(), u)` ——
  那个 1 永远压着横幅，字落在全黑上，观感是"特效播了一下就没了"；cleared 局没有那个
  分支，所以只有失败局看得见。分支已删，失败局的黑场完全交给横幅自己的 u 曲线
  （切结算前照样黑透，summary 的 fromBlack 不受影响）。

**坑**：

1. **画布坐标原点在左上角**，不是屏幕中心。结算/HUD 的常量都是绝对坐标（`kPanelLeft=413`
   那种）；这套 prefab 的字母位置是相对中心的 `anchoredPosition`，要先加 `960/540` 再平移
   `−60.7`（prefab 里字母和背光的共同中心在 x=+60.7）。第一版忘了加 960，字整个跑出屏幕左边。
2. **ImGui 只有 alpha 混合**，`text_ADD` 那种加色层用叠几层白图模拟不出来（最多把米白推到纯白，
   溢不出去）。用 `ImDrawList::AddCallback` 在绘制前后切 `glBlendFunc(GL_SRC_ALPHA, GL_ONE)`，
   画完恢复 `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`。ImGui 每帧会重设混合状态，不用额外清理。
3. **遮罩要画在字下面**。遮罩和 banner 都在 foreground 层，先画黑幕再画字；顺序反了整行字
   会发灰（2026-10-25 第二轮踩到，看起来像"字变成灰色米白"）。
4. 加法层分两段 callback：`glow`/`burst` 在字**之前**（光在字后面），`text_ADD`/`pt_loop`
   在字**之后**（光在字上面）。早先全塞在字后面，光斑把字糊成一团白饼。
5. 粒子位置是**确定性哈希**（`hash01`），不是真随机 —— 否则 `--screenshot` 每次抓到的
   星点位置都不同，没法比对。要改效果改常量，别改成 `rand()`。

**验证**：`CPSEKAI_BANNER_T=<sec>` 把 banner 时钟钉住（只影响显示，不推进），配合
`--result-at 0.3 --screenshot-time 0.6` 抓任意一帧：

```bash
CPSEKAI_BANNER_T=0.19 bash .workbuddy/tools/shot.sh shots/banner.png \
  --sus build/charts/0001_easy.sus --auto --no-party --result-at 0.3 --screenshot-time 0.6
```

0.06 字母还挤在中心（白块）、0.19 刚到位（加法层拉满、纯白）、0.70 背光铺开（米白带光晕，
背景已被 0.5 遮罩压暗）、1.40 背景基本黑掉而字还亮着、1.50 全黑切结算。

