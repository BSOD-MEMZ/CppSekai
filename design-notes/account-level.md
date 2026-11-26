# 账户 / 等级

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 账户 / 等级（2026-09-15，`game::AccountData`）

- **数据**（`game/SongSelect.hpp` 的 `AccountData`，存在 `userdata.json` 的 `account` 段）：
  `name` / `org` / `note` / `rank` / `exp`（**指向下一级**的存量，`addPlayerExp()` 已经把超出
  的部分滚过去了）/ `plays` / `totalScore`。老存档没有 `account` 段就是默认账户，不报错。
  `loadUserData` / `saveUserData` 多带一个 `AccountData&` 参数（只有 main.cpp 两个调用点）。
- **等级曲线**（`expToNextRank`，1:1 照 Sekaipedia 的官方表）：1 级 10、2 级 8010、
  3~12 级 `8000+500*(r-2)`、13~15 级 `13000+1000*(r-12)`、16 级起 `16000+480*(r-15)`，
  上限 `kMaxPlayerRank = 900`。一局的经验 = **分数评级倍率**（`scoreRankExp`：
  d 20 / c 200 / b 240 / a 280 / s 320）—— 官方是 `m_score × m_bonus`，我们**没有加成系统**，
  所以恒等于 `m_score`。评级用 `game::scoreRankAndBar()`，和结算画面牌子同一个调用，不会打架。
- **经验只在完整跑完时结算**：挂在 `main.cpp` 记成绩那块（`!autoPlay && !session.scoreRecorded
  && songTime >= trackDurationSec - 0.25`）里，所以 **autoplay 预览不加经验**，中途放弃也不加。
  **`--result-at` 提前切结算不会记成绩**（songTime 还没到片尾），所以也拿不到经验——想在无头
  环境验证就得真放完整首歌（`--screenshot-time` 给够）。
  日志：`[rank] <评级> +N exp -> rank R (x/need)`（有 ` (rank up)` 就是升级了）。
- **等级牌**（`ui::playerLevelChip`，`game/Ui.hpp`）：圆角深灰药丸，**左边那截绿色是经验条
  不是图标底板**——宽度 = `expRatio` × 药丸宽（左端跟着药丸的圆角、右端切平），
  `assets/select/level.png` 直接盖在它上面（金色音符 + 透明底，压在绿/深灰上都看得见）。
  药丸高度写死 `33*unit`，就是同 unit 下 `ui::combo` 的框高（17u 字 + 8u 上下 padding），
  所以选曲界面里它和「排序 / 分组」两个下拉框一样高、一样平（`headerRowY`）。
  宽度 158u 固定（等级数字在槽里右对齐），几何全部按 `unit`（= 调用方的 px/1080p 单位）缩放。
  贴图走 `ui::setLevelIconTexture()`（main.cpp 启动时
  `loadUiTexture(baseDir + "assets\\select\\level.png")` 注册），**丢了会退化成手画的八分音符**。
  两处调用：选曲右上角（`w - 26*k` / `headerRowY`，`alignRight`）、结算面板右下角
  （`kCanvasW - kPlateRightInset`，和 SCORERANK 牌子右缘对齐）——结算界面**不再另画经验条**，
  同一件事只画一遍。
  结算那块的淡入是**重写这一段顶点 alpha**（芯片自己没有 alpha 参数），和手机面板那套一样。
  独立的长条经验条 `ui::expBar` 现在只有个人资料卡在用（那儿地方宽，值得画一整条）。
- **个人资料卡**（`drawSongSelect` 末尾、`ImGui::End()` 之后）：`ui::beginCard` + `infoRows`，
  点选曲右上角的等级牌打开（卡片是带 dim 的模态，关窗靠 X / 关闭按钮——**别指望 Escape，
  选曲界面按 Escape 是退出游戏**）。`gProfileOpen` 是**模块级**变量（不是函数 static），
  这样 `--profile` 能在无头运行里直接把它顶开。
- **平时不显示**：昵称 / 学校 / 签名只有资料卡和设置「账户」页会画，演奏、HUD、结算都只有等级。
- 无头检查：`--profile`（开资料卡）、`--player 昵称:组织`、`--player-rank N`、`--player-exp <0..1>`
  （本级经验的比例，用来截等级牌那截绿色进度）。后三个**只改内存**（在 `loadUserData`
  之后套用），不会往存档里写测试数据。
- **坑**：`--sus` 的相对路径在 Git Bash 下不可靠（MSYS 会改写 `../x` 这类参数，
  实测 `--sus ../charts/x.sus` 和 `--sus charts/x.sus` 都读不到，绝对路径正常）。
  脚本里一律给绝对路径。另外 `--screenshot` 收的是**文件路径**不是目录，指到目录上会静默不写。
## 头像（2026-11-25 取代卡面）

原来的「选卡面」整块删掉了（`assets/card/` 与 `fetch_card_art.py` 一并删除）。
现在玩家在 **设置 → 账户 → 头像** 上传一张本地图片，存成
`<data>/profiles/<profileId>_avatar.<ext>`，`AccountData.avatar` 只记这个**相对路径**
（换机器 / 改目录 / 重建 build 都不会丢）。空串 = 没头像，多人计分板回退到昵称首字。

- 选图走系统 `GetOpenFileNameW`（PNG / JPG / BMP / GIF / TGA），**复制**进 profile
  目录而不是引用原路径；替换 / 清除都会删旧文件。`--avatar <path>` 是无头检查用的
  临时覆盖（绝对路径，不写盘、不会被「清除」误删）。
- 显示位置：多人演奏的队友计分块（`drawPartyScores`，cover-fit 进 80px 方块）和多人
  结算的成员卡（`Result.cpp`，成员卡上方 200px 圆）。贴图走
  `Renderer::avatarArt(path)`（按路径缓存，上限 24 张）。
- 房间协议 v6 给每个座位加了 `avatar` 字段（绝对路径，最长 780 字节 = MAX_PATH 个
  CJK 字符）；同机所有窗口共用文件系统，所以别的窗口能直接加载。结果块不再带卡面。
- 无头检查：`--avatar <image>`（只改内存，和 `--player` 同性质）；
  配合 `CPSEKAI_RESULT_PARTY=5 --result-preview --screenshot-time 4.5` 可以截出
  「五人成员页 + 每人一个头像」。
