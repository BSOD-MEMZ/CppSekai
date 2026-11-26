# 多人游玩

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 多人游玩（`platform/Party.*` + `game/PartyScreen.*`）

同一台机器开多个窗口一起打。房间**没有自己的页面**：它就在选曲界面上。

**房里只有一个人时整套流程按单人走**（2026-11-25）：选曲列表可正常操作，`确定` →
最终确认页 → `开始` 立刻开局，节奏和单人演出完全一致。房间本身照旧活着（下一个窗口
开起来就加入）。2026-11-25 还删掉了左下角的房间胶囊（`drawPartyBadge`）和手机面板
的薄荷色状态行——多人 UI 只在真正有两个以上玩家时才出现，等待信息挪到最终确认页。

流程（2026-09-18 定稿，之前的"房间页 + 准备 + 10s 倒计时"已删；2026-10-17 收尾）：
**第一个窗口是房主**，选曲列表归它（它的光标停在哪个曲目，就立刻广播哪个曲目）；
其他窗口的列表是**只读**的（灰罩 + 「只读·由房主选曲」+ 随机按钮变灰），
**但列表之外的工具一律不禁用**——搜索 / 排序 / 分组 / 刷新 / 音乐商店 / 猜歌 / 设置都是
每个窗口自己的事（2026-10-17 去掉了顶栏的 `BeginDisabled` 和那条盖住顶栏的
「多人游玩 · 列表只读」横幅）。搜索框在成员窗口也真的会过滤镜像列表，
只是**房主那首永远留在 `visible` 里**（`lockedGroup` union 回去，并进列表缓存签名），
否则列表和手机面板会各说各话。
它们的**手机面板显示房主的曲目**，难度就在那里点；**所有人的「确定」都只负责进入
最终确认页**（和单人一样：卡组 / 自动演出 / MV / 换歌手），**只有在这一页按「开始」
才等于"我准备好了"**。房主等所有在局座位都按过开始，才 `beginLoading()`；所以是
「所有人都点最终页的开始」才开打，不是「所有人点选曲页的确定」。

- **必须全员在最终页按开始，没有强制开始**（2026-10-17 删「房主再按确定强开」，
  2026-11-25 把触发点从「确定」挪到最终页的「开始」）：房主再按一次开始**什么都不额外发生**。
  有人不按就谁也别想开——这是刻意的，不是漏洞。
- **谁按了什么**（`mpConfirmed` / `mpPublished`，都在 main.cpp）：
  - 房主：光标换曲 → `publishHostSong()`（`lockSong`，epoch++，成员据此清空自己的难度/确定）；
    最终页按开始 → `mpConfirmed` + `party.setReady(true)` + 记住 `mpConfirmedEpoch`。
  - 成员：手机里点难度 → `party.setDifficulty`；确定只翻到最终页；最终页按开始 → ready。
    换曲/回到大厅时自己的答案作废。
  - 最终页按返回 / ESC → `SelectPartyResult::cancel`：座位退回 `PartySeatLobby`，
    房主不再等它（否则一个已经走掉的人会把整房卡死）。
  - `allReady()` 只看**非 Lobby** 座位（`inRoom >= 1`）：Lobby 座位 = 没进这一轮、
    或者从最终页退出的那个窗口，不算在局里。
  - 进房间时座位直接是 `PartySeatChoosing`：刚加入的窗口也算"在局里"，房主要等它按开始
    （否则房主会在新窗口还没看清楚前就开局）。
- **加载宽限没了**：老代码是 `4s 宽限 + lead-in`（默认 10s），现在只有 lead-in（默认 6s）。
  各窗口在 `PartyCharging` 那一帧加载自己的谱面（**白色确定闪光就是盖这次加载的**，
  在 charge 里手动点亮 `confirmFlashActive`，不要设 `confirmStartPending`——这里没有异步加载），
  然后等共享 QPC 时刻。
