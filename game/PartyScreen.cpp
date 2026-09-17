// CppSekai - the multiplayer room screens (see PartyScreen.hpp).
#include "PartyScreen.hpp"

#include "Intro.hpp" // titleFont / bodyFont
#include "SongSelect.hpp" // difficultyColor
#include "Ui.hpp"   // ui::se

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace game
{
namespace
{
constexpr const char* kDifficultyNames[7] = {"EASY", "NORMAL", "HARD", "EXPERT", "MASTER", "APPEND",
    "ETERNAL"};

constexpr ImU32 kPanelBg = IM_COL32(16, 18, 32, 236);
constexpr ImU32 kCardBg = IM_COL32(30, 33, 52, 255);
constexpr ImU32 kRowBg = IM_COL32(44, 48, 72, 255);
constexpr ImU32 kRowBgHost = IM_COL32(58, 64, 96, 255);
constexpr ImU32 kText = IM_COL32(236, 238, 248, 255);
constexpr ImU32 kTextDim = IM_COL32(158, 164, 190, 255);
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

void textCentered(const Canvas& c, ImFont* font, float size, float cx, float cy, ImU32 col,
    const std::string& text)
{
    if (font == nullptr || text.empty()) {
        return;
    }
    const float px = size * c.scale;
    c.dl->AddText(font, px,
        ImVec2(c.x(cx) - textWidth(font, px, text) * 0.5f,
            textTopForCenterPx(font, px, c.y(cy), text)),
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

// Capsule button on the canvas. Returns true on the frame it is clicked.
bool button(const Canvas& c, const char* id, const char* label, float vx, float vy, float vw,
    float vh, bool primary, bool enabled = true)
{
    ImGui::SetCursorScreenPos(ImVec2(c.x(vx), c.y(vy)));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(c.s(vw), c.s(vh))) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool active = enabled && ImGui::IsItemActive();

    ImU32 fill = primary ? kAccent : IM_COL32(238, 240, 250, 255);
    if (!enabled) {
        fill = IM_COL32(96, 100, 120, 255);
    } else if (active) {
        fill = primary ? IM_COL32(80, 202, 180, 255) : IM_COL32(216, 219, 232, 255);
    } else if (hovered) {
        fill = primary ? IM_COL32(126, 242, 220, 255) : IM_COL32(255, 255, 255, 255);
    }
    ImU32 textCol = primary ? IM_COL32(24, 46, 42, 255) : IM_COL32(40, 42, 58, 255);
    if (!enabled) {
        textCol = IM_COL32(198, 200, 214, 255);
    }
    c.dl->AddRectFilled(ImVec2(c.x(vx), c.y(vy)), ImVec2(c.x(vx + vw), c.y(vy + vh)), fill,
        c.s(vh * 0.5f));
    ImFont* font = bodyFont() != nullptr ? bodyFont() : ImGui::GetFont();
    textCentered(c, font, vh * 0.42f, vx + vw * 0.5f, vy + vh * 0.5f, textCol, label);
    if (clicked) {
        ui::se(ui::SeClick);
    }
    return clicked;
}

std::string seatStatus(const platform::PartyPlayer& player)
{
    switch (player.seat) {
    case platform::PartySeatReady:
        return "准备完成";
    case platform::PartySeatPlaying:
        return "游玩中";
    case platform::PartySeatResult:
        return "结算中";
    default:
        return player.difficulty >= 0 ? "已选难度" : "选择难度中";
    }
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

PartyScreenOutput drawPartyScreen(const PartyScreenInput& in)
{
    PartyScreenOutput out;
    const float W = static_cast<float>(in.windowW);
    const float H = static_cast<float>(in.windowH);
    Canvas c;
    c.scale = std::min(W / 1920.0f, H / 1080.0f) * (in.uiScale > 0.0f ? in.uiScale : 1.0f);
    c.offX = (W - 1920.0f * c.scale) * 0.5f;
    c.offY = (H - 1080.0f * c.scale) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##partyroom", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoCollapse);
    c.dl = ImGui::GetWindowDrawList();

    ImFont* title = titleFont() != nullptr ? titleFont() : bodyFont();
    ImFont* body = bodyFont() != nullptr ? bodyFont() : ImGui::GetFont();

    // Backdrop over the stage plate the caller rendered behind us.
    c.dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(W, H), IM_COL32(6, 7, 14, 170));
    c.dl->AddRectFilled(ImVec2(c.x(72.0f), c.y(72.0f)), ImVec2(c.x(1848.0f), c.y(1008.0f)), kPanelBg,
        c.s(28.0f));

    // ---- header ----------------------------------------------------------
    textLeft(c, title, 46.0f, 120.0f, 128.0f, kText, "多人游玩");
    {
        std::string room = "房间 " + std::to_string(in.players.size()) + "/"
            + std::to_string(platform::kPartyMaxSlots);
        textRight(c, body, 26.0f, 1800.0f, 126.0f, kTextDim, room);
        if (in.host) {
            textRight(c, body, 22.0f, 1800.0f, 164.0f, kAccent, "你是房主");
        } else {
            textRight(c, body, 22.0f, 1800.0f, 164.0f, kTextFaint, "等待房主开始");
        }
    }

    // ---- left: the locked song -------------------------------------------
    c.dl->AddRectFilled(ImVec2(c.x(120.0f), c.y(196.0f)), ImVec2(c.x(1010.0f), c.y(936.0f)), kCardBg,
        c.s(20.0f));
    {
        const float coverSize = 300.0f;
        const float coverX = 152.0f;
        const float coverY = 228.0f;
        if (in.cover != 0) {
            c.dl->AddImage(in.cover, ImVec2(c.x(coverX), c.y(coverY)),
                ImVec2(c.x(coverX + coverSize), c.y(coverY + coverSize)));
        } else {
            c.dl->AddRectFilled(ImVec2(c.x(coverX), c.y(coverY)),
                ImVec2(c.x(coverX + coverSize), c.y(coverY + coverSize)), kRowBg, c.s(16.0f));
            textCentered(c, body, 28.0f, coverX + coverSize * 0.5f, coverY + coverSize * 0.5f,
                kTextFaint, "无封面");
        }
        const float textX = coverX + coverSize + 40.0f;
        const float textMax = 1010.0f - 40.0f - textX;
        textLeft(c, title, 40.0f, textX, coverY + 34.0f, kText,
            ellipsize(title, 40.0f * c.scale, in.title, textMax * c.scale));
        textLeft(c, body, 24.0f, textX, coverY + 92.0f, kTextDim,
            ellipsize(body, 24.0f * c.scale, in.artist, textMax * c.scale));
        textLeft(c, body, 20.0f, textX, coverY + 132.0f, kTextFaint,
            ellipsize(body, 20.0f * c.scale, in.songKey, textMax * c.scale));
        std::string hostLine = "房主难度 ";
        hostLine += in.hostDifficultyName.empty() ? "未选择" : in.hostDifficultyName;
        textLeft(c, body, 24.0f, textX, coverY + 186.0f, kAccent, hostLine);
    }

    // ---- difficulty picker ------------------------------------------------
    textLeft(c, body, 26.0f, 152.0f, 592.0f, kText, "我的难度");
    {
        const float chipW = 156.0f;
        const float chipH = 64.0f;
        const float gap = 14.0f;
        float x = 152.0f;
        const float chipY = 634.0f;
        for (std::size_t i = 0; i < in.options.size() && i < 7; ++i) {
            const PartyDifficultyOption& option = in.options[i];
            char id[32];
            std::snprintf(id, sizeof(id), "##diff%d", option.index);
            const bool selected = option.index == in.myDifficulty;
            ImGui::SetCursorScreenPos(ImVec2(c.x(x), c.y(chipY)));
            ImGui::InvisibleButton(id, ImVec2(c.s(chipW), c.s(chipH)));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                out.difficulty = option.index;
                ui::se(ui::SeLevelChoose);
            }
            ImU32 fill = difficultyColor(option.name, selected ? 255 : (hovered ? 170 : 110));
            c.dl->AddRectFilled(ImVec2(c.x(x), c.y(chipY)), ImVec2(c.x(x + chipW), c.y(chipY + chipH)),
                fill, c.s(14.0f));
            if (selected) {
                c.dl->AddRect(ImVec2(c.x(x) - 2.0f, c.y(chipY) - 2.0f),
                    ImVec2(c.x(x + chipW) + 2.0f, c.y(chipY + chipH) + 2.0f), kText, c.s(16.0f), 0,
                    c.s(3.0f));
            }
            textCentered(c, body, 26.0f, x + chipW * 0.5f, chipY + chipH * 0.5f, kText, option.name);
            const std::string level = option.level > 0 ? std::to_string(option.level) : "-";
            textCentered(c, body, 18.0f, x + chipW * 0.5f, chipY + chipH + 22.0f, kTextDim, level);
            if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
                std::printf("[party-ui] chip %s (%d) center %.0f,%.0f\n", option.name.c_str(),
                    option.index, c.x(x + chipW * 0.5f), c.y(chipY + chipH * 0.5f));
            }
            x += chipW + gap;
        }
        if (in.options.empty()) {
            textLeft(c, body, 22.0f, 152.0f, 660.0f, kTextDim, "这首曲子在本窗口没有可用难度");
        }
    }

    // ---- buttons ----------------------------------------------------------
    const float btnY = 762.0f;
    const float btnH = 72.0f;
    float btnX = 152.0f;
    if (in.host) {
        if (button(c, "##start", "开始", btnX, btnY, 260.0f, btnH, true)) {
            out.startNow = true;
        }
        if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
            std::printf("[party-ui] start button center %.0f,%.0f\n", c.x(btnX + 130.0f),
                c.y(btnY + btnH * 0.5f));
        }
        btnX += 284.0f;
    } else {
        if (button(c, "##ready", in.ready ? "取消准备" : "准备", btnX, btnY, 260.0f, btnH, !in.ready)) {
            out.readyToggle = true;
        }
        if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
            std::printf("[party-ui] ready button center %.0f,%.0f\n", c.x(btnX + 130.0f),
                c.y(btnY + btnH * 0.5f));
        }
        btnX += 284.0f;
    }
    if (button(c, "##leave", "返回选曲", btnX, btnY, 220.0f, btnH, false)) {
        out.leave = true;
    }
    if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
        std::printf("[party-ui] leave button center %.0f,%.0f\n", c.x(btnX + 110.0f),
            c.y(btnY + btnH * 0.5f));
        std::fflush(stdout);
    }

    if (!in.status.empty()) {
        textLeft(c, body, 24.0f, 152.0f, 880.0f, kTextDim, in.status);
    }

    // ---- right: the player list -------------------------------------------
    // (listX/listW live outside the block: the charge countdown is drawn into
    // the same column further down.)
    const float listX = 1050.0f;
    const float listW = 750.0f;
    {
        textLeft(c, body, 26.0f, listX + 6.0f, 216.0f, kText, "玩家");
        c.dl->AddRectFilled(ImVec2(c.x(listX), c.y(248.0f)), ImVec2(c.x(listX + listW), c.y(936.0f)),
            kCardBg, c.s(20.0f));
        const float rowH = 72.0f;
        const float rowGap = 12.0f;
        float rowY = 276.0f;
        for (const platform::PartyPlayer& player : in.players) {
            const bool me = player.slot == in.mySlot;
            c.dl->AddRectFilled(ImVec2(c.x(listX + 20.0f), c.y(rowY)),
                ImVec2(c.x(listX + listW - 20.0f), c.y(rowY + rowH)),
                player.host ? kRowBgHost : kRowBg, c.s(14.0f));
            if (me) {
                c.dl->AddRect(ImVec2(c.x(listX + 20.0f), c.y(rowY)),
                    ImVec2(c.x(listX + listW - 20.0f), c.y(rowY + rowH)), kAccent, c.s(14.0f), 0,
                    c.s(2.0f));
            }
            const float cy = rowY + rowH * 0.5f;
            std::string name = player.name;
            if (me) {
                name += " (你)";
            }
            textLeft(c, body, 24.0f, listX + 40.0f, cy, kText,
                ellipsize(body, 24.0f * c.scale, name, c.s(260.0f)));
            if (player.host) {
                textLeft(c, body, 18.0f, listX + 300.0f, cy, kAccent, "房主");
            }
            const char* diffName = difficultyName(player.difficulty);
            if (player.difficulty >= 0) {
                c.dl->AddRectFilled(ImVec2(c.x(listX + 372.0f), c.y(cy - 20.0f)),
                    ImVec2(c.x(listX + 372.0f + 176.0f), c.y(cy + 20.0f)),
                    difficultyColor(diffName, 210), c.s(10.0f));
                textCentered(c, body, 20.0f, listX + 372.0f + 88.0f, cy, kText, diffName);
            } else {
                c.dl->AddRectFilled(ImVec2(c.x(listX + 372.0f), c.y(cy - 20.0f)),
                    ImVec2(c.x(listX + 372.0f + 176.0f), c.y(cy + 20.0f)), IM_COL32(70, 74, 98, 255),
                    c.s(10.0f));
                textCentered(c, body, 18.0f, listX + 372.0f + 88.0f, cy, kTextFaint, "未选择");
            }
            const std::string status = seatStatus(player);
            const ImU32 statusCol = player.ready ? kAccent : kTextFaint;
            textRight(c, body, 20.0f, listX + listW - 44.0f, cy, statusCol, status);
            rowY += rowH + rowGap;
            if (rowY + rowH > 930.0f) {
                break;
            }
        }
    }

    // ---- charge countdown --------------------------------------------------
    if (in.phase == platform::PartyCharging) {
        const float remain = std::max(0.0f, in.countdownSec);
        char text[16];
        std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::ceil(remain)));
        c.dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(W, H), IM_COL32(6, 7, 14, 190));
        // Centred on the *player list* column: the left card is full of live
        // widgets (the difficulty chips sit right where a screen-centred number
        // would land), while the list is mostly empty space below its rows.
        textCentered(c, title, 140.0f, listX + listW * 0.5f, 560.0f, kText, text);
        textCentered(c, body, 32.0f, listX + listW * 0.5f, 700.0f, kTextDim, "即将开始");
    }

    ImGui::End();
    return out;
}

void drawPartyBadge(const std::vector<platform::PartyPlayer>& players, int mySlot, int windowW,
    int windowH, float uiScale, const std::string& hint)
{
    if (players.empty()) {
        return;
    }
    Canvas c;
    const float W = static_cast<float>(windowW);
    const float H = static_cast<float>(windowH);
    c.scale = std::min(W / 1920.0f, H / 1080.0f) * (uiScale > 0.0f ? uiScale : 1.0f);
    c.offX = (W - 1920.0f * c.scale) * 0.5f;
    c.offY = (H - 1080.0f * c.scale) * 0.5f;
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
    Canvas c;
    const float W = static_cast<float>(windowW);
    const float H = static_cast<float>(windowH);
    c.scale = std::min(W / 1920.0f, H / 1080.0f);
    c.offX = (W - 1920.0f * c.scale) * 0.5f;
    c.offY = (H - 1080.0f * c.scale) * 0.5f;
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
