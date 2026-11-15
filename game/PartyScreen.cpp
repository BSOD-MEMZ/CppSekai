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

    const float block = 156.0f;
    const float tapGap = 12.0f;
    // 接在左上分数面板下面**横着**排一排（和原作一样），不是靠左缘竖排 ——
    // 之前那版是我照节点名猜的，prefab 里 HUD 是 Transform + 世界坐标、
    // 没有 RectTransform，位置量不出来，只能按观感摆。
    const float baseX = 36.0f;
    const float baseY = 208.0f;

    // ---- 每个人的头像块（左缘竖排，避开左上的分数面板） -------------------
    int index = 0;
    for (const platform::PartyPlayer& player : players) {
        if (player.slot == mySlot) {
            continue;
        }
        const ImU32 col = kSlotColors[((player.slot % 4) + 4) % 4];
        const int cr = static_cast<int>(col & 0xFF);
        const int cg = static_cast<int>((col >> 8) & 0xFF);
        const int cb = static_cast<int>((col >> 16) & 0xFF);

        const float bx = baseX + static_cast<float>(index) * (block + tapGap);
        const float y = baseY;
        ++index;

        const bool dead = player.life <= 0.001f;
        const bool dying = !dead && player.life < 0.3f;

        // Plate. Dead = washed out, dying = a red wash creeping up the block.
        c.dl->AddRectFilled(ImVec2(c.x(bx), c.y(y)), ImVec2(c.x(bx + block), c.y(y + block)),
            IM_COL32(12, 14, 24, dead ? 120 : 205), c.s(16.0f));
        c.dl->AddRectFilled(ImVec2(c.x(bx), c.y(y)), ImVec2(c.x(bx + block), c.y(y + 5.0f)),
            dead ? IM_COL32(90, 96, 112, 200) : col, c.s(16.0f));
        if (dying) {
            const float fill = std::clamp(1.0f - player.life / 0.3f, 0.0f, 1.0f);
            c.dl->AddRectFilled(ImVec2(c.x(bx), c.y(y + block * (1.0f - fill * 0.55f))),
                ImVec2(c.x(bx + block), c.y(y + block)),
                IM_COL32(230, 60, 80, static_cast<int>(70 + 60 * fill)), c.s(16.0f));
            img("multi_dying", bx + block - 16.0f, y + block * 0.5f - 26.0f, 10.0f, 64.0f,
                IM_COL32(255, 255, 255, 220));
            c.dl->AddRect(ImVec2(c.x(bx), c.y(y)), ImVec2(c.x(bx + block), c.y(y + block)),
                IM_COL32(240, 70, 90, static_cast<int>(120 + 100 * fill)), c.s(16.0f), 0, c.s(3.0f));
        }

        // Initial as a stand-in for the card thumbnail (we have no card data).
        {
            const std::string initial = firstGlyph(player.name);
            const float px = 46.0f * c.scale;
            const float tw = textWidth(title, px, initial);
            c.dl->AddText(title, px,
                ImVec2(c.x(bx + block * 0.5f) - tw * 0.5f,
                    textTopForCenterPx(title, px, c.y(y + 62.0f), initial)),
                dead ? IM_COL32(150, 155, 170, 200) : IM_COL32(cr, cg, cb, 235), initial.c_str());
        }

        // Rim. The official frame is a 20x20 nine-slice and ImGui has no
        // nine-slice path, so stretching it just smears a white wash over the
        // block (tried it - see MULTI-LIVE-UI.md 9). Draw the rim by hand.
        c.dl->AddRect(ImVec2(c.x(bx), c.y(y)), ImVec2(c.x(bx + block), c.y(y + block)),
            dead ? IM_COL32(120, 126, 142, 220) : IM_COL32(cr, cg, cb, 235), c.s(16.0f), 0,
            c.s(3.0f));
        if (dead) {
            img("multi_death", bx, y, block, block, IM_COL32(255, 255, 255, 210));
        }

        // Combo, bottom of the block, with the official 154px digits (purple,
        // self-coloured - no tint needed).
        if (player.combo > 1 && !dead) {
            char digits[12];
            std::snprintf(digits, sizeof(digits), "%d", player.combo);
            const int count = static_cast<int>(std::strlen(digits));
            float heights = 42.0f;
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
                img(key, cx, y + block - heights - 10.0f, widths[i], heights,
                    IM_COL32(255, 255, 255, 255));
                cx += widths[i];
            }
        }

        // Name (ellipsized) + score under the block.
        const float nameY = y + block + 4.0f;
        textLeft(c, body, 20.0f, bx, nameY + 10.0f, dead ? IM_COL32(140, 146, 162, 220) : kText,
            ellipsize(body, 20.0f * c.scale, player.name, c.s(96.0f)));
        char scoreText[32];
        std::snprintf(scoreText, sizeof(scoreText), "%.0f", player.score);
        textRight(c, body, 21.0f, bx + block, nameY + 10.0f,
            dead ? IM_COL32(140, 146, 162, 220) : IM_COL32(cr, cg, cb, 240), scoreText);
    }

    // ---- 团队合計分条（生命条正下方） --------------------------------------
    // 刻度按 master playLevelScores 的比例（C 1.9% / B 38.5% / A 80.8% / S 100%，
    // 见 MV2D-SCORE.md 2.7），分子是全队分数合计。
    double totalScore = 0.0;
    for (const platform::PartyPlayer& player : players) {
        totalScore += player.score;
    }
    const float gx = 1442.0f;
    const float gy = 126.0f;
    const float gw = 444.0f;
    const float gh = 20.0f;
    // S 线按谱面等级走：S = 1040000 × levelFactor（MV2D-SCORE.md 2.7），
    // 这样 5 级和 40 级的条不会一个打满一个永远空。
    const float levelFactor = 1.0f + std::max(0.0f, chartRating - 5.0f) * 0.005f;
    const float sLine = 1040000.0f * levelFactor;
    const float ratio = static_cast<float>(std::clamp(totalScore / sLine, 0.0, 1.0));

    c.dl->AddRectFilled(ImVec2(c.x(gx), c.y(gy)), ImVec2(c.x(gx + gw), c.y(gy + gh)),
        IM_COL32(12, 14, 24, 200), c.s(gh * 0.5f));
    img("multi_gauge_bg", gx, gy, gw, gh, IM_COL32(255, 255, 255, 150));
    if (ratio > 0.0f) {
        img("multi_gauge_fill", gx, gy, gw * ratio, gh, IM_COL32(120, 240, 220, 255), ratio);
    }
    static const float kMarks[4] = {0.0192f, 0.3846f, 0.8077f, 1.0f};
    static const char* kMarkNames[4] = {"C", "B", "A", "S"};
    for (int i = 0; i < 4; ++i) {
        const float mx = gx + gw * kMarks[i];
        c.dl->AddLine(ImVec2(c.x(mx), c.y(gy + gh * 0.5f) - c.s(6.0f)),
            ImVec2(c.x(mx), c.y(gy + gh * 0.5f) + c.s(6.0f)),
            i == 3 ? IM_COL32(255, 230, 120, 220) : IM_COL32(200, 214, 236, 170), c.s(1.5f));
        textLeft(c, body, 13.0f, mx + 3.0f, gy + gh + 9.0f, IM_COL32(170, 180, 200, 200),
            kMarkNames[i]);
    }
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "TEAM %.0f", totalScore);
        textRight(c, body, 18.0f, gx + gw, gy - 10.0f, IM_COL32(220, 232, 246, 230), buf);
    }

    if (!note.empty()) {
        textLeft(c, body, 20.0f, baseX + 4.0f, 1080.0f - 40.0f, paused ? kAccent : kTextFaint, note);
    }
}

} // namespace game
