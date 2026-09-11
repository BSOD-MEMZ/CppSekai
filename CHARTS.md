# 下载谱面

CppSekai 的谱面**不随仓库分发**（官方游戏数据，版权原因）。你需要自己从公开资源站
[unipjsk](https://assets.unipjsk.com/) 拉下来，放到 `charts/` 目录里。

> ⚠️ 谱面 / BGM / 曲绘都是 Project SEKAI 官方素材，**仅限本地游玩**。
> 不要把 `charts/` 提交到公开仓库，也不要二次分发。

---

## 一、最快路径（推荐）

```bash
bash setup.sh --charts    # 下载 0075 / 0127 两首歌的全部 5 个难度 + BGM
bash build.sh             # 编译
```

`setup.sh` 还会顺带做两件事：

- 从官方 `musicDifficulties` 表生成 `music-levels.json`（**难度定数**，缺了它选曲界面
  的等级会显示 `-`）；仓库里已有一份，所以通常会被跳过。
- 已有的文件不会重复下载，可以反复执行。

跑完直接：

```bash
cd build
./cppsekai.exe            # 不带 --sus 就会进选曲界面
```

---

## 二、目录与命名约定（**必须按这个来**）

谱面都放在 `charts/` 下。程序按 **文件名** 认歌，命名错了就不会出现在列表里。

| 文件 | 命名 | 说明 |
|---|---|---|
| 谱面 | `charts/<id4>_<难度>.sus` | `id4` = 4 位补零曲目 id，如 `0075_master.sus` |
| BGM | `charts/<id4>.mp3` | 一首歌共用一个音频，所有难度共享 |
| 曲绘 | `charts/<id4>.png` | 也可以叫 `0075_master.png` / 名字里带 `jacket` |
| 元数据 | `charts/<id4>.json` | **全难度共用**，如 `0075.json` |
| 元数据（单难度） | `charts/<id4>_<难度>.json` | 优先级**高于**上面那个 |
| 定数表 | `music-levels.json`（仓库根） | 所有歌共用，`setup.sh` 自动生成 |

**难度名只有 5 个**，小写文件名即可，程序不区分大小写：

```
easy  normal  hard  expert  master
```

> `APPEND` / `ETERNAL` 谱面虽然能解析，但选曲界面只有 5 个格子，不会显示。

**曲目 id 从文件名里解析**，所以同一首歌的不同难度会自动合并成列表里的一条。
这也是为什么必须写成 `0075_master.sus` 而不是随便起名——名字里解析不出 id，
每个难度就会各占一条，散成一堆。

`charts/` 的搜索顺序（取第一个**有谱面的**）：

```
--charts <dir>   →   exe 同级 charts/   →   exe 上一级 charts/   →  当前工作目录 charts/
```

默认 `build/cppsekai.exe` 的上一级就是仓库根，所以直接把谱面放仓库根的 `charts/` 就行，
不用手动拷到 `build/`。

---

## 三、查曲目 id

仓库根的 `musics.json` 里有全部 715 首歌的官方元数据（`id` / `title` / `assetbundleName`）。
查某一首：

```bash
python -c "
import json,sys
q = '海底譚'          # 改成你想找的关键词
for m in json.load(open('musics.json', encoding='utf-8')):
    if q.lower() in m['title'].lower():
        print('%04d  %-20s %s' % (m['id'], m['assetbundleName'], m['title']))
"
```

输出里的 **4 位数字就是 `<id4>`**（例如 `0075`），`jacket_s_075` 里的 3 位数字是
曲绘用的 `<id3>`。

---

## 四、URL 模板（已实测可用）

### 1. 谱面

```
https://assets.unipjsk.com/startapp/music/music_score/<id4>_01/<难度>
```

注意：**末尾没有 `.sus` 扩展名**，下载后要自己改名成 `<id4>_<难度>.sus`。
`<难度>` 就是 `easy` / `normal` / `hard` / `expert` / `master`。

### 2. BGM

两个候选地址，按顺序试，第一个 404 就用第二个：

```
# ① 游戏内短版（大部分歌用这个）
https://assets.unipjsk.com/ondemand/music/long/se_<id4>_01/se_<id4>_01.mp3

# ② 原曲完整版（老歌只有这个，例如 0001 / 0002）
https://assets.unipjsk.com/ondemand/music/long/<id4>_01/<id4>_01.mp3
```

不是每首歌都有 BGM，两个都 404 就说明这个站没放，只能自己找音频放成 `charts/<id4>.mp3`。

### 3. 曲绘

```
https://assets.unipjsk.com/startapp/music/jacket/jacket_s_<id3>/jacket_s_<id3>.png
```

⚠️ **曲绘是 3 位补零**（`jacket_s_075`），而谱面和 BGM 是 **4 位**（`0075_01`）。
这是最容易踩的坑。

---

## 五、一次下完一首歌（复制即用）

把 `ID` 改成你要的曲目 id：

```bash
ID=0075
DIR=charts
mkdir -p "$DIR"

# 5 个难度的谱面
for d in easy normal hard expert master; do
    curl -s --max-time 60 -o "$DIR/${ID}_${d}.sus" \
        "https://assets.unipjsk.com/startapp/music/music_score/${ID}_01/$d"
done

# BGM：短版 → 原曲版 依次尝试
curl -sf --max-time 300 -o "$DIR/${ID}.mp3" \
    "https://assets.unipjsk.com/ondemand/music/long/se_${ID}_01/se_${ID}_01.mp3" \
 || curl -sf --max-time 300 -o "$DIR/${ID}.mp3" \
    "https://assets.unipjsk.com/ondemand/music/long/${ID}_01/${ID}_01.mp3"

# 曲绘：id 去掉一位前导零 → 3 位补零
ID3=$(printf "%03d" "$((10#$ID))")
curl -s --max-time 60 -o "$DIR/${ID}.png" \
    "https://assets.unipjsk.com/startapp/music/jacket/jacket_s_${ID3}/jacket_s_${ID3}.png"

ls -la "$DIR"
```

批量下载就外面再套一层 `for ID in 0075 0127 0200 0500; do ... done`。

---

## 六、元数据 sidecar（可选，但强烈建议）

没有 sidecar 时，列表里会显示文件名（`0075 master`）、定数显示 `-`、歌手栏空着。
写一个 `charts/<id4>.json` 就能全难度共用（比 `setup.sh` 拉的音频更省事）：

```json
{
  "title": "ウミユリ海底譚",
  "lyricist": "n-buna",
  "composer": "n-buna",
  "arranger": "n-buna",
  "vocal": "初音ミク",
  "fillerSec": 9.0
}
```

支持的字段：

| 字段 | 作用 |
|---|---|
| `title` | 曲名（列表 + 手机面板） |
| `artist` | 艺术家 |
| `lyricist` / `composer` / `arranger` | 作词 / 作曲 / 编曲 |
| `vocal` | 歌手，显示成 `Vo. 初音ミク` |
| `difficulty` / `level` | 难度名 / 定数（覆盖 `music-levels.json`） |
| `mv` | 显示 `2D MV` / `3D MV` 标签 |
| `fillerSec` | **BGM 开头的静音长度（秒）**，见下 |
| `offset` | 同上，但单位是**毫秒**（`fillerSec` 优先；两者只取一个） |

### `fillerSec` 是干嘛的

官服 mp3 不是从音乐第 0 秒开始的——前面有一段静音填充（大多数歌 ≈9 秒），谱面的
tick 0 落在静音**之后**。不对齐的话整个谱面会早/晚 9 秒，看起来就是"音画完全错位"。

优先级：sidecar 的 `fillerSec` / `offset` > **程序自动检测静音** > 0。

所以大多数情况下不写也能自动对上；只有自动检测失灵（BGM 本身就带前奏）时才需要手填。
命令行也能临时覆盖：

```bash
./cppsekai.exe --sus ../charts/0075_master.sus --bgm ../charts/0075.mp3 --filler 9.0
./cppsekai.exe --sus ../charts/0075_master.sus --bgm ../charts/0075.mp3 --offset 9000
```

---

## 七、难度定数（`music-levels.json`）

unipjsk 导出的 SUS 把 `#TITLE` 和 `#PLAYLEVEL` 清空了（`#DIFFICULTY 0`），所以定数
不能从谱面里读，只能查官方 `musicDifficulties` 表。`setup.sh` 会抓下来压缩成：

```json
{ "75": [6, 13, 17, 23, 28], "127": [9, 14, 19, 27, 32] }
```

顺序是 `[easy, normal, hard, expert, master]`。想手动重建：

```bash
rm music-levels.json && bash setup.sh
```

或者干脆在单难度 sidecar 里写 `"level": "32"` 覆盖它。

---

## 八、加完谱面之后

谱面目录**只在游戏启动时扫描一次**，没有运行时热重载。加完文件重启游戏即可。
启动日志里有扫描结果，对不上就看这里：

```
[select] 10 chart(s) under D:\Dev\CppSekai\build\..\charts
```

如果显示 `no .sus found. Looked in:` 后面跟着一串路径，说明命名不对或者放错目录了。

---

## 九、常见问题

| 现象 | 原因 / 处理 |
|---|---|
| 列表里没有这首歌 | 文件名必须是 `<4位id>_<难度>.sus`；确认放在 `charts/` 且重启了游戏 |
| 歌名显示成 `0075 master` | 缺 sidecar 的 `title` |
| 定数显示 `-` | `music-levels.json` 缺失或该 id 不在表里；也可以 sidecar 写 `"level"` |
| 同一首歌散成 5 条 | 文件名里解析不出曲目 id，命名改成 `0075_expert.sus` 这种 |
| 只有 5 个难度格 | 正常，只支持 easy～master |
| 完全没有声音 | 少了 `charts/<id4>.mp3`；或两个 BGM 地址都 404，需自备音频 |
| 音画错位约 9 秒 | sidecar 加 `"fillerSec": 9.0`（或 `"offset": 9000`） |
| 曲绘是紫色占位块 | `charts/<id4>.png` 没下到，或曲绘 URL 用错了位数（必须是 **3 位**） |
| 难度数字变了 | `music-levels.json` 是按官方表生成的，会随游戏版本更新 |
