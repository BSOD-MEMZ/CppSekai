// CppSekai - pointing at the standalone chart downloader from the tutorial.
//
// chartdl.exe is a separate Win32 process with plain common controls, so the
// game cannot draw inside it. What it can do is read the real control rects
// (GetWindowRect on a child HWND) and paint a topmost, click-through ring on a
// layered window of our own. That is what GuideOverlay does: it never takes
// focus and never eats a click, so the player keeps operating the actual
// downloader while the tutorial points at the exact box to use.
//
// It also reads the downloader's state (search text, list selection, progress,
// status line) so the script can wait on "the player did the thing" instead of
// on a timer.
#pragma once

#include <string>

namespace platform
{
// Control ids inside chartdl.exe (downloader/chartdl.cpp, kId*).
enum ChartDlControl
{
    ChartDlSearch = 1000,
    ChartDlQueue = 1004,
    ChartDlList = 1007,
    ChartDlProgress = 1008,
    ChartDlStatus = 1009,
    ChartDlSettings = 1015,
    ChartDlEasy = 1100,
};

bool chartDlWindow(void** outHwnd);
bool chartDlRect(void** outHwnd, int controlId, void* outRect); // RECT*, screen pixels
std::wstring chartDlText(int controlId);                        // WM_GETTEXT, "" if gone
int chartDlSelectedCount();
int chartDlProgress(); // 0..1000, -1 when unavailable
bool chartDlSearchHasText();
bool chartDlSongSelected();
bool chartDlEasyChecked(); // the EASY tick box in the right-hand panel
bool chartDlDownloadDone(); // status says a job finished / nothing to do

// Puts the downloader in the upper-right corner of the desktop, clear of the
// game's dialogue box. No-op when it is already there.
void chartDlArrange();

// Topmost click-through ring + label over an external window's control.
// Safe to call every frame; nothing is repainted unless the rect/label moved.
void guideShow(void* rect, const wchar_t* label); // rect is a RECT in screen pixels
void guideHide();
} // namespace platform
