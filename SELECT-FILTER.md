# SELECT-FILTER.md — 选曲界面的「絞り込み」弹窗 与 左侧团体页签（日服 7.1.0 实测）

> 逆向对象：`com.sega.pjsekai` 7.1.0（`libil2cpp.so` 199MB + `dump.cs` 82MB + `GameData`/AssetRipper）。
> 本文所有数值/RVA 都是实测，不是推断；推断处单独标注。
> 示意图 [`docs/select-filter-dialog.png`](docs/select-filter-dialog.png)（按 prefab 尺寸重建，非截图），
> 生成脚本 `.workbuddy/tools/gen_select_filter_dialog.py`（可重跑）。

## 0. 结论速览

- 顶栏那颗**筛选圆钮**（`FilterButton`，`icon_refine_dbl`）弹的是 **`MusicSelectFilterDialog`**，
  `DialogType.MusicSelectFilterDialog = 14`，prefab 是 `Assets/Resources/dialog/MusicSelectFilterDialog`
  （`PrefabHierarchyObject` PathID **1732**），窗口 **1168 × 1032**（`DialogSize.Large`）。
- 弹窗内容只有 **三段单/多选** + 底部 決定/キャンセル：
  1. **クリア状況**（5 个单选）
  2. **MV**（4 个复选）
  3. **楽曲**（4 个单选）
- **排序不在这里**，是另一个弹窗 `MusicSelectSortDialog`（`DialogType = 117`）。
- 单选的**组索引 index 直接就是枚举值**（`clearFilterGroup.SelectedIndex = (int)data.ClearFilterType`），
  没有额外的映射表；MV 是「多选框 + 未勾选集合」：持久化的是 `UnCheckedFilterMVTypes[]`。
- **左侧竖排不是"组合/团体页签的筛选弹窗"**，而是一列独立的 tab（`MusicSelectUnitTabList`），
  tab 的内容就是 `CategoryTabType`：`ALL/VOCALOID/LIGHT_MUSIC_CLUB/IDOL/STREET/THEME_PARK/
  SCHOOL_REFUSAL/OTHER` +（可选）`LIMITED`、`TOURNAMENT`、`MY_LIST_1..5`。
  点 tab 只是换 `categoryTabType` 然后重算列表；真正的"团体"判定走 **master `musicTags` 表**。

## 1. 弹窗本体

| 项 | 值 | 证据 |
|---|---|---|
| 打开方式 | `ScreenManager.Show2ButtonDialog<MusicSelectFilterDialog>(DialogType 14, OnFilterOK, OnFilterCancel, Layer_Dialog(4), DialogSize.Manual(3), allowCloseExternal: true)`，拿到实例后 `filterDialog.Setup(sortFilterData)` | `ScreenLayerMusicSelectBase.OnClickFilter` RVA `0x6614A98`；`Setup(MusicSelectSortFilterData)` RVA `0x4C94338` |
| prefab | `Assets/Resources/dialog/MusicSelectFilterDialog`，PrefabHierarchyObject **1732**（156 个 GameObject） | AssetRipper `/Search/View?q=MusicSelectFilterDialog` |
| 窗口 | **1168 × 1032**，`DialogSizeFitter`（comp 460：`dialogSize=2` / `buttonCount=2` / `tabCount=0`）；`SIZE_DIC = {336, 688, 1032}` | `DialogSizeFitter..cctor` RVA `0x4CB0274`（0x43A80000=336、0x442C0000=688、0x44810000=1032） |
| 底板 | `bg_base_r30_wh`（九宫格 r30）+ `bg_base_shadow_r30`（外扩 8） | comp 609 / 615 |
| 关闭钮 | 44 × 44，`btn_close_dbl`，锚右上，偏移 **(-15, -15)** | go 149 |
| 内容区 | `bg_base_r20_wh` **白 29.8%**；`RectMask2D` softness **(0, 50)**、padding (0,-25,0,-25)；右侧 `ScrollbarVertical` **6px**（`bg_base_round_vertical_h6_wh`，handle `..._h8_wh`），x = -20 | go 70 / 34 / 73 |
| 内容排版 | `VerticalLayoutGroup`：padding **L64 R64 T3 B0**、spacing **32**、`childControl*`=0 | comp 561 |
| 段标题 | 1032 × 44 = 文字 40 + 下划线 **4px**（`bg_base_wh`）；文字 **32px**，颜色 `#44446A`(0.2667,0.2667,0.4) | go 43/150/120 |
| 底部按钮 | 2 × `UIPartsCommonButton` **336 × 96**，`HorizontalLayoutGroup` spacing **24**，居中；`btn_round_h80_wh` + `eff_btn_round_h80_a70_wh`，标签 288×96 / 32px | go 147/144/81 |

