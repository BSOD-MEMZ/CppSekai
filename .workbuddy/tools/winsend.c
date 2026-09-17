// Minimal Win32 input poster, so the UI can be driven while a headless
// --screenshot run is going on (there is no interactive session to click in).
// It finds a window by a title substring and posts a mouse click or a key to
// it; PostMessage is enough because SDL reads its input from the window
// messages (WM_MOUSEMOVE / WM_LBUTTONDOWN / ... / WM_KEYDOWN).
//
//   winsend <titleSubstring> click <clientX> <clientY>
//   winsend <titleSubstring> key <virtualKeyCode>
//   winsend <titleSubstring> move <clientX> <clientY>
//   winsend <titleSubstring> focus
//   winsend <titleSubstring> place <x> <y>
//
// `click` activates the window first (SetForegroundWindow + SetFocus). SDL only
// reports a button press for the window it considers the mouse/keyboard focus
// holder, so a click posted to a background window - which is what every window
// but one is when several copies run side by side - is swallowed without this.
// That is also why the same click used to work for the SDL-event driven screens
// (nothing to focus-trigger there) and silently did nothing for the ImGui ones.
// `place` moves the window to a screen position, so two instances can be put
// next to each other instead of stacking exactly on top of one another.
//
// Build (same toolchain as the game):
//   "$ZIG" cc -O2 .workbuddy/tools/winsend.c -o build/winsend.exe
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HWND g_found;
static const char* g_needle;

static BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp)
{
    (void)lp;
    char buf[512];
    if (GetWindowTextA(hwnd, buf, sizeof(buf)) == 0) {
        return TRUE;
    }
    if (strstr(buf, g_needle) != NULL && IsWindowVisible(hwnd)) {
        g_found = hwnd;
        return FALSE;
    }
    return TRUE;
}

// Brings the window to the foreground for real. SetForegroundWindow alone is
// refused when the current foreground window belongs to another process (which
// it always does when the tool is run from a script), and a window that never
// became the real foreground never gets WM_SETFOCUS - so SDL keeps reporting
// "no keyboard focus" and silently drops every WM_KEYDOWN it is sent.
// Attaching to the foreground thread's input queue lifts that restriction.
static void activate(HWND hwnd)
{
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground != NULL
        ? GetWindowThreadProcessId(foreground, NULL)
        : 0;
    const DWORD myThread = GetCurrentThreadId();
    const BOOL attached = foregroundThread != 0 && foregroundThread != myThread
        && AttachThreadInput(foregroundThread, myThread, TRUE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    if (attached) {
        AttachThreadInput(foregroundThread, myThread, FALSE);
    }
    // SDL tracks focus from these two messages, and refuses to turn a
    // WM_KEYDOWN into an SDL_KEYDOWN without it. When the real activation
    // above is still refused (a locked workstation, no interactive session)
    // posting them directly is what keeps the input path usable.
    PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
    PostMessage(hwnd, WM_SETFOCUS, 0, 0);
    Sleep(150);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: winsend <title> click <x> <y> | key <vk> | move <x> <y>\n");
        return 2;
    }
    g_needle = argv[1];
    g_found = NULL;
    EnumWindows(enumProc, 0);
    if (g_found == NULL) {
        fprintf(stderr, "winsend: no visible window matching '%s'\n", g_needle);
        return 1;
    }

    if (strcmp(argv[2], "click") == 0 && argc >= 5) {
        const int x = atoi(argv[3]);
        const int y = atoi(argv[4]);
        const LPARAM lp = MAKELPARAM(x, y);
        // Activate first: SDL drops button messages for a window that is not
        // its focus window, so posting into the background does nothing.
        // Restore *only* when minimized: Windows animates a restore, and a
        // ClientToScreen taken mid-animation is a few dozen pixels away from
        // where the window ends up, which lands the click on the wrong widget.
        activate(g_found);
        // Put the *real* cursor on the spot as well. SDL reports a button
        // message with the position it has recorded for the mouse, not the one
        // in the message, and it refreshes that from the system cursor - which
        // is why a posted click used to land wherever the pointer happened to
        // be parked.
        {
            POINT pt;
            pt.x = x;
            pt.y = y;
            ClientToScreen(g_found, &pt);
            SetCursorPos(pt.x, pt.y);
            Sleep(120);
        }
        PostMessage(g_found, WM_MOUSEMOVE, 0, lp);
        Sleep(80);
        PostMessage(g_found, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        Sleep(80);
        PostMessage(g_found, WM_LBUTTONUP, 0, lp);
    } else if (strcmp(argv[2], "focus") == 0) {
        activate(g_found);
    } else if (strcmp(argv[2], "rect") == 0) {
        RECT window;
        RECT client;
        POINT origin;
        GetWindowRect(g_found, &window);
        GetClientRect(g_found, &client);
        origin.x = 0;
        origin.y = 0;
        ClientToScreen(g_found, &origin);
        printf("rect: window %ld,%ld %ldx%ld client %ldx%ld origin %ld,%ld\n", window.left,
            window.top, window.right - window.left, window.bottom - window.top, client.right,
            client.bottom, origin.x, origin.y);
    } else if (strcmp(argv[2], "place") == 0 && argc >= 5) {
        RECT rect;
        GetWindowRect(g_found, &rect);
        MoveWindow(g_found, atoi(argv[3]), atoi(argv[4]), rect.right - rect.left,
            rect.bottom - rect.top, TRUE);
    } else if (strcmp(argv[2], "move") == 0 && argc >= 5) {
        PostMessage(g_found, WM_MOUSEMOVE, 0, MAKELPARAM(atoi(argv[3]), atoi(argv[4])));
    } else if (strcmp(argv[2], "key") == 0 && argc >= 4) {
        const WPARAM vk = (WPARAM)atoi(argv[3]);
        // SDL only turns WM_KEYDOWN into an SDL_KEYDOWN while the window owns
        // the keyboard focus, so activate first (same reason as `click`). This
        // is also the more robust of the two ways in: a key carries no
        // coordinates, so nothing can be measured wrong.
        activate(g_found);
        PostMessage(g_found, WM_KEYDOWN, vk, 0);
        Sleep(60);
        PostMessage(g_found, WM_KEYUP, vk, 0);
    } else {
        fprintf(stderr, "winsend: unknown action\n");
        return 2;
    }
    printf("winsend: %s %s ok\n", argv[1], argv[2]);
    return 0;
}
