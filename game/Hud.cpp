#include "Hud.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace game
{
namespace
{
    constexpr float SCORE_ROOT_SCALE = 1.5f;

    constexpr float scoreX(float v) { return 36.0f + v * SCORE_ROOT_SCALE; }
    constexpr float scoreY(float v) { return -3.0f + v * SCORE_ROOT_SCALE; }
    constexpr float scoreS(float v) { return v * SCORE_ROOT_SCALE; }

    std::string digitsOf(double value, int minSlots)
    {
        long long v = value < 0 ? 0 : static_cast<long long>(value + 0.5);
        std::string text = std::to_string(v);
        while (static_cast<int>(text.size()) < minSlots) {
            text.insert(text.begin(), '0');
        }
        return text;
    }
} // namespace

HudRect lifePauseRect()
{
    // Right 600/2560 of the 640x150 virtual life panel.
    constexpr float kLifeW = 640.0f;
    constexpr float kLifeH = 150.0f;
    return HudRect{1920.0f - kLifeW + kLifeW * (1960.0f / 2560.0f), 0.0f,
        kLifeW * (600.0f / 2560.0f), kLifeH};
}

ScoreRank scoreRankAndBar(double score, float rating)
{
    // 1:1 port of the upstream overlay player (native/src/mmw_overlay_player.cpp:
    // scoreRankAndBar). The thresholds scale with the chart's level.
    const double r = static_cast<double>(rating > 0.0f ? rating : 26.0f);
    const double rankBorder = 1200000.0 + (r - 5.0) * 4100.0;
    const double rankS = 1040000.0 + (r - 5.0) * 5200.0;
    const double rankA = 840000.0 + (r - 5.0) * 4200.0;
    const double rankB = 400000.0 + (r - 5.0) * 2000.0;
    const double rankC = 20000.0 + (r - 5.0) * 100.0;

    constexpr float rankBorderPos = 1.0f;
    constexpr float rankSPos = 1478.0f / 1650.0f;
    constexpr float rankAPos = 1234.0f / 1650.0f;
    constexpr float rankBPos = 990.0f / 1650.0f;
    constexpr float rankCPos = 746.0f / 1650.0f;

    const auto lerpRatio = [](double value, double start, double end, float startPos, float endPos) {
        if (end <= start) {
            return endPos;
        }
        return static_cast<float>(((value - start) / (end - start)) * (endPos - startPos) + startPos);
    };
    const auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };

    if (score >= rankBorder) {
        return ScoreRank{'s', rankBorderPos};
    }
    if (score >= rankS) {
        return ScoreRank{'s', clamp01(lerpRatio(score, rankS, rankBorder, rankSPos, rankBorderPos))};
    }
    if (score >= rankA) {
        return ScoreRank{'a', clamp01(lerpRatio(score, rankA, rankS, rankAPos, rankSPos))};
    }
    if (score >= rankB) {
        return ScoreRank{'b', clamp01(lerpRatio(score, rankB, rankA, rankBPos, rankAPos))};
    }
    if (score >= rankC) {
        return ScoreRank{'c', clamp01(lerpRatio(score, rankC, rankB, rankCPos, rankBPos))};
    }
    return ScoreRank{'d', clamp01(static_cast<float>(score / std::max(rankC, 1.0)) * rankCPos)};
}