## 2. 三段内容（这就是"弹出来到底有什么"）

标题与选项都是 `CustomTextMesh` 的 **wording key**（`useWordingKey: 1`），下表 key 是 prefab 真值，
日文是 prefab 里写死的默认串（运行时按 key 取词）。

### 2.1 クリア状況 — `clearFilterGroup`（单选，5 项）

- 标题：`WORD_FILTER_TITLE_CLEAR`（クリア状況）
- 容器：`UIPartsGroupFilterTextRadio`，`GridLayoutGroup` **cell 288×72 / spacing (56,24) / FixedColumnCount 3 / padding 8,8**
- 单项：`UIPartsRadioToggle` = 圆盘 **72²**（`btn_circle_h56_wh`）+ 勾 **36²**（`icon_radio_gn`，内缩 18）+ 标签 **256×50 @ +78**，32px

| 视觉顺序(兄弟序) | 标签 | wording key | 组索引 | `MusicSelectClearFilterType` |
|---|---|---|---|---|
| 1 | すべて | `WORD_ALL` | 0 | `None` |
| 2 | 未クリア | `WORD_NOT_CLEAR` | 1 | `NotClear` |
| 3 | 未フルコンボ | `WORD_NOT_FULL_COMBO` | 2 | `NotFullCombo` |
| 4 | 未AP | `WORD_SORT_NOT_AP` | **4** | `NotAP` |
| 5 | 未スコアS | `WORD_NOT_SCORE_S` | **3** | `NotScoreS` |
| —（不可见） | MASTER未解放 | `WORD_NOT_RELEASE_MASTER` | (5) | `NotReleaseMaster` |

⚠ 两个"坑"：

1. **视觉顺序 ≠ 索引顺序**。`indexToggles` 序列化顺序是
   `[すべて, 未クリア, 未フルコンボ, 未スコアS, 未AP]`（comp 461/560/619/**516**/**591**），
   而网格按**兄弟顺序**排：`すべて・未クリア・未フルコンボ` / `未AP・未スコアS`。
   也就是说"未AP"排在"未スコアS"前面，但索引 4 / 3 是反的 —— 索引仍然等于枚举值，没有 bug。
2. **`NotReleaseMaster` = 5 是残骸**：prefab 里有一个同名 radio（go 69，带 `WORD_NOT_RELEASE_MASTER`），
   但 `m_IsActive: 0`、也不在 `indexToggles` 里 → 运行时根本看不见、也选不到。

### 2.2 MV — `mvFilterTypeCheckBoxes`（复选，4 项）

- 标题：`WORD_FILTER_TITLE_MV`（MV）
- 容器：`UIPartsGroupFilterTextCheckBox`，`GridLayoutGroup` **cell 288×72 / spacing (56,16) / 3 列 / padding 8,8**
- 单项：`UIPartsCheckBox` = 方框 **56²**（`bg_base_r16_wh` + `bg_base_shadow_r16`）+ 勾 **44×34**（`icon_check_pk`）+ 标签 **200×50 @ +78**，32px

