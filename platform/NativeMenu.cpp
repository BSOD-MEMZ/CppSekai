#include "NativeMenu.hpp"

#ifdef _WIN32
#include <cstdio>

namespace platform::menu
{
namespace
{
// 文件 子菜单里每一项的位置（CheckMenuItem / EnableMenuItem 用 id 也行，但
// 位置在同一个文件里对着建菜单那段看最直观）。
//   0 设置… / 1 分隔 / 2 刷新谱面列表 / 3 音乐商店… / 4 分隔 /
//   5 结束当前实例 / 6 结束所有实例…
// 视图 子菜单：0 窗口模式（子菜单）/ 1 分隔 / 2 显示帧率 / 3 显示播放进度条 /
//   4 弱化打击特效
// 帮助 子菜单：0 关于本软件 / 1 许可与免责声明 / 2 分隔 / 3 GitHub / 4 访问 xxtsoft
// （帮助里没有勾选也没有置灰的项，sync 不碰它，所以这里只留文件/视图两个下标。）
constexpr int kFileSub = 0;
constexpr int kViewSub = 1;
constexpr int kWinModeSub = 0; // 视图 -> 窗口模式

// 三个勾选项在 视图 里的位置。
constexpr int kViewFpsPos = 2;
constexpr int kViewBarPos = 3;
constexpr int kViewFxPos = 4;

HMENU buildBar()
{
    HMENU bar = CreateMenu();
    if (bar == nullptr) {
        return nullptr;
    }

    // ---- 文件 ----
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, CmdSettings, L"设置(&S)...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    // F5 只是个**提示**：这里没有建加速键表（SDL 的消息泵不调
    // TranslateAccelerator），真正响应 F5 的是游戏自己的按键处理。
    AppendMenuW(file, MF_STRING, CmdRescan, L"刷新谱面列表(&R)\tF5");
    AppendMenuW(file, MF_STRING, CmdDownload, L"音乐商店（下载谱面）(&D)...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, CmdQuitInstance, L"结束当前实例(&C)");
    AppendMenuW(file, MF_STRING, CmdQuitAll, L"结束所有实例(&A)...");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"文件(&F)");

    // ---- 视图 ----
    HMENU view = CreatePopupMenu();
    HMENU winMode = CreatePopupMenu();
    AppendMenuW(winMode, MF_STRING, CmdWinBorderless, L"borderless");
    AppendMenuW(winMode, MF_STRING, CmdWinWindowed, L"windowed");
    AppendMenuW(winMode, MF_STRING, CmdWinFullscreen, L"fullscreen");
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(winMode), L"窗口模式(&W)");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, CmdShowFps, L"显示帧率(&F)");
    AppendMenuW(view, MF_STRING, CmdProgressBar, L"显示播放进度条(&P)");
    AppendMenuW(view, MF_STRING, CmdSimpleFx, L"弱化打击特效(&E)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"视图(&V)");

    // ---- 帮助 ----
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, CmdAbout, L"关于本软件(&A)");
    AppendMenuW(help, MF_STRING, CmdLicense, L"许可与免责声明(&L)");
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(help, MF_STRING, CmdGithub, L"GitHub(&G)");
    AppendMenuW(help, MF_STRING, CmdXxtsoft, L"访问 xxtsoft(&X)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"帮助(&H)");

    return bar;
}
} // namespace

bool attached(HWND window)
{
    return window != nullptr && GetMenu(window) != nullptr;
}

bool apply(HWND window, bool enabled)
{
    if (window == nullptr) {
        return false;
    }
    const bool isAttached = attached(window);
    if (enabled == isAttached) {
        return isAttached; // 已经是想要的样子，别重画一遍
    }

    if (enabled) {
        HMENU bar = buildBar();
        if (bar == nullptr) {
            std::printf("[menu] CreateMenu failed (0x%lX) - menu bar left off\n",
                static_cast<unsigned long>(GetLastError()));
            std::fflush(stdout);
            return false;
        }
        SetMenu(window, bar);
        DrawMenuBar(window);
        // SetMenu 只动非客户区：窗口外框不变，客户区矮掉一条菜单栏。SDL 会把它
        // 当 SIZE_CHANGED 发出来，游戏重排（见 NativeMenu.hpp 的说明）。
        RECT client{};
        GetClientRect(window, &client);
        std::printf("[menu] menu bar attached, client now %ldx%ld\n",
            client.right - client.left, client.bottom - client.top);
        std::fflush(stdout);
        return true;
    }

    HMENU old = GetMenu(window);
    SetMenu(window, nullptr);
    DrawMenuBar(window);
    if (old != nullptr) {
        DestroyMenu(old); // SetMenu 之后这个菜单已经不属于任何窗口了，归我们销毁
    }
    RECT client{};
    GetClientRect(window, &client);
    std::printf("[menu] menu bar removed, client now %ldx%ld\n",
        client.right - client.left, client.bottom - client.top);
    std::fflush(stdout);
    return false;
}

void sync(HWND window, const State& state)
{
    HMENU bar = GetMenu(window);
    if (bar == nullptr) {
        return;
    }
    HMENU file = GetSubMenu(bar, kFileSub);
    HMENU view = GetSubMenu(bar, kViewSub);
    if (view == nullptr) {
        return;
    }

    // 窗口模式：三个是**一组单选**，CheckMenuRadioItem 一次把整组的点画对
    // （逐个 CheckMenuItem 会画出三个勾而不是一个点）。
    HMENU winMode = GetSubMenu(view, kWinModeSub);
    if (winMode != nullptr) {
        int index = state.windowMode;
        if (index < 0 || index > 2) {
            index = 1;
        }
        CheckMenuRadioItem(winMode, 0, 2, static_cast<UINT>(index), MF_BYPOSITION);
    }

    CheckMenuItem(view, kViewFpsPos,
        MF_BYPOSITION | (state.showFps ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(view, kViewBarPos,
        MF_BYPOSITION | (state.showProgressBar ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(view, kViewFxPos,
        MF_BYPOSITION | (state.simpleEffects ? MF_CHECKED : MF_UNCHECKED));

    // 演奏中把两个会打断这一局的项灰掉（重扫 charts/ 会让正在跑的那一帧卡一下，
    // 下载器更不该从演出中间弹出来）。
    if (file != nullptr) {
        const UINT flags = MF_BYCOMMAND | (state.playing ? MF_GRAYED : MF_ENABLED);
        EnableMenuItem(file, CmdRescan, flags);
        EnableMenuItem(file, CmdDownload, flags);
    }

    DrawMenuBar(window);
}
} // namespace platform::menu
#endif
