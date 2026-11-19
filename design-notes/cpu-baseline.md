# CPU 基线：老机器上的 `0xC000001D`

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## CPU 基线：老机器上的 `0xC000001D`（2026-09-25 实测）

**症状**：用户双击后**看到启动画面，接着进程静默消失**（Windows 子系统程序，没有控制台、
没有错误框），`cppsekai-crash.log` 里只有：

```
--- crash: exception 0xC000001D
  state=-1 glassMode=0 frameless=-1 frames=0
  fault    cppsekai.exe+0x2113A1  (rva 0x2113A1)
--- end
```

`0xC000001D` = `STATUS_ILLEGAL_INSTRUCTION`：CPU 不认识那条指令。`state=-1 / frames=0` 说明
主循环一帧都没跑，死在启动阶段。

**根因不在代码，在构建参数**：zig 不给 `-mcpu` 时默认目标是 **native**，也就是*构建这台机器*的
CPU。实测 `zig c++ -O2 -### -c x.cpp` 里有 `"-target-cpu" "alderlake"`，于是 exe 里混进了
AVX2 / FMA / BMI2 / **AVX-VNNI**。那条崩溃地址上的字节是：

```
rva 0x2113A1:  C4 E2 59 52 C5 F9 7F 45 10 ...   ->  VPDPWSSD（VEX.128.66.0F38.W0 52）
```

AVX-VNNI 的 128 位 int16 点积 —— **Intel 12 代（Alder Lake，2021）起才有**，连 AVX2 时代的
Haswell 都认不了。这类指令出自 `platform/Renderer.cpp`（它 define 了 `STB_IMAGE_IMPLEMENTATION`），
实测把该 TU 编成汇编数一下：`-mcpu=alderlake` 下 `vpdpwssd` 正好 **41 条**，与发布 exe 里
扫出来的 41 个 AVX-VNNI 编码一一对应；**其它 TU 一条都没有**。所以是 stb_image 的像素解码循环
被 LLVM 用点积指令向量化了，游戏自己的逻辑、上游 mmw、imgui 都干净。

修法：`build.sh` 的 `CXXFLAGS` 里钉 `-mcpu=baseline`（x86-64 基线 / SSE2）。实测（同一份源码）：

| 构建 | `.text` | VEX 前缀字节 | AVX-VNNI 编码 |
|---|---|---|---|
| 旧（native，09-13/09-19/09-24/09-25 四个发布版都是它） | 3 066 470 | 116 642（**3.80%**） | **41** |
| 新（`-mcpu=baseline`） | 2 948 374 | 10 749（0.36%） | **0** |

0.36% 是**噪音底**：`C4`/`C5` 本来就会作为立即数/数据出现，纯随机字节命中率约 0.78%，所以
真实 AVX 代码会把比例抬高一个数量级 —— 1% 就是安全的分界线。AVX-VNNI 那个 4 字节模式在 3MB
里期望误报 ~0.4 个，所以 41 vs 0 是决定性的。**`.text` 顺带小了 119KB。**

**这份 exe 能跑多老？** 闸门工具现在直接给结论行。baseline 版实测：`AVX 家族 0.36%`（= 噪音底）、
`AVX-VNNI 0`、`popcnt 0`、`crc32 0`、`movbe 0`、`SSSE3~SSE4.x 0` → **只剩 SSE2**，而 SSE2 是
x86-64 架构的强制组成部分，所以 **2003 年之后任何 64 位 CPU 都满足**（Athlon 64 / Pentium 4 EM64T /
Core 2 / K8~K10 / 各种 Atom）。对照的 native 版同一套模式是 `3.80% / 41 / 4 / 41 / 365 / 0`，
说明这些模式确实抓得住真指令（不是空转）。编译器侧独立佐证：`zig c++ -mcpu=baseline -### -c x.cpp`
里 `sse3 ssse3 sse4.1 sse4.2 popcnt avx avx2 fma bmi bmi2 lzcnt movbe crc32 avxvnni aes sha gfni`
**全部是 `-target-feature "-xxx"`（关闭）**，而 native 时它们全是 `+`。
（`lzcnt 1`、`SSE3` 个位数这种是字节模式噪音：native 里同模式是 17 / 8，同量级。）

**代价：没有可测的性能损失**（2026-09-25 实测，同一份源码只换 `-mcpu`，交替跑 3 次取最小值）：
启动到首帧 baseline **785.0 ms** vs native **784.7 ms**（+0.2 ms / 0.0%），中间各阶段 ±20 ms 双向跳动
＝噪声。启动耗时里最大的一块其实不是解码而是字体（`fonts` 阶段占 ~700 ms）。所以别拿"性能"当理由
把 `-mcpu` 抬回 native。

**发布闸门**：`.workbuddy/tools/cpu_isa_scan.py`（纯 stdlib，按上面这套阈值判 OK/FAIL），
`package.sh` 在 `bash build.sh` 之后自动跑，FAIL 就拒绝打包（`CPSEKAI_SKIP_ISA_CHECK=1` 可强行绕过）。
`-mcpu=baseline` 会连带 zig 自带的 libc++ / libc / compiler-rt 一起降下来 —— 这点实测过：
最小 hello 程序 native 是 6956 个 VEX 字节、baseline 只剩 617（噪音），说明不是"只有我们的 TU 降了"。
`package.sh` 找 Python 的顺序是 `python` → `python3` → `py -3`（Git Bash 里 `python` 常常不在 PATH），
**一个都没有就大声警告并跳过** —— 不要改回静默跳过：没有闸门的包正是这次事故的成因。

想换性能档位就改 `-mcpu`（`x86_64_v2` = Nehalem 2008+，`x86_64_v3` = Haswell 2013+），**但别删**。

**拿一台陌生机器现场判**：`.workbuddy/tools/cpu_features.c` 编成 `build/cpuinfo.exe`
（`zig cc -std=c11 -O2 -mcpu=baseline -Wall -Wextra`，**它自己也必须是 baseline 编的**，
否则它正好在要诊断的机器上先崩）。双击报 CPU 型号 + AVX2 / AVX512-VNNI / AVX-VNNI 支持情况，
并按"旧包要 AVX-VNNI、新包只要 SSE2"给出能不能跑的结论；把 exe 路径当参数传给它，它还会扫那个
exe（VEX 占比 / AVX-VNNI 条数）再说一遍。2026-09-25 用它确认了构建机是 **i7-1260P（Model 154）**、
i3-7100U（Kaby Lake）属于"没有 AVX-VNNI"的可复现机型。

**注意 `AVX512-VNNI` ≠ `AVX-VNNI`**：前者 10/11 代 Intel 和 AMD Zen 4 都有，**但我们 exe 里用的是
后者（VEX 编码），Intel 要 12 代（2021 Q4）、AMD 要 Zen 5（2024）**。所以"能装 Win11 的机器"
（Intel 8 代起 / AMD Zen 2 起）里，**凡是 2021 年前的全都中招** —— 报"能装 Win11 所以不该太老"
这个直觉在这里不成立。

