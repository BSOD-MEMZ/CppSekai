# 原作的设置界面（OptionDialog）实测规格

> 2026-11-21 逆向日服 7.1.0。证据基座：`dump.cs`（TypeDefIndex / RVA 均标注）
> + `libil2cpp_arm64.so`（capstone 反汇编，VA 标注）+ AssetRipper 2.0.0 读 prefab
> + `tex_all_sprite/` 逐像素量 alpha。
>
> 一句话结论：**1168 宽、按高度自适应、圆角 16px、9-slice 边框 21px、黑 20% 阴影外扩 8px**；
> **开：0 → 1 缩放，0.125s Linear；关：1 → 0，同样 0.125s Linear**，背景遮罩 0.025s 淡入。

---

## 0 怎么复现（工具链）

AssetRipper 常驻后：

```bash
cd /d/Tools/rea/work/jp
/d/Tools/rea/work/tools/arip/app/AssetRipper.GUI.Free.exe --headless --port 17654 &
curl -X POST "http://127.0.0.1:17654/LoadFolder" --data-urlencode "path=D:/Tools/rea/work/jp/GameData"

python arip_search.py Option PrefabHierarchyObject     # OptionDialog = prefab 1647
python ms_geom.py 1647 opt_dialog                      # 几何树（祖先链 + size/anchor）
python arip_dump.py 1647 opt_full --all                # 每个 GO 的组件 YAML（含 Image/Text/Layout）
python sprite_info.py bg_base_r16_wh                   # sprite 的 m_Rect / m_Border
python so_xref.py 0xB6F7908                            # 找静态字段的引用点
python ../..//Dev/CppSekai/.workbuddy/tools/il2cpp_disasm.py libil2cpp_arm64.so --range 0x.. 0x..
python ../..//Dev/CppSekai/.workbuddy/tools/il2cpp_where.py dump/dump.cs 0xVA
```

本次新增并入库在 `D:\Tools\rea\work\jp\`：`arip.py`（HTTP 客户端）、`arip_dump.py`
（prefab 逐节点组件导出）、`sprite_info.py`（sprite 九宫格边框）、`so_xref.py`（`adrp+ldr` 交叉引用）。

**画布基准**：`CanvasRoot`(prefab 426) 的 `ScreenCanvasScaler` = ScaleWithScreenSize /
**1920×1080** / `m_MatchWidthOrHeight: 0`（按宽）。所以 prefab 数值就是像素，不换算。

---

## 1 类结构（`dump.cs`）

| 类 | TypeDefIndex | 角色 |
|---|---|---|
| `OptionDialog : Common1ButtonDialog` | 8817 | 设置弹窗本体，五个页子对象 |
| `DialogBase : MonoBehaviour` | 8531 | **所有弹窗的基类**：开关动画、尺寸、遮罩全在这 |
| `DialogSizeFitter` | 8887 | 按 `DialogSize` 设窗口高度 |
| `DialogHeader` | 8532 | 只有 `CustomText title` 一个字段 |
| `UIPartsDialogTab` / `UIPartsDialogTabGroup` | 8890 / 8892 | 页签与页签组 |
| `OptionLiveSetting` | 6243 | 演奏页 |
| `OptionVolumeSetting` | 6265 | 音量页 |
| `OptionSystemSetting` | 6260 | 系统页 |
| `OptionCommunicationSetting` | 6240 | 通信页 |
| `OptionCustomScoreSetting` | 6242 | 自定义谱面页 |

`OptionDialog` 的五个 `[SerializeField]` 正好对应五个页；
`Setup(tabIndex, canAssetSetting, canBlockListSetting, showCustomScoreTab, showCommunicationTab)`
决定哪些页签存在（`_activeTabIndices` + `UpdateTabLines()`）。

`DialogSize`：`Small=0 / Medium=1 / Large=2 / Manual=3`。

---

## 2 窗口几何（prefab 1647，1920×1080 基准）

```
OptionDialog                      全屏 0 尺寸（anchor 0..1，CanvasGroup）
└ WindowRoot                      宽 1168、高由 VerticalLayoutGroup 撑开（见下）
  ├ Tabs                          高 72     （LayoutElement preferredHeight）
  ├ Bg                            铺满 WindowRoot（忽略布局）
  │ ├ Shadow                      尺寸 (W+16, H+16)   铺满 + 8px 四周
  │ └ Base                        铺满
  ├ CloseButton                   44×44，anchor(1,1)，pos(-15,-15)
  ├ Padding                       高 64
  ├ ContentRoot                   高 752
  │ └ Content                     宽 = W-128（左右各 64 内边距）
  └ FooterButtons                 高 144，按钮 336×96，水平间距 24
