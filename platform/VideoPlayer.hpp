// CppSekai - the 2D MV background (settings > 演出 > MV = 2D MV).
//
// The official game plays these with CriWare Sofdec2 (CriMana) and each song's
// video is a .usm container with an mpeg1 video stream and no audio
// (MV2D-SCORE.md 1.3/1.7). We do not link an ffmpeg build: the video is decoded
// by a *child process* (`ffmpeg -f rawvideo -pix_fmt rgba -`) and this class
// just pumps fixed-size RGBA frames off its stdout.
//
// Two reasons for the process boundary:
//   * **Licensing** - an independent process fed by a pipe is aggregation, not a
//     derived work, so the LGPL of the ffmpeg build does not reach this program.
//   * **Build risk** - no MinGW .a has to link against zig's lld, and the
//     decoder can be swapped for a trimmed build later without touching us.
//
// If no ffmpeg binary is found the player simply stays closed and the caller
// keeps the static stage plate (the same fallback the official game uses for a
// song without a 2D MV).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform
{

class VideoPlayer
{
  public:
    VideoPlayer() = default;
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // Starts decoding `videoPath`. `ffmpegPath` may be empty to search PATH /
    // next to the exe / %USERPROFILE%\scoop\shims. `maxWidth` caps the decoded
    // width (the source is only 576 px wide anyway, so this mostly guards
    // against a hi-res MV). Returns false and fills `outError` when nothing can
    // be started or the first frame never arrives.
    bool open(const std::string& ffmpegPath, const std::string& videoPath, int maxWidth,
        std::string& outError);

    // True while the child is alive and the last read succeeded.
    [[nodiscard]] bool isOpen() const { return mProcess != nullptr; }
    [[nodiscard]] int width() const { return mWidth; }
    [[nodiscard]] int height() const { return mHeight; }
    [[nodiscard]] int fps() const { return mFps; }
    // Index of the frame currently in pixels(), -1 before the first read.
    [[nodiscard]] int frame() const { return mFrame; }
    [[nodiscard]] const std::uint8_t* pixels() const { return mBuffer.data(); }

    // Decodes up to (and including) `targetFrame`, discarding everything in
    // between. Forward only - the caller seeks by decoding ahead, exactly like
    // the official player does with SetSeekPosition. Returns true when the
    // buffer now holds the requested frame (or the stream just ended).
    bool pumpTo(int targetFrame);

    void close();

    // Where the ffmpeg binary came from, for the log.
    [[nodiscard]] const std::string& binaryPath() const { return mBinary; }

  private:
    bool readOneFrame();

    void* mProcess = nullptr; // PROCESS_INFORMATION*, owned
    void* mWritePipe = nullptr; // our end of the child's stdin (kept open)
    void* mReadPipe = nullptr;
    std::vector<std::uint8_t> mBuffer;
    std::string mBinary;
    int mWidth = 0;
    int mHeight = 0;
    int mFps = 24;
    int mFrame = -1;
    std::size_t mFrameBytes = 0;
    bool mEnded = false;
};

// Finds an ffmpeg binary: `hint` when given, else next to the exe, then PATH.
// Empty when nothing usable was found.
std::string findFfmpeg(const std::string& hint);

} // namespace platform
