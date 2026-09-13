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
};

// Draws the whole screen on ImGui's background draw list (call after
// ImGui::NewFrame, before ImGui::Render). `elapsedSec` is the time since the
// screen appeared and drives the entrance animation. Returns true when 继续
// was pressed this frame.
bool drawResult(platform::Renderer& renderer, const ResultData& data, float elapsedSec,
    int windowW, int windowH);

} // namespace game
