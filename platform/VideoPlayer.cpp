// CppSekai - 2D MV decoder, built on the trimmed ffmpeg from
// tools/build_ffmpeg_min.sh (usm demuxer + mpeg1 decoder + swscale only).
// See VideoPlayer.hpp for the shape and MV2D-SCORE.md 1.7 for the container.
#include "platform/VideoPlayer.hpp"

#include "path_utf8.hpp"
#include "platform/Log.hpp"

#include <algorithm>
#include <cstdio>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}

namespace platform
{
namespace
{
constexpr int kOutWidth = 640; // output height derives from the source aspect
} // namespace

VideoPlayer::~VideoPlayer()
{
    close();
}

bool VideoPlayer::open(const std::string& videoPath, int maxWidth, std::string& outError)
{
    close();
    outError.clear();

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, videoPath.c_str(), nullptr, nullptr) != 0 || fmt == nullptr) {
        outError = "avformat_open_input failed: " + videoPath;
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        outError = "avformat_find_stream_info failed";
        return false;
    }

    const int stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0) {
        avformat_close_input(&fmt);
        outError = "no video stream";
        return false;
    }

    const AVCodecParameters* par = fmt->streams[stream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (codec == nullptr) {
        avformat_close_input(&fmt);
        outError = "no decoder for the stream codec (is mpeg1video compiled in?)";
        return false;
    }
    AVCodecContext* dec = avcodec_alloc_context3(codec);
    if (dec == nullptr || avcodec_parameters_to_context(dec, par) < 0
        || avcodec_open2(dec, codec, nullptr) < 0) {
        if (dec != nullptr) {
            avcodec_free_context(&dec);
        }
        avformat_close_input(&fmt);
        outError = "avcodec_open2 failed";
        return false;
    }

    // Output size: keep the source aspect, then scale into a 16:9 RGBA buffer so
    // every frame is the same byte count (the caller may treat it as a record).
    int width = std::clamp(maxWidth > 0 ? maxWidth : kOutWidth, 160, 1920);
    if (width % 2 != 0) {
        ++width;
    }
    const int height = width * 9 / 16;

    const AVRational rate = fmt->streams[stream]->avg_frame_rate.num > 0
        ? fmt->streams[stream]->avg_frame_rate
        : fmt->streams[stream]->r_frame_rate;
    int fps = rate.num > 0 && rate.den > 0 ? static_cast<int>(rate.num / rate.den + 0.5) : 24;
    if (fps < 1 || fps > 240) {
        fps = 24; // usm's own header says 24; do not trust a bogus rational
    }
    double fpsExact = rate.num > 0 && rate.den > 0
        ? static_cast<double>(rate.num) / static_cast<double>(rate.den)
        : 24.0;
    if (fpsExact < 1.0 || fpsExact > 240.0) {
        fpsExact = 24.0; // same distrust as above
    }

    SwsContext* sws = sws_getContext(dec->width, dec->height, dec->pix_fmt, width, height,
        AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws == nullptr) {
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        outError = "sws_getContext failed";
        return false;
    }

    mFmt = fmt;
    mDec = dec;
    mSws = sws;
    mPacket = av_packet_alloc();
    mAvFrame = av_frame_alloc();
    mStreamIndex = stream;
    mWidth = width;
    mHeight = height;
    mFps = fps;
    mFpsExact = fpsExact;
    mFrame = -1;
    mEnded = false;
    mBuffer.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);

    logf(LogPlay, "[mv] opened %s src=%dx%d out=%dx%d @%d fps\n", videoPath.c_str(), dec->width,
        dec->height, width, height, fps);
    std::fflush(stdout);

    decodeNextFrame(); // one frame up front so the caller has something to upload
    return true;
}

bool VideoPlayer::decodeNextFrame()
{
    if (mFmt == nullptr || mEnded) {
        return false;
    }
    auto* fmt = static_cast<AVFormatContext*>(mFmt);
    auto* dec = static_cast<AVCodecContext*>(mDec);
    auto* pkt = static_cast<AVPacket*>(mPacket);
    auto* frm = static_cast<AVFrame*>(mAvFrame);
    auto* sws = static_cast<SwsContext*>(mSws);

    std::uint8_t* dst[4] = {mBuffer.data(), nullptr, nullptr, nullptr};
    const int dstStride[4] = {mWidth * 4, 0, 0, 0};

    while (true) {
        const int ret = av_read_frame(fmt, pkt);
        if (ret < 0) {
            // Flush: the decoder may still hold a frame or two.
            avcodec_send_packet(dec, nullptr);
            if (avcodec_receive_frame(dec, frm) == 0) {
                sws_scale(sws, frm->data, frm->linesize, 0, dec->height, dst, dstStride);
                av_frame_unref(frm);
                ++mFrame;
                return true;
            }
            mEnded = true;
            return false;
        }
        if (pkt->stream_index != mStreamIndex) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(dec, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);
        if (avcodec_receive_frame(dec, frm) == 0) {
            sws_scale(sws, frm->data, frm->linesize, 0, dec->height, dst, dstStride);
            av_frame_unref(frm);
            ++mFrame;
            return true;
        }
    }
}

bool VideoPlayer::pumpTo(int targetFrame)
{
    if (mFmt == nullptr) {
        return false;
    }
    if (targetFrame < 0) {
        targetFrame = 0;
    }
    auto* fmt = static_cast<AVFormatContext*>(mFmt);
    auto* dec = static_cast<AVCodecContext*>(mDec);
    // Backwards target (progress-bar seek, retry): rewind to the stream start
    // and decode forward again. Without this the video would sit on a stale
    // frame until song time caught back up - visibly out of sync.
    if (targetFrame < mFrame) {
        if (av_seek_frame(fmt, mStreamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0) {
            av_seek_frame(fmt, 0, 0, AVSEEK_FLAG_BACKWARD);
        }
        avcodec_flush_buffers(dec);
        mFrame = -1;
        mEnded = false;
    }
    // The audio clock is the master, so a lagging video simply decodes a few
    // frames back to back to catch up.
    while (mFrame < targetFrame && !mEnded) {
        if (!decodeNextFrame()) {
            break;
        }
    }
    return mFrame >= 0;
}

void VideoPlayer::close()
{
    if (mSws != nullptr) {
        sws_freeContext(static_cast<SwsContext*>(mSws));
        mSws = nullptr;
    }
    if (mAvFrame != nullptr) {
        AVFrame* frm = static_cast<AVFrame*>(mAvFrame);
        av_frame_free(&frm);
        mAvFrame = nullptr;
    }
    if (mPacket != nullptr) {
        AVPacket* pkt = static_cast<AVPacket*>(mPacket);
        av_packet_free(&pkt);
        mPacket = nullptr;
    }
    if (mDec != nullptr) {
        AVCodecContext* dec = static_cast<AVCodecContext*>(mDec);
        avcodec_free_context(&dec);
        mDec = nullptr;
    }
    if (mFmt != nullptr) {
        AVFormatContext* fmt = static_cast<AVFormatContext*>(mFmt);
        avformat_close_input(&fmt);
        mFmt = nullptr;
    }
    mFrame = -1;
    mEnded = false;
    mBuffer.clear();
}

} // namespace platform