```

- `WindowRoot` 宽 **1168**（`m_SizeDelta.x = 1168`，anchor 全居中 `(0.5,0.5)`）。
- 高度 = `Tabs(72) + Padding(64) + ContentRoot(752) + FooterButtons(144)` = **1032**
  （`DialogSizeFitter.Setup` 里按 `DialogSize` 查 `SIZE_DIC` 覆写，再减 `bottomPadding`）。
- 内容区有效宽 = `1168 - 128 = 1040`（左右各 64）。
- `Bg` 与 `CloseButton` 的 `LayoutElement.m_IgnoreLayout = 1`，不参与竖向排布。

**`DialogSizeFitter` 的三档高度（`.cctor` VA `0x4CB0274` 实证）**：

| DialogSize | `SIZE_DIC` 值 | 浮点位模式 | prefab 里 `dialogSize` |
|---|---|---|---|
| Small (0) | **336** | `0x43A80000` | — |
| Medium (1) | **688** | `0x442C0000` | — |
| Large (2) | **1032** | `0x44810000` | **`OptionDialog` 用的就是这档** |
| Manual (3) | 不查表 | — | — |

`1032 = 72(Tabs) + 64(Padding) + 752(ContentRoot) + 144(FooterButtons)` —— 与 VLG 完全对上，
反过来证明 Large 就是「四个子块全展开」的高度。

`bottomPadding = 0x43100000 = **144.0**`。`Setup()`（VA `0x4CAF8EC`）的计算是：

```
content.preferredHeight = SIZE_DIC[dialogSize] − paddingLayoutElement.preferredHeight − 144
                        （若 dialogSize==Large 且 viewType==HasTab，再 −72）
```

- `fsub s0, s9, s8` / `fsub s0, s0, s1`（`s1 = [klass.static+8] = 144.0`）
- `mov w8, #-0x3d700000` → 该 32 位模式是 **`0xC2900000` = −72.0f**（不是 -3.65）
- `ccmp w19,#2,#0,eq` + `fcsel`：条件 = 前一条 `cmp w20,#1` 成立的**且** `w19==2`，
  即 `viewType == HasTab && dialogSize == Large` 才走 −72 分支（页签版额外让出 72px）。

> 早期手算把这组常量记成「567 / −3.65」是**错的**（把 `movz w8,#0xC290,lsl#16`
> 当成独立浮点）。已按位模式重新解码。

---

## 3 圆角、阴影、描边（逐像素实测）

### 3.1 窗口圆角 = **16px**

| 用途 | sprite | 贴图 | 九宫格 `m_Border` | 实测圆角 |
|---|---|---|---|---|
| 窗口底板 | `bg_base_r16_wh` | 44×44 | **21** | **16.25** |
| 大圆角底板 | `bg_base_r30_wh` | 70×70 | 34 | 29.00 |
| 中号 | `bg_base_r20_wh` | 50×50 | 24 | 19.68 |
| 数字框/胶囊 | `bg_base_r16_wh` | 44×44 | 21 | 16.25 |
| 圆形按钮 | `btn_round_r16_wh` | 56×56 | 28 | 15.68 |

**`m_Type: 1`（Sliced）**，不是 procedural 圆角也没有 shader。
`m_PixelsPerUnitMultiplier: 1`，`m_Sprite` 走 `useSharedSprite: 1` + `spriteName`。

> **border ≠ radius**：`r16` 的 border 是 21 = 16（圆角）+ 5（抗锯齿余量）。
> 照抄 border 会做出偏胖的角。**圆角按名字里的数字取，border 按 21 摆九宫格**。

实测（alpha ≥127 的圆弧拟合）：`bg_base_r16_wh` 圆心 (16.5,16.5)、半径 **16.25**（方差 0.058）。

### 3.2 阴影 = 黑色 20%，外扩 8px，模糊半径 ≈12px

