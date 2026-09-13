// miniaudio implementation lives in this translation unit.
#ifndef MINIAUDIO_IMPLEMENTATION
#define MINIAUDIO_IMPLEMENTATION
#endif

#include "Audio.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    if (mCountdownSeLoaded) {
        ma_sound_uninit(&mCountdownSe);
        mCountdownSeLoaded = false;
    }
    stopPreview();
    stopResultBgm();
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

    // Resume-countdown beep (optional - a missing file just disables it).
    const std::string countdownPath = dir + "/count_down.mp3";
    if (ma_sound_init_from_file(&mEngine, countdownPath.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr,
            &mCountdownSe)
        == MA_SUCCESS) {
        mCountdownSeLoaded = true;
    } else {
        std::printf("[audio] no countdown SE (%s)\n", countdownPath.c_str());
    }
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

void AudioEngine::skipLeadIn()
{
    if (!mStarted || mMusicStarted || mPaused || !mMusicLoaded) {
        return;
    }
    const double filePos = std::max(0.0, mMusicStartPosSec + mUserOffsetSec);
    ma_sound_seek_to_pcm_frame(&mMusic, static_cast<ma_uint64>(filePos * sampleRate()));
    ma_sound_start(&mMusic);
    mMusicStartFrames = ma_engine_get_time_in_pcm_frames(&mEngine);
    mMusicStarted = true;
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
    stopPreview(); // a running preview must never leak into a play session
}

bool AudioEngine::startPreview(const std::string& path, std::string& outError, bool carryPosition)
{
    if (mPreviewActive && mPreviewLoaded && mPreviewPath == path) {
        return true; // already playing exactly this clip
    }
    // Switching between two versions of the same song (a vocal chip): keep the
    // position the player is listening at instead of jumping back to the clip
    // start, so the switch sounds like the same take with a different singer.
    // Both versions share the song's structure, so a clip-relative offset
    // lands on the same bar.
    double carriedSec = 0.0;
    if (carryPosition && mPreviewActive && mPreviewLoaded) {
        ma_uint64 cursor = 0;
        if (ma_sound_get_cursor_in_pcm_frames(&mPreviewSound, &cursor) == MA_SUCCESS) {
            carriedSec = std::max(0.0, static_cast<double>(cursor) / sampleRate() - mPreviewStartSec);
        }
    }
    stopPreview();
    if (path.empty()) {
        return true; // no BGM for this chart: stay silent
    }
    // STREAM, not DECODE: this runs on the UI thread between frames, and a
    // full pre-decode of the track visibly froze the song list on every
    // selection change. Streaming opens the file and decodes on the fly.
    const auto loadStart = std::chrono::steady_clock::now();
    if (ma_sound_init_from_file(&mEngine, path.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr,
            &mPreviewSound)
        != MA_SUCCESS) {
        outError = "failed to load preview: " + path;
        mPreviewPath.clear();
        return false;
    }
    mPreviewLoaded = true;

    ma_uint64 lengthFrames = 0;
    if (ma_sound_get_length_in_pcm_frames(&mPreviewSound, &lengthFrames) != MA_SUCCESS) {
        lengthFrames = 0;
    }
    const double len = static_cast<double>(lengthFrames) / sampleRate();
    if (len < 4.0) {
        // Too short to cut a meaningful clip - treat as silence.
        stopPreview();
        return false;
    }
    // The official select-screen preview plays an excerpt from partway into
    // the song, never the intro. Approximate it: clip starts at ~35% of the
    // track and loops after 30 seconds.
    mPreviewStartSec = std::min(len * 0.35, len - 10.0);
    mPreviewEndSec = std::min(len, mPreviewStartSec + 30.0);
    // Continue where the previous version's preview was (clamped inside the
    // new clip so a carried offset can never seek past its end).
    const double seekSec =
        std::clamp(mPreviewStartSec + carriedSec, mPreviewStartSec, mPreviewEndSec - 1.0);
    ma_sound_set_volume(&mPreviewSound, 0.85f);
    ma_sound_seek_to_pcm_frame(&mPreviewSound, static_cast<ma_uint64>(seekSec * sampleRate()));
    ma_sound_start(&mPreviewSound);
    mPreviewActive = true;
    mPreviewPath = path;
    const auto loadMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - loadStart)
            .count();
    std::printf("[audio] preview streaming %s%s (%.0f ms)\n", path.c_str(),
        carriedSec > 0.05 ? " (continued)" : "", static_cast<double>(loadMs));
    return true;
}

void AudioEngine::updatePreview()
{
    if (!mPreviewActive || !mPreviewLoaded || mPaused) {
        return;
    }
    ma_uint64 cursor = 0;
    if (ma_sound_get_cursor_in_pcm_frames(&mPreviewSound, &cursor) == MA_SUCCESS) {
        const double pos = static_cast<double>(cursor) / sampleRate();
        if (pos >= mPreviewEndSec || pos < mPreviewStartSec - 0.5) {
            ma_sound_seek_to_pcm_frame(&mPreviewSound,
                static_cast<ma_uint64>(mPreviewStartSec * sampleRate()));
        }
    }
}

void AudioEngine::stopPreview()
{
    if (mPreviewLoaded) {
        ma_sound_stop(&mPreviewSound);
        ma_sound_uninit(&mPreviewSound);
        mPreviewLoaded = false;
    }
    mPreviewActive = false;
    mPreviewPath.clear();
}

bool AudioEngine::startResultBgm(const std::string& path, float volume, std::string& outError)
{
    if (mResultBgmActive && mResultBgmLoaded && mResultBgmPath == path) {
        return true; // already looping exactly this track
    }
    stopResultBgm();
    if (path.empty()) {
        return true; // no result track shipped: stay silent
    }
    // Stream (like the song-select preview): the result screen appears right
    // after gameplay, and a full decode would stall that transition.
    if (ma_sound_init_from_file(&mEngine, path.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr,
            &mResultBgm)
        != MA_SUCCESS) {
        outError = "failed to load result bgm: " + path;
        mResultBgmPath.clear();
        return false;
    }
    mResultBgmLoaded = true;
    mResultBgmPath = path;
    ma_sound_set_volume(&mResultBgm, volume);
    ma_sound_set_looping(&mResultBgm, MA_TRUE);
    ma_sound_start(&mResultBgm);
    mResultBgmActive = true;
    std::printf("[audio] result bgm looping %s\n", path.c_str());
    return true;
}

void AudioEngine::stopResultBgm()
{
    if (mResultBgmLoaded) {
        ma_sound_stop(&mResultBgm);
        ma_sound_uninit(&mResultBgm);
        mResultBgmLoaded = false;
    }
    mResultBgmActive = false;
    mResultBgmPath.clear();
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

void AudioEngine::playCountdownSe(float volume)
{
    if (!mCountdownSeLoaded) {
        return;
    }
    ma_sound_stop(&mCountdownSe);
    ma_sound_seek_to_pcm_frame(&mCountdownSe, 0);
    ma_sound_set_volume(&mCountdownSe, std::max(0.0f, volume));
    ma_sound_start(&mCountdownSe);
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
