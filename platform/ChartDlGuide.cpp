// CppSekai - Win32 side of the tutorial's chartdl guidance (see the header).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>

#include <commctrl.h>

#include "ChartDlGuide.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace platform
{
namespace
{
    constexpr const wchar_t* kChartDlClass = L"CppSekaiChartDl";
    constexpr const wchar_t* kChartDlTitle = L"CppSekai 谱面下载器";

    struct FindCtx
    {
        int id = 0;
        HWND found = nullptr;
    };

    BOOL CALLBACK findChildProc(HWND child, LPARAM param)
    {
        auto* ctx = reinterpret_cast<FindCtx*>(param);
        if (GetDlgCtrlID(child) == ctx->id)
        {
            ctx->found = child;
            return FALSE;
        }
        return TRUE;
    }

    HWND findControl(HWND window, int id)
    {
        if (window == nullptr)
        {
            return nullptr;
        }
        FindCtx ctx;
        ctx.id = id;
        EnumChildWindows(window, findChildProc, reinterpret_cast<LPARAM>(&ctx));
        return ctx.found;
    }

    HWND chartDlWindowRaw()
    {
        HWND window = FindWindowW(kChartDlClass, kChartDlTitle);
        if (window == nullptr)
        {
            window = FindWindowW(nullptr, kChartDlTitle);
        }
        return window;
    }

    std::wstring controlText(HWND control)
    {
        wchar_t buffer[512] = {};
        if (control == nullptr)
        {
            return {};
        }
        DWORD_PTR result = 0;
        SendMessageTimeoutW(control, WM_GETTEXT, 511, reinterpret_cast<LPARAM>(buffer),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 120, &result);
        return std::wstring(buffer);
    }

    bool contains(const std::wstring& haystack, const wchar_t* needle)
    {
        return haystack.find(needle) != std::wstring::npos;
    }

    // ---- overlay -------------------------------------------------------
    HWND gOverlay = nullptr;
    HDC gMem = nullptr;
    HBITMAP gBitmap = nullptr;
    void* gBits = nullptr;
    int gW = 0;
    int gH = 0;
    RECT gShownRect{};
    std::wstring gShownLabel;
    bool gVisible = false;

    void destroyBitmap()
    {
        if (gBitmap != nullptr)
        {
            if (gMem != nullptr)
            {
                SelectObject(gMem, nullptr);
            }
            DeleteObject(gBitmap);
            gBitmap = nullptr;
            gBits = nullptr;
        }
        if (gMem != nullptr)
        {
            DeleteDC(gMem);
            gMem = nullptr;
        }
        gW = 0;
        gH = 0;
    }

    bool ensureOverlay()
    {
        if (gOverlay != nullptr)
        {
            return true;
        }
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CppSekaiGuideOverlay";
        RegisterClassExW(&wc); // already registered is fine
        gOverlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW
                | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
        return gOverlay != nullptr;
    }

    int uiScale()
    {
        HDC screen = GetDC(nullptr);
        const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(nullptr, screen);
        return dpi > 0 ? dpi : 96;
    }
} // namespace

bool chartDlWindow(void** outHwnd)
{
    const HWND window = chartDlWindowRaw();
    if (outHwnd != nullptr)
    {
        *outHwnd = window;
    }
    return window != nullptr;
}

bool chartDlRect(void** outHwnd, int controlId, void* outRect)
{
    const HWND window = chartDlWindowRaw();
    if (outHwnd != nullptr)
    {
        *outHwnd = window;
    }
    if (window == nullptr || outRect == nullptr)
    {
        return false;
    }
    const HWND control = findControl(window, controlId);
    if (control == nullptr)
    {
        return false;
    }
    RECT rect{};
    if (GetWindowRect(control, &rect) == FALSE)
    {
        return false;
    }
    if (rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0)
    {
        return false;
    }
    *reinterpret_cast<RECT*>(outRect) = rect;
    return true;
}

std::wstring chartDlText(int controlId)
{
    return controlText(findControl(chartDlWindowRaw(), controlId));
}

int chartDlSelectedCount()
{
    const HWND list = findControl(chartDlWindowRaw(), ChartDlList);
    if (list == nullptr)
    {
        return 0;
    }
    return static_cast<int>(SendMessageW(list, LVM_GETSELECTEDCOUNT, 0, 0));
}

int chartDlProgress()
{
    const HWND progress = findControl(chartDlWindowRaw(), ChartDlProgress);
    if (progress == nullptr)
    {
        return -1;
    }
    return static_cast<int>(SendMessageW(progress, PBM_GETPOS, 0, 0));
}

bool chartDlSearchHasText()
{
    const std::wstring text = chartDlText(ChartDlSearch);
    return !text.empty();
}

bool chartDlSongSelected()
{
    return chartDlSelectedCount() > 0;
}

bool chartDlEasyChecked()
{
    const HWND check = findControl(chartDlWindowRaw(), ChartDlEasy);
    if (check == nullptr)
    {
        return false;
    }
    return SendMessageW(check, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool chartDlDownloadDone()
{
    const std::wstring status = chartDlText(ChartDlStatus);
    return contains(status, L"全部完成") || contains(status, L"完成，失败")
        || contains(status, L"已经下载完整") || contains(status, L"下载完成");
}

void chartDlArrange()
{
    const HWND window = chartDlWindowRaw();
    if (window == nullptr)
    {
        return;
    }
    RECT current{};
    GetWindowRect(window, &current);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info) == FALSE)
    {
        return;
    }
    const RECT work = info.rcWork;
    const int dpi = uiScale();
    const int gap = MulDiv(24, dpi, 96);
    const int width = std::min(MulDiv(980, dpi, 96), static_cast<int>((work.right - work.left) * 62 / 100));
    const int height = std::min(MulDiv(660, dpi, 96), static_cast<int>((work.bottom - work.top) * 62 / 100));
    const int x = work.right - width - gap;
    const int y = work.top + gap;
    if (std::abs(current.left - x) < 12 && std::abs(current.top - y) < 12
        && std::abs((current.right - current.left) - width) < 24)
    {
        return;
    }
    SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
    std::printf("[guide] chartdl arranged to %d,%d %dx%d\n", x, y, width, height);
    std::fflush(stdout);
}

void guideShow(void* rectPtr, const wchar_t* label)
{
    if (rectPtr == nullptr || !ensureOverlay())
    {
        return;
    }
    RECT target = *reinterpret_cast<RECT*>(rectPtr);
    const int dpi = uiScale();
    const int pad = MulDiv(8, dpi, 96);
    const int labelH = MulDiv(38, dpi, 96);
    const int stem = MulDiv(10, dpi, 96);
    const int gap = MulDiv(10, dpi, 96);
    const std::wstring text = label != nullptr ? label : L"";

    // Total window: the ring area plus the label above it (or below when the
    // control is too close to the top of the screen).
    RECT ring{target.left - pad, target.top - pad, target.right + pad, target.bottom + pad};
    const int ringW = ring.right - ring.left;
    const int ringH = ring.bottom - ring.top;
    // Rough advance width (CJK glyphs are close to one em); the label is a
    // single line of at most a dozen characters, and an exact measurement on a
    // not-yet-created memory DC is not worth another round trip.
    const int labelW = text.empty()
        ? 0
        : static_cast<int>(text.size()) * MulDiv(15, dpi, 96) + MulDiv(48, dpi, 96);
    const int w = std::max({ringW, labelW, MulDiv(260, dpi, 96)});
    const int h = ringH + (text.empty() ? 0 : labelH + stem + gap);
    int x = ring.left - (w - ringW) / 2;
    int y = ring.top - (text.empty() ? 0 : labelH + stem + gap);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST), &info) != FALSE)
    {
        const RECT work = info.rcWork;
        if (y < work.top)
        {
            y = ring.bottom + stem + gap; // label below instead
        }
        x = std::clamp(x, static_cast<int>(work.left), static_cast<int>(work.right - w));
        y = std::clamp(y, static_cast<int>(work.top), static_cast<int>(work.bottom - h));
    }

    if (w != gW || h != gH || gMem == nullptr)
    {
        destroyBitmap();
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr);
        gMem = CreateCompatibleDC(screen);
        gBitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &gBits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (gMem == nullptr || gBitmap == nullptr)
        {
            destroyBitmap();
            return;
        }
        SelectObject(gMem, gBitmap);
        gW = w;
        gH = h;
    }

    // Repaint only when something moved: the layer is re-blended every frame
    // otherwise, and UpdateLayeredWindow copies the whole bitmap.
    const bool sameRect = gShownRect.left == x && gShownRect.top == y && gShownRect.right == w
        && gShownRect.bottom == h;
    if (gVisible && sameRect && gShownLabel == text)
    {
        return;
    }
    gShownRect = RECT{x, y, w, h};
    gShownLabel = text;

    auto* pixels = static_cast<unsigned int*>(gBits);
    std::memset(pixels, 0, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);

    const int ringX = (w - ringW) / 2;
    const int ringY = h - ringH;

    HPEN pen = CreatePen(PS_SOLID, std::max(2, MulDiv(4, dpi, 96)), RGB(119, 238, 221));
    HGDIOBJ oldPen = SelectObject(gMem, pen);
    HGDIOBJ oldBrush = SelectObject(gMem, GetStockObject(NULL_BRUSH));
    RoundRect(gMem, ringX + 2, ringY + 2, ringX + ringW - 2, ringY + ringH - 2, 18, 18);
    SelectObject(gMem, oldBrush);
    SelectObject(gMem, oldPen);
    DeleteObject(pen);

    if (!text.empty())
    {
        const int lx = (w - labelW) / 2;
        const int ly = ringY - gap - stem - labelH;
        HBRUSH bg = CreateSolidBrush(RGB(38, 38, 58));
        HPEN noPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
        HGDIOBJ previousBrush = SelectObject(gMem, bg);
        HGDIOBJ previousPen = SelectObject(gMem, noPen);
        RoundRect(gMem, lx, ly, lx + labelW, ly + labelH, labelH, labelH);
        SelectObject(gMem, previousPen);
        SelectObject(gMem, previousBrush);
        DeleteObject(noPen);
        DeleteObject(bg);

        HFONT font = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        HGDIOBJ oldFont = SelectObject(gMem, font);
        SetBkMode(gMem, TRANSPARENT);
        SetTextColor(gMem, RGB(255, 255, 255));
        RECT textRect{lx + 6, ly, lx + labelW - 6, ly + labelH};
        DrawTextW(gMem, text.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(gMem, oldFont);
        DeleteObject(font);
    }

    // GDI leaves alpha at 0. Everything that was painted is opaque, so any
    // non-black pixel becomes alpha 255 - that is enough for a ring + label and
    // keeps the window fully click-through.
    for (int i = 0; i < w * h; ++i)
    {
        unsigned int c = pixels[i];
        pixels[i] = (c & 0x00FFFFFFu) != 0 ? (c | 0xFF000000u) : 0u;
    }

    HDC screen = GetDC(nullptr);
    POINT dst{x, y};
    POINT src{0, 0};
    SIZE size{w, h};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(gOverlay, screen, &dst, &size, gMem, &src, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    SetWindowPos(gOverlay, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    gVisible = true;
}

void guideHide()
{
    if (gOverlay != nullptr && gVisible)
    {
        ShowWindow(gOverlay, SW_HIDE);
        gVisible = false;
        gShownLabel.clear();
        gShownRect = RECT{};
    }
}

} // namespace platform
