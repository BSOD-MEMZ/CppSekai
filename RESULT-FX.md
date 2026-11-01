# RESULT-FX.md —— 曲末五支 `fx_result_*` 的 prefab 实测

> 2026-10-31。日服 7.1.0（`GameData` 624MB，AssetRipper 2.0.0）。
> 这份文档替代 AGENTS.md 里「待办：五支 fx_result_* 的 prefab 内容」那一条。

## 0. 先纠正命名：五支里只有三支是 prefab`stringliteral.json` 里那一族（`fx_result_finish` / `fx_result_clear_v2` /
`fx_result_failed_v2` / `fx_result_full_combo_v2` / `fx_result_all_perfect_v2`）
是**代码里的字符串**，不是资产名。资产里的真名是 PascalCase，而且只有三支：

| 代码里的名字 | 资产里的 prefab | pathID | clip | 状态 |
|---|---|---|---|---|
| `fx_result_clear_v2` | `fx_result_LiveClear` | 1430 | `clip_result_LiveClear` 1.5167s | **已实现**（`LiveClearBanner`） |
| `fx_result_full_combo_v2` | `fx_result_FullCombo` | 1431 | `clip_result_FullCombo` 1.0s | 待做 |
| `fx_result_all_perfect_v2` | `fx_result_AllPerfect` | 1435 | `clip_result_AllPerfect` 1.0s | 待做 |
| `fx_result_finish` | **不存在** | — | — | 见 §5 |
| `fx_result_failed_v2` | **不存在** | — | — | 见 §5 |

另外还有结算页气泡版 `fx_result_baloon_FullCombo`(1433) /
`fx_result_baloon_AllPerfect`(1434)，以及多人模式的 `fx_result_MVP` /
`_ace` / `_SuperStar` / `rank_1..5` / `_win` / `_lose` / `_draw` / `_team` /
`_gage` / `_LiveReward` / `_ScoreRank_Success|Faild`。**没有 Timeline 资产**
（搜 `TimelineAsset` 只有 MonoScript 类型定义），所以这一族就是
`Animator + AnimationClip + ParticleSystem`，没有别的中间层。

## 1. 共同点（三支都一样）

- 根节点 `RectTransform`：`100×100`，`anchor (0.5,0.5)-(0.5,0.5)`。
  **`fx_result_FullCombo` / `fx_result_AllPerfect` 的根 `anchoredPosition` 是
  `(0, -48)`** —— 屏幕上比 LiveClear 低 48 单位。
- 子 `root` 是唯一带 **Animator** 的节点（时间轴都挂在它上面）。
- 文字是 **UI 图**（`CanvasRenderer + MonoBehaviour`，字段 `m_Texture` +
  `m_Color`），不是 Text / TextMeshPro —— 也就是**一张贴图整块画上去**，
  没有字形排版问题。
- `*_light` 层和本体**共用同一张 `m_Texture`**，区别只在 `m_Material`：
  light 是一个加法材质，本体没有。所以 light = 同图再画一遍、加法混合、
  单独一条 alpha 曲线。
- 全部 `clip` 的 `sampleRate = 60`，都只有 **`m_IsActive` + `m_Color.a`** 两类曲线，
  **没有位移 / 缩放 / 旋转**。这是这三支最省事的地方。

## 2. `fx_result_FullCombo`（1.0s）

```
fx_result_FullCombo   pos=(0,-48)     100x100
  root                pos=(0,0)       [PS, ParticleSystemRenderer, Animator]
    FullCombo_light   pos=(0,0)  300x56   [CanvasRenderer, MonoBehaviour(add 材质)]
    FullCombo         pos=(0,0)  300x56   [CanvasRenderer, MonoBehaviour]
    flash             pos=(0,0)           [PS, ParticleSystemRenderer]
```

时间轴（`clip_result_FullCombo`，stopTime 1.0）：

| binding | 曲线 |
|---|---|
| `FullCombo_light` `m_IsActive` | `0:0 0.0167:1` |
| `FullCombo_light` `m_Color.a` | `0:1 0.0167:0 0.1667:0.705882 0.4167:0` |
| `FullCombo` `m_IsActive` | `0:0 0.0167:1` |
| `FullCombo` `m_Color.a` | `0:1 0.0167:0 0.1667:1`（之后保持） |
| `flash` `m_IsActive` | `0:0 0.15:1` |

读法（Unity 的 `m_IsActive` 曲线 = 激活开关，1 才是显示）：
**开局两帧内本体和光层都被关掉（a:1→0），0.0167s 打开，本体在 0.1667s 淡到全**不透明，
之后一直亮着；光层从同一时刻起以 0.705882 起步、0.4167s 淡没。`flash` 在 0.15s 出现。**

贴图：`tex_result/FullCombo.png`（**300×56**，颜色是粉色带斜纹的 `FULL COMBO!`），
已拷进 `assets/mmw/overlay/result/fullcombo.png`。