| 序号 | 标签 | wording key | 索引 | `MusicSelectMVFilterType` |
|---|---|---|---|---|
| 1 | 3DMVあり | `WORD_EXIST_MVFILTER_3D_MV` | 0 | `MV` |
| 2 | 2DMVあり | `WORD_EXIST_MVFILTER_2D_MV` | 1 | `MV2D` |
| 3 | 原曲MVあり | `WORD_EXIST_MVFILTER_ORIGINAL_MV` | 2 | `OriginalMV` |
| 4 | MVなし | `WORD_NOT_EXIST_MV` | 3 | `None` |

- 另有一个第 5 个复选框节点「すべて」（go 87 `Select4`，go 86 的 `UIPartsCheckBox`），但 **`m_IsActive: 0`
  且没被序列化进 `mvFilterTypeCheckBoxes`** → 运行时不参与（默认全勾就是"すべて"，不需要它）。
- 语义是**反过来的**：`SetupMVFilterType` 让每个框 `Check = !UnCheckedFilterMVTypes.Contains(i)`；
  确定时 `GetUnCheckedMVTypes()` 把**没勾**的索引收进数组。即"持久化的是被排除的类型"。

### 2.3 楽曲 — `musicFilterGroup`（单选，4 项）

- 标题：`WORD_MUSIC`（楽曲）
- 容器/单项同 2.1（`GridLayoutGroup` cell 288×72、spacing (56,24)、3 列）

| 序号 | 标签 | wording key | 索引 | `MusicSelectMusicFilterType` |
|---|---|---|---|---|
| 1 | すべて | `WORD_ALL` | 0 | `None` |
| 2 | 書き下ろし楽曲 | `WORD_NEWLY_WRITTEN_MUSIC` | 1 | `NewlyWrittenMusic` |
| 3 | APPENDあり | `WORD_EXIST_APPEND` | 2 | `ExistAppend` |
| 4 | クリエイト譜面あり | `WORD_SORT_EXIST_CUSTOM_SCORE` | 3 | `ExistCustomScore` |

- 第 4 项**可被禁用**：`SetupExistCustomScoreToggle(bool enable)` →
  `musicFilterGroup.EnableToggle(3)` / `DisableToggle(3)`，参数来自
  `MusicSelectSortFilterData.EnableExistCustomMusicScoreFilter`（RVA `0x4C943C0`；
  `EnableToggle` = `indexToggles[i].gameObject.SetActive(true)`）。
- 同格还有两个 `m_IsActive: 0` 的残骸（`NotScoreS` go 83、`NotReleaseMaster` go 46），不在任何组里。

## 3. 交互与数据

```text
OnClickFilter
  dlg = Show2ButtonDialog<MusicSelectFilterDialog>(14, OnFilterOK, OnFilterCancel, Layer_Dialog, Manual, true)
  dlg.Setup(sortFilterData)
      data.ClearFilterType       = filterData.ClearFilterType     (dialog+0xE8)
      data.UnCheckedFilterMVTypes= filterData.UnCheckedFilterMVTypes (0xF0)
      data.MusicFilterType       = filterData.MusicFilterType     (0xF8)
      SetupExistCustomScoreToggle(filterData.EnableExistCustomMusicScoreFilter)
      SetupMVFilterType()          # 逐框 Check = !UnChecked.Contains(i)
      StartCoroutine(InitializeComponent)
          clearFilterGroup.SelectedIndex  = (int)data.ClearFilterType
          musicFilterGroup.SelectedIndex  = (int)data.MusicFilterType
  # 单选的 onValueChanged → OnClearFilterSelectedIndexChanged(int) / OnMusicFilterSelectedIndexChanged(int)
  #   直接把 int 写回 +0xE8 / +0xF8（所以 index 就是枚举值）

OnClickOK  → data.UnCheckedFilterMVTypes = GetUnCheckedMVTypes(); base.OnClickOK()
OnFilterOK → sortFilterData.SetFilterType(data) → config.SetFilterType(...)+Save() → 重算列表/过滤器圆点
OnFilterCancel → 什么都不改
```

- `MusicSelectSortFilterDialogBase.SettingData` 的 4 个字段就是
  `ClearFilterType / SortType / UnCheckedFilterMVTypes[] / MusicFilterType`；
  **`SortType` 本弹窗不碰**（排序弹窗改），只是随结构体透传。
