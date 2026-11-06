#include "Judgement.hpp"
#include "platform/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace game
{

namespace
{
    // 无头验证开关（2026-11-01）：CPSEKAI_HOLD_TEST=1 时所有长条的起点强制
    // MISS、Auto 也不再当"按着"，用来截"没接上的 hold"该长什么样（钉在判定线、
    // 半透明、MISS 常驻）。正常游玩和 --auto 基线都不受影响。
    // 值写 "reconnect" 时再模拟"中途按回去"，用来验证接上之后 tick 继续判。
    const char* gHoldTestEnv = std::getenv("CPSEKAI_HOLD_TEST");
    const bool gHoldTest = gHoldTestEnv != nullptr;
    const bool gHoldReconnect = gHoldTestEnv != nullptr && gHoldTestEnv[0] == 'r';

    // Life costs, from the official pjsk judgement table: a whole-note MISS
    // costs 80, a BAD costs 50, and a hold broken mid-way costs 40.
    constexpr float kLifeMiss = -80.0f;
    constexpr float kLifeBad = -50.0f;
    constexpr float kLifeHoldBreak = -40.0f;

    // Release grace / grab grace now live in JudgementWindows
    // (holdTailGraceMs / holdStartGraceMs) so they can be tuned in the settings
    // dialog; see Judgement.hpp.

    // ------------------------------------------------------------------
    // Official judgement windows.
    //
    // master/ingameJudgeFrames.json (the runtime copy of dump.cs's
    // MasterIngameJudgeFrame) lists one row per note type, in units of 60 fps
    // frames, with EARLY and LATE tolerances split. They are genuinely
    // asymmetric: a flick forgives a late swipe by 1 frame more than an early
    // one, a hold END is looser when you let go late, and every friction note
    // uses one width for all four grades because the gesture is pass/fail.
    //
    // The engine used to have a single symmetric window and graded everything
    // with it, which is why a late tap could still count as BAD at 200 ms.
    constexpr float kFrameMs = 1000.0f / 60.0f;

    struct RawWindow
    {
        float pEarly, pLate, gEarly, gLate, gdEarly, gdLate, bEarly, bLate;
    };

    // Indexed by WindowType (Judgement.hpp). Values are frames.
    constexpr RawWindow kOfficialWindows[11] = {
        {2.5f, 2.5f, 5.0f, 5.0f, 6.5f, 6.5f, 7.5f, 7.5f}, // Normal
        {3.3f, 3.3f, 4.5f, 4.5f, 6.5f, 6.5f, 7.5f, 7.5f}, // NormalCritical
        {2.5f, 2.5f, 6.5f, 7.5f, 7.0f, 8.0f, 7.5f, 8.5f}, // Flick
        {3.5f, 3.5f, 6.5f, 7.5f, 7.0f, 8.0f, 7.5f, 8.5f}, // FlickCritical
        {3.5f, 4.0f, 6.5f, 8.0f, 7.5f, 8.5f, 7.5f, 8.5f}, // LongEnd
        {3.5f, 4.0f, 6.5f, 8.0f, 7.5f, 8.5f, 7.5f, 8.5f}, // LongEndCritical
        {3.5f, 4.0f, 6.5f, 8.0f, 7.5f, 8.5f, 7.5f, 8.5f}, // LongEndFlick
        {5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f}, // Friction
        {5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f}, // FrictionCritical
        {6.5f, 8.0f, 6.5f, 8.0f, 6.5f, 8.0f, 6.5f, 8.0f}, // FrictionLongEnd
        {6.5f, 7.5f, 6.5f, 7.5f, 6.5f, 7.5f, 6.5f, 7.5f}, // FrictionFlick
    };

    // Which row a note reads. `critical` is flags bit 0; hold tails get their
    // own family because the official table grades them on the release, not on
    // the press.
    // Bit 4 of HitEvent.flags (core/native/src/mmw_preview.cpp): the note is a
    // friction note even though its kind may say flick. `isFlick()` is tested
    // first there, so friction_flick arrives as kind 2.
    constexpr int kFlagFriction = 16;

    WindowType windowTypeFor(float kind, bool critical, bool holdTail, bool friction)
    {
        if (holdTail) {
            if (kind == 2.0f) {
                return WindowType::LongEndFlick;
            }
            return critical ? WindowType::LongEndCritical : WindowType::LongEnd;
        }
        if (friction) {
            // friction_long_end / friction_flick have their own rows in the
            // official table; a plain friction note's row is uniform across all
            // four grades, which is the "touch it and it is PERFECT" rule.
            if (kind == 2.0f) {
                return WindowType::FrictionFlick;
            }
            return critical ? WindowType::FrictionCritical : WindowType::Friction;
        }
        if (kind == 3.0f) {
            return critical ? WindowType::FrictionCritical : WindowType::Friction;
        }
        if (kind == 2.0f) {
            return critical ? WindowType::FlickCritical : WindowType::Flick;
        }
        return critical ? WindowType::NormalCritical : WindowType::Normal;
    }

    bool noteHasFlag(float flags, int bit)
    {
        return (static_cast<int>(std::lround(flags)) & bit) != 0;
    }

    // Lane coordinates: `center` is the middle of the note, `width` its span.
    // Two notes whose spans touch belong to the same lane area - which is how
    // a sliding long note's tail is matched to its marker (see load()).
    bool laneSpansOverlap(float centerA, float widthA, float centerB, float widthB)
    {
        const float left = std::max(centerA - widthA * 0.5f, centerB - widthB * 0.5f);
        const float right = std::min(centerA + widthA * 0.5f, centerB + widthB * 0.5f);
        return right - left > 0.01f;
    }

    // Bit 3 of a HitEvent's flags word: the core says this event is a long
    // note's tail (SUS NoteType::HoldEnd). See mmw_preview.cpp.
    constexpr int kFlagHoldTail = 8;
} // namespace

TypeWindows JudgementWindows::windowsFor(WindowType type) const
{
    const RawWindow& raw = kOfficialWindows[static_cast<int>(type)];
    // The four settings numbers describe the NORMAL note. Everything else is
    // the official row plus the same offset, so dragging a slider still moves
    // the whole table (宽松 / 标准 / 严格 all stay meaningful) without throwing
    // away the per-type shape.
    const RawWindow& base = kOfficialWindows[static_cast<int>(WindowType::Normal)];
    const float dp = perfectMs - base.pEarly * kFrameMs;
    const float dg = greatMs - base.gEarly * kFrameMs;
    const float dgd = goodMs - base.gdEarly * kFrameMs;
    const float db = badMs - base.bEarly * kFrameMs;

    auto pair = [](float earlyFrames, float lateFrames, float delta) {
        JudgeWindow w;
        w.earlyMs = std::max(1.0f, earlyFrames * kFrameMs + delta);
        w.lateMs = std::max(1.0f, lateFrames * kFrameMs + delta);
        return w;
    };

    TypeWindows out;
    out.perfect = pair(raw.pEarly, raw.pLate, dp);
    out.great = pair(raw.gEarly, raw.gLate, dg);
    out.good = pair(raw.gdEarly, raw.gdLate, dgd);
    out.bad = pair(raw.bEarly, raw.bLate, db);
    return out;
}

float JudgementWindows::widestBadMs() const
{
    float widest = 0.0f;
    for (int i = 0; i < 11; ++i) {
        const TypeWindows w = windowsFor(static_cast<WindowType>(i));
        widest = std::max(widest, std::max(w.bad.earlyMs, w.bad.lateMs));
    }
    return widest;
}

namespace
{
    // deltaMs > 0 means the press landed LATE (after the note).
    Judge gradeFor(const TypeWindows& w, float deltaMs)
    {
        if (-deltaMs <= w.perfect.earlyMs && deltaMs <= w.perfect.lateMs) {
            return Judge::Perfect;
        }
        if (-deltaMs <= w.great.earlyMs && deltaMs <= w.great.lateMs) {
            return Judge::Great;
        }
        if (-deltaMs <= w.good.earlyMs && deltaMs <= w.good.lateMs) {
            return Judge::Good;
        }
        return Judge::Bad;
    }
} // namespace

void JudgementEngine::load(const float* packed, int count)
{
    mNotes.clear();
    mNotes.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int offset = i * 7;
        HitNote note;
        note.timeSec = packed[offset + 0];
        note.center = packed[offset + 1];
        note.width = packed[offset + 2];
        note.kind = packed[offset + 3];
        note.flags = packed[offset + 4];
        note.endTimeSec = packed[offset + 5];
        note.volume = packed[offset + 6];
        note.state = 0;
        note.holdTail = false;
        mNotes.push_back(note);
    }
    mCursor = 0;
    mActiveHolds.clear();
    mMissedHoldKeys.clear();
    mHitEventIndices.clear();
    mTailClaimed.assign(mNotes.size(), 0);
    mStats = JudgementStats{};
    // Loading a chart wipes the stats just like reset() does, so the configured
    // starting life has to be seeded here too - this is the one that runs last
    // when a session starts (reset() then load()).
    mStats.life = std::clamp(mInitialLife, 1.0f, kMaxInitialLife);
    mLoaded = true;

    // Score-able notes: taps, flicks, traces, hold starts and hold tails.
    // Hold ticks and hold-loop markers are auto-resolved.
    mTotalScoreNotes = 0;
    for (const auto& note : mNotes) {
        if (note.kind == 0.0f || note.kind == 1.0f || note.kind == 2.0f || note.kind == 3.0f || note.kind == 5.0f) {
            ++mTotalScoreNotes;
        }
    }

    // Score model (see kTeamPower): normalise by the summed weight of every
    // scoreable event, exactly like the upstream overlay player does for its
    // hud-event timeline.
    mWeightedNoteCount = 0.0;
    for (const auto& note : mNotes) {
        mWeightedNoteCount += hudWeight(note.kind, (static_cast<int>(note.flags) & 1) != 0);
    }
    mWeightedNoteCount = std::max(mWeightedNoteCount, 1.0);
    mComboFactor = 1.0;

    // Flag hold tails. The core emits a kind 5 marker at the hold's start
    // carrying endTimeSec, and a normal tap/flick/trace event at the end
    // time; that end event is the tail the player releases on.
    //
    // The core marks the tail event itself (flags bit 3), which is the only
    // reliable key: a SLIDING long note (SUS ease / lane change) ends in a
    // different lane than it started (チームメイト master: marker center -4
    // width 4, tail center -3 width 6), and an unflagged tail is graded as a
    // plain tap - which a player who is *holding* the lane can never clear, so
    // the long note dropped at its very end. The lane matching below only
    // covers events that arrive without the flag.
    bool coreFlagsTails = false;
    for (const HitNote& note : mNotes) {
        if ((static_cast<int>(std::lround(note.flags)) & kFlagHoldTail) != 0) {
            coreFlagsTails = true;
            break;
        }
    }
    for (HitNote& note : mNotes) {
        if ((static_cast<int>(std::lround(note.flags)) & kFlagHoldTail) != 0) {
            note.holdTail = true;
        }
    }
    for (std::size_t i = 0; i < mNotes.size(); ++i) {
        if (static_cast<int>(mNotes[i].kind) != 5) {
            continue;
        }
        const float endTime = mNotes[i].endTimeSec;
        const float headCenter = mNotes[i].center;
        const float headWidth = mNotes[i].width;
        // The flagged tail closest to the hold's own lane (two holds can end
        // on the same tick).
        std::size_t flagged = mNotes.size();
        float flaggedDistance = std::numeric_limits<float>::max();
        std::size_t exact = mNotes.size();
        std::size_t overlapping = mNotes.size();
        for (std::size_t j = 0; j < mNotes.size(); ++j) {
            const HitNote& candidate = mNotes[j];
            if (candidate.timeSec > endTime + 0.02f) {
                break; // HitEvents are sorted by time
            }
            if (candidate.timeSec < endTime - 0.02f) {
                continue;
            }
            if (candidate.holdTail) {
                const float distance = std::fabs(candidate.center - headCenter);
                if (distance < flaggedDistance) {
                    flaggedDistance = distance;
                    flagged = j;
                }
                continue;
            }
            if (static_cast<int>(candidate.kind) > 3) {
                continue;
            }
            if (std::fabs(candidate.center - headCenter) < 0.01f) {
                exact = j;
                break;
            }
            if (overlapping == mNotes.size() && laneSpansOverlap(headCenter, headWidth, candidate.center, candidate.width)) {
                overlapping = j;
            }
        }
        const std::size_t pick = flagged != mNotes.size() ? flagged
            : (exact != mNotes.size() ? exact : overlapping);
        if (pick != mNotes.size()) {
            mNotes[pick].holdTail = true;
        }
    }

    // CppSekai: resolve each hold tick's owning hold (its kind 5 marker and
    // the hold's start note). Ticks may only auto-hit while their hold was
    // actually grabbed, so every tick needs to know which start note to ask.
    // Matching is by TIME WINDOW, not lane: a hold's steps can ease sideways,
    // so a mid-hold tick may sit on a different lane than the hold's start.
    // Ticks without any covering marker (guide holds emit no kind 5 marker)
    // keep the old always-auto-hit behaviour.
    for (std::size_t i = 0; i < mNotes.size(); ++i) {
        HitNote& tick = mNotes[i];
        if (static_cast<int>(tick.kind) != 4) {
            continue;
        }
        std::size_t bestMarker = static_cast<std::size_t>(-1);
        float bestCenterDist = 0.0f;
        for (std::size_t j = 0; j < mNotes.size(); ++j) {
            const HitNote& marker = mNotes[j];
            if (static_cast<int>(marker.kind) != 5) {
                continue;
            }
            if (marker.timeSec > tick.timeSec + 0.02f) {
                break; // markers are sorted by time
            }
            if (tick.timeSec > marker.endTimeSec + 0.02f) {
                continue;
            }
            const float centerDist = std::fabs(marker.center - tick.center);
            if (bestMarker == static_cast<std::size_t>(-1) || centerDist < bestCenterDist) {
                bestMarker = j;
                bestCenterDist = centerDist;
            }
        }
        if (bestMarker == static_cast<std::size_t>(-1)) {
            continue; // guide / markerless tick: stays always-auto-hit
        }
        tick.holdMarkerIndex = bestMarker;
        const HitNote& marker = mNotes[bestMarker];
        // The start note: a sibling tap at the marker's own time and lane;
        // without one the marker itself stands in (spawn marks it as read, so
        // the hold counts as engaged).
        for (std::size_t k = bestMarker + 1; k-- > 0;) {
            const HitNote& candidate = mNotes[k];
            if (std::fabs(candidate.timeSec - marker.timeSec) > 0.05f) {
                break;
            }
            if ((candidate.kind == 0.0f || candidate.kind == 1.0f)
                && std::fabs(candidate.center - marker.center) < 0.01f) {
                tick.holdStartIndex = k;
                break;
            }
        }
        if (tick.holdStartIndex == static_cast<std::size_t>(-1)) {
            tick.holdStartIndex = bestMarker;
        }
    }

    // One summary line per chart. A hold marker whose tail event was not found
    // is almost always a stream/engine mismatch (sliding long notes used to
    // hit this), and the symptom - the tail silently graded as a plain tap -
    // is invisible in the [stats] line, so state the counts up front.
    int holdMarkers = 0;
    int tailsMatched = 0;
    for (const HitNote& note : mNotes) {
        if (static_cast<int>(note.kind) == 5) {
            ++holdMarkers;
        }
        if (note.holdTail) {
            ++tailsMatched;
        }
    }
    platform::logf(platform::LogPlay, "[judge] %d event(s): %d note(s), %d hold(s), %d tail(s) matched%s\n",
        static_cast<int>(mNotes.size()), mTotalScoreNotes, holdMarkers, tailsMatched,
        coreFlagsTails ? " (core flags tails)" : " (lane-matched tails)");
    std::fflush(stdout);
}

