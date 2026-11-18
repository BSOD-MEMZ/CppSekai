# 演出中的 2D MV、资源数据源、分数公式（2026-11-15，REA 逆向）

> 免责声明同仓库其他文档：本文是**自己玩、自己看**的对照记录，素材与数据权利归原权利人，
> 不随代码分发、不对外发布。
>
> 证据基座：日服 7.1.0 `dump.cs`（TypeDefIndex / RVA 都在本文里）+ `libil2cpp_arm64.so` 反汇编
> + master 六张表（`ingameNotes` / `ingameNoteJudges` / `ingameCombos` / `ingamePlayLevels` /
> `playLevelScores` / `musicDifficulties`，都是公开镜像的**真值**）。

---

## 0 结论速览

| 问题 | 答案 |
|---|---|
| 2D MV 怎么放的 | CriWare Sofdec2（CriMana）播 `.usm`，按 `trunc(fps × 秒)` seek，**不是** Unity VideoPlayer |
| 现在下载谱面的源 | unipjsk（日服）/ sekai.best（国服独占），`chartdl.exe` 就用这两个 |
| 那些站有 2D MV 吗 | **unipjsk 和 sekai.best 都没有**；`storage.exmeaning.com` 有，**186 首** |
| 分数和综合力有关吗 | **强相关**：`基础分 = 综合力×4×levelFactor / Σ音符权重`（asm 铁证） |
| 等级判定和综合力有关吗 | **无关**：S/A/B/C 线 = `基础值 × levelFactor`，只看难度等级（但分数受综合力影响） |

---

# 第一部分：演出时怎么放 2D MV

## 1.1 三种演出模式，2D MV 只是其中一档

`LiveSettingData.LiveModeType`（TypeDefIndex 6039）—— 就是「演出设置」里那个下拉：

| 值 | 名字 | 实际内容 |
|---|---|---|
| 0 | `High3D` | 3D MV，高画质 |
| 1 | `Default3D` | 3D MV，普通画质 |
| 2 | `Mode2D` | **2D MV** |
| 3 | `Low` | 省电（低配 3D） |
| 4 | `OriginalMV` | 原版 MV（3D，original 编排） |

`MusicCategory`（original / another / sekai…）是**另一根轴**，决定「同一首歌唱哪版 MV」；
`LiveModeType` 决定「用 2D 视频还是 3D 模型演」。两者组合才唯一确定背景。

## 1.2 `Background2DView`：一台机器，两个子模式

`Sekai.Live.Background2DView`（TypeDefIndex 11920）字段：

```
movieCamera        相机（渲染 MV 层）
jacketCamera       相机（渲染封面层）
jacketModeRoot     ← 没有 2D MV 时走这条：封面
movieModeRoot      ← 有 2D MV 时走这条：视频
backgroundRenderer 底图
movieRenderer      MV 的 SpriteRenderer（CriMana 输出打在这上面）
jackets[]          封面层（多张并排/叠放）
movieAssetBundleName
```

| 方法 | 做什么 |
|---|---|
| `OnLoad()` | `SetupJacket()` + `SetupMovie(musicData, category)` |
| `SetupMovie(LiveMusicData, MusicCategory)` | async，返回 `MovieLoadResult{Success, Aborted, Failed}` |
| `MusicStart(float time)` | → `SeekAndPlayMovieAsync(startMusicTimeMs)` |
| `Pause/Resume/Retry/OnFailure` | 跟着演出时钟走 |

**「2D 模式」不是「拿封面当背景」**：有 2D MV 资源就播视频，没有才退回 `jacketModeRoot`。
两个 root 各配一台相机，是为了 MV 层和封面层能独立控制分辨率/色彩。

## 1.3 播放器是 CriWare Sofdec2（CriMana）

`MovieBasePlayer`（10607）：

```csharp
protected CriManaMovieController criManaMovieController;
protected MovieData data;
protected List<int> entryCountList;
public const int MOVIE_MAX_ENTRIES = 3;      // 最多 3 路并发
protected void SetMovieFile(string movieFile, Player.SetMode mode);
```

`MovieManager : CriCore<MovieManager>`（10604）全局单例，按 key 管多路：

