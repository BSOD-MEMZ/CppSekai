# 贴图瘦身 / UTF-8 路径 / 扫描开销

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 贴图瘦身 / UTF-8 路径 / 扫描开销（2026-09-16 晚）

- **贴图瘦身是改文件，不是改加载策略**。`loadTextureFromFile` 那套 maxDim/cropHeight
  策略（`keepRawTextures()`）**仍然默认关闭**——缩/裁之后的纹理尺寸和调用方自己维护的
  sprite 矩形对不上（2026-09-14 踩过，画面整体错位）。所以瘦身只动**消费方与纹理尺寸无关**
  的那几张，`.workbuddy/tools/shrink_assets.py` 里就是这份白名单（原图备份在
  `.workbuddy/backup/assets-20260916/`），跑 `--apply` 生效：

  | 文件 | 改动 | 解码内存 |
  |---|---|---|
  | `assets/mmw/stage.png` | 2048x2840 → **裁到 2048x1176**（下面 1664 行没有任何四边形采样） | 23.3 → 9.6 MB |
  | `assets/mmw/background_overlay.png` | 2048 → 1024（整张 UV，房间底板本来就是糊的） | 16.8 → 4.2 MB |
  | `assets/select/img_smartphone.png` | 1034x1942 → 517x971（画进一个矩形，内部布局全是比例） | 8.0 → 2.0 MB |

  合计 `[tex] 53.2 MB uploaded` → **28.2 MB**，启动 `stage textures` 484ms → ~100ms。
  A/B 验证：只换这三张、同一时刻截图，全图差异和"同一套素材跑两次"的噪声完全一致
  （mean 3.53 / >12 的像素 2.63%）→ 视觉无损。
  `buildStaticVertices()` 的舞台 sprite 现在写 `min(2048, mStage.width) x min(1176, mStage.height)`：
  裁过的文件和重新下载的原始文件都会映射到**同一片像素**，谁都不会被拉变形。
  **别动**：`notes*.png`（音符图集）、`effect.png`（判定特效）、`longNoteLine*`、`touchLine*`、
  `assets/mmw/overlay/**`（HUD 精灵，全都有写死的 sprite 矩形）——HUD 那套的瘦身走
  `overlay_opt/` + `shrink_hud.exe`（整张等比缩，UV 仍然对得上）。