void JudgementEngine::reset()
{
    for (auto& note : mNotes) {
        note.state = 0;
    }
    mCursor = 0;
    mActiveHolds.clear();
    mMissedHoldKeys.clear();
    mHitEventIndices.clear();
    mTailClaimed.assign(mNotes.size(), 0);
    mStats = JudgementStats{};
    // Settings > 判定 > 初始血量: a smaller pool makes the clear harder (the bar is
    // normalised against the pool, so the run always starts at a full bar).
    mStats.life = std::clamp(mInitialLife, 1.0f, kMaxInitialLife);
    mComboFactor = 1.0;
}

void JudgementEngine::registerMiss(float songTimeSec, float lifeCost)
{
    mStats.miss += 1;
    mStats.combo = 0;
    // A combo break drops the score bonus back to its base level (pjsk pays
    // the combo bonus again from scratch after a miss).
    mComboFactor = 1.0;
    mStats.life = std::clamp(mStats.life + lifeCost, 0.0f, lifeCeiling());
    mStats.lastJudge = Judge::Miss;
    // Record the time, otherwise the HUD never sees the judge change and the
    // MISS sprite is never shown.
    mStats.lastJudgeTimeSec = songTimeSec;
}

double JudgementEngine::scoreDeltaFor(float kind, bool critical) const
{
    const float weight = hudWeight(kind, critical);
    if (weight <= 0.0f) {
        return 0.0;
    }
    const double levelFactor = static_cast<double>((mChartRating - 5.0f) * 0.005f + 1.0f);
    return (kTeamPower / mWeightedNoteCount) * 4.0 * static_cast<double>(weight) * levelFactor * mComboFactor;
}

