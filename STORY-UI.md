# STORY-UI.md — 剧情风格组件 & 应用内新手教学

> 目标不是做剧情播放器，而是做一层**能被新手教学复用的「场景层」**：
> 一句一句说话 + 选项 + 指认界面元素。以后真要放剧情，同一层直接拿来用。
>
> 资源已落地：`assets/story/ui/`（12 张，全部来自 sekai-stories.pages.dev 的 `/img/`）。
> 版式示意见 `docs/story-ui-mock.png`（用真实资源 + 真实坐标合的，不是效果图）。

## 0. 结论

1. **只做 3 个视觉组件**：对话框、场景标题（时间/地点）、选项。其余（暗角、网格）是氛围层，教学用不上。
2. **资源全是 1920x1080 满幅 PNG，整张画，别切九宫格**。切了圆角和投影会糊，而项目本来就有 1920 虚拟画布（`game/Result.cpp:183` 的 `Canvas`）。
3. **两个新模块就够**：`game/StoryUi.*`（纯绘制）+ `game/Story.*`（脚本运行时）。
   教学触发/存档先塞在 `Story.cpp` 里，等真有第二个消费者再拆 `Tutorial.*`。
4. **先做 P0**：Classic 对话框 + 打字机 + 点击推进 + 压暗。一天能看到东西，再谈选项和高亮。
5. 真正的难点不是画框，是**中文排版**：ImGui 自带换行是给英文写的（`third_party/imgui/imgui_draw.cpp:5387` 自己写着
   "Simple word-wrapping for English, not full-featured"），标点会落到行首；而且打字机**必须先折行再逐字显示**，
   否则每滚一个字整段重排，文字会跨行跳。

## 1. 资源清单（已下载，含实测像素）

| 文件 | 尺寸 | 实际内容 bbox | 用途 | 用不用 |
|---|---|---|---|---|
| `Dialogue_Background_Classic.png` | 1920×1080 | 盒 154,734 – 1766,1053；名牌 220,742 – 888,814 | 圆角白框 + 烤进图里的深色名牌 | **默认用这个** |
| `Dialogue_Background.png` | 1920×1080 | 全宽渐变带 0,771 – 1920,1080 | 新式底部渐变条，名牌得自己画 | 备选 |
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

### 3.1 对话框 `dialogueBox`

```
盒：    154,734 → 1766,1053     圆角 46
正文：  x 206 → 1722           首行 y 838，行高 46，字号 34
名牌：  220,742 → 888,814       字号 31，左边距 50（让开青色菱形）
▼：     1690,1008 附近，1.6s 一个上下浮动周期
```

- **必须先铺一层不透明底**：这张图的盒内是半透明白，直接叠在选曲/演奏的暗画面上字会糊。
  先画 `kCardBg`（#ebebf2，alpha 242）圆角矩形，再叠图。版式图就是这么合出来的。
- 文字色用现有的 `ui::kText`（#444466），和设置卡片、选曲完全同源。
- 名字超过名牌宽度（668px，去掉菱形后约 19 个 31px 汉字）→ **缩字号，别缩牌**。

### 3.2 场景标题 `sceneTitle`

- `SceneText_Background.png` 居中，两行分别画在 y≈510 / 560，字号 30，白色 + 一点投影。
- 淡入 0.35s → 停 1.6s → 淡出 0.35s；期间吞掉推进输入（pjsk 就是这么干的）。
- 左上的 `SceneText_TopLeft` 只在「章节名」场合用，教学先不接。

### 3.3 选项 `choiceRow`

- 整张 `Choices_Background.png` 画上去就是两个 522×98 的胶囊，标签居中，字号 30。
- **只支持 2 个**。真需要 3 个以上：自己在 489 起往下堆 120 间距的行，别去拉伸这张图。
- hover → `kPrimaryHover`，按下 → 反白（`kPrimaryPress` + 薄荷字），和 `ui::capsuleButton` 同一套手感。

### 3.4 压暗与高亮 `dim` / `highlight`

- 教学统一先压 `IM_COL32(0,0,0,115)`（约 45%）。
- 高亮 = 用 4 块压暗矩形在目标矩形四周挖洞 + 一圈 3px 薄荷描边 + `sin` 呼吸，**不用暗角图**。
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
- 键盘：`escapePressed`（main.cpp:7744 那条链）加上 `&& !story::active()`，否则选曲界面按 ESC 直接退游戏。
  F5 刷新、空格预览同理。
- 手柄：选项和推进都要进 `ui::PadScope` / `padNav`，这是老问题（自绘控件键盘够不着），别又漏一次。

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

1. **半透明白盒**：不铺不透明底，暗画面上字直接糊（`docs/story-ui-mock.png` 是铺了底的版本）。
2. **中文折行**：ImGui 只会「超宽硬切」，标点会跑到行首；且打字机不能边打边重排。
3. **显存**：每张 1080p RGBA 约 8MB。只加载 Classic 那一版，别把 default/mysekai 一起读进来。
4. **名字牌宽度固定 668px**：长英文名会溢出，缩字号。
5. **`Choices_Background.png` 只有两个坑**：第三个选项自己画，别拉伸。
6. **窗口缩放**：所有坐标必须过 `Canvas`（`Result.cpp:183`），直接写像素的话 4:3 窗口会错位。
7. **字体**：继续用 `game::bodyFont()`；站点那套 FOT-Rodin 是 Fontworks 商业字体，别顺手抄。
8. **Release 打包**：`assets/story/` 不能进 `dist`（`COPYRIGHT.md` 第 31 行的口径）。
