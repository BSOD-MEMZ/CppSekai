# STORY-UI.md — 剧情风格组件 & 应用内新手教学

> 目标不是做剧情播放器，而是做一层**能被新手教学复用的「场景层」**：
> 一句一句说话 + 选项 + 指认界面元素。以后真要放剧情，同一层直接拿来用。
>
> 资源已落地：`assets/story/ui/`（12 张，全部来自 sekai-stories.pages.dev 的 `/img/`）。
> 版式示意见 `docs/story-ui-mock.png`（用真实资源 + 真实坐标合的，不是效果图）。
>
> **2026-10-18：已实现**。模块、脚本、触发、chartdl 外部引导都在跑，实机截图见
> `.workbuddy/shots/story_*.png`；实现细节与踩到的坑在文末「实现记录」一节。

## 0. 结论

1. **只做 3 个视觉组件**：对话框、场景标题（时间/地点）、选项。其余（暗角、网格）是氛围层，教学用不上。
2. **资源全是 1920x1080 满幅 PNG，整张画，别切九宫格**。切了圆角和投影会糊，而项目本来就有 1920 虚拟画布（`game/Result.cpp:183` 的 `Canvas`）。
3. **两个新模块就够**：`game/StoryUi.*`（纯绘制）+ `game/Story.*`（脚本运行时）。
   教学触发/存档先塞在 `Story.cpp` 里，等真有第二个消费者再拆 `Tutorial.*`。
4. **先做 P0**：对话渐变条 + 打字机 + 点击推进 + 压暗。一天能看到东西，再谈选项和高亮。
5. 真正的难点不是画框，是**中文排版**：ImGui 自带换行是给英文写的（`third_party/imgui/imgui_draw.cpp:5387` 自己写着
   "Simple word-wrapping for English, not full-featured"），标点会落到行首；而且打字机**必须先折行再逐字显示**，
   否则每滚一个字整段重排，文字会跨行跳。

## 1. 资源清单（已下载，含实测像素）

| 文件 | 尺寸 | 实际内容 bbox | 用途 | 用不用 |
|---|---|---|---|---|
| `Dialogue_Background.png` | 1920×1080 | 全宽渐变带 0,771 – 1920,1080（上沿细线 y≈840） | 新式底部渐变条 + 白字描边（参考站成品就是这个） | **默认用这个** |
| `Dialogue_Background_Classic.png` | 1920×1080 | 盒 154,734 – 1766,1053；名牌 220,742 – 888,814 | 旧版圆角白框 + 烤进图里的名牌 | 备用（复刻旧 UI 才用） |
| `Dialogue_Background_MYSEKAI.png` | 1920×1080 | 160,705 – 1761,1016 | 烤森风 | 不用 |
| `Choices_Background.png` | 1920×1080 | **左右两个** 522×98 胶囊：239–760 / 1159–1680，y 489–586 | 两选一（正好是教学最常见的形态） | 用 |
| `Choices_Background_Classic.png` | 1920×1080 | 满幅不透明 | 旧版整块底 | 不用 |
| `SceneText_Background.png` | 1920×1080 | 422,480 – 1499,600（高 120） | 居中两行「时间 / 地点」 | 用 |
| `SceneText_TopLeft.png` | 1920×1080 | 1,31 – 821,94 | 左上角渐变标签（章节名/地点） | 可选 |
| `vignette.png` | 1920×1080 | 满幅（只有约 1/3 像素是全透明） | 电影黑边暗角 | 只在纯剧情模式用，教学别用（太重） |
| `vignette-round.png` | 2340×1080 | 满幅（同上） | 宽屏版暗角 | 同上 |
| `grid.png` | 1920×1080 | 满幅，很淡（5% 区域有像素） | 网点纹理，压平背景 | 可选 |
| `SceneText_Background_Classic.png` / `SceneText_TopLeft_Classic.png` | 1920×1080 | — | 旧版 | 不用 |

**刻意没下的**：`/background_compressed/`、`/background_special/` 那 60 多张场景背景（不是 UI）、
`menu*.png` / `Preview-*.png`（站点自己的插画和截图）、`characters/`（Live2D 立绘）、
`/fonts/FOT-RodinNTLGPro-*.otf`（**Fontworks 商业字体，别碰**，继续用 `game::bodyFont()`）。

