# RE.md — pjsk 解包产物与工具链（2026-10-25）

> 这份文档记录**怎么把 pjsk 拆开、拆出来什么、东西在哪、踩过哪些坑**。
> 目的是下个 session 不用重新摸索：想查一个 UI 尺寸或一个类，照 §4 的 recipe 直接跑。
> 版权口径见 [COPYRIGHT.md](COPYRIGHT.md)：解出来的资产**只限本机研究**，
> 不进 `dist/`、不发 Release、不进公开仓库。

## 1. 结论：日服能拆，国服拆不动

| | 日服 `com.sega.pjsekai`（7.1.0） | 国服 `com.hermes.mk`（6.4.0） |
|---|---|---|
| `global-metadata.dat` | **128 字节重复 XOR**，一次成功 | 8 字节前缀 + 名字表 **AES** + RSA 包裹的 key |
| 需要的工具 | 一个 python 脚本 | 得破 `libhhld-rt.so`（Crypto++） |
| 结果 | `dump.cs` 82MB，15,072 个类型 | `dump.cs` 797KB 全是乱码 |
| 资源包 | 明文 Unity 序列化数据，AssetStudio/AssetRipper 都能吃 | `assets/data/` 是自家加密容器（无 UnityFS 头） |

**所以：要代码语义或 UI 几何，一律用日服包。** 国服那条线目前卡在 AES，没有继续投入。

## 2. 工具链与路径

| 工具 | 版本 / 位置 | 用途 |
|---|---|---|
| REA（`rea-agents`） | 6.7.0，MCP 服务名 `rea`；注册在 `~/.workbuddy/mcp.json` **和** `%APPDATA%\Code\User\mcp.json`（见 §2.1） | 统一入口（PE 的原生逆向；**Windows 上只收 PE，不收 ELF**） |
| Ghidra | 12.1.4 → `D:\Tools\ghidra_12.1.4_PUBLIC` | 原生代码（REA 只认 12.1.x，**别升 12.2**） |
| Temurin JDK | 21.0.12.1 → `D:\Tools\jdk-21.0.12.1+1` | Ghidra / JADX 运行时（要 full JDK） |
| JADX（headless MCP） | `D:\Tools\rea\android\jadx-headless-mcp-0.7.1-all.jar` | APK 的 Java 层（环境变量 `REA_JADX_MCP_JAR`） |
| Il2CppDumper | 6.7.46 → `D:\Tools\rea\work\tools\Il2CppDumper` | metadata + libil2cpp → `dump.cs` / `DummyDll` |
| AssetStudioModCLI | 0.19.0 → `D:\Tools\rea\work\tools\asmod\net8\AssetStudioModCLI_net8_portable` | 资源枚举 / sprite / mesh / texture 导出（用 `dotnet` 跑） |
| AssetRipper | 2.0.0 → `D:\Tools\rea\work\tools\arip\app\AssetRipper.GUI.Free.exe` | **读 prefab 的 RectTransform / 粒子参数**，本地 HTTP API |

工作目录：

```
D:\Tools\rea\work\
  jp\                 日服 7.1.0（正在用）
    dump\             dump.cs 82MB / il2cpp.h 238MB / script.json 249MB / DummyDll(159 个)
    GameData\         base + UnityDataAssetPack + config 合并后的 624MB（喂 AssetRipper）
    global-metadata-decrypted.dat
    libil2cpp_arm64.so
    as_out\ sprite-geometry.csv mesh_out\ tex_tap\ tex_sekai\ fx_touch\ ms_*.json/txt
    RE-report-jp.md   逆向报告（含触摸特效参数、选曲界面几何）
  pjsk_6.4.0\         国服 6.4.0（卡在 AES，只留证据）
  tools\
```

## 2.1 重装记录：工具链曾整个丢失（2026-10-26）

某天 `D:\Tools\rea` 整个目录不在了（只剩 `D:\Tools` 下别的软件），REA 也同时在
**所有**客户端里变成 `missing`。2026-10-26 重装了**引擎与注册**（JDK / Ghidra / JADX
+ 两个客户端的 MCP 配置），但**上面的 `work\` 产物树没有恢复** —— `dump.cs`、`GameData\`、
`fx_touch\` 那些要按 §3 重新解一遍才有。顺带记下这次绕网络的两条硬事实
（GitHub release 的 CDN 在这台机器上直连会被 reset）：

| 包 | 源 | 校验 |
|---|---|---|
| JDK 21.0.12.1+1 | 清华镜像 `mirrors.tuna.tsinghua.edu.cn/Adoptium/21/jdk/x64/windows/` | SHA256 对上 Adoptium API 的 `binary.package.checksum` |
| Ghidra 12.1.4 | GitHub release → `https://gh-proxy.com/<github url>` | 尺寸 569,649,598 字节（官方没发 sha256） |
| jadx-headless-mcp 0.7.1 | 同上代理 | 与 release 里的 `jadx-headless-mcp.sha256` 一致 |

