# 借用清单（CREDITS.md）

> 这个文件回答一个问题：**CppSekai 里有哪些东西不是我自己写的，它们分别从哪来、什么许可、
> 现在放在哪。**
>
> `COPYRIGHT.md` 讲的是**风险和发布纪律**（什么能发、什么不能发）；本文件是**来源台账**
> （东西从哪来、归谁、什么证）。两份配合看。
>
> 审计时间：**2026-09-18**，方法是 `git ls-files` 实测，不是回忆。数字与当年那份
> 2026-09-12 的审计**已不一致**（素材从 327 个涨到 772 个），以本文件为准。

---

## 一、代码谱系

继承关系是单向的一条链，每一环都合法可传递：

```
MikuMikuWorld (MIT)                       谱面编辑器，判定/渲染核心的原作者
      │  核心逻辑被移植（core/native/mmw_port/，命名空间就叫 MikuMikuWorld）
      ▼
sekai-mmw-preview-web (AGPL-3.0)          把上面那套核心编译成 Web 版的预览器
      │  mmw_preview.cpp 直接取自它
      ▼
CppSekai (AGPL-3.0-only)  ← 本仓库        加平台层、判定引擎、UI、下载器
```

| 环节 | 仓库 | 许可 | 本项目里的落点 |
|---|---|---|---|
| 上游预览器 | `sekai-mmw-preview-web` | AGPL-3.0 | `core/native/src/mmw_preview.cpp`（谱面解析、判定事件生成、packed quad 输出） |
| 核心原作者 | `MikuMikuWorld` | MIT | `core/native/mmw_port/**`（Score / Note / Tempo / EffectView / Particle / Camera / Renderer 等 31 个文件） |

**AGPL 传给整个仓库**：本仓库整体是 AGPL-3.0-only，改动必须继续开源。这不是选择，
是上游许可的强制要求。

> ⚠️ **已知的合规小缺口**：`core/native/src/mmw_preview.cpp` 与 `mmw_port/**`
> 本身**没有文件头许可声明**。仓库根的 `LICENSE` + `COPYRIGHT.md` 覆盖了整仓，
> 法律上够用，但上游文件自带的版权行在移植时被删掉了。若要严格合规，应该在这两处
> 补一段「本文件派生自 XXX（年份，作者），依据 XXX 许可」的头注释。

---

## 二、第三方库（都在 `third_party/`，随仓库分发）

| 库 | 许可 | 用途 | 位置 |
|---|---|---|---|
| Dear ImGui | MIT | 全部 UI（HUD / 结算 / 设置 / 弹窗 / 选曲） | `third_party/imgui/`（含 SDL2 与 OpenGL3 后端） |
| miniaudio | MIT-0（等同公有领域） | 音频播放（BGM / 预览 / SE），可任意嵌入 | `third_party/miniaudio.h`（单头） |
| stb_image | MIT / 公有领域（双授权） | 贴图解码（谱面素材、曲绘） | `third_party/mmw_preview/vendor/stb_image.h`、`third_party/stb_image_write.h` |
| stb_image_write | 同上 | `--screenshot` 写 PNG | `third_party/stb_image_write.h` |
| nlohmann/json | MIT | 读 `musics.json` / `music-vocals.json` / `userdata.json` | `third_party/nlohmann/json.hpp`、`core/native/vendor/nlohmann/json.hpp` |
| DirectXMath | MIT（Microsoft） | 只用头文件，做矩阵/向量数学 | `third_party/DirectXMath/` |

**MIT 的义务只有一条**：保留版权与许可声明。这些库的声明就在各自源文件头部，
**别删**。本表是最省事的「一张表满足」方式。

### 工具链（不在仓库里，但发布时会随包带出去）

| 东西 | 许可 | 说明 |
|---|---|---|
| zig 0.14.1 | MIT | 只当编译器用，产物里不留它的代码。`toolchain/`（已被 gitignore） |
| SDL2 2.32.10 | **zlib** | 窗口 / 输入 / 手柄。`package.sh` 会把 `SDL2.dll` 拷进 Release —— zlib 许可要求保留声明，**发二进制时别漏掉 SDL2 的 LICENSE** |

> SDL2 是 zlib 许可（不是 MIT、也不是 GPL），宽松，但**必须带许可文本**。