- 节点 `Shadow` 尺寸 `Base + (16, 16)`，铺满 `Bg`。
- sprite `bg_base_shadow_r16`（60×60，border 30），`m_Type: 1`。
- 贴图像素：中心 `RGBA(0,0,0,51)` → **黑色 20%**；边缘 alpha 从 51 衰减到 0 约 12px。
- 大窗口版 `bg_base_shadow_r30`（86×86，border 42）。
- `Bg` 上还有一个 `m_Padding / m_Softness` 的软阴影脚本，但 **`m_Enabled: 0`**，没在用。

### 3.3 没有 Outline

`Shadow`/`Outline` 这类 `UnityEngine.UI` 描边组件在整个 prefab 里**一个都没有**；
视觉上的分界全靠底色和 1px 级别的色块（页签的 `Line` 是 4×32 的实心块，见 §5）。

---

## 4 开 / 关动画（`DialogBase`，全部 asm 实证）

常量：`DIALOG_OPEN_DURATION = 0.125`（`protected const float`，dump.cs 305352）。

### 4.1 初始化 `InitializeAnimation()` — VA `0x4C4C984`

```
windowObject.transform.localScale = Vector3.zero
```
即窗口一出来就是 **0 缩放**（读的是 `Vector3` 的静态字段 `static_fields[0..8]`，
引用槽 `0xB6F7908`，偏移 0/4/8 → `Vector3.zero`）。

### 4.2 打开 `OpenAnimation()` — VA `0x4C4C9F4`

```
transform.DOScale(1.0f, 0.125f).SetEase(Ease.Linear)
     .OnComplete(mat → null, method → this.OnFinishOpenAnimation)
fillCover.Play(遮挡色, delay = 0.0f, duration = 0.025f)
```

- `fmov s0, #1.0` / `fmov s1, #0.125`（VA `0x4C4CA84`）。
- `SetEase` 的枚举值传的是 `w1 = 1` → **`Ease.Linear`**（`Ease.Unset=0, Linear=1`）。
- `ColorFader.Play(Color, delay, duration, Action)`：`s4 = fmov wzr = 0`（delay）、
  `s5 = 0x1F23DAC = 0.025`（duration）。

### 4.3 关闭 `CloseAnimation()` — VA `0x4C4CB7C`

```
transform.DOScale(0.0f, 0.125f).SetEase(Ease.Linear).OnComplete(→ DestroyMyself)
fillCover.Play(透明色, 0.0f, 0.025f)
```

与打开完全对称（`fmov s0, wzr`）。**没有位移、没有 alpha 渐变、没有回弹/过冲**。

> 所以「窗口出现」= 从中心 0 倍瞬间长出到 1 倍，125ms 线性；配合遮罩 25ms 一闪。
> 想做得像原作，**不要**用 OutBack / 弹性，也别加位移。

### 4.4 状态机

`DialogState` 在 `Open()` 里置 **2**（VA `0x4C4C8D8`）、`OnFinishOpenAnimation` 里置 **3**
（VA `0x4C4D5EC`）。`Close()` 走 `CloseKeepObject` / `DestroyMyself` 两条路。
`_allowCloseExternal` 控制点背景能否关闭（`AttachCloseExternalListener`）。

---

## 5 页签（Tab）—— prefab 327 + `UIPartsDialogTab`

`Tabs` 容器自己也是一张图：`bg_base_half_r30_wh`（70×40，border 34/0/34/34，
**上直角、下 r30**），`m_Color = #BCBCD0`（0.7373,0.7373,0.8157），`m_Type: 1`。
所以页签条的底是「上边平、下边圆」的一条灰带，和每个页签的 `bg_base_half_r16_wh` 同构。

几何：`Tabs` 高 **72**，`HorizontalLayoutGroup` + `m_Padding = 左右各 70`、
`m_Spacing = 0`、`ChildControlWidth/Height = 1`、`ChildForceExpandWidth = 1`
（**每个页签等分剩余宽度**）、`ChildAlignment = 7`。
页签数量由 `UIPartsDialogTabGroup.Setup(int tabCount)` 运行时克隆 `tabPrefab`。

单页签（运行时由布局撑成 `(可用宽 - 140) / N × 72`）：