> 版权：这套图来自 `lezzthanthree/SEKAI-Stories`。代码是 GPLv3，但它的 LICENSE 白纸黑字写着
> **assets 不属于 GPL、仅限个人/教育用途**；图本身是 SEGA 资产。处理口径和现有 `assets/**` 一致：
> 本地用、可以进库（库里已有 257 个同类文件），**绝不进 Release 包**（`COPYRIGHT.md` 第 31 行）。

## 2. pjsk 的剧情结构（2026-10-18 从官方 master DB 实测）

目录只有三层，第四层（台词）在 assetbundle 里，`scenarioId` 是那把钥匙：

```
组合(unit) → 章(chapter) → 话(episode) → scenarioId → 逐句 action（台词/动作/演出）
```

| 表 | 实测规模 | 层级 |
|---|---|---|
| `unitStories` | 6 个组合 × 1 章 × 20~21 话 | light_sound / idol / street / theme_park / school_refusal / piapro |
| `eventStories` | 218 期，共 **1776** 话（每期 2~15 话） | 活动 → 话 |
| `cardEpisodes` | **2804** 段 | 卡 → 前篇/后篇（`first_part` / `second_part`） |
| `actionSets` | **2895** 条 | 区域对话：`scriptId` → `scenarioId` |
| `specialStories` / `liveTalks` | — | 特殊剧情 / 演出前后对话 |

阅读界面的组成（对照 `pjsk.moe/zh-cn/story/unit/`）：

- 全屏背景图 + Live2D 角色（**教学不需要**）
- 底部对话框：名字牌 + 正文 + ▼ 推进提示
- 场景标题：黑屏/转场时居中两行「19:30 / 教室」
- 选项：两列胶囊（就是 `Choices_Background.png` 那两个坑）
- 右上：自动 / 跳过 / 记录（backlog）；左上：返回

**对我们的启示**：真正值得抄的只有一句话——「一话 = 一串有类型的 action」。
官方 master DB 只是目录，台词拿不到（编辑器也是手填的），所以**教学脚本自己定义格式**，
别去兼容 `actionSet`，那套字段里 90% 是抽卡奖励和 Live2D 动作。

## 3. 组件规格（1920×1080 虚拟坐标）

### 3.1 对话框 `dialogue`

**用 `Dialogue_Background.png`（底部半透明渐变条），不是 Classic 白框。** 参考站的成品图
（`.workbuddy/shots/preview1.png`）里没有白框：名字在渐变条上沿之上，正文是白字 + 深色描边。

以下数字不是量出来的，是**从站点 JS 里抄的**（`ss.js` 的 `dialogueBoxesSetupData.default`，
1920x1080 画布，Pixi 文本左上角对齐）：

| 元素 | 位置 | 字号 | 填充 | 描边 |
|---|---|---|---|---|
| 名字 | (225, 775) | 44 | `#EBEBEF` | `#5D5D79`，thickness 8 |
| 正文 | (245, 845) | 44 | `#FFFFFF` | `#5D5D79`，thickness 8，行高 55，换行宽 1300 |

- Pixi 的 `strokeThickness: 8` 是**以字形轮廓为中心**的描边，往外只溢 4px；ImGui 没有描边，
  用 16 个方向各偏 4px 的副本叠出来（`outlineText`）。
- 参考图里名字下面那条横线**不是 UI 画的**，是渐变条自己在 y≈840 的上沿细线 —— 别多加一条
  下划线（我加过，明显是两回事）。
- 渐变条整张 1920x1080 直接画。它最深只有 **50%**（`2,0,32,128`），那是为明亮的剧情背景
  设计的；选曲界面太暗，所以叠两层（≈75%）并且对话页压暗到 0.62。
- 两个布局共用同一张图、同一条垂直节奏（名字 775 / 正文 845），只改 x、宽度和字号：
  `Full`（245 / 1300 / 44）、`Left`（120 / 640 / 38，右侧手机面板要让出来）。
  **设置卡片那一步也用 `Full`**：早先给它单独画过一块深色面板，和参考站一比就是「另一套 UI」，
  已经删掉。代价是渐变条会盖住卡片底部那几行（含「关闭」）——那一步的出口是提示条上的
  「完成」按钮，不再依赖卡片的关闭键。
