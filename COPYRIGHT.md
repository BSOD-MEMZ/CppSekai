# 版权与合规说明（COPYRIGHT.md）

> 本文档回答两个问题：**这个仓库里到底有什么东西是别人的**，以及**怎样发布/使用才能把法律风险压到最低**。
> 写于 2026-09-12，基于当天的 `git ls-files` 审计。这不是法律意见书，但每一条都给出了理由。

---

## 一、结论速览

| 问题 | 答案 |
|---|---|
| 代码能随便开源吗 | **能**。本仓库代码遵循 AGPL-3.0-only，公开源码本身就是合规动作 |
| 能把 exe 直接发给别人吗 | 裸 exe（不带素材）可以，但要附源码链接（AGPL 义务） |
| 能把 `assets/`、`charts/` 一起打包发吗 | **不能**。这是全部风险里最大的一块，明确不要做 |
| 仓库现在干净吗 | **不干净**：305 个官方素材文件 + `Drafts/` 22 个官方图 + 官方数据表已被 git 跟踪（见下文第三节） |

---

## 二、代码授权谱系（这部分是干净的）

```
CppSekai（本仓库）            AGPL-3.0-only
 ├─ sekai-mmw-preview-web     AGPL-3.0    谱面核心 mmw_preview.cpp 的来源
 │   └─ MikuMikuWorld         MIT         core/native/mmw_port/ 的移植来源
 ├─ third_party/imgui         MIT         （v1.92+，含 SDL2 / OpenGL3 后端）
 ├─ third_party/miniaudio     MIT-0       （公有领域等效，可任意嵌入）
 ├─ third_party/stb_*         MIT / 公有领域（双授权，随用随挑）
 ├─ third_party/nlohmann-json MIT
 └─ third_party/DirectXMath  MIT          （Microsoft，仅用其头文件）
```

- **AGPL 的义务只有三条**，对本项目来说全部容易满足：
  1. 分发（包括发 exe）时**附上许可证文本和版权声明**——AGPL-3.0 全文由仓库持有者在 GitHub 仓库页提供，发布二进制时记得带上；
  2. 以任何形式分发二进制时，**必须同时提供（或指明获取方式）对应完整源码**——把 GitHub 仓库链接写在 Release 说明里即可；
  3. AGPL 多一条「网络使用也要给源码」：即便只搭了个网页让别人在线玩（本项目的上游就是这么用的），同样要公开源码。本地 exe 不涉及这条。
- **MIT 部分**（MikuMikuWorld、第三方库）：保留版权与许可声明即可，`third_party/` 里各库自带的头文件注释就是声明，别删。
- 注意：**没有任何代码是「SEGA 的」**。谱面解析、判定、渲染全部来自 AGPL/MIT 的开源项目，法律上这叫独立著作权作品，官方无法对代码本身主张权利。他们能主张的是**素材和数据**。

---

## 三、素材与数据：哪些是官方的，现在实际在哪里

这是本项目**全部的真实风险**所在。逐项审计（2026-09-12，`git ls-files`）：

| 内容 | 版权归属 | 是否已被 git 跟踪 | 说明 |
|---|---|---|---|
| `assets/mmw/**`（305 个文件：notes 贴图、HUD 精灵图、effect.png、**ap.mp4 官方 MV 视频**等） | SEGA / Colorful Palette | **是（在库！）** | 早期先 commit 后加 gitignore，规则管不了已跟踪文件。README 里写的「素材不入库」与事实不符 |
| `Drafts/**`（22 个文件：clear/fullcombo 指示灯、官方活动图等） | SEGA / Colorful Palette | **是（在库！）** | 素材暂存区，一直被跟踪 |
| `assets/se/**`、`assets/select/**`、`assets/fx/**`、`charts/**`、`toolchain/` | 同上 / 谱面数据 | 否 | gitignore 生效，安全 |
| `musics.json`、`music-levels.json` | 官方数据（曲库元数据 / 难度定数表） | **是** | 事实数据（标题、数字），著作权风险低，但属于官方数据库的整表复制 |
| `docs/preview*.png` 截图 | 截图里含官方 UI 贴图 | 是 | 游戏截图的著作权风险普遍被视作低（合理使用倾向），但严格说含官方美术 |

**为什么这是个问题**：官方素材的复制权在权利人手里。哪怕免费、哪怕非商业、哪怕声明"版权归官方"，**未经许可的再分发仍然是侵权**。GitHub 上大量 pjsk 谱面模拟器存活至今，是因为权利人**没有执法**，不是因为他们**不能**。SEGA 对《メントルコ》歌包泄露、外挂工具等都有过 DMCA 前科。