| 子节点 | 尺寸 | sprite | 颜色（off 态） | 备注 |
|---|---|---|---|---|
| `background` | 铺满 | `bg_base_half_r16_wh`（42×26，border 21/1/21/25） | `#EBEBF2` (0.9216,0.9216,0.9490) | 上是**直角**、下是 r16 圆角（"半圆角"） |
| `Cover` | 铺满 | `bg_tab_dbl`（`m_Type:1`） | `(0,0,0.1333, a=0.5)` = 深蓝 50% | 选中时压在上面 |
| `Line` | **4×32**，anchor(1,0.5) | `bg_base_wh`（4×4） | `#A7A7BC` (0.654,0.654,0.737) | 页签之间的分隔竖线，选中时藏起 |
| `Text` | 铺满 | — | `#444466` (0.2667,0.2667,0.4) | **fontSize 28** |
| `Badge` | **44×44**，anchor(1,1)，pos(-14,-17) | `badge_attention_rd`（32×32） | 白 | 红点角标 |

**选中态切换（asm VA `0x4CB0464` On / `0x4CB04B0` Off）**：

- `On()`：`backgroundImage.enabled = true`，`text` 换色 `GetColor(ColorEntry)` 索引 **0**
- `Off()`：`backgroundImage.enabled = false`，`text` 换色索引 **2**

即**选中时底色块整个关掉**（露出底下的 `Cover` 深蓝半透明），文字换成色表第 0/2 号色。
`ColorEntry`：`0=base_dbl、1=base_dbl_a50、2=base_wh、…`（dump.cs 629）。

---

## 6 窗口内的控件

### 6.1 单选（`CustomIndexToggleGroup`）—— 演奏页最常见

`ToggleList` 布局：`HorizontalLayoutGroup`，`m_Spacing 0`，
单元 `UIPartsRadioToggleText` **340×96**（页签版）或 **160×96**（两选版），
内部 `UIPartsRadioToggle` **72×72**（anchor 左中、pivot 左中），
文字在下方 `-50`（两选版）/ 由 `UIPartsRadioToggleText` 自带字号。

- 圆点 sprite `icon_radio_gn`（36×36，无九宫格）
- 行进间距：`ToggleList` 中心偏移 `(-8, -100)`，单元 pos.y = `-24` / `-112`（两行）

### 6.2 数字选择器（`UIPartsNumericSelector`）

```
UIPartsNumericSelector        592×80  （页签版）/ 324×96（紧凑版）
├ NumBase                    112×80 / 144×80   bg_base_r16_wh  #BCBCD0 (0.7373,0.7373,0.8157)
├ UIPartsDecrementButton     96×96  anchor(0,0.5) pivot(0,0.5)   btn_round_r16_wh
│ └ CustomImage              32×8   （减号横线，不是 glyph）
└ UIPartsIncrementButton     96×96  anchor(1,0.5) pivot(1,0.5)   btn_round_r16_wh
  └ CustomImage              36×36  （`icon_plus_h36_wh`）
```
- 加减按钮与数字框**重叠**（数字框 112 宽、按钮各 96），靠 `EffectCover`(6×6) 做按下反馈。
- `UIPartsFourButtonNumericSelector`（timing adjust 用）590×96，四个按钮各 **105×96**，
  内圈两个 pos ±140、外圈 ±252，`NumBase` 144×80。

### 6.3 按钮（`UIPartsCommonButton`，prefab 330）

```
UIPartsCommonButton   336×96
├ ButtonImage         铺满（圆角靠 sprite，如 btn_round_h80_wh 106×96 border 51/0/51/0）
├ Cover               铺满 -16（按下时内缩 8px 的描边）
├ FrontGlowCover      铺满 +8
├ Content / Text      铺满 / 铺满 -48（左右各 24 的文本内边距）
```
底部按钮排：`FooterButtons` 高 **144**，`HorizontalLayoutGroup` 居中、
**间距 24**、`ChildForceExpandWidth = 0`（按钮保持 336 宽，不拉伸）。

### 6.4 其它控件尺寸速查

| 控件 | 尺寸 | 备注 |
|---|---|---|
| 关闭 X | **44×44**，pos(-15,-15) | sprite `btn_close_dbl` 44×44（无九宫格），右下角对齐 |
| 页签角标 | 44×44 @ (-14,-17) | `badge_attention_rd` 32×32 |
| 左右翻页箭头 | 72×102 | `btn_arrow_switching_wh` 80×108；`ClickArea` 92×102（命中区比可见大） |
| 音符皮肤预览 | 864×152 | `NotePreview` |
| 音符预览 | 401×264 | `NotesPreview` |
| 长条预览 | 400×152，线 160 宽 | `LongNotesPreview` |
| 滑块行 | 592×152（标题 592×48） | `UIPartsNumericSelector` 592×80 |
| 滚动条 | 6 宽，`SlidingArea` -20，`Handle` 2×20 | `UIPartsScrollbarVertical`，pos.x = +36 |
| 页内标题 | 1040×44，下面 4px 线 | `LiveSettingTitle` |
| 滚动内容 | 1040×(每页内容高) | `Content`，`verticalNormalizedPosition` 顶对齐 |