- **不要给渐变条垫白色不透明底**：它是半透明设计，垫了底就变成一块死白。
- 名字用 `game::boldFont()`（系统没有粗体时回退到正文）。
- Classic 那张白框（`Dialogue_Background_Classic.png`）保留在库里，留给以后真要复刻旧版 UI
  的场合，教学不用它。

### 3.2 场景标题 `sceneTitle`

- `SceneText_Background.png` 居中，文字锚点 (960, 540)，**字号 44**、白色、行距 1.2
  （站点：`sceneCenterTextsSetupData.default`）。行距不能再大：那块色带只有 y 480..600。
- 淡入 0.35s → 停 1.6s → 淡出 0.35s；期间吞掉推进输入（pjsk 就是这么干的）。
- 左上的 `SceneText_TopLeft` 只在「章节名」场合用，教学先不接。

### 3.3 选项 `choices`

- 位置抄站点（`choicesTextsSetupData.default`）：两个 **522×98** 的胶囊，中心
  **(500, 537) / (1420, 537)**，文字 32px、`#444466`。**只支持 2 个**；3 个以上自己在
  489 起往下堆 120 间距。
- **画法用 `ui::capsuleDraw`**（设置卡片里 `capsuleButton` 内部调的同一个函数），不再贴
  `Choices_Background.png` 再自己叠一层薄荷色。之前 hover 是「薄荷色填充 + 薄荷描边」，
  整套 UI 里没有第二个控件长那样，玩家一眼就看出是另写的。
  现在 hover = 白→`#f3f3f9` + 放大 2%，按下 = 反白 + 薄荷字，和设置页完全一致；
  手柄焦点也照抄 `ui::PadScope` 的那个环（46,186,164 / 2px）。

### 3.4 压暗与高亮 `dim` / `highlight`

- 教学统一先压 `IM_COL32(0,0,0,115)`（约 45%）。
- 高亮 = 用 4 块压暗矩形在目标矩形四周挖洞 + 一圈 3px 薄荷描边，**不用暗角图**。
  描边外面是**水波**：3 圈薄荷圆角矩形从洞口向外扩散（每圈 1.8s、相位差 1/3），
  每圈一层宽 8px 的淡光晕 + 一条 2px 的亮边，alpha 线性衰减。原来只有 `sin` 呼吸，
  读起来像在闪，不像波。
- 目标矩形来自锚点注册表（见 5.2），取不到就退化成纯对话，不许崩。

## 4. 代码架构

```
game/StoryUi.hpp/.cpp    组件层：加载 3~4 张纹理；dialogueBox / sceneTitle / choiceRow /
                         dim / highlight / vignette。无状态，调一次画一帧。
game/Story.hpp/.cpp      运行时：JSON 解析、步骤状态机、打字机、输入、当前脚本。
```

`main.cpp` 只加三处：`story::update(dt)`、`story::draw(canvas)`、输入闸门。

API 草案（照着现有风格写，不引新依赖，JSON 用 `nlohmann/json`，SongSelect 已经在用）：

```cpp
namespace story {

struct Step {
    enum class Kind { Say, SceneTitle, Choose, Highlight, Wait, Goto };
    Kind kind = Kind::Say;
    std::string who;      // 发言者；空 = 旁白（不画名牌）
    std::string text;     // 正文
    std::string anchor;   // Highlight：ui 锚点 id
    std::string target;   // Goto
    std::vector<std::string> options;
    float hold = 1.6f;    // SceneTitle / Wait 的秒数
};

bool load(const std::string& path, std::string& err);  // 一次读全部脚本
bool start(const char* id);                            // 已在跑 / 找不到 → false
bool active();
void update(float dt);                                 // 推进打字机、计时
bool handleInput();                                    // 返回 true = 这一帧输入被吃掉
void draw(const Canvas& c, ImFont* font);              // Canvas 见 Result.cpp:183
void stop();
bool finishedOnce(const char* id);                     // 给存档用

} // namespace story
```

状态机只有三态：`Typing → Waiting(文字已出完) → 下一句`。选项只在 `Waiting` 且当前步是 `Choose` 时出现。