- 持久化（`MusicSelectSortFilterSettingData`，MessagePack）只存这 4 项 —— **分类 tab 不在这里**。
- 筛选圆钮的"已生效"两态用 `filterOnImage` / `filterOffImage` 切换，判据
  `MusicSelectSortFilterConfigBase.IsFiltered()`（`sortFilterData.IsFiltered()`）。

## 4. 筛选语义（选中之后按什么判）

`ScreenLayerMusicSelectBase.GetFilteredCellDataList`（RVA `0x6615970`）逐首判：

1. **free word**（搜索框）：`sortFilterData.IsFilteredFreeWord` 且 `MasterMusicAllModel.ContainsWord(词)`
2. **クリア状況**：`MusicViewDataBase.IsTargetClearFilterType(difficulty, clearFilterType)`（RVA `0x64700F8`）

   | clearFilterType | 判定 |
   |---|---|
   | 0 `None` | 恒 true |
   | 1 `NotClear` | `GetClearStatus() == NotClear` |
   | 2 `NotFullCombo` | `GetClearStatus() < FullCombo` |
   | 3 `NotScoreS` | `MusicViewDataBase.NotScoreS`（+0x70 的缓存布尔） |
   | 4 `NotAP` | `GetClearStatus() < AllPerfect` |
   | 其它 | false |

3. **MV**：`IsTargetMVFilterType(unCheckedTypes)`（RVA `0x646FE38`）——
   这首歌的 MV 类型集合与"未勾选集合"求交，命中即排除
4. **楽曲**：`MusicSelectMusicFilterType`
   - 1 `NewlyWrittenMusic` → `MasterMusicModel.IsNewlyWrittenMusic`
   - 2 `ExistAppend` → `MasterMusicAllModel.ExistsAppend`
   - 3 `ExistCustomScore` → `MusicUtility.HasSelectableCustomScore(dic, musicId, difficulty, liveType, isCustomRoom)`
   - 0 `None` → 不过滤

之后才排序（`GetSortedCellDataList`）→ `MusicSelect.UpdateMusicDataList`。

## 5. 左侧"按团体"页签到底怎么筛

### 5.1 它是什么

`ScreenLayerMusicSelectBase.categoryTabList` = **`MusicSelectUnitTabList`**（一个 `ListView`，
item 是 `MusicSelectUnitTab`）。单人页 `ScreenLayerFreeLiveMusicSelect` 派生。
prefab：`MusicSelectUnitTab` = **1118**、`MusicSelectUnitTabList` = **1117**。
item 尺寸 **112 × 100**（图标 **112 × 112**、偏移 y -12；文字 112 × 64.8）。

### 5.2 tab 列表怎么来

`ScreenLayerFreeLiveMusicSelect.CreateFilteringTabDataList()`（RVA `0x655F48C`）**按固定顺序拼**：

```
0  ALL            (すべて)
1  VOCALOID       (バーチャル・シンガー)
2  LIGHT_MUSIC_CLUB  (Leo/need)
3  IDOL           (MORE MORE JUMP!)
4  STREET         (Vivid BAD SQUAD)
5  THEME_PARK     (ワンダーランズ×ショウタイム)
6  SCHOOL_REFUSAL (25時、ナイトコードで。)
7  OTHER          (その他)
   ↓ 若 _shouldShowLimitedTermTab（0x192）：Insert(1, 16) —— LIMITED 紧跟 ALL
   ↓ 若 isTournamentInSession（0x108）：Add(8) —— TOURNAMENT 追加在最后
   ↓ 对 i = 1..MAX_MYLIST_COUNT：MyUtility.GetMyList(i) 非空 → Add(MyListIndexToCategoryType(i-1))
     → 10..14 = MY_LIST_1..5（空表不出现；15 = ADD_MY_LIST、17 = BGM 不在这条链里）
```

