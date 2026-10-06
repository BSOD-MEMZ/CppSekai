#include "NativeMenu.hpp"

#ifdef _WIN32
#include <cstdio>

namespace platform::menu
{
namespace
{
// 菜单栏上的分组下标（GetSubMenu(bar, n) / EnableMenuItem(bar, n, MF_BYPOSITION)）。
// 顺序就是 buildBar 里 AppendMenu 的顺序：文件 / 编辑 / 视图 / 帮助。
//
// 子菜单里的项一律按 **id**（MF_BYCOMMAND）认，不按下标 —— 加一项就要重数一遍
// 位置，这个文件之前就是这么被弄脆的。
constexpr int kFileSub = 0;
constexpr int kEditSub = 1;
constexpr int kViewSub = 2;
// ⚠ 这几个是 **GetSubMenu 的下标**，而 GetSubMenu 是"第 n 个**项**"，分隔线也占
// 一个位置（而且碰到分隔线返回 nullptr）。视图 里的排布是
//   0 窗口模式 / 1 分隔线 / 2 排序方式 / 3 分组依据 / 4 分隔线 / 5..7 三个勾选项
// —— 所以排序/分组是 2 和 3，不是 1 和 2（2026-10-06 就是这里写错了一位，
// 两个子菜单一个勾都打不上，dump 出来全是空的）。
constexpr int kWinModeSub = 0; // 视图 -> 窗口模式
constexpr int kSortSub = 2;    // 视图 -> 排序方式
constexpr int kGroupSub = 3;   // 视图 -> 分组依据

HMENU buildBar()
{
    HMENU bar = CreateMenu();
    if (bar == nullptr) {
        return nullptr;
    }

    // ---- 文件 ----
    HMENU file = CreatePopupMenu();
    // 访问键只是**提示**：这里没有建加速键表（SDL 的消息泵不调
    // TranslateAccelerator），真正响应 Ctrl+O / F5 的是游戏自己的按键处理。
    AppendMenuW(file, MF_STRING, CmdOpenChart, L"打开谱面(&O)...\tCtrl+O");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, CmdSettings, L"设置(&S)...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, CmdRescan, L"刷新谱面列表(&R)\tF5");
    AppendMenuW(file, MF_STRING, CmdDownload, L"音乐商店（下载谱面）(&D)...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, CmdQuitInstance, L"结束当前实例(&C)");
    AppendMenuW(file, MF_STRING, CmdQuitAll, L"结束所有实例(&A)...");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"文件(&F)");

    // ---- 编辑 ----
    // 整条菜单跟着"选曲界面里有没有一首当前曲目"亮 / 灰（见 State::songSelected）。
    // 「加入收藏夹」的文案在 sync 里按当前曲目改（加入 / 取消）。
    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, CmdFavorite, L"加入收藏夹(&F)");
    AppendMenuW(edit, MF_STRING, CmdDeleteChart, L"删除谱面文件(&D)...");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"编辑(&E)");