**打字机**：先 `wrapLines()` 折成 `vector<{begin,end}>`，再按码点露出。CJK 28 字/秒，标点额外停 80ms。
点击/空格第一次 = 立刻补完全句，第二次 = 下一句（pjsk 手感，别反过来）。

**折行**：包一层 `font->CalcWordWrapPosition(size, ...)`，然后补禁则——
若下一行首字是 `。、，．）」』！？…` 就往前收一个字。缓存 `(text, width) → lines`，别每帧算。
每次改中文正文都会踩这坑，写的时候就把缓存做进去。

## 5. 接缝清单（每条都是现成的坑）

### 5.1 存档
`userdata.json` 的 `settings` 段加 `tutorialSeen: ["first_launch"]`。
**`SongSelect.cpp` 的 `loadUserData`（:1000）和 `saveUserData`（:1180）必须同时改**，漏一处静默丢设置。
脚本跑完写一次，不要在每帧写（会把 SSD 当草稿纸）。

### 5.2 锚点注册表
加到 `game/Ui.*`：

```cpp
void setAnchor(const char* id, ImVec2 lo, ImVec2 hi);  // 画到哪个控件就在旁边报一次
bool anchorRect(const char* id, ImVec2* lo, ImVec2* hi);
void beginFrame();                                     // 主循环每帧清表
```

`SongSelect` 的曲目列表、随机/设置按钮、开始按钮各报一个（`select.list` / `select.random` / `select.play`）。
表每帧清空，所以底层界面必须在教学期间继续画（压暗但不能停绘）。

### 5.3 输入闸门
- 鼠标：教学层是一个全屏 `ImGuiWindowFlags_NoBackground` 的窗口，ImGui 按 z 序判定 hover，
  底层按钮自然点不到，**不需要到处加 `if`**。
  但**光有窗口不够**：ImGui 的 hover 是从窗口链表的**队首**往下找的，而玩家点过的窗口
  （选曲界面自己的输入层）会被提到队首。原实现带了 `NoBringToFrontOnFocus`，教学层就永远
  待在它下面 —— 对话框因为用的是全局 `IsMouseClicked` 还能推进，选项胶囊和跳过键却
  `IsItemHovered()` 永远是 false，**整层看着正常、实际是点击穿透的**。
  现在非 passthrough 的步骤每帧 `ImGui::SetNextWindowFocus()`（passthrough 不抢，
  否则设置卡片点不动）。
- 键盘：`escapePressed`（main.cpp:7744 那条链）加上 `&& !story::active()`，否则选曲界面按 ESC 直接退游戏。
  F5 刷新、空格预览同理。推进键是 **空格 / 回车 / 小键盘回车 / 左 Ctrl / 右 Ctrl**——节奏游戏
  玩家打完一首习惯把空着的那只手放在 Ctrl 上，只认空格会让人觉得「按了没反应」。
- 手柄：选项和推进都要进 `ui::PadScope` / `padNav`，这是老问题（自绘控件键盘够不着），别又漏一次。

### 5.3.1 跳过键

右上角那颗是 **pjsk 的圆形跳过键**，和开演前空白卡上的「跳过」是同一个控件
（`game/Intro.cpp` 的 `SKIP_BTN_*` ↔ `storyui::skipKey`，40px 圆盘 + 三层投影 +
`ui_skip` 图标染 `#3D3C5C`）。命中区是**圆**，不是外接矩形；点中就直接 `finish()`。
**它常驻**，包括 passthrough 的步骤。所以命中判定不能用 `InvisibleButton`（那一步的窗口是
`NoInputs`），走的是 `MousePos + IsMouseClicked` 的全局判定。
两个地方的手感参数要一起改，别只改一边。

### 5.3.2 顶栏要一直能用

教学层是全屏窗口，**默认会把选曲界面顶栏（刷新 / 音乐商店 / 猜歌 / 设置）的点击吃掉** ——
玩家点「音乐商店」没反应，十有八九就是这一层还在。两件事一起做才通：

1. `ImGui::SetWindowHitTestHole(win, (0,0), (displayW, c.y(76)))`：把顶栏那条带挖成
   **命中孔**，鼠标落到那里时 ImGui 会把 hover 交给下面的选曲窗口
   （`ImGuiWindow::HitTestHoleSize`，只有 `imgui_internal.h` 里能碰，没有公开 setter）。
