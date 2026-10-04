# CppSekai 猴子测试报告

日期：2026-10-04 ｜ 用例 **240 个** ｜ 提交基线：`1fd6227`

## 怎么跑的

三条输入通道，因为玩家能手动弄坏的东西就这三类：

| 组 | 做法 | 用例数 |
|---|---|---|
| `cli` | 命令行参数：越界数值、非数值、缺值、互相矛盾 | 104 |
| `file` | `--sus` / `--bgm` / `--cover` / `--charts` 指向不存在的、目录、错误类型的文件；谱面内容灌垃圾、截断、BOM、超长行、异常 BPM | 23 |
| `data` | 程序自己写但玩家能编辑的文件：`userdata.json`、`profiles/*.json`、四个数据表、`chartdl.json` | 31 |
| `ui` | `--fake-pad` 随机按键序列（它推的是真 SDL 按键事件，**能真的驱动 UI**），8 个界面 × 20 种序列 | 72 |
| `exit` | 不带 `--screenshot`，跑几秒后用 `taskkill /PID`（不带 `/F`）发 WM_CLOSE，走正常退出路径 | 4 |

**数据是隔离的**：沙箱在 `%TEMP%\cppsekai_monkey\`，是一份独立的 exe + assets + charts 副本。
`userDataPath()` 判定 `<exe>/../charts` 不存在时会回落成 `<exe>/userdata.json`，
所以沙箱的数据文件全在沙箱内生成 —— **仓库根的 `profiles/` 全程没被碰过**（已确认）。
每个用例前会把沙箱的数据文件复位，`charts/` 只在被改过的下一次复位。

工具：`.workbuddy/tools/monkey_test.py`（`--list` 看用例、`--only <子串>` 单跑、`--report <路径>` 换输出）。

## 先看这一栏：谁能碰到

下面的 P0/P1/P2 **是按"触发之后的后果"排的，不是按"碰到的概率"排的** ——
这两件事得分开看，否则会以为到处是雷。实际的触发条件：

| 编号 | 谁能触发 | 正常玩会不会碰到 |
|---|---|---|
| 1 `--ui-scale nan` 崩 | 命令行手打 `--ui-scale nan` | **不会**（双击没有参数入口） |
| 2 profile id 穿越 | 手改 `profiles/index.json` 的 `id` | **不会**（游戏生成的 id 是白名单，昵称塞 `../` 没用；`--activate-profile` 也会拒绝不在列表里的 id） |
| 3 `--speed` 越界 | 命令行传 >12 | **不会** |
| 4 字段类型错静默丢设置 | 改 `userdata.json`；或**将来某版本改了字段类型**（升级路径，这才是真风险） | 正常不会，升级时可能 |
| 5 NaN 家族 | 命令行传 `nan`/`inf` | **不会** |
| 6 缺值开关吞下一个开关 | 命令行漏写值 | **不会** |
| 7 `--settings-tab ≥ 6` | 命令行传 ≥6 | **不会** |
| 8 坏谱面静默放空舞台 | **下载器取到的谱面缺失/截断**、自己放进去的文件损坏 | **会** —— 这是全表里唯一日常能撞上的 |
| 9 窗口尺寸不校验 | 命令行传 0/负数 | **不会** |
| 10 导入只查键不查类型 | 手动挑一个 `{"settings": 5}` 之类的文件导入 | 极少 |

结论：**绝大多数条目是"自己手打命令行 / 自己改文件"型的**，真正的玩家暴露只有第 8 条
（第三方镜像下下来的谱面截断 → 静默空舞台，没有任何提示）和第 4 条的升级路径。
修不修可以按"改动成本 vs 万一撞上"来取舍，不必因为标了 P0 就觉得必须马上动。

---

---

# 一、真 bug

## 1. `--ui-scale nan` 直接访问违例崩溃 ★ 本次最严重

```
cppsekai.exe --no-party --ui-scale nan --screenshot x.png --screenshot-time 1.5
→ exit 0xC0000005（访问违例），0.8 秒死
```

日志停在窗口刚建好、选曲界面第一次布局之前：

```
[boot] ready (first frame up)   627.0 ms
[window] window procedure subclassed for drag frames
<崩溃，之后一行都没有>
```

**根因**：`std::clamp` / `std::min` / `std::max` **都不消 NaN** —— 它们的实现是
`v < lo ? lo : (hi < v ? hi : v)`，NaN 参与的比较全是 false，于是原样返回 NaN。
而 `atof("nan")` 正好给得出 NaN：

- `main.cpp:1140` `uiScaleArg = atof(...)`
- `main.cpp:1691` `userSettings.uiScale = std::clamp(uiScaleArg, 0.5f, 2.0f);` ← NaN 穿过去了
- `game/SongSelect.cpp:2588` `const float scale = std::clamp(uiScale, 0.5f, 2.0f);` ← 又一次
- `game/SongSelect.cpp:2590` `const float k = kBase * scale;` ← **k = NaN**

`k` 是 1080p 画布的 px-per-unit，NaN 顺着它进到选曲界面每一个尺寸/坐标/字号里。
具体崩在哪一行没定位到（没有调试器），但 `--result-preview` 和 `--settings` 也一样崩，
说明不只选曲界面一处。

注意：`--ui-scale 0 / -1 / 1e9 / abc` **都不崩**（0/abc → 0，被 clamp 兜住），
**只有 NaN 会**。也就是说这不是"范围校验缺失"，是"NaN 校验缺失"。

## 2. profile 的 `id` 不校验，可以写到数据目录外面

`game/SongSelect.cpp:801-804` 的 `profileDataPath()` 就是纯字符串拼接：

```cpp
return fromFsPath(toFsPath(dataDir) / "profiles" / (id + ".json"));
```

而 `id` 是从玩家可编辑的 `profiles/index.json` 里读出来的，**没有任何校验**
（`SongSelect.cpp:846` `user.id = row.value("id", std::string{})`）。

实测（`.workbuddy/tools/monkey_test.py --only traversal`）：

```json
{"active":"../../escaped","users":[{"id":"../../escaped","name":"X"}]}
```

关窗时程序**写出了 `<dataDir>/../escaped.json`** —— 已经出了 data 目录。
`userdata.json` 本来就在 dataDir，多几个 `../` 就能读到/覆盖仓库根甚至更远的文件。
这条要靠玩家自己改 index.json 才能触发，不是远程漏洞，但它是"外部输入直接当路径用"
这一类里最典型的，而且**读和写都走这条路**。

---

# 二、逻辑漏洞（不崩，但行为是错的）

## 3. `--speed` 越界 → 音符全部不可见（判定却照跑）

`--speed <1-12>` 是自己写在 usage 里的范围，但**没有夹取**
（`main.cpp:1204` `noteSpeed = atof(...)`）。上游核心
`core/native/src/mmw_preview.cpp:730`：

```cpp
return static_cast<float>(lerpD(0.35, 4.0, std::pow(unlerpD(12.0, 1.0, noteSpeed), 1.31)));
```

`speed > 12` 时 `unlerpD` 给负数，`pow(负数, 1.31)` = **NaN** → 音符时长 NaN → 位置 NaN。

实测 `--speed 999`：演奏区**一个音符都没有**（见证据图 `sp_999.png`），
但 COMBO 正常涨到 9、分数 13787 —— 判定用的是时间不是位置，所以完全正常，
玩家只会觉得"这谱面怎么空的"。

修复点在我们这侧（上游那段别动）：接受 speed 的地方夹到 [1, 12]。
顺带 `--speed 0 / -5` 不崩也不错乱，但同样是范围外的值被静默接受。

## 4. 一个字段类型写错 → 后面所有设置静默回退（且会永久化）

`loadUserData` 用的是 `s.value("field", 默认值)`，**nlohmann 在类型不匹配时会抛
`type_error`**，而外层是：

```cpp
} catch (...) {
    // malformed file: keep the defaults
}
```

全吞、**一行日志都没有**。抛出点在哪个字段，**那个字段之后的全部设置都不再加载**。

实测 —— profile 内容：

```json
{"settings": {"noteSpeed": "abc", "bgmVolume": 0.25, "padRumble": 0.7, "offsetSec": 0.25}}
```

打开设置 → 演奏页，显示的是 **音频偏移 `+0 ms`、BGM 音量 `100%`**
（写的明明是 `0.25` 秒和 `25%`）—— 排在坏字段后面的三个全被丢了（证据图 `typed.png`）。
而且下次保存会把默认值写回去，**丢失就永久化了**。

## 5. NaN 家族：`atof` 能进 NaN，而 clamp/max/min 都不消它

除了第 1 条崩掉的那个，下面这些 **NaN 全被静默接受**，行为未定义但都不崩：

| 参数 | 入口 | 下游 |
|---|---|---|
| `--offset nan / inf / -inf` | `main.cpp:1135` | `audio.setUserOffset()` |
| `--lead-in nan` | `main.cpp:2580` `std::max(leadIn, 5.8)` ← NaN 照样穿过去 | 输出时钟 |
| `--player-exp nan` | `main.cpp:1273` `std::clamp(atof(...), 0, 1)` | 等级经验条 |
| `--filler nan` | `main.cpp:1138` | 音频起始位置 |
| `--se-volume nan` | `main.cpp:1207` | 音效增益 |

建议：在**接受参数的那一处**统一做 `if (!std::isfinite(v)) v = 默认值;`，
比在每个消费者里防要省事得多。

## 6. 缺值的开关会吞掉下一个开关 → headless 跑挂死

所有参数都有 `i + 1 < utf8Argc` 保护（**这点写得好，不会越界读 argv**），
但**没有"这个开关需要一个值"的报错**，于是直接吃掉下一个 token：

```
--sus --screenshot x.png --screenshot-time 1.5
→ susPath = "--screenshot"，screenshotPath 空 → 永不自动退出
→ 实测挂死（25 秒被 kill）
```

`--sus` 后面跟任何开关都会这样。玩家手滑漏一个值，得到的不是"参数错误"，
而是一个**看起来卡住**的程序（headless 场景是真挂死）。

## 7. `--settings-tab ≥ 6` → 一张空白卡片

分支链最后一个是 `} else if (tab == 5) {`（`main.cpp:4589`），**没有兜底 else**。
`main.cpp:3292` `settingsTab = settingsTabShot >= 0 ? settingsTabShot : 0` 也不夹上限，
于是 `--settings-tab 99` 画出一张只有标题栏的空白卡片。

截图尺寸是间接证据：正常页 719 KB，`--settings-tab 99` / `2147483647` 都是 **672 KB**。

游戏内切页签走 `% kSettingsTabCount` 是安全的，所以这条只有 CLI 能碰到。

## 8. 谱面不校验格式，而且处理不一致 ✅ 已修（commit 01f8e2f）

`--sus` 指一个 **PNG、随机二进制、截断的 SUS、只有 BOM 的文件** → **exit 0**，
演奏区全空（这几张截图尺寸一模一样：767,373 字节）。

但 `--sus` 指一个**空文件**或 **512 个 NUL** → **exit 1**。

同样是"解析不出音符"，一个静默放个空舞台、一个直接退出，标准不统一。
另外 `cannot read chart: <路径>` 这句错误只写进 `cppsekai.log`，
**stdout / stderr 是空的**（GUI 子系统进程，重定向也抓不到），
双击启动时玩家看不到任何提示。

**修法**：`startSession` 里音符数为 0 直接算加载失败（这是"文件不可用"的第三种
情形，前两种本来就有报错）；三个玩家能碰到的入口统一走 `reportLoadFailure()`，
在屏幕下方显示一条深色提示条（`#44466` 底白字，300 帧后淡出）。
命令行路径也从 "exit 0 + 空舞台" 变成 "exit 1 + 日志里一行 `chart has no notes`"。
验证：正常谱面仍是 1040 个事件、照常可玩；坏文件被拒。

## 9. 窗口 / 渲染尺寸完全不校验

| 参数 | 结果 |
|---|---|
| `--width 0` / `-100` / `abc` | **exit 0**，截图 222 KB（基本是空窗） |
| `--height 0` | exit 0，截图 294 KB |
| `--height 999999` | exit 0，截图 **7.1 MB** |
| `--render-size 1x1` | exit 0，截图 55 KB（渲染成 1 像素再拉满窗口） |
| `--render-size 0x0` / `99999x1` / `-4x-4` | exit 0 |

`SDL_CreateWindow` 是有 null 检查的（`main.cpp:1850`），所以不崩；
但"0 宽窗口"这种退化状态是静默接受的。

## 10. 导入只查"键在不在"，不查类型

`game/SongSelect.cpp:1129-1141`：

```cpp
return doc.is_object() && (doc.contains("settings") || doc.contains("scores"));
```

`{"settings": 5}` 完全能通过这道筛查 → 弹确认框 → 覆盖当前 profile →
`loadUserData` 发现 `settings` 不是对象、跳过 → **全部设置回默认，静默**。
好消息是**纯垃圾文件会被挡住**（我先以为没有筛查、差点报错，读代码后否掉了）。

---

# 三、确认没问题的地方

免得看报告以为到处是洞，这些是我专门验过、**结果是对的**：

- **参数解析的越界保护是完整的**：每个取值开关都有 `i + 1 < utf8Argc`，
  缺值最多是被忽略，不会越界读 argv。
- **72 个手柄猴子用例零异常**：没有 ImGui 断言、没有卡死、每一帧都出了图；
  `--fake-pad` 的 8 个界面 × 20 种按键序列全部正常退出。
- **脏数据文件基本都能扛**：`userdata.json` 灌垃圾 / `null` / 数组 / `{}` / 错形状，
  `profiles/index.json` 垃圾 / 空 users / active 指向不存在的 id / 重复 id / 10 万字符昵称，
  四个数据表和 `chartdl.json` 灌垃圾 —— **全部 exit 0、正常出图、无报错**。
- `uiScale` 从文件读时夹 0.7–1.5、使用时再夹 0.5–2.0，是双保险
  （只有 NaN 能穿过，见第 1 条）。
- 负数 `--lead-in` 有夹取（`main.cpp:2580`）；`--player-rank` 有 `std::max(1, ...)`。
- 导入前有 `isUserDataFile()` 筛查，**纯垃圾文件不会毁掉 profile**。
- 多人游玩相关参数（`--party --no-party` 同时给、`--party-auto 99/abc/-1`）都能正常收敛。

---

# 四、建议的修复顺序

| 优先级 | 事项 | 改动量 |
|---|---|---|
| P0 | NaN 统一消毒（第 1、5 条）：在参数入口对每个 double/float 做 `isfinite` 检查 | 一个 helper + 十来处调用 |
| P0 | profile id 白名单化（第 2 条）：只接受 `[A-Za-z0-9_-]`，非法就丢弃该条 profile | 一个校验函数 + 两处调用 |
| P1 | `noteSpeed` 夹到 [1,12]（第 3 条） | 一行 |
| P1 | `loadUserData` 改成逐字段 try 或先查类型，失败**至少要打一行日志**（第 4 条） | 中等，值得做 |
| P2 | 缺值开关报错 / 退出（第 6 条） | 一个 else 分支 |
| P2 | `--settings-tab` 夹到 [0,5] + 兜底 else（第 7 条） | 两行 |
| P3 | 谱面格式校验 + 统一失败策略（第 8 条） | 需要先定策略 |
| P3 | 窗口尺寸下限校验（第 9 条） | 几行 |

## 原始数据

- 全量报告：`%TEMP%\cppsekai_monkey\report-main.md`（233 个用例的完整表格）
- 复跑：`python .workbuddy/tools/monkey_test.py`（约 12 分钟，后台跑）
- 单跑一条：`python .workbuddy/tools/monkey_test.py --only traversal`