| 侧 | 方法 |
|---|---|
| 准备 | `PreLoad(key, gameObject, shader)` → `MovieLoadResult`；`Prefetch`；`IsMoviePreloaded` |
| 控制 | `SetSeekPosition(key, frameNumber)` / `Play(key, loop)` / `Pause` / `Resume` / `Stop` / `WaitEnd` |
| 查询 | `GetMovieInfo(key)` / `GetFrameInfo(key)` / `Status(key)` |
| 释放 | `Free(key, owner, controller)` / `ForceFree` / `StopFree` / `FreeAll` |

`MovieController.RenderTarget` 是 `GameObject`（CriMana 往它的 MeshRenderer + material 输出）。
`MovieData` 只有一个字段 `List<string> FilePathDatas` —— **一个 key = 多个 usm**，按顺序连播。

`MovieManager.Save(int index, long totalbyte, string usmFileName, string outputFilePath, ...)`
+ `movie_data_storage_path` —— **usm 从 bundle 里解出来、落盘再交给 CriMana**
（CriMana 只吃文件路径）。这就是为什么 MV 必须单独下载。

## 1.4 数据描述：`MusicVideo2DModeData` + seek 表

```
MusicVideo2DModeData : ScriptableObject        (15075)
  int id / string name
  List<BundleTimeLineMovieData> movieDatas     (15076)
        string movieName
        List<BundleSeekMovieData> seekMovieDatas  (15077)
              int frameNo
```

`GetMusicVideo2DModeDataBundleName(int musicId)` 拼 bundle 名，字符串表里有模板 `live_pv/mv_data/{0}`。

**2026-11-15 更正**：CDN 上真正的落盘路径不是 `live_pv/mv_data/`，而是
`live/2dmode/original_mv/{id4}/`（见 §1.7）。`live_pv/mv_data/{0}` 是**游戏内逻辑名**，
服务端会映射成真实目录。

## 1.5 时间同步：ms → 帧（反汇编硬证据）

`Background2DView.<SeekAndPlayMovieAsync>d__14.MoveNext`，VA `0x50ACC08`：

```asm
0x050ACCAC  ldr  s0, [x0, #0x2c]      ; FrameInfo.framerateN（CriWare 千分单位：60000 = 60fps）
0x050ACCB0  ldr  x9, [x19, #0x18]     ; startMusicTimeMs
0x050ACCB4  mov  w8, #0x447a0000      ; 1000.0f
0x050ACCBC  ucvtf s0, s0
0x050ACCC0  scvtf s2, x9
0x050ACCC8  fdiv s0, s0, s1           ; framerateN / 1000
0x050ACCCC  fdiv s1, s2, s1           ; ms / 1000（秒）
0x050ACCD4  fmul s0, s0, s1           ; fps × 秒
0x050ACCD8  fcvtzs w8, s0             ; 截断取整
0x050ACCEC  ldr  w9, [x0, #0x54]      ; totalFramesPerFile
0x050ACCF8  csel w8, w9, w8, gt       ; clamp 到末帧
0x050ACCFC  cmp  w8, #1
0x050ACD00  b.lt ...                  ; < 1 帧就整个不播
```

```
frame = trunc( framerateN / 1000 * startMusicTimeMs / 1000 )
frame = clamp(frame, 0, totalFramesPerFile)
frame < 1  →  不播
```

三个要点：`framerateN` 是**千分单位**；**截断**不是四舍五入；越界夹到末帧、`<1` 不播。

## 1.6 3D MV 是另一套，别混

`MusicVideoData : ScriptableObject`（15078）里没有任何视频字段：

```
CharacterInfo[] characterInfos   上场角色 / 卡面 / 服装
StageInfo / CameraInfo / PostEffectInfo / CutinInfo
```

3D MV = 模型 + Timeline + Cinemachine + post effect，走 `Background3DView` /
`Background3DPlayer` / `IMusicVideo3DPlayer`，由 `MusicVideoPlayerFactory.CreateMain/CreateCutIn` 造。

## 1.7 ★ 2D MV 从哪下（2026-11-15 实测）

### 结论表

| 站 | base | 谱面 | BGM | 曲绘 | **2D MV** |
|---|---|---|---|---|---|
| unipjsk | `https://assets.unipjsk.com` | ✅ | ✅ | ✅ | ❌ 404 |
| sekai.best | `https://storage.sekai.best` | ✅（国服独占 11000+） | ✅ | ✅ | ❌ 404 |
| **exmeaning** | `https://storage.exmeaning.com` | ✅ | ✅ | ✅ | **✅ 200** |

