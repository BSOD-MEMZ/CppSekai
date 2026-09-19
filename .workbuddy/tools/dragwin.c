// dragwin.c -> build/dragwin.exe
//
// 用**真实输入**（SendInput，走系统输入栈）拖一次窗口。这是验证"拖动窗口期间还出不出帧"的
// 唯一办法：PostMessage 伪造按钮状态骗不过系统的模态移动循环（DefWindowProc 看的是物理按键
// 状态），所以 winmsg 的 raw 只能证明钩子被调用、不能证明真实拖动时的消息流。
//
// 编译（不进 build.sh，和 winmsg 一样）：
//   zig cc -x c -std=c11 -O2 -s .workbuddy/tools/dragwin.c -luser32 -o build/dragwin.exe
//
// 用法：
//   dragwin.exe rect                     # 只打印窗口外框 + 当前前台窗口
//   dragwin.exe [steps=40] [stepPx=3] [sleepMs=25]
//
// 踩过的坑：`SetForegroundWindow` 从后台进程调用会被系统拒绝，于是"拖动"拖到的是压在上面的
// 别的窗口，日志里什么都没有 —— 所以这里用 SetWindowPos(HWND_TOPMOST) 强制置顶，并且**拖完
// 再读一次窗口位置**，位置没变就说明根本没拖到它。
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_state(const char* tag)
{
    HWND hwnd = FindWindowW(L"SDL_app", NULL);
    if (hwnd == NULL) {
        printf("%s: no SDL_app window\n", tag);
        return;
    }
    RECT r;
    GetWindowRect(hwnd, &r);
    HWND fg = GetForegroundWindow();
    printf("%s: hwnd=%p rect=%ld,%ld-%ld,%ld fg=%p %s\n", tag, (void*)hwnd, r.left, r.top, r.right,
        r.bottom, (void*)fg, fg == hwnd ? "(ours)" : "(other)");
    fflush(stdout);
}

int main(int argc, char** argv)
{
    HWND hwnd = FindWindowW(L"SDL_app", NULL);
    if (hwnd == NULL) {
        fprintf(stderr, "no SDL_app window\n");
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "rect") == 0) {
        print_state("rect");
        return 0;
    }
    const int resizeMode = argc > 1 && strcmp(argv[1], "resize") == 0;

    const int steps = argc > (resizeMode ? 2 : 1) ? atoi(argv[resizeMode ? 2 : 1]) : 40;
    const int stepPx = argc > (resizeMode ? 3 : 2) ? atoi(argv[resizeMode ? 3 : 2]) : 3;
    const int sleepMs = argc > (resizeMode ? 4 : 3) ? atoi(argv[resizeMode ? 4 : 3]) : 25;

    // 置顶一会儿：后台进程的 SetForegroundWindow 会被拒，但 SetWindowPos 不会被拒。
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    Sleep(250);
    RECT r;
    GetWindowRect(hwnd, &r);
    printf("before: rect=%ld,%ld-%ld,%ld (%ldx%ld)\n", r.left, r.top, r.right, r.bottom,
        r.right - r.left, r.bottom - r.top);
    // 关键：拖之前确认窗口真的在最前面。后台进程的 SetForegroundWindow 会被系统拒，
    // 于是"拖动"拖的是压在上面的别的窗口 —— 测试就白做了（这个坑踩过两次）。
    if (GetForegroundWindow() != hwnd) {
        INPUT focus;
        ZeroMemory(&focus, sizeof(focus));
        focus.type = INPUT_MOUSE;
        SetCursorPos(r.left + (r.right - r.left) / 2, r.bottom - 60);
        Sleep(120);
        focus.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &focus, sizeof(focus));
        focus.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &focus, sizeof(focus));
        Sleep(300);
    }
    printf("foreground before drag: %s\n",
        GetForegroundWindow() == hwnd ? "ours (good)" : "NOT ours -> the drag would hit another window");
    fflush(stdout);

    const LONG cx = resizeMode ? (r.right - 3) : (r.left + (r.right - r.left) / 2);
    const LONG cy = resizeMode ? (r.top + (r.bottom - r.top) / 2) : (r.top + 10);

    INPUT in;
    SetCursorPos(cx, cy);
    Sleep(150);
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &in, sizeof(in));
    for (int i = 1; i <= steps; ++i) {
        if (resizeMode) {
            SetCursorPos(cx + i * stepPx, cy); // 拖右边缘 → WM_SIZING / WM_SIZE
        } else {
            SetCursorPos(cx + i * stepPx, cy + (i * stepPx * 2) / 5);
        }
        Sleep(sleepMs);
    }
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));

    Sleep(250);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    print_state("after");
    printf("dragged %d px over ~%d ms\n", steps * stepPx, steps * sleepMs);
    fflush(stdout);
    return 0;
}
