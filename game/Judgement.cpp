#include "Judgement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game
{

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
        mNotes.push_back(note);
    }
    mCursor = 0;
    mActiveHolds.clear();
    mStats = JudgementStats{};
    mLoaded = true;

    // Score-able notes: taps, flicks, traces, hold starts. Hold ticks and
    // hold-loop markers are auto-resolved.
    mTotalScoreNotes = 0;
    for (const auto& note : mNotes) {
        if (note.kind == 0.0f || note.kind == 1.0f || note.kind == 2.0f || note.kind == 3.0f || note.kind == 5.0f) {
            ++mTotalScoreNotes;
        }
    }
}

void JudgementEngine::reset()
{
    for (auto& note : mNotes) {
        note.state = 0;
    }
    mCursor = 0;
    mActiveHolds.clear();
    mStats = JudgementStats{};
}

void JudgementEngine::registerMiss()
{
    mStats.miss += 1;
    mStats.combo = 0;
    mStats.lastJudge = Judge::Miss;
}

Judge JudgementEngine::registerJudge(Judge judge, bool critical, float volume)
{
    (void)volume;
    switch (judge) {
        case Judge::Perfect:
            mStats.perfect += 1;
            mStats.score += critical ? 1500.0 : 1000.0;
            break;
        case Judge::Great:
            mStats.great += 1;
            mStats.score += 800.0;
            break;
        case Judge::Good:
            mStats.good += 1;
            mStats.score += 500.0;
            break;
        default:
            break;
    }
    mStats.combo += 1;
    mStats.maxCombo = std::max(mStats.maxCombo, mStats.combo);
    mStats.lastJudge = judge;
    mStats.lastJudgeCritical = critical;
    return judge;
}

bool JudgementEngine::laneCovers(const HitNote& note, float lanePos, float margin) const
{
    const float half = std::max(0.5f, note.width * 0.5f);
    return lanePos >= note.center - half - margin && lanePos <= note.center + half + margin;
}

HitNote* JudgementEngine::findCandidate(float lanePos, float songTimeSec, float margin, bool wantFlick)
{
    HitNote* best = nullptr;
    float bestAbsDt = std::numeric_limits<float>::max();

    const float goodSec = mWindows.goodMs / 1000.0f;
    const std::size_t scanEnd = std::min(mNotes.size(), mCursor + 256);

    for (std::size_t i = mCursor; i < scanEnd; ++i) {
        HitNote& note = mNotes[i];
        if (note.state != 0) {
            continue;
        }
        if (note.timeSec > songTimeSec + goodSec) {
            break;
        }
        // Flick notes (kind 2) and tap/trace notes are player-hit. Kind 4
        // ticks are auto and kind 5 markers are hold bookkeeping.
        if (note.kind == 4.0f || note.kind == 5.0f) {
            continue;
        }
        const bool noteIsFlick = note.kind == 2.0f;
        if (wantFlick != noteIsFlick) {
            // In this skeleton a plain tap also clears a flick note; the
            // strict flick gesture check is a TODO.
            continue;
        }
        if (!laneCovers(note, lanePos, margin)) {
            continue;
        }
        const float dt = std::fabs(note.timeSec - songTimeSec);
        if (dt <= goodSec && dt < bestAbsDt) {
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
    } else {
        judge = Judge::Good;
    }
    best->state = 1;
    const bool critical = best->flags != 0.0f;
    mStats.lastHitKind = best->kind;
    registerJudge(judge, critical, best->volume);
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

Judge JudgementEngine::flick(float lanePos, float songTimeSec, float margin)
{
    HitNote* hit = findCandidate(lanePos, songTimeSec, margin, true);
    if (hit == nullptr) {
        return Judge::None;
    }
    mStats.lastJudgeTimeSec = songTimeSec;
    return mStats.lastJudge;
}

void JudgementEngine::update(float songTimeSec)
{
    if (!mLoaded) {
        return;
    }

    // Advance cursor past fully judged notes.
    while (mCursor < mNotes.size() && mNotes[mCursor].state != 0) {
        ++mCursor;
    }

    const float goodSec = mWindows.goodMs / 1000.0f;
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
            // Hold ticks auto-hit while a hold covering this lane is active
            // (skeleton: auto-hit unconditionally, matching no-fail preview).
            note.state = 1;
            registerJudge(Judge::Perfect, note.flags != 0.0f, note.volume);
            mStats.lastJudgeTimeSec = songTimeSec;
            continue;
        }

        if (note.kind == 5.0f) {
            // Hold-loop marker: spawns hold tracking when the start note was
            // hit. We detect via matching active holds instead; skip here.
            continue;
        }

        if (note.timeSec < songTimeSec - missSec) {
            note.state = 1;
            registerMiss();
            continue;
        }
        (void)goodSec;
    }

    // Hold tracking: break holds whose lane is no longer held.
    const float holdGraceSec = 0.18f;
    for (auto& hold : mActiveHolds) {
        if (hold.broken) {
            continue;
        }
        if (songTimeSec >= hold.endTimeSec) {
            hold.broken = true;
            continue;
        }
        bool held = false;
        for (const float lane : mHoldLanes) {
            const float half = std::max(0.5f, hold.width * 0.5f);
            if (lane >= hold.center - half - 0.5f && lane <= hold.center + half + 0.5f) {
                held = true;
                break;
            }
        }
        if (!held && songTimeSec < hold.endTimeSec - holdGraceSec) {
            hold.broken = true;
            mStats.combo = 0;
            mStats.lastJudge = Judge::Miss;
            mStats.lastJudgeTimeSec = songTimeSec;
        }
    }

    // Spawn hold tracking from hit hold-start notes (kind 5 markers preceded
    // by a hit tap in the same lane at the same time). We detect kind 5
    // markers directly: when the marker time arrives, check whether the
    // corresponding tap (same timeSec, same lane) was hit.
    for (std::size_t i = mCursor; i < scanEnd; ++i) {
        HitNote& marker = mNotes[i];
        if (marker.state != 0 || marker.kind != 5.0f) {
            continue;
        }
        if (marker.timeSec > songTimeSec) {
            break;
        }
        marker.state = 1;
        // Find sibling tap (same time & lane, kind 0/1) that was judged.
        bool startHit = false;
        for (std::size_t j = i; j-- > 0;) {
            const HitNote& candidate = mNotes[j];
            if (std::fabs(candidate.timeSec - marker.timeSec) > 0.05f) {
                break;
            }
            if ((candidate.kind == 0.0f || candidate.kind == 1.0f) && std::fabs(candidate.center - marker.center) < 0.01f && candidate.state == 1) {
                startHit = true;
                break;
            }
        }
        if (startHit) {
            mActiveHolds.push_back(ActiveHold{
                i,
                marker.endTimeSec,
                marker.center,
                marker.width,
                false,
            });
        }
    }

    // Retire finished holds.
    mActiveHolds.erase(std::remove_if(mActiveHolds.begin(), mActiveHolds.end(),
                          [](const ActiveHold& hold) {
                              return hold.broken;
                          }),
        mActiveHolds.end());
}

} // namespace game
