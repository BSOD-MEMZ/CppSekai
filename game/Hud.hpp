// CppSekai - pjsk style HUD (score / life / combo / judge) drawn with ImGui
// draw lists on top of the playfield. Layout constants mirror the overlay in
// sekai-mmw-preview-web (AGPL-3.0): everything is authored in a 1920x1080
// virtual space and scaled to fit the window.
#pragma once

#include "platform/Renderer.hpp"

#include "game/Judgement.hpp"

#include <string>
#include <vector>

namespace game
{

struct HitFx
{
    float center = 0.0f; // lane coordinate of the hit
    float age = 0.0f;    // seconds since the hit
};

struct HudState
{
    double score = 0.0;
    int combo = 0;
    game::Judge lastJudge = game::Judge::None;
    float lastJudgeAtSec = -100.0f;
    float lifeRatio = 1.0f;
};

void drawHud(platform::Renderer& renderer, const HudState& state, float songTimeSec, int windowW, int windowH,
    const std::vector<HitFx>& hitEffects = {}, float leadInSec = 3.5f, bool dumpJudgeSheet = false);

} // namespace game
