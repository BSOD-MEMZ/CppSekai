// winstyle.c -> build/winstyle.exe
//
// Prints the Win32 style bits of a window (found by title). Written while
// chasing "the window cannot be resized": SDL's SDL_WINDOW_RESIZABLE shows up
// as WS_THICKFRAME, and a borderless->bordered round trip (the image splash
// forces frameless, then the frame comes back) is exactly where that bit can
// get lost. Add-Type/GetWindowLongPtr from PowerShell is blocked in this
// sandbox, so it is a tiny C tool instead.
//
//   winstyle.exe <windowTitle>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: winstyle.exe <windowTitle>\n");
        return 2;
    }
    HWND hwnd = FindWindowA(NULL, argv[1]);
    if (hwnd == NULL) {
        fprintf(stderr, "winstyle: no window titled '%s'\n", argv[1]);
        return 1;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    printf("hwnd=0x%p style=0x%08llX exstyle=0x%08llX\n", (void*)hwnd,
        (unsigned long long)style, (unsigned long long)exStyle);
    struct
    {
        const char* name;
        LONG_PTR bit;
    } bits[] = {
        {"WS_THICKFRAME (resize frame)", 0x00040000},
        {"WS_CAPTION", 0x00C00000},
        {"WS_BORDER", 0x00800000},
        {"WS_DLGFRAME", 0x00400000},
        {"WS_SYSMENU", 0x00080000},
        {"WS_MINIMIZEBOX", 0x00020000},
        {"WS_MAXIMIZEBOX", 0x00010000},
    };
    for (int i = 0; i < (int)(sizeof(bits) / sizeof(bits[0])); ++i) {
        printf("  %-30s %s\n", bits[i].name, (style & bits[i].bit) == bits[i].bit ? "yes" : "NO");
    }
    RECT rect;
    GetWindowRect(hwnd, &rect);
    printf("  rect %ld,%ld %ldx%ld\n", rect.left, rect.top, rect.right - rect.left,
        rect.bottom - rect.top);
    return 0;
}