- **开局没有倒计时（2026-09-19 改）**：charge 分两段，靠 `startCounter == 0` 区分：
  1) **加载段**：房主 `beginLoading()`（epoch++，phase=Charging，**不写 startCounter**），
     每个窗口加载完自己的谱面就 `setSeat(PartySeatLoaded)`（**新加的座位状态**，不是 Ready）；
  2) **武装段**：房主看到 `allLoaded()`（非 Lobby 座位全是 Loaded）就把起奏时刻写下去
     （`armStart(now + 0.8s)`，**不动 epoch**，否则成员会以为换了一轮）。
  于是等待 = 最慢那个窗口的加载（实测 0.2~0.3s）+ 0.8s 引信，**不再是固定的 5.8s**。
  实测日志：`[party] start armed: ... (every chart loaded; the room waited 0.24s for the loads)`。
  坑：
  - `armStart` 只在 `phase == PartyCharging` 时才写，否则会把已经释放的房间"复活"；
    房主侧用 `mpArmedEpoch` 保证一轮只武装一次（重复武装会把时刻一直往后推）。
  - 房主自己的加载失败时 `releaseSong()` 回大厅，成员按"房主已放弃本曲"回列表。
  - **没有"强制开始"**（2026-10-17）：房主再按确定不开局，只有 `allReady()` 才 `beginLoading()`。
    迟到/没按确定的窗口在 charge 那一帧 `setSeat(PartySeatLobby)` 让开（`charge skipped`），
    这是**它自己让开**，不是房主把它踢出去。
  - 成员第一次看到 charge 时的 **`late` 判定必须带 `startCounter != 0`**：QPC 是个大正数，
    裸写 `now >= startCounter` 会在加载段就把所有窗口判成"错过开局"。
  - **看门狗** `kPartyLoadTimeoutSec = 6.0`：某个窗口加载失败/卡住时房主照常武装
    （日志写 `watchdog: somebody never loaded`），否则整房永远停在加载段。
    迟到的窗口仍然会**正确入局**——时钟锚在共享 QPC 上，它只是少看一段开幕卡。
  - 房主自己的加载失败时 `releaseSong()` 回大厅，成员按"房主已放弃本曲"回列表。
- **结算画面进不去了（2026-09-18 修）**：多人路径从来没调 `announceTrack()`，
  `trackDurationSec` 一直是 0，于是 `resultDue` 恒假 —— 歌放完就停在演奏画面。
  现在 charge 加载成功后两边都调 `announceTrack()`，并且**房主把 `trackDurationSec`
  也写进共享页**（`publishTrackEnd` / `readTrackEnd`，共享块多了 `trackEndMs`，映射名 v2；
  2026-10-17 又一个字段 `skipReq`，映射名升到 `Local\CppSekai.Party.v3`——旧窗口会去开
  另一个映射，不会读错偏移；其后 v4 加结果块、v5 加卡面、**v6 卡面换成头像路径**）。
  成员每帧读它——成员没有 BGM，自己算的话会拿谱面最后一个音符当曲终，比房主早好几秒切结算。
- **传输**：一条命名文件映射（`platform/Party.cpp`），
  8 个座位 + 一个控制块。没有 socket、没有管道、没有序列化，也不 flush：**一次状态变更
  就是往共享页写一个 LONG**，别的窗口下一帧就读到了——同机上这是延迟的物理下限
  （只剩读者自己的一帧）。控制块用 seqlock（写者 `InterlockedIncrement(&seq)` 包住），
  座位里每个字段只有主人写、天然无冲突。
- **座位/主机**：进房间时 CAS 抢最低空位；`hostSlot` 谁都不是有效活人时，最低活位接管。
  每帧写 `heartbeat`（GetTickCount64 低 32 位），超过 6 秒没心跳的座位被判死——但
  **`PartyCharging` 阶段不回收**（那时所有窗口都在加载谱面，谁都不心跳，回收会把房间打散）；
  被误判死掉的窗口会自己把座位抢回来（`update()` 开头）。
- **起奏同步**：`startCounter` 是一个 **QPC 值**（不是相对时间）。QPC 全机唯一，所以
  "T 时刻开始"对所有窗口意义相同。各窗口到点各自 `beginSessionClockAt(startCounter)`
  （`perfStart = startCounter`），之后再走房主的 lead-in（默认 6s，就是开幕卡 + 淡入）。
