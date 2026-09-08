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

    std::uint8_t state = 0; // 0 pending, 1 judged
};

struct JudgementWindows
{
    float perfectMs = 40.0f;
    float greatMs = 90.0f;
    float goodMs = 140.0f;
    float missAfterMs = 180.0f;
};

struct JudgementStats
{
    int perfect = 0;
    int great = 0;
    int good = 0;
    int miss = 0;
    int combo = 0;
    int maxCombo = 0;
    double score = 0.0;

    Judge lastJudge = Judge::None;
    float lastJudgeTimeSec = -100.0f;
    bool lastJudgeCritical = false;
    float lastHitKind = -1.0f;   // HitEvent kind of the last successful player hit
    float lastHitCenter = 0.0f;  // lane coordinate of the last successful player hit
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
    // isFlick: the gesture was an upward swipe (leniently accepted for tap input in this skeleton).
    // Returns the judge result if a note was hit, Judge::None otherwise.
    Judge tap(float lanePos, float songTimeSec, bool critical, float margin = 0.5f);
    Judge flick(float lanePos, float songTimeSec, float margin = 0.5f);

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

    [[nodiscard]] bool loaded() const { return mLoaded; }
    [[nodiscard]] int totalNotes() const { return mTotalScoreNotes; }

    [[nodiscard]] bool hasPendingNotes() const { return mCursor < mNotes.size(); }

  private:
    struct ActiveHold
    {
        std::size_t noteIndex = 0;
        float endTimeSec = 0.0f;
        float center = 0.0f;
        float width = 1.0f;
        bool broken = false;
    };

    std::vector<HitNote> mNotes;
    std::vector<ActiveHold> mActiveHolds;
    std::vector<float> mHoldLanes;
    std::size_t mCursor = 0;
    bool mLoaded = false;
    int mTotalScoreNotes = 0;

    JudgementWindows mWindows;
    JudgementStats mStats;

    Judge registerJudge(Judge judge, bool critical, float volume);
    void registerMiss(float songTimeSec);
    HitNote* findCandidate(float lanePos, float songTimeSec, float margin, bool wantFlick);
    bool laneCovers(const HitNote& note, float lanePos, float margin) const;
};

} // namespace game