前两个是 `chartdl.exe` 在用的（见 `CHARTS.md` / `downloader/chartdl.cpp`）。
第三个是 `D:\Dev\pjskassets\download_assets.py` 用的，覆盖 14777 个 bundle 组，
**是唯一带 2D MV 的公开镜像**。

### 真实 URL（已 HEAD 验证 200）

```
https://storage.exmeaning.com/sekai-jp-assets/live/2dmode/original_mv/{id4}/{id4}.usm
https://storage.exmeaning.com/sekai-jp-assets/live/2dmode/original_mv/{id4}/moviebundlebuilddata.json
https://storage.exmeaning.com/sekai-jp-assets/live/2dmode/background/default/default.png
```

- `{id4}` 是 4 位补零的 musicId（`0036` = ミラクルペイント）。
- usm 大小量级 **10~40 MB**（0036 = 19,545,888 B）。
- `background/default/default.png`（1.16 MB）是**2D 模式没有 MV 时的兜底背景** —— 对应
  `Background2DView.jacketModeRoot` 之外的底图。

### 有 2D MV 的歌：358 首，而且分两个版本

`live/2dmode/` 下有两个平级目录，**同名文件**：

| 目录 | 是什么 | 数量 |
|---|---|---|
| `original_mv/` | 原曲 MV（原曲编排） | 186 |
| `sekai_mv/` | セカイ MV（游戏默认那版） | 200 |

合起来 **358 首**有 MV（只有原曲 158 / 只有セカイ 172 / 两版都有 28），
清单见仓库根的 `music-mv.json`（`.workbuddy/tools/gen_music_mv.py` 生成）。
因为它们都叫 `<id4>.usm`，本地必须分开存：原曲 `mv/<id4>.usm`、セカイ `mv/<id4>_sekai.usm`。

#### 旧记录（只剩 original_mv 时的数字）

`_files_jp.json` 里 `live/2dmode/original_mv/*` 共 186 个 id，清单已落盘
`build/2dmv_ids.txt`（build/ 不入库）。要查全量可：

```bash
python - <<'PY'
import json,re
d=json.load(open(r'D:/Dev/pjskassets/_files_jp.json',encoding='utf-8'))
ids=sorted({k.rsplit('/',1)[1] for k in d if re.match(r'^live/2dmode/original_mv/\d+$',k)})
print(len(ids))
PY
```

### `moviebundlebuilddata.json` 的真身（233 字节）

```json
{"m_Name":"MovieBundleBuildData",
 "totalbyte":19545888,
 "movieBundleDatas":[{"index":0,"usmFileName":"0036.usm.bytes"}]}
```

注意 **`usmFileName` 带 `.bytes` 后缀** —— 官方是把 usm 当 TextAsset 打的包，
`MovieManager.Save(index, totalbyte, usmFileName, outputFilePath, loader)` 把它写成
`.usm` 再喂 CriMana。我们自己下的时候直接把 `/0036.usm` 存盘即可，不用管 `.bytes`。

### 也可以用 API 列/拿

```bash
# 列某个 bundle 里的文件（返回真实 url / size / sha256 / version）
curl -s "https://storage.exmeaning.com/api/assets/bundle-files?server=jp&path=live/2dmode/original_mv/0036&limit=10"
# 列某个前缀下的 bundle
curl -s "https://storage.exmeaning.com/api/assets/bundles?server=jp&path=live/2dmode&limit=50"
```

## 1.7b 「MV 上面那层纹理」查清了（2026-11-15）

`Background2DView` 的 prefab 是 **1249**（37 个节点），结构：

```
Background2DView               (场景里偏在 x=100，不在 HUD 层)
├ movieContent
│ ├ movieCamera                ← 独立相机，MV 由它渲染
│ │ └ Movie          scale=120 ← CriMana 的输出面
├ Back               scale=10  ← 底板（在 movieContent **外面**）
└ jacketContent
  ├ Right / Bot / top / Left / Top   ← 封面模式的五块拼贴（各带 Jacket/Mask/Gaku）
```

