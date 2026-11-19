# 手柄 / 单实例 / 触摸 flick

> 从 `AGENTS.md` 拆出（2026-11-19），**内容一字未改**，只补了这一行头。
> 索引与入口：[`AGENTS.md`](../AGENTS.md) → 「设计笔记」。

## 手柄 / 单实例 / 触摸 flick（2026-09-16 晚）

- **手柄绑定**（`main.cpp` 的 pad 块，仍是"手柄→键盘脉冲"这一套；**设置卡片里的自绘控件
  另有焦点环**，见下面「手柄焦点环 / 震动」一节）：
  A = 确定 / 开始 / **跳过开场卡片** / 结算继续；START = 演奏中暂停、其他地方开设置；
  **X / Y = 暂停对话框的 重试 / 放弃**；LB / RB = **设置卡片切页签**；B / BACK = 返回。
  - **演奏中 BACK 改成暂停**（原来发 Escape，而 Play + Escape = 放弃并回选曲，误触代价太大）。
  - **暂停对话框是自绘的**（`ui::messageDialog` 的胶囊按钮是 InvisibleButton，ImGui 自己的焦点
    导航够不着），所以给 `messageDialog` 加了 `forcedChoice` 参数：手柄的选择按"点了一下那个
    按钮"处理（含关闭动画），调用方的 action 分支一行都不用加。
  - **同一帧里 `pressed(btn, prev)` 只能调一次**：第二次算出来一定是 false（prev 已经置真），
    所以 A/START 在"暂停对话框"和"跳过开场"两处都要用，就必须先 `const bool padA = pressed(...)`
    存下来。这是很容易踩的坑（写了两次，A 确认暂停对话框就永远不生效）。
  - 设置卡片的页签变量从 lambda 里的 `static int tab` **提到 main 作用域**（`settingsTab`），
    否则手柄改不到它。
- **无头验证手柄**：`--fake-pad <A|B|X|Y|LB|RB|START|BACK>[,...]` 让这些键以 30 帧压 / 30 帧松
  的节奏循环（多个键各错开半周期，否则 START 会抢掉 X 的那一帧），默认 6 个周期后停
  （不停的话会一直重开对话框，跑不到截图）。SDL 的按钮名是 `leftshoulder`/`dpup` 这种，
  短名在 `padDown()` 里做别名映射。**注意控制台输出不进管道**（GUI 子系统），要看
  `cppsekai.log`，而且每个进程启动时会**截断**这个日志，多开时只能看到最后一个。
- **单实例 / 多开**（`UserSettings::instanceMode`，设置→系统 或 `--instance single|multi`）：
  - single（默认）：`Local\CppSekai.SingleInstance` 命名互斥体已存在 → `EnumWindows` 找标题
    `CppSekai` 的窗口，`ShowWindow(SW_RESTORE)` + `SetForegroundWindow`，然后自己退出。
  - multi：再占一个 `Local\CppSekai.Profile.<id>` 互斥体。**每个实例都要占自己那份**，
    否则第二个窗口会照样挑 `default` 然后两边同时写同一个存档。已有用户都被占了就自动建
    `用户N`（在 设置→账户 里能看到，可改名）。
  - 互斥体靠进程结束由系统释放，崩溃也不会留下死锁。
  - **用户自己开着游戏时怎么做无头验证**（2026-09-23）：实例互斥体会把无头跑挡掉
    （日志只有 `[instance] already running`），而且正在运行的 exe 会让链接直接失败
    （`failed to write output ... Permission denied`）。两条出路一起用：
    `OUT=build/probe.exe bash build.sh` 换个输出名编译（build.sh 支持 `OUT=`），
    再 `./build/probe.exe --instance multi ...` 跑——`--instance multi` 会另占一份
    `Profile.<id>` 锁，既不会撞他的实例，也**不会**像 `--party` 那样加入房间。
    注意多开实例会挑**另一个档案**（不是他在用的那个），所以截出来的设置/成绩可能不同；
    验完把 `build/probe.exe` 删掉。
- **多开的内存**：实测三个实例 207 / 299 / 333 MB（工作集），**线性增长，没有共享**。
  跨进程共享不了 GL 纹理和解码缓冲，想省只能是（a）单进程多窗口，或（b）给非首个实例开
  `CPSEKAI_TEX_RAW=0` 那套贴图缩小策略（`loadTextureFromFile` 的 maxDim/cropHeight 还编在里面，
  现在只是没人用）。**这轮没有实现，只测了数**。

