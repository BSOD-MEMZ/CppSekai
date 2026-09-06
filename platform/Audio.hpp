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

    // Starts the internal clock at songTime = -leadInSec. Music starts
    // automatically when the clock crosses (musicDelaySec), which implements
    // the SUS offset ("audio starts N seconds after chart time zero").
    void start(double leadInSec);
    void setMusicDelay(double delaySec) { mMusicDelaySec = delaySec; }
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

    bool hasMusic() const { return mMusicLoaded; }

  private:
    ma_engine mEngine{};
    bool mEngineInitialized = false;
    bool mMusicLoaded = false;
    ma_sound mMusic{};

    SeBank mSe{};

    bool mStarted = false;
    bool mMusicStarted = false;
    bool mPaused = false;
    double mLeadInSec = 0.0;
    double mMusicDelaySec = 0.0;
    std::uint64_t mAnchorFrames = 0;      // engine frame counter when clock armed
    std::uint64_t mMusicStartFrames = 0;  // engine frame counter when music actually started
    double mPauseSongTime = 0.0;
};

} // namespace platform