Judge JudgementEngine::registerJudge(Judge judge, bool critical, float volume, float kind, float noteTimeSec)
{
    (void)volume;
    // Hit tallies for the host's per-hit feedback (pad rumble): this is the one
    // place every successful hit goes through, so a chord judged in one frame
    // still counts every note.
    mStats.hitCount += 1;
    if (critical) {
        mStats.criticalHitCount += 1;
    }
    if (static_cast<int>(kind) == 2) {
        mStats.flickHitCount += 1;
    }
    switch (judge) {
        case Judge::Perfect:
            mStats.perfect += 1;
            break;
        case Judge::Great:
            mStats.great += 1;
            break;
        case Judge::Good:
            mStats.good += 1;
            break;
        case Judge::Bad:
            mStats.bad += 1;
            break;
        default:
            break;
    }

    // Combos: only PERFECT and GREAT keep the chain alive. pjsk breaks the
    // combo on GOOD and BAD just like on MISS (the combo bonus then restarts).
    if (judge == Judge::Good || judge == Judge::Bad) {
        mStats.combo = 0;
        mComboFactor = 1.0;
    } else {
        mStats.combo += 1;
        mStats.maxCombo = std::max(mStats.maxCombo, mStats.combo);
        // Combo bonus: +1% every 100 combo, capped at +10% (upstream formula).
        if (mStats.combo % 100 == 1 && mStats.combo > 1) {
            mComboFactor = std::min(mComboFactor + 0.01, 1.1);
        }
    }

    // BAD still costs life (but not as much as a MISS).
    if (judge == Judge::Bad) {
        mStats.life = std::clamp(mStats.life + kLifeBad, 0.0f, lifeCeiling());
    }

    const double delta = scoreDeltaFor(kind, critical) * judgeMultiplier(judge);
    mStats.score += delta;
    // Drives the HUD "+N": the delta and the chart time of the note that paid
    // it. A MISS / BAD multiplies by 0, so the HUD shows nothing for them.
    mStats.lastScoreDelta = delta;
    mStats.scoreDeltaAtSec = noteTimeSec;
    mStats.lastJudge = judge;
    mStats.lastJudgeCritical = critical;
    return judge;
}