2. 鼠标在顶栏带里时**不要** `SetNextWindowFocus()`：每帧抢焦点会在玩家按下按钮的那一帧
   把 press 取消掉，表现就是「按钮亮着但点不动」。
3. 顶栏带里的点击也不当「推进台词」（`inHeaderBand`）。

`dl_intro`（"点右上角的音乐商店"）另外还要 `passthrough`：那一步在请求玩家点它，
不放开输入等于让人点空气。

### 5.3.3 「完成」按钮

`"done": true` 的步骤会在提示条右边挂一颗薄荷胶囊。它和设置卡片里的按钮**同一套绘制**
（`ui::capsuleDraw`，`capsuleButton` 内部也调它，不是另写一份），按下去会顺手发
`closeSettings` 并 `next()`。因为这一步是 passthrough，窗口 `NoInputs`，所以命中同样是
全局鼠标判定——别改成 `InvisibleButton`，那样在高亮步骤里点不动。

### 5.4 演奏中教学
要 `paused = true` + `audio.pause()`，但**不要走暂停卡片那条路**（那里会触发 3-2-1 倒计时和房主权限检查）。
单独一个 `tutorialPause` 标志，跟 `paused` 或起来用，结束时直接解冻、不倒数。

### 5.5 模态互斥
教学开始前把设置卡片、暂停卡、EULA 全关掉；教学期间 `ui::beginCard` 那边也要挡住（否则两层输入打架）。

## 6. 脚本格式

`assets/story/scripts/startup.json`——纯内容，纯文本，可以入库：

```json
{
  "version": 1,
  "scripts": [
    {
      "id": "first_launch",
      "steps": [
        { "type": "scene", "text": "19:30\n教室", "hold": 1.6 },
        { "who": "助手", "text": "欢迎来到 CppSekai。左边这一列是曲目列表。" },
        { "type": "highlight", "anchor": "select.list",
          "who": "助手", "text": "先点一首你想打的歌。" },
        { "who": "助手", "text": "选好之后，右边挑难度，再按开始。" },
        { "type": "choose",
          "text": "准备好了吗？",
          "options": ["开始吧", "再说一遍", "去设置看看"] }
      ]
    }
  ]
}
```

- `type` 省略 = `say`，这样手写脚本时 80% 的行最短。
- 选项跳转用 `target`（步骤 `id`）而不是下标，改脚本时不会错位。
- **条件/触发不放 JSON**：`{when, once}` 这种逻辑写在 C++ 里（比如「首次进选曲界面」），
  JSON 只当台词表。这样以后换语言/换触发时机都不用动内容。

## 7. 分期

| 阶段 | 内容 | 验收 |
|---|---|---|
| **P0** | `StoryUi` 的对话框 + 压暗 + 打字机 + 点击推进；脚本先硬编码一个 5 步场景 | `--story-test first_launch --screenshot out.png --no-party` 拍到那张图，和 `docs/story-ui-mock.png` 对得上 |
| **P1** | JSON 加载 + 选项 + 场景标题 + 锚点高亮 | 选曲界面上真点得到「开始」，高亮跟着按钮走，窗口缩放后仍然对准 |
| **P2** | `tutorialSeen` 存档 + 首次启动触发 + 演奏中暂停式教学 | 删 `userdata.json` 重开只出现一次；按 ESC 不退游戏 |
| **P3**（可选） | 真剧情阅读器：背景图、淡入淡出、backlog、自动/跳过 | 同一套运行时，只多一个全屏背景层 |

CLI 加 `--story-test <id>`（无头开），和 `--drop-test` / `--chartdl-test` 一个路子；
需要的话再加 `--story-fast` 把打字机调成 200 字/秒方便截图。

## 8. 坑（按踩到的概率排）

1. **白字在亮背景上会糊**：正文必须带深色描边（八方向 2px），渐变条本身只压暗一点点 ——
   参考站的场景图都很暗，选曲界面不是。
2. **中文折行**：ImGui 只会「超宽硬切」，标点会跑到行首；且打字机不能边打边重排。
3. **显存**：每张 1080p RGBA 约 8MB。只加载实际用到的三张（Dialogue/Choices/SceneText），
   别把 Classic / MYSEKAI / TopLeft 一起读进来。