- **时钟跟随**（`resolveSongClock` / `songClock`）：房主每帧把自己的 `songTime` **连同采样时的
  QPC** 发到共享页（`publishHostClock`）；成员的本底时钟是 QPC（它没有 BGM，走 `wallSongTime`），
  用包里的 QPC 差把房主时钟外推到"现在"，再把差值当作**偏移量**缓变跟上：
  `t = 本底 + offset`。三点要注意：
  - **房主未发布之前读到的包是 0**，直接拿去算会得到几个小时量级的误差（第一次实测就是：
    成员时钟瞬间跳到 +3993s，整首歌全 MISS）。所以控制块里有 `hostClockValid`，
    没发布就返回 false；另外 `|error| > 5s` 的包一律丢弃（`[party] clock sample rejected`）。
  - **起奏前允许自由对齐**（`local < 0.5` 直接 snap）：那时只有开幕卡在看时钟，
    跳一下没人看得出来，但可以把对齐误差压到噪声级——慢慢挪的话第一串音符会差几十毫秒。
  - **起奏后 3 秒内用 50% 速率修正，之后 3%**。因为房主的音频时钟在**音乐真正开始**的那一刻
    会被重新锚定（`AudioEngine::update` 里 `mMusicStartFrames = 现在`），一下子跳最多一帧
    （30fps 下 25ms），而那一刻正是第一批音符要判的时候。3% 要好几秒才吃完这个台阶。
  实测（`--party-auto` 两个窗口，`CPSEKAI_MP_TRACE=1` 对 QPC 轴）：**中位 3.4ms / 最差 6.7ms**。
- **BGM 只有房主播**：成员的 `startSession(..., allowMusic=false)` —— 不 `loadMusic`、不检测
  头部静音，`audio.hasMusic()` 为假，于是时钟自然落回 wall clock（正好被上面的跟随覆盖）。
  选曲界面的试听也只有房主放（`playPreviewHere`）。同一份 mp3 在两个窗口差几毫秒放出来就是回声。
- **开幕卡的「跳过」是整房一起跳**（2026-10-17 修，原来在多人里按了没反应）：
  lead-in 属于房间而不是某个窗口——成员的时钟每帧都被拉到房主的时钟上
  （`resolveSongClock`），所以在成员窗口里挪 `perfStart` / `audio` 位置**下一帧就被擦掉**，
  看起来就是按钮坏了。现在 `skipIntro()` 在 `party.active()` 时**只记一个请求**
  （`PartyLink::requestSkip()`，往**自己座位**里的 `skipReq` 计数器 +1），
  房主在演奏态每帧比一次 `skipTotal()`（所有座位之和），涨了就自己执行
  `performIntroSkip()` —— 房主的时钟一跳，全房跟着跳。只认**上涨**：
  座位被回收会让总和掉下去，那不算请求。一帧延迟，看不出来。
  开局时 `mpSkipSeen = party.skipTotal()` 取基线，否则上一局的请求会在新一局开头触发一次跳跃。
  实测（两窗口 `--party-auto`，winsend 点成员窗口的跳过钮）：
  成员日志 `[intro] lead-in skip requested (the host performs it)` →
  房主日志 `[intro] lead-in skipped (the whole room)`。
- **暂停 / 放弃 / 重试**（2026-09-18 补）：房主暂停时 `setHostPaused(true)`，成员看到后
  **把画面钉在房主报的那个 songTime 上**（不外推，否则暂停期间会越跑越远）；恢复瞬间靠
  `|error| > 0.2` 的 snap 重新对齐。**成员按不动暂停**（`requestPause()` 直接拒绝并往
  `mpStatus` 写一行提示）——时钟是房主的，本地暂停只会让自己脱节。多人模式还会**强制关掉
  "失焦自动暂停"**（多个窗口并排，几乎总有一个不在前台）。
  - 房主在暂停对话框选**放弃 / 重试**：`releaseSong()` 把房间丢回大厅（epoch++），
    **成员在演奏态每帧看一眼房间**（`phase == PartyLobby` 且 epoch 变了）就 `leaveLiveForRoom()`
    回选曲——否则它会继续打一首已经没人计时的歌，而且再也回不到列表。
  - **重试在多人里 = 重开本轮**（回大厅 → 房主重新广播同一首 → 全员再按确定），
    不是原地重启：共享一局里的时钟/谱面/起奏时刻都是房间的，一个窗口自己重启必然失步。