- `Back` 用的是 `live/2dmode/background/default/default.png`：**2048×2048、全不透明、
  深蓝黑底**（实测 alpha 全 255，中心色 `(0,9,47)`）。它在 MV **下面**，不是覆盖层。
- `Back` / `Jacket` 的 `SpriteRenderer.m_Sprite` 全是 `null`（运行时由代码赋值），
  所以 asset 里查不到"那块纹理叫什么"。

**真正的那层"纹理"在视频里**。把 0036 的 MV 抽一帧出来看
（`build/mv20.png`）：画面本身就带**圆角 + 四周压暗的黑框**，是 MV 美术做的
"投影幕布"效果，不是引擎叠的。我们直接播原画就已经包含了它。

所以播放端**不需要额外叠任何层**。如果看上去和原作不一样，只可能是：

1. 我们 `pad` 到 640×360，而原视频 DAR 是 72:41（1.756），会多出约 4px 的左右黑边；
2. 原作把 MV 铺在 `Back` 之上时可能有自己的缩放/位置（`Movie` 的 scale=120 是相机
   空间的值，不是屏幕比例）。

---

## 1.8 我们（CppSekai）现在的状态

> **2026-11-17 更新：下面「零视频通路」那一段已经过时，保留作为决策记录。**
> 现在三条都做了：`platform/VideoPlayer.cpp`（子进程 ffmpeg 解 usm，进程边界 =
> 许可边界，不链 ffmpeg 库）、`Renderer::updateSongBackground`（`glTexSubImage2D`
> 到 MV 纹理 + 整帧适配 quad）、帧号按 `songTime + musicStartPos + offset` 换算
> （§1.5 的公式）。设置 > 演出 > MV = 2D MV 打开。

**intro 之后怎么出现（2026-11-17 定的规则）**：**硬切**，不渐变。

| 阶段 | 背景 | 依据 |
|---|---|---|
| 开头卡片期间 | 舞台底板（`stage.png`/封面模糊），**视频 alpha = 0** | 卡片不能漏 MV |
| 卡片结束的那一帧 | 视频 **alpha = 1**，底板直接被盖住 | 原作 MV 模式就是硬切 |
| `stage` quad（音符区/舞台前景） | 仍按 `openingPlayfieldVisibility` 淡入 1.8s | 它是前景，压在视频之上 |

之前把视频 alpha 直接绑在 `openingPlayfieldVisibility` 上，于是 MV 跟着那 1.8s
从紫底板里浮出来 —— 看起来就是「先一个紫色舞台背景，再慢慢变成视频」，是 bug 不是设计。
两个防退化条件（`main.cpp` 渲染循环里的 `mvReady`）：至少解出一帧（`mvShownFrame >= 0`）、
且解码器**追上过**时钟要的帧（`mvFrameCaughtUp`，粘性标记 —— 用实时 lag 判断的话，
MV 比谱面短时播完帧号就停了，歌尾会退回底板）。
`--screenshot` 会打 `[mv] shot frame=N lag=M`，用来区分「MV 这一帧本身是黑的」和
「纹理里根本没有帧」—— 截图看不出来。

---

### 当初的调研（保留）

只有 `game/StageBackground.cpp`（封面投到舞台屏的静态合成，上游 `bggen v3` 移植），
本质等价于原作的 `jacketModeRoot`。**零视频通路。**

最小可用三步：

| 步骤 | 做法 | 备注 |
|---|---|---|
| 1 解码 | ffmpeg（`libavformat/libavcodec`）解 usm → BGRA | usm = Sofdec2 容器（MPEG-2/AVC + 自定义头），ffmpeg 能读 |
| 2 上传 | 每帧 `glTexSubImage2D` 到 RGBA 纹理，画在背景 quad | `Renderer.cpp` 已有 quad 通路，加一条「外部纹理 quad」 |
| 3 同步 | 音频时钟为主钟，`frame = trunc(fps × songTime)` | 照抄 §1.5：截断 + clamp + `<1` 不播 |

### 体积账（2026-11-15 实测 + 估算）

`ffmpeg.exe` **能直接读 usm**（`-i` 就识别出来了）：

```
Input #0, usm, from '0036.usm':
  Stream #0:0: Video: mpeg1video, yuv420p, 576x328, 24 fps
```

