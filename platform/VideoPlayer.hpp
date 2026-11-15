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
    [[nodiscard]] int frame() const { return mFrame; }
    [[nodiscard]] const std::uint8_t* pixels() const { return mBuffer.data(); }

    // Decodes forward until `targetFrame` is in pixels(). Forward only: a seek
    // backwards would mean reopening (a live never runs backwards except on a
    // retry, which closes and reopens the whole player). Returns true when the
    // buffer holds the wanted frame, or the stream ended first.
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
    int mFrame = -1;
    bool mEnded = false;
    bool mDirty = false; // a new frame was decoded but not yet reported
};

} // namespace platform
