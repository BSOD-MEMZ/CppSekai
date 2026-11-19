# 内存 / 显存

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 内存 / 显存（2026-10-25 实测审计）

`build/cppsekai.exe --no-party`，1280x720，选曲界面，140 首谱面，Intel Iris Xe：

| | 改之前 | 改之后 |
|---|---|---|
| 稳态（任务管理器「内存」） | 282.6 MB | **228.6 MB** |
| 启动峰值 | 372.6 MB | **291.9 MB** |

**峰值比稳态高的 60~90 MB 不是泄漏**：字体文件整读、贴图解码这些临时分配在 10~11 秒
后被 Windows 归还（固定出现在 10.5~11.3s，跟切背景之类完全无关 —— 2026-09-19 就量过）。
判断"是不是真涨了"只认稳态，或者用下面的工具连续采。

### 已做的四项（都是常驻，别再改回去）

1. **字体 oversample**：3 处硬写 `OversampleH/V = 2` → 删掉走 auto。详见「约定与坑 →
   字体只用系统字体」那一节，实测算法与原因都在那儿。
2. **字体只读一份 TTF**：`mergedBodyRanges()` 把 Japanese + 简中 ranges 并成一个数组，
   只调一次 `AddFontFromFileTTF`。**1.92 的 `FontDataOwnedByAtlas=false` 是个陷阱** ——
   它反而 `ImMemdup` 复制一份（`imgui_draw.cpp:3059`），共享 buffer 这条路根本不存在。
3. **教学三张全帧图矢量化**（`game/StoryUi.cpp`）：Dialogue 只是 band 缺失时的 fallback、
   Choices 加载了但从没被画过（选项早就改成 `ui::capsuleDraw`）、SceneText 是纯水平渐变
   （x431..1489 / y480..599 / RGB(68,68,102) / alpha 峰值 204，竖直方向逐像素相同）→
   `fadeBand()` 用 13 个 quad 画。三张 1920x1080 RGBA 各 7.9 MB，**共省 23.7 MB**。
   源 PNG 还在 `assets/story/ui/`（只作参考，不进显存）。
4. **选曲封面缩略图**（`game/SongSelect.cpp` 的 `thumbFor`）：原图 740×740 原来全尺寸
   上传（2.09 MB/张，列表只画 60px、手机面板 200px），且 `static map` 永不释放 ——
   **这是全程序唯一会"用越久越高"的一块**（29 首全看过一遍 +60 MB）。现在
   `loadUiTexture(..., 256)` 整倍降到 370×370（0.55 MB）+ 64 张封顶 LRU
   （`Renderer::releaseUiTexture` 删纹理）。上限必须大于「同屏张数」（十几行 + 面板），
   否则会删掉正在画的那张。

### 剩下的大头，不建议动

- **窗口 + GL context + 显卡驱动 61 MB**：Intel 驱动自己映射的，换机器就变，动不了。
- **音频设备 30 MB**：miniaudio / WASAPI 初始化。
- **场景贴图 48 MB / HUD 精灵 31 MB**：2026-09-14 试过降采样，**精灵矩形是写死的像素坐标，
  一缩就错位**（`Renderer::loadTextureFromFile` 的注释就是那次写的），别重试。

### 工具

`.workbuddy/tools/mem_trace.py` —— 把游戏的**日志行**和当时的 `WorkingSetSize` 对齐打印，
直接看出「哪一行之前涨的」。stdout 走管道时游戏不往 stdout 说话（判断"没附加控制台"就
改写 cwd 的 `cppsekai.log`），所以它跟的是日志文件。没有日志输出的空档每 250ms 补采一次
（标 `·`）。老的 `mem_sample.py` 只按固定间隔采、看不出是哪一步。

---