- **代理单连接只有 100~400KB/s**，543MB 的 Ghidra 单线程要 1.5 小时。用**多连接分段**
  （`.workbuddy/tools/seg_download.js`：`curl -r` 切 16MB 段、6~10 并发、拼装前校验总尺寸；<br>
  用法 `node .workbuddy/tools/seg_download.js <url> <out> <totalBytes> [并发] [段MB]`）。
- 分段时**必须给 `curl --max-filesize <段长+1>`**：gh-proxy 偶尔忽略 `Range` 直接回整包
  （200），不加限制的话会把 543MB 整个下进某个 16MB 的段里再丢掉（实测出现过 391MB 的半成品）。
- 注册与引擎是两件事：`rea setup --client vscode --client workbuddy --yes` 只写
  MCP 配置；引擎靠注册里的 `env`（`JAVA_HOME` / `GHIDRA_INSTALL_DIR` / `REA_JADX_MCP_JAR`）。
  验证：`doctor --client vscode --format json --filter-output providerInspections`，
  期望 ghidra `available:true providerVersion "12.1.4"`、jadx `configured:true`。
- IDA（`REA_IDA_MCP_CONFIG`）与 Kuna（`REA_KUNA_COMMAND`）仍是有意不配（都要用户自备）。

### 重新解包（2026-10-26 / 27，同一天做完）