4. **名字没有牌**（渐变条版）：直接左对齐写在横线上方，别再按 Classic 的名牌去算宽度。
5. **`Choices_Background.png` 只有两个坑**：第三个选项自己画，别拉伸。
6. **窗口缩放**：所有坐标必须过 `Canvas`（`Result.cpp:183`），直接写像素的话 4:3 窗口会错位。
7. **字体**：继续用 `game::bodyFont()`；站点那套 FOT-Rodin 是 Fontworks 商业字体，别顺手抄。
8. **Release 打包**：`assets/story/` 不能进 `dist`（`COPYRIGHT.md` 第 31 行的口径）。


---

## 9. 实现记录（2026-10-18）

### 9.1 文件

| 文件 | 作用 |
|---|---|
| `game/StoryUi.*` | 组件层：对话框 / 场景标题 / 选项 / 设备卡片 / 图片卡 / 聚光灯。无状态 |
| `game/Story.*` | 运行时：JSON 脚本、分支、打字机、等待条件、手柄焦点 |
| `platform/ChartDlGuide.*` | Win32：找 chartdl 窗口、读控件矩形与状态、置顶点击穿透的圈选浮层 |
| `assets/story/scripts/tutorial.json` | 31 步教学脚本（含分支与异常分支） |
| `assets/story/img/` | **已删**：教学不再用图片（键位页画的是 `assets/mmw/stage.png` 的裁剪） |
| `.workbuddy/tools/story_assets.py` | 两张卡片的生成脚本：seewo 剪屏幕（保留原图 alpha）+ 12 键示意图 |

### 9.2 渲染/坐标上的坑（都是实测踩过的）

1. **视觉必须画在前景 draw list。** 这个项目把 HUD、淡入淡出、打击特效画在
   `ImGui::GetForegroundDrawList()`（渲染在**所有窗口之上**）。剧情层如果画自己窗口的
   draw list，会被整个盖住 —— 表现是「日志有顶点、代码在跑、屏幕什么都没有」。所以
   `Story::draw` 的窗口只用来 `Begin` + 摆 `InvisibleButton`，所有图形走前景层。
2. **窗口/画布用 `io.DisplaySize`，不是 `windowW/windowH`。** 后者是渲染目标尺寸，
   开了 renderScale 或截图分辨率不同时，虚拟画布会被缩放两次（文字跑到屏幕外）。
3. **虚拟单位和像素不能混着用。** `storyui::imageCard` 的说明文字一开始把虚拟的
   `cx = 960` 直接当屏幕 x 用（`top` 却已经是像素），结果说明条整体偏到右边 ~400px。
   规律：`c.p()/c.x()/c.y()` 的输出是像素，`layoutText/font->CalcTextSizeA` 的返回值
   也是像素，只有**画布坐标**是虚拟单位。
4. **自定义控件的 item rect 不等于控件本身。** `ui::slider` 内部是一串 InvisibleButton，
   调用后 `GetItemRectMin/Max` 拿到的是最后一个 **+ 按钮**。现在滑杆自己收一个
   `anchorId`（最后一个参数），由它 `setAnchor` 出整行的真实矩形——别在外面拿
   `GetCursorScreenPos()` 猜高度，+/- 按钮的下沿在 `trackY + btnSize/2`，猜 40px 会切掉一半。
5. **spotlight 的洞会外扩 6px**（`pad`），锚点别贴着标签下沿，否则圆环会切到上一行的字。

### 9.3 脚本字段

