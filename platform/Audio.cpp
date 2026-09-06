// miniaudio implementation lives in this translation unit.
#ifndef MINIAUDIO_IMPLEMENTATION
#define MINIAUDIO_IMPLEMENTATION
#endif

#include "Audio.hpp"

#include <algorithm>
#include <cmath>

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
    if (songTime() >= mMusicDelaySec) {
        ma_sound_set_start_time_in_pcm_frames(&mMusic, 0);
        ma_sound_start(&mMusic);
        mMusicStartFrames = mAnchorFrames + static_cast<std::uint64_t>((mLeadInSec + mMusicDelaySec) * sampleRate());
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
    return static_cast<double>(now - mMusicStartFrames) / sampleRate() + mMusicDelaySec;
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
    if (mMusicStarted) {
        // Re-anchor: restore the music at the paused audio position.
        const double musicPos = mPauseSongTime - mMusicDelaySec;
        ma_sound_seek_to_pcm_frame(&mMusic, static_cast<ma_uint64>(std::max(0.0, musicPos) * sampleRate()));
        ma_sound_start(&mMusic);
        mMusicStartFrames = ma_engine_get_time_in_pcm_frames(&mEngine) - static_cast<std::uint64_t>(std::max(0.0, musicPos) * sampleRate());
    } else {
        // Still in lead-in: re-anchor the lead-in clock.
        const double remaining = -mPauseSongTime;
        mAnchorFrames = ma_engine_get_time_in_pcm_frames(&mEngine) - static_cast<std::uint64_t>(remaining * sampleRate());
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

} // namespace platform
