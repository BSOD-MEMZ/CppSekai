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

void drawHud(platform::Renderer& renderer, const HudState& state, float songTimeSec, int windowW, int windowH, bool dumpJudgeSheet)
{
    ImDrawList* drawList = ImGui::GetForegroundDrawList();

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
    // Life panel (top-right)
    // ------------------------------------------------------------------
    img("life_bg", 1442.0f, 11.0f, 444.0f, 104.0f);
    img("life_fill", 1442.0f, 11.0f, 444.0f, 104.0f, 1.0f, std::clamp(state.lifeRatio, 0.0f, 1.0f));
    {
        const int lifeValue = std::max(0, static_cast<int>(std::lround(1000.0f * std::clamp(state.lifeRatio, 0.0f, 1.0f))));
        const std::string lifeText = std::to_string(lifeValue);
        for (size_t i = 0; i < lifeText.size(); ++i) {
            const std::string key(1, lifeText[lifeText.size() - 1 - i]);
            const float slotX = 1442.0f + 319.0f - static_cast<float>(i) * 22.0f;
            const float slotY = 11.0f + 10.0f;
            const platform::Renderer::HudSprite* shadow = renderer.hud("life_digit_s" + key);
            const platform::Renderer::HudSprite* main = renderer.hud("life_digit_" + key);
            const float shadowH = ps(37.0f);
            const float mainH = ps(34.0f);
            if (shadow != nullptr && shadow->height > 0) {
                const float w = shadowH * (static_cast<float>(shadow->width) / static_cast<float>(shadow->height));
                img("life_digit_s" + key, slotX - w * 0.5f / scale, slotY - 2.0f, w / scale, shadowH / scale);
            }
            if (main != nullptr && main->height > 0) {
                const float w = mainH * (static_cast<float>(main->width) / static_cast<float>(main->height));
                img("life_digit_" + key, slotX - w * 0.5f / scale, slotY, w / scale, mainH / scale);
            }
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
    // Judge text (PERFECT / GREAT / GOOD) - only on a successful hit.
    // The official overlay ships one combined "PERFECT GREAT GOOD BAD"
    // sprite as judge/v3/1.png and a separate "MISS" as judge/v3/2.png.
    // We use them that way instead of trying to pick per-grade sprites.
    // ------------------------------------------------------------------
    const float since = songTimeSec - state.lastJudgeAtSec;
    if (state.lastJudge != game::Judge::Miss && state.lastJudge != game::Judge::None && since >= 0.0f && since < 0.5f) {
        const char* key = "judge_1";
        const platform::Renderer::HudSprite* sprite = renderer.hud(key);
        if (sprite != nullptr && sprite->height > 0) {
            const float h = 60.0f;
            const float w = h * (static_cast<float>(sprite->width) / static_cast<float>(sprite->height));
            const float alpha = since < 0.35f ? 1.0f : 1.0f - (since - 0.35f) / 0.15f;
            const float pop = since < 0.08f ? 1.18f : 1.0f;
            img(key, 960.0f - w * 0.5f * pop, 560.0f - h * 0.5f * pop, w * pop, h * pop, alpha);
        }
    } else if (state.lastJudge == game::Judge::Miss && since >= 0.0f && since < 0.5f) {
        const platform::Renderer::HudSprite* sprite = renderer.hud("judge_2");
        if (sprite != nullptr && sprite->height > 0) {
            const float h = 60.0f;
            const float w = h * (static_cast<float>(sprite->width) / static_cast<float>(sprite->height));
            img("judge_2", 960.0f - w * 0.5f, 560.0f - h * 0.5f, w, h);
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
