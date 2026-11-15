# 原作的「按设备性能自动调设置」是怎么做的（2026-11-15，REA 逆向）

> 免责声明同仓库其他文档。本文是**自己看**的记录。
>
> **先说结论**：在日服 7.1.0 的 `dump.cs` 里**没有找到**任何独立的「设备分级 / 自动画质」
> 类，也**没有** `QualitySettings.SetQualityLevel` 的调用点。能确证的是：
> 演出画质的两个开关（`LiveModeType` / `MVQualityType`）都是**用户设置项**，
> 出厂默认值由启动流程给，而"低端机第一次进 Live 是 2D/低画质"这个现象
> 只能用「默认值 + Unity `SystemInfo` 的硬件字段」来解释 —— 后者是推断，不是实证。

---

## 1. 查过什么

| 查法 | 结果 |
|---|---|
| `LiveSettingData.LiveModeType`（6039） | 5 档：`High3D / Default3D / Mode2D / Low / OriginalMV`，**是设置项** |
| `MVQualityType`（5377） | 3 档：`Default / High / VirtualLiveDefault`，**是设置项** |
| 类名搜 `DeviceTier` / `DeviceGrade` / `AutoQuality` / `RecommendQuality` / `GraphicLevel` / `Benchmark` | **零命中** |
| 方法名搜 `SetQualityLevel` | **零命中**（说明不吃 Unity 的 QualityLevel 分级） |
| `UnityEngine.SystemInfo`（28681） | 字段齐全：`processorType / processorFrequency / processorCount / systemMemorySize / graphicsDeviceName / graphicsMemorySize / deviceModel` |
| `DeviceInfoPlugin`（19） | `device_info_plus` 插件的 C# 侧，只做**上报**（拿设备型号给服务端/分析用） |

## 2. 能确证的

1. **画质不是 Unity 的质量档**。`QualitySettings` 类在 dump 里存在（Unity 自带），
   但游戏代码**一次都没调 `SetQualityLevel`** —— 它用的是自己那套
   `LiveModeType`（演出本身 3D/2D）+ 一堆布尔开关（`UseCutIn` / `UseAllPerfectEffect` /
   `Use120FPS` / `NoteEffect` / `FeverEffectTypeIndex` …），见 `LiveSettingData` 的字段表。
2. **设置是持久化的**（`[Key("LiveMode")]` / `[Key("QualityType")]`），
   所以"第一次进 Live 自动变成 2D"这件事**一旦发生就留在存档里**，
   之后不会每局重新判。
3. `VirtualLiveDefault` 这一档说明**不同场景（普通 Live / 虚拟 Live）有各自的默认**。

## 3. 推断（标注清楚，别当实证用）

pjsk 的启动流程（`EnvironmentConfig` → 首次设置初始化）大概率做了类似：

```
if (SystemInfo.processorCount <= 4 || SystemInfo.systemMemorySize <= 2048
    || graphicsDeviceName 在低端白名单/黑名单里)
    LiveMode = Mode2D（或 Low）
else
    LiveMode = Default3D
```

依据：
- 这套字段是 Unity 里唯一跨平台能拿到的（Android/iOS 都有），而且**不需要任何额外权限**；
- iOS 上 Unity 的 `deviceModel` 是 "iPhone12,1" 这种，社区一直是**按型号表**分档；
- 客户端没有独立的判定类，说明逻辑必然内联在初始化里（所以类名搜不到）。

**没能确认的部分**：具体阈值、黑白名单、以及它到底是不是"过了教程才判"。这三点要真机
（或反汇编 `EnvironmentConfig` 的初始化链）才能定，成本高于收益。

## 4. 我们能做什么（CppSekai 的对应方案）

我们没有卡组/机型表，也不该依赖 Unity。可行的、代价从低到高：

| 方案 | 判据 | 代价 | 备注 |
|---|---|---|---|
| **A. 显存 + GL 版本**（推荐） | `glGetString(GL_RENDERER/GL_VERSION)` + `GL_MAX_TEXTURE_SIZE` + `NVX_gpu_memory_info`（NVIDIA）/`ATI_meminfo` | 一次查询，~30 行 | 能识别 "Microsoft Basic Render Driver" / llvmpipe（软件渲染）→ 直接降到最省 |
| **B. CPU 核数** | `SDL_GetCPUCount()` | 1 行 | 只能区分"很弱"，四核以下降档 |
| **C. 首局自适应** | 记录前 200 帧的 `[gl] slow swap` / 平均帧时，超过阈值降 `renderScale` | 需要一个采样窗口 | 最贴近"用起来才知道"，但第一局已经卡了 |
| **D. 探测性跑一帧** | 起一个 1/1 的离屏 FBO 跑一次，量耗时 | 启动多几十 ms | 过于激进，不推荐 |

`GPU-GLITCH.md` 那个探针（`[gl] dropped` / `[gl] slow swap`）其实已经是 C 的雏形，
差一个"自动降档"动作。**建议：A 做一次粗判 + C 兜底**，并且和现有的
`renderScale` / `simpleEffects` 设置打通（自动值只写默认，用户手动改过就不再覆盖）。

## 5. 和本次改动的关系

我们新加的 `UserSettings::mvMode`（0 无 / 1 原曲 / 2 2D）目前**纯手动**。
如果以后要"自动"，就按上面 A 给初始值，并且**只在用户没手动改过的时候**写。

⚠️ 不要照抄"低端机锁 2D MV" —— 我们的 2D MV 是要**解码 mpeg1 视频**的，
低端机上可能比现在的静态舞台更慢。真要自动，判据应该是"能不能稳定解码 30fps"，
而不是"机型低不低"。