---

## 三、参考过但没取代码的项目

这几个是用来「对齐行为」的，一行代码都没抄，列出来是说明思路来源：

| 项目 | 借鉴了什么 |
|---|---|
| `sekai-mmw-preview-web` | 判定与渲染的行为基准（上游，见第一节） |
| `MikuMikuWorld` | 同上（上游，见第一节） |
| [MajdataPlay](https://github.com/LingFeng-bbben/MajdataPlay) | **发布姿态**的社区先例：仓库零官方素材、皮肤音效让用户自己丢、README 导流官方。见 `COPYRIGHT.md` 第五节 |

---

## 四、美术素材（全部属于 SEGA / Colorful Palette）

**这一节是全部风险所在。** 这些是官方游戏解包出来的美术资源，不是自制。

| 路径 | 数量 | 内容 | 被 git 跟踪？ |
|---|---|---|---|
| `assets/mmw/overlay/**` + `overlay_opt/**` | 566 | HUD 全套精灵：分数 `score/**`、连击 `combo/**`、血量条 `life/**`、背景层 `bggen/**`、判定音效提示等 | **是** |
| `assets/mmw/effects/**` | 75 | 音符特效图集（`effect.png` 等，含判定光效） | **是** |
| `assets/mmw/*.png`（顶层 13 个） | 13 | `notes*.png`（音符）、`longNoteLine*.png`（长条）、`touchLine*.png`（触摸线）、`stage.png`、`background_overlay.png`、`default.png` | **是** |
| `assets/select/**` | 12 | 选曲界面：`indicate_back_new.png`、`img_smartphone.png`、`musicsetting.png`、`refresh.png`、`search.png`（搜索框内的放大镜）、`shufflebutton.png`、`skip.png`、clear/FC 指示灯等 | **是** |
| `assets/fx/**` | 4 | 打击特效：`tap_ring.png`、`tap_tri_0..2.png` | **是** |
| `assets/mmw/ui/close.png` | 1 | 关闭按钮 | **是** |
| `Drafts/**` | 64 | 素材暂存区：`icon/profile_icon_0001..0042.png`（官方头像）、`friend_invitation_campaign_*`（官方活动图）、clear/FC 指示灯草稿等 | **是** |
| `assets/splashscreen.png` | 1 | 启动闪屏 | **是** |
| `docs/preview*.png` | 10 | README 截图。**截图内容含官方 UI 贴图**（严格说也是官方美术的再现） | **是** |

`assets/se/**`、`charts/**`、`toolchain/` 等确实不在库里（gitignore 生效）。

> **为什么这是问题**：官方素材的复制权在权利人手里。免费、非商业、写了「版权归官方」
> —— 都**不改变「未经许可再分发即侵权」**这个事实。GitHub 上同类项目活着是因为
> 权利人没执法，不是因为合法。详见 `COPYRIGHT.md` 第四节风险矩阵。

---

## 五、音频

| 路径 | 数量 | 内容 | 被跟踪？ |
|---|---|---|---|
| `assets/se/**` | 20 | UI / 游玩音效：`click`、`select`、`level_choose`、`window_open`、`window_close`、`start`（确定时的光效音）、`count_down`、`touch`、`LIVE_CLEAR`、`LIVE_FINISH`，以及判定音 `se_live_{tap,flick,long,trace,connect}{,_critical}` 全套 | **是** |
| `assets/mmw/sound/**` | 11 | 判定音的另一份拷贝（与 `assets/se/` 的 `se_live_*` 重复） | **是** |
| `assets/ost/**` | 2 | `BGM_LIVE_RESULT_2.mp3`（结算 BGM）与 `キミだけの跳躍.mp3` | **是** |
| `assets/mmw/overlay/ap.mp4` | 1 | 官方 MV 视频（AUTO LIVE 背景） | **是** |
| `assets/mmw/overlay/ap-native/all-perfect.m4a` | 1 | ALL PERFECT 语音 | **是** |

---

## 六、数据表（官方数据库的整表复制）

| 文件 | 大小 | 内容 | 风险 |
|---|---|---|---|
| `musics.json` | ~392 KB | 曲库元数据（曲名、读音、曲绘名、演唱版本、BPM 等） | 事实数据，风险低，但属整表复制 |
| `music-levels.json` | ~17 KB | 难度定数表（EASY..APPEND 的实际数值） | 同上 |
| `music-vocals.json` | ~241 KB | 演唱版本表（哪位歌手唱哪版） | 同上 |

三个都在仓库根、**都被跟踪**。保留可接受（是事实数据，且 `setup.sh` 能按模板从公开
接口重建），但**不要再往里加更多官方表的拷贝**。

---

## 七、字体

| 文件 | 许可 | 用途 | 被跟踪？ |
|---|---|---|---|
| `assets/mmw/font/FOT-RodinNTLG Pro EB.otf` | ⚠️ **Fontworks 商业字体** | `--pjsk-font` 模式下的标题 / 难度字体（`gTitleFont` / `gDiffFont`） | **是** |
| `assets/mmw/font/FOT-RodinNTLGPro-DB.ttf` | ⚠️ **Fontworks 商业字体** | `--pjsk-font` 模式下的正文（`gBodyFont`） | **是** |
| `assets/mmw/font/NotoSansCJKSC-Black.ttf` | SIL OFL 1.1 | 简体中文字形回退，与上面那个合并使用；无 FOT-Rodin 时充当替补 | **是** |

**运行时的默认行为是对的**：不加 `--pjsk-font` 时用系统字体（注册表找字体文件 +
CJK 字形探测，见 `game/Intro.cpp`），FOT-Rodin 只是 opt-in。

**但文件本身在仓库里** —— 这跟「运行时默认不用」是两件事，嵌入分发商业字体同样需要
授权。这是 2026-09-12 那版审计漏掉的一条（它只说了运行时绕开，没查文件是否入库）。
若要彻底干净：把两个 FOT-Rodin 文件从仓库移除，改成 `--pjsk-font` 时从本地游戏目录读，
或干脆只留 Noto（OFL，可自由分发）。

> Noto Sans CJK 的 OFL 要求：再分发时保留其许可文本。OFL 允许与其它字体「合并」，
> 但合并产物不能只按 OFL 发布（这里的合并只是运行时 ImGui 加载，不产生衍生字体文件，
> 所以不触发）。

---

## 八、自制的东西（顺手澄清，这些不是借的）

| 东西 | 说明 |
|---|---|
| `assets/test.sus` | 冒烟测试谱面，标题 `CppSekai Smoke Test` / 作者 `test`，手写的 |
| `icon.ico` / `icon.png` | 自绘图标（**没有**用官方 logo —— 这是 `COPYRIGHT.md` 明令禁止的） |
| `game/**`、`platform/**`、`downloader/**`、`main.cpp` | 本项目原创：判定引擎、渲染器封装、音频封装、SMTC、多人总线、全部 UI、谱面下载器 |
| `build/winsend.c`、`.workbuddy/tools/**` | 自制开发/验证工具，不入发布包 |

---

## 九、一张速查表

```
要发源码  -> 现在就能发（AGPL 合规，附仓库链接）
要发裸 exe -> 能发，但要附 AGPL 文本 + 源码链接，且带上 SDL2 的 zlib 许可
要带素材  -> 别做（见 COPYRIGHT.md 第四节：★★★★★ 风险）
```

| 类别 | 归属 | 许可 | 在库里 |
|---|---|---|---|
| 本项目代码 | 我 | AGPL-3.0-only | ✅ |
| mmw 核心 | MikuMikuWorld / sekai-mmw-preview-web | MIT / AGPL-3.0 | ✅ |
| 第三方库 | 各作者 | MIT / MIT-0 / 公有领域 | ✅ |
| SDL2 | SDL 社区 | zlib | ❌（发布包里有 DLL） |
| zig | zig 社区 | MIT | ❌ |
| 美术 / 音频 / 数据表 | SEGA / Colorful Palette | 保留所有权利 | ⚠️ 大部分在库里 |
| FOT-Rodin | Fontworks | 商业授权 | ⚠️ 在库里 |
| Noto Sans CJK | Google | SIL OFL 1.1 | ✅ |

---

*这是来源台账，不是法律意见。真实纠纷请找律师。风险与处置见 [COPYRIGHT.md](COPYRIGHT.md)。*