    // ---- 视图 ----
    HMENU view = CreatePopupMenu();
    HMENU winMode = CreatePopupMenu();
    AppendMenuW(winMode, MF_STRING, CmdWinBorderless, L"borderless");
    AppendMenuW(winMode, MF_STRING, CmdWinWindowed, L"windowed");
    AppendMenuW(winMode, MF_STRING, CmdWinFullscreen, L"fullscreen");
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(winMode), L"窗口模式(&W)");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    // 排序 / 分组：跟选曲界面那两个下拉是**同一个值**（settings.sortMode /
    // settings.groupMode），菜单开着的时候游戏里那两个下拉会收起来，两处不会打架。
    HMENU sort = CreatePopupMenu();
    AppendMenuW(sort, MF_STRING, CmdSortByName, L"按名称(&N)");
    AppendMenuW(sort, MF_STRING, CmdSortByLevel, L"按难度(&L)");
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(sort), L"排序方式(&O)");

    HMENU group = CreatePopupMenu();
    AppendMenuW(group, MF_STRING, CmdGroupOff, L"关闭(&O)");
    AppendMenuW(group, MF_STRING, CmdGroupDiff, L"按难度段(&D)");
    AppendMenuW(group, MF_STRING, CmdGroupReading, L"按读音(&R)");
    AppendMenuW(group, MF_STRING, CmdGroupInitial, L"按首字(&I)");
    AppendMenuW(group, MF_STRING, CmdGroupFavorite, L"按收藏(&F)");
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(group), L"分组依据(&P)");

    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, CmdShowFps, L"显示帧率(&F)");
    // 播放进度条的访问键原本是 (&P)，让给 分组依据(&P) 了 —— 同一个下拉里的访问键
    // 不能撞（撞了 Alt+P 的落点由系统任选，等于随机）。
    AppendMenuW(view, MF_STRING, CmdProgressBar, L"显示播放进度条(&B)");
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

    // 窗口模式 / 排序方式 / 分组依据：三组都是**单选**，CheckMenuRadioItem 一次把
    // 整组的点画对（逐个 CheckMenuItem 会画出好几个勾而不是一个点）。
    const auto radio = [](HMENU menu, UINT first, UINT last, int index, int count) {
        if (menu == nullptr) {
            return;
        }
        if (index < 0 || index >= count) {
            index = 0;
        }
        CheckMenuRadioItem(menu, first, last, first + static_cast<UINT>(index), MF_BYCOMMAND);
    };
    radio(GetSubMenu(view, kWinModeSub), CmdWinBorderless, CmdWinFullscreen,
        state.windowMode, 3);
    radio(GetSubMenu(view, kSortSub), CmdSortByName, CmdSortByLevel, state.sortMode, 2);
    radio(GetSubMenu(view, kGroupSub), CmdGroupOff, CmdGroupFavorite, state.groupMode, 5);

    CheckMenuItem(view, CmdShowFps, MF_BYCOMMAND | (state.showFps ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(view, CmdProgressBar,
        MF_BYCOMMAND | (state.showProgressBar ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(view, CmdSimpleFx,
        MF_BYCOMMAND | (state.simpleEffects ? MF_CHECKED : MF_UNCHECKED));

    // 演奏中把三个会打断这一局的项灰掉（重扫 charts/ 会让正在跑的那一帧卡一下，
    // 下载器和"换一首打"更不该从演出中间弹出来）。
    if (file != nullptr) {
        const UINT flags = MF_BYCOMMAND | (state.playing ? MF_GRAYED : MF_ENABLED);
        EnableMenuItem(file, CmdRescan, flags);
        EnableMenuItem(file, CmdDownload, flags);
        EnableMenuItem(file, CmdOpenChart, flags);
    }

    // 编辑：没有当前曲目就整条灰掉 —— 里面的两项都是"对当前这首做点什么"，
    // 没有当前曲目时它们没有意义。这里灰的是**菜单栏上的标题**（MF_BYPOSITION），
    // 灰掉之后 Alt+E 也打不开；子项也一起灰，免得从别处（比如键盘）绕进去。
    {
        const UINT flags = MF_BYPOSITION | (state.songSelected ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(bar, kEditSub, flags);
        HMENU edit = GetSubMenu(bar, kEditSub);
        if (edit != nullptr) {
            // 文案先改、灰后置。**顺序不能反**：ModifyMenuW 是把这一项整个重新加
            // 回去（新文案 + MF_STRING），顺手把 enabled 状态也刷成"可用"了 ——
            // 先置灰再改文案的话，收藏那一项在空列表下还是亮的（实测踩到过）。
            static bool lastFavorite = false;
            static bool haveFavorite = false;
            if (!haveFavorite || lastFavorite != state.songFavorite) {
                haveFavorite = true;
                lastFavorite = state.songFavorite;
                ModifyMenuW(edit, CmdFavorite, MF_BYCOMMAND | MF_STRING,
                    static_cast<UINT_PTR>(CmdFavorite),
                    state.songFavorite ? L"取消收藏(&F)" : L"加入收藏夹(&F)");
            }
            const UINT itemFlags = MF_BYCOMMAND | (state.songSelected ? MF_ENABLED : MF_GRAYED);
            EnableMenuItem(edit, CmdFavorite, itemFlags);
            EnableMenuItem(edit, CmdDeleteChart, itemFlags);
        }
    }

    DrawMenuBar(window);
}
} // namespace platform::menu
#endif