bool JudgementEngine::laneCovers(const HitNote& note, float lanePos, float margin) const
{
    const float half = std::max(0.5f, note.width * 0.5f);
    return lanePos >= note.center - half - margin && lanePos <= note.center + half + margin;
}

bool JudgementEngine::laneHeld(const ActiveHold& hold) const
{
    const float half = std::max(0.5f, hold.width * 0.5f);
    for (const float lane : mHoldLanes) {
        if (lane >= hold.center - half - 0.5f && lane <= hold.center + half + 0.5f) {
            return true;
        }
    }
    return false;
}

HitNote* JudgementEngine::findCandidate(float lanePos, float songTimeSec, float margin, bool wantFlick,
    FlickDir flickDir, float dirX, float dirY)
{
    (void)flickDir; // the vector is the primary form now (see flickVector)
    HitNote* best = nullptr;
    float bestAbsDt = std::numeric_limits<float>::max();
    // Second chance: a swipe that went the wrong way still clears the note in
    // the official game - it is capped at GREAT instead of being dropped. Those
    // candidates are only used when nothing with a correct direction is in
    // reach, exactly like FlickNote.JudgmentFlick prefers `distance &&
    // trueDirection` over a bare `distance` record.
    HitNote* bestWrongDir = nullptr;
    float bestWrongAbsDt = std::numeric_limits<float>::max();
    const bool haveSwipeDir = dirX != 0.0f || dirY != 0.0f;

    // Look-ahead bound: the widest BAD any note type can have, since the row
    // is only known once we reach the note.
    const float badSec = mWindows.widestBadMs() / 1000.0f;
    const std::size_t scanEnd = std::min(mNotes.size(), mCursor + 256);

    for (std::size_t i = mCursor; i < scanEnd; ++i) {
        HitNote& note = mNotes[i];
        if (note.state != 0) {
            continue;
        }
        if (note.timeSec > songTimeSec + badSec) {
            break;
        }
        // Flick notes (kind 2) and tap/trace notes are player-hit. Kind 4
        // ticks are auto, kind 5 markers are hold bookkeeping. A hold tail
        // is normally resolved by the hold tracker (releasing the lane) -
        // but a flick TAIL is cleared by swiping while still holding, like
        // the official game, so flick gestures may reach it here.
        if (note.kind == 4.0f || note.kind == 5.0f) {
            continue;
        }
        if (note.holdTail && !(wantFlick && note.kind == 2.0f)) {
            continue;
        }
        const bool noteIsFlick = note.kind == 2.0f;
        // "Flick 视作 Tap": a flick note takes any press inside its window, so
        // neither the kind mismatch nor the direction check below may reject it.
        // Hold tails never reach this point as flick-as-tap (they are completed
        // by the hold tracker instead, see update()).
        const bool flickAsTap = noteIsFlick && mFlickAsTap;
        if (wantFlick != noteIsFlick && !flickAsTap) {
            if (mStrictFlick) {
                // Strict: a plain tap never clears a flick note (and a swipe
                // never clears a tap).
                continue;
            }
            // Lenient skeleton mode: either gesture clears either kind.
        }
        // Official direction test (FlickNote.UpdateDelta):
        //   isTrueDirection = dot(normalize(v), noteDir) >= 0 || Direction == Default
        // noteDir is (-1,0) / (1,0) for left/right, and a Default (arrow-less)
        // flick ignores the direction completely. So a LEFT note is "correct"
        // for any swipe whose x is not positive - a straight UP swipe (x == 0)
        // clears it, which is what players actually do and what the old
        // `flickDir == noteDir` test rejected. A wrong direction is not a
        // reject either: the note still clears, capped at GREAT (see below),
        // exactly like the official GetJudgeResultInternal.
        bool dirWrong = false;
        if (wantFlick && mStrictFlick && !flickAsTap && haveSwipeDir) {
            const FlickDir noteDir = static_cast<FlickDir>(noteFlickDir(note));
            if (noteDir == FlickLeft) {
                dirWrong = dirX > 0.0f;
            } else if (noteDir == FlickRight) {
                dirWrong = dirX < 0.0f;
            }
            // FlickUp / FlickNone: always accepted.
        }
        if (!laneCovers(note, lanePos, margin)) {
            continue;
        }
        // Official long-note tail flick: FlickNote.Judgment (parentNote != null)
        // requires `noteTime - touchTime <= -1/120`, i.e. the swipe must land at
        // least one JustPerfect half-window AFTER the tail's time. Early
        // swipes simply do not resolve it (it stays pending).
        if (note.holdTail && wantFlick && note.kind == 2.0f
            && songTimeSec - note.timeSec < 1.0f / 120.0f) {
            continue;
        }
        // Signed distance: positive means the press is late. A note only
        // takes the press inside its own BAD window - outside it the note is
        // gone (MISS), and grading it BAD anyway is exactly the old bug.
        const float deltaMs = (songTimeSec - note.timeSec) * 1000.0f;
        const bool noteCritical = noteHasFlag(note.flags, 1);
        const JudgeWindow noteBad = mWindows
            .windowsFor(windowTypeFor(note.kind, noteCritical, note.holdTail,
                noteHasFlag(note.flags, kFlagFriction)))
            .bad;
        if (-deltaMs > noteBad.earlyMs || deltaMs > noteBad.lateMs) {
            continue;
        }
        const float dt = std::fabs(deltaMs) / 1000.0f;
        if (dirWrong) {
            // Keep it as a fallback only - a correctly-swiped note always wins.
            if (dt < bestWrongAbsDt) {
                bestWrongAbsDt = dt;
                bestWrongDir = &note;
            }
            continue;
        }
        if (dt < bestAbsDt) {
            bestAbsDt = dt;
            best = &note;
        }
    }

    bool dirWasWrong = false;
    if (best == nullptr && bestWrongDir != nullptr) {
        best = bestWrongDir;
        dirWasWrong = true;
    }

    if (best == nullptr) {
        return nullptr;
    }
    // Signed: positive = the press was late. The official table gives every
    // note type its own early/late pair, so the grade comes from that row.
    const bool bestCritical = noteHasFlag(best->flags, 1);
    const TypeWindows bestWindows = mWindows.windowsFor(
        windowTypeFor(best->kind, bestCritical, best->holdTail, noteHasFlag(best->flags, kFlagFriction)));
    Judge judge = gradeFor(bestWindows, (songTimeSec - best->timeSec) * 1000.0f);
    if (dirWasWrong && judge == Judge::Perfect) {
        // Swiped the wrong way but landed inside the PERFECT window: the
        // official game reports (GREAT, FlickMiss). Time-wise worse than
        // PERFECT grades are left alone (a late GREAT is still a GREAT).
        judge = Judge::Great;
    }
    best->state = 1;
    mHitEventIndices.push_back(static_cast<int>(best - mNotes.data()));
    // Critical is bit0 only - bits 1-2 carry the flick direction, so a
    // directional flick note (flags >= 2) must not read as critical.
    const bool critical = (static_cast<int>(best->flags) & 1) != 0;
    mStats.lastHitKind = best->kind;
    mStats.lastHitCenter = best->center;
    mStats.lastHitWidth = best->width;
    mStats.lastHitTimeSec = best->timeSec;
    mStats.lastHitFlickDir = noteFlickDir(*best);
    mStats.lastHitFriction = best->kind == 3.0f; // kind 3 = trace / friction
    registerJudge(judge, critical, best->volume, best->kind, best->timeSec);
    mStats.pushFreshHit(critical);
    return best;
}

