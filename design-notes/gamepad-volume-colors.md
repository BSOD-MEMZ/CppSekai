# 手柄 / 音量 / 结算配色

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 手柄 / 音量 / 结算配色（2026-09-15）

- **XBOX 手柄**（`main.cpp` 主循环前的 `openPad` + 帧首的映射块）：只做菜单，
  **不参与打歌**（12 轨的东西手柄打不了）。实现方式是**把手柄按键翻译成键盘按键**，
  用 `SDL_PushEvent` 塞回 SDL 队列（`SDL_INIT_GAMECONTROLLER` 已加进 `SDL_Init`）——
  于是选曲/设置/弹窗**原本就有的方向键 / Enter / Escape 逻辑**直接生效，不用给每个
  画面再写一条输入路径。要点：
  - 一次按压 = **一个脉冲**：本帧压 KEYDOWN，下一帧补 KEYUP（`padReleaseQueue`）。
    ImGui 对同帧 down+up 的识别不可靠，拆两帧最稳；方向键按住时按 400/110ms 自动重复。
  - 映射：方向/左摇杆 = 方向键（左摇杆死区 12000），A = Enter（确定 / 开始 / 结算继续），
    START = 选曲里开设置卡（H）、演奏中开暂停弹窗、结算页继续，Y = F5 重扫，
    B / BACK = 关弹窗（B 只在有卡/弹窗时发 Escape —— 选曲界面按 Escape 会退出游戏）。
  - 热插拔：`SDL_CONTROLLERDEVICEADDED/REMOVED` 里开关（注意 ADDED 给的是 device index、
    REMOVED 给的是 instance id，两者不能混用）。
  - 已验证：合成按键这条路是通的（`SDL_PushEvent(KEYDOWN)` 能把设置卡打开，截图确认）。
    真手柄的按键映射还需要人手试一遍。
- **音量**（设置 → 演奏页签）：`BGM 音量` = `UserSettings::bgmVolume`（存 userdata.json），
  走 `AudioEngine::setBgmVolume`，它是引擎侧的主音量，**同时**缩放谱面音轨 / 选曲试听
  （0.85 基准）/ 结算 BGM（0.85 基准），所以一定要在第一次 `loadMusic` **之前**设一次；
  `音效音量` = 老 `seVolume`，判定音本来就按它播，UI 音是 `ui::bindSe(&audio, 0.8f * seVolume)`
  —— 改滑杆时要**重新 bind**，否则 UI 音量不跟。
- **结算画面难度配色**：`difficultyColor()`（`SongSelect.hpp` 公开，选曲徽章和结算共用）
  现在同时给**难度胶囊**和**曲绘边框**上色。原来是写死的 `kPink`，因为参考截图是 EXPERT 的，
  于是打 EASY 也是红的。
- **确定键光效**（`main.cpp` 的 confirmFlash 块，2026-09-16 重做）：原来是一圈**平的 50% 白三角**
  （硬边、没有衰减）+ 两层满 alpha 的实心圆，而且**铺不满屏**——圆半径按"按钮到最远角"给，
  16 层同心圆每层恒定 0.18 alpha 叠起来，最远那个角只叠到 ~29%，加载那 1 秒能看见屏幕。
  现在：
  - **包络**加了一段 attack（`kConfirmAttack = 0.09s`，smoothstep 进），旧版第 0 帧就是全亮，
    那是"硬"的一半；
  - 半径改成两个值：`rOuter = want * (0.85 + 0.90*cover)`、`rInner = 0.62 * rOuter`。
    `want` = 按钮到最远角的距离；**关键是 `rInner`（满亮度核心）必须越过 `want`**，
    cover=1 时 rInner = 1.085 want，于是 `startSession` 那一下整屏都是峰值亮度；
  - 40 层圆的 alpha 是**解出来的**，不是常数：第 i 层的目标合成率 = smoothstep(径向位置)，
    而叠加是 `1-prod(1-a_i)`，所以 `a_i = (target - comp) / (1 - comp)`。
    这样出来的是真径向渐变（`target-alpha` 只出现在最外圈，等于 0），外缘没有硬边。
  - 峰值仍是 `kConfirmPeak = 0.88`（纯白帧看着像切一刀而不是光）。
  - 实测（1280x720，按钮中心 1083,521）：t=0.10 时最远角 156 / 近角 241（有渐变、软）；
    t=0.32（正白的时刻）四角 233~241、中心 238 —— **整屏铺满**。
  - 调试：`--confirm-flash [<sec>]` 单独放一次这个特效（不加载歌曲），配 `--screenshot` 看帧。
    无头断言用像素探针量四角 / 中心亮度（见下「验证」）。
- **`ui::combo` 的弹窗动画曾经把第一行吃掉**（2026-09-16 修，症状：分组下拉里「关闭」没了）。
  两个坑叠在一起，改 `Ui.cpp` 的 combo 前必读：
  1. **`ImGui::IsPopupOpen("##combo")` 永远是 false**。它比的是弹窗**窗口自身**的 ID，
     而 combo 的弹窗窗口名是 `"##Combo_%02d"`（按嵌套深度回收），跟 combo 的 ID 无关。
     所以 `animValue` 的 t 恒为 0 → 弹窗被永久上移 14px、箭头也从来没转过。
     现在用一个 `static std::unordered_map<ImGuiID,bool> comboOpen` 记住**上一帧**的
     `BeginCombo` 返回值来驱动动画（晚一帧正好是缓动要的"开门那帧 t≈0"）。
  2. **绝对不要 `SetWindowPos()` 挪弹窗**。ImGui 在 `Begin()` 里就把弹窗的裁剪矩形
     (`ClipRect` / `InnerRect`) 按当时的位置算好了，帧中途挪窗口只改 `Pos` 和内容游标
     （`SetWindowPos` 会把 `CursorStartPos` 一起偏移），**内容就跑到裁剪矩形外面被裁掉**——
     上移 14px 正好裁掉第一行。要动只能动内容，而内容一样会出裁剪框。
     所以入场动画改成**淡入弹窗内容**（`ImGuiStyleVar_Alpha` 只 push 在 popup 内部，
     不能包住 `BeginCombo`，否则关着的 combo 也一起变透明）。
  3. 另外 `ImGuiComboFlags_HeightSmall` 把弹窗高度卡在 4 行，而它算行高时**没算调用方的
     `FramePadding`**（`Selectable` 的 bb 高是 `label_size.y + FramePadding.y*2`），
     选曲界面那套 padding 下 4 行会多出 ~4px，最后一行被削掉。
     `ui::combo` 内部把它换成 `HeightRegular`（弹窗是 `AlwaysAutoResize`，仍然贴着内容）。
  无头验证：跑起来后用 `winsend.exe` 点下拉框（1920x1080 时分组框中心 ≈ `882,50`），
  截图后数弹窗区域里有几行文字（原来是 3 行，现在 4 行）。

