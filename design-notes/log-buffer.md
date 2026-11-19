# 日志缓冲

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 日志缓冲（2026-09-18，排查多人问题时的副产品）

- `main.cpp` 启动早期现在无条件 `setvbuf(stdout/stderr, _IONBF)`。
- 原因：这个 exe 是 **Windows 子系统**，`AttachConsole(ATTACH_PARENT_PROCESS)` 成功后会
  `freopen("CONOUT$", ...)`，**而那一步会把 stdout 打回 msvcrt 的默认（行/全）缓冲**。
  从 cmd / PowerShell / Git Bash 里 `> run.txt` 时它同时拥有控制台和被重定向的 stdout，
  日志只在缓冲填满（4KB）时才落到文件——**表现就是"跑着跑着不打了，像卡死"**，
  实际最后几分钟的输出还躺在用户态缓冲里，进程被 kill 就永远丢了。
  排查多人问题花的时间有一半耗在这上面。

