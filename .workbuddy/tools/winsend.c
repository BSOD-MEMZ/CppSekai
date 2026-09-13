// Minimal Win32 input poster, so the UI can be driven while a headless
// --screenshot run is going on (there is no interactive session to click in).
// It finds a window by a title substring and posts a mouse click or a key to
// it; PostMessage is enough because SDL reads its input from the window
// messages (WM_MOUSEMOVE / WM_LBUTTONDOWN / ... / WM_KEYDOWN).
//
//   winsend <titleSubstring> click <clientX> <clientY>
//   winsend <titleSubstring> key <virtualKeyCode>
//   winsend <titleSubstring> move <clientX> <clientY>
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
        PostMessage(g_found, WM_MOUSEMOVE, 0, lp);
        Sleep(80);
        PostMessage(g_found, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        Sleep(80);
        PostMessage(g_found, WM_LBUTTONUP, 0, lp);
    } else if (strcmp(argv[2], "move") == 0 && argc >= 5) {
        PostMessage(g_found, WM_MOUSEMOVE, 0, MAKELPARAM(atoi(argv[3]), atoi(argv[4])));
    } else if (strcmp(argv[2], "key") == 0 && argc >= 4) {
        const WPARAM vk = (WPARAM)atoi(argv[3]);
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