只有视频流、没有音频（2D MV 的音乐走 BGM 那一路），所以解码链路很窄：
**usm demuxer + mpeg1video 解码 + swscale 的 YUV→RGB**，别的全不要。

| 方案 | 体积 | 说明 |
|---|---:|---|
| 本机 `ffmpeg.exe`（全量静态 win64） | **102 MB** | 所有编解码器/filter/协议都在里面，**不能这么进包** |
| 裁剪静态库（只留 usm + mpeg1/h264 解码） | **约 4~8 MB**（估） | `--disable-everything --enable-demuxer=usm,mov --enable-decoder=mpeg1video,h264 --enable-parser=mpegvideo,h264 --enable-protocol=file --enable-filter=scale`，strip 后 |
| 动态 DLL（avcodec/avformat/avutil/swscale） | 约 25~35 MB | 放 exe 旁边，不算进 exe 但一样占安装包 |
| 自己解 usm 容器 + 只链 avcodec | 约 3~5 MB（估） | usm 结构简单（CRID 头 + chunk），能省掉 avformat |

**会变大，但能压到 5 MB 级**：现在 exe 5.6 MB，接上后大约翻倍；
和 102 MB 的全量 ffmpeg 差二十倍。真正要操心的不是体积，是 **LGPL 许可**和
**Win7 兼容**（官方 build 的 CRT 依赖）。

---

# 第二部分：分数公式

## 2.1 总公式 + 综合力（关键）

`ScoreLogic.CalculateAddScore(INote, bool dropOut)`，VA `0x545E5B8`：

```asm
0x0545E984  fmul s1, s10, s11     ; s1 = s10 × s11
0x0545E988  fmul s1, s8, s1       ; s1 = s8  × (s10 × s11)
0x0545E98C  fmul s0, s1, s0       ; s0 = s8 × s10 × s11 × comboFactor
0x0545E994  ldr  s1, [x19, #0x38] ; dropOutFactor
0x0545E998  fmul s0, s0, s1       ; if (dropOut) × dropOutFactor
```

```
单音符得分 = 基础分 × 音符系数 × 判定系数 × 连击系数 [× dropOutFactor]
```

**基础分的分子就是综合力** —— `ScoreLogic.Setup`，VA `0x545CC08`–`0x545CD94`：

```asm
0x0545CC08  ldr  x8, [x22, #0x28]   ; bootData.DeckData            （LiveBootDataBase 0x28）
0x0545CC10  ldr  x0, [x22, #0x10]   ; bootData.MusicData           （0x10）
0x0545CC18  ldr  w22, [x8, #0x28]   ; ★ LiveDeckData.TotalPowerIncludeBuff（0x28）
0x0545CC20  bl   get_PlayLevel()
0x0545CC2C  lsl  w10, w22, #2       ; ★ TotalPower × 4
0x0545CC44  fmul s1, s2, s1         ; (playLevel - 5) × 0.005
0x0545CC48  fadd s8, s1, s0         ; levelFactor = 1 + (lv-5)×0.005
0x0545CC4C  scvtf s9, w10           ; (float)(TotalPower × 4)
0x0545CC74  fmul s8, s8, s9         ; levelFactor × TotalPower × 4
0x0545CC94  fcvtms w8, s8           ; 向下取整
0x0545CD94  fdiv s0, s1, s0         ; 除以 Σ音符权重
0x0545CD98  str  s0, [x20, #0xc0]   ; → BaseNoteScore
```

```
BaseNoteScore = floor( levelFactor × TotalPowerIncludeBuff × 4 ) / Σ(音符权重)
单音符得分    = BaseNoteScore × 权重 × 判定系数 × 连击系数
```

**所以：分数和卡组综合力（含 buff）是线性正相关。** 综合力翻倍，分数翻倍。

我们 `Judgement.cpp` 的写法：

```cpp
(kTeamPower / mWeightedNoteCount) * 4.0 * weight * levelFactor * comboFactor
```

和原作**数学等价**，唯一差别是 `kTeamPower = 250000` 是**写死的**，原作取真实
`DeckData.TotalPowerIncludeBuff`。也就是说我们的分数是「250000 综合力」的恒定模拟。

