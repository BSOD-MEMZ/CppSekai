// CppSekai - Windows system media integration
// Two pieces, both optional (every failure degrades to a no-op):
//
//  1. SMTC (System Media Transport Controls) - reports the current song,
//     artist and playback position to Windows, so the volume flyout / the
//     taskbar media widget shows what is playing.
//  2. ITaskbarList3 - draws the progress bar on the taskbar button.
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

  private:
    void* mWindow = nullptr; // HWND
    void* mTaskbar = nullptr; // ITaskbarList3*
    void* mSmtc = nullptr;    // ISystemMediaTransportControls*
    // The RandomAccessStreamReference behind the current thumbnail. Kept alive
    // on purpose: the shell resolves it (opens the file) whenever it feels like
    // rendering the flyout, not at the moment it is handed over.
    void* mThumbnail = nullptr;

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