Judge JudgementEngine::tap(float lanePos, float songTimeSec, bool critical, float margin)
{
    (void)critical;
    HitNote* hit = findCandidate(lanePos, songTimeSec, margin, false);
    if (hit == nullptr) {
        return Judge::None;
    }
    mStats.lastJudgeTimeSec = songTimeSec;
    return mStats.lastJudge;
}

Judge JudgementEngine::flick(float lanePos, float songTimeSec, FlickDir dir, float margin)
{
    // Legacy enum entry point (keyboard, scripts, --drop-test): translate to
    // the official direction vector. Up/default carries no horizontal
    // component, so it is acceptable for left/right notes as well - that is
    // exactly what `dot(v, (-1,0)) >= 0` says for a straight vertical swipe.
    float dx = 0.0f;
    float dy = 0.0f;
    switch (dir) {
        case FlickLeft: dx = -1.0f; break;
        case FlickRight: dx = 1.0f; break;
        case FlickUp: dy = 1.0f; break;
        default: break; // FlickNone: no direction information, skip the test
    }
    return flickVector(lanePos, songTimeSec, dx, dy, margin);
}

Judge JudgementEngine::flickVector(float lanePos, float songTimeSec, float dirX, float dirY, float margin)
{
    HitNote* hit = findCandidate(lanePos, songTimeSec, margin, true, FlickNone, dirX, dirY);
    if (hit == nullptr) {
        return Judge::None;
    }
    mStats.lastJudgeTimeSec = songTimeSec;
    return mStats.lastJudge;
}

void JudgementEngine::judgeHoldTail(ActiveHold& hold, Judge judge, float songTimeSec)
{
    if (hold.tailIndex >= mNotes.size()) {
        return;
    }
    HitNote& tail = mNotes[hold.tailIndex];
    if (tail.state != 0) {
        return; // already resolved (e.g. the hold broke earlier)
    }
    tail.state = 1;
    mHitEventIndices.push_back(static_cast<int>(hold.tailIndex));
    mStats.holdTails += 1;
    const bool critical = (static_cast<int>(tail.flags) & 1) != 0;
    mStats.lastHitKind = tail.kind;
    mStats.lastHitCenter = tail.center;
    mStats.lastHitWidth = tail.width;
    mStats.lastHitTimeSec = tail.timeSec;
    mStats.lastHitFlickDir = noteFlickDir(tail);
    mStats.lastHitFriction = tail.kind == 3.0f;
    registerJudge(judge, critical, tail.volume, tail.kind, tail.timeSec);
    mStats.pushFreshHit(critical);
    mStats.lastJudgeTimeSec = songTimeSec;
}

