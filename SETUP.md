# 环境配置

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
| `assets/` | pjsk 官方 UI 贴图 + 判定音效 | ~10MB | 官方游戏素材，从上游 AGPL 仓库（sekai-mmw-preview-web）拉取，由 setup.sh 代劳 |
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

完整参数见 `README.md`。

## 已知的坑（改动前先看 AGENTS.md）

- zig 的编译缓存在 D 盘会报 `CacheCheckFailed`，build.sh 已强制缓存到 C 盘临时目录——**别改回去**
- 本项目需要 OpenGL 3.3+（Intel 核显 generally OK，和原版 WebGL2 要求相当）
- 运行时如果 exe 被占用无法覆盖，先关掉正在运行的游戏实例
