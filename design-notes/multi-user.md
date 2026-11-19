# 多用户 / 渲染模式 / 谱面目录

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 多用户 / 渲染模式 / 谱面目录（2026-09-16）

- **谱面目录一律是「exe 同级的 charts\」**（`downloader/chartdl.cpp` 的 `defaultChartsDir()`），
  不再是 `..\charts`。打包版解压出来是 `CppSekai-<日期>\`，`..\charts` 会落到游戏文件夹**外面**。
  游戏侧**两个目录都扫并且合并**（`main.cpp` 扫 `chartCandidates` 全部候选、按 .sus 文件名去重，
  先到的目录赢），所以老的仓库根 `charts\` 和新的 `build\charts\` 同时可见，不会“谱面消失”。
  `chartsDir`（`--select-id` 交接、空列表提示用）仍取**第一个非空**候选。
- **多用户**（`game/SongSelect.cpp` 的 `loadProfiles` / `saveProfiles` / `profileDataPath`）：
  `<dataDir>\profiles\<id>.json` 一人一份 `{settings, scores, account}`，
  `<dataDir>\profiles\index.json` 记 `{active, users:[{id,name}]}`。
  首次运行**把老的 `userdata.json` 复制**成 `profiles/default.json`（复制不是搬，老版本回滚照样能跑）。
  切换走 `main.cpp` 的 `activateProfile()`：存旧的 → 读新的 → **把 live 镜像全部从新档案重新推一遍**
  （`resW/resH` / `fpsLimitLive` / `noteSpeed` / `leadIn` / `gUserOffsetSec` / `windowMode` …）。
  **坑**：`persistUserData()` 是「把 live 值抄进 userSettings 再写盘」的方向，不重推的话切换用户会把
  上一个用户的窗口尺寸/帧率写进新用户的档里（实测 `default` 被 `second` 的 800x450/144 覆盖）。
  命令行给过的项（`--speed` 等）保持优先，和启动时同一套 `if (!xxxGiven)` 判断。
  账户页的 `nameBuf/orgBuf/noteBuf` 是函数 static，用 `bufProfile` 记住它是哪个用户填的，
  切换后必须重填，否则下一次击键会把上一个用户的名字写进新用户。
  切换的窗口模式 / 分辨率 / 开屏样式**下次启动才生效**（写在界面上），其余当场生效。
  删除用户**只从 index 里拿掉，文件留在 `profiles\`**（不删数据）。无头检查：`--activate-profile <id>`。
- **渲染模式**（`UserSettings::renderScale`，0=窗口多大渲染多大 / 1=固定分辨率）：
  1 的时候 `Renderer::setRenderTargetSize()` 开一个 `resW x resH` 的 FBO，场景和 ImGui 都画进去，
  每帧末尾 `presentFrame()` 再按**等比 + 黑边**贴到窗口。这样拖动窗口只缩放画面、不改排版。
  关键是三件事同时成立，少一件画面就错位：
  1. `windowW/windowH`（游戏内的一切坐标）= **渲染尺寸**，`winPixelW/H` = 真实窗口（SDL 事件 / 视口）；
  2. **在 SDL 事件进 switch 之前把指针坐标改写成渲染坐标**（`mapPointerEvent`，在
     `ImGui_ImplSDL2_ProcessEvent` 之前调用），这样游戏、ImGui、触摸三套代码一行都不用改；
     `SDL_GetMouseState` 那种直接轮询的要单独过 `toGamePoint()`；
  3. `ImGui_ImplSDL2_NewFrame()` **之后**把 `io.DisplaySize` 设成渲染尺寸、`DisplayFramebufferScale`
     设成 1（后者让字体图集也按渲染分辨率栅格化，这正是「只渲染多大分辨率」的含义）。
  `--screenshot` 读的是**真实帧缓冲**（`winPixelW/H`，带黑边），读 `windowW/H` 只会拿到画面一角。
  无头检查：`--render-size <w>x<h>`（配 `--width/--height` 就能造出窗口≠渲染尺寸）。
  验证方法：`--result-preview` + `winsend.exe CppSekai click <x> <y>` 打「继续」按钮，
  窗口模式和固定模式下**同一个窗口像素**都要命中（`[result] continue -> song select`）。
  **winsend 的 click 会先发 WM_MOUSEMOVE**，SDL 的按键事件是用最后一次移动的位置，
  所以只发 click 不先 move 的话点击会落在 (0,0)。
- **分辨率**：预设从 640x360 起（`kResW/kResH`），还有「自定义…」两个输入框
  （**提交才应用**，`IsItemDeactivatedAfterEdit`，否则输入 "1280" 会中途 resize 四次窗口）。
- **`.workbuddy/tools/shot_probe.py` 现在带 Pillow 回退**（`magick` 不在 PATH 时用 PIL 解码），
  没有 ImageMagick 的机器也能用像素探针。