void drawHud(platform::Renderer& renderer, const HudState& state, float songTimeSec, int windowW, int windowH,
    float leadInSec, bool dumpJudgeSheet)
{
    (void)leadInSec;
    // Background list: above the GL frame, but below pjsk dialog cards so
    // pause dialogs / panels can dim and cover the HUD.
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // Virtual 1920x1080 -> window transform (letterboxed, like the original).
    const float scale = std::min(static_cast<float>(windowW) / 1920.0f, static_cast<float>(windowH) / 1080.0f);
    const float offsetX = (static_cast<float>(windowW) - 1920.0f * scale) * 0.5f;
    const float offsetY = (static_cast<float>(windowH) - 1080.0f * scale) * 0.5f;
    const auto px = [&](float x) { return offsetX + x * scale; };
    const auto py = [&](float y) { return offsetY + y * scale; };
    const auto ps = [&](float v) { return v * scale; };

    auto img = [&](const std::string& name, float x, float y, float w, float h, float alpha = 1.0f, float clipRatio = 1.0f) {
        const platform::Renderer::HudSprite* sprite = renderer.hud(name);
        if (sprite == nullptr || sprite->id == 0) {
            return;
        }
        const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
        drawList->AddImage(
            reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(sprite->id)),
            ImVec2(px(x), py(y)),
            ImVec2(px(x) + ps(w), py(y) + ps(h)),
            ImVec2(0.0f, 0.0f),
            ImVec2(clipRatio, 1.0f),
            tint);
    };

    // ------------------------------------------------------------------
    // Intro: main.cpp owns the opening card + the playfield fade (see
    // game/Intro.cpp); nothing to draw here.
    // ------------------------------------------------------------------

    // ------------------------------------------------------------------
    // Judgement hit effects are gone from the HUD: the chart core's own
    // particle system (assets/mmw/effect.png + the embedded pjsk effect
    // definitions) draws them now, triggered by game/Judgement through
    // core_api::triggerNoteEffect(). That is the original effect 1:1 -
    // same spritesheet frames, timings and additive passes.
    // ------------------------------------------------------------------

    // ------------------------------------------------------------------
    // Score panel (top-left)
    // ------------------------------------------------------------------
    img("score_bg", scoreX(0.0f), scoreY(0.0f), scoreS(444), scoreS(96));
    // The bar follows the score, not the life: upstream maps the score onto
    // the bar through scoreRankAndBar() (see game/ScoreBar in main.cpp).
    img("score_bar", scoreX(79.0f), scoreY(37.0f), scoreS(354), scoreS(16),
        std::clamp(state.scoreBarRatio, 0.0f, 1.0f));
    img("score_fg", scoreX(0.0f), scoreY(0.0f), scoreS(444), scoreS(96));

    // Rank letter + its label (upstream: 49x58 at scoreX(10)/scoreY(13) and a
    // 60x8 label at scoreX(6)/scoreY(77), drawn between the frame and the
    // digits).
    {
        const char rankChar = state.rank >= 'a' && state.rank <= 's' ? state.rank : 'd';
        const std::string rankKey(1, rankChar);
        img("rank_char_" + rankKey, scoreX(10.0f), scoreY(13.0f), scoreS(49.0f), scoreS(58.0f));
        img("rank_txt_" + rankKey, scoreX(6.0f), scoreY(77.0f), scoreS(60.0f), scoreS(8.0f));
    }

    // Score text: no leading zeros - empty slots use the blank "n" sprite,
    // exactly like the upstream overlay (scoreDigitsText).
    std::string scoreText = digitsOf(state.score, 0);
    while (scoreText.size() < 8) {
        scoreText.insert(scoreText.begin(), 'n');
    }
    for (size_t i = 0; i < scoreText.size(); ++i) {
        const std::string key(1, scoreText[i]);
        const float slotX = scoreX(82.0f + static_cast<float>(i) * 22.0f);
        const float slotY = scoreY(60.0f);
        const platform::Renderer::HudSprite* shadow = renderer.hud("digit_s" + key);
        const platform::Renderer::HudSprite* main = renderer.hud("digit_" + key);
        // Upstream sizes: shadow 36px, glyph 29px, both centred on slotX + 11.
        // The sizes go through ps() as well - they are pixel-space, like the
        // px()/py() positions below. Without it the glyphs are drawn 1.5x
        // (SCORE_ROOT_SCALE) too wide for the 22u slot advance and overlap.
        const float shadowH = ps(scoreS(36.0f));
        const float mainH = ps(scoreS(29.0f));
        const float shadowW = shadow != nullptr && shadow->height > 0
            ? shadowH * (static_cast<float>(shadow->width) / static_cast<float>(shadow->height))
            : shadowH;
        const float mainW = main != nullptr && main->height > 0
            ? mainH * (static_cast<float>(main->width) / static_cast<float>(main->height))
            : mainH;
        const float centerX = px(slotX + scoreS(11.0f));
        if (shadow != nullptr && shadow->id != 0) {
            drawList->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(shadow->id)),
                ImVec2(centerX - shadowW * 0.5f, py(slotY - scoreS(4.0f))),
                ImVec2(centerX + shadowW * 0.5f, py(slotY - scoreS(4.0f)) + shadowH));
        }
        if (main != nullptr && main->id != 0) {
            drawList->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(main->id)),
                ImVec2(centerX - mainW * 0.5f, py(slotY)),
                ImVec2(centerX + mainW * 0.5f, py(slotY) + mainH));
        }
    }

    // ------------------------------------------------------------------
    // Life panel (top-right), pjsk life v3 sheet (2560x600, right 600x600
    // is the pause button zone). Drawn 640x150 in virtual space.
    // ------------------------------------------------------------------
    constexpr float kLifeW = 640.0f;
    constexpr float kLifeH = 150.0f;
    const float lifeX = 1920.0f - kLifeW;
    const float lifeY = 0.0f;
    img("life_bg", lifeX, lifeY, kLifeW, kLifeH);

    // The fill capsule lives at u in [0.148, 0.793], v in [0.40, 0.60] of the
    // sheet; it drains from the right end (next to the pause button) back
    // toward the heart as life is lost. Below 30% the red danger sheet is used.
    const float ratio = std::clamp(state.lifeRatio, 0.0f, 1.0f);
    if (ratio > 0.0f) {
        const float fillU0 = 0.1484f;
        const float fillU1 = 0.7930f;
        const float fillV0 = 0.40f;
        const float fillV1 = 0.60f;
        const char* fillKey = ratio <= 0.30f ? "life_danger" : "life_fill";
        const platform::Renderer::HudSprite* fill = renderer.hud(fillKey);
        if (fill != nullptr && fill->id != 0) {
            drawList->AddImage(
                reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(fill->id)),
                ImVec2(px(lifeX + fillU0 * kLifeW), py(lifeY + fillV0 * kLifeH)),
                ImVec2(px(lifeX + (fillU0 + (fillU1 - fillU0) * ratio) * kLifeW),
                    py(lifeY + fillV1 * kLifeH)),
                ImVec2(0.0f, fillV0),
                ImVec2((fillU1 - fillU0) * ratio, fillV1),
                IM_COL32(255, 255, 255, 255));
        }
    }

    // Life value digits, right-aligned left of the pause zone.
    {
        const int lifeValue = std::max(0, static_cast<int>(std::lround(1000.0f * ratio)));
        const std::string lifeText = std::to_string(lifeValue);
        const float rightEdge = lifeX + 470.0f;
        const float centerY = lifeY + 62.0f;
        float digitRight = rightEdge;
        for (size_t i = 0; i < lifeText.size(); ++i) {
            const std::string key(1, lifeText[lifeText.size() - 1 - i]);
            const platform::Renderer::HudSprite* main = renderer.hud("life_digit_" + key);
            const platform::Renderer::HudSprite* shadow = renderer.hud("life_digit_s" + key);
            const float mainH = 52.0f;
            const float shadowH = 57.0f;
            const float w = main != nullptr && main->height > 0
                ? mainH * (static_cast<float>(main->width) / static_cast<float>(main->height))
                : mainH * 0.75f;
            const float sw = shadow != nullptr && shadow->height > 0
                ? shadowH * (static_cast<float>(shadow->width) / static_cast<float>(shadow->height))
                : 0.0f;
            const float x = digitRight - w;
            img("life_digit_s" + key, x + (w - sw) * 0.5f, centerY - shadowH * 0.5f - 3.0f, sw, shadowH);
            img("life_digit_" + key, x, centerY - mainH * 0.5f, w, mainH);
            digitRight = x - 2.0f;
        }
    }

    // ------------------------------------------------------------------
    // Combo (right side)
    // ------------------------------------------------------------------
    if (state.combo >= 2) {
        const std::string comboText = std::to_string(state.combo);
        float totalW = 0.0f;
        for (const char ch : comboText) {
            const platform::Renderer::HudSprite* sprite = renderer.hud(std::string("combo_digit_n_") + ch);
            if (sprite != nullptr && sprite->height > 0) {
                totalW += 34.0f * (static_cast<float>(sprite->width) / static_cast<float>(sprite->height));
            }
        }
        float cursorX = 1634.0f - totalW * 0.5f;
        const float comboY = 420.0f;
        for (const char ch : comboText) {
            const std::string key(1, ch);
            const platform::Renderer::HudSprite* sprite = renderer.hud("combo_digit_n_" + key);
            const float h = 44.0f;
            const float w = sprite && sprite->height > 0 ? h * (static_cast<float>(sprite->width) / static_cast<float>(sprite->height)) : h;
            img("combo_digit_b" + key, cursorX, comboY + 3.0f, w, h * 1.08f, 0.85f);
            img("combo_digit_n_" + key, cursorX, comboY, w, h);
            cursorX += w;
        }
        img("combo_tag", 1634.0f - 63.5f, comboY + 50.0f, 127.0f, 42.0f);
    }

    // ------------------------------------------------------------------
    // Judge text. Official sprites: 1=PERFECT 2=GREAT 3=GOOD 4=BAD
    // 5=MISS 6=AUTO. Only shown on an actual judge; miss uses sprite 5.
    //
    // 1:1 port of the upstream overlay (native/src/mmw_overlay_player.cpp):
    // the text stays invisible until frame 2, pops in over frames 2..5 with a
    // quartic ease (rawScale 0 -> 2/3) and then holds scale 1 (rawScale * 1.5)
    // for the rest of the 0.24s judge window. Base size 310x81 centred at
    // (960, 667.5) in the 1920x1080 HUD space.
    // ------------------------------------------------------------------
    constexpr float kJudgeVisibleSec = 0.24f;
    constexpr float kJudgeBaseH = 81.0f; // sprite height at scale 1 (PERFECT is 310x81)
    constexpr float kJudgeCenterX = 960.0f;
    constexpr float kJudgeCenterY = 667.5f;

    const float since = songTimeSec - state.lastJudgeAtSec;
    int judgeSprite = 0;
    if (state.lastJudge == game::Judge::Perfect) judgeSprite = 1;
    else if (state.lastJudge == game::Judge::Great) judgeSprite = 2;
    else if (state.lastJudge == game::Judge::Good) judgeSprite = 3;
    else if (state.lastJudge == game::Judge::Bad) judgeSprite = 4;
    else if (state.lastJudge == game::Judge::Miss) judgeSprite = 5;

    if (judgeSprite > 0 && since >= 0.0f && since <= kJudgeVisibleSec) {
        const float progressFrames = since * 60.0f;
        float alpha = 1.0f;
        float rawScale = 2.0f / 3.0f;
        if (progressFrames < 2.0f) {
            alpha = 0.0f;
        } else if (progressFrames < 5.0f) {
            const float t = -1.45f + progressFrames / 4.0f;
            rawScale = (2.0f / 3.0f) - t * t * t * t * (2.0f / 3.0f);
        }
        const float scale = std::max(0.01f, rawScale * 1.5f);
        const std::string key = "judge_" + std::to_string(judgeSprite);
        // The upstream constant is PERFECT's own size; every other judge sprite
        // shares the height and keeps its own aspect.
        const platform::Renderer::HudSprite* sprite = renderer.hud(key);
        const float aspect = sprite != nullptr && sprite->height > 0
            ? static_cast<float>(sprite->width) / static_cast<float>(sprite->height)
            : 310.0f / 81.0f;
        const float h = kJudgeBaseH * scale;
        const float w = h * aspect;
        img(key, kJudgeCenterX - w * 0.5f, kJudgeCenterY - h * 0.5f, w, h, alpha);
    }

    if (dumpJudgeSheet) {
        for (int i = 1; i <= 6; ++i) {
            const platform::Renderer::HudSprite* sprite = renderer.hud("judge_" + std::to_string(i));
            if (sprite == nullptr || sprite->height == 0) {
                continue;
            }
            const float h = 70.0f;
            const float w = h * (static_cast<float>(sprite->width) / static_cast<float>(sprite->height));
            img("judge_" + std::to_string(i), 200.0f + static_cast<float>(i - 1) * (w + 20.0f), 850.0f, w, h, 1.0f);
        }
    }
}

} // namespace game
