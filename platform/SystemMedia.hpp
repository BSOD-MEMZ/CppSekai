// CppSekai - Windows system media integration
// Three pieces, both optional (every failure degrades to a no-op):
//
//  1. SMTC (System Media Transport Controls) - reports the current song,
//     artist and playback position to Windows, so the volume flyout / the
//     taskbar media widget shows what is playing.
//  2. ITaskbarList3 - the progress bar on the taskbar button, and the little
//     buttons on its hover thumbnail (缩略图工具栏).
//
// There is no Windows SDK in this toolchain, so the WinRT ABI interfaces are
// declared by hand. The vtable order and the IIDs were taken from the system
// metadata (C:\Windows\System32\WinMetadata\Windows.Media.winmd); the order of
// methods in an interface's metadata is its ABI vtable order.
#pragma once

#include <string>

struct SDL_Window;

namespace platform
{

// 缩略图工具栏按钮的 id。点下去会以 WM_COMMAND 发回主窗口
// （HIWORD = kThumbButtonClicked，LOWORD = 这里的值），由 main.cpp 的窗口过程收。
// 故意从 1 起跳：菜单栏那批命令从 101 起，两套 id 不重叠，所以能共用同一条
// "挂起命令"通道。
enum ThumbButtonId
{
    ThumbNone = 0,
    ThumbPause = 1,    // 暂停 / 继续
    ThumbMute = 2,     // 静音 / 取消静音（游戏自己的静音，不是系统音量）
    ThumbBack = 3,     // 返回选曲（跟 ESC 同一条路）
};

// WM_COMMAND 的 HIWORD：缩略图按钮被点了。THBN_CLICKED 的定义值。
constexpr unsigned kThumbButtonClicked = 0x1800;

class SystemMedia
{
  public:
    // Binds to the window. Safe to call without a window (everything no-ops).
    bool init(SDL_Window* window);
    void shutdown();

    bool smtcAvailable() const { return mSmtc != nullptr; }
    bool taskbarAvailable() const { return mTaskbar != nullptr; }

    // Call once per song. durationSec is the playable length (0 = unknown).
    // coverPath is an absolute path to the jacket image ("" = none); it becomes
    // the thumbnail the media flyout shows.
    void setTrack(const std::string& title, const std::string& artist, double durationSec,
        const std::string& coverPath = std::string());

    // Turns the SMTC side on/off at runtime (the settings toggle). Off makes
    // Windows drop our media session, so the flyout falls back to whatever was
    // playing before instead of keeping the last song of ours stuck there.
    // init() leaves it enabled.
    void setReporting(bool on);

    // Call every frame (cheap: only pushes an update when the position moved
    // by more than ~0.5s or the state changed).
    void updatePlayback(bool playing, bool paused, double positionSec, double durationSec);

    // Taskbar button progress, 0..1. Values < 0 clear the bar.
    void setTaskbarProgress(double ratio01, bool paused, bool indeterminate = false);

    // 任务栏缩略图上的三个小按钮（暂停 / 静音 / 返回选曲）。图标是现画的，
    // 不依赖 assets 里的贴图。失败（老系统 / 非 Windows）就是没有按钮，不影响别的。
    // `live` = 正在演奏：不演奏时三个按钮都灰着（返回选曲在选曲界面没有意义）。
    bool addThumbButtons(SDL_Window* window);
    // 状态变了再调（每次都会让 shell 重画那一条）。playing/paused 决定第一颗是
    // 暂停还是继续，muted 决定第二颗的图标。
    void updateThumbButtons(bool live, bool paused, bool muted);

  private:
    void* mWindow = nullptr; // HWND
    void* mTaskbar = nullptr; // ITaskbarList3*
    void* mSmtc = nullptr;    // ISystemMediaTransportControls*
    // The RandomAccessStreamReference behind the current thumbnail. Kept alive
    // on purpose: the shell resolves it (opens the file) whenever it feels like
    // rendering the flyout, not at the moment it is handed over.
    void* mThumbnail = nullptr;

    // 缩略图按钮的五张 HICON：暂停 / 继续 / 有声 / 静音 / 返回。故意不在 shutdown()
    // 里销毁 —— 那时窗口还挂着它们（见 .cpp 里的说明），进程退出会一起收掉。
    void* mThumbIcons[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    bool mThumbAdded = false;
    int mLastThumbLive = -1;  // -1 = 还没同步过
    int mLastThumbPaused = -1;
    int mLastThumbMuted = -1;

    // Cached so we only touch WinRT when something actually changed.
    std::string mTrackTitle;
    std::string mTrackArtist;
    double mLastPositionSec = -1.0e9;
    double mLastDurationSec = -1.0e9;
    int mLastStatus = -1;
    double mLastTaskbarRatio = -1.0e9;
    int mLastTaskbarState = -1;
    bool mComInitialized = false;
};

} // namespace platform
