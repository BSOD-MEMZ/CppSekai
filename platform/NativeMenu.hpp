// ---------------------------------------------------------------------------
// 原生 Win32 菜单栏（设置 > 系统 > 原生菜单栏）。
//
// 为什么有它：选曲顶栏那几个按钮里，刷新谱面列表 / 音乐商店 / 设置 其实都是
// **窗口级**操作，不是曲目属性。习惯用菜单栏的人可以把它们搬到菜单里，此时游戏内
// 那几个按钮会一起收起来，免得两处重复（见 game::drawSongSelect 的
// showHeaderFileButtons）。默认关闭 —— 游戏自己那套 chrome 才是正常外观。
//
// 两条硬限制都是 Windows 给的，不是这里的实现偷懒：
//   ① 菜单栏画在窗口的**非客户区**，所以 WS_POPUP 的窗口根本画不出来 ——
//      windowMode 0（borderless）就是 WS_POPUP，fullscreen 铺满桌面，
//      玻璃实现 = 自绘无框（WM_NCCALCSIZE -> 0）更是把非客户区整个交了出去。
//      这三种情况下 main.cpp 会直接不挂菜单，设置项本身留着。
//   ② 菜单展开时 Windows 跑自己的**模态循环**，SDL 的消息泵被停住（跟拖标题栏
//      是同一个病，AGENTS.md 里记过）。能在那里面继续跑的是**窗口过程**，所以
//      菜单命令由 main.cpp 那层 subclass 接、由 WM_TIMER 喂帧，而不是走
//      SDL_SetWindowsMessageHook。
//
// 这个文件只负责"把菜单搭出来 / 把勾打对"，不碰任何游戏状态：命令 id 由
// main.cpp 映射成动作。
// ---------------------------------------------------------------------------
#pragma once

#ifdef _WIN32
#include <windows.h>

namespace platform::menu
{
// WM_COMMAND 的 id。从 101 起跳，跟窗口可能收到的别的命令错开。
//
// 这一串**必须连续**：窗口过程是靠 `[CmdSettings, CmdXxtsoft]` 这个区间把菜单命令
// 从别的 WM_COMMAND 里认出来的（见 main.cpp 的 subclass）。加新项就往中间插，
// 别在 CmdXxtsoft 后面追加。
enum Command
{
    CmdNone = 0,
    CmdSettings = 101, // 设置…            -> 打开设置卡片
    CmdOpenChart,      // 打开谱面… Ctrl+O -> 原生选文件框
    CmdRescan,         // 刷新谱面列表 F5   -> 重扫 charts/
    CmdDownload,       // 音乐商店…         -> 起 chartdl.exe
    CmdQuitInstance,   // 结束当前实例      -> running = false
    CmdQuitAll,        // 结束所有实例…     -> 二次确认后关掉每一个窗口
    CmdFavorite,       // 编辑：加入 / 取消收藏夹
    CmdDeleteChart,    // 编辑：删除谱面文件…（问一句，然后删本地 .sus）
    CmdWinBorderless,  // 窗口模式 三选一（单选标记）
    CmdWinWindowed,
    CmdWinFullscreen,
    CmdSortByName,     // 排序方式 二选一（跟选曲界面的「排序」下拉同一个值）
    CmdSortByLevel,
    CmdGroupOff,       // 分组依据 五选一（同上，跟「分组」下拉同一个值）
    CmdGroupDiff,
    CmdGroupReading,
    CmdGroupInitial,
    CmdGroupFavorite,
    CmdShowFps,        // 视图 三个勾选项
    CmdProgressBar,
    CmdSimpleFx,
    CmdAbout,          // 关于本软件        -> 设置卡片 关于 页
    CmdLicense,        // 许可与免责声明     -> 首次启动那张卡
    CmdGithub,         // 两条外链
    CmdXxtsoft,
};

// 勾选 / 单选 / 置灰所依赖的全部状态。main.cpp 每帧把当前值填进来，
// 只在**真的变了**的时候调 sync（它会重画菜单栏）。
struct State
{
    int windowMode = 1;         // 0 borderless / 1 windowed / 2 fullscreen：单选
    int sortMode = 0;           // 0 按名称 / 1 按难度：跟 settings.sortMode 同一个值
    int groupMode = 0;          // 0 关闭 / 1 按难度段 / 2 按读音 / 3 按首字 / 4 按收藏
    bool showFps = false;       // 显示帧率
    bool showProgressBar = true; // 显示播放进度条
    bool simpleEffects = false; // 弱化打击特效
    // 演奏中：刷新谱面列表 / 音乐商店 / 打开谱面 置灰（重扫会让正在跑的那一局卡一下，
    // 下载器和"换一首打"更不该从演出中间弹出来）。
    bool playing = false;
    // 选中了一首歌（选曲界面 + 列表里有一首当前曲目）。编辑 整条菜单跟着它灰 / 亮。
    bool songSelected = false;
    // 「加入收藏夹」现在显示成「取消收藏」（当前这首已经在收藏里了）。只影响文案。
    bool songFavorite = false;
};

// 挂上（enabled）或摘掉菜单栏，幂等。
//
// 客户端尺寸会**跟着变**：SetMenu 只改非客户区，窗口外框不动，所以客户区高度会
// 少掉一条菜单栏（~20px），反过来摘掉就长回来。这是 Windows 上所有带菜单栏程序的
// 标准行为，SDL 会把新尺寸当 SDL_WINDOWEVENT_SIZE_CHANGED 发出来，游戏自己重排
// （设置 > 画面的"分辨率"本来就是客户区尺寸，少这 20px 不影响读）。
//
// 返回调用之后菜单栏是否挂着（true = 挂着）。
bool apply(HWND window, bool enabled);

// 现在挂着吗。
bool attached(HWND window);

// 勾 / 单选点 / 置灰。会重画菜单栏，所以只在值变化时调。
void sync(HWND window, const State& state);
} // namespace platform::menu
#endif
