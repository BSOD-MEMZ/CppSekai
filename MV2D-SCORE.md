# 演出中的 2D MV 与分数公式（2026-11-15，REA 逆向）

> 免责声明同仓库其他文档：本文是**自己玩、自己看**的对照记录，素材与数据权利归原权利人，
> 不随代码分发、不对外发布。
>
> 证据基座：日服 7.1.0 `dump.cs`（TypeDefIndex / RVA 都在本文里）+ `libil2cpp_arm64.so` 反汇编
> + master `ingameNotes.json`（15 行的音符系数真值）。复现命令见 §4。

---

## 第一部分：演出时怎么放 2D MV

### 1.1 三种演出模式，2D MV 只是其中一档

`LiveSettingData.LiveModeType`（TypeDefIndex 6039）—— 这是「演出设置」里的那个下拉：

| 值 | 名字 | 实际内容 |
|---|---|---|
| 0 | `High3D` | 3D MV，高画质 |
| 1 | `Default3D` | 3D MV，普通画质 |
| 2 | `Mode2D` | **2D MV** |
| 3 | `Low` | 省电（低配 3D） |
| 4 | `OriginalMV` | 原版 MV（3D，带 original 编排） |

注意 `MusicCategory`（original / another / sekai 等）是**另一根轴**，它决定「同一首歌唱哪版 MV」，
`LiveModeType` 决定「用 2D 视频还是 3D 模型演」。两者组合才唯一确定背景。

### 1.2 `Background2DView`：一台机器，两个子模式

`Sekai.Live.Background2DView`（TypeDefIndex 11920，prefab id 待查）字段：

```
movieCamera        相机（专门渲染 MV 层）
jacketCamera       相机（专门渲染封面层）
jacketModeRoot     ← 没有 2D MV 时走这条：封面
movieModeRoot      ← 有 2D MV 时走这条：视频
backgroundRenderer 底图
movieRenderer      MV 的 SpriteRenderer（CriMana 输出打在这上面）
jackets[]          封面层（原作是多张并排/叠放）
movieAssetBundleName
```

方法签名把生命周期说得很清楚：

| 方法 | 做什么 |
|---|---|
| `Setup(BaseLiveController)` | 拿控制器 |
| `OnLoad()` | `SetupJacket()` + `SetupMovie(musicData, category)` |
| `SetupMovie(LiveMusicData, MusicCategory)` | async，返回 `MovieLoadResult{Success, Aborted, Failed}` |
| `MusicStart(float time)` | 音乐起点 → `SeekAndPlayMovieAsync(startMusicTimeMs)` |
| `Pause/Resume/Retry/OnFailure` | 跟着演出时钟走 |

**所以「2D 模式」不是「把封面当背景」**：有 2D MV 资源就播视频，没有才退回 `jacketModeRoot`。
两个 root 各配一台相机，是为了让 MV 层和封面层能独立控制分辨率/色彩（和 `FrontMusicVideoView`
那套截图/分享功能共用）。

### 1.3 播放器是 CriWare Sofdec2（CriMana），不是 Unity VideoPlayer

硬证据（`MovieBasePlayer`，TypeDefIndex 10607）：

```
protected CriManaMovieController criManaMovieController;
protected MovieData data;
protected List<int> entryCountList;
public const int MOVIE_MAX_ENTRIES = 3;      // 最多 3 路并发
protected void SetMovieFile(string movieFile, Player.SetMode mode);
```

`MovieManager : CriCore<MovieManager>`（TypeDefIndex 10604）是全局单例，**按 key 管多路**：

| 侧 | 方法 |
|---|---|
| 准备 | `PreLoad(key, gameObject, shader)` → `MovieLoadResult`；`Prefetch(bundleName)`；`IsMoviePreloaded(key)` |
| 控制 | `SetSeekPosition(key, frameNumber)` / `Play(key, loop)` / `Pause` / `Resume` / `Stop` / `WaitEnd` |
| 查询 | `GetMovieInfo(key)` / `GetFrameInfo(key)` / `Status(key)` |
| 释放 | `Free(key, owner, controller)` / `ForceFree` / `StopFree` / `FreeAll` |

`RenderTarget` 在 `MovieController` 里是一个 `GameObject`（CriMana 往它的 MeshRenderer + material 输出）。
`MovieData` 只有一个字段：

```csharp
public class MovieData { private List<string> <FilePathDatas>; }   // 一个 key = 多个 usm 文件
```

