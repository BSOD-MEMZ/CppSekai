#include "Judgement.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>

namespace game
{

namespace
{
    // Life costs, from the official pjsk judgement table: a whole-note MISS
    // costs 80, a BAD costs 50, and a hold broken mid-way costs 40.
    constexpr float kLifeMiss = -80.0f;
    constexpr float kLifeBad = -50.0f;
    constexpr float kLifeHoldBreak = -40.0f;

    // Release grace / grab grace now live in JudgementWindows
    // (holdTailGraceMs / holdStartGraceMs) so they can be tuned in the settings
    // dialog; see Judgement.hpp.

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
    std::printf("[judge] %d event(s): %d note(s), %d hold(s), %d tail(s) matched%s\n",
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
    FlickDir flickDir)
{
    HitNote* best = nullptr;
    float bestAbsDt = std::numeric_limits<float>::max();

    const float badSec = mWindows.badMs / 1000.0f;
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
        if (wantFlick && mStrictFlick && !flickAsTap) {
            const FlickDir noteDir = static_cast<FlickDir>(noteFlickDir(note));
            // Up/default flicks (and legacy FlickNone) accept any upward
            // swipe; left/right flicks require the matching horizontal swipe.
            const bool dirOk = noteDir == FlickNone || noteDir == FlickUp
                ? flickDir == FlickUp || flickDir == FlickNone
                : flickDir == noteDir;
            if (!dirOk) {
                continue;
            }
        }
        if (!laneCovers(note, lanePos, margin)) {
            continue;
        }
        const float dt = std::fabs(note.timeSec - songTimeSec);
        if (dt <= badSec && dt < bestAbsDt) {
            bestAbsDt = dt;
            best = &note;
        }
    }

    if (best == nullptr) {
        return nullptr;
    }
    const float dtMs = bestAbsDt * 1000.0f;
    Judge judge;
    if (dtMs <= mWindows.perfectMs) {
        judge = Judge::Perfect;
    } else if (dtMs <= mWindows.greatMs) {
        judge = Judge::Great;
    } else if (dtMs <= mWindows.goodMs) {
        judge = Judge::Good;
    } else {
        judge = Judge::Bad;
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
    HitNote* hit = findCandidate(lanePos, songTimeSec, margin, true, dir);
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
                if (note.holdMarkerIndex < mNotes.size()) {
                    for (const ActiveHold& hold : mActiveHolds) {
                        if (hold.noteIndex == note.holdMarkerIndex && hold.broken) {
                            holdBroken = true;
                            break;
                        }
                    }
                }
            }

            if (startState == 1 && !holdBroken) {
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
            } else if (startState == 2 || holdBroken) {
                // The hold's own MISS / break already counted; swallow the
                // tick without judging it again.
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
            // A tail is resolved by its hold (release at the end). Only when
            // it is still pending well after its time the hold never started
            // at all - then it is a plain miss. This also guarantees the
            // cursor keeps moving even if a hold is dropped.
            if (note.timeSec < songTimeSec - missSec) {
                note.state = 2;
                registerMiss(songTimeSec, kLifeMiss);
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
                continue;
            }
            // Not covered: fall through to the normal miss timing below.
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
            continue;
        }

        if (note.timeSec < songTimeSec - missSec) {
            note.state = 2;
            registerMiss(songTimeSec, kLifeMiss);
            continue;
        }
    }

    // Hold tracking: judge the tail on release, break the hold when the lane
    // is let go too early.
    for (auto& hold : mActiveHolds) {
        if (hold.finished) {
            continue;
        }

        // In autoplay the lane is always considered held: holds never break
        // and tails hold through for a PERFECT.
        const bool held = mAutoPlay || laneHeld(hold);

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
            // Never grabbed: the start's own MISS already counted for the
            // whole hold, so the tail is consumed silently. The renderer is
            // told the hold was missed so its body keeps scrolling past the
            // line instead of parking on it like a held hold.
            hold.broken = true;
            hold.finished = true;
            if (hold.tailIndex < mNotes.size() && mNotes[hold.tailIndex].state == 0) {
                mNotes[hold.tailIndex].state = 2;
            }
            mMissedHoldKeys.push_back(hold.center);
            mMissedHoldKeys.push_back(hold.startTimeSec);
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
                const float dtMs = (hold.endTimeSec - hold.releaseTimeSec) * 1000.0f;
                if (dtMs <= mWindows.perfectMs) {
                    judge = Judge::Perfect;
                } else if (dtMs <= mWindows.greatMs) {
                    judge = Judge::Great;
                } else if (dtMs <= mWindows.goodMs) {
                    judge = Judge::Good;
                } else {
                    judge = Judge::Bad;
                }
            }
            judgeHoldTail(hold, judge, songTimeSec);
        }
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