```jsonc
{
  "id": "dl_search",        // 跳转标签
  "type": "say",            // say | scene | image | keys | choice
  "who": "初音未来",         // 空 = 旁白（不画名牌）
  "text": "……",
  "image": "xxx.png",       // 已不再使用（assets/story/img 空了）
  "caption": "图片 / 键位图下面那行",
  "action": "openKeymap",   // keys 页那颗胶囊按下去执行的宿主命令
  "actionLabel": "修改键位", // 它的文案（空 = 不画按钮）
  "do": "openSettings",     // openSettings / launchChartdl
  "wait": "chartdlSearch",  // 见下表
  "onClose": "dl_closed",   // chartdl 被关掉时跳这里
  "timeout": 120, "onTimeout": "dl_slow",
  "goto": "device",         // 本步结束后无条件跳
  "highlight": "settings.speed", "tip": "音符速度，先调到 7~9",
  "done": true,             // 提示条右边挂一颗「完成」胶囊（推进 + 关掉卡片）
  "guide": "search",        // 指向 chartdl 的控件：search/list/easy/queue
  "passthrough": true,      // 只画不挡输入（设置卡片那一步）
  "box": "left",            // 左下紧凑框，把右侧手机面板让出来
  "set": { "device": "touch" },
  "options": [ { "text": "触摸屏", "goto": "touch_hit" } ],
  "subtitles": ["白板 / 平板 / 触屏笔记本", "12 键，或者鼠标点"]
}
```

等待条件：`seconds` / `settingsOpen` / `settingsClosed` / `chartdlOpen` / `chartdlSearch` /
`chartdlSong` / `chartdlEasy` / `chartdlDone` / `chartdlClosed` / `playStarted`。

`type: "scene"` 的卡片不看 `wait`：它按自己的 `hold` 自动推进（+0.35s 淡出）。脚本里
**不要给场景卡写 `wait`**，写了反而会绕开这条；`hold` 是唯一的时间来源。

`playStarted` 是**锁存**的：进游戏时剧情层挂起（不 update），所以回来时 `inPlay` 已经是
false —— 不锁存的话会永远等一局已经打完的歌。锁存点不只在 `wait == "playStarted"`，
**当前步骤是 `play` 也算**（玩家是在那一页按的开始），详见 9.7。

### 9.4 chartdl 的外部引导

- 窗口类名 `CppSekaiChartDl`（`main.cpp` 里原来写成 `#32770`，那是对话框类，永远找不到，
  顺手修了 —— 「已经开着就提到前面」以前其实没生效）。
- 控件 ID（`downloader/chartdl.cpp`）：搜索 1000、列表 1007、下载 1004、EASY 1100、状态 1009。
- 跨进程读状态：`WM_GETTEXT`（编辑框/状态栏）、`LVM_GETSELECTEDCOUNT`、`BM_GETCHECK`、
  `PBM_GETPOS`。都只传标量，不需要跨进程指针封送。
- 浮层：`WS_EX_LAYERED | TOPMOST | TRANSPARENT | NOACTIVATE` 的 popup，GDI 画薄荷圆环 +
  深色标签，`UpdateLayeredWindow`。**GDI 不写 alpha**，所以画完把「非黑像素」的 alpha 一律
  置 255 —— 圈和标签因此是不透明、背景完全透明的，且永远不会吃掉点击。
- 第一次进入引导时把下载器摆到桌面右上角（`chartDlArrange`），避开剧情框。
- 下载器被关掉：3 秒后仍找不到窗口 → 跳 `onClose`（实测跳到「重新打开 / 先跳过」那一步）。

### 9.5 触发与存档

- `userdata.json` → `settings.tutorialDone`；`SongSelect.cpp` 的 `loadUserData` / `saveUserData`
  **两处都改**（老规矩，漏一处静默丢设置）。
- 首启条件：`!tutorialDone && !eulaAlive && (eulaDismissedThisRun || eulaAccepted)
  && state == Select && 房间里只有自己`，再等 1.2 秒让选曲入场动画走完。
  **别用 `party.active()` 当"没开多人"**：`PartyLink::init()` 一启动就映射共享块并占 seat 0，
  单人时它也是 true。用它做闸门 = 教学永远不启动、重播按钮永远没反应（2026-10-18 就是这么
  翻车的）。判断"房间里是否还有别人"要用 `party.playerCount() <= 1`。
- 重播：设置 → 关于 → 「重播新手教学」。**必须就地 `story::stop()` 再 `story::start()`**：
  这张卡片能被点到的时候，教学通常正开着（速度那一步就是教学自己打开的设置卡片，且
  `passthrough` 所以点击能落进来），而选曲界面的启动闸门有 `!story::active()` ——
  老实现只举了个 `tutorialReplay` 标志，于是按钮看起来完全没反应。
