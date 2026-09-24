# 环境配置

> ### ⚠️ 免责声明
>
> CppSekai 是**非官方、非营利的爱好者作品**，与 SEGA、Colorful Palette 及
> 「プロジェクトセカイ カラフルステージ！ feat. 初音ミク」（Project SEKAI）官方
> **没有任何隶属、赞助、授权或认可关系**，也**未获其许可**。
> `setup.sh` 拉的贴图 / 音效与 `--charts` 下的谱面**权利全部归原权利人**，
> 本项目不主张任何权利，**仅限本机个人游玩与学习**，
> **禁止二次分发、公开发布与任何商业使用**（**发布包必须剥掉 `assets/`**）。
> 按「现状」提供，使用后果由使用者自负。详见 [COPYRIGHT.md](COPYRIGHT.md)。

新机器从 clone 到跑起来一共两条命令，无需安装任何东西（不需要 Visual Studio、不需要 cmake）：

```bash
bash setup.sh          # 拉取工具链（zig + SDL2）和贴图/音效资源
bash build.sh          # 编译，产物在 build/
```

想顺便下测试谱面：

```bash
bash setup.sh --charts # 额外下载 0075 / 0127 两张谱 + BGM 到 charts/
```

想下别的歌 / 自己塞谱面进 `charts/`，看 **[CHARTS.md](CHARTS.md)**（命名规则、下载地址、
元数据 sidecar、常见问题）。

## 为什么仓库里没有 toolchain 和 assets

| 目录 | 内容 | 大小 | 为什么不入库 |
|---|---|---|---|
| `toolchain/` | zig 0.14.1 + SDL2 2.32.10 MinGW 包 | ~300MB | zig.exe 超 GitHub 100MB 单文件硬限制；二进制入库会让每次 clone 都全量拉 300MB。`setup.sh` 30 秒拉完，版本已钉死 |
| `assets/mmw/` | pjsk 官方 UI 贴图 + 特效 | ~10MB | ⚠️ 因历史提交**实际在库里**（见 [COPYRIGHT.md](COPYRIGHT.md)）；`assets/` 其余子目录（se/select/fx）不入库，缺了由 `setup.sh` 补 |
| `charts/` | 官方谱面 + 音频 | 每首几 MB | 官方游戏数据，仅限本地游玩，不要公开分发 |
| `build/` | 编译产物 | — | `build.sh` 重新生成 |

## 手动配置（不用脚本的话）

1. 下载 [zig 0.14.1](https://ziglang.org/download/0.14.1/zig-x86_64-windows-0.14.1.zip)，解压到 `toolchain/zig014/`
   - **必须 0.14.1**。0.16 的 `zig c++` 驱动会吞 `-I` 参数，编译必挂
2. 下载 [SDL2 2.32.10 MinGW 开发包](https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10)，解压到 `toolchain/SDL2-2.32.10/`
3. 贴图/音效：clone 上游 [sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web)，把 `public/assets/mmw/` 整个复制到本仓库 `assets/mmw/`
4. `bash build.sh`

## 运行

```bash
cd build
./cppsekai.exe --sus <谱面.sus> --bgm <音频.mp3>
```

完整参数见 **[CLI.md](CLI.md)**。

## 打包发布

```bash
bash package.sh              # -> dist/CppSekai-<日期>/ + 同名 .zip（自带素材，约 66 MB / 51 MB）
bash package.sh --no-assets  # 精简包（约 2.7 MB，用户侧跑一次 setup.sh --assets-only 拉素材）
```

打出来的 `dist/CppSekai-<日期>/` 就是发 Release 该传的全部内容：

| 文件 | 说明 |
|---|---|
| `cppsekai.exe` / `chartdl.exe` | 游戏本体 + 谱面下载器（图标与版本信息已由 `app.rc` 嵌进 exe） |
| `SDL2.dll` | 唯一的运行时依赖，**必须和 exe 同目录** |
| `icon.png` | 运行时窗口 / 任务栏图标（exe 里另有一份） |
| `assets/` | 贴图 / UI 音效 / 开屏图（**不含字体**，字体只用系统的）。默认打包；`--no-assets` 不带 |
| `musics.json` + `music-vocals.json` + `music-levels.json` | 官方事实数据（曲名 / 读音 / 定数 / 演唱版本）。缺了也能开，只是会退化 |
| `setup.sh` | 精简包用：用户跑一次 `bash setup.sh --assets-only` 补素材 |
| `README.md` / `SETUP.md` / `COPYRIGHT.md` / `CREDITS.md` / `LICENSE` | 说明与许可（**AGPL-3.0-only，发二进制必须附带**） |
| `charts/` | 空目录 + 说明，谱面放这里或用 chartdl 下载 |


## 已知的坑（改动前先看 AGENTS.md）

- zig 的编译缓存在 D 盘会报 `CacheCheckFailed`，build.sh 已强制缓存到 C 盘临时目录——**别改回去**
- 本项目需要 OpenGL 3.3+（Intel 核显 generally OK，和原版 WebGL2 要求相当）
- 运行时如果 exe 被占用无法覆盖，先关掉正在运行的游戏实例

## 再来一遍免责声明（发布前必读）

- **非官方**：与 SEGA / Colorful Palette 及 Project SEKAI 官方**没有任何关系**，
  未获授权、赞助或认可；**别用官方 logo 当图标，也别把 exe 叫成含 "Project SEKAI" 的名字**。
- **素材权利**：`setup.sh` 拉下来的 `assets/**`、`--charts` 下的谱面、四张 JSON 表，
  **权利全部归原权利人**，本项目不主张任何权利。
- **打包纪律（最容易手滑）**：`package.sh` 默认**会把 assets 打进发布目录**——
  那个包**只能自己用**。要对外发，只能用 `--no-assets`，或者只发源码 zip。
  传之前 `git archive` / 翻一遍 zip 内容确认。
  条理见 [COPYRIGHT.md](COPYRIGHT.md) 第五节第 3 条。
- **仅限本地个人使用**：**禁止**分发、公开传播与任何商业使用。
- **无担保 / 责任自负**：程序按 AS IS 提供，使用后果由使用者自行承担。

完整版见 [COPYRIGHT.md](COPYRIGHT.md) 第九节，来源台账见 [CREDITS.md](CREDITS.md)。