`0x545CBEC` 还顺带确认了自定义谱面：`bootData.IsCustomMusicScore`（0x71）为真时
`customMusicScoreFactor` 取 `0x626ac7c` 的返回值，否则 1.0。

## 2.2 音符系数：`ingameNotes`（15 行真值）

`[Key("ingameNotes")] Dictionary<int, MasterIngameNotes>`：

```csharp
int id; string ingameNoteType; float scoreCoefficient; int damageBad; int damageMiss;
```

| id | ingameNoteType | scoreCoefficient | damageBad | damageMiss |
|---:|---|---:|---:|---:|
| 1 | `normal` | **10** | 50 | 80 |
| 2 | `long`（长条头） | **10** | 50 | 80 |
| 3 | `long_mid`（中段 tick） | **1** | 40 | 40 |
| 4 | `flick` | **10** | 50 | 80 |
| 5 | `normal_critical` | **20** | 50 | 80 |
| 6 | `long_critical` | **20** | 50 | 80 |
| 7 | `long_mid_critical` | **2** | 50 | 80 |
| 8 | `flick_critical` | **30** | 50 | 80 |
| 9 | `long_combo_auto` | **1** | 40 | 40 |
| 10 | `friction` | **1** | 40 | 40 |
| 11 | `friction_long` | **1** | 40 | 40 |
| 12 | `friction_flick` | **10** | 50 | 80 |
| 13 | `friction_critical` | **2** | 50 | 80 |
| 14 | `friction_long_critical` | **2** | 50 | 80 |
| 15 | `friction_flick_critical` | **30** | 50 | 80 |

**和我们 `hudWeight()` 是 10:1 的关系，结构完全一致**（tap 1↔10、flick crit 3↔30、
tick 0.1↔1、friction 0.1↔1）。上游 web 版整体除 10 做归一化，最后靠分母抵消，结果等价。
唯一没对上的：`friction_long` 原作 1，我们没区分。

## 2.3 判定系数：`ingameNoteJudges`（7 行真值）

`[Key("ingameNoteJudges")]` → `SetupResultScoreFactor()` 灌进 `resultScoreFactor`：

| id | ingameNoteJadgeType（原文拼错，Jadge） | scoreCoefficient | damage |
|---:|---|---:|---:|
| 1 | `perfect` | **1.0** | 0 |
| 2 | `great` | **0.7** | 0 |
| 3 | `good` | **0.5** | 0 |
| 4 | `bad` | **0.0** | 50 |
| 5 | `miss` | **0.0** | 100 |
| 6 | `dead` | **0.7** | 0 |
| 7 | `auto` | **0.7** | 0 |

我们 `judgeMultiplier()` = 1.0 / 0.7 / 0.5 / 0 / 0 —— **判定档位全对**。

两点补充：
- `dead`（复活前/死亡态）和 `auto`（自动演奏）都是 **0.7**，不是 1.0。
  我们 `--auto` 给 1.0，是"预览满连"的取向，和原作不同（原作 auto 只有 GREAT 级分数）。
- `ingameNoteJudges.damage`（miss=100）和 `ingameNotes.damageMiss`（=80）**不一致**。
  真正参与扣血的是后者：`AddNoteDamageFactor(MasterIngameNotes, ...)` 建的是
  `resultDamage[category][type][result]`，源头是 `ingameNotes` 的 `damageBad/damageMiss`。
  `ingameNoteJudges.damage` 在 `ScoreLogic` 里没有被读到（可能是给别处/旧逻辑用）。

## 2.4 连击系数：`ingameCombos`（11 行真值）

`[Key("ingameCombos")] List<KeyValuePair<int,float>> ingameComboScoreFactors` ——
`CalculateAddScore` 里用二分查找（找不到默认 1.0）。

| fromCount | toCount | scoreCoefficient |
|---:|---:|---:|
| 1 | 100 | 1.00 |
| 101 | 200 | 1.01 |
| 201 | 300 | 1.02 |
| 301 | 400 | 1.03 |
| 401 | 500 | 1.04 |
| 501 | 600 | 1.05 |
| 601 | 700 | 1.06 |
| 701 | 800 | 1.07 |
| 801 | 900 | 1.08 |
| 901 | 1000 | 1.09 |
| 1001 | 1000000 | **1.10** |