- 教学期间把 `nativeMenuBar` 临时关掉（顶栏按钮才在），结束时恢复；**不落盘**，
  中途崩溃不会把用户设置改掉。
- 演奏中挂起剧情层：`story::setSuspended(state != AppState::Select)` 必须放在
  `ImGui::NewFrame()` 之后、Select 分支**之外** —— 放进 Select 分支的话，整首歌里
  剧情层都处于「active 且未挂起」，会把手柄的 A 全吞掉。

### 9.6 教学插图 / 键位页

- **教学里不再有任何图片素材**：`seewo_small.png`、`kb_mouse.png`、`notes_touch.png` 全部删了
  （`assets/story/img/` 目录都不存在了，`.workbuddy/tools/story_assets.py` 一并删除）。
  实拍设备图读者看不出跟「怎么打音符」的关系，音符拼图又不如直接讲——删掉之后台词反而更集中。
- **键位页用游戏自己的轨道图**：`type: "keys"` 走 `storyui::keypad`，底图是
  `assets/mmw/stage.png`（1024×588）**裁下半部分**（y 294..575，即判定线以下），
  等比放进 880×242 的框，底下一层浅色板（stage 本身是半透明白，直接压在压暗的选曲界面上
  会黑成一团）。
- 12 个键帽按**轨道的透视**摆：轨道左右边界随 y 线性变化（实测 y=300 是 240..783、
  y=560 是 23..1000，斜率 0.8346），所以第 i 条轨的中心 = 左边界 + (i+0.5)×轨宽，
  换算到贴图坐标后再乘 `880/1024`。改 `kRow`（键帽所在的行）会同时改字号可用宽度。
- 键帽上的字来自 `host.laneKeys()`（main 里 `SDL_GetKeyName(laneKeys[i])`），
  永远跟着玩家的实际绑定；写死 `Z S X D C V` 曾误导过一次——那既不是默认值，也不是他设的。
- 图下面那颗 **「修改键位」** 是 `"action": "openKeymap"` + `"actionLabel": "修改键位"`：
  键位页 → `gHost.command("openKeymap")` → main 立 `keymapCardOpen`。
  它和选项一样用 `ui::capsuleDraw`；命中用 `InvisibleButton`（这一步不是 passthrough），
  并且**点中它不再顺带推进台词**（`keysActionClicked`）。
### 9.7 打完一首歌之后

`play` / `play_wait` 两步是「去打一首」和「等你回来」。挂起（进入演奏）时如果当前步骤是
其中任意一步，就把 `gPlaySeen` **锁存**并置 `gResumeAdvance`；回到选曲后 `update()` 里
把这两步一起跳过。原来只在 `wait == "playStarted"` 时锁存，而玩家是在 `play` 那一页点的
开始——于是回来还停在「来试试吧」，看着像教程死了。
无头验证：`CPSEKAI_STORY_PLAYTEST=1` 会在 5~7 秒假装去演奏了一趟（走的是 main 里那次
`story::setSuspended()` 调用，不是另写一条路径）。

### 9.8 文案

讲解人是**初音未来**（`who: "初音未来"`），语气按 pjsk 里那个 16 岁的 Miku 来：明亮、
爱用「～」和短句、会「哇——！」「诶嘿嘿」，鼓励为主，不说教。写新台词时注意：

- 不 OOC：她不会阴阳怪气、不会摆前辈架子；讲解可以清楚，但不能像客服。
- 字体只有 msyh + `GetGlyphRangesJapanese/ChineseSimplifiedCommon` 的范围，**♪（U+266A）
  不在里面**，会渲染成「?」——第一版就踩了。真要音符，得给 atlas 加自定义 range。
- 长句会换行成三行，1300 宽 / 44px 大约 29 个汉字一行，超过约 85 字就会挤到框外。

### 9.9 验证

```bash
./build/cppsekai.exe --story-test --story-step N     --screenshot .workbuddy/shots/x.png --screenshot-time 4 --no-party
```

`N` 是 0 基下标，常用的几页：0 场景标题 / 1 对话 / 3 选项 / 4 设置聚光灯 / 19 设备卡片 /
20 触摸图 / 25 绿谱高亮。chartdl 浮层是独立窗口，要抓**整个桌面**才看得到（游戏的
`--screenshot` 只拍自己）。