基类 `ScreenLayerMusicSelectBase.CreateTabData()`（RVA `0x661516C`）负责把数组塞给
`CategoryTabList.Initialize(...)`；如果 `needRecreateTab` 会先 `ClearTabDataList()`。

### 5.3 tab 长什么样

`MusicSelectUnitTab.GetKind(type)`（RVA `0x647D510`）：`(type-1) > 5` → **text**，否则 **icon**。
即 **只有 1..6（六个团）是图标 tab**，`ALL / OTHER / TOURNAMENT / LIMITED / BGM / MY_LIST_n` 都是文字 tab。

- 图标 tab：`AtlasImage.SpriteName = GetSpriteName(type, status)`，藏文字。
  sprite 族实测为 **`icon_tab_unit_{vs|ln|mj|vb|ws|25}_{on|off}`**
  （AssetRipper 里 12 张：`vs/ln/mj/vb/ws/25` × `on/off`；prefab 默认串是 `icon_tab_unit_ln_on`）。
  对应关系（按 `MusicTag` 语义，**推断**但唯一）：`vs`=VOCALOID、`ln`=LIGHT_MUSIC_CLUB(Leo/need)、
  `mj`=IDOL(MMJ)、`vb`=STREET(VBS)、`ws`=THEME_PARK、`25`=SCHOOL_REFUSAL。
- 文字 tab：`GetText(type)`（RVA `0x647D7EC`）
  - 1..6 → 返回空串（不需要文字）
  - `ALL` / `OTHER` / `TOURNAMENT` / `LIMITED` / `BGM` → `WordingManager.Get(key)`
  - `MY_LIST_n` → `MusicUtility.GetMyList(n).Name`；名字空则 `string.Format` 兜底
- 文字 tab 字号**按字符串长度**自适应（`SetTabText`，RVA `0x647DA00`）：
  长度 ≤3 → **22px**、=4 → **26px**、≥5 → **28px**（`String.length` @+0x10，不是"按 tab 种类"）。
- `setup` 里还对 `_tapEffectList` 做 `Where(active).ForEach(...)`，即只给开着的点击特效挂回调。

### 5.4 点一下发生什么

```
MusicSelectUnitTabList.OnClickTab(type) → OnSelectCategory(type)
ScreenLayerMusicSelectBase.OnSelectCellCategoryTabList(type)      (RVA 0x661548C)
    categoryTabType = CategoryTabCellDataList.Contains(type) ? type : CategoryTabCellDataList[0]
    UpdateMusicListView(categoryTabType, changeCategory: true)     (0x661472C)
        UpdateMusicDisplayCellData(category, sortFilterData)       # 见 5.5
        UpdateHighlightedCellData(change)
        musicSelect.UpdateMusicDataList(MusicDisplayCellDataList, difficulty, selectedCell)
        （然后走虚槽 0x2a8：刷新/保存）
```

### 5.5 从 tab 到歌曲 id：`MusicUtility.GetMusicIds`

`MusicUtility.GetMusicIds(allMusicViewDataBases, categoryTabType, difficultyType, defaultMusicType,
liveType, isCustomRoom, customScoreDic)`（RVA `0x4DCC344`）按 category 分派：

| category | 走哪个 | 语义 |
|---|---|---|
| `TOURNAMENT(8)` | `AssortTournamentMusic`（`0x4DCCCF0`） | 赛事指定曲 |
| `VOCALOID(1)` | `AssortVocaloidMusic`（`0x4DCCF30`） | 见下 |
| `LIMITED(16)` | `CreateLimitedTermMusicIds`（`0x4DCC8DC`） | 期间限定曲（`SpecialSeasonUtility.GetCurrentSpecialSeason`） |
| `MY_LIST_1..5(10..14)` | `AssortMyListMusic`（`0x4DCCAA4`） | 用户 mylist 里的曲 |
| 其它（含 ALL） | `AssortUseCategoryMusic`（`0x4DCD18C`） | 见下 |

`AssortUseCategoryMusic` 的实际判定（这是"按团体筛选"的正身）：