**2026-11-15 更正**：上一版本说「原作是阈值表、我们不是」—— **结论错了**。
表展开就是「每 100 连 +1%，1001 连封顶 1.1」，**和我们的
`combo % 100 == 1 → +0.01，上限 1.1` 完全等价**。只是实现形式不同（查表 vs 递增）。

## 2.5 难度系数：`ingamePlayLevels`（40 行真值）

| playLevel | 1-5 | 6 | 7 | … | 40 |
|---|---:|---:|---:|---|---:|
| scoreCoefficient | 1.0 | 1.005 | 1.01 | +0.005/级 | **1.175** |

即 `1 + max(0, lv-5) × 0.005`，与 §2.1 asm 里那条 `1 + (lv-5)*0.005` 完全吻合。
（1~5 级也满足这个式子，因为负数被 `max(0,...)` 吃掉了 —— 表里 1~5 全写 1.0 只是冗余。）

我们 `(mChartRating - 5.0f) * 0.005f + 1.0f` ✅ 一字不差，但没有 `max(0,...)`，
等级 < 5 的谱面会算出 < 1.0 的系数。我们的谱面最低也是 EASY 1 级，实际影响可忽略，
但如果要严谨可以补一个 `max`。

## 2.6 扣血

`ScoreLogic.Damage(INote)` 查 `resultDamage[category][type][result]`，源头是
§2.2 的 `damageBad / damageMiss`：

| 音符类 | BAD | MISS |
|---|---:|---:|
| normal / long / flick（含 critical） | 50 | 80 |
| long_mid / friction / long_combo_auto | 40 | 40 |

我们 `kLifeMiss=80`、`kLifeBad=50`、`kHoldBreak=40` ✅。

## 2.7 等级分数线：`playLevelScores`（280 行，可压成公式）

`MasterData.playLevelScores : Dictionary<int, Dictionary<string, MasterPlayLevelScore>>`，
每行 `{liveType, playLevel, s, a, b, c}`。

**7 种 liveType**：`solo` / `multi_open` / `cheerful_carnival_open` /
`cheerful_carnival_private` / `rank_match` / `preliminary_tournament` / `challenge_live`。
每种 40 个等级。

### S / A / B 三档与 liveType 无关，只与等级有关

```
S = 1040000 × levelFactor
A =  840000 × levelFactor
B =  400000 × levelFactor
levelFactor = 1 + max(0, lv-5) × 0.005
```

（L1~5 = 1040000 / 840000 / 400000；L40 = 1222000 / 987000 / 470000，实测逐级 +5200/+4200/+2000。）

### C 档是三套基准

| liveType | C 基准 | L1 | L40 |
|---|---:|---:|---:|
| `challenge_live` | 4,000 | 4000 | 4700 |
| `solo` / `rank_match` | 20,000 | 20000 | 23500 |
| `multi_open` / `cheerful_carnival_*` / `preliminary_tournament` | 40,000 | 40000 | 47000 |

**这直接回答「等级判定和综合力有关吗」：无关。** 分数线只看 `liveType + playLevel`，
是一个固定表；但因为**分数本身**随综合力线性增长（§2.1），综合力高的卡组自然更容易够到 S。

顺带解释 `RESULT-SUMMARY.md` 那个坑：HUD 分条的满值就是这里的 S 线，且**要乘 levelFactor**，
不能直接拿 1000000 当满值。

## 2.8 我们 vs 原作

| 项 | 原作 | CppSekai | 处置 |
|---|---|---|---|
| 综合力 | `DeckData.TotalPowerIncludeBuff` 实时值 | `kTeamPower = 250000` 写死 | 想准就按卡组算；现在是等价模拟 |
| 音符权重 | `ingameNotes` 15 行 | `hudWeight()`，比例一致 ×1/10 | 加 `friction_long` 更严谨 |
| 判定系数 | 1.0/0.7/0.5/0/0（+dead/auto 0.7） | 1.0/0.7/0.5/0/0 | ✅（auto 给 1.0 是有意的） |
| 连击系数 | 每 100 连 +1%，封顶 1.1（查表） | 同，递增实现 | ✅ **等价** |
| 难度系数 | `1 + max(0,lv-5)×0.005` | `1 + (lv-5)×0.005` | 差一个 `max`，实际无影响 |
| 扣血 | 50/80 与 40/40 | 同 | ✅ |
| 等级线 | `基础值 × levelFactor`，C 分三档 | prefab 刻度反推 | 现在有真值了，可以照抄 |
| 2D MV | CriMana 播 usm | **无** | 见 §1.8 |

