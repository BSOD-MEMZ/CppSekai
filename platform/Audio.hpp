// CppSekai - miniaudio based audio engine
// Music playback + song clock (audio-clock-as-master) + pooled SE voices.
#pragma once

#include <miniaudio.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace platform
{

class AudioEngine
{
  public:
    static constexpr int SE_POOL = 4;

    struct SeBank
    {
        // Index matches SeKind.
        std::array<std::array<ma_sound, SE_POOL>, 10> sounds;
        std::array<int, 10> next = {};
        bool loaded = false;
    };

    enum SeKind
    {
        SePerfect = 0,
        SeCriticalTap,
        SeFlick,
        SeFlickCritical,
        SeTrace,
        SeTraceCritical,
        SeTick,
        SeTickCritical,
        SeHoldLoop,
        SeHoldLoopCritical,
    };

    bool init(std::string& outError);
    void shutdown();

    bool loadMusic(const std::string& path, std::string& outError);
    bool loadSe(const std::string& dir, std::string& outError);

    // Starts the internal clock at songTime = -leadInSec; update() then starts
    // the music itself once the clock reaches 0.
    void start(double leadInSec);

    // ---- Chart / audio alignment ----------------------------------------
    //
    // Official pjsk audio files start with a few seconds of silence (the
    // "filler" - see fillerSec in the game's musics.json, 9.0s for most
    // songs). The chart's tick 0 sits *after* that silence, so:
    //
    //     music file position = songTime + startPos + userOffset
    //
    // startPos comes from the chart sidecar or from detectLeadingSilence();
    // userOffset is the manual fine tune (positive = music plays later).
    void setMusicStartPos(double seconds) { mMusicStartPosSec = seconds; }
    double musicStartPos() const { return mMusicStartPosSec; }
    void setUserOffset(double seconds) { mUserOffsetSec = seconds; }
    double userOffset() const { return mUserOffsetSec; }

    // Length of the loaded music file in seconds (0 when unknown).
    double musicDurationSec() const;

    // Scans the start of an audio file and returns how many seconds of silence
    // precede the first audible sample. Returns 0 when there is no meaningful
    // leading silence (or the file cannot be decoded).
    static double detectLeadingSilence(const std::string& path, double maxScanSec = 20.0);

    void stopMusic();

    // Must be called once per frame; starts the music when the lead-in ends.
    void update();

    // Current chart time in seconds (negative = lead-in).
    double songTime() const;
    double sampleRate() const;

    bool musicPlaying() const { return mMusicStarted; }
    bool paused() const { return mPaused; }

    void pause();
    void resume();

    void playSe(SeKind kind, float volume);

    // Hold loop SE: starts a looping voice while a hold is active, stops it
    // (with a short fade handled by calling code each frame) when not. Uses
    // the last pooled voice of the two hold-loop kinds, so one-shots are
    // unaffected. Safe to call every frame.
    void setHoldLoop(bool active, bool critical, float volume);

    bool hasMusic() const { return mMusicLoaded; }

  private:
    ma_engine mEngine{};
    bool mEngineInitialized = false;
    bool mMusicLoaded = false;
    ma_sound mMusic{};

    SeBank mSe{};
    bool mHoldLoopPlaying = false;
    int mHoldLoopKind = -1;

    bool mStarted = false;
    bool mMusicStarted = false;
    bool mPaused = false;
    double mLeadInSec = 0.0;
    double mMusicStartPosSec = 0.0; // music file position that is chart time 0
    double mUserOffsetSec = 0.0;    // manual fine tune, added to the above
    std::uint64_t mAnchorFrames = 0;      // engine frame counter when clock armed
    std::uint64_t mMusicStartFrames = 0;  // engine frame counter when music actually started
    double mPauseSongTime = 0.0;
};

} // namespace platform
