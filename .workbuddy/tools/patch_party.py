"""One-shot patch: rewrite game/PartyScreen.cpp's drawPartyScores into the
multi-live thumbnail-block HUD. Kept as a script only because the replacement
block is long and quoting it through the shell was fragile.
"""
import io

PATH = r'D:/Dev/CppSekai/game/PartyScreen.cpp'

NEW = '''void drawPartyScores(platform::Renderer& renderer, const std::vector<platform::PartyPlayer>& players,
    int mySlot, int windowW, int windowH, const std::string& note, bool paused)
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
    const float rowH = block + 34.0f;
    const float bx = 28.0f;
    const float by = 236.0f;

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

        const float y = by + static_cast<float>(index) * (rowH + tapGap);
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
        }

        // Initial as a stand-in for the card thumbnail (we have no card data).
        {
            const std::string initial = player.name.empty() ? std::string("?") : player.name.substr(0, 1);
            const float px = 46.0f * c.scale;
            const float tw = textWidth(title, px, initial);
            c.dl->AddText(title, px,
                ImVec2(c.x(bx + block * 0.5f) - tw * 0.5f,
                    textTopForCenterPx(title, px, c.y(y + 62.0f), initial)),
                dead ? IM_COL32(150, 155, 170, 200) : IM_COL32(cr, cg, cb, 235), initial.c_str());
        }

        // Frame overlay (20x20 nine-slice, tinted with the room colour).
        img("multi_frame", bx, y, block, block,
            dead ? IM_COL32(110, 116, 132, 200) : IM_COL32(cr, cg, cb, 255));
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
    const float sLine = 1300000.0f;
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
        textLeft(c, body, 20.0f, bx + 4.0f, 1080.0f - 40.0f, paused ? kAccent : kTextFaint, note);
    }
}

'''


def main():
    s = io.open(PATH, encoding='utf-8').read()
    if '#include <cstring>' not in s:
        s = s.replace('#include <cstdio>', '#include <cstdio>\n#include <cstring>', 1)
    if 'platform/Renderer.hpp' not in s:
        s = s.replace('#include "Intro.hpp" // titleFont / bodyFont',
                      '#include "Intro.hpp" // titleFont / bodyFont\n#include "platform/Renderer.hpp"', 1)
    i = s.find('void drawPartyScores(')
    j = s.find('} // namespace game', i)
    assert i > 0 and j > i, (i, j)
    s = s[:i] + NEW + s[j:]
    io.open(PATH, 'w', encoding='utf-8').write(s)
    print('patched', PATH)


if __name__ == '__main__':
    main()