---

# 第三部分：资源站总表（谱面 / BGM / 曲绘 / 2D MV）

| 资源 | 站 | URL 模板 |
|---|---|---|
| 谱面（SUS/json） | unipjsk | `https://assets.unipjsk.com/startapp/music/music_score/{id4}_01/{diff}` |
| BGM | unipjsk | `https://assets.unipjsk.com/ondemand/music/long/{asset}/{asset}.mp3` |
| 曲绘 | unipjsk | `https://assets.unipjsk.com/startapp/music/jacket/{stem}/{stem}.png` |
| 国服独占（id ≥ 11000） | sekai.best | `https://storage.sekai.best/sekai-cn-assets/...`（同 unipjsk 布局） |
| **2D MV（usm）** | **exmeaning** | `https://storage.exmeaning.com/sekai-jp-assets/live/2dmode/original_mv/{id4}/{id4}.usm` |
| **2D MV 构建数据** | exmeaning | `.../{id4}/moviebundlebuilddata.json` |
| 2D 无 MV 兜底背景 | exmeaning | `.../live/2dmode/background/default/default.png` |
| master 各表 | sekai-world | `https://sekai-world.github.io/sekai-master-db-diff/{表名}.json` |

master 表名与 `MasterData` 字段的对应关系（**Key 名不总是等于字段名**，踩过）：

| `MasterData` 字段 | JSON 文件名 |
|---|---|
| `ingameNotes` | `ingameNotes.json` |
| `ingameNoteJudges` | `ingameNoteJudges.json` |
| `ingameComboScoreFactors` | **`ingameCombos.json`** |
| `ingamePlayLevels` | `ingamePlayLevels.json` |
| `playLevelScores` | `playLevelScores.json` |
| `ingameJudgeFrames` | `ingameJudgeFrames.json`（判定窗口真值，见 JUDGE-WINDOWS.md） |
| `musicDifficulties` | `musicDifficulties.json` |

---

## 4. 复现

```bash
cd /d/Tools/rea/work/jp
python q.py ScoreLogic Background2DView MovieManager LiveDeckData    # 干净类定义
python q.py -f ScoreLogic                                           # 带 RVA 原文

# 反汇编（capstone 装在托管 venv，见 RE.md §4.1b）
"C:/Users/Miku/.workbuddy/binaries/python/envs/default/Scripts/python.exe" \
  /d/Dev/CppSekai/.workbuddy/tools/il2cpp_disasm.py \
  D:/Tools/rea/work/jp/libil2cpp_arm64.so --range 0x50ACC08 0x50AD05C

# 反查调用目标属于哪个方法（这次靠它找到 ingameCombos）
"C:/Users/Miku/.workbuddy/binaries/python/envs/default/Scripts/python.exe" \
  /d/Dev/CppSekai/.workbuddy/tools/il2cpp_where.py D:/Tools/rea/work/jp/dump/dump.cs 0x62af0fc
```

读 `.rodata` 浮点常量（`adrp` 注释里的地址就是文件偏移）：

```python
import struct
f = open(r'D:/Tools/rea/work/jp/libil2cpp_arm64.so','rb')
f.seek(0x1F242F0); print(struct.unpack('<f', f.read(4)))   # → 0.005
```

## 5. 产物索引

| 想要什么 | 在哪 |
|---|---|
| 音符 / 判定 / 连击 / 难度 / 分数线 master | `D:/Dev/CppSekai/build/ingame*.json`、`playLevelScores.json` |
| 有 2D MV 的 186 个 id | `D:/Dev/CppSekai/build/2dmv_ids.txt` |
| 2D 兜底背景 | `D:/Dev/CppSekai/build/2d_default.png` |
| 类定义提取器 | `D:/Tools/rea/work/jp/q.py` |
| 2D MV 状态机 asm | VA `0x50ACC08` |
| 分数公式 asm | VA `0x545E5B8`（CalculateAddScore）、`0x545CAA4`（Setup） |
| 综合力那四行 asm | VA `0x545CC08`–`0x545CC74` |