**多段拼接**是原作的常态：一首 2D MV 被打包成若干个 `.usm`，播放器按 `FilePathDatas` 顺序连着放
（`FrameInfo.cntConcatenatedMovie` 就是当前处在第几段）。

另外 `MovieManager` 里有 `Save(int index, long totalbyte, string usmFileName, string outputFilePath, ...)`
和 `movie_data_storage_path` —— **usm 是从 assetbundle 里解出来、落到本地缓存目录再交给 CriMana 的**
（CriMana 只吃文件路径，不认 bundle 内存流）。这也是 MV 要单独下载的原因。

### 1.4 2D MV 的数据描述：`MusicVideo2DModeData` + seek 表

```
MusicVideo2DModeData : ScriptableObject        (TypeDefIndex 15075)
  int id
  string name
  List<BundleTimeLineMovieData> movieDatas     (15076)
        string movieName
        List<BundleSeekMovieData> seekMovieDatas  (15077)
              int frameNo
```

`GetMusicVideo2DModeDataBundleName(int musicId)`（`AssetBundleName` 一族，VA `0x6221D54`）拼出
bundle 名，字符串表里有现成的路径模板：

```
live_pv/mv_data/{0}
```

`seekMovieDatas` 的作用是**把「乐曲时间」映射到「视频帧」**：每个 `movieName` 有一个基准帧号，
音乐跳到某个时间点时，先按帧号找到该落在哪一段、再 `SetSeekPosition` 到段内帧。
这就是为什么它是一张表而不是一个字段 —— 一首歌可以有多个切入版本（完整版 / 游戏用剪短版 /
Another 编排版），各版本起播帧不同。

### 1.5 时间同步：ms → 帧的换算（反汇编硬证据）

`Background2DView.<SeekAndPlayMovieAsync>d__14.MoveNext`，VA `0x50ACC08`：

```asm
0x050ACCAC  ldr  s0, [x0, #0x2c]      ; x0 = FrameInfo → framerateN（CriWare 的千分单位，60000 = 60fps）
0x050ACCB0  ldr  x9, [x19, #0x18]     ; startMusicTimeMs
0x050ACCB4  mov  w8, #0x447a0000      ; 1000.0f
0x050ACCBC  ucvtf s0, s0
0x050ACCC0  scvtf s2, x9
0x050ACCC8  fdiv s0, s0, s1           ; s0 = framerateN / 1000
0x050ACCCC  fdiv s1, s2, s1           ; s1 = ms / 1000（秒）
0x050ACCD4  fmul s0, s0, s1           ; s0 = fps × 秒
0x050ACCD8  fcvtzs w8, s0             ; 截断取整（不是四舍五入）
0x050ACCEC  ldr  w9, [x0, #0x54]      ; totalFramesPerFile
0x050ACCF4  cmp  x10, x9
0x050ACCF8  csel w8, w9, w8, gt       ; clamp 到末帧
0x050ACCFC  cmp  w8, #1
0x050ACD00  b.lt ...                  ; 算出来 < 1 帧就整个不播
```

写成公式：

```
frame = trunc( framerateN / 1000 * startMusicTimeMs / 1000 )
frame = clamp(frame, 0, totalFramesPerFile)
frame < 1  →  不播
```

要点三条：
1. **`framerateN` 是千分单位**（30000 / 60000），不是 30 / 60；
2. **截断**，不是四舍五入 —— 差半帧靠音频对齐补，不靠取整；
3. **越界夹到末帧**，`<1` 直接不播（避免从第 0 帧开始对不上拍）。

### 1.6 3D MV 是另一套，别混

`MusicVideoData : ScriptableObject`（15078）里根本没有任何视频字段：

```
int id / string name
CharacterInfo[] characterInfos   ← 上场的角色、卡面、服装
StageInfo / CameraInfo / PostEffectInfo / CutinInfo
```

3D MV = 模型 + Timeline + Cinemachine 相机 + post effect，走 `Background3DView` /
`Background3DPlayer` / `IMusicVideo3DPlayer`，由 `MusicVideoPlayerFactory.CreateMain/CreateCutIn` 造。
**2D MV 唯一的「数据」就是那张 seek 表 + 一堆 usm。**

### 1.7 我们（CppSekai）现在的状态

我们只有 `game/StageBackground.cpp`：「封面投到舞台屏幕」的静态合成（上游 `bggen v3` 移植），
本质对应原作的 `jacketModeRoot`。**完全没有视频通路**。

