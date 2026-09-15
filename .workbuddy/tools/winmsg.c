// winmsg.c -> build/winmsg.exe
//
// Tiny diagnostic helper: reach into another window's controls by class name and
// push text / keys at them, so a headless session can drive a GUI that has no
// CLI hook. Written while chasing the chartdl crash ("type a kana in the search
// box -> the app dies"): WM_CHAR of a kana reproduces it, no real IME needed.
//
// Usage:
//   winmsg.exe <windowClass> list
//   winmsg.exe <windowClass> alive          [--pid N]
//   winmsg.exe <windowClass> gettext <id>   [--pid N]
//   winmsg.exe <windowClass> settext <id> <utf8 text>        [--pid N]
//   winmsg.exe <windowClass> char    <id> <hex utf16 unit>   [--pid N]
//   winmsg.exe <windowClass> ime     <id> <hex utf16 unit>   [--pid N]
//   winmsg.exe <windowClass> click   <clientX> <clientY>      [--pid N]
//
// --pid targets one process, which matters because every instance of the app
// shares the same window class and title.
// Exit code 0 = the message went through, 1 = window/control not found.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static HWND gFound = nullptr;
static const wchar_t* gWantClass = nullptr;
static DWORD gWantPid = 0;
static int gPrintAll = 0;

static BOOL CALLBACK enumProc(HWND hwnd, LPARAM param)
{
    (void)param;
    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 255);
    if (gWantClass != nullptr && wcscmp(cls, gWantClass) != 0) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (gWantPid != 0 && pid != gWantPid) {
        return TRUE;
    }
    if (IsWindowVisible(hwnd) == FALSE) {
        return TRUE;
    }
    if (gPrintAll != 0) {
        std::printf("window %p  pid %lu  class %ls\n", static_cast<void*>(hwnd), pid, cls);
        return TRUE;
    }
    gFound = hwnd;
    return FALSE;
}

static HWND findWindow(const wchar_t* windowClass, DWORD pid)
{
    gFound = nullptr;
    gWantClass = windowClass;
    gWantPid = pid;
    gPrintAll = 0;
    EnumWindows(enumProc, 0);
    return gFound;
}

