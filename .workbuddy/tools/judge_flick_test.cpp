// Flick direction regression: verifies the rules reverse-engineered from
// Sekai.Live.FlickNote (see JUDGE-WINDOWS.md §4).
//
//   isTrueDirection = dot(normalize(v), noteDir) >= 0 || Direction == Default
//   a wrong direction still clears the note, capped at GREAT
//   a long-tail flick cannot resolve earlier than noteTime + 1/120 s
//
// Build & run:
//   g++ -std=c++17 -I. .workbuddy/tools/judge_flick_test.cpp \
//       game/Judgement.cpp platform/Log.cpp -o /d/tmp/judge_flick_test.exe
//   /d/tmp/judge_flick_test.exe
#include "game/Judgement.hpp"

#include <cmath>
#include <cstdio>

namespace
{
int gFailures = 0;

void expect(const char* what, game::Judge got, game::Judge want)
{
    const bool ok = got == want;
    std::printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        std::printf("    got %d want %d\n", static_cast<int>(got), static_cast<int>(want));
        ++gFailures;
    }
}

// One packed HitEvent: {time, center, width, kind, flags, endTime, volume}
void loadOne(game::JudgementEngine& engine, float time, int kind, int flickDir, bool holdTail)
{
    float packed[7] = {time, 0.0f, 1.0f, static_cast<float>(kind), static_cast<float>(flickDir * 2),
        -1.0f, 1.0f};
    if (holdTail) {
        packed[4] += 8.0f;
    }
    engine.load(packed, 1);
}
} // namespace

int main()
{
    using namespace game;
    JudgementEngine engine;
    engine.setStrictFlick(true);

    // A LEFT flick note: a straight-up swipe has x == 0, so dot((0,1),(-1,0))
    // is 0 and the official test calls it correct. The old code rejected it.
    loadOne(engine, 1.0f, 2, static_cast<int>(FlickLeft), false);
    expect("left flick + straight up swipe", engine.flickVector(0.0f, 1.0f, 0.0f, 1.0f), Judge::Perfect);

    loadOne(engine, 1.0f, 2, static_cast<int>(FlickLeft), false);
    expect("left flick + swipe left", engine.flickVector(0.0f, 1.0f, -1.0f, 0.2f), Judge::Perfect);

    // Wrong way: capped at GREAT rather than dropped.
    loadOne(engine, 1.0f, 2, static_cast<int>(FlickLeft), false);
    expect("left flick + swipe right (capped)", engine.flickVector(0.0f, 1.0f, 1.0f, 0.2f), Judge::Great);

    // A RIGHT flick accepts a straight-up swipe as well.
    loadOne(engine, 1.0f, 2, static_cast<int>(FlickRight), false);
    expect("right flick + straight up swipe", engine.flickVector(0.0f, 1.0f, 0.0f, 1.0f), Judge::Perfect);

    // A default (arrow-less) flick ignores the direction entirely.
    loadOne(engine, 1.0f, 2, static_cast<int>(FlickUp), false);
    expect("default flick + swipe right", engine.flickVector(0.0f, 1.0f, 1.0f, 0.0f), Judge::Perfect);

    // Long-tail flick: must land at least 1/120 s late.
    loadOne(engine, 1.0f, 2, static_cast<int>(FlickUp), true);
    expect("hold tail flick 5 ms early", engine.flickVector(0.0f, 0.995f, 0.0f, 1.0f), Judge::None);

    loadOne(engine, 1.0f, 2, static_cast<int>(FlickUp), true);
    expect("hold tail flick 20 ms late", engine.flickVector(0.0f, 1.020f, 0.0f, 1.0f), Judge::Perfect);

    // A plain tap still must not clear a flick note in strict mode.
    loadOne(engine, 1.0f, 2, static_cast<int>(FlickUp), false);
    expect("flick note + plain tap (strict)", engine.tap(0.0f, 1.0f, false), Judge::None);

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "PASS" : "FAILED", gFailures,
        gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