落地一条最小可用的 2D MV，需要三件事（按依赖顺序）：

| 步骤 | 做法 | 备注 |
|---|---|---|
| 1. 解码 | ffmpeg（`libavcodec/libavformat`）解 usm→BGRA/YUV，或提前转 mp4 | usm 是 Sofdec2 容器（MPEG-2/AVC + 自定义头），ffmpeg 能读 |
| 2. 上传 | 每帧 `glTexSubImage2D` 到一张 RGBA 纹理，画在背景 quad 上 | 我们 `Renderer.cpp` 已有 quad 通路，加一条「外部纹理 quad」即可 |
| 3. 同步 | 音频时钟为主钟，`frame = trunc(fps * songTime)`，大于 1 帧差就 seek | 照抄 §1.5，**截断 + clamp + <1 不播** |

不需要学它那套「3 路 CriMana 并发」，我们是单机单背景。
真要支持「多段拼接」，把 `movieDatas` 那张 seek 表照结构实现就行（它本来就是为这个设计的）。

---

## 第二部分：分数公式

### 2.1 总公式

`ScoreLogic.CalculateAddScore(INote, bool dropOut)`，VA `0x545E5B8`，核心四行：

```asm
0x0545E984  fmul s1, s10, s11     ; s1 = s10 × s11
0x0545E988  fmul s1, s8, s1       ; s1 = s8  × (s10 × s11)
0x0545E98C  fmul s0, s1, s0       ; s0 = s8 × s10 × s11 × comboFactor
0x0545E994  ldr  s1, [x19, #0x38] ; dropOutFactor
0x0545E998  fmul s0, s0, s1       ; if (dropOut) × dropOutFactor
```

展开成可读形式：

```
单音符得分 = 基础分 × 音符系数(category,type) × 判定系数(result) × 连击系数(combo)
             [× dropOutFactor，仅掉线时]
```

`ScoreLogic` 的字段就是这套的四个查找表：

```csharp
Dictionary<NoteCategory, Dictionary<NoteType, float>>  noteScoreFactor;   // 音符系数
Dictionary<NoteResult, float>                          resultScoreFactor; // 判定系数
List<KeyValuePair<int, float>>                         comboScoreFactor;  // 连击系数（查表！）
MasterPlayLevelScore                                   scoreInfo;         // C/B/A/S 分数线
float BaseNoteScore;  float totalScoreF;  float dropOutFactor;
```

### 2.2 音符系数：master `ingameNotes` 全表（真值）

`SetupNoteScoreFactor()` 逐行读 master：

```csharp
class MasterIngameNotes {
    [Key("id")]              int id;
    [Key("ingameNoteType")]  string ingameNoteType;
    [Key("scoreCoefficient")] float scoreCoefficient;
    [Key("damageBad")]       int damageBad;
    [Key("damageMiss")]      int damageMiss;
}
```

日服 master 全 15 行（`ingameNotes.json`，已核对）：

| id | ingameNoteType | scoreCoefficient | damageBad | damageMiss |
|---:|---|---:|---:|---:|
| 1 | `normal` | **10** | 50 | 80 |
| 2 | `long`（长条头） | **10** | 50 | 80 |
| 3 | `long_mid`（长条中段 tick） | **1** | 40 | 40 |
| 4 | `flick` | **10** | 50 | 80 |
| 5 | `normal_critical` | **20** | 50 | 80 |
| 6 | `long_critical` | **20** | 50 | 80 |
| 7 | `long_mid_critical` | **2** | 50 | 80 |
| 8 | `flick_critical` | **30** | 50 | 80 |
| 9 | `long_combo_auto`（长条自动 combo） | **1** | 40 | 40 |
| 10 | `friction`（摩擦条） | **1** | 40 | 40 |
| 11 | `friction_long` | **1** | 40 | 40 |
| 12 | `friction_flick` | **10** | 50 | 80 |
| 13 | `friction_critical` | **2** | 50 | 80 |
| 14 | `friction_long_critical` | **2** | 50 | 80 |
| 15 | `friction_flick_critical` | **30** | 50 | 80 |

**这张表跟我们的 `hudWeight()` 是 10:1 的关系，结构完全一致**：