## 3. `fx_result_AllPerfect`（1.0s）

```
fx_result_AllPerfect  pos=(0,-48)    100x100
  root                pos=(0,0)      [PS, ParticleSystemRenderer, Animator]
    star              pos=(0,-19)    [PS, ParticleSystemRenderer]
      star_bg         pos=(0,0)      [PS, ParticleSystemRenderer]
    flash             pos=(0,0)      [PS, ParticleSystemRenderer]
    AllPerfect        pos=(0,0)  320x56   [CanvasRenderer, MonoBehaviour]
    AllPerfect_light  pos=(0,0)  320x56   [CanvasRenderer, MonoBehaviour(add 材质)]
```

时间轴（`clip_result_AllPerfect`，stopTime 1.0）：

| binding | 曲线 |
|---|---|
| `AllPerfect_light` `m_IsActive` | `0:0 0.0167:1` |
| `AllPerfect_light` `m_Color.a` | `0:1 0.0167:0 0.0833:0.705882 0.3333:0` |
| `AllPerfect` `m_IsActive` | `0:0 0.0167:1` |
| `AllPerfect` `m_Color.a` | `0:1 0.0167:0 0.0833:1`（之后保持） |
| `flash` `m_IsActive` | `0:0 0.0833:1` |
| `star` `m_IsActive` | `0:0 0.1667:1` |

**和 FullCombo 同构，只是整体快了半拍**：淡入在 0.0833s（FullCombo 是 0.1667s），
光在 0.3333s 收完，闪光 0.0833s，星星 0.1667s 才出现。

贴图：`tex_result/AllPerfect.png`（**320×56**，彩虹渐变的 `ALL PERFECT!!`），
已拷进 `assets/mmw/overlay/result/allperfect.png`。

**两张贴图的尺寸和 `sizeDelta` 完全一致（1:1）**，不像 LiveClear 那张是 1236×182 的
10 字母 atlas —— 这两支不用切图，整张画上去就行。

## 4. 粒子（粗粒度，够用）

`parse_ps.py` 对这两支的输出（Unity 世界单位，父级是 100×100 的 RectTransform，
换算链太长，**照抄数值没有意义**，这里只给形态）：

| 节点 | 形态 |
|---|---|
| `root` | loop，`rateOverTime 10/s`，`startLifetime 5`，`startSpeed 5`，`startSize 1`，无 burst —— 一层很淡的常驻微光 |
| `flash` | 不 loop，`rate 0` + **burst**，`startLifetime 0.3`，`startSpeed 0`，`startSize 4` —— 一次性闪一下 |
| `star` | loop，`rate 5/s`，`startLifetime 1`，`startSize 0.06`，子节点还有 `star_bg` —— 细小星点 |

三支的 `ParticleSystemRenderer` 都只有 1 个有效材质（第二个槽是空的）。

## 5. `fx_result_finish` / `fx_result_failed_v2` 不存在

翻遍资产（`Search/View?q=fx_result` 全部 205 条，包含 GameObject / PrefabHierarchy /
PrefabInstance / AnimationClip / AnimatorState / Texture2D）**没有任何 `Finish` 或
`Failed` 的曲末 prefab**：

- 搜 `Finish` 只命中 UI 的 `LiveFinishedFrame` / `FinishedMessage` /
  `TutorialFinishedNewUserButton`，以及 3 个名字叫 `Finish` 的 AnimatorState；
- 搜 `Failed` / `LiveFailed` 在 `fx*` / `result*` 下 0 命中；
- 搜 `TimelineAsset` 只有类型定义，没有资产。

**结论：这两个名字是代码字符串，不是资源。** 最可能的两种情况：

1. 血空那一支**复用 `fx_result_LiveClear` 的 prefab，只换文字贴图**
   （`tex_livefailed_wh.png` 已经在用，结算页那张 1352×166 的 `LIVE FAILED`），
   代码按枚举分支换 `m_Texture` 再播同一个 clip —— 这样"五支"其实只有 3 个 prefab。
2. 或者 7.1.0 里这两支已经被砍掉，改由结算页的 `txt_livefailed_wh` 负责。

要定位得回到 `dump.cs` / `script.json` 找那个字符串被哪个方法引用 —— **今天没做**。
在没确认之前，**不要凭空照 LiveClear 的样子做一支 "finish"**，那是编的。

## 6. 工具（都在 `D:\Tools\rea\work\jp\`）

```bash
# 起 AssetRipper（headless，必须常驻）
/d/Tools/rea/work/tools/arip/app/AssetRipper.GUI.Free.exe --headless --port 17654 &
curl -X POST "http://127.0.0.1:17654/LoadFolder" --data-urlencode "path=D:/Tools/rea/work/jp/GameData"

# 导一支 prefab：树 + 每个组件的 YAML
python fx_result_dump.py "<搜索结果.html>" fx_result_FullCombo

# 粒子参数表
python fx_result_ps.py fx_result_FullCombo fx_result_AllPerfect

# 时间轴（喂 /Assets/View?Path=... 的值）
python clip_dump.py '<path>' clip_fullcombo.txt
```

