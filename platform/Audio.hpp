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

    // Menu / dialog sound effects (assets/se: click, select, level_choose,
    // window_open, window_close). Ordered by priority: when two are requested in
    // the same frame the later one wins, so a click that opens a dialog is
    // swallowed by the window_open (that file already has a click baked in) and
    // a dialog that closes plays window_close instead of the click that
    // dismissed it. Keep this order in sync with ui::SeKind.
    enum UiSe
    {
        UiSeClick = 0,
        UiSeSelect,
        UiSeLevelChoose,
        UiSeWindowOpen,
        UiSeWindowClose,
        UiSeCount,
    };
    static constexpr int UI_SE_POOL = 3;

    bool init(std::string& outError);
    void shutdown();

    bool loadMusic(const std::string& path, std::string& outError);
    bool loadSe(const std::string& dir, std::string& outError);
    // Loads the UI sound effects. Missing files just leave those slots silent.
    void loadUiSe(const std::string& dir);
    void playUiSe(UiSe kind, float volume);

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

    // Jumps the lead-in clock straight to chart time 0 and starts the music
    // right now (the opening card's skip button). No-op once the music is
    // already playing, while paused, or when there is no music loaded.
    void skipLeadIn();

    // Current chart time in seconds (negative = lead-in).
    double songTime() const;
    double sampleRate() const;

    bool musicPlaying() const { return mMusicStarted; }
    bool paused() const { return mPaused; }

    void pause();
    void resume();

    void playSe(SeKind kind, float volume);

    // ---- Master BGM volume (0..1, the settings card's BGM slider) --------
    // Scales every *music* voice: the gameplay track, the song select preview
    // and the result BGM. The engine owns the value (rather than callers
    // passing it around) so it also applies at load time: set it once at
    // startup, before the first loadMusic / startPreview / startResultBgm.
    void setBgmVolume(float volume);
    float bgmVolume() const { return mBgmVolume; }

    // One-shot countdown beep (assets/se/count_down.mp3), played by the
    // resume countdown. A missing file just disables it.
    void playCountdownSe(float volume);

    // ---- Song-select music preview ---------------------------------------
    // Plays a clip cut from partway into the track (like the official select
    // screen, which never previews from the top) and loops it. The preview
    // sound STREAMS from disk instead of pre-decoding: the song select loads
    // it on the UI thread, and a full decode stalled the list on every
    // selection change. startPreview is safe to call every frame with the
    // same path - it only reloads when the path changes; an empty path just
    // stops the preview.
    // `carryPosition` keeps the current clip offset when the path changes -
    // used when switching between two vocal versions of the same song, so the
    // switch sounds like the same take with a different singer. Leave it false
    // when the *song* changed.
    bool startPreview(const std::string& path, std::string& outError, bool carryPosition = false);
    // Call once per frame while the preview shows: loops the clip.
    void updatePreview();
    void stopPreview();
    bool previewActive() const { return mPreviewActive; }

    // ---- Result screen BGM ----------------------------------------------
    // The result screen loops the game's own result track (assets/ost). It is
    // independent of the chart music - that one has already been stopped - and
    // streams from disk, so entering the screen costs no decode time.
    // Safe to call every frame with the same path. A missing file reports an
    // error and leaves the screen silent.
    bool startResultBgm(const std::string& path, float volume, std::string& outError);
    void stopResultBgm();
    bool resultBgmActive() const { return mResultBgmActive; }

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

    std::array<std::array<ma_sound, UI_SE_POOL>, UiSeCount> mUiSe{};
    std::array<int, UiSeCount> mUiSeNext = {};
    std::array<bool, UiSeCount> mUiSeLoaded = {};

    bool mCountdownSeLoaded = false;
    ma_sound mCountdownSe{};

    bool mPreviewActive = false;
    std::string mPreviewPath;
    double mPreviewStartSec = 0.0;
    double mPreviewEndSec = 0.0;
    bool mPreviewLoaded = false;
    ma_sound mPreviewSound{}; // streaming; separate from the pre-decoded gameplay music

    bool mResultBgmActive = false;
    bool mResultBgmLoaded = false;
    std::string mResultBgmPath;
    ma_sound mResultBgm{}; // streaming, looping

    bool mStarted = false;
    bool mMusicStarted = false;
    bool mPaused = false;
    float mBgmVolume = 1.0f; // master volume for every music voice
    double mLeadInSec = 0.0;
    double mMusicStartPosSec = 0.0; // music file position that is chart time 0
    double mUserOffsetSec = 0.0;    // manual fine tune, added to the above
    std::uint64_t mAnchorFrames = 0;      // engine frame counter when clock armed
    std::uint64_t mMusicStartFrames = 0;  // engine frame counter when music actually started
    double mPauseSongTime = 0.0;
};

} // namespace platform