```
tag = GetMusicTagByCategoryType(category)          # RVA 0x4DCC20C，查表
rows = MasterDataManager.GetMasterMusicTags(tag)   # master 的 musicTags 表
foreach row in rows:
    if !musicDict.ContainsKey(row.musicId)      continue
    v = musicDict[row.musicId]
    if !v.IsPublished                            continue
    if category != LIMITED && !v.ExistsDifficulty(difficulty) continue   # 当前难度没谱就不出现
    if musicFilter == ExistCustomScore && !HasSelectableCustomScore(v) continue
    add row.musicId
# 例外：category == ALL(0) 或 BGM(17) 时，还会把 Mysekai 的 BGM 唱片（IsSoundTrack）并进来
```

- `GetMusicTagByCategoryType` 查表（`.rodata` @`0x1F2A3F8`，实测 18 项）：
  `[all, vocaloid, light_music_club, idol, street, theme_park, school_refusal, other, all…(占位), all, …, BGM]`
  —— 即 `CategoryTabType` **按名字一一对应到 `MusicTag`**，没有特殊映射。
- `AssortVocaloidMusic` 比通用路径多一条：还要求 **`MasterMusicAllModel.IsForVirtualSinger`**
  → "バーチャル・シンガー"页 = `musicTags` 打了 `vocaloid` **且** `isForVirtualSinger = true` 的歌。
- 另外 `UnitType → MusicTag` 也有一张表（`0x1F29BDC`）：
  `{none→other, piapro→vocaloid, theme_park→theme_park, idol→idol, street→street,
    light_sound→light_music_club, school_refusal→school_refusal, any→all}`，
  反向 `UnitType → CategoryTabType` 在 `0x1F2A500`。这套是给别的界面（如角色/活动）用的，选曲左侧 tab 走的是
  `CategoryTabType → MusicTag`。
- 结论：**"团体"不是算出来的，是 master `musicTags` 的 tag 行**；一首歌可以同时有多个 tag
  （所以 ALL 之外，团页与"その他"不互斥）。难度过滤是同一层顺手做的（`ExistsDifficulty`）。

## 6. 照抄时要注意的几条

1. `DialogSize.Manual` + `DialogSizeFitter(dialogSize=Large)` = 1168×1032；窗口高度**不是**内容高度，
   内容超出走滚动（`RectMask2D` 上下 50px 软边）。
2. 内容宽度 **1032**、网格 3 列 **288 + 56**（间距 56 不是 64、不是 16）；行距单选 24 / 复选 16 ——
   两个网格**不一样**，抄的时候容易混。
3. 单选组索引 = 枚举值，但**兄弟顺序与索引顺序可以不一致**（未AP/未スコアS 就是），
   要按 `indexToggles` 的序列化顺序对齐语义，不要按视觉顺序。
4. MV 存的是"未勾选集合"；默认空数组 = 全勾。
5. `実装上の残骸`（`NotReleaseMaster` 单选、MV 的「すべて」复选、楽曲里的两个 radio）都是
   `m_IsActive: 0`，别当功能抄。
6. 左侧 tab：图标 tab 只有 6 个团；其余是文字，字号按**字符串长度** 22/26/28。

## 7. 证据索引（RVA / 资产）

