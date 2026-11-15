// CppSekai - the 2D MV decoder.
//
// The official game plays these through CriWare Sofdec2 (CriMana); each song's
// video is a .usm container holding one mpeg1 video stream and no audio
// (MV2D-SCORE.md 1.3/1.7). We decode with a **trimmed ffmpeg** built by
// tools/build_ffmpeg_min.sh: `--disable-everything` plus only usm demuxer /
// mpeg1 decoder / swscale, which is ~11 MB of static libs and links to a few MB
// in the exe.
//
// The API surface used here is deliberately tiny: open → pumpTo(frame) →
// pixels(). Everything about *when* a frame should be shown lives in the caller
// (the audio clock is the master), matching what the official player does with
// SetSeekPosition.
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

    // Opens `videoPath` (.usm) and decodes the first frame. `maxWidth` caps the
    // RGBA output width; the aspect is preserved and the result is padded
    // (letterboxed) to a 16:9 buffer so every frame is the same byte count.
    bool open(const std::string& videoPath, int maxWidth, std::string& outError);

    [[nodiscard]] bool isOpen() const { return mFmt != nullptr; }
    [[nodiscard]] int width() const { return mWidth; }
    [[nodiscard]] int height() const { return mHeight; }
    // Frames per second as reported by the container, rounded. Index of the
    // frame currently in pixels() is frame(); the caller maps song time to it.
    [[nodiscard]] int fps() const { return mFps; }
    // Exact container rate (num/den, e.g. 23.976). Frame indexing MUST use
    // this, not fps(): rounding 24000/1001 up to 24 drifts ~4 frames over a
    // three-minute song, which reads as the MV slowly sliding out of sync.
    [[nodiscard]] double fpsExact() const { return mFpsExact; }
    [[nodiscard]] int frame() const { return mFrame; }
    [[nodiscard]] const std::uint8_t* pixels() const { return mBuffer.data(); }

    // Decodes forward until `targetFrame` is in pixels(). A target *behind*
    // the current frame (progress-bar seek, retry) rewinds the stream to the
    // start and decodes forward again - staying put would leave the video
    // frozen on an old frame until song time caught up, i.e. permanently out
    // of sync. Returns true when the buffer holds the wanted frame, or the
    // stream ended first.
    bool pumpTo(int targetFrame);

    void close();

  private:
    bool decodeNextFrame();

    void* mFmt = nullptr;   // AVFormatContext*
    void* mDec = nullptr;   // AVCodecContext*
    void* mSws = nullptr;   // SwsContext*
    void* mPacket = nullptr; // AVPacket*
    void* mAvFrame = nullptr; // AVFrame* (mFrame is the frame *number*)
    int mStreamIndex = -1;
    std::vector<std::uint8_t> mBuffer;
    int mWidth = 0;
    int mHeight = 0;
    int mFps = 24;
    double mFpsExact = 24.0;
    int mFrame = -1;
    bool mEnded = false;
    bool mDirty = false; // a new frame was decoded but not yet reported
};

} // namespace platform