void JudgementEngine::update(float songTimeSec)
{
    // Nothing can be judged during the lead-in (negative chart time).
    if (!mLoaded || songTimeSec < 0.0f) {
        return;
    }

    // Advance cursor past fully resolved notes.
    while (mCursor < mNotes.size() && mNotes[mCursor].state != 0) {
        ++mCursor;
    }

    const float missSec = mWindows.missAfterMs / 1000.0f;
    const std::size_t scanEnd = std::min(mNotes.size(), mCursor + 256);

    for (std::size_t i = mCursor; i < scanEnd; ++i) {
        HitNote& note = mNotes[i];
        if (note.state != 0) {
            continue;
        }
        if (note.timeSec > songTimeSec) {
            break;
        }

        if (note.kind == 4.0f) {
            std::uint8_t startState;
            bool holdBroken = false;
            bool holdLive = false;
            if (note.holdMarkerIndex >= mNotes.size()) {
                // Guide / hidden hold ticks (竹节): no kind-5 marker exists,
                // so there is no hold tracker to ask. Official behaviour is
                // the loosest possible - a finger anywhere over the segment
                // is enough, and missing it costs nothing: the tick is
                // consumed silently (no MISS, no life, no combo break).
                bool covered = mAutoPlay; // autoplay preview: no fingers exist
                for (const float lane : mHoldLanes) {
                    if (laneCovers(note, lane, 0.5f)) {
                        covered = true;
                        break;
                    }
                }
                if (!covered) {
                    note.state = 2;
                    continue;
                }
                startState = 1;
            } else {
                // Hold ticks auto-hit only while the hold they belong to was
                // actually grabbed and has not broken. A hold that was never
                // hit is already fully accounted for by its start note's
                // MISS, so its ticks are consumed silently - otherwise every
                // tick would hand out a free PERFECT (and its combo / score)
                // for a hold the player never touched.
                startState = note.holdStartIndex < mNotes.size()
                    ? mNotes[note.holdStartIndex].state
                    : static_cast<std::uint8_t>(1);
                // holdLive (2026-11-01): 这条 hold 现在是否"抓着"。起点没按上
                // 的 hold 之后 startState 永远是 2（note.state 不回退），光看它
                // 会把接上之后的 tick 也吞掉 -- 而原作接上之后后面照常判。
                // 所以额外看 hold 自己的状态：没 broken 且没 finished 就是活着。
                if (note.holdMarkerIndex < mNotes.size()) {
                    for (const ActiveHold& hold : mActiveHolds) {
                        if (hold.noteIndex != note.holdMarkerIndex) {
                            continue;
                        }
                        if (hold.broken) {
                            holdBroken = true;
                        } else if (!hold.finished) {
                            holdLive = true;
                        }
                        break;
                    }
                }
            }

            if ((startState == 1 || holdLive) && !holdBroken) {
                note.state = 1;
                mHitEventIndices.push_back(static_cast<int>(i));
                registerJudge(Judge::Perfect, (static_cast<int>(note.flags) & 1) != 0, note.volume, note.kind,
                    note.timeSec);
                mStats.lastJudgeTimeSec = songTimeSec;
                // A tick is its own hit: report its own lane so the effect for
                // the hold step plays there instead of re-using the previous
                // hit.
                mStats.lastHitKind = note.kind;
                mStats.lastHitCenter = note.center;
                mStats.lastHitWidth = note.width;
                mStats.lastHitTimeSec = note.timeSec;
                mStats.lastHitFlickDir = noteFlickDir(note);
                mStats.lastHitFriction = false;
                mStats.pushFreshHit((static_cast<int>(note.flags) & 1) != 0);
            } else if (startState == 2 || holdBroken) {
                // The hold is unattached right now (start missed and not yet
                // re-grabbed, or let go mid-way): swallow the tick without
                // judging it. Nothing is deducted a second time.
                note.state = 2;
            }
            // startState == 0: the start note is still pending (the player may
            // be a little late) - leave the tick undecided until next frame.
            continue;
        }

        if (note.kind == 5.0f) {
            // Hold-loop marker: spawns hold tracking when the start note was
            // hit. We detect via matching active holds instead; skip here.
            continue;
        }

        if (note.holdTail) {
            // A tail is scored through its hold: releasing at the end is graded
            // by how early the finger came off, holding through is a PERFECT,
            // and the lane is never pressed *for the tail itself*. So if it is
            // still pending long after its time, either the hold never started
            // (and its start note's own MISS already paid for the whole thing)
            // or no hold ever claimed this event. Judging it as a second MISS
            // here is what an autoplay run scored ~20 misses on - sliding long
            // notes whose tail event sits in a different lane than the hold
            // started in, and a human player saw the same as pointless combo
            // breaks. Consume it silently and let the cursor move past it.
            if (note.timeSec < songTimeSec - missSec) {
                note.state = 2;
            }
            continue;
        }

        if (note.kind == 3.0f) {
            // Trace notes (the green "bamboo" segments / slides): the official
            // rule is COVERAGE, not timing. Keeping a finger on the lane while
            // the segment passes is enough, there is no tail, and the head does
            // not have to be re-tapped - which is why a player who holds
            // through the whole bamboo should not lose a single note.
            //
            // A well timed press still goes through findCandidate() and keeps
            // its graded judgement; this branch only catches the "already
            // holding" case, which used to fall through to the auto-miss and
            // made every trace in the chart unplayable on a hold-through.
            bool covered = mAutoPlay;
            for (const float lane : mHoldLanes) {
                if (laneCovers(note, lane, 0.5f)) {
                    covered = true;
                    break;
                }
            }
            if (covered) {
                note.state = 1;
                mHitEventIndices.push_back(static_cast<int>(i));
                registerJudge(Judge::Perfect, (static_cast<int>(note.flags) & 1) != 0, note.volume, note.kind,
                    note.timeSec);
                mStats.lastJudgeTimeSec = songTimeSec;
                mStats.lastHitKind = note.kind;
                mStats.lastHitCenter = note.center;
                mStats.lastHitWidth = note.width;
                mStats.lastHitTimeSec = note.timeSec;
                mStats.lastHitFlickDir = noteFlickDir(note);
                mStats.lastHitFriction = true;
                mStats.pushFreshHit((static_cast<int>(note.flags) & 1) != 0);
                continue;
            }
            // Not covered: fall through to the normal miss timing below.
        }

        if (gHoldTest && note.endTimeSec > 0.0f) {
            // 调试：长条起点直接判 MISS，好让后面那段"没接上"的分支跑起来。
            note.state = 2;
            registerMiss(songTimeSec, kLifeMiss);
            continue;
        }

        if (mAutoPlay) {
            // Autoplay preview: every note is a timed PERFECT, so the HUD /
            // score / combo machinery shows a flawless run without input.
            // Effects come from the core's own timeline in this mode.
            note.state = 1;
            registerJudge(Judge::Perfect, (static_cast<int>(note.flags) & 1) != 0, note.volume, note.kind,
                    note.timeSec);
            mStats.lastJudgeTimeSec = songTimeSec;
            mStats.lastHitKind = note.kind;
            mStats.lastHitCenter = note.center;
            mStats.lastHitWidth = note.width;
            mStats.lastHitTimeSec = note.timeSec;
            mStats.lastHitFlickDir = noteFlickDir(note);
            mStats.lastHitFriction = note.kind == 3.0f;
            mStats.pushFreshHit((static_cast<int>(note.flags) & 1) != 0);
            continue;
        }

        // A note gives up when it is past its own LATE BAD bound. Using one
        // shared number here would keep a normal note alive 30 ms longer than
        // the game does while cutting a flick short.
        const bool noteCritical = noteHasFlag(note.flags, 1);
        const TypeWindows noteWindows = mWindows.windowsFor(
            windowTypeFor(note.kind, noteCritical, note.holdTail, noteHasFlag(note.flags, kFlagFriction)));
        if (note.timeSec < songTimeSec - noteWindows.bad.lateMs / 1000.0f) {
            note.state = 2;
            registerMiss(songTimeSec, kLifeMiss);
            continue;
        }
    }

    // Hold tracking: judge the tail on release, break the hold when the lane
    // is let go too early.
    //
    // pinMiss: 本帧有"没接上的 hold"就让 HUD 把 MISS 常驻（原作是整条期间一直
    // 挂着 MISS 字样），重连或结束就撤。
    const bool pinMissPrev = mStats.pinMiss;
    mStats.pinMiss = false;
    for (auto& hold : mActiveHolds) {
        if (hold.finished) {
            continue;
        }

        // In autoplay the lane is always considered held: holds never break
        // and tails hold through for a PERFECT. (CPSEKAI_HOLD_TEST 例外：那个
        // 开关就是要把 hold 卡在"没人按"的状态上。)
        bool held = (!gHoldTest && mAutoPlay) || laneHeld(hold);
        if (gHoldReconnect && songTimeSec >= hold.startTimeSec
            + (hold.endTimeSec - hold.startTimeSec) * 0.5f) {
            held = true; // 测试：长条走到一半"按回去"
        }

        if (hold.broken) {
            if (held && songTimeSec < hold.endTimeSec) {
                // pjsk: re-pressing the lane reconnects a broken hold. The
                // break itself (MISS, -40 life, combo break) stands and the
                // ticks consumed while detached stay missed, but everything
                // from here on is judged normally again.
                hold.broken = false;
                hold.dimmed = false;
                if (hold.tailIndex < mNotes.size() && mNotes[hold.tailIndex].state == 2) {
                    mNotes[hold.tailIndex].state = 0; // back to pending
                }
                // fall through to the normal tracking below
            } else {
                // Still detached: pjsk washes the remaining body out while the
                // lane is up and puts it back to normal as soon as the player
                // presses again.
                hold.dimmed = !held;
                mStats.pinMiss = true;
                if (songTimeSec >= hold.endTimeSec) {
                    hold.finished = true;
                }
                continue;
            }
        }

        const std::uint8_t startState = hold.startIndex < mNotes.size()
            ? mNotes[hold.startIndex].state
            : static_cast<std::uint8_t>(2);
        // engaged = the hold's start tap was actually hit. While the start is
        // still pending the player may simply be a little late, so nothing is
        // decided yet: its own judgement (hit or auto-miss) settles it.
        const bool engaged = startState == 1;

        if (startState == 2) {
            // 起点没按上（2026-11-01 用户实测原作）：整条**不掉下去**，头钉在判定
            // 线上、整体半透明、MISS 字样一直挂着，玩家中途按下来还能接上 --
            // 表现和"按上了又松手"完全一样。所以这里既不 finish 也不进
            // missedHoldKeys（那是"掉出屏幕"），只打破 + 洗白，交给上面那段
            // broken 分支去等 reconnect。
            //
            // 起点的自动 MISS 已经付过整条的账（kLifeMiss），这里不再扣分；尾判
            // 先挂起，接上时 broken 分支会把它恢复成 pending 再正常判。
            hold.broken = true;
            hold.dimmed = true;
            mStats.pinMiss = true;
            if (hold.tailIndex < mNotes.size() && mNotes[hold.tailIndex].state == 0) {
                mNotes[hold.tailIndex].state = 2;
            }
            continue;
        }

        if (held) {
            hold.released = false;
            hold.releaseTimeSec = -1.0f;
        } else if (!hold.released) {
            hold.released = true;
            hold.releaseTimeSec = songTimeSec;
        }

        if (!held && engaged && songTimeSec >= hold.startTimeSec + mWindows.holdStartGraceMs / 1000.0f
            && songTimeSec < hold.endTimeSec - mWindows.holdTailGraceMs / 1000.0f) {
            // Let go too early: the hold breaks (mid-hold miss, -40 life) and
            // is drawn washed out from here on - unless the player presses the
            // lane again, which reconnects it (see the broken branch above).
            hold.broken = true;
            hold.dimmed = true;
            mStats.holdBreaks += 1;
            if (hold.tailIndex < mNotes.size() && mNotes[hold.tailIndex].state == 0) {
                mNotes[hold.tailIndex].state = 2;
            }
            registerMiss(songTimeSec, kLifeHoldBreak);
            continue;
        }

        if (songTimeSec >= hold.endTimeSec) {
            hold.finished = true;
            if (!engaged) {
                // The start never landed - one MISS for the whole hold, which
                // the start note has already registered.
                if (hold.tailIndex < mNotes.size() && mNotes[hold.tailIndex].state == 0) {
                    mNotes[hold.tailIndex].state = 2;
                }
                continue;
            }
            // Tail judgement: releasing inside the window is graded by how far
            // from the end the finger came off; holding through is a PERFECT.
            // A flick TAIL is the exception: it is cleared by swiping while
            // still holding (findCandidate accepts it now), never by holding
            // through or releasing - leaving it pending lets the holdTail
            // auto-miss path grade a forgotten flick instead of gifting a
            // PERFECT for doing nothing.
            if (hold.tailIndex < mNotes.size() && mNotes[hold.tailIndex].kind == 2.0f) {
                if (mAutoPlay) {
                    // Nothing swipes in a preview run, so the flick tail would
                    // sit pending and auto-miss - autoplay has to stay flawless
                    // (its [stats] line is the headless baseline).
                    judgeHoldTail(hold, Judge::Perfect, songTimeSec);
                } else if (mFlickAsTap) {
                    // "Flick 视作 Tap": the player is still holding this lane,
                    // so the tail must not ask for anything. It stops being a
                    // note: the hold simply ends and the tail is paid as a
                    // PERFECT, whether the lane was released or held through.
                    // (Turning it into a tap instead would demand a second
                    // press on a lane that is already down, and grading it on
                    // the release - the rule for tap tails - would punish a
                    // gesture the setting exists to remove.)
                    judgeHoldTail(hold, Judge::Perfect, songTimeSec);
                }
                continue;
            }
            Judge judge = Judge::Perfect;
            if (hold.released && hold.releaseTimeSec >= 0.0f) {
                // Graded on the release, so it reads the long_end row - which
                // is deliberately lopsided (a late release is forgiven a frame
                // more than an early one).
                const std::size_t tailIdx = hold.tailIndex;
                const bool tailFlick = tailIdx < mNotes.size() && mNotes[tailIdx].kind == 2.0f;
                const bool tailCritical = tailIdx < mNotes.size()
                    && noteHasFlag(mNotes[tailIdx].flags, 1);
                const bool tailFriction = tailIdx < mNotes.size()
                    && noteHasFlag(mNotes[tailIdx].flags, kFlagFriction);
                const TypeWindows tailWindows = mWindows.windowsFor(
                    windowTypeFor(tailFlick ? 2.0f : 0.0f, tailCritical, true, tailFriction));
                judge = gradeFor(tailWindows, (hold.releaseTimeSec - hold.endTimeSec) * 1000.0f);
            }
            judgeHoldTail(hold, judge, songTimeSec);
        }
    }
    if (gHoldTest && mStats.pinMiss != pinMissPrev) {
        platform::logf(platform::LogPlay, "[play] hold pinMiss=%d at %.2fs\n",
            mStats.pinMiss ? 1 : 0, songTimeSec);
    }

    // Spawn hold tracking from the kind 5 markers. The marker sits at the
    // hold's start and carries its end time; the *scoreable* tail note is the
    // tap/flick/trace event emitted at that end time.
    for (std::size_t i = mCursor; i < scanEnd; ++i) {
        HitNote& marker = mNotes[i];
        if (marker.state != 0 || marker.kind != 5.0f) {
            continue;
        }
        if (marker.timeSec > songTimeSec) {
            break;
        }
        marker.state = 1;

        ActiveHold hold;
        hold.noteIndex = i;
        hold.startTimeSec = marker.timeSec;
        hold.endTimeSec = marker.endTimeSec;
        hold.center = marker.center;
        hold.width = marker.width;

        // Sibling tap (same time & lane, kind 0/1) - the hold's start note.
        for (std::size_t j = i; j-- > 0;) {
            const HitNote& candidate = mNotes[j];
            if (std::fabs(candidate.timeSec - marker.timeSec) > 0.05f) {
                break;
            }
            if ((candidate.kind == 0.0f || candidate.kind == 1.0f)
                && std::fabs(candidate.center - marker.center) < 0.01f) {
                hold.startIndex = j;
                break;
            }
        }
        if (hold.startIndex > mNotes.size()) {
            // No sibling tap in the stream: the marker itself is the start
            // (it reads as hit below, so the hold is not treated as missed).
            hold.startIndex = i;
        }

        // The tail event: the flagged event at the marker's end time, closest
        // to the hold's own lane (two holds may end on the same tick). Sliding
        // long notes end in a different lane, so a lane match alone is not
        // enough - see the flagging loop in load().
        std::size_t tail = mNotes.size();
        std::size_t exactTail = mNotes.size();
        std::size_t overlappingTail = mNotes.size();
        float tailDistance = std::numeric_limits<float>::max();
        for (std::size_t j = i; j < mNotes.size(); ++j) {
            const HitNote& candidate = mNotes[j];
            if (candidate.timeSec > marker.endTimeSec + 0.02f) {
                break;
            }
            if (candidate.timeSec < marker.endTimeSec - 0.02f) {
                continue;
            }
            if (candidate.holdTail) {
                // A tail another hold has already taken is off the table. Two
                // long notes can end on the very same tick, and their tails sit
                // in the lanes the holds *end* in - a sliding long note does not
                // end where it started - so "closest to the start lane" alone
                // picks the same note twice and leaves the other one orphaned.
                if (mTailClaimed[j] != 0) {
                    continue;
                }
                const float distance = std::fabs(candidate.center - marker.center);
                if (distance < tailDistance) {
                    tailDistance = distance;
                    tail = j;
                }
                continue;
            }
            if (exactTail == mNotes.size() && std::fabs(candidate.center - marker.center) < 0.01f) {
                exactTail = j;
            } else if (overlappingTail == mNotes.size()
                && laneSpansOverlap(marker.center, marker.width, candidate.center, candidate.width)) {
                overlappingTail = j;
            }
        }
        hold.tailIndex = tail != mNotes.size() ? tail
            : (exactTail != mNotes.size() ? exactTail : overlappingTail);
        if (hold.tailIndex < mNotes.size()) {
            // Claim it, so the next hold looking at this tick cannot take the
            // same note. Without this the leftover tail stayed pending until the
            // auto-miss path below swallowed it: an autoplay run scored ~22
            // misses on a chart full of sliding long notes, and a human player
            // saw the same thing as random combo breaks.
            mTailClaimed[hold.tailIndex] = 1;
        }

        const bool startMissed = hold.startIndex < mNotes.size() && mNotes[hold.startIndex].state == 2;
        if (startMissed) {
            // Never grabbed: the start's own miss is the whole hold's miss.
            if (hold.tailIndex < mNotes.size()) {
                mNotes[hold.tailIndex].state = 2;
            }
            mMissedHoldKeys.push_back(marker.center);
            mMissedHoldKeys.push_back(marker.timeSec);
            continue;
        }
        mActiveHolds.push_back(hold);
    }

    // Retire finished holds. A broken hold deliberately lingers until its end
    // time: it is no longer judged, but the renderer still needs to know the
    // player let go of it so the remaining body stays washed out.
    mActiveHolds.erase(std::remove_if(mActiveHolds.begin(), mActiveHolds.end(),
                          [](const ActiveHold& hold) {
                              return hold.finished;
                          }),
        mActiveHolds.end());
}

