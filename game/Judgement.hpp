// CppSekai - judgement engine
// Consumes the packed HitEvent stream produced by the chart core and turns
// player input (multi-touch / keyboard) into PERFECT / GREAT / GOOD / MISS.
//
// HitEvent packed layout (7 floats per event, sorted by timeSec):
//   [0] timeSec   note hit time in chart seconds
//   [1] center    lane coordinate of note center (world x units)
//   [2] width     note width in lane units
//   [3] kind      0=tap 1=critical tap 2=flick 3=trace(friction)
//                 4=hold tick(auto) 5=hold start (endTimeSec valid)
//   [4] flags     bit0 = critical, bits 1-2 = flick direction (FlickDir)
//   [5] endTimeSec  hold end time (-1 for non-holds)
//   [6] volume    SE volume at this note
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace game
{

// Flick direction, mirroring the core's FlickType (None/Default/Left/Right).
enum FlickDir : std::uint8_t
{
    FlickNone = 0,
    FlickUp = 1,
    FlickLeft = 2,
    FlickRight = 3,
};

enum class Judge : std::uint8_t
{
    Perfect,
    Great,
    Good,
    Miss,
    None,
};

struct HitNote
{
    float timeSec = 0.0f;
    float center = 0.0f;
    float width = 1.0f;
    float kind = 0.0f;
    float flags = 0.0f;
    float endTimeSec = -1.0f;
    float volume = 1.0f;

    std::uint8_t state = 0; // 0 pending, 1 hit, 2 missed
    // Set for the note the chart core emits at a hold's end time (SUS
    // NoteType::HoldEnd). It looks like a plain tap in the HitEvent stream,
    // but pjsk judges it by *releasing* the lane at the tail, so it must not
    // go through the plain auto-miss path.
    bool holdTail = false;
};

struct JudgementWindows
{
    float perfectMs = 40.0f;
    float greatMs = 90.0f;
    float goodMs = 140.0f;
    float missAfterMs = 180.0f;
};

// Life pool. pjsk starts every live at 1000 life (skills can push it to
// 2000, which we do not model). Judgement costs, from the official table:
// MISS -80, a hold broken mid-way -40 (BAD -50 is unused - the engine has no
// BAD judgement).
constexpr float kMaxLife = 1000.0f;

// Score constants ported from sekai-mmw-preview-web's overlay player
// (native/src/mmw_overlay_player.cpp). The score of a note is
//   (TEAM_POWER / weightedNoteCount) * 4 * noteWeight * levelFactor * comboFactor
// scaled by the judgement multiplier (PERFECT 1.0 / GREAT 0.7 / GOOD 0.5),
// with comboFactor growing by 0.01 per 100 combo up to 1.1.
constexpr double kTeamPower = 250000.0;
constexpr float kDefaultChartRating = 26.0f;

struct JudgementStats
{
    int perfect = 0;
    int great = 0;
    int good = 0;
    int miss = 0;
    int combo = 0;
    int maxCombo = 0;
    double score = 0.0;
    float life = kMaxLife;
    int holdTails = 0;  // hold tails judged by releasing / holding through
    int holdBreaks = 0; // holds let go too early (mid-hold MISS)

    Judge lastJudge = Judge::None;
    float lastJudgeTimeSec = -100.0f;
    bool lastJudgeCritical = false;
    float lastHitKind = -1.0f;   // HitEvent kind of the last successful player hit
    float lastHitCenter = 0.0f;  // lane coordinate of the last successful player hit
    float lastHitWidth = 0.0f;   // lane width of the last successful player hit
    float lastHitTimeSec = -1.0f; // the hit note's own time (HitEvent timeSec)
    std::uint8_t lastHitFlickDir = 0; // the note's flick direction (FlickDir)
    bool lastHitFriction = false;
};

class JudgementEngine
{
  public:
    // Packed HitEvents from the chart core (getHitEventBufferPointer).
    void load(const float* packed, int count);
    void reset();

    // Advances auto-miss / hold tracking. Call once per frame with chart time.
    void update(float songTimeSec);

    // Player input. lanePos is in lane coordinates (world x units);
    // a note matches when center - width/2 - margin <= lanePos <= center + width/2 + margin.
    // Flick input carries the swipe direction; in strict mode (default) a
    // flick note only clears when the direction matches (up flicks also
    // accept the ambiguous FlickNone), and a plain tap never clears a flick.
    // Returns the judge result if a note was hit, Judge::None otherwise.
    Judge tap(float lanePos, float songTimeSec, bool critical, float margin = 0.5f);
    Judge flick(float lanePos, float songTimeSec, FlickDir dir, float margin = 0.5f);

    // Keyboard lane tracking for hold notes. heldLanes is a list of currently
    // held lane positions; touches count as holds while the finger is down.
    void setHoldLanes(const std::vector<float>& lanes) { mHoldLanes = lanes; }

    // True while at least one hold is being tracked (started and not yet
    // finished/broken). criticalOut receives whether it is a critical hold -
    // used to pick the hold loop SE variant.
    [[nodiscard]] bool anyActiveHold(bool* criticalOut = nullptr) const;

    [[nodiscard]] const JudgementStats& stats() const { return mStats; }
    [[nodiscard]] const JudgementWindows& windows() const { return mWindows; }
    void setWindows(const JudgementWindows& windows) { mWindows = windows; }

    // Chart level, used by the score formula's levelFactor (the upstream
    // overlay player hard-codes 26; we use the song's real difficulty level).
    void setChartRating(float rating)
    {
        if (rating > 0.0f) {
            mChartRating = rating;
        }
    }
    [[nodiscard]] float chartRating() const { return mChartRating; }

    // Life in 0..1, for the HUD's life bar.
    [[nodiscard]] float lifeRatio() const { return mStats.life / kMaxLife; }

    // Strict flick validation: on (default) a flick needs a matching swipe
    // direction and taps never clear flicks; off restores the lenient
    // skeleton behavior (any flick gesture / tap clears any flick note).
    void setStrictFlick(bool strict) { mStrictFlick = strict; }
    [[nodiscard]] bool strictFlick() const { return mStrictFlick; }

    [[nodiscard]] bool loaded() const { return mLoaded; }
    [[nodiscard]] int totalNotes() const { return mTotalScoreNotes; }

    [[nodiscard]] bool hasPendingNotes() const { return mCursor < mNotes.size(); }

  private:
    struct ActiveHold
    {
        std::size_t noteIndex = 0;   // the kind 5 marker
        std::size_t startIndex = static_cast<std::size_t>(-1); // the start tap
        float startTimeSec = 0.0f;
        float endTimeSec = 0.0f;
        float center = 0.0f;
        float width = 1.0f;
        bool broken = false;
        // Tail bookkeeping: kind 5 markers carry the hold's end time, while
        // the *scoreable* end note sits at that same time as a 0/1/2/3 event.
        std::size_t tailIndex = static_cast<std::size_t>(-1);
        bool released = false;
        float releaseTimeSec = -1.0f;
    };

    std::vector<HitNote> mNotes;
    std::vector<ActiveHold> mActiveHolds;
    std::vector<float> mHoldLanes;
    std::size_t mCursor = 0;
    bool mLoaded = false;
    int mTotalScoreNotes = 0;

    JudgementWindows mWindows;
    JudgementStats mStats;
    bool mStrictFlick = true;

    // Score model (see kTeamPower above).
    float mChartRating = kDefaultChartRating;
    double mWeightedNoteCount = 1.0;
    double mComboFactor = 1.0;

    Judge registerJudge(Judge judge, bool critical, float volume, float kind);
    void registerMiss(float songTimeSec, float lifeCost);
    double scoreDeltaFor(float kind, bool critical) const;
    void judgeHoldTail(ActiveHold& hold, Judge judge, float songTimeSec);
    HitNote* findCandidate(float lanePos, float songTimeSec, float margin, bool wantFlick,
        FlickDir flickDir = FlickNone);
    bool laneCovers(const HitNote& note, float lanePos, float margin) const;
    bool laneHeld(const ActiveHold& hold) const;

    // The score weight of one note, mirroring the upstream getHudWeight().
    static float hudWeight(float kind, bool critical)
    {
        switch (static_cast<int>(kind)) {
            case 2: return critical ? 3.0f : 1.0f;  // flick
            case 3: return critical ? 0.2f : 0.1f;  // trace
            case 4: return critical ? 0.2f : 0.1f;  // hold tick
            case 0: case 1: return critical ? 2.0f : 1.0f; // tap / critical tap
            default: return 0.0f;                   // kind 5 = hold marker (not scored)
        }
    }

    static double judgeMultiplier(Judge judge)
    {
        switch (judge) {
            case Judge::Perfect: return 1.0;
            case Judge::Great: return 0.7;
            case Judge::Good: return 0.5;
            default: return 0.0;
        }
    }

    // flags packing (see mmw_preview.cpp): bit0 = critical, bits 1-2 = FlickDir.
    static std::uint8_t noteFlickDir(const HitNote& note)
    {
        return static_cast<std::uint8_t>((static_cast<int>(note.flags) >> 1) & 3);
    }
};

} // namespace game
