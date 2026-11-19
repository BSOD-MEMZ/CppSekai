# 弱化打击特效

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 弱化打击特效（2026-09-26，`settings > 画面`，`EffectView`）

pjsk 原版就有这一档（演出效果的轻量版）：**只留一次判定里最基础的那一层**。默认关。给核显机器省
填充率（`Renderer` 是 `glDisable(GL_DEPTH_TEST)` + 全局 `glEnable(GL_BLEND)`，也就是纯
overdraw，填充率是弱 GPU 撞的第一道墙）。

**两层一起收**，都在 core 里做：

1. **粒子树剪枝**（第一版漏掉的那一半，也是玩家真正感觉得到的那一半）：
   `EffectPool::setup(type, count, simple)` → `createEmitterFromParticle(id, simple)`
   在简化时**不建任何 children**。一个 note 特效是一棵树 —— 根发一张基础贴图，下面挂着
   `Ripple_01` + 两组飞散粒子 + 两个方形碎片；**"一堆雪碧图拼起来"的夸张效果就是这棵树**，
   砍到只剩根，留下的才是"基础那一层"。只做下面第 2 层的话，tap 命中时那团粒子原封不动。
2. **整层跳过**（`EffectView::isSuppressed()`）：`fx_lane_*`（3 种，铺满整条轨道）、
   所有 `*_aura`（就是 `drawUnderNoteEffects()` 列的那 9 种铺在音符下层的氛围层）、
   `fx_note_long_hold_gen` 与 `fx_note_critical_long_hold_gen`（长条按住期间持续燃烧的火焰）。
   早退挂在 `addEffect` / `addAuraEffect` / `addLaneEffect` 三个入口，**不是** `addNoteEffects`：
   这样 autoplay 的 timeline 路径和玩家命中的 `triggerNoteEffect()` 路径一起覆盖。

不在渲染层过滤：packed quad（25 float）里没有 EffectType 信息，到了 `Renderer` 已经分不出层次。

### ⚠️ 开关不能只存在 `EffectView` 里（2026-09-26 踩到的真 bug）

`initializeEffects()` 里有一句 `gRuntime.effectView = {}`，而它**每次加载谱面都会跑**
（`finishLoadedScore()`）。第一版把开关放在 `EffectView::simpleEffect` 成员上 → **选曲界面
勾得好好的，一进歌就被清成 false，玩家看到的是全量特效**（"我勾了但完全不觉得被简化"就是这个）。
真值现在放在 `PreviewRuntimeConfig::simpleEffect`，`initializeEffects()` 在 `= {}` 之后立刻
`setSimpleEffect(config.simpleEffect)` 补回去。**以后往 `EffectView` 加任何"设置类"状态，
先确认它会不会被这一句冲掉。**

配套：`EffectView::setSimpleEffect()` 在值真变化且 `initialized` 时自己 `init()` 重建粒子树
（树是按开关建好的），core 侧 `setSimpleEffect(int)` 再跟一次 `initializeEffects() +
rebuildEffectScore()`，所以运行中也能切。`main.cpp` 在启动和切 profile 时各推一次，并打一行
`[core] effects ready (simple=0|1)` —— **排查"开关没生效"先看这一行。**

### 实测

`11004_master`，t=30s，autoplay，两次跑的判定统计完全一致（perfect=278 / score=224034）：
- 全开：判定线上密布菱形粒子、中央一团放射状火焰光球；
- 弱化：粒子整片消失，只剩长条 / 判定线那层基础光 —— **只动画面，不动判定**。

### 这类 A/B 截图怎么拍

python 显式写 `profiles/default.json` 的 `settings.<键>` → **读回来打印确认** →
`./cppsekai.exe --sus <谱面> --no-party --screenshot D:/tmp/x.png --screenshot-time 30`
（Git Bash 里必须 `( exe & wait )`）→ **在日志里断言实际值**（如 `[core] effects ready (simple=N)`）
→ 最后把改回去。坑：

- **别信备份文件**：第一次对照就是"文件里出现了 `simpleEffects` 这个键"就当成值对了，结果两次跑的
  其实是同一个状态，对照整个作废。**断言日志才是硬证据**，`cp` 之后也要读回值确认。
- `--test-hits` 只在 `!autoPlay` 时生效；本机 `profiles/default.json` 的 `autoplay` 是 true，
  要测"玩家命中"那条路得先把 `settings.autoplay` 改成 false。
- 实测 `--screenshot` 跑完 `profiles/<id>.json` 会被写回（键会新增 / 更新），测试值要手动恢复。
  「平台 / 输入相关的坑」里那条"`--screenshot` 不写 `userdata.json`"说的是旧的单文件模式。