| 音符 | 原作 | 我们（`Judgement.hpp`） | 比值 |
|---|---:|---:|---|
| tap | 10 / 20（crit） | 1 / 2 | ×10 ✅ |
| flick | 10 / 30 | 1 / 3 | ×10 ✅ |
| hold tick | 1 / 2 | 0.1 / 0.2 | ×10 ✅ |
| friction | 1 / 2 | 0.1 / 0.2 | ×10 ✅ |
| hold 尾 | 10（复用 long） | 1 | ×10 ✅ |

也就是说**权重比例我们抄对了**，绝对尺度差 10 倍 —— 因为上游 web 版做了整体除以 10 的归一化，
最后再乘 `4 × teamPower / Σweight` 抵消回去。只要分母用同一套权重，结果等价。

唯一没对上的：`friction_long`（摩擦长条）原作 1，我们没区分，长条一律按 tap 权重算。
它只在带摩擦条的谱面里出现，影响很小。

### 2.3 判定系数

`SetupResultScoreFactor()`（VA `0x545D7B0`）往 `resultScoreFactor` 灌值。这段是
`Dictionary.Add` 的样板代码，逐条浮点常量被编译器优化成了 `fmov` 立即数，反汇编读不出干净的表。
沿用社区已确认且与 `CalculateAddScore` 的乘数个数一致的一组：

| 判定 | 系数 |
|---|---:|
| PERFECT | 1.0 |
| GREAT | 0.7 |
| GOOD | 0.5 |
| BAD | 0.0 |
| MISS | 0.0 |

（BAD/MISS 为 0 这点有旁证：`CalculateAddScore` 只在 `result` 有效时被调用，且 BAD/MISS 走
`Damage()` 而不是加分路径。）我们 `judgeMultiplier()` 就是这套，**这条一致**。

### 2.4 连击系数：查表，不是线性

`comboScoreFactor` 是 `List<KeyValuePair<int, float>>`，在 `Setup()` 里填，`CalculateAddScore`
里用二分查找（`bl 0x86d0804` 是 `List.BinarySearch` 的实例化）：

```asm
0x0545E934  b.ge ...              ; 找到 → 取 value
0x0545E960  fmov s0, #1.0         ; 找不到 → 1.0
0x0545E980  fmov s0, w8           ; 或直接转 int
```

**这跟我们的实现不一样**。我们是硬编码「每 100 combo +1%，上限 1.1」（`Judgement.cpp` 里
`if (mStats.combo % 100 == 1) mComboFactor = min(+0.01, 1.1)`）。原作是**一张阈值表**，
所以它的加成曲线可以是阶梯的、非等距的、还能有上限之外的特殊段。

⚠️ **表内容没取到**：填充逻辑在 `ScoreLogic.Setup` 的 `0x545CC0C` 之后，中间夹着十几层
`Dictionary/List` 的内部调用，没有静态数据可对。要拿准确值只能跑一次真机（见 §4.3）。

### 2.5 难度系数（这条我们是对的）

`Setup()` 里，VA `0x545CC20`–`0x545CC48`：

```asm
0x0545CC20  bl   #0x63E30FC       ; w0 = playLevel
0x0545CC28  ldr  s1, [x9, #0x2f0] ; = 0x1F242F0 → 0.005f（常量池实测）
0x0545CC34  sub  w8, w0, #5       ; playLevel - 5
0x0545CC3C  scvtf s2, w8
0x0545CC40  fmov s0, #1.0
0x0545CC44  fmul s1, s2, s1       ; (playLevel - 5) × 0.005
0x0545CC48  fadd s8, s1, s0       ; 1 + (playLevel - 5) × 0.005   ← levelFactor
```

```
levelFactor = 1 + (playLevel - 5) × 0.005
```

**这就是我们 `Judgement.cpp` 里的 `(mChartRating - 5.0f) * 0.005f + 1.0f`**，一个字都不差。
上游 web 版显然也是从同一处逆出来的。

顺带：`0x545CC94 fcvtzs / fcvtms` 说明基础分是**向下取整成整数**再进乘法的，我们全程 double
累加 —— 末位会有几十到几百分的差，属于可接受偏差，但结算分数线如果卡得很死要注意。

### 2.6 扣血（也已核对）

`MasterIngameNotes.damageBad / damageMiss` 就是 §2.2 表里那两列，`ScoreLogic.AddNoteDamageFactor`
按 `(NoteCategory, NoteType, NoteResult)` 建三层字典，`Damage()` 查表扣。

对照我们的常量：`kLifeMiss = 80`、`kLifeBad = 50`、`kHoldBreak = 40` —— 与
`normal/long/flick(50/80)` 和 `long_mid/friction(40/40)` 完全吻合。

