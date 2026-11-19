# Windows 7 兼容

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## Windows 7 兼容（2026-09-19 实测）

Win7 SP1 上启动直接弹 **「无法定位程序输入点 GetSystemTimePreciseAsFileTime 于动态链接库
KERNEL32.dll 上」**，一行业务代码都不执行。根因不在业务代码，在工具链：

- zig 自带 libc++（`toolchain/…/lib/libcxx/src/chrono.cpp`）编译时 `_WIN32_WINNT=0x0a00`，
  于是 `std::chrono::system_clock::now()` 走 `#if _WIN32_WINNT >= _WIN32_WINNT_WIN8` 分支，
  **静态导入** `GetSystemTimePreciseAsFileTime`（Win8+ 才导出）。这个导入是 libc++ 内部的，
  连只 `#include <iostream>` 的空程序都会中招 —— 所以**没法在业务代码里绕开**（实测：
  项目里一处 `system_clock` 都没用）。
- `-D_WIN32_WINNT=0x0601` 没用：它只影响我们自己的 TU，libc++ 是 zig 按自己的规则预编译的。
- 同名强符号顶掉导入也不行：zig 的 windows-gnu 链接是「直接拿 DLL 当输入」，
  会报 `duplicate symbol: GetSystemTimePreciseAsFileTime ... defined at KERNEL32.dll`，
  而且 zig 不认 `--allow-multiple-definition`。

**修法（已落地，在 `build.sh` 顶部）**：构建前幂等地给那份 `chrono.cpp` 打两处小补丁，强制走
libc++ 自带的「运行时探测」分支（`GetProcAddress` 找得到就用精确时钟，找不到退回
`GetSystemTimeAsFileTime`）。Win8+ 行为完全不变（实测精度仍是微秒级，`gap_us=0`），Win7 退到
15ms 粒度 —— 游戏计时走 QPC，只影响 `std::chrono::system_clock`。补丁后有
`grep -c CPPSEKAI-WIN7 = 2` 的断言，打不上就 `exit 1`，不会静默产出 Win7 打不开的 exe。
`toolchain/` 不入库，所以补丁必须留在 build.sh 里，重新解压 zig 也能自愈。

**复查手法**（`.workbuddy/tools/pe_imports.py`，纯 stdlib 的 PE 导入表解析）：

```bash
python .workbuddy/tools/pe_imports.py build/cppsekai.exe build/chartdl.exe build/SDL2.dll
# 期望：三个都是「未发现已知 Win8+ 独占导入」
```

注意别用 `strings | grep` 判断：libc++ 的运行时探测分支里**还留着那个名字的字面量**（喂给
`GetProcAddress`），会永远命中；`grep -c` 判断是否为静态导入必须是**看导入表**。
`GetFileInformationByHandleEx` / `SetFileInformationByHandle` 是 **Vista** 就有的，不是坑。

已经查过、没问题的：`SDL2.dll` 导入表干净（msvcrt + Vista 级 API），其余 94 个 KERNEL32 导入
（SRWLock / FlsAlloc / InitOnceExecuteOnce / GetTickCount64 / CreateSymbolicLinkW / RtlVirtualUnwind…）
全是 Vista 基线。SMTC 那套本来就有 Win10 门的容错。

### 还没解决的：Win7 缺 UCRT

exe 静态导入 `api-ms-win-crt-{runtime,stdio,string,math,heap,locale,convert,time,environment,
multibyte,utility,private}-l1-1-0.dll`。Win8 起由系统 API set 解析到 `ucrtbase.dll`（所以
System32 里根本看不到这些文件，Win10 上一直无事），**Win7 上没有这套 API set**，必须真文件在。
三条路，任选：

1. 让用户装一次 VC++ 2015-2022 运行库 / KB2999226（最省事，但 README 那句「不需要安装运行库」
   在 Win7 上就不成立了）；
2. 随包带 app-local UCRT：`ucrtbase.dll` + 12 个 `api-ms-win-crt-*.dll` 放 exe 旁边
   （MS 支持的部署方式，来源是 Windows SDK 的 `Redist\ucrt\DLLs\x64`，约 1.5MB，对 66MB 的包
   不痛不痒）；`package.sh` 加一段拷贝即可；
3. 放弃 Win7（要动 README / COPYRIGHT 里的最低系统要求）。

**待用户拍板**，暂时只在文档里写明前置条件。

