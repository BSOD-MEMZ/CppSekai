// CppSekai - pjsk style result screen.
// Rebuilt 1:1 from a reference screenshot of the official result screen
// (2388x1080 phone capture): the top strip panel with the song card + score
// bar + SCORERANK plate, the big score counter, the judge counts and the
// 继续 button. Everything is authored in the 1920x1080 virtual space the HUD
// uses, with the right-hand cluster (score bar / rank plate / button)
// anchored to the panel's right edge - the phone layout reserves that space
// for the character, which we do not draw.
#pragma once

#include "platform/Renderer.hpp"

#include <string>

namespace game
{

struct ResultData
{
    std::string title;
    std::string difficulty; // "EXPERT" / "MASTER" / ...
    std::string level;      // "23"
    double score = 0.0;
    double highScore = 0.0; // previous best (0 = first clear)
    bool newRecord = false;
    int perfect = 0;
    int great = 0;
    int good = 0;
    int bad = 0;
    int miss = 0;
    int maxCombo = 0;
    float chartRating = 26.0f; // drives the square-rule / score bar thresholds

    // Player rank (the account). rank/exp are the *post-run* values, so the chip
    // and the bar already show the rank this score just earned.
    int playerRank = 1;
    double playerExp = 0.0;     // banked towards the next rank
    double playerExpNeed = 0.0; // what that next rank costs
    int expGain = 0;            // granted by this run
    int rankUps = 0;            // > 0 -> the screen shows 等级提升
};

// Draws the whole screen on ImGui's background draw list (call after
// ImGui::NewFrame, before ImGui::Render). `elapsedSec` is the time since the
// screen appeared and drives the entrance animation. `uiScale` zooms the whole
// canvas (UserSettings::uiScale, 1.0 = fit the window).
void drawResult(platform::Renderer& renderer, const ResultData& data, float elapsedSec,
    int windowW, int windowH, float uiScale = 1.0f);

// Hit test for the 继续 button, in *window pixel* coordinates. The press is
// handled by the SDL event path (the same way the HUD pause button and the
// intro's round skip key are), not by ImGui: the synthesized mouse events touch
// contacts produce are filtered out in main.cpp, so an ImGui-only test would
// leave the button dead on a touchscreen. Pass the same uiScale the frame was
// drawn with, or the hitbox and the button drift apart.
bool resultContinueHitTest(int windowW, int windowH, int x, int y, float uiScale = 1.0f);

} // namespace game