- **选歌试听与打歌 BGM 重叠（2026-09-18 修）**：房主进入 charge 后**还停在选曲界面**
  等整个 lead-in，而这一帧的 BGM 已经加载好了——试听每帧还会重启，于是"选歌的曲子"和
  "正在打的曲子"叠在一起（而且看起来是概率触发，取决于 charge 落在哪一帧）。
  修法：`playPreviewHere` 里加上 `!mpStartPending`（charge 一武装就停试听）。
- **charge 阶段的数字倒计时已删**（2026-09-19）：原来在选曲界面上盖一层 + 中央大数字
  （"即将开始 5.8"），因为那时 charge 是**固定的 5.8s**，不盖点什么就像"按了确定又回到选歌"。
  改成"加载完就开"之后整段只剩约 1 秒，数字只会闪一下，于是：
  遮罩和大数字全删（`state == AppState::Select && phase == PartyCharging` 那个块没了），
  只在手机面板状态行写 `谱面加载中…` → `即将开始`（**不带数字**，
  `startCounter != 0` 区分两段）。这段等待由房主那边点亮的白色确定闪光自解释。
- **"失焦"在多人里一律忽略**（2026-09-18）：不只 `party.active()`，只要**本机有房间**
  （`roomOpen`，启动时 `roomExists()` 的结果）就跳过所有失焦处理——包括演奏态失焦直接
  回选曲那条老路径（lead-in 期间失焦会 `state = Select`，在两个窗口互相点击时特别容易触发）。
- **房主中途关窗**：成员发现 `hostAlive()` 为假就放弃跟随、退回自己的 QPC 时钟
  （`[party] host gone: running on the local clock`），不会因为跟着一份冻结的包而卡死。
