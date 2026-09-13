# CppSekai — 项目长期记忆

## hold combo 机制（2026-09-12 实装）
- pjsk 的 combo 按 **hold 期间每半拍 +1**（社区公式："八分拍中继点 = 0.1 倍权重的
  COMBO 加分"），不是只有首尾两个音符。核心 calculateHitEvents 现在为每个 hold 合成
  半拍 kind-4 tick（起点向上取整到八分位、终点向上取整、不含端点，同时间同轨道去重），
  判定侧复用 tick 门控（按住才计分、断了静默）。
- ~~坑：`--auto` 无头运行退出时会把 autoplay=true 持久化进 userdata.json~~ **已修（2026-09-12 晚）**：
  `persistUserData()` 看 `autoplayGiven`，命令行给的 autoplay 不再写回存档；`--screenshot` 模式
  则整段跳过 `persistUserData()`，一个字节都不写。

## 选曲列表的滚动模型 + 平台层三件事（2026-09-12 晚，详见 AGENTS.md）
- **列表是自写状态机 + 现在是真循环**：`scroll` = 列表视口垂直中线处的内容坐标，等间距 pitch 104*k；
  滚轮/拖拽/惯性/吸附都只改它；`scrolling` 期间**不高亮也不换曲**，停手 0.20s 才把中间那行提交为
  选中。**行是无限序列的 slot（`wrapSlot()` 取模），滚过最后一首接第一首**，不要加 clamp。
  点击在松手时判定、位移<8px 才算点击。触摸复用 SDL 的 touch→mouse 合成，别另写手指滚动路径。
