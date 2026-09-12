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
    // Right 600x600 of the 2560x600 life sheet, drawn at (1442, 11) 444x104.
    constexpr float kLifeW = 444.0f;
    constexpr float kLifeH = 104.0f;
    constexpr float kLifeX = 1442.0f;
    constexpr float kLifeY = 11.0f;
    return HudRect{kLifeX + kLifeW * (1960.0f / 2560.0f), kLifeY,
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
    // Upstream drawHudImageClipX: the quad is *cropped* to the ratio (width
    // shrinks with the UVs), not stretched - the score bar uses this so the
    // fill actually grows from the left instead of the texture being squeezed
    // into the full-width slot.
    auto imgClipX = [&](const std::string& name, float x, float y, float w, float h, float ratio, float alpha = 1.0f) {
        const platform::Renderer::HudSprite* sprite = renderer.hud(name);
        if (sprite == nullptr || sprite->id == 0) {
            return;
        }
        const float clipped = std::clamp(ratio, 0.0f, 1.0f);
        if (clipped <= 0.0f) {
            return;
        }
        const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
        drawList->AddImage(
            reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(sprite->id)),
            ImVec2(px(x), py(y)),
            ImVec2(px(x) + ps(w * clipped), py(y) + ps(h)),
            ImVec2(0.0f, 0.0f),
            ImVec2(clipped, 1.0f),
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
    // the bar through scoreRankAndBar() and draws it *cropped* (quad width
    // shrinks with the UVs) - see imgClipX above.
    imgClipX("score_bar", scoreX(79.0f), scoreY(37.0f), scoreS(354), scoreS(16),
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
    // Life panel (top-right). The life v3 sheets are 2560x600 and the upstream
    // overlay draws the whole sheet at 444x104, positioned at (1442, 11) -
    // right 600x600 of the sheet is the pause button zone.
    // ------------------------------------------------------------------
    constexpr float kLifeW = 444.0f;
    constexpr float kLifeH = 104.0f;
    constexpr float kLifeX = 1442.0f;
    constexpr float kLifeY = 11.0f;
    img("life_bg", kLifeX, kLifeY, kLifeW, kLifeH);

    // life_fill / life_danger / life_overflow are the same 2560x600 sheets with
    // only the green capsule drawn. Measured off the png, the capsule occupies
    // u [0.1531, 0.7414] / v [0.4617, 0.6083]. Draw its left `ratio` fraction:
    // the bar drains from the right end (next to the pause button) back toward
    // the heart. The UV *must* start at fillU0 - sampling from 0 picks up the
    // transparent left margin and the fill lands in the wrong place.
    const float ratio = std::clamp(state.lifeRatio, 0.0f, 1.0f);
    if (ratio > 0.0f) {
        constexpr float fillU0 = 0.1531f;
        constexpr float fillU1 = 0.7414f;
        constexpr float fillV0 = 0.4617f;
        constexpr float fillV1 = 0.6083f;
        const char* fillKey = ratio <= 0.30f ? "life_danger" : "life_fill";
        const platform::Renderer::HudSprite* fill = renderer.hud(fillKey);
        if (fill != nullptr && fill->id != 0) {
            const float fillUEnd = fillU0 + (fillU1 - fillU0) * ratio;
            drawList->AddImage(
                reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(fill->id)),
                ImVec2(px(kLifeX + fillU0 * kLifeW), py(kLifeY + fillV0 * kLifeH)),
                ImVec2(px(kLifeX + fillUEnd * kLifeW), py(kLifeY + fillV1 * kLifeH)),
                ImVec2(fillU0, fillV0),
                ImVec2(fillUEnd, fillV1),
                IM_COL32(255, 255, 255, 255));
        }
    }

    // Life value digits, right-aligned in the empty top-right of the pill
    // (upstream: slotX = 1442+319 - i*22, slotY = 11+10, shadow 37 / glyph 34).
    {
        const int lifeValue = std::max(0, static_cast<int>(std::lround(1000.0f * ratio)));
        const std::string lifeText = std::to_string(lifeValue);
        for (size_t i = 0; i < lifeText.size(); ++i) {
            const std::string key(1, lifeText[lifeText.size() - 1 - i]);
            const float slotX = kLifeX + 319.0f - static_cast<float>(i) * 22.0f;
            const float slotY = kLifeY + 10.0f;
            const platform::Renderer::HudSprite* shadow = renderer.hud("life_digit_s" + key);
            const platform::Renderer::HudSprite* main = renderer.hud("life_digit_" + key);
            const float shadowH = 37.0f;
            const float mainH = 34.0f;
            const float shadowW = shadow != nullptr && shadow->height > 0
                ? shadowH * (static_cast<float>(shadow->width) / static_cast<float>(shadow->height))
                : 0.0f;
            const float mainW = main != nullptr && main->height > 0
                ? mainH * (static_cast<float>(main->width) / static_cast<float>(main->height))
                : mainH * 0.75f;
            const float centerX = slotX + 13.0f;
            img("life_digit_s" + key, centerX - shadowW * 0.5f, slotY - 2.0f, shadowW, shadowH);
            img("life_digit_" + key, centerX - mainW * 0.5f, slotY, mainW, mainH);
        }
    }

    // ------------------------------------------------------------------
    // Combo (right side). 1:1 port of the upstream overlay: digits + tag form
    // one group scaled about (1634, 478); each hit pops the digits and fires a
    // short glow burst, and the glow breathes with the AP pulse.
    // ------------------------------------------------------------------
    if (state.combo > 0) {
        constexpr float COMBO_BASE_SCALE = 0.85f;
        constexpr float COMBO_DIGIT_STEP = 92.0f;
        constexpr float COMBO_GROUP_SCALE = 1.25f;
        constexpr float COMBO_GROUP_OFFSET_Y = -12.0f;
        constexpr float COMBO_GROUP_CENTER_X = 1634.0f;
        constexpr float COMBO_GROUP_CENTER_Y = 478.0f;
        auto comboGroupX = [&](float x) {
            return COMBO_GROUP_CENTER_X + (x - COMBO_GROUP_CENTER_X) * COMBO_GROUP_SCALE;
        };
        auto comboGroupY = [&](float y) {
            return COMBO_GROUP_CENTER_Y + (y - COMBO_GROUP_CENTER_Y) * COMBO_GROUP_SCALE
                + COMBO_GROUP_OFFSET_Y;
        };
        auto comboGroupS = [&](float v) { return v * COMBO_GROUP_SCALE; };

        constexpr float kApPulseAngular = 3.14159265359f * (4.0f / 3.0f);
        const float apAlpha = std::clamp((std::sin(songTimeSec * kApPulseAngular) + 1.0f) * 0.5f, 0.0f, 1.0f);

        constexpr float kTagGlowW = 197.0f * 0.67f;
        constexpr float kTagGlowH = 79.0f * 0.67f;
        img("combo_tag_glow", comboGroupX(1634.0f - kTagGlowW * 0.5f),
            comboGroupY((478.0f - 70.0f) - kTagGlowH * 0.5f), comboGroupS(kTagGlowW),
            comboGroupS(kTagGlowH), apAlpha);
        constexpr float kTagW = 127.0f;
        constexpr float kTagH = 42.0f;
        img("combo_tag", comboGroupX(1634.0f - kTagW * 0.5f),
            comboGroupY(478.0f - 67.0f - kTagH * 0.5f), comboGroupS(kTagW), comboGroupS(kTagH));

        // Pop scale: within 8 frames of the last combo increment the digits
        // shrink back to base, and the glow bursts over the first 14 frames.
        // (lastJudgeAtSec is the last combo-positive judge, so combo > 0 means
        // it is also the last time the counter went up.)
        const float progress = (songTimeSec - state.lastJudgeAtSec) * 60.0f;
        float comboScale = COMBO_BASE_SCALE;
        if (progress >= 0.0f && progress < 1000.0f) {
            const float shiftScale = std::min(1.0f, std::max(0.5f, (progress / 8.0f) * 0.5f + 0.5f));
            comboScale = COMBO_BASE_SCALE * shiftScale;
        }
        const float burstAlpha =
            (progress >= 0.0f && progress < 14.0f) ? std::max(0.0f, 1.0f - progress / 14.0f) : 0.0f;

        const std::string comboText = std::to_string(state.combo);
        const float mid = static_cast<float>(comboText.size()) / 2.0f;
        constexpr float comboCenterYOffset = 18.0f;
        for (size_t i = 0; i < comboText.size(); ++i) {
            const char ch = comboText[i];
            const float left = (static_cast<float>(i) - mid + 0.5f) * COMBO_DIGIT_STEP * comboScale;
            const float centerX = comboGroupX(1634.0f + left);
            const std::string key(1, ch);
            const platform::Renderer::HudSprite* glow = renderer.hud("combo_digit_b_" + key);
            const platform::Renderer::HudSprite* main = renderer.hud("combo_digit_n_" + key);
            const float mainH = comboGroupS(134.0f * comboScale);
            const float glowH = comboGroupS(150.0f * comboScale);
            const float mainW = main != nullptr && main->height > 0
                ? mainH * (static_cast<float>(main->width) / static_cast<float>(main->height))
                : mainH;
            const float glowW = glow != nullptr && glow->height > 0
                ? glowH * (static_cast<float>(glow->width) / static_cast<float>(glow->height))
                : glowH;
            const float centerY = comboGroupY(478.0f + comboCenterYOffset * comboScale);
            const float digitGlowAlpha = std::min(1.0f, 0.18f + apAlpha * 0.82f);
            // NOTE the sprite keys carry a trailing underscore
            // ("combo_digit_b_0"), unlike the life/score ones.
            img("combo_digit_b_" + key, centerX - glowW * 0.5f, centerY - glowH * 0.5f, glowW, glowH,
                digitGlowAlpha);
            img("combo_digit_n_" + key, centerX - mainW * 0.5f, centerY - mainH * 0.5f, mainW, mainH);
            if (burstAlpha > 0.0f) {
                // Hit burst pass: intentionally larger than the base glow.
                const float burstScaleMul = 1.28f + 0.22f * burstAlpha;
                img("combo_digit_b_" + key, centerX - glowW * 0.5f * burstScaleMul,
                    centerY - glowH * 0.5f * burstScaleMul, glowW * burstScaleMul,
                    glowH * burstScaleMul, std::min(1.0f, (0.35f + apAlpha * 0.65f) * burstAlpha));
            }
        }
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
    // Autoplay preview shows AUTO (sprite 6) with the same pop-in animation.
    if (state.autoJudge && judgeSprite > 0) {
        judgeSprite = 6;
    }

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