- **SMTC / 任务栏进度只有房主汇报**（`smtcHere`），否则两个窗口抢同一个媒体控件。
- **换歌靠文件名的**：房主广播的是**谱面文件名**（`0001_expert.sus`）+ `musicId`，
  成员在自己的谱面表里找同 musicId 的同难度（`findPartyEntry` / `buildPartyOptions`）。
  两台窗口读的是同一个 `charts\`，所以一定找得到；成员没有的难度不会出现在它的难度条里。
- **多人模式下窗口标题带玩家名**（`CppSekai - <名字>`）：否则任务栏里几个窗口分不出来，
  无头脚本也没法指定要点哪个（winsend 按标题子串找窗口）。
- **开关**：`设置 -> 系统 -> 多人游玩`（`UserSettings::multiplayer`，写进档案）或命令行
  `--party`（本次运行有效，不落盘）。打开它会**顺带把多开策略设成"允许多开"**——多人本来
  就需要多个窗口，而多开时每个窗口登录不同用户（成绩各自记账）。
- **只有一个人的房间 = 单人流程**（2026-11-25）：`partyUsable()`（active && 玩家数 ≥ 2）
  才把多人 UI / 确认流程交给房间；只剩自己时选曲列表完全可用，`确定` 进最终页、`开始`
  直接开打，BGM 由本窗口播、结算也是单人两页。日志写 `[party] solo start ...`。
  中途加入的窗口会落在 **charge 已经过了**的那一侧，就本曲不参加
  （`[party] charge skipped (...)`），不会半路掉进已经开始的谱面。
- **回归脚本**：`.workbuddy/tools/mp_verify.sh`（三轮全自动：一轮完整对局 + 暂停广播 +
  **打到结算画面并回选曲**，全 PASS/FAIL 输出）。`--party-auto <难度>` 现在会走真实的
  **最终确认页**：主机 1.8s 打开它、再按开始；成员选完难度后同样打开它再按开始。
  没有房间时它就是在 1.8s 按一下确定（配 `--no-party` 可以测纯单机流程）。
- **房间状态在选曲界面上**（2026-09-18）：`game::SelectPartyInfo/Result` 是选曲界面
  与房间之间的接口（`drawSongSelect` 最后两个参数，不传就是纯单机）。
  `--screenshot-time` 在选曲态就能拍到灰罩 / 手机面板（顶栏那条横幅 2026-10-17 已删；
  手机面板的状态行与左下角房间胶囊 2026-11-25 已删，等待信息只在最终确认页）。
- **偏差断言要绕开"成员追赶"那一下**：mp_verify 里插值时如果成员这段 offset 变了 >20ms
  就单独统计（`member clock corrections seen`），不算进 skew——那是它按设计跳到房主时钟上，
  插值会把一个 350ms 的假偏差报出来（真机上两窗口始终是同一条时间轴）。

### 设置 / 加入房间的几个坑（2026-09-17 补，都是用户实际踩到的）

- **多人游玩开关是"按档案"存的，房间是"按机器"的**。多开时第二个窗口会**换成另一个档案**
  （`[instance] multi-open -> profile 'other'`），而这次换档案发生在实例策略判定**之后**、
  于是它读到的 `multiplayer` 是**那个档案自己的**（默认 false）→ 于是第二个窗口不进房间、
  房主永远只有自己一个人 → 表现就是"开了多人游玩却打不起来"。
  修法：进房间前先 `PartyLink::roomExists()`（`OpenFileMappingW` 试探），
  **本机已经有别人开好的房间就无条件加入**（日志会写
  `[joined a room this profile has switched off]`）。档案之间不再需要各自打开一次。
- 房间没起来时启动日志会直说：`[party] off (设置 -> 系统 -> 多人游玩 没开，本机也没有别人开好的房间)`
  / `[party] ready: seat N ...` / 单人开局时 `[party] solo start (1 player in the room...)`。
  **"多人没生效"先看这三行**。
- 设置卡片（系统页）：**"多人游玩"勾选框在"只允许一个实例"时是灰的且点不动**（`ui::checkBox`
  加了 `enabled` 参数，`BeginDisabled` + 灰化；灰着的时候**不回写**，否则打开一次设置页
  就把开关悄悄关掉了）。反过来，选"只允许一个实例"会**自动关掉**多人游玩。
  档案加载时也补了一条：`multiplayer == true` 就把 `instanceMode` 抬成 1，保证文件自洽。
- **昵称和"用户"下拉框是两个东西**：下拉框列的是 `profiles/index.json` 里的档案标签，
  昵称在档案数据里（`account.name`）。以前改昵称不更新标签，于是设置页显示"默认用户"。
  现在：改昵称即同步标签（空昵称不动，避免把标签清成空）；启动时也会把不一致的标签
  自动纠正（`[profile] label of 'default' updated to 'xxx'`），多开换档案、切用户同理。
- **窗口标题带玩家名**（`CppSekai - <名字>`）：多开时任务栏/无头脚本都靠它区分窗口；
  注意昵称改了要重开窗口才更新标题（房间里的名字是即时更新的）。

### 触摸屏多窗口（2026-09-17 补）

- 同机两个窗口并排打时，**SDL 默认会吞掉"点击激活窗口"的那一次按下**
  （`SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH` 默认 0：只激活、不给事件），于是后台窗口的
  第一次触摸直接消失 = 一个音符判废。**多人模式启动时已把它设成 "1"**（房间起来之后才设，
  单机手感不变），激活和事件一起给。
- 触摸本身不会"抢键盘焦点导致另一边判不了"：Windows 把每个触点按坐标投给**它下面的那个窗口**
  （WM_POINTER/WM_TOUCH），SDL 把它变成该窗口的 `SDL_FINGER*`；游戏判定走 finger 路径，
  触摸顺带产生的合成鼠标事件带 `SDL_TOUCH_MOUSEID`，在鼠标分支里被过滤掉。
- 真正会烦人的是**层级**：触摸会把那个窗口抬到最前，两个窗口重叠时会被对方盖住。
  解法是并排放（不重叠），或者给从属窗口点 `WS_EX_NOACTIVATE`（还没做，需要时再加开关）。
- 验收办法：两个窗口都开 `--flick-log`（各自 cwd 下的 `flick_debug.log`，**要放不同目录**
  否则互相覆盖），或者直接看 HUD 左下角队友分数条——某个窗口如果摸不到，它的分数就一直是 0。