static int utf8ToWide(const char* text, wchar_t* out, int outCount)
{
    return MultiByteToWideChar(CP_UTF8, 0, text, -1, out, outCount);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: winmsg.exe <windowClass> list|alive|gettext|settext|char|ime ... [--pid N]\n");
        return 2;
    }
    wchar_t classBuffer[128] = {};
    utf8ToWide(argv[1], classBuffer, 128);

    // Pull --pid out of the tail; the verb's own arguments keep their order.
    DWORD pid = 0;
    int count = 0;
    const char* rest[8] = {};
    for (int i = 2; i < argc && count < 8; ++i) {
        if (std::strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            pid = static_cast<DWORD>(std::strtoul(argv[++i], nullptr, 10));
            continue;
        }
        rest[count++] = argv[i];
    }
    const char* verb = count > 0 ? rest[0] : "";

    if (std::strcmp(verb, "list") == 0) {
        gWantClass = classBuffer;
        gWantPid = 0;
        gPrintAll = 1;
        EnumWindows(enumProc, 0);
        return 0;
    }

    HWND hwnd = findWindow(classBuffer, pid);
    if (hwnd == nullptr) {
        std::fprintf(stderr, "window not found: %ls (pid %lu)\n", classBuffer, pid);
        return 1;
    }
    // "click" takes coordinates, not a control id, so it must not be looked up.
    const int ctrlId = count > 1 && std::strcmp(verb, "click") != 0 ? std::atoi(rest[1]) : 0;
    HWND control = ctrlId != 0 ? GetDlgItem(hwnd, ctrlId) : hwnd;

    if (std::strcmp(verb, "alive") == 0) {
        std::printf("window %p alive, pid %lu\n", static_cast<void*>(hwnd), pid);
        return 0;
    }
    if (std::strcmp(verb, "click") == 0) {
        // Real mouse press/release in *client* coordinates, so an ImGui-based UI
        // (which reads SDL's mouse events rather than WM_CHAR) can be driven.
        const int x = count > 1 ? std::atoi(rest[1]) : 0;
        const int y = count > 2 ? std::atoi(rest[2]) : 0;
        const LPARAM pos = MAKELPARAM(x, y);
        std::printf("click (%d,%d) -> %p\n", x, y, static_cast<void*>(hwnd));
        std::fflush(stdout);
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, pos);
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        Sleep(60);
        PostMessageW(hwnd, WM_LBUTTONUP, 0, pos);
        Sleep(60);
        std::printf("done, window alive=%d\n", IsWindow(hwnd));
        return 0;
    }
    if (std::strcmp(verb, "clickchild") == 0) {
        // Like "click", but aimed at a child window picked by class. Plenty of
        // common controls are their own HWNDs (a ListView header is
        // "SysHeader32") and EnumWindows never sees those, so clicking a column
        // header needs this instead of "click".
        wchar_t childClass[128] = {};
        if (count < 2) {
            std::fprintf(stderr, "usage: winmsg.exe <parentClass> clickchild <childClass> <x> <y>\n");
            return 2;
        }
        utf8ToWide(rest[1], childClass, 128);
        gFound = nullptr;
        gWantClass = childClass;
        gWantPid = 0;
        gPrintAll = 0;
        EnumChildWindows(hwnd, enumProc, 0);
        if (gFound == nullptr) {
            std::fprintf(stderr, "child window not found: %ls\n", childClass);
            return 1;
        }
        const int x = count > 2 ? std::atoi(rest[2]) : 0;
        const int y = count > 3 ? std::atoi(rest[3]) : 0;
        const LPARAM pos = MAKELPARAM(x, y);
        HWND child = gFound;
        std::printf("clickchild (%d,%d) -> %p\n", x, y, static_cast<void*>(child));
        std::fflush(stdout);
        PostMessageW(child, WM_MOUSEMOVE, 0, pos);
        PostMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        Sleep(60);
        PostMessageW(child, WM_LBUTTONUP, 0, pos);
        Sleep(150);
        std::printf("done, child alive=%d\n", IsWindow(child));
        return 0;
    }
    if (control == nullptr) {
        std::fprintf(stderr, "control id %d not found\n", ctrlId);
        return 1;
    }
    if (std::strcmp(verb, "gettext") == 0) {
        wchar_t wide[512] = {};
        GetWindowTextW(control, wide, 512);
        std::printf("text = %ls\n", wide);
        return 0;
    }
    if (std::strcmp(verb, "settext") == 0) {
        wchar_t wide[512] = {};
        utf8ToWide(count > 2 ? rest[2] : "", wide, 512);
        std::printf("WM_SETTEXT %ls -> %p\n", wide, static_cast<void*>(control));
        std::fflush(stdout);
        SendMessageW(control, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(wide));
        std::printf("done, window alive=%d\n", IsWindow(hwnd));
        return 0;
    }
    if (std::strcmp(verb, "char") == 0) {
        const unsigned code = count > 2 ? static_cast<unsigned>(std::strtoul(rest[2], nullptr, 16)) : 0;
        std::printf("WM_CHAR U+%04X -> %p\n", code, static_cast<void*>(control));
        std::fflush(stdout);
        SendMessageW(control, WM_CHAR, static_cast<WPARAM>(code), 1);
        std::printf("done, window alive=%d\n", IsWindow(hwnd));
        return 0;
    }
    if (std::strcmp(verb, "ime") == 0) {
        const unsigned code = count > 2 ? static_cast<unsigned>(std::strtoul(rest[2], nullptr, 16)) : 0;
        SetForegroundWindow(hwnd);
        SetFocus(control);
        HIMC context = ImmGetContext(control);
        if (context == nullptr) {
            std::fprintf(stderr, "no IME context\n");
            return 1;
        }
        ImmSetOpenStatus(context, TRUE);
        ImmSetConversionStatus(context, IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE, 0);
        ImmReleaseContext(control, context);
        std::printf("IME opened, composing U+%04X\n", code);
        std::fflush(stdout);
        SendMessageW(control, WM_IME_STARTCOMPOSITION, 0, 0);
        SendMessageW(control, WM_IME_COMPOSITION, static_cast<WPARAM>(code), GCS_COMPSTR | GCS_RESULTSTR);
        SendMessageW(control, WM_IME_ENDCOMPOSITION, 0, 0);
        std::printf("done, window alive=%d\n", IsWindow(hwnd));
        return 0;
    }
    if (std::strcmp(verb, "click") == 0) {
        const int x = count > 1 ? std::atoi(rest[1]) : 0;
        const int y = count > 2 ? std::atoi(rest[2]) : 0;
        const LPARAM pos = MAKELPARAM(x, y);
        std::printf("click (%d,%d) -> %p\n", x, y, static_cast<void*>(hwnd));
        std::fflush(stdout);
        PostMessageW(hwnd, WM_MOUSEMOVE, 0, pos);
        PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        Sleep(60);
        PostMessageW(hwnd, WM_LBUTTONUP, 0, pos);
        Sleep(60);
        std::printf("done, window alive=%d\n", IsWindow(hwnd));
        return 0;
    }
    std::fprintf(stderr, "unknown verb: %s\n", verb);
    return 2;
}