### 6.5 页内排版（`Live` 页实测）

`VerticalLayoutGroup`：`m_Padding = 顶 68`、`m_Spacing = 32`、
`ChildControlWidth/Height = 0`（**子节点尺寸自己管**）、`ChildAlignment = 0`（UpperLeft）。
所以行高由每个块自己的 `RectTransform` 决定，垂直间距固定 **32**。

---

## 7 颜色表（本界面对照）

| 用途 | 值 | 来源 |
|---|---|---|
| 窗口底板 | 白（sprite 本身白，靠 `m_Color` 调） | `bg_base_r16_wh` 中心 (255,255,255,255) |
| 数字框/灰块 | `#BCBCD0` | `m_Color: 0.7373,0.7373,0.8157` |
| 页签底（未选） | `#EBEBF2` | `bg_base_half_r16_wh` 的 `m_Color` |
| 页签遮罩（选中） | `rgba(0,0,34,0.5)` | `Cover` |
| 页签文字 | `#444466` | `m_fontColor` |
| 页签分隔线 | `#A7A7BC` | `Line` |
| 阴影 | `rgba(0,0,0,0.2)` | `bg_base_shadow_r16` 贴图像素 |
| `Tabs` 容器底色 | (0.7373,0.7373,0.8157,1) | `Tabs` 自己也有一个 Image |

> `#444466` 与我们仓库现有观感基准一致（见 `MEMORY.md`「观感基准」条）。

---

## 8 落进我们代码时的对照表

| 原作 | 我们现状 | 动作 |
|---|---|---|
| 窗口宽 1168 | 设置卡片 920×780（七页签） | 不改宽度（我们是 7 页，原作最多 5 页） |
| 圆角 16px | `14*s` | 可提到 16，或保持 14（差异 2px） |
| 阴影 黑 20% / 外扩 8 / 模糊 12 | `ui::dropShadow(dl, lo, hi, rounding, s)` 三层同心 | 参数对齐：`s ≈ 8~12`，颜色 `0,0,0,0.2` |
| 开启 0→1 / 0.125s / **Linear** | 缩放入场动画（`ui::anim` 指数逼近） | **改 Linear + 125ms 会明显更像原作** |
| 关闭 1→0 / 0.125s / Linear | — | 同上 |
| 遮罩 25ms | — | 几乎瞬时，可省 |
| 页签高 72、等分宽、左右 70 padding | `ui::tabBar` 灰带 + 选中块 | 高度 72 可以直接用 |
| 页签文字 28px / `#444466` | 系统字体 | 28 在 1080p 下偏大，我们是 22~24，按需 |
| 页签选中 = **关掉底块** + 露深蓝遮罩 | 我们画内容色选中块 | 视觉等价，不必改 |

---

## 9 还没拿到的（别当成事实）

1. **`Vector3.zero` 那条是推断**：`InitializeAnimation` 读的静态槽 `0xB6F7908`
   我们没能把该槽反解成类名（`script.json` 的 `Addresses`/`ScriptMetadata` 都不含这个区间）。
   但关闭动画 `fmov s0, wzr`（0.0）与之对称，且 `DOScale(1f, .125f)` 的起点只能是 0，
   **结论"从 0 长到 1"是可靠的**；只是"它确实引用 `Vector3.zero`"这层身份未证实。
2. **`DialogSizeFitter.SIZE_DIC` 的三个高度是 asm 常量**（567 / 688 / 1032）。
   `Large` 与我们的 780 差得远，因为原作 `Large` 是**带内容滚动的整页**。
3. **关闭时的窗口本体位移**：没有。`CloseAnimation` 只做缩放，`CloseKeepObject` 也不补位移动画。
4. **`stopSE` / 打开音效**：`DialogBase.openSE` 是枚举，具体 SE 名未查（走 `seArray`）。
5. **`UIPartsScrollbarVertical` 的滚动惯性参数**（`CustomScrollRect`）未取。