用用户手动下的 `%USERPROFILE%\Downloads\プロジェクトセカイ+カラフルステージ！+feat.+初音ミク_7.1.0_APKPure.xapk`
（455MB）把 `work\jp\` 重新跑了一遍，**结果与上一轮一致**，可以直接用：

| 产物 | 大小 | 备注 |
|---|---|---|
| `splits/*.apk` | base 87.2 / config 64.2 / UnityDataAssetPack 303.1 MB | `tar -xf/x` 就够了（见下） |
| `assets/.../global-metadata.dat` | 27.0 MB | 密文，**需要 XOR** |
| `lib/arm64-v8a/libil2cpp.so` | 189.9 MB | |
| `global-metadata-decrypted.dat` | 27.0 MB | key anchor **0x1F23B7C**、metadata **version 31** |
| `dump/dump.cs` | 81.1 MB / 225 万行 / 3.7 万条类型声明 | |
| `dump/DummyDll/` | 159 个 dll / 46.1 MB | 含 `Assembly-CSharp.dll` |
| `dump/il2cpp.h` / `script.json` / `stringliteral.json` | 237.3 / 248.2 / 2.9 MB | script.json 给 IDA/Ghidra |

两条实测修正：

- **不需要 `jp_pipeline.py`**：`tar -tf/-xf` 直接读 XAPK 与 APK（bsdtar 认 zip），
  `tar -xf <xapk> -C splits` 拆出三个 split，再
  `tar -xf splits/com.sega.pjsekai.apk -C . "assets/bin/Data/Managed/Metadata/global-metadata.dat"`、
  `tar -xf splits/config.arm64_v8a.apk -C . "lib/arm64-v8a/libil2cpp.so"`。
- **反混淆脚本已入库**：`.workbuddy/tools/pjsk_deobf_metadata.py`（不硬编码偏移：
  明文前 4 字节 `AF1BB1FA` 反推 key[0:4] → 在 `.so` 里找锚点 → 用 version 与头部
  offset 自洽性选唯一解）。本次 3 个候选锚点里只有 `0x1F23B7C` 通过校验。
- **Il2CppDumper 的两行警告是正常的**：`WARNING: find JNI_OnLoad` /
  `ERROR: This file may be protected.` 之后它会自己 `Searching...` 找到
  `CodeRegistration` / `MetadataRegistration`，190MB 的 so 要几分钟。

工具按 RE.md 的路径重装并**逐一验过**（都是 `gh-proxy.com` 下的）：

| 工具 | 版本 | 验证 |
|---|---|---|
| Il2CppDumper | 6.7.46（`Il2CppDumper-win-v6.7.46.zip`，自包含，不用装 .NET） | 产出上表全部文件 |
| AssetStudioModCLI | 0.19.0（`net8_portable`） | `-m info` 读 `UnityDataAssetPack.apk`：**7033 文件 / 194422 资产**，Sprite 2807、Mesh 39、Unity 2022.3.62f2 |
| AssetRipper | 2.0.0（`AssetRipper_win_x64.zip`） | 解压在 `work/tools/arip/`（`AssetRipper.GUI.Free.exe` + `capstone.dll`），本次未跑 |

工作区占用：`work\jp` 1.28 GB、`work\tools` 0.21 GB。

**还没重建的只有 `GameData\`**（base + UnityDataAssetPack + config 合并成一份喂
AssetRipper，约 624MB，做法见 §4.2 之前的步骤）。要读 prefab 的 RectTransform /
粒子参数时先补这一步，否则 §4.2 那几个 `ms_geom.py` 跑不起来。

> 2026-10-27 已补：三个 apk 依次 `tar -xf ... -C GameData` 叠在一起即可
> （8848 文件 / 672MB）。**但必须把解密后的 metadata 覆盖进去**
> （`cp global-metadata-decrypted.dat GameData/assets/bin/Data/Managed/Metadata/global-metadata.dat`），
> 否则 AssetRipper 的 Cpp2IL 报 `Invalid or corrupt metadata (magic number check failed)`、
> 退回 `Unknown` scripting backend（MonoBehaviour 字段就解析不了）。覆盖后实测：
> `Cpp2IL : Using actual IL2CPP Metadata version 31.1`、`codereg 0xB0AEFC0 / metareg 0xB548B38`
> （与 Il2CppDumper 报的完全一致）、`Processed 212684 OK`、application model 19s 建好。
> `POST /LoadFolder` **会阻塞几分钟且可能直接超时**，超时不等于失败 —— 看服务端日志判断。

**AssetRipper 验证**：`Search/View?q=MusicSelect` 返回 196 个资产链接，
名字含 `MusicSelect` / `MusicSelectListView` / `ScreenLayerMusicSelectBackground` /
`MultiLiveMusicSelectPlayerCell`，即 §4.2 那条几何导出路径可用。

## 3. 从 XAPK 到 dump.cs（已经跑通过的流程）

```bash
# 0) 前置：APKPure 下日服 XAPK（我这边的出口连不上 APKPure 的文件 CDN，一律让用户手动下）
#    放到 D:\Downloads，文件名带 pjsk 即可。XAPK 内含 3 个 split：
#    base.apk（metadata）/ UnityDataAssetPack.apk（资源）/ config.arm64_v8a.apk（libil2cpp.so）

# 1) 一条龙（找包 → 拆 split → 抽 metadata+so → XOR 反混淆 → 检查资源包）
cd /d/Tools/rea/work
python jp_pipeline.py            # 自动在 Downloads 里找 pjsk 的 xapk/apkm

# 2) 出 dump.cs（工具输出落在工具自己的 cwd，不是最后一个参数）
cd /d/Tools/rea/work/tools/Il2CppDumper
./Il2CppDumper.exe <metadata.dat> <libil2cpp.so> <输出目录>
```

实测结果：metadata 27.0MB，**XOR key @ `libil2cpp_arm64.so + 0x1F23B7C`**，
锚点命中 3 个、校验通过 1 个；Il2CppDumper 一次 `Done!`，产出 `dump.cs` 82MB。

## 4. 常用 recipe

### 4.1 查一个类 / 方法的字段与签名

```bash
cd /d/Tools/rea/work/jp
python - <<'PY'
import io, re
lines = io.open('dump/dump.cs', encoding='utf-8', errors='replace').read().split('\n')
name = 'TouchEffect'
for i, ln in enumerate(lines):
    if re.match(r'^public class %s\b' % name, ln):
        b = [ln]; d = ln.count('{') - ln.count('}')
        j = i + 1
        while j < len(lines) and d > 0:
            b.append(lines[j]); d += lines[j].count('{') - lines[j].count('}'); j += 1
        print('\n'.join(x for x in b if not re.search(r'public \S+ (get|set)_', x)))
        break
PY
```

现成索引：`jp_index.py` 会把 dump.cs 切成 `ui-SekaiUI.txt` / `ui-ScreenLayer.txt` /
`ui-Tween.txt` / `tween-defs.txt`（按命名空间与关键字过滤）。

### 4.2 读一个 prefab 的完整 RectTransform 树（**几何对齐就靠它**）

```bash
cd /d/Tools/rea/work/jp

# 1) 起 AssetRipper（必须常驻，后台跑；否则进程随命令结束被带走）
/d/Tools/rea/work/tools/arip/app/AssetRipper.GUI.Free.exe --headless --port 17654 &

# 2) 加载 GameData（POST form 字段名是小写 path；返回 302 就是受理了，加载是异步的）
curl -X POST "http://127.0.0.1:17654/LoadFolder" --data-urlencode "path=D:/Tools/rea/work/jp/GameData"

# 3) 搜名字（结果里的 /Assets/View?Path=... 链接就是资产句柄）
curl -s "http://127.0.0.1:17654/Search/View?q=MusicSelect" | grep -o '/Assets/View?Path=[^"]*' | head

# 4) 导出整个 prefab 的几何树（txt 给人看，json 给脚本用）
python ms_geom.py 1121 ms_row      # MusicSelectSingleListCell
python ms_geom.py 702  ms_freelive # ScreenLayerFreeLiveMusicSelect
```

`ms_geom.py` 输出每行：`名字  anchoredPosition  sizeDelta  anchorMin-anchorMax`，
并按 `m_Father` 还原成缩进树。**`anchoredPosition` 只当参考**（选曲是代码排版），
尺寸和层级才可信 —— 理由见 [SELECT-UI.md](SELECT-UI.md) §2。

### 4.3 读粒子系统参数（触摸特效就是这么来的）

```bash
# fx_touch/ 里已经有 75 个 ParticleSystem + Renderer 的 YAML，直接看
cd /d/Tools/rea/work/jp/fx_touch && ls | head
python ../parse_ps.py            # 把 startSize / startSpeed / UVModule / ColorModule 解成表
```

分组方法见 `jp_fx_group.py`：prefab View 页的 `GameObjects[]` 是**深度优先**顺序，
按各 fx 节点的下标切区间就能把 75 个粒子系统归到 `fx_Tap_single` / `Follow` / `fx_Tap_flick`。

### 4.4 枚举资源 / 导 sprite、mesh、贴图

```bash
cd /d/Tools/rea/work/tools/asmod/net8/AssetStudioModCLI_net8_portable
DOTNET="C:/Program Files/dotnet/dotnet.exe"

# 统计
$DOTNET AssetStudioModCLI.dll "D:/Tools/rea/work/jp/UnityDataAssetPack.apk" -m info \
  --assembly-folder "D:/Tools/rea/work/jp/dump/DummyDll" --unity-version 2022.3.62f2

# 导 sprite（输出是 .txt，里面带 m_Rect / m_Pivot / m_PixelsToUnits）
$DOTNET AssetStudioModCLI.dll <apk> -m dump -t sprite,textAsset -o <目录> \
  --assembly-folder "D:/Tools/rea/work/jp/dump/DummyDll" --unity-version 2022.3.62f2

# 导贴图（按名字过滤）
$DOTNET AssetStudioModCLI.dll <apk> -m export -t tex2d --filter-by-name "tap" -o <目录> \
  --unity-version 2022.3.62f2
```

## 5. 坑清单（都是踩过的，别再踩）

### APK / metadata
- **APKMirror 没有日服** `com.sega.pjsekai`（它那个 "Sekai" 是别的 App）；APKPure 有 XAPK 直链，
  但我这边出口是受限代理（`28.0.0.x` / `2001:2::x`），**下载一律让用户手点**。
- XAPK 必须拆 **3 个 split**：metadata 在 base、`libil2cpp.so` 在 `config.arm64_v8a`、
  资源在 `UnityDataAssetPack`。少一个都没法继续。
- 国服 metadata 头多了 **8 字节**（`56BEF089C25389F0`），剥掉后 offset 8 才是 `FAB11BAF`；
  **日服没有这个前缀**（直接用）。
- 日服的保护是 **128 字节重复 XOR**，key 在 `.so` 的 `.rodata` 里。定位方法不依赖固定签名：
  明文前 4 字节必是 `AF1BB1FA` → 反推 `key[0:4]` → 在 `.so` 里锚点搜索 → 用 version + 头部
  offset 自洽性二次校验（`deobf_metadata.py`）。

### Il2CppDumper
- 输出**落在工具自己的 cwd**，不看最后一个参数；跑完要先 `mv` 或者去工具目录找。
- 会顺手生成 `DummyDll/`（159 个 dll，含 `Assembly-CSharp.dll`）—— **这是 AssetStudio
  解析 MonoBehaviour 字段的钥匙**，别删。
- 日志里 `find .init_proc` / `Some errors in dumping` 在今天这两包上都没出现（日服一次通过）。

### AssetStudioModCLI
- 用 `dotnet` 跑（`net9_win64` 版要 .NET 9，本机只有 8 → 用 `net8_portable`）。
- dump 输出是 **`.txt` 不是 `.json`**（里面其实是 JSON 文本）。
- `--filter-by-name` **只匹配资产名**，而 MonoBehaviour 的资产名不是 GameObject 名 →
  按"选曲界面"这种屏幕名搜不到 prefab。要找 GameObject 得上 AssetRipper。

### AssetRipper
- **必须用 `127.0.0.1`**，`localhost` 会解析到 IPv6 连不上。
- 进程要常驻（后台任务），否则随命令结束被带走。
- `POST /LoadFolder` 的 form 字段是**小写 `path`**（不是 `Path`）；返回 302 = 受理。
- 首页一直显示 "Welcome" 不代表没加载，看 `/Bundles/View?Path={"P":[]}`。
- **prefab 自身**路径是 `{"C":{"B":{"P":[0]},"I":0},"D":<prefabPathId>}`，
  但里面的 GameObject 在**另一个 collection**（如 `I:3250` / `I:4345`）。
  正确做法：抓页面里第一条 `GameObjects[]` 链接，取它 `,"D":` 之前的部分当前缀。
- GO 路径**不能**用 `/Assets/Yaml`（404），只有组件能；要拿 GO 的信息走 `/Assets/View`。

### Unity 序列化语义（这几条最容易把结论带偏）
- **`MinMaxCurve` 两种模式**：`minMaxState: 2`（Two Curves）的 `scalar` 是**两条曲线共用的
  乘数**，`minScalar` 不参与；`minMaxState: 3`（Two Constants）才是 `scalar`=上限、
  `minScalar`=下限。混了会把范围算错一个数量级。
- **Gradient**：只有前 `m_NumColorKeys` 个 key 的 RGB 是颜色（走 `ctimeN` 轴），
  前 `m_NumAlphaKeys` 个的 A 是 alpha（走**另一条** `atimeN` 轴），其余 key 的 rgb 是占位黑。
- **颜色是线性空间**，当 sRGB 用会得到近黑色块。
- 纹理序列帧模块序列化成 **`UVModule`**，不叫 `TextureSheetAnimationModule`。
- prefab 里的 `anchoredPosition` 常是占位值（运行时代码排版）。

### 环境
- 含中文/日文的文件名经 bash 传给 python 会乱码 → 用 python `glob` 定位再传。
- Git Bash 的 `tar` 不支持 zip → 用 python `zipfile`。
- python 里拼含 `\n` 的 printf/字符串：`\\n` 会变成真换行（Bash 工具是 JSON），
  用 `chr(92)+'n'` 或直接用 Edit 工具改源码。

## 6. 产物索引（要用什么直接去拿）

| 想要什么 | 去哪 |
|---|---|
| 类名 / 字段 / 方法签名 | `jp/dump/dump.cs`（82MB，`// Namespace:` 分段） |
| 函数的 native 地址（喂 IDA/Ghidra） | `jp/dump/script.json` |
| MonoBehaviour 字段解析 | `jp/dump/DummyDll/`（给 AssetStudio / AssetRipper 当 `--assembly-folder`） |
| 全部 sprite 的尺寸/pivot | `jp/sprite-geometry.csv`（2807 行） |
| 单个 sprite 的完整 dump | `jp/as_out/*.txt` |
| mesh 几何 | `jp/mesh_out/*.txt`（`sekai_PT_05` 是触摸特效的三角） |
| 触摸特效贴图 | `jp/tex_tap/`、`jp/tex_sekai/` |
| 粒子参数（YAML 原文） | `jp/fx_touch/*.yml` + `manifest.json` |
| 选曲界面几何 | `jp/ms_freelive.*`、`ms_info.*`、`ms_row.*`、`ms_diffbtn.*`、`ms_bg.*` |
| 结论汇总 | `jp/RE-report-jp.md`（§8 触摸特效参数、§11 选曲界面几何） |
| 对项目的落地规格 | [SELECT-UI.md](SELECT-UI.md)（选曲）、AGENTS.md「触摸特效」一节 |

## 7. 还没做的

- 国服那道 AES（`libhhld-rt.so` 里的 Crypto++ + `assets/hhld_info.json` 的 RSA 包 key）：
  没继续，因为日服已经够用。
- `decode_android_resources`（APK 资源解码）需要 apktool 的 launcher，REA 不代装。
- Live2D 模型（`.moc3` / `model3.json`）**不在 APK 里**，是运行时下载的 bundle。
- ACB 音频（`BGM_TITLE*` / `MenuCommon` / `TitleCall`）还没解：要用 vgmstream（CriWare HCA）。
