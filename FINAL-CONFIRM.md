# FINAL-CONFIRM.md — 最终确认页的实测规格（2026-11-22）

> 对应代码：`game/SongSelect.cpp` → `drawFinalConfirm()`。
> 这一页是**照 prefab 逐节点重画**的，不是照着截图比划的。下面每个数字都能在
> dump 里 grep 到；改这里之前先按 §1 把 dump 打开对一遍。
>
> 样张：[`docs/final-confirm-1920.png`](docs/final-confirm-1920.png)（1920×1080）、
> [`docs/page-transition.png`](docs/page-transition.png)（转场逐帧：原页 → 淡出 40% →
> 全白 → 淡入 60% → 完成，960×540 抓的）。

## 1. 数据来源（日服 7.1.0）

| 文件（`D:\Tools\rea\work\jp\`） | 内容 |
|---|---|
| `final703.txt` | prefab **703** `ScreenLayerFreeLiveFinalConfirmation` 的节点树：绝对矩形（1920×1080 Unity 坐标）+ `sprite=` / `m_Type` / `m_Color` / `m_FontSize` |
| `final_geo.txt` | 同一棵树，但给的是 `pos / size / anchor / pivot / scale`（像素推导的原始字段） |
| `final_full.json` | 同上 + **每个节点的 `m_IsActive`** + 全部组件 YAML（`scripts[]`） |
| `as_out/<sprite>.txt` | 精灵的 `m_Rect` / `m_Border` —— **九宫格切边** |
| `phone704.txt` / `ms_phone_final.txt` | prefab **704** `ScreenLayerFreeLiveCellPhone`（右侧那台手机） |
| `export_pc/Scripts/Assembly-CSharp/Sekai/*.cs` | AssetRipper 导出的 C# **只有签名、没有函数体**，能看字段与调用关系，看不到实现 |

重建 dump 的脚本在工作目录里（`deep_dump.py` → `deep_rects.py` / `deep_layout.py`，
`m_IsActive` 用 `arip.py` 的 `Arip(703).yaml(go_pid)` 逐节点取）。
**注意 `FinalConfirmationView` / `ScreenLayerFreeLiveFinalConfirmation` 的 C# 体是空的**，
所以"某个节点运行时开不开"只能靠 `m_IsActive` + 字段名推断，别指望读代码。

### ⚠ `m_Border` 是切边、不是圆角，顺序是 左/下/右/上

Unity `Sprite.m_Border` 的序列化顺序是 **X=左、Y=下、Z=右、W=上**。上一版把它当圆角半径，
`btn_round_h80_wh` 的 `(51,0,51,0)` 被写成 `(28,12)`，白色胶囊两端就被压扁成椭圆 ——
这一页"形状被切得乱七八糟"一半来自这里。

| sprite | 源尺寸 | m_Border (L/B/R/T) | 用在哪 |
|---|---|---|---|
| `bg_base_three_r16_wh` | 36×36 | 17/17/17/17 | 卡组面板 |
| `bg_base_round_h56_wh` | 66×56 | 33/28/33/28 | 队伍选择条、AUTO 轨道 |
| `bg_base_half_r16_wh` | 42×26 | 21/1/21/25 | 「当前队伍」页签（上圆角/下直角） |
| `btn_round_h80_wh` | 106×96 | 51/0/51/0 | 自动编成、MV 模式钮 |
| `eff_btn_round_h80_a70_wh` | 112×104 | 56/52/56/52 | 悬停光晕（prefab 里 `m_IsActive=0`） |
| `bg_base_circle_h96_wh` | 96×96 | 48/0/48/0 | 空槽里那颗圆（Simple 用） |

## 2. 显示出来的东西（1920×1080 绝对矩形）

```
DeckListView/DeckView
  DeckBase            70.4, 256    1080×624   bg_base_three_r16_wh  #DFDFEA a80%
  DeckCardList        容器 0 宽，x 中心 609.6，y 346..856（HorizontalLayoutGroup
                      m_ChildAlignment=4 MiddleCenter、**m_Spacing=7.66**）
    5 × 200×510       x0 = 609.6 − 1030.64/2 = 94.28，步距 207.66
    Empty/Frame       纯色 #B0BDC8（sprite `base_wh` 在包里**缺失**，Unity 退化成方角纯色）
    Empty/Icon        72×72  bg_base_circle_h96_wh  #919FAF  居中
    Empty/Plus        36×36  icon_plus_h36_wh       #B0BDC8  居中
  DeckName            70.4, 200.7  375.7×55.4 bg_base_half_r16_wh  #DFDFEA a80%
    CustomText        32px #444466，左边缘 x=91.4，垂直中心 228.4
  TotalPower
    Label/Tittle      「総合力」32px #444466，矩形 73.3..207.5
    IconTotal         38×42 @ 221.4,278.8   icon_deckPower_dbl
    Value             280×32 右对齐到 x=430.2，32px #555577
    info              72×72 @ 441.8,263.5   btn_circle56_wh
      CustomImage     8×30  @ 473.8,284.6   icon_infomation（烘焙深色，别再自己画 "i"）
DeckSelector/DeckSelectList
  Base                70.4, 127.6  720.1×56 bg_base_round_h56_wh  #BCBCD0 a90%
  Arrow 左/右          Image 80×108 @ 6.8,101.6 / 774.1,101.6
                      **右箭头 Image 的 rotation 是 (0,1,0,0) = 绕 Y 180°，同一张图镜像**
AutoEditButton        862.3, 119.7 296×72  btn_round_h80_wh，文字 28px #444466
BottomLeft/UIPartsLiveModeButton  57, 962  176×72（同一张白胶囊），文字 28px
BottomLeft/AutoLiveToggle
  bg_auto             180×110（prefab 的 sizeDelta.x 是 **0**，Image 根本画不出来 →
                      位置/宽度由代码给，所以 932.4 那个 y 不能当真值），
                      "AUTO" 烤在图上、70% 白烤在图里
  ToggleRoot          822.6, 1014.4  152×56
    Bg                bg_base_round_h56_wh：On #FF77AA / Off #444466
    Toggle            72×72 toggle_btn_wh（贴图缺失 → 用同尺寸 btn_circle_h56_wh）
                      On 中心 (946.1,1042.4)、Off 中心 (850.6,1042.4) —— 比轨道 ±47.5
    TextOn/TextOff    29px 白，框中心 872.6 / 923.8，垂直中心 1041.9（**在旋钮对面**）
  ConsecutiveAutoLiveSetting  72×72 @ 862.6,1006.4（btn_circle56_wh + 32×32 icon_setting）
                      这颗"小齿轮"prefab 摆在 AUTO 的**正中**（和开关同一个点），
                      两者不可能同时可见 —— 我们的摆法见 §3.1
```

### 3.1 AUTO 那一坨到底怎么摆（2026-11-22 第二轮）

三个数字互相打架，得先把它们分开看：

1. `AutoLiveToggle.sizeDelta.x = 0` —— 节点只有 110 高、宽度没有。Image 是
   Sliced（border 119/0/51/0，**左 119 装 "AUTO" 三个字母、右 51 是右圆头**），
   这种切法就是给"横向拉伸"用的，所以宽度一定是代码给的（`SetNativeSize` 之类）。
   既然宽度是运行时给的，`y=932.4` 也不是能直接用的渲染位置。
2. `ToggleRoot` 挂在节点的**左下角**（anchor 0,0 / pos 0,0），所以轨道永远在
   节点的竖直中线下方、水平居中：822.6..974.6 / 1014.4..1070.4。
3. 小齿轮 `ConsecutiveAutoLiveSetting` 72×72 的中心是 (898.6, 1042.4) —— **和轨道中心
   是同一个点**。两者只能二选一（原作用 `consecutiveAutoLiveSettingRoot` 在
   `SetConsecutiveAutoLiveView()` 里切）。

所以我们的摆法：
- 卡片 **180×110，底边对齐轨道底边** → 960.4..1070.4（上 54px 放 "AUTO"、下 56px 放开关）。
  照 932.4 画的话卡片和开关会叠成"两个互相错开的胶囊"，这就是之前"明显不对"的来源。
- 小齿轮放卡片右侧 8px、竖直对齐开关中心：**996.6..1068.6 / 1006.4..1078.4**，
  点它 = 直接打开设置卡（不走原作那个"連続オートライブ設定"）。


## 3. 明确**不**画的东西（每条都有 `m_IsActive=0` 或代码开关的依据）

| 节点 | 依据 |
|---|---|
| `BG` / `BGUnder` | `m_IsActive=0`。BG 是 1546×555 的**不透明纯白**，而且是 DeckBase 的后一个兄弟（Unity 里会盖住面板），只在面板左边露 14px、上边露 48px —— 像一张切坏的白框，原作这一页它就是关的 |
| `BottomRight`（決定 354.5×116 + 齿轮） | `m_IsActive=0`。开始键是手机上的 `SoloLiveFinalConfirmButton` |
| `DeckInfo`（阵型缎带 / 5 个 DanceFormationCell / 変更 钮） | `m_IsActive=0` |
| `TotalPower/WithEventLimit` | 活动加成那条红牌；`WithoutEventLimit` 才是常显那条，代码二选一 |
| `DeckCard/Exist`、`Leader`、`Selected`、`Lock` | 真实卡组数据（暂时忽略）；后三个本身 `m_IsActive=0` |
| `OpenBondRankDialogButton` | 角色羁绊按钮（用户要求先不画） |
| `OriginalMemberButton` | 原始成员 MV（同上） |
| `ConsecutiveAutoLiveRoot` | 连续自动演出，只有订阅状态才由 `SetConsecutiveAutoLiveView()` 打开 |
| `SwapMember`（「メンバー入替」勾选框） | prefab 里 `m_IsActive=1`，但勾选框在 931.6..986.6、标签 200×32 从 1001 排到 **1201**，末端 51px 落在手机底下（手机左缘 1144.9），原作不可能这么摆；而且它属于卡组编成模式（`IsMemberSwapMode`），这页不做编成 |

## 4. 右侧手机（prefab 704）

`CellPhone` 的 anchoredPosition 是 **(562, −202)**、尺寸 754.3×1416.7、绕中心 **+3°**
（`TransformList/DefaultPosition`，全游戏只有一个位置，**最终页和选曲页一样**）。
换算到 1920 画布：x 1144.85..1899.15、y 33.65..1450.35。

页面内关键零件（手机局部坐标，乘 `754.3/754.3` 的比例）：

| 零件 | 局部矩形 |
|---|---|
| 曲绘 | 376×376 @ 186.5, 92.5（方角，只有 ~1.9px 边缘渐隐，见 SELECT-CELLPHONE.md） |
| 标题 / 歌手 / Vo. | 左边缘 81.7，垂直中心 513.9 / 557.0 / 594.9 |
| 难度牌 | 144×40 @ 303.0, 637.3（圆角 = 半高） |
| **开始钮** | `SoloLiveFinalConfirmButton/PlayButton` 160×160，中心 (377.1, 812.9)，贴图 `btn_play_gn` |
| 底部两格 | `CommonButtonContent`：702×100 的条在 (24.45, 898.55)，格子 80×80，**中心 y = 948.55**；两格中心 **281.45 / 469.45**（步距 188） |

### 4.1 换页时手机面板哪些东西是**不动**的

标题 / 歌手 / Vo. 三行（左边缘 81.7、垂直中心 513.9 / 557.0 / 594.9）和底部那两格
**完全不动**，只有左格那颗图标从 `btn_random_lgr`（随机）换成 `btn_settingLive_lgr`
（齿轮 + 音符 = live settings）。**这一页不会多出第三颗按钮。**

依据是 `CommonButtonContent.SetupButton`(RVA 0x62459D4)：

```
randomButton.SetActive(info.ShouldShowRandomButton);   // 最终确认页 = false
settingButton.SetActive(info.EnableSettingButton);     // SetupFinalConfirm 传 1
changeVocalButton.enabled = info.EnableChangeVocalButton;  // 只改 enabled，格子还在
```

布局是 `HorizontalLayoutGroup`（spacing 108、MiddleCenter、child-control 0）：
2 格 → 总宽 268 → 左 217 → 条内 257/445 → phone-local **281.45/469.45**；
3 格（随机也留着）→ 总宽 456 → 左 123 → 163/351/539，**整排会平移**，那不是原作的样子
（`SettingButton` 自带的 anchoredPosition.x = 163 只是编辑器里的存档值，不代表这一页）。
左格点开设置卡，右格换音源版本。


## 5. 页面转场：淡出 → 换页 → 淡入（不是闪白）

原作的 screen layer 切换走 `SekaiTransitionController`：一个全屏盖色（`coverFader`）+
`FadeOut(duration)` / `FadeIn(duration)` / `FadeOutSimple`，也就是**先淡出到盖色、换页、
再从盖色淡入**。我们 2026-11-22 之前是翻页那一帧直接铺一层 **210/255** 的白再衰减 0.24s，
于是进出最终页各闪一次，看起来就是"两段屏闪白"。

现在照原作拆成三段（`game/SongSelect.cpp` 里 `requestPageSwitch` / `tickPageSwitch` /
`drawPageTransition`）：

```
t=0            点 确定 / Enter          requestPageSwitch(true, now, ...)   只记状态
t=0 .. 0.15    旧页正常出帧，白盖色 0→1  drawPageTransition（alpha = t / kPageFadeOut）
t=0.15         真正换页（一帧）          tickPageSwitch：gFinalConfirmOpen = 目标
t=0.15 .. 0.36 新页从纯白淡出来          alpha = 1 − (t−0.15) / kPageFadeIn
```

- `kPageFadeOut = 0.15` / `kPageFadeIn = 0.21`，盖色用**白**（和 确定/开始 的爆闪同色，
  衔接处看不出第二条边界）。
- **换页动作必须推到淡出结束**（`tickPageSwitch`）—— 在点击那一帧就翻页的话，
  旧页一帧都没有淡出过程，又变回闪屏。
- 淡出期间整页吃掉点击（`pageSwitchBusy()`），不然白屏上还能先爆一颗闪。
- 返回方向（ESC / 左上返回钮 / 抽屉里的"离开这一页"命令）走同一个 `requestPageSwitch(false, …)`。
- 调试：`CPSEKAI_FINAL_FADE=<sec>` 在选曲页 t=秒 处自动触发一次"去最终页"的转场，
  配 `--screenshot-time` 抓中间帧：`1.08` 淡出、`1.26` 淡入（`--width 960 --height 540` 够快）。

## 6. 验收手法

1. 无头截图：`CPSEKAI_FINAL_CONFIRM=1 ./build/cppsekai.exe --screenshot out.png --width 1920 --height 1080 --screenshot-time 1.5`。
2. **矩形叠加自检**：把 `final703.txt` 里选中的节点矩形按 2px 红线画到截图上，
   逐个确认零件落在框里（本轮就是这么发现箭头偏 6/14px、AUTO 卡偏下 55px 的）。
   脚本思路见 `build/_overlay.py`（临时文件，别入库）。
3. 尺寸回归：1024×768 / 1280×720 / 2560×1080 各截一张，确认"列表贴左、手机贴右"的贴边规则没崩。
