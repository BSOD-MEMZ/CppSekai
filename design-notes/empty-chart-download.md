# 空谱面时的下载按钮

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 空谱面时的「下载谱面（chartdl）」按钮 2026-09-18
- 列表为空时（`rowCount == 0`）在选曲界面画一句提示 + 一个
  `ui::capsuleButton("下载谱面（chartdl）")`，返回 `SelectAction::SelectDownload(-5)`。
  **`emptyAction` 优先于列表本身产出的 action 返回**（列表为空，本来就产不出别的）。
- `main.cpp` 收到后 `launchChartDownloader()`：先 `FindWindowW(L"#32770", L"CppSekai 谱面下载器")`
  找已开的窗口（有就 `SetForegroundWindow`），否则依次试 `baseDir + "chartdl.exe"` /
  `baseDir + "build\\chartdl.exe"` / `baseDir + "..\\build\\chartdl.exe"`，
  `CreateProcessW` 起独立进程（工作目录 = `baseDir`，所以它写到 `<baseDir>\charts`）。
- 每帧 `pollChartDownloader()` 轮询进程句柄，退出后 **自动重扫谱面**——这就是这个按钮的
  全部意义（下完回来列表里就该有东西）。`wantRescan` 的三个来源：
  `rescanRequested || action == SelectRescan || chartDlFinished`。
- 调试：`--chartdl-test [<sec>]`（默认 1.5s）在列表为空时自动按一次这个按钮。
  **为什么需要它**：`winsend.exe` 的 PostMessage 点击到不了这个界面（无交互会话下 SDL 不收注入输入），
  所以验证只能从代码里按。