void JudgementEngine::appendDimmedHoldKeys(std::vector<float>& out) const
{
    for (const ActiveHold& hold : mActiveHolds) {
        if (!hold.dimmed) {
            continue;
        }
        out.push_back(hold.center);
        out.push_back(hold.startTimeSec);
    }
}

bool JudgementEngine::anyActiveHold(bool* criticalOut) const
{
    if (criticalOut != nullptr) {
        *criticalOut = false;
    }
    for (const ActiveHold& hold : mActiveHolds) {
        // A broken hold is still tracked (for the washed-out look) but it is no
        // longer "active": the hold loop SE must have stopped.
        if (hold.finished || hold.broken) {
            continue;
        }
        if (criticalOut != nullptr && hold.noteIndex < mNotes.size()) {
            *criticalOut = (static_cast<int>(mNotes[hold.noteIndex].flags) & 1) != 0;
        }
        return true;
    }
    return false;
}

void JudgementEngine::debugFlickNotesNear(
    float songTimeSec, float windowSec, std::vector<FlickDebugNote>& out) const
{
    out.clear();
    for (const HitNote& note : mNotes) {
        if (static_cast<int>(note.kind) != 2) {
            continue; // flick notes only
        }
        const float delta = note.timeSec - songTimeSec;
        if (std::fabs(delta) > windowSec) {
            continue;
        }
        FlickDebugNote entry;
        entry.timeSec = note.timeSec;
        entry.center = note.center;
        entry.width = note.width;
        entry.dir = noteFlickDir(note);
        entry.state = note.state;
        out.push_back(entry);
    }
    std::sort(out.begin(), out.end(), [&](const FlickDebugNote& a, const FlickDebugNote& b) {
        return std::fabs(a.timeSec - songTimeSec) < std::fabs(b.timeSec - songTimeSec);
    });
}

} // namespace game
