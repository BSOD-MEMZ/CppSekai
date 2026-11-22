# 确定闪光 / 猜歌

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 确定闪光 / 猜歌（2026-09-20）

### 确定闪光必须"全白"，并且加载只发生在全白之后

它是加载卡顿的遮羞布：`startSession()` 要花一秒左右，那一秒里屏幕上是什么，玩家就看着什么。

- **峰值改成 1.0**（原来是 0.88）。0.88 加上远处仍在渐变里，卡住的那一帧就是"88% 白"，
  能透出选曲列表 —— 用户报的"有点透"就是这个。射线（rays）是**故意**在 cover→1 时退场的
  （`rayFade = 1 - cover*1.3`），所以纯白只剩那颗盘，不再是"硬"的那种白。
- **加载改由 `confirmWhiteShown` 把关**：以前是 `confirmFlashTime >= kConfirmExpand` 就加载，
  而 `cover` 恰好在这一刻才到 1，也就是说**卡住的那一帧画的是它上一帧**（还没铺满）。
  现在：本帧画满（`cover >= 1 && alpha >= 0.999`）→ 置 `confirmWhiteShown` → **下一帧**才 `startSession()`。
  时间线加了不到 16 ms，但保证卡顿发生时屏幕上已经是纯白。
- **"白色消失 ⇒ 已经在 intro 里"** 靠三件事共同成立：① 白色只在 `startSession()` 成功后才可能退场
  （`!confirmStartPending` 才允许关闭）；② 同一分支里 `state = Play` + `beginSessionClock()`，
  歌内时间从 0 开始；③ intro 卡片在 `outputTime` 0~3.4s 是**不透明**的（`introCardAlpha` = 1）
  且遮罩 `INTRO_BG_ALPHA` 也开着。所以白色是在 intro 卡片之上淡出的。
  失败路径（`startSession` 返回 false）没有 intro 可露，改成**跳到淡出阶段**而不是硬切回列表，
  否则看着像故障；错误本身还是只打 stderr。
- 验证手法：`--confirm-flash <sec>` 起爆 + `--screenshot-time <sec+0.35>` 抓**平台期**那一帧，
  然后逐像素断言**整屏纯白（含四角）**。2026-09-20 实测 1280x720 全采样点 `min(r,g,b) = 255`。
  别拿攻击段（前 0.09s）或淡出段去抓，那两段本来就不是全白。

### 2026-11-22 最终确认页复用同一颗爆闪

最终确认页的 `SoloLiveFinalConfirmButton` 不再"先回列表再爆闪"：点开始/Enter 时
`drawFinalConfirm` 直接用 main 传进来的 `DecideBurst*` 在按钮位置 `spawn()`，然后
在最终页停 `kDecideLead`（0.4167 s）让爆闪播完，才把 entry 返回给 main。main 的
action 分支用 `game::finalConfirmBurstQueued()` 判断"最终页已经放过了"，跳过第二次
`decideBurst.spawn()` 和 `-kDecideLead`，只把白屏从 0 接上；`se(SeStart)` 也只在
最终页放一次。列表页的 `确定` / 回车路径不变（没有 queued 标记，照旧 spawn）。

> **别和"页面转场"搞混**：选曲列表 ↔ 最终确认页之间**没有**爆闪，那是
> `coverFader` 的淡出→换页→淡入（白盖色 0.15 s + 0.21 s），2026-11-22 之前我们
> 错把它做成了"翻页那一帧白闪 210/255"，看着像闪屏。细节见
> [`FINAL-CONFIRM.md`](../FINAL-CONFIRM.md) §5。爆闪只出现在**开始演出**那一步。

### 猜歌（选曲界面头排「猜歌」按钮 / `--guess`）

用社区别名表当题库：`assets/select/guess.png` 图标，按钮就排在「音乐商店」右边
（`guessX = storeX + storeW + 10k`；**多人横幅的右边界也跟着改成 `guessX + guessW`**，
不然成员窗口的横幅盖不住这个新按钮）。点开是 `ui::beginCard` 卡片，
和 个人资料 / messageDialog 同一套外壳：标题 + 细线 + 胶囊按钮。

- 状态与出题都在 `game/SongSelect.cpp` 第一个匿名 namespace（`GuessState gGuess`），
  因为别名表（`gAliasIndex`）和曲名表（`gMusicTitles` / `titleFor()`）都住在那里。
