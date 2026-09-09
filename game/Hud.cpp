#include "Hud.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace game
{
namespace
{
    constexpr float SCORE_ROOT_SCALE = 1.5f;
    constexpr float JUDGE_LINE_Y = 1.0f; // fake-perspective height of the judge line

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

void drawHud(platform::Renderer& renderer, const HudState& state, float songTimeSec, int windowW, int windowH,
    const std::vector<HitFx>& hitEffects, float leadInSec, bool dumpJudgeSheet)
{
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
    // Judgement hit effects - spawned by real hits and by lane presses
    // (a press without a note still flashes the judge line).
    // ------------------------------------------------------------------
    for (const HitFx& fx : hitEffects) {
        const platform::Renderer::HudSprite* sprite = renderer.hud("effect_hit");
        if (sprite == nullptr || sprite->id == 0) {
            break;
        }
        const float t = std::clamp(fx.age / 0.35f, 0.0f, 1.0f);
        const float alpha = (1.0f - t) * std::clamp(fx.strength, 0.0f, 1.0f);
        const float grow = (1.0f + t * 0.6f) * (0.55f + 0.45f * std::clamp(fx.strength, 0.0f, 1.0f));
        float sx = 0.0f, sy = 0.0f, sx2 = 0.0f, sy2 = 0.0f;
        // The playfield is a fake perspective: a lane coordinate x at
        // height y is drawn at world (x * y, y); y = 1 is the judge line.
        renderer.worldToScreen((fx.center - 1.1f * grow) * JUDGE_LINE_Y, JUDGE_LINE_Y, sx, sy);
        renderer.worldToScreen((fx.center + 1.1f * grow) * JUDGE_LINE_Y, JUDGE_LINE_Y, sx2, sy2);
        // Center the effect on the judge line; both corners above share the
        // same world y, so the square height is derived from the width.
        const float halfH = (sx2 - sx) * 0.5f;
        const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f));
        drawList->AddImage(
            reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(sprite->id)),
            ImVec2(sx, sy - halfH), ImVec2(sx2, sy + halfH),
            ImVec2(0, 0), ImVec2(1, 1), tint);
    }

    // ------------------------------------------------------------------
    // Score panel (top-left)
    // ------------------------------------------------------------------
    img("score_bg", scoreX(0.0f), scoreY(0.0f), scoreS(444), scoreS(96));
    img("score_bar", scoreX(79.0f), scoreY(37.0f), scoreS(354), scoreS(16), 1.0f, state.lifeRatio);
    img("score_fg", scoreX(0.0f), scoreY(0.0f), scoreS(444), scoreS(96));

    const std::string scoreText = digitsOf(state.score, 7);
    for (int i = 0; i < 7; ++i) {
        const char ch = scoreText[static_cast<size_t>(i)];
        const std::string key = std::string(1, ch);
        const float slotX = scoreX(82.0f + static_cast<float>(i) * 22.0f);
        const float slotY = scoreY(60.0f);
        const platform::Renderer::HudSprite* shadow = renderer.hud("digit_s" + key);
        const platform::Renderer::HudSprite* main = renderer.hud("digit_" + key);
        const float shadowH = ps(scoreS(36.0f));
        const float mainH = ps(scoreS(33.0f));
        if (shadow != nullptr && shadow->height > 0) {
            const float w = shadowH * (static_cast<float>(shadow->width) / static_cast<float>(shadow->height));
            img("digit_s" + key, slotX - 22.0f * 0.5f - w * 0.5f / scale, slotY - 2.0f, w / scale, shadowH / scale);
        }
        if (main != nullptr && main->height > 0) {
            const float w = mainH * (static_cast<float>(main->width) / static_cast<float>(main->height));
            img("digit_" + key, slotX - 22.0f * 0.5f - w * 0.5f / scale, slotY, w / scale, mainH / scale);
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
    // ------------------------------------------------------------------
    const float since = songTimeSec - state.lastJudgeAtSec;
    int judgeSprite = 0;
    if (state.lastJudge == game::Judge::Perfect) judgeSprite = 1;
    else if (state.lastJudge == game::Judge::Great) judgeSprite = 2;
    else if (state.lastJudge == game::Judge::Good) judgeSprite = 3;
    else if (state.lastJudge == game::Judge::Miss) judgeSprite = 5;

    if (judgeSprite > 0 && since >= 0.0f && since < 0.5f) {
        const std::string key = "judge_" + std::to_string(judgeSprite);
        const platform::Renderer::HudSprite* sprite = renderer.hud(key);
        if (sprite != nullptr && sprite->height > 0) {
            const float h = 60.0f;
            const float w = h * (static_cast<float>(sprite->width) / static_cast<float>(sprite->height));
            const float alpha = since < 0.35f ? 1.0f : 1.0f - (since - 0.35f) / 0.15f;
            const float pop = since < 0.08f ? 1.18f : 1.0f;
            img(key, 960.0f - w * 0.5f * pop, 560.0f - h * 0.5f * pop, w * pop, h * pop, alpha);
        }
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
