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

### 3.1 AUTO 那张卡片（2026-11-22 第三轮，照国服截图重做）

原作的 AUTO 是**一张卡片**：卡片上烤着 "AUTO" 字样，卡片**里面**左边是开关
（白色圆钮 + 状态字「开启 / 关闭」），右边一颗白色小圆齿轮 —— 齿轮在卡片里，
点它进设置。prefab 里这几个零件都在 `AutoLiveToggle` 底下：

| 零件 | prefab 值 | 说明 |
|---|---|---|
| `Image` | `bg_auto` 180×110，**Sliced border 119/0/51/0** | 左 119 装 "AUTO"、右 51 是右圆头，中间 10px 是给**横向拉伸**用的；70% 白烤在贴图里 |
| `ToggleRoot` | 822.6, 1014.4 / 152×56 | `Bg` = `bg_base_round_h56_wh`（33/28/33/28）：On `#FF77AA` / Off `#444466` |
| 旋钮 | 72×72（可见圆 56） | On 中心 (946.1,1042.4)、Off 中心 (850.6,1042.4) |
| `TextOn`/`TextOff` | 29px 白，中心 872.6 / 923.8 | 状态字在**旋钮对面** |
| `ConsecutiveAutoLiveSetting` | 72×72（`btn_circle56_wh` + 32×32 `icon_setting`） | 齿轮 |

⚠ **prefab 把齿轮摆在 AUTO 的正中**（898.6, 1042.4）——和开关是同一个点，
那是"开关和齿轮互斥"的摆法，两者不可能同时可见。国服版是**并排**放在一张更宽的
卡片里，所以这里按内容把卡片撑开：

```
卡片   802.6..1074.6  x  960.4..1070.4   (272x110，底边对齐轨道底边)
轨道   822.6..974.6   x  1014.4..1070.4  (prefab 原值，不动)
齿轮   982.6..1054.6  x  1006.4..1078.4  (可见圆 1014.4..1070.4，和轨道齐)
```

另外两条硬事实：`AutoLiveToggle.sizeDelta.x = 0`（Image 根本画不出来，宽度/高度都是
运行时给的，所以 prefab 里那个 `y=932.4` 不能当渲染位置）；把卡片顶边贴 932.4 会
和开关叠成"两个互相错开的胶囊"，这就是前两版看着不对的原因。

## 4. 右侧手机（prefab 704）

`CellPhone` 的 anchoredPosition 是 **(562, −202)**、尺寸 754.3×1416.7、绕中心 **+3°**
（`TransformList/DefaultPosition`，全游戏只有一个位置，**最终页和选曲页一样**）。
换算到 1920 画布：x 1144.85..1899.15、y 33.65..1450.35。

**两页共用一份 `phoneScreenRect(w,h,k)`（game/SongSelect.cpp）** —— 换页时手机不可能
再挪位。两个坑都是这条规则治好的：

1. 最终页以前多夹了一次 `k = min(k, h/1080)`：窗口高度不是 1080（最大化后被任务栏
   吃掉一截、或 125% 缩放的 1440p）时，最终页整体缩到 0.94 倍，手机跟着变小。
   现在**两页用同一个 k**（选曲页的"按宽度匹配 + 锁 100%"）。
2. 垂直位置：原作是中心锚点（中心 = 画布中心 + 202），画布矮时整台手机上移、把
   确定留在屏内；缩放调小后画布高于 1080 时中心锚点又把它往下推。规则是
   `y = min(33.6k, h/2 + 202k − 754.3k/2)`，两页都必须用它。

实测（1920×1017 与 1920×1080 各截两页，取手机左缘在第 400/600/800 行的 x）：
两页逐行完全相同。

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


## 5. 页面转场：内容淡出 → 换页 → 内容淡入（**不铺白色**）

原作的 screen layer 切换走 `SekaiTransitionController`：`coverFader` + `FadeOut(duration)`
/ `FadeIn(duration)`。2026-11-22 第一版我把它做成"淡出到**纯白**、换页、再从纯白淡入"，
结果淡出到底的那一帧本身就是一次白闪 —— 用户一眼看出来，我还在逐帧图上说"没有"，
那是错的：**只要画面上出现过一整帧纯白，它就是闪白**。

现在的做法（`pageFadeContentAlpha` / `fadeContentVertices` / `tickPageFadeEnd`）：

- 两页画的是**同一张背景**（壁纸 / bg_common，像素一致），所以只淡"背景之后的顶点"：
  把整段 VtxBuffer 的颜色 alpha 乘一个 0→1 的系数。背景始终不动 → 没有白、没有黑。
- `kPageFadeOut = 0.15` / `kPageFadeIn = 0.21`；**换页动作推到淡出结束那一帧**
  （`tickPageSwitch`），所以旧页在变淡的过程里还在正常出帧。
- 选曲页有**两个 draw list**（窗口 + 列表子窗口 `listDl`），两个都要乘，
  不然列表会留在原地。最终页只有一个。
- 淡出期间整页吃点击（`pageSwitchBusy()`）。
- 返回方向（ESC / 左上返回钮）走同一个 `requestPageSwitch(false, …)`。

全屏平均亮度的实测（960×540，`CPSEKAI_FINAL_FADE=1.0`）：

```
1.00 → 119.6   1.05 → 115.0   1.10 → 110.2   1.13 → 106.9   (旧页淡到底)
1.16 → 105.2   1.20 → 114.9   1.26 → 126.7   1.32 → 132.5   1.38 → 135.5 (新页)
```

单调下、单调上，**中间没有尖峰**（第一版那里会冲到 253 又掉回来）。

## 6. 验收手法

1. 无头截图：`CPSEKAI_FINAL_CONFIRM=1 ./build/cppsekai.exe --screenshot out.png --width 1920 --height 1080 --screenshot-time 1.5`。
2. **矩形叠加自检**：把 `final703.txt` 里选中的节点矩形按 2px 红线画到截图上，
   逐个确认零件落在框里（本轮就是这么发现箭头偏 6/14px、AUTO 卡偏下 55px 的）。
   脚本思路见 `build/_overlay.py`（临时文件，别入库）。
3. 尺寸回归：1024×768 / 1280×720 / 2560×1080 各截一张，确认"列表贴左、手机贴右"的贴边规则没崩。