- **排序 / 分组**：搜索框右边两个 combobox（按名称/按难度；关闭/按难度段/按标题/**按首字母**）。名称排序与
  标题分组用 musics.json 的 `pronunciation`（片假名折平假名），没有读音的谱退回标题本身。
  行结构 `ListRow{header,song}` 由 `buildRows()` 生成，按 signature 缓存。这两个值**存进 settings**
  （`sortMode`/`groupMode`，drawSongSelect 收 `int&`，调用方变更时写盘）。
- **段标题可点 → 索引面板**（2026-09-13）：分组开着时点段标题把列表换成 key 面板
  （`indexOpen`/`indexAnim`，底板缩放+淡入），点 key 用 `nearestSlotOfRow()` 飞过去并关面板。
  面板开着时要 `listHovered = !indexOpen && ...`，否则背后列表会跟着滚。
- **选曲背景可用桌面壁纸**（2026-09-13）：`bgStyle/bgBlur/bgDim`；路径三级回退
  （SPI_GETDESKWALLPAPER → HKCU\Control Panel\Desktop\WallPaper → Themes\TranscodedWallpaper，
  各自验存在）；解码+降采样到 1024+三次 box 模糊在 `Renderer::loadBackdropTexture()`，
  **只在设置开着时加载**（默认零开销），纹理由 main.cpp 持有、`setSelectBackdrop()` 交给 SongSelect，
  cover 铺满 + dim 黑罩；模糊只在滑条松手时重算。
- **入场/过渡动画**（2026-09-13）：手机面板入场滑入+淡入挂在"手机顶点整体旋转"那趟循环里
  （位移+顶点 alpha）；`enterAnim` 靠"隔 >0.5s 才又调用一次 = 刚进来"判定；选中卡片高度按槽位
  做指数趋近（`slotHeights` 在 signature 变化时必须 assign）。
- **列表前导等级跟当前选中难度走**（`levelForDifficulty()`，缺谱面文件时回落官方定数表）；
  手机面板未选中的难度是空心圆；「歌曲等级」牌子压在等级圆上沿。
- **trace（kind 3，绿色竹节）按"覆盖"判定**：按住那条轨道就 PERFECT，没有尾判、头不用重按
  （以前只有"按一下"能清 → 按住不放全 MISS）。准点按下仍走 findCandidate 拿分级。
  竹节的构成 = tap 头 + N 个 friction tap(kind 3) + 一条 guide hold（纯视觉，不发判定事件）。
- **可点元素必须在 `SDL_FINGERDOWN` 里也 hit-test**：触摸的合成鼠标事件带 `SDL_TOUCH_MOUSEID`
  会被鼠标分支过滤 → 只写在鼠标分支的按钮触摸屏点不到（踩过两次：HUD 暂停、开场「跳过 >>」）。
- **exe 是 Windows 子系统**（`-Wl,--subsystem,windows`）：双击无 cmd 窗口。
  `AttachConsole(ATTACH_PARENT_PROCESS)` 只在 stdout 句柄无效时才 `freopen("CONOUT$")`，
  否则会把管道/mintty 的输出抢走；都没有就写 `cppsekai.log`。
- **flick 方向判定用屏幕 px/s**（阈值按 `windowH/1080` 缩放）。别退回 worldY vs lane 单位——
  两者尺度差 ~6 倍，等于要求上滑"竖直 3.6 倍"才算 flick，触摸屏上根本刷不出来。
- **flick 手势喂进判定引擎的三条硬规矩**（2026-09-13 修，改 `movePointer` 前先读）：
  1) **轨道要用 `flickJudge()` 两次尝试**：先 `track.lanePos`（按下时的轨道），None 再试
     `track.restLanePos`（手指低速停留时记录的轨道）。走位 hold 的**尾判事件是终点轨道**
     （kind 5 标记带终点时间但 center 是起点轨道，kind 2 尾判在终点轨道），
     只按按下轨道滑 → `laneCovers` 永远落空 → 表现就是"必须松手再滑才能清"
     （新触点从当前位置重算轨道，所以才"好使"）。
  2) **`dt` 必须钳到 ≤30ms**：触摸屏手指静止时**一个 motion 事件都不发**，flick 第一帧的
     `now - lastMoveTimeSec` 可能是几百 ms，不钳的话 800px/s 被算成 ~100px/s，直接刷不出来。
     同时空档 >50ms 要清速度滤波（`kFlickIdleGapSec`）。
  3) **不要用一次性闩锁**：旧 `track.flicked` 让整次触点只能 fire 一次，hold 途中任何提前/误触的
     滑动都把机会用掉。改成 fire 后归零 `travelUp/Side` + 60ms 冷却，松手 last-chance 不跳过。
  另外 `track.isTouch` 要显式记录（别用 `fingerId > 0` 猜），触摸/鼠标的阈值差一倍。
- **autoplay 必须零 miss**：flick 尾在 `mActiveHolds` 里是故意留白等玩家滑的，`--auto` 下要
  用 `judgeHoldTail(hold, Perfect)` 兜底，否则预览会把血打空。
- **`--test-hits` 是判定侧的回归/复现利器**：它把每条 HitEvent 按自己的 `event[1]`（音符轨道）
  和谱面时间喂给 `judgement.tap/flick`，等价于"完美玩家"。要复现输入层 bug，临时把那一行的
  lane 改成别的值即可（例如走位 hold 用 1.5f 复现漏判），跑完记得改回来。
  无头跑法：`./build/cppsekai.exe --sus charts/0628_hard.sus --test-hits --window windowed
  --screenshot <真实路径> --screenshot-time <秒>`，看 `cppsekai.log` 的 `[stats]` 行当断言。
  **注意 `--screenshot` 给不存在的目录会静默不开跑**（进程挂着不退出）。
- **图片开屏绝对不能是全屏窗口**（2026-09-13）：透明底靠 `ALPHA_SIZE=8` + `glClear(alpha=0)`
  + `DwmExtendFrameIntoClientArea(-1,-1,-1,-1)`；但**覆盖整个桌面的窗口会被 Windows
  fullscreen optimizations 接管、DWM 合成被绕过 → 透明变不透明黑**（表现："黑底 + 一张图"）。
  所以 `windowMode==2 && splashStyle==0` 时**创建时不进全屏**，加载完在 boot 末尾再
  `SetWindowFullscreen`；开屏窗口尺寸还要限制在 `SDL_GetDisplayUsableBounds - 16px` 内
  （存档分辨率==显示器尺寸也会被 FSO 抓走）。经典开屏（深色底）保持创建即全屏。
- 文档：根目录新增 **`CLI.md`**（命令行手册）。工具：`.workbuddy/tools/pngcrop.py`
  （纯 python PNG 裁剪 + 放大，工具链没有 Pillow/ffmpeg）。


## SMTC 的 TimeSpan ABI（2026-09-12 修，别再踩）
- `ITimelineVtbl` 的 TimeSpan 参数（StartTime/EndTime/Position 等）是 **8 字节 struct
  按值传（INT64 tick，100ns 单位）**，不是 boxed IPropertyValue 指针——传指针会得到
  垃圾刻度，系统面板永远显示不了进度（这就是"SMTC 无法传导"的真凶）。
- updatePlayback 里有一次性 `[media]` HRESULT 诊断日志，SMTC 再出问题先看这个。
- 新曲 BGM 路径不一定是 `se_<id>_01`：先查 `viewer-api.unipjsk.com/api/master/1/musicVocals`
  的 `assetbundleName`（如视奸 628 = `vs_0628_01`），走
  `assets.unipjsk.com/ondemand/music/long/<bundle>/<bundle>.mp3`；封面是
  `startapp/music/jacket/jacket_s_<id>/jacket_s_<id>.png`；官方 fillerSec 在
  musics master 里，可写进 sidecar `<id>.json`。

## 判定↔渲染联动协议（2026-09-12 新增）
- **宿主→核心的三个发布口**（都在 main.cpp 播放分支，仅 `!autoPlay`）：
  `markNoteHit(HitEvent索引)`（增量，`s_hitPublishCursor` 在 startSession 重置）、
  `setMissedHolds(键)`（每帧全量）、`setDimmedHolds(键)`（原有）。核心侧键表
  `hitEventNoteIds` 与事件流同序（calculateHitEvents 里一起 stable_sort）。
- **音符到线后的行为**：drawingNotes 用独立的 `fallTime` 字段延长**裁剪**窗口（miss 滑出屏幕）；
  `visualTime` 保持上游值——它同时是 `approach()` 的**位置锚点**，动它全场音符错位
  （2026-09-12 踩过，"音符全部散架"就是这个）。击中隐藏靠 hitNoteIds；autoplay 靠
  `effectsAutoplay && t>visualTime.max` 保持上游到线即消失。过线后的下坠靠 approach() 外推。
- **miss 的 hold**：主体继续下落（drawHoldCurves 的 missed 分支，`segmentStartScaled`
  不再钳到当前时间），tick 同步延长窗口；判定侧静默（state=2 不记分）。
- **tick→hold 匹配必须按时间窗**（hold 走位时 tick 中心≠起点中心），无 marker 的
  guide tick 保持 always-auto-hit。tail 标记（load 里的 `holdTail` flagging）仍是按**中心**
  匹配 → 走位 hold 的尾巴（终点轨道 ≠ 起点轨道）**不会被标记**，`hold.tailIndex` 也解析不到，
  **未修**。2026-09-13 确认后果：那条尾判按**普通 flick 音符**走（滑到就清、按住不放 180ms
  后 auto-miss），`tails` 统计少算它、松手分级那条路不生效——行为上可接受，要彻底对齐得把
  flagging 改成"按时间窗匹配"。同一处还是 O(n²) 扫描（每根 hold 遍历全表）。

## 资源与 git 的坑（务必记住）
- `.gitignore` **忽略整个 `assets/`**（连同 `toolchain/`、`charts/`、`build/`）。
  - `assets/mmw/**` 在仓库里是因为**先 commit 后加规则**——已跟踪文件不受 gitignore 影响。
  - `assets/select/**` **从未被跟踪**（后期手动建，`git add` 静默跳过）→ **换机会丢**，且
    `setup.sh` 只 re-fetch `assets/mmw`，**不处理 select**。
  - 结论：任何新加的 `assets/` 子目录都不安全，必须 `git add -f` 才会进仓库。
- `assets/select/` 是选曲界面的 UI 图，代码需要这 7 个（名字固定在 `SongSelect.cpp` 的 `selectTex()`）：
  `indicate_back_new.png` `clear_indicate.png` `fullcombo_indicate.png` `songlevel.png`
  `img_smartphone.png` `shufflebutton.png` `musicsetting.png`。
  缺失时**不崩**，只是图标/边框不显示（`selectTex` 返回 0 → 跳过绘制）。
- `assets/mmw/ui/close.png`（44×44，弹窗右上角的关闭叉；`Renderer::loadHud` 注册为 `"ui_close"`，
  经 `ui::setCloseTexture` 使用）。2026-09-11 发现丢失，从 `Drafts/close.png` 补回。
- **核对 HUD 精灵是否齐全**：`Renderer::loadHud()` 的 `add(...)` 列表（overlay 相对路径）逐个 `-f` 测试即可，
  共 92 项。全量跑一次能捞出所有缺失（这次只捞出 close.png）。
- `Drafts/` 是被跟踪的素材暂存区（webp 原图 + png 转换版），select 的多数图能从这里找。
  webp→png 用系统 `ffmpeg`（保留 alpha），没有 Pillow/magick。
- **`setSelectAssetDir` 契约**：传的是 **assets 根目录（`<exeDir>\assets`）**，SongSelect 内部再拼
  `select\<name>.png`。（2026-09-11 修：曾误传 `baseDir`，导致找 `<exeDir>\select\` 而图层永远加载不出。）
- **审计「换机必丢」清单**：`git ls-files -o -i --exclude-standard assets/`；取回被删文件用
  `git cat-file blob <commit>:<path>`。

## 手写 WinRT / COM vtable 的坑（`platform/SystemMedia.cpp`）
- vtable 槽位**必须按接口本身的方法顺序**，接口里的方法**不能乱拼**。踩过的雷：把
  `ISystemMediaTransportControls2` 的 `AutoRepeatMode/ShuffleEnabled/PlaybackRate/UpdateTimelineProperties`
  拼进了基接口 `ISystemMediaTransportControls` 的尾部 → 越界槽位 → **一进曲目就 SIGSEGV**（且加日志就不崩，
  很难查）。基接口只有 30 个方法（0–29，到 `remove_PropertyChanged`），`UpdateTimelineProperties` 在 `...2` 的槽 6。
- **已修（2026-09-11）**：`ISMTCVtbl` 尾部删干净，另建 `ISMTC2Vtbl`；`updatePlayback()` 里
  `QueryInterface(mSmtc, IID_ISystemMediaTransportControls2, &smtc2)` 成功才调 `UpdateTimelineProperties`。
  **IID = `{EA98D2F6-7F3C-4AF2-A586-72889808EFB1}`**（winmd 的 `winmd_guid.py` 查不到 versioned 接口，
  是从 docs.rs 的 windows crate 拿到的）。
- 核对槽位：`python .workbuddy/tools/winmd_dump.py C:\Windows\System32\WinMetadata\Windows.Media.winmd <接口名>`
  （打印方法声明顺序 = ABI 顺序）；IID 用 `winmd_guid.py <winmd> <名字>`（对 versioned 接口可能无效）。
- 同类风险：`IInteropVtbl` / `ITaskbarList3Vtbl` / `ITimelineVtbl` 也是手写的，换 Windows 版本后要重新核对。
- 遗留：`createTimeSpan` 把 boxed IPropertyValue 指针当 `TimeSpan` 传（ABI 应为按值 INT64），SMTC 进度条位置值可能不准。

## 判定/加分/扣血：与官方仍有差异（用户要求只改两项，其余不动）
- **已改**：BAD 判定（0 分、−50 血、断连击）+ **GOOD 断连击**（`registerJudge()` 里 `Good||Bad` 都 combo=0、
  mComboFactor 回 1.0）。`badMs` 默认 180 = `missAfterMs`（BAD 只填 goodMs 与自动 miss 之间的空档）。
  HUD 用 `judge/v3/4.png`（BAD）。
- **仍未改**（用户说"其它不用管"）：判定窗 40/90/140/180ms（官方 41.7/83.3/108.3/125，偏松）；
  血上限只有 1000（官方 2000）；baseNoteScore 缺 `floor()` 与 skill/fever 倍率；
  hold tick critical 权重 0.2（官方 Hold Sustain 是 0.1）；官方 AUTO = GREAT(70%)、血尽得分 ×0.1 未建模。
- 详见当日日志 2026-09-11.md。

## 玩家数据（`userdata.json`）
- 一个文件装**设置 + 成绩**：`{ "settings": {...}, "scores": {...} }`，由 `game::userDataPath()` 定位 ——
  有 `<exe>\..\charts` 就用**那一层**（build/ 布局 = 仓库根，和 charts/ 并排 → `rm -rf build` 不丢、
  拷到新机器 charts/ 旁边成绩就回来），否则 `<exe>\userdata.json`。
- 成绩 key = **谱面文件名**（与绝对路径无关，所以重下同样的谱能对上）。
- 优先级：**命令行 > userdata.json > 内置默认**（靠 `*Given` 标志）。
- `.gitignore` 里有 `userdata.json`；`--screenshot` 模式**不写**这个文件。

## HUD 缩放陷阱（`platform::Renderer` / `game/Hud.cpp`）
- `px()/py()` 是虚拟坐标→像素，`ps()` 是尺寸→像素。**位置用了 px/py，尺寸就必须用 ps()**。
  踩过：分数数字写成 `shadowH = scoreS(36.0f)`（漏 `ps()`）→ 画大 1.5 倍（`SCORE_ROOT_SCALE`）→ 数字重叠。
- 走 `img()` 辅助函数最安全（内部自动 `ps()`，虚拟坐标进出，且对缺失贴图安全）；
  **直接调 `AddImage` 的地方要自己记得是像素空间**。
- **贴图 key 的下划线**：combo 是 `combo_digit_b_0` / `combo_digit_n_0`（**带下划线**），
  life/score 的 shadow 是 `life_digit_s0` / `digit_s0`（**不带**）。踩过：combo 辉光写成
  `combo_digit_b0` 查不到 → 辉光从来没画出来。拿不准就对照 `Renderer::loadHud()` 的 `add(...)`。
- **生命条**：2560×600 sheet 整张画在 `(1442, 11)`、`444×104`；填充胶囊实测
  u [0.1531, 0.7414] / v [0.4617, 0.6083]，且 **UV 起点必须是 fillU0**（从 0 采样会取到透明区）；
  数字 `slotX = 1442+319-i*22`、`slotY = 21`、37/34。`lifePauseRect()` 必须跟这套几何一致。
  上游是 autoplay（血条永远满），所以它对"血量 <100%"的处理不可信，以我们的实测为准。
- 对齐 UI 时直接看上游 `D:\Dev\sekai-mmw-preview-web\native\src\mmw_overlay_player.cpp`（CppSekai 的移植源）。

## 协作约定
- **改完 + 验证过就 commit**（用户明确要求），别攒着。

## 构建
- `bash build.sh`（仅 Git Bash）。自带 zig **0.14.1**（`toolchain/`）+ SDL2 2.32.10，零系统依赖。
- **别用 zig 0.16**（c++ 驱动会吞 `-I`）；zig 缓存必须放 C 盘（build.sh 已设 `ZIG_GLOBAL_CACHE_DIR`）。
- DirectXMath 的 8 个 `-Wdefaulted-function-deleted` 警告无害，属第三方头文件。
- Git Bash 无 Unix `timeout`（会被解析成 Windows timeout.exe）。