---

## 四、风险矩阵：什么行为踩什么雷

| 行为 | 风险 | 理由 |
|---|---|---|
| 自己本地玩 / 自己编译 | ★☆☆☆☆ | 私人复制不涉及分发，各国著作权法都不追究 personal use |
| 公开 git 仓库**只含代码**（无素材） | ★☆☆☆☆ | AGPL 合规即可 |
| 公开仓库**含官方素材**（现状） | ★★★☆☆ | 明确的再分发。被 takedown 的概率不高（官方对同人工具长期默许），但不是零 |
| 发 Release 附带打包了 assets 的 exe/zip | ★★★★★ | 完整、可直接使用的官方素材分发，是最典型的侵权形态，最容易被盯上 |
| 发 Release 只含源码 / 裸 exe | ★★☆☆☆ | 代码层面干净；exe 里没有素材（资源按运行目录解析），剩下的是名称/商标边缘问题 |
| 把下载官方 CDN 的脚本写进文档（CHARTS.md 现状） | ★★☆☆☆ | 教学如何获取 ≠ 代为分发，主流做法（各种 wiki 均如此），但 unipjsk 本身是第三方镜像站，注意别声称这是"官方渠道" |
| 用 "Project SEKAI" 官方 logo / 名字做图标或标题 | ★★☆☆☆ | 商标性使用。叫 "CppSekai" 这种描述性名字风险低；把官方 logo 画进 exe 图标风险高 |
| 收费 / 挂广告 / 开赞赏 | ★★★★☆ | 非营利是执法取舍的重要考量；商业化分发侵权素材几乎必然招致行动 |
| 移植到安卓 / iOS 分发安装包 | ★★★★☆ | 应用商店会主动扫侵权素材，且移动端权利人执法意愿明显更强 |

---

## 五、规避措施清单

### 已经做对的（保持）

- ✅ 代码 AGPL-3.0（许可证全文由仓库持有者在 GitHub 提供）
- ✅ README / CHARTS 反复写明「素材仅限本地游玩、不再分发」
- ✅ `charts/`、`assets/se|select|fx`、`toolchain/` 确实不在库里
- ✅ UI 内有「与官方无关」声明（README §11）
- ✅ 不收费、无广告、无统计
- ✅ 默认用系统字体，绕开了 FOT-Rodin（Fontworks 商业字体）的嵌入式分发问题——`--pjsk-font` 是可选 opt-in

### 建议补齐的（按性价比排序）

1. **决定 `assets/mmw/**` 与 `Drafts/` 的去留**（见第六节，二选一，别拖着）。
2. **发 Release 的纪律**：永远只传「源码 zip」或「裸 exe + SETUP 说明」。传之前 `git archive` 或检查 zip 内容，别把本地 build/ 目录（里面被 build.sh 拷了 assets）直接压上去。**这是最容易手滑翻车的一步。**
3. **EXE 图标与名称**：别用官方 logo / 曲绘做 `cppsekai.ico`。想好看就自己画（你本来就会）。
4. **第三方许可声明**：在 README 或 `NOTICE` 里列一张第三方库清单（第二节那张表就行），MIT 要求"保留许可声明"，一张表是最省事的满足方式。
5. **`musics.json` / `music-levels.json`**：保留没问题（事实数据 + 可由 `setup.sh` 从官方公开接口重建），但别再往里加更多官方表的拷贝。
6. **声明措辞**：保留现有「本项目与 SEGA / Colorful Palette 无关，素材版权归原作者」之外，建议加一句「如有侵权请联系移除」——这是同人圈标准姿势，能显著降低被投诉时的对抗性。

### 不要做的

- ❌ 任何形式的官方素材打包分发（包括"帮你整理好的 charts 合集"）
- ❌ 收费 / 开赏 / 接广告
- ❌ 把 exe 名字改成含 "Project SEKAI" 的字样
- ❌ 在宣传里用官方截图以外的方式暗示官方背书

### 社区先例：MajdataPlay 是怎么做的