| 方法 / 数据 | RVA | 备注 |
|---|---|---|
| `ScreenLayerMusicSelectBase.OnClickFilter` | `0x6614A98` | 开弹窗（DialogType 14） |
| `MusicSelectFilterDialog.Setup(MusicSelectSortFilterData)` | `0x4C94338` | 填 3 个组 + 协程 |
| `MusicSelectFilterDialog.SetupMVFilterType` | `0x4C943F4` | `Check = !UnChecked.Contains(i)` |
| `MusicSelectFilterDialog.SetupExistCustomScoreToggle` | `0x4C943C0` | index 3 的 `SetActive` |
| `MusicSelectFilterDialog.InitializeComponent.MoveNext` | `0x4C947D4` | `SelectedIndex = (int)枚举` |
| `MusicSelectFilterDialog.OnClickOK` | `0x4C94628` | 收"未勾选"集合 |
| `MusicSelectFilterDialog.GetUnCheckedMVTypes` | `0x4C94654` | 遍历 4 个 checkbox |
| `ScreenLayerMusicSelectBase.OnFilterOK` | `0x6614C24` | 落盘 + 刷新 |
| `ScreenLayerMusicSelectBase.GetFilteredCellDataList` | `0x6615970` | 4 层过滤 |
| `MusicViewDataBase.IsTargetClearFilterType` | `0x64700F8` | §4 表 |
| `MusicViewDataBase.IsTargetMVFilterType` | `0x646FE38` | |
| `MusicUtility.GetMusicIds` | `0x4DCC344` | 分派 |
| `MusicUtility.AssortUseCategoryMusic` | `0x4DCD18C` | 团体 tag 主体 |
| `MusicUtility.AssortVocaloidMusic` | `0x4DCCF30` | + `IsForVirtualSinger` |
| `MusicUtility.GetMusicTagByCategoryType` | `0x4DCC20C` | 查表 @`0x1F2A3F8` |
| `ScreenLayerFreeLiveMusicSelect.CreateFilteringTabDataList` | `0x655F48C` | tab 顺序 |
| `MusicSelectUnitTab.GetKind` / `GetText` / `GetSpriteName` | `0x647D510` / `0x647D7EC` / `0x647D520` | 图标 vs 文字 |
| `MusicSelectUnitTab.SetTabText` | `0x647DA00` | 字号 22/26/28 |
| `ScreenLayerMusicSelectBase.OnSelectCellCategoryTabList` | `0x661548C` | 选中 → 重建 |
| `ScreenLayerMusicSelectBase.UpdateMusicListView` | `0x661472C` | |
| `DialogSizeFitter..cctor` | `0x4CB0274` | SIZE_DIC 336/688/1032 |
| prefab `MusicSelectFilterDialog` | PathID **1732** | 156 个 GO |
| prefab `MusicSelectUnitTab` / `…TabList` | **1118** / **1117** | |

> 复现：`il2cpp_disasm.py`（VA 直读 `.text`）+ `il2cpp_where.py`；prefab 用 AssetRipper
> `--headless --port 17654`，`POST /LoadFolder path=D:/Tools/rea/work/jp/GameData`，
> 再 `/Assets/View?Path=…` 与 `/Assets/Yaml?Path=…` 取 rect / 组件。
> 注意 `.so` 里的 `adrp+ldr` 拉到的是 **metadata usage 槽**（运行时才填），
> 想拿字符串字面量要走 metadata 的 string-literal 表，别在 `.so` 里硬读。

## 8. 我们这边的落地（2026-11-23，2026-11-27 统一成矢量卡）

照 §0~§6 抄成了三件事，全部实测过（下面有数）。

**① 弹窗**（`game/SongSelect.cpp` 里 `gFilterOpen` 那一段）
几何按 §1：1168×1032 等比烘进 1920×1080 画布（工作窗口 1366×768 时实际 ≈ 832×736），
内容块白 29.8%、三段标题 32px `#444466` + 4px `#A7A7BC` 下划线、
单选 72 白盘 + 36 薄荷点、复选 56 方框 + 44×34 `#FF77AC` 勾、
底部 決定/キャンセル 各 336×96（白胶囊 + `eff_btn_round_h80_a70_wh` 光晕）。
网格用 §6 的真值：单选格 288×72 间距 (56,24)、复选格 288×72 间距 (56,16)、3 列固定。