- **路径一律 UTF-8**（`path_utf8.hpp`，2026-09-16 修启动崩溃）。Windows 上 `fs::path` 的窄端
  是**本地 ANSI 代码页**，`fs::path(std::string)` / `path.string()` 碰到代码页表示不了的字符会
  **抛 `filesystem_error`**，没人接 → `std::terminate`。实测：谱面目录里放一个
  `テスト曲_初音ミク_🎵_master.sus`，进程在 `[boot] chart scan` 之前就死了（cp936 机器上
  一串 UTF-8 字节不是合法 GBK 双字节序列）。所以：
  - `ChartEntry::susPath` / `bgmPath` / `coverPath`、`userDataPath()` 系列、`--sus` 的参数
    **全是 UTF-8**（和 `SDL_GetBasePath()`、`utf8Argv`、nlohmann::json 一致，也是 miniaudio
    和开了 `STBI_WINDOWS_UTF8` 的 stb_image 要的编码）；
  - 过 `fs::path` 边界只走 `path_utf8::toPath()` / `path_utf8::fromPath()`，
    不许再出现 `fs::path(窄串)` 和 `.string()`（`game/SongSelect.cpp` 里已经清干净，
    调用方想省事就照它那两行包装 `toFsPath` / `fromFsPath`）；
  - **隐式构造同样是坑（2026-10-10 修）**：`const fs::path p = <UTF-8 std::string>;`、
    或者把「返回 `std::string` 的函数」直接赋给 `fs::path`，走的还是 ACP 解码 —— 上面那句
    「不许 `fs::path(窄串)`」按字面 grep 抓不到这种写法。踩到的就是 `game::userDataDir()`：
    `const fs::path file = userDataPath(exeDir);` 把 UTF-8 的 `...\userdata.json` 交给 libc++
    用 cp936 解，目录名被毁（末尾的 `\` 还可能被双字节字的尾字节吞掉，于是 `parent_path()`
    切在别处 —— 表现为「存档目录从 exe 目录变成了它的父目录」）；字节不合法时直接
    `filesystem_error: __char_to_wide: Illegal byte sequence` → 静默退出。
    实测：程序装在 `D:\tmp-cn2\中文安装目录\子目录\` 时，存档落到
    `D:\tmp-cn2\涓枃瀹夎鐩綍\profiles\`（目录名里带 U+E15F 私用区字符）。
    **审计办法**：`grep -rn "fs::path [a-zA-Z_]* *=" game/ platform/ main.cpp` 逐条看右边
    是不是 `toFsPath(...)`；形参是 `const fs::path&` 的函数，调用点也要确认没传窄串。
    **这也解释了「Win11 的下载目录没事、Win10 的不行」**：Win11 那台开了
    「Beta: 使用 Unicode UTF-8 支持全球语言」时 ACP=65001，往返就是恒等变换，
    同样的代码不会坏 —— 不是 Win11 更聪明，是那台机器的 ANSI 代码页碰巧是 UTF-8。
  - `build.sh` 加了 `-DSTBI_WINDOWS_UTF8`（stb 才会用 `_wfopen`）；
    miniaudio 自己就把 UTF-8 转宽字符，不用管。
  回归：`build/cppsekai.exe --charts <含 emoji 文件名的目录>` 必须能扫出 1 首并出图。
  顺带修好的：成绩 key（`scoreKey()` = 谱面文件名）以前是 ACP 窄串写进 JSON 的，
  非 ASCII 名会存成乱码，现在和 ASCII 名一样（ASCII 下 ACP == UTF-8，老档兼容）。
- **`findSidecar` 的目录兜底以前是 O(n²)**（2026-09-16 修）。关键字兜底每次都
  `directory_iterator` 重走一遍整个谱面目录，而没有 `<stem>.png` 的谱面（很常见）每首都会走到
  这条分支。实测 **700 首 = 扫描 3.2s**（70 首 192ms，10 倍数量 17 倍耗时）。
  现在 `folderListing()` 按目录缓存一份排序后的 `(小写文件名, 完整路径)`，关键字兜底和
  **精确名探测**都改成查内存（后者以前每首约 12 次 stat）；`scanChartFolder()` 开头
  `clearFolderListingCache()`，所以 F5 / 刷新按钮照样能看到新丢进来的谱面。
  **这条缓存只在扫描期有效，不要提到扫描外面用**（否则会藏住新文件）。
  实测：700 首 3.25s → **0.25s**（热）。
- **`scanAllChartDirs` 曾经把每个候选目录扫两遍**（2026-09-16 修）：`chartsDir`
  （第一个非空候选）原来是靠再调一次 `scanChartFolder` 判断"空不空"找出来的，结果整个扫描
  白跑一遍。现在在合并那一趟里顺手记下来。**别再为了"探测有没有谱面"调 `scanChartFolder`**，
  它是全量扫描 + 每首开文件。
- `CPSEKAI_SCAN_TIMING=1` 打 `[scan] N chart(s): header X ms, sidecar Y ms`（开文件读 SUS 头
  和各 sidecar 的耗时分解）。剩下的时间在 `entries.push_back`（每条 15+ 个 string）和
  `std::sort` 上，700 首约 0.15s，不值得再动。
- 无头量性能：`[boot] chart scan` 行给的是**累计到那一刻**的耗时（前面的行一减就是这一段）；
  造大库就用脚本把一首谱复制 N 份到 `build/charts_bench/`，再 `--charts charts_bench`。
  注意 `--charts` 收的是**能被 Windows 打开的路径**：Git Bash 的 `$(pwd)` 会给出
  `/d/...` 这种 MSYS 路径，`--charts "$(pwd)/charts_bench"` 会扫出 0 首（用相对路径或 `D:/...`）。
- 扫描本身的固定开销还有：每首 `readSusHeader`（开一个文件读 40 行）+ sidecar 的存在性检查。
  这是 715 首那套的真实成本（约 0.4s），暂时够用；要再快只能并行，不是必须。

