# CppSekai — 项目长期记忆

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