- **题库只收"唯一且有意义"的别名**：`gAliasIndex[alias].size() == 1`（多个答案不算题）、
  长度 ≥ 2 字节、不是纯数字、不等于这首的曲名或读音、**且没有别的歌的曲名正好是这个别名**
  （否则那个错项也是对的）。2026-09-20 实测 12896 条里 12660 条可用。题库建一次就缓存
  （后面那条检查是 O(别名×曲名)，表在运行时不变）。
- **别名还得画得出来**（2026-09-25）：别名就是题目，字体缺字会变成 `?`，那就成了猜方块。
  判据是 `game::fontCoversText()`（`game/Intro.*`）——逐码点问 `ImFont::IsGlyphInFont()`，
  也就是**字体文件的 cmap**，不是 ImGui 的 GlyphRanges（1.92 里那是 legacy，字形按需从
  字体文件加载，所以真正的闸门是 face 有没有这个字）。表里韩文 / 泰文 / 制表符 / 花式符号
  的昵称（"텔유월"、"헬로"、`(╯°口°)╯`）全被剔掉，实测 12660 → **11876** 可用。
  `fontCoversText()` 在没加载字体时返回 true（**fail open**），免得早期调用把题库清空。
- ⚠ 顺带推翻一条老认识：**`GlyphRanges` 不是"能画什么"的闸门**（1.92 标它 LEGACY，
  真正决定的是 face 的 cmap）。所以 `☆`、`碲` 这类"范围外、但字体里有"的字游戏里画得出来
  ——实测出题抽到过"碲氧"。
- 干扰项是随机别的曲名，只要求四项**互不相同**且不同于正确答案；`std::shuffle` 打乱后才记
  `answer` 下标。答完锁定（胶囊 `clickable = false`），正确项染薄荷、选错的那项染红，
  下面是"答对 x / y 连对 z（最佳 w）"+ 下一题 / 关闭。
- 出题时打一行 `[guess] "别名" -> <id> 正确曲名 | *正确曲名 / 干扰 / ...`，
  无头跑只能靠它看到出了什么题（`--guess` 起手就能抓）。
- **题库是分帧建的，卡片先出来（2026-11-02）**。以前点击那一帧同步跑完整个池子，
  12.9k 个别名里每个都要和**全部曲名**比一遍（`toLower(titleFor(id))` 双层循环
  = 6.4M 次临时 string），点下去就是几百毫秒的卡死。现在三段：
  - `openGuessDialog()` 只把 `gGuess.open = true` + `gGuess.loading = true` 立起来，
    卡片立刻画出来（标题 + 「正在准备题库…」+ 进度条 + 关闭）；
  - `tickGuessPool()` 每帧推 ~3ms（每 64 条看一眼表），进度写 `gGuess.loadingPercent`；
    卡片关了也继续推，下次点就直接能玩。收尾会打
    `[guess] pool built in X ms over N frame(s)`（**注意这个数是墙钟**，含启动那一帧的调查
    200ms，真实 CPU 也就 20~30ms）；
  - 收尾时自己 `newGuessQuestion()` 出第一题。
  - 那条"别人的曲名不能正好是这个别名"改成查 `gTitleLowerIndex`（小写曲名 → 曲 id 们），
    6.4M 次 string 比较变成一次哈希。**结果必须仍然是 11876 可用**（前 12896 条），
    改这块之后拿这个数对一下。
  - 凑不出题时**不关卡片**：`gGuess.unavailable` 立起来，卡片里写"没有可用的题库"。
    以前是 `open = false`，看起来就像按钮没反应。
- **入场/出场动画要 `alive` 守着**（2026-11-02）：`if (gGuess.open && options.size() == 4)`
  这种写法会同时踩两个坑 —— 关的时候那一帧就不画了，出场动画没有；而 `beginCard` 只有在
  **关闭动画真的播完**（`st.t` 走到 0 → `st.ended = true`）之后才会在下次 open 时重置 `t`，
  所以"第一次点有入场、之后都没有"。现在和 `keymapCardAlive` / `eulaAlive` 一个套路：
  `open` 只当目标态，`gGuessAlive` 在 `beginCard()` 返回 false 时才落下来。
  **新加卡片照抄这个模式**，别用 `if (open) { beginCard(...); }`。
- `--guess` 是给 `--screenshot` 用的钩子，同 `--profile`；正式入口就是头排那个按钮。
- 实现细节：选项胶囊不能用 `ui::capsuleButton`（它只认薄荷/白两色），所以
  `guessOptionPill()` 照它的外观重画一遍（半高圆角 + 底部浅阴影 + `bodyFont` 居中），
  但填充色由调用方给。

