// miniaudio implementation lives in this translation unit.
#ifndef MINIAUDIO_IMPLEMENTATION
#define MINIAUDIO_IMPLEMENTATION
#endif

#include "Audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace platform
{

bool AudioEngine::init(std::string& outError)
{
    const ma_result result = ma_engine_init(nullptr, &mEngine);
    if (result != MA_SUCCESS) {
        outError = std::string("ma_engine_init failed: ") + ma_result_description(result);
        return false;
    }
    mEngineInitialized = true;
    return true;
}

void AudioEngine::shutdown()
{
    if (mMusicLoaded) {
        ma_sound_uninit(&mMusic);
        mMusicLoaded = false;
    }
    if (mSe.loaded) {
        for (auto& kind : mSe.sounds) {
            for (auto& sound : kind) {
                ma_sound_uninit(&sound);
            }
        }
        mSe.loaded = false;
    }
    if (mEngineInitialized) {
        ma_engine_uninit(&mEngine);
        mEngineInitialized = false;
    }
}

bool AudioEngine::loadMusic(const std::string& path, std::string& outError)
{
    if (!mEngineInitialized) {
        outError = "audio engine not initialized";
        return false;
    }
    // Release the previous track first. miniaudio zeroes the ma_sound inside
    // ma_sound_init_from_file, so re-initialising a live sound (give up ->
    // pick another song / retry) drops its resource-manager entry on the
    // floor: the engine then walks a corrupt list and the process hangs.
    if (mMusicLoaded) {
        ma_sound_stop(&mMusic);
        ma_sound_uninit(&mMusic);
        mMusicLoaded = false;
        mMusicStarted = false;
    }
    const ma_result result = ma_sound_init_from_file(&mEngine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &mMusic);
    if (result != MA_SUCCESS) {
        outError = std::string("failed to load music: ") + path;
        return false;
    }
    mMusicLoaded = true;
    return true;
}

bool AudioEngine::loadSe(const std::string& dir, std::string& outError)
{
    static const char* kFiles[10] = {
        "se_live_perfect.mp3",
        "se_live_critical.mp3",
        "se_live_flick.mp3",
        "se_live_flick_critical.mp3",
        "se_live_trace.mp3",
        "se_live_trace_critical.mp3",
        "se_live_connect.mp3",
        "se_live_connect_critical.mp3",
        "se_live_long.mp3",
        "se_live_long_critical.mp3",
    };

    for (int kind = 0; kind < 10; ++kind) {
        for (int voice = 0; voice < SE_POOL; ++voice) {
            const std::string path = dir + "/" + kFiles[kind];
            const ma_result result = ma_sound_init_from_file(&mEngine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, &mSe.sounds[kind][voice]);
            if (result != MA_SUCCESS) {
                outError = std::string("failed to load SE: ") + path;
                return false;
            }
        }
    }
    mSe.loaded = true;
    return true;
}

void AudioEngine::start(double leadInSec)
{
    if (!mStarted) {
        mLeadInSec = leadInSec;
        mAnchorFrames = ma_engine_get_time_in_pcm_frames(&mEngine);
        mStarted = true;
        mPaused = false;
    }
}

double AudioEngine::musicDurationSec() const
{
    if (!mMusicLoaded) {
        return 0.0;
    }
    ma_uint64 length = 0;
    if (ma_sound_get_length_in_pcm_frames(const_cast<ma_sound*>(&mMusic), &length) != MA_SUCCESS || length == 0) {
        return 0.0;
    }
    return static_cast<double>(length) / sampleRate();
}

double AudioEngine::detectLeadingSilence(const std::string& path, double maxScanSec)
{
    if (path.empty() || maxScanSec <= 0.0) {
        return 0.0;
    }
    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 1, 44100);
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
        return 0.0;
    }

    constexpr ma_uint64 kBlockFrames = 1024;
    constexpr double kSampleRate = 44100.0;
    // -45 dBFS: well above the noise floor of a silent mp3, well below any
    // real musical content.
    constexpr ma_int16 kThreshold = 184;

    const ma_uint64 maxFrames = static_cast<ma_uint64>(maxScanSec * kSampleRate);
    std::vector<ma_int16> buffer(kBlockFrames);
    ma_uint64 consumed = 0;
    double firstLoudSec = -1.0;

    while (consumed < maxFrames) {
        ma_uint64 framesRead = 0;
        if (ma_decoder_read_pcm_frames(&decoder, buffer.data(), kBlockFrames, &framesRead) != MA_SUCCESS
            || framesRead == 0) {
            break;
        }
        ma_int16 peak = 0;
        ma_uint64 peakIndex = 0;
        for (ma_uint64 i = 0; i < framesRead; ++i) {
            const ma_int16 value = static_cast<ma_int16>(std::abs(static_cast<int>(buffer[static_cast<size_t>(i)])));
            if (value > peak) {
                peak = value;
                peakIndex = i;
            }
        }
        if (peak > kThreshold) {
            firstLoudSec = (static_cast<double>(consumed) + static_cast<double>(peakIndex)) / kSampleRate;
            break;
        }
        consumed += framesRead;
    }

    ma_decoder_uninit(&decoder);

    // Anything shorter than this is just an encoder gap, not a filler.
    if (firstLoudSec < 0.3) {
        return 0.0;
    }
    return firstLoudSec;
}