产物（`D:\Tools\rea\work\jp\`）：

| 路径 | 内容 |
|---|---|
| `fx_result_LiveClear/` `fx_result_FullCombo/` `fx_result_AllPerfect/` `fx_result_baloon_*/` | 树（`tree.txt`/`tree.json`）+ 每个组件的 YAML |
| `clip_liveclear.txt` `clip_fullcombo.txt` `clip_allperfect.txt` | 解好的时间轴 |
| `tex_result/FullCombo.png` `tex_result/AllPerfect.png` | 文字贴图（原始尺寸 300×56 / 320×56） |

## 7. 还没做

- [ ] `FullComboBanner` / `AllPerfectBanner` 的实现（`LiveClearBanner` 是最接近的模板：
      都是"根 → 本体层 + 加法光层"，不过这两支**没有字母拆分**，一张图直接画）。
- [ ] 定位 `fx_result_finish` 的字符串引用（见 §5）。
- [ ] `fx_result_MVP` / `_ace` / `_SuperStar` / `rank_*` 是**多人模式**的结算特效，
      和单人结算不是一回事，要单独确认它们挂在哪个界面。

---

## 8. 2026-11-01 更正（用户给了实机截图）

用户提供了曲末的真实截图：**白色到浅蓝渐变的 `LIVE FINISH`**，样式和 `LIVE CLEAR` 一致。
这一条推翻了上面几处推断，记下来：

### 8.1 `LIVE CLEAR` 用的是「Sprite 拼字母」，不是整张图

`fx_result_LiveClear` 的 10 个字母节点，`MonoBehaviour` 里是 **`m_Sprite`**（不是
`m_Texture`），指向 `tex_LiveClear` 这张 1236×182 atlas 切出来的 10 个 Sprite：

```
tex_LiveClear_I(2)  _E2(3)  _L2(4)  _R(5)  _E!(6)
tex_LiveClear_V(7)  _L1(8)  _C(9)   _!(10) _A(11)
```

字母宽度各不相同（`I` 39 / `A` 134），**是按字形裁的独立 Sprite**，所以可以按任意顺序
和间距摆放 —— 这才是"字母从中心炸开"那支能做到的原因。

### 8.2 但 `FullCombo` / `AllPerfect` 是整张 `m_Texture`

这两支的 `FullCombo` / `AllPerfect` 节点是 **`m_Texture`（RawImage）**，300×56 / 320×56，
**没有字母节点**。对照 `LIVE CLEAR` 的字母高 147，这两张只有 56 —— **差 2.6 倍**。

**所以它们很可能不是曲末那层大字**，而是别的层级（结算页小字/即时提示）。
§2、§3 里"这就是曲末 FC/AP 大字"的隐含结论**站不住**，先别照着做。

旁证：`SUPERSTAR` 那支用的是 **`text_superstar` 整条 Sprite**，不是字母拼 ——
说明「整条文字图」才是通用做法，字母拼是 `LIVE CLEAR` 为了炸开单独做的。

### 8.3 `fx_result_finish` 仍然不在包里

复查（这次服务正常、`--noproxy`、GameData 全量 194631 资产）：

- `fx_result` 前缀的 prefab **21 个**，一个 `finish` 都没有；
- `ac_result` 前缀的 AnimatorController **38 个**，没有 `finish`；
- `liveclear` 搜出 13 个 Sprite / 2 个 Texture2D，`Finish` 在 tex2d 里 **0 命中**
  （`UnityDataAssetPack.apk` 和合并后的 `GameData` 都试过）；
- 没有 `TimelineAsset` 资产（只有类型定义）。

**结论：`fx_result_finish` 的 prefab / controller / 贴图都不在首包。** pjsk 的 bundle
是按需从 CDN 下的，`assets/data/` 里只有清单 —— 这一支要么是后续版本加的，要么属于
下载资源。**如果要做，只能自己拼。**

### 8.4 所以现在有两条路

1. **不等资产，照 `LiveClearBanner` 的机制自己画**：那支已经实现了"根 → 本体层 +
   加法光层 → 10 个字母按 x 从中心散开"，把文字换成 `LIVE FINISH` / `FULL COMBO` /
   `ALL PERFECT`，字母用系统字体渲染（项目里 `FontOutline` 已经在用），渐变用两条
   顶点色。**不需要那几张缺失的贴图，也不需要字母 Sprite。**
2. 等 CDN 索引（Moesekai 的 asset-viewer 是 Next.js 客户端渲染，HTML 里没有数据，
   要抓它的 API 或跑浏览器；`agent-browser` 在这台机器上开那个站 3 分钟没出画面，
   已放弃）。

**推荐 1** —— 观感能对上（截图就是白字 + 浅蓝渐变 + 光晕），而且不受资产缺失影响。