[MajdataPlay](https://github.com/LingFeng-bbben/MajdataPlay)（maimai 谱面模拟器，GPL-3.0）
是同类项目里活得最健康的之一，它的三板斧：

1. **仓库零官方素材**。UI 全部自己在 Unity 里重画，皮肤 / 音效 / 判定音 / 谱面 MV
   全部让用户自己丢进 `StreamingAssets/`——"replace the files you want"。
2. **免责 + 导流官方**："This software has no affair with the big S four letter
   company, **please support the arcade whenever you can**"——README、Wiki、B站专栏三处都有。
3. **定位克制**：只做自制谱（Simai）演奏器，不提供任何获取官方数据的功能。

对照 CppSekai 的差异与启示：

| 项目 | MajdataPlay | CppSekai 现状 |
|---|---|---|
| 代码来源 | 从零手搓 | 衍生自 AGPL 上游（合规，无问题） |
| UI 素材 | 自绘 | **官方解包**（assets/mmw，经上游仓库） |
| 数据获取 | 不提供 | CHARTS.md 给了 CDN 模板 |

你判断"mmw 素材本来就是原游戏解包的、问题不大"——同意风险可控（上游公开托管多年，
官方默许同人工具），这也是本文档给 assets/mmw 定 ★★★☆☆ 而非五星的原因。若想进一步
靠拢 MajdataPlay 模式，中期可做的事：把 notes/长条/HUD 贴图换成自绘重制版（判定音效可
用 CC0 音源替代），那样仓库就可以连图带 exe 随便发。

---

## 六、仓库历史清理（可选，破坏性操作）

如果决定让公开仓库彻底干净（比如准备把链接发到大群 / B站简介），需要把已跟踪的官方素材从**历史里**抹掉——只删文件是没用的，历史 commit 里还在。

```bash
# 0. 备份整个仓库（这一步做完再往下！）
cp -r CppSekai CppSekai-backup

# 1. 装 git-filter-repo（比 filter-branch 快且安全）
pip install git-filter-repo

# 2. 从全部历史中抹掉官方素材与数据表
cd CppSekai
git filter-repo --invert-paths --path assets/mmw --path Drafts \
    --path musics.json --path music-levels.json --force

# 3. setup.sh 需要相应升级：改为从上游 sekai-mmw-preview-web 拉
#    assets/mmw/（它本来就是这么干的，脚本里已有现成逻辑）
# 4. force push + 所有 clone 作废重拉
git push --force origin main
```

**代价与替代**：

- 代价：commit 哈希全部改变、issue/PR 关联断掉、所有已 clone 的副本过期。
- 折中方案 A：老仓库转 private，新开一个干净仓库发布（最省事，历史断开无所谓——你本来就"只管生不管养"）。
- 折中方案 B：什么都不动，接受 ★★★☆☆ 的风险。老实说：上游 sekai-mmw-preview-web 自己就把 `assets/mmw/` 托管在公开仓库里，本仓库的素材**本来就来自它**。这不是脱罪理由，但说明执法现状。
- `git filter-repo` 之后 `userdata.json` 之外的东西不受影响；`build.sh` / `setup.sh` 的资源获取路径要跑一遍 `setup.sh` 验证。

---

## 七、收到 DMCA / 权利方联系怎么办

1. **别慌，也别硬刚**。GitHub 的 DMCA 流程是：投诉方发 takedown → GitHub 下架并通知你 → 你可以在 14 天内提交 counter-notice。
2. 99% 的情况正确响应是：**下架素材部分，保留代码部分，回信说明已移除**。官方真正在意的从来是音源和谱面数据，不是你的 C++ 判定引擎。
3. 如果只对代码提 takedown（不太可能但理论存在），AGPL 给了你完整抗辩——公开仓库本身就是许可授权。
4. 收到的是邮件而非 GitHub 转发的投诉时，礼貌回复 + 48 小时内处理，几乎都能体面收场。积累"即时响应"记录对后续万一的纠纷有利。

---

## 八、给使用者的最小声明（可直接贴在 Release 说明里）

```
CppSekai 是开源（AGPL-3.0）的 Project SEKAI 风格谱面演奏器，与 SEGA /
Colorful Palette / Craft Egg 无关，亦未获得任何官方认可。

本仓库不含任何官方游戏素材。谱面、音频、曲绘、UI 贴图由使用者自行获取，
仅供已拥有相应游戏数据的玩家本地研究谱面使用，请于 24 小时内自行删除，
不要分发。素材版权归 SEGA / Colorful Palette 及各词曲版权方所有。

使用本软件产生的任何后果由使用者自行承担。
```

---

*最后强调一次：以上是风险评估和社区实践总结，不是法律意见。涉及真实纠纷请找真律师。*