void AudioEngine::stopMusic()
{
    if (mMusicStarted) {
        ma_sound_stop(&mMusic);
        mMusicStarted = false;
    }
    mStarted = false;
}

void AudioEngine::update()
{
    if (!mStarted || mMusicStarted || mPaused || !mMusicLoaded) {
        return;
    }
    // Chart time 0 == the first audible sample of the song, which lives at
    // (startPos + userOffset) inside the file.
    if (songTime() >= 0.0) {
        const double filePos = std::max(0.0, mMusicStartPosSec + mUserOffsetSec);
        ma_sound_seek_to_pcm_frame(&mMusic, static_cast<ma_uint64>(filePos * sampleRate()));
        ma_sound_start(&mMusic);
        mMusicStartFrames = ma_engine_get_time_in_pcm_frames(&mEngine);
        mMusicStarted = true;
    }
}

double AudioEngine::sampleRate() const
{
    return static_cast<double>(ma_engine_get_sample_rate(const_cast<ma_engine*>(&mEngine)));
}

double AudioEngine::songTime() const
{
    if (!mStarted) {
        return 0.0;
    }
    if (mPaused) {
        return mPauseSongTime;
    }
    const std::uint64_t now = ma_engine_get_time_in_pcm_frames(const_cast<ma_engine*>(&mEngine));
    if (!mMusicStarted) {
        // Lead-in: start at -leadInSec and climb toward zero.
        return -mLeadInSec + static_cast<double>(now - mAnchorFrames) / sampleRate();
    }
    return static_cast<double>(now - mMusicStartFrames) / sampleRate();
}

void AudioEngine::pause()
{
    if (mPaused || !mStarted) {
        return;
    }
    mPauseSongTime = songTime();
    mPaused = true;
    if (mMusicStarted) {
        ma_sound_stop(&mMusic);
    }
}

void AudioEngine::resume()
{
    if (!mPaused) {
        return;
    }
    const double filePos = mPauseSongTime + mMusicStartPosSec + mUserOffsetSec;
    if (mMusicStarted) {
        // Re-anchor: put the music back at the position the clock expects.
        const double seekPos = std::max(0.0, filePos);
        ma_sound_seek_to_pcm_frame(&mMusic, static_cast<ma_uint64>(seekPos * sampleRate()));
        ma_sound_start(&mMusic);
        mMusicStartFrames =
            ma_engine_get_time_in_pcm_frames(&mEngine) - static_cast<std::uint64_t>(mPauseSongTime * sampleRate());
        mMusicStarted = true;
    } else {
        // Still in the lead-in: re-anchor the lead-in clock.
        mAnchorFrames = ma_engine_get_time_in_pcm_frames(&mEngine)
            - static_cast<std::uint64_t>((mLeadInSec + mPauseSongTime) * sampleRate());
    }
    mPaused = false;
}

void AudioEngine::playSe(SeKind kind, float volume)
{
    if (!mSe.loaded) {
        return;
    }
    const int index = static_cast<int>(kind);
    ma_sound& sound = mSe.sounds[index][mSe.next[index]];
    mSe.next[index] = (mSe.next[index] + 1) % SE_POOL;
    ma_sound_stop(&sound);
    ma_sound_seek_to_pcm_frame(&sound, 0);
    ma_sound_set_volume(&sound, std::max(0.0f, volume));
    ma_sound_start(&sound);
}

void AudioEngine::setHoldLoop(bool active, bool critical, float volume)
{
    if (!mSe.loaded) {
        return;
    }
    const int kind = critical ? SeHoldLoopCritical : SeHoldLoop;
    // Reserve the last pool voice of each hold-loop kind for the loop so the
    // one-shot rotation never steals it.
    ma_sound& loop = mSe.sounds[kind][SE_POOL - 1];

    if (active) {
        if (mHoldLoopPlaying && mHoldLoopKind != kind) {
            // Variant switched (critical flag changed): stop the old one.
            ma_sound& old = mSe.sounds[mHoldLoopKind][SE_POOL - 1];
            ma_sound_set_looping(&old, MA_FALSE);
            ma_sound_stop(&old);
            mHoldLoopPlaying = false;
        }
        if (!mHoldLoopPlaying) {
            ma_sound_stop(&loop);
            ma_sound_seek_to_pcm_frame(&loop, 0);
            ma_sound_set_looping(&loop, MA_TRUE);
            ma_sound_set_volume(&loop, std::max(0.0f, volume));
            ma_sound_start(&loop);
            mHoldLoopPlaying = true;
            mHoldLoopKind = kind;
        } else {
            ma_sound_set_volume(&loop, std::max(0.0f, volume));
        }
    } else if (mHoldLoopPlaying) {
        ma_sound& playing = mSe.sounds[mHoldLoopKind][SE_POOL - 1];
        ma_sound_set_looping(&playing, MA_FALSE);
        ma_sound_stop(&playing);
        mHoldLoopPlaying = false;
        mHoldLoopKind = -1;
    }
}

} // namespace platform