> **底板/关闭 X/复选/单选/按钮不再用位图。** 2026-11-23 那版是"`ui::beginCard`
> 的矢量卡体 + 又盖一层 `bg_base_r30_wh` 九宫格 + `bg_base_shadow_r30`"，
> 等于每帧两个卡体两套阴影（r16 的角从 r30 板下面露出来，光晕也叠了一层）。
> 而且它点名的 12 张 sprite 里 `bg_base_r20_wh` / `bg_base_r16_wh` /
> `bg_base_shadow_r16` / `btn_round_h80_wh` **在 `assets/select/` 下根本不存在**
> （前三个只有 `assets/final/` 有，`bg_base_shadow_r16` 全仓库没有），
> 所以内容区/复选框/按钮一直是静默走手绘回退的。2026-11-27 起整张卡统一走
> `ui::beginCard`（和设置/资料/猜歌/键位卡同一套），只剩
> `eff_btn_round_h80_a70_wh`（一束光晕，矢量画不出来）还是位图。


**② 三条筛选**（`passesDialogFilters`，本文件 5413 行附近）
- クリア状況 / 楽曲 按**当前难度**的那一张谱（`groups[gi].idx[diffIndex]`），
  对应原作的 `ExistsDifficulty` 之后取那一个 `MusicViewDataBase`。
- MV 与难度无关，读 master `musicCategories` → `music-categories.json`
  （479 首；3D 194 / 2D 257 / 原曲 MV 161；`image` 行 = MVなし 已丢弃）。
- `mvUnchecked` 是**位集**：bit0=3D、bit1=2D、bit2=原曲、bit3=MVなし；
  `check = !(mvUnchecked & (1<<i))`，一首歌"有"的类型里只要有一个还勾着就留下。
- 我们**没有**自制谱面功能，所以 楽曲 的「クリエイト譜面あり」按原作
  `SetupExistCustomScoreToggle` 的套路画成灰的（`musicFilter == 3` 选不出来）。
- `§6.5` 的残骸（`NotReleaseMaster`、MV 的「すべて」复选、楽曲多出来的两个 radio）都没抄。

**③ 落盘**
`userSettings.clearFilter` / `mvUnchecked` / `musicFilter` 三个字段，和 `unitTab` 同一套读写
（`game/SongSelect.cpp` 的设置块 + `persistUserData()` 的"变没变"比较）。
右上的筛选钮**点决定才提交**；キャンセル、关闭 X、关窗口都丢弃草稿（原作 `OnClickOK` 语义）。

**验证**（`--screenshot --select-filter` 无头跑，`cppsekai.log` 的
`[select] list rebuilt: N visible of M`；本机 `charts/` 只有 55 张谱 = 11 组）：

| 设置 | 结果 |
|---|---|
| 全默认 | 11 / 11 |
| `mvUnchecked=15`（四个全不勾） | **0** / 11 |
| `mvUnchecked=7`（只留 MVなし） | 3 / 11 |
| `mvUnchecked=8`（只留"有 MV"一类） | 8 / 11（3 + 8 = 11 ✅） |
| `musicFilter=1`（書き下ろし） | 3 / 11 |
| `musicFilter=2`（APPEND あり） | 0 / 11（本机没有 APPEND 谱） |
| `clearFilter=1`（未クリア） | 9 / 11 —— 两份 `cleared` 记录被藏掉 |
| `clearFilter=2`（未フルコンボ） | 9 / 11 —— 两份 `fullCombo` 被藏掉 |
| `clearFilter=3`（未スコアS） | 11 / 11 —— 那两份分数都不是 S，留下 |
| `clearFilter=4`（未AP） | 10 / 11 —— 只藏掉那一份 `allPerfect`（证明 AP 位真的接到了筛选） |

> 后四行是往档案里塞两条假记录（`0451_expert` AP、`0577_expert` 只 FC）跑出来的，
> 跑完已还原。空白档案时 `clearFilter` 四个值都是 11/11，看着像"没生效"，其实是没记录可藏。

**两条我们自己加的、原作没有的东西**（都是给玩家解释"为什么列表变短了"）：
- 顶栏筛选钮的粉点（`Dot` 48×36）原来只看团体页签和搜索框，现在**三条弹窗条件也算**；
- 列表被筛空时的空状态按钮改成「清除全部筛选」，一次清掉页签 + 搜索 + 三条弹窗条件
  （只清页签的话按钮看起来像坏了）。
