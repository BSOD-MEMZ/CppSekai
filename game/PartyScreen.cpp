// CppSekai - the multiplayer overlays (see PartyScreen.hpp).
#include "PartyScreen.hpp"

#include "Intro.hpp" // titleFont / bodyFont

#include <algorithm>
#include <cstdio>
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

void textRight(const Canvas& c, ImFont* font, float size, float vx, float centerVy, ImU32 col,
    const std::string& text)
{
    if (font == nullptr || text.empty()) {
        return;
    }
    const float px = size * c.scale;
    c.dl->AddText(font, px,
        ImVec2(c.x(vx) - textWidth(font, px, text), textTopForCenterPx(font, px, c.y(centerVy), text)),
        col, text.c_str());
}

// Cuts `text` to whatever fits in `maxPx`, keeping whole UTF-8 code points.
std::string ellipsize(ImFont* font, float pixelSize, const std::string& text, float maxPx)
{
    if (font == nullptr || text.empty() || maxPx <= 0.0f) {
        return text;
    }
    if (textWidth(font, pixelSize, text) <= maxPx) {
        return text;
    }
    std::string out;
    const float ellipsisW = textWidth(font, pixelSize, "...");
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[cursor]);
        std::size_t len = 1;
        if ((lead & 0xE0) == 0xC0) {
            len = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            len = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            len = 4;
        }
        len = std::min(len, text.size() - cursor);
        const std::string next = out + text.substr(cursor, len);
        if (textWidth(font, pixelSize, next) + ellipsisW > maxPx) {
            break;
        }
        out = next;
        cursor += len;
    }
    return out + "...";
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
    std::string text = "多人房间 " + std::to_string(players.size()) + "/"
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

void drawPartyScores(const std::vector<platform::PartyPlayer>& players, int mySlot, int windowW,
    int windowH, const std::string& note, bool paused)
{
    if (players.size() < 2) {
        return;
    }
    Canvas c = makeCanvas(windowW, windowH, 1.0f);
    c.dl = ImGui::GetForegroundDrawList();
    ImFont* body = bodyFont() != nullptr ? bodyFont() : ImGui::GetFont();

    const float x = 24.0f;
    const float w = 430.0f;
    const float rowH = 48.0f;
    const float gap = 8.0f;
    int shown = 0;
    for (const platform::PartyPlayer& player : players) {
        if (player.slot == mySlot) {
            continue;
        }
        ++shown;
    }
    if (shown == 0) {
        return;
    }
    float y = 1080.0f - 24.0f - static_cast<float>(shown) * (rowH + gap);
    for (const platform::PartyPlayer& player : players) {
        if (player.slot == mySlot) {
            continue;
        }
        c.dl->AddRectFilled(ImVec2(c.x(x), c.y(y)), ImVec2(c.x(x + w), c.y(y + rowH)),
            IM_COL32(10, 12, 24, 170), c.s(10.0f));
        textLeft(c, body, 22.0f, x + 18.0f, y + rowH * 0.5f, kText,
            ellipsize(body, 22.0f * c.scale, player.name, c.s(190.0f)));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f", player.score);
        textRight(c, body, 24.0f, x + w - 18.0f, y + rowH * 0.5f, kText, buf);
        if (player.combo > 1) {
            std::snprintf(buf, sizeof(buf), "%d combo", player.combo);
            textRight(c, body, 18.0f, x + w - 140.0f, y + rowH * 0.5f, kAccent, buf);
        }
        y += rowH + gap;
    }
    if (!note.empty()) {
        textLeft(c, body, 20.0f, x + 6.0f, y + 18.0f, paused ? kAccent : kTextFaint, note);
    }
}

} // namespace game