### 2.7 结算分数线

`MasterPlayLevelScore`（3243）每行：

```
string liveType;  int playLevel;  int s;  int a;  int b;  int c;
```

`ScoreGaugeCalculator.Create(MasterPlayLevelScore)` 建条，`GetScoreRank(master, score)` 判级，
`CalculateGaugeProgress(score)` 给填充比例。另有 `RankRateS/A/B/C` 四个静态 float
（`0x0/0x4/0x8/0xC`）是**多人/CC 条**用的固定比例 —— 这就是为什么打歌 HUD 的刻度和结算页的
刻度不是一套（`RESULT-SUMMARY.md` 已经踩过这个坑）。

⚠️ `playLevelScore.json` 在公开 master 镜像里**没有**（`sekai-master-db-diff` 404、
`assets.unipjsk.com` 404、`storage.sekai.best` NoSuchKey）。所以分数线这张表我们暂时只能从
prefab 的刻度反推。要真值只能从游戏内抓 `/api/` 或本地 master 缓存里取。

---

## 3. 我们 vs 原作：一张对照表

| 项 | 原作 | CppSekai | 处置 |
|---|---|---|---|
| 2D MV | CriMana 播 usm，按帧 seek，多段拼接 | **无** | 要做就按 §1.7 三步 |
| 2D 模式的退路 | 无 MV → `jacketModeRoot` | 只有封面（等价于这条） | 保持 |
| 音符权重 | master 15 行系数表 | `hudWeight()`，比例一致 ×1/10 | 加 `friction_long` 更严谨，不做也不影响 |
| 判定系数 | 1.0 / 0.7 / 0.5 / 0 / 0 | 同 | ✅ |
| 连击系数 | **阈值表**（`List<KVP<int,float>>` 二分查找） | 每 100 连 +1%，封顶 1.1 | 想准就抓真机数据换表 |
| 难度系数 | `1 + (lv-5)×0.005` | 同 | ✅ |
| 扣血 | 50/80 与 40/40 | 50/80、长条 40 | ✅ |
| 分数线 | `MasterPlayLevelScore` 逐难度逐级 | prefab 刻度反推 | 缺真值 |

---

## 4. 复现

### 4.1 查类 / 反汇编

```bash
cd /d/Tools/rea/work/jp
python q.py ScoreLogic Background2DView MovieManager     # 我这次加的提取器（干净输出）
python q.py -f ScoreLogic                                # 带 RVA 的原文

# 反汇编（capstone 装进了托管 venv：
#   pip install --no-index build/tmpw/capstone-5.0.9-py3-none-win_amd64.whl）
"C:/Users/Miku/.workbuddy/binaries/python/envs/default/Scripts/python.exe" \
  /d/Dev/CppSekai/.workbuddy/tools/il2cpp_disasm.py \
  D:/Tools/rea/work/jp/libil2cpp_arm64.so --range 0x50ACC08 0x50AD05C
```

⚠️ `il2cpp_disasm.py` 的 so 路径是 **`libil2cpp_arm64.so`**（`jp/` 根下），
不是 RE.md 早期写的 `jp/lib/arm64-v8a/libil2cpp.so`。

### 4.2 读 `.rodata` 浮点常量

`adrp` 注释里的 `=0x1F242F0` 已经是文件偏移，直接按小端 float 读：

```python
import struct
f = open(r'D:/Tools/rea/work/jp/libil2cpp_arm64.so','rb')
f.seek(0x1F242F0); print(struct.unpack('<f', f.read(4)))   # → 0.005
```

### 4.3 还差的两块真值

1. **连击系数表**：`ScoreLogic.Setup` 里构建，只能真机 dump（`dump.cs` 只有签名）。
2. **`playLevelScore`**：公开镜像没有，要抓游戏内 master。

---

## 5. 产物索引

| 想要什么 | 在哪 |
|---|---|
| 音符系数全表 | `D:/Dev/CppSekai/build/ingameNotes.json`（这次下的） |
| 类定义提取器 | `D:/Tools/rea/work/jp/q.py`（`python q.py Name1 Name2`） |
| 2D MV 状态机 asm | VA `0x50ACC08`（`<SeekAndPlayMovieAsync>d__14.MoveNext`） |
| 分数公式 asm | VA `0x545E5B8`（`CalculateAddScore`）、`0x545CAA4`（`Setup`） |
