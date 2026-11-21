// CppSekai - the multiplayer overlays (see PartyScreen.hpp).
#include "PartyScreen.hpp"

#include "Intro.hpp" // titleFont / bodyFont
#include "platform/Renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace game
{
namespace
{
constexpr const char* kDifficultyNames[7] = {"EASY", "NORMAL", "HARD", "EXPERT", "MASTER", "APPEND",
    "ETERNAL"};

constexpr ImU32 kText = IM_COL32(236, 238, 248, 255);
constexpr ImU32 kTextFaint = IM_COL32(116, 122, 148, 255);
constexpr ImU32 kAccent = IM_COL32(106, 232, 208, 255);

// 1920x1080 virtual canvas -> window pixels, the same convention the song
// select and the result screen use.
struct Canvas
{
    ImDrawList* dl = nullptr;
    float scale = 1.0f;
    float offX = 0.0f;
    float offY = 0.0f;
    float x(float v) const { return offX + v * scale; }
    float y(float v) const { return offY + v * scale; }
    float s(float v) const { return v * scale; }
};

float textWidth(ImFont* font, float pixelSize, const std::string& text)
{
    if (font == nullptr || text.empty()) {
        return 0.0f;
    }
    return font->CalcTextSizeA(pixelSize, FLT_MAX, 0.0f, text.c_str()).x;
}

float textTopForCenterPx(ImFont* font, float pixelSize, float centerYpx, const std::string& text)
{
    const float boxH = font != nullptr && !text.empty()
        ? font->CalcTextSizeA(pixelSize, FLT_MAX, 0.0f, text.c_str()).y
        : pixelSize;
    return centerYpx - boxH * 0.5f - pixelSize * 0.035f;
}

void textLeft(const Canvas& c, ImFont* font, float size, float vx, float centerVy, ImU32 col,
    const std::string& text)
{
    if (font == nullptr || text.empty()) {
        return;
    }
    const float px = size * c.scale;
    c.dl->AddText(font, px, ImVec2(c.x(vx), textTopForCenterPx(font, px, c.y(centerVy), text)), col,
        text.c_str());
}

// First UTF-8 code point of `text` (the stand-in "card thumbnail" initial).
// substr(0, 1) would cut a multi-byte CJK glyph in half and the font renderer
// would draw nothing at all.
std::string firstGlyph(const std::string& text)
{
    if (text.empty()) {
        return "?";
    }
    const unsigned char lead = static_cast<unsigned char>(text[0]);
    std::size_t len = 1;
    if ((lead & 0xE0) == 0xC0) {
        len = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        len = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        len = 4;
    }
    return text.substr(0, std::min(len, text.size()));
}
Canvas makeCanvas(int windowW, int windowH, float uiScale)
{
    Canvas c;
    const float W = static_cast<float>(windowW);
    const float H = static_cast<float>(windowH);
    c.scale = std::min(W / 1920.0f, H / 1080.0f) * (uiScale > 0.0f ? uiScale : 1.0f);
    c.offX = (W - 1920.0f * c.scale) * 0.5f;
    c.offY = (H - 1080.0f * c.scale) * 0.5f;
    return c;
}

} // namespace

int difficultyIndex(const std::string& name)
{
    for (int i = 0; i < 7; ++i) {
        if (name == kDifficultyNames[i]) {
            return i;
        }
    }
    return -1;
}

const char* difficultyName(int index)
{
    if (index < 0 || index >= 7) {
        return "";
    }
    return kDifficultyNames[index];
}

void drawPartyBadge(const std::vector<platform::PartyPlayer>& players, int mySlot, int windowW,
    int windowH, float uiScale, const std::string& hint)
{
    if (players.empty()) {
        return;
    }
    Canvas c = makeCanvas(windowW, windowH, uiScale);
    c.dl = ImGui::GetForegroundDrawList();

    ImFont* body = bodyFont() != nullptr ? bodyFont() : ImGui::GetFont();
    std::string text = "多人演出 " + std::to_string(players.size()) + "/"
        + std::to_string(platform::kPartyMaxSlots) + "  ";
    for (std::size_t i = 0; i < players.size(); ++i) {
        if (i > 0) {
            text += " · ";
        }
        text += players[i].name;
        if (players[i].host) {
            text += "(房主)";
        }
        if (players[i].slot == mySlot) {
            text += "(你)";
        }
    }
    const float size = 22.0f;
    const float px = size * c.scale;
    const float w = textWidth(body, px, text);
    const float h = 44.0f;
    const float x = 24.0f;
    const float y = 1080.0f - 24.0f - h;
    c.dl->AddRectFilled(ImVec2(c.x(x), c.y(y)), ImVec2(c.x(x) + w + c.s(56.0f), c.y(y + h)),
        IM_COL32(10, 12, 24, 190), c.s(h * 0.5f));
    c.dl->AddCircleFilled(ImVec2(c.x(x + 26.0f), c.y(y + h * 0.5f)), c.s(7.0f), kAccent);
    c.dl->AddText(body, px, ImVec2(c.x(x + 44.0f), textTopForCenterPx(body, px, c.y(y + h * 0.5f), text)),
        kText, text.c_str());
    if (!hint.empty()) {
        const float hx = x + (w + c.s(56.0f)) / c.scale + 20.0f;
        textLeft(c, body, 18.0f, hx, y + h * 0.5f, kTextFaint, hint);
    }
}

void drawPartyScores(platform::Renderer& renderer, const std::vector<platform::PartyPlayer>& players,
    int mySlot, int windowW, int windowH, const std::string& note, bool paused, float chartRating)
{
    if (players.size() < 2) {
        return;
    }
    Canvas c = makeCanvas(windowW, windowH, 1.0f);
    c.dl = ImGui::GetForegroundDrawList();
    ImFont* body = bodyFont() != nullptr ? bodyFont() : ImGui::GetFont();
    ImFont* title = titleFont() != nullptr ? titleFont() : body;

    // The multi-live art is half pure-white masks (MULTI-LIVE-UI.md 9), so every
    // draw goes through here with an explicit tint - white where the file
    // already has its own colour.
    auto img = [&](const char* key, float x, float y, float w, float h, ImU32 tint,
                   float uvMaxX = 1.0f) {
        const platform::Renderer::HudSprite* sprite = renderer.hud(key);
        if (sprite == nullptr || sprite->id == 0 || w <= 0.0f || h <= 0.0f) {
            return;
        }
        c.dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(sprite->id)),
            ImVec2(c.x(x), c.y(y)), ImVec2(c.x(x + w), c.y(y + h)), ImVec2(0.0f, 0.0f),
            ImVec2(uvMaxX, 1.0f), tint);
    };

    // pjsk's four-player room colours (pink / blue / yellow / green).
    static const ImU32 kSlotColors[4] = {IM_COL32(255, 102, 153, 255), IM_COL32(64, 172, 240, 255),
        IM_COL32(255, 190, 40, 255), IM_COL32(70, 210, 160, 255)};

    // ---- 几何：原作的实测值（FrontMultiUIView, prefab 1241） --------------
    // 五个槽位横排在左上分数面板下面，`MultiThumbnailViewGroup` 挂在
    // LeftTopAnchor(-10.880, 5.0) 下，组缩放 0.8，块中心 x 从 -8.732 起、
    // 每个 +0.82 单位，五个块 y 全是 3.234。按 pixels_to_units=108 换算：
    //   起点 232 px、y 190 px、间距 88.6 px
    // （prefab 是按 19.5:9 摆的，锚点 x = ±10.88 对应 2.148 单位的左侧留白。）
    const float block = 80.0f;
    const float step = 88.6f;
    const float firstCx = 232.0f;
    const float cy = 190.0f;
    const int kSlots = 5;

    // 槽位决定 x（掉线的人位置也不动，和原作一样）。自己的那块带 YOU 标记，
    // 所以这里**不跳过自己**。
    for (const platform::PartyPlayer& player : players) {
        if (player.slot < 0 || player.slot >= kSlots) {
            continue;
        }
        const ImU32 col = kSlotColors[((player.slot % 4) + 4) % 4];
        const int cr = static_cast<int>(col & 0xFF);
        const int cg = static_cast<int>((col >> 8) & 0xFF);
        const int cb = static_cast<int>((col >> 16) & 0xFF);

        const float bx = firstCx + static_cast<float>(player.slot) * step - block * 0.5f;
        const float y = cy - block * 0.5f;

        const bool dead = player.life <= 0.001f;
        const bool dying = !dead && player.life < 0.3f;

        // Plate. Dead = washed out, dying = a red wash creeping up the block.
        c.dl->AddRectFilled(ImVec2(c.x(bx), c.y(y)), ImVec2(c.x(bx + block), c.y(y + block)),
            IM_COL32(12, 14, 24, dead ? 120 : 205), c.s(10.0f));
        c.dl->AddRectFilled(ImVec2(c.x(bx), c.y(y)), ImVec2(c.x(bx + block), c.y(y + 3.0f)),
            dead ? IM_COL32(90, 96, 112, 200) : col, c.s(10.0f));
        if (dying) {
            const float fill = std::clamp(1.0f - player.life / 0.3f, 0.0f, 1.0f);
            c.dl->AddRectFilled(ImVec2(c.x(bx), c.y(y + block * (1.0f - fill * 0.55f))),
                ImVec2(c.x(bx + block), c.y(y + block)),
                IM_COL32(230, 60, 80, static_cast<int>(70 + 60 * fill)), c.s(10.0f));
            img("multi_dying", bx + block - 8.0f, y + block * 0.5f - 14.0f, 5.0f, 33.0f,
                IM_COL32(255, 255, 255, 220));
            c.dl->AddRect(ImVec2(c.x(bx), c.y(y)), ImVec2(c.x(bx + block), c.y(y + block)),
                IM_COL32(240, 70, 90, static_cast<int>(120 + 100 * fill)), c.s(10.0f), 0, c.s(2.0f));
        }

        // Initial as a stand-in for the card thumbnail (we have no card data).
        {
            const std::string initial = firstGlyph(player.name);
            const float px = 26.0f * c.scale;
            const float tw = textWidth(title, px, initial);
            c.dl->AddText(title, px,
                ImVec2(c.x(bx + block * 0.5f) - tw * 0.5f,
                    textTopForCenterPx(title, px, c.y(y + block * 0.42f), initial)),
                dead ? IM_COL32(150, 155, 170, 200) : IM_COL32(cr, cg, cb, 235), initial.c_str());
        }

        // Rim. The official frame is a 20x20 nine-slice and ImGui has no
        // nine-slice path, so stretching it just smears a white wash over the
        // block (tried it - see MULTI-LIVE-UI.md 9). Draw the rim by hand.
        c.dl->AddRect(ImVec2(c.x(bx), c.y(y)), ImVec2(c.x(bx + block), c.y(y + block)),
            dead ? IM_COL32(120, 126, 142, 220) : IM_COL32(cr, cg, cb, 235), c.s(10.0f), 0,
            c.s(2.0f));
        if (dead) {
            img("multi_death", bx, y, block, block, IM_COL32(255, 255, 255, 210));
        }

        // Combo, bottom of the block, with the official 154px digits (purple,
        // self-coloured - no tint needed).
        if (player.combo > 1 && !dead) {
            char digits[12];
            std::snprintf(digits, sizeof(digits), "%d", player.combo);
            const int count = static_cast<int>(std::strlen(digits));
            float heights = 24.0f;
            float total = 0.0f;
            float widths[12] = {0.0f};
            for (int i = 0; i < count && i < 12; ++i) {
                char key[16];
                std::snprintf(key, sizeof(key), "multi_d%c", digits[i]);
                const platform::Renderer::HudSprite* sprite = renderer.hud(key);
                widths[i] = sprite != nullptr && sprite->height > 0
                    ? heights * static_cast<float>(sprite->width) / static_cast<float>(sprite->height)
                    : heights * 0.7f;
                total += widths[i];
            }
            const float room = block - 18.0f;
            if (total > room && total > 0.0f) {
                const float k = room / total;
                heights *= k;
                total = room;
                for (int i = 0; i < count && i < 12; ++i) {
                    widths[i] *= k;
                }
            }
            float cx = bx + (block - total) * 0.5f;
            for (int i = 0; i < count && i < 12; ++i) {
                char key[16];
                std::snprintf(key, sizeof(key), "multi_d%c", digits[i]);
                img(key, cx, y + block - heights - 4.0f, widths[i], heights,
                    IM_COL32(255, 255, 255, 255));
                cx += widths[i];
            }
        }

        // 「YOU」标签：原作放在块正下方（`YouIcon` 节点，local y=-0.693、
        // scale 1.25，换算下来比块宽略窄、贴着块底）。自己的名字/分数原作
        // **不在这块里显示**（`MultiThumbnailView` 节点树没有 Text 子节点），
        // 所以这里也不画 —— 认人靠卡面，我们暂时用首字代替卡面。
        if (player.slot == mySlot) {
            img("multi_you", bx + block * 0.5f - 30.0f, y + block + 3.0f, 60.0f, 31.0f,
                IM_COL32(255, 255, 255, 235));
        }
    }

    // 右上角只有生命条本身，**没有**额外的团队分条 —— 原作多人 HUD 的
    // 「团队合計」是左上那个 MultiScoreView（prefab 1226，位置和单人
    // ScoreView 一样在 LeftTopAnchor，只是数字换成全队合计）。
    // 2026-11-15 之前在这里画过一条 TEAM 条，是臆造的，已删。

    if (!note.empty()) {
        textLeft(c, body, 20.0f, firstCx - 40.0f, 1080.0f - 40.0f, paused ? kAccent : kTextFaint, note);
    }
}

} // namespace game
