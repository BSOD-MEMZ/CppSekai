// CppSekai - story UI drawing (see StoryUi.hpp / STORY-UI.md).
#include "StoryUi.hpp"

#include "Ui.hpp"
#include "Intro.hpp"
#include "platform/Renderer.hpp"
#include "../third_party/mmw_preview/vendor/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace storyui
{
namespace
{
    // The three frames, in window-pixel textures. 0 = not loaded.
    ImTextureID gDialogue = 0;
    ImTextureID gChoices = 0;
    ImTextureID gSceneText = 0;

    // Measured off Dialogue_Background_Classic.png (see STORY-UI.md):
    // the box, the baked-in name plate, and where text starts inside it.
    constexpr float kBoxL = 154.0f, kBoxT = 734.0f, kBoxR = 1766.0f, kBoxB = 1053.0f;
    // The name plate's right edge (888) is not used: the label is drawn
    // left-aligned and long names shrink rather than overflow.
    constexpr float kNameL = 220.0f, kNameT = 742.0f, kNameB = 814.0f;
    constexpr float kTextL = 206.0f, kTextT = 838.0f;
    constexpr float kTextSize = 34.0f;
    constexpr float kLineGap = 1.35f;

    bool isNoLineStart(unsigned int cp)
    {
        switch (cp)
        {
            case 0x3001: // 、
            case 0x3002: // 。
            case 0xFF0C: // ，
            case 0xFF0E: // ．
            case 0xFF01: // ！
            case 0xFF1F: // ？
            case 0xFF1A: // ：
            case 0xFF1B: // ；
            case 0x300D: // 」
            case 0x300F: // 』
            case 0x3011: // 】
            case 0x3015: // 〕
            case 0xFF09: // ）
            case 0x2026: // …
                return true;
            default:
                return false;
        }
    }

    unsigned int decodeUtf8(const char* s)
    {
        const auto* p = reinterpret_cast<const unsigned char*>(s);
        if (p[0] < 0x80)
        {
            return p[0];
        }
        if ((p[0] & 0xE0) == 0xC0)
        {
            return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        }
        if ((p[0] & 0xF0) == 0xE0)
        {
            return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        }
        return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6)
            | (p[3] & 0x3Fu);
    }

    int utf8Len(unsigned char lead)
    {
        if (lead < 0x80)
        {
            return 1;
        }
        if ((lead & 0xE0) == 0xC0)
        {
            return 2;
        }
        if ((lead & 0xF0) == 0xE0)
        {
            return 3;
        }
        if ((lead & 0xF8) == 0xF0)
        {
            return 4;
        }
        return 1;
    }

    void textAt(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 col,
        const std::string& s, std::size_t begin, std::size_t end)
    {
        if (end <= begin || dl == nullptr || font == nullptr)
        {
            return;
        }
        dl->AddText(font, size, pos, col, s.data() + begin, s.data() + end);
    }

    void shadowedText(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 col,
        const char* s)
    {
        dl->AddText(font, size, ImVec2(pos.x + 1.5f, pos.y + 1.5f), IM_COL32(0, 0, 0, 110), s);
        dl->AddText(font, size, pos, col, s);
    }

    // Cover-crop UVs: the picture fills `box` without stretching, the excess is
    // cropped evenly on both sides (same rule CSS object-fit: cover uses).
    void coverUv(const Image& img, float boxW, float boxH, ImVec2* uv0, ImVec2* uv1)
    {
        const float srcAspect = static_cast<float>(img.w) / static_cast<float>(img.h);
        const float boxAspect = boxW / boxH;
        if (srcAspect > boxAspect)
        {
            const float keep = boxAspect / srcAspect;
            *uv0 = ImVec2((1.0f - keep) * 0.5f, 0.0f);
            *uv1 = ImVec2(1.0f - (1.0f - keep) * 0.5f, 1.0f);
        }
        else
        {
            const float keep = srcAspect / boxAspect;
            *uv0 = ImVec2(0.0f, (1.0f - keep) * 0.5f);
            *uv1 = ImVec2(1.0f, 1.0f - (1.0f - keep) * 0.5f);
        }
    }

} // namespace

Canvas makeCanvas(float windowW, float windowH)
{
    Canvas c;
    c.scale = std::min(windowW / 1920.0f, windowH / 1080.0f);
    c.ox = (windowW - 1920.0f * c.scale) * 0.5f;
    c.oy = (windowH - 1080.0f * c.scale) * 0.5f;
    return c;
}

bool loadTextures(platform::Renderer& renderer, const std::string& assetDir, std::string& err)
{
    const std::string base = assetDir + "\\story\\ui\\";
    std::string localError;
    const GLuint dialogue = renderer.loadUiTexture(base + "Dialogue_Background_Classic.png", localError);
    const GLuint choices = renderer.loadUiTexture(base + "Choices_Background.png", localError);
    const GLuint sceneText = renderer.loadUiTexture(base + "SceneText_Background.png", localError);
    std::printf("[story] textures dialogue=%u choices=%u scene=%u\n", dialogue, choices, sceneText);
    std::fflush(stdout);
    gDialogue = reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(dialogue));
    gChoices = reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(choices));
    gSceneText = reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(sceneText));
    if (gDialogue == 0 && gChoices == 0 && gSceneText == 0)
    {
        err = "story ui textures missing under " + base;
        return false;
    }
    return true;
}

Image loadImage(platform::Renderer& renderer, const std::string& path)
{
    Image img;
    std::string err;
    const GLuint id = renderer.loadUiTexture(path, err);
    if (id == 0)
    {
        std::printf("[story] image missing: %s\n", path.c_str());
        return img;
    }
    img.tex = reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(id));
    // loadUiTexture reports no size; the story art we ship is 16:9 or 4:3 and
    // the cover-crop only needs the ratio, but a wrong ratio would stretch the
    // picture. Read the size with the same decoder the loader used.
    if (FILE* f = std::fopen(path.c_str(), "rb"); f != nullptr)
    {
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<unsigned char> bytes(static_cast<std::size_t>(std::max(size, 0L)));
        if (!bytes.empty())
        {
            std::fread(bytes.data(), 1, bytes.size(), f);
            int w = 0;
            int h = 0;
            int comp = 0;
            if (stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp) != 0)
            {
                img.w = w;
                img.h = h;
            }
        }
        std::fclose(f);
    }
    return img;
}

TextLayout layoutText(ImFont* font, float size, const std::string& text, float width)
{
    TextLayout out;
    if (font == nullptr)
    {
        return out;
    }
    for (std::size_t i = 0; i < text.size();)
    {
        out.codepoints.push_back(i);
        i += static_cast<std::size_t>(utf8Len(static_cast<unsigned char>(text[i])));
    }
    out.codepoints.push_back(text.size());

    std::size_t pos = 0;
    while (pos <= text.size())
    {
        if (pos == text.size())
        {
            break;
        }
        if (text[pos] == '\n')
        {
            out.lines.push_back(Line{pos, pos, 0.0f});
            ++pos;
            continue;
        }
        std::size_t paraEnd = text.find('\n', pos);
        if (paraEnd == std::string::npos)
        {
            paraEnd = text.size();
        }
        while (pos < paraEnd)
        {
            const char* s = text.data() + pos;
            const char* e = text.data() + paraEnd;
            const char* wrap = font->CalcWordWrapPosition(size, s, e, width);
            if (wrap <= s)
            {
                wrap = s + utf8Len(static_cast<unsigned char>(*s));
            }
            // Line-start ban: if the character that would start the next line
            // is punctuation, pull one character back onto this one.
            if (wrap < e && isNoLineStart(decodeUtf8(wrap)) && wrap > s)
            {
                const char* back = wrap - 1;
                while (back > s && (static_cast<unsigned char>(*back) & 0xC0) == 0x80)
                {
                    --back;
                }
                if (back > s)
                {
                    wrap = back;
                }
            }
            const std::size_t end = static_cast<std::size_t>(wrap - text.data());
            out.lines.push_back(Line{pos, end, font->CalcTextSizeA(size, FLT_MAX, 0.0f, s, wrap).x});
            pos = end;
            // A wrapped line never starts with the spaces the break consumed.
            while (pos < paraEnd && text[pos] == ' ')
            {
                ++pos;
            }
        }
        if (pos < text.size() && text[pos] == '\n')
        {
            ++pos;
        }
    }
    return out;
}

void dim(ImDrawList* dl, float alpha)
{
    if (dl == nullptr)
    {
        return;
    }
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize,
        IM_COL32(0, 0, 0, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)));
}

void dialogue(const Canvas& c, ImFont* font, const std::string& name, const std::string& body,
    const TextLayout& layout, std::size_t revealBytes, float time, bool showArrow, bool compact,
    bool left)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (dl == nullptr)
    {
        return;
    }
    const float s = c.scale;
    if (compact)
    {
        // Right: clear of the settings card (which ends at x 938).
        // Left: clear of the song select's phone panel (which starts at ~780).
        const float boxL = left ? 142.0f : 952.0f;
        const float boxR = left ? 930.0f : 1790.0f;
        const ImVec2 lo = c.p(boxL, 772.0f);
        const ImVec2 hi = c.p(boxR, 1054.0f);
        dl->AddRectFilled(ImVec2(lo.x + 4.0f * s, lo.y + 8.0f * s), ImVec2(hi.x + 4.0f * s, hi.y + 8.0f * s),
            IM_COL32(0, 0, 0, 55), c.s(22.0f));
        dl->AddRectFilled(lo, hi, IM_COL32(235, 235, 242, 248), c.s(22.0f));
        dl->AddRect(lo, hi, IM_COL32(196, 196, 214, 255), c.s(22.0f), 0, c.s(2.0f));
        if (!name.empty() && font != nullptr)
        {
            const float nameSize = c.s(25.0f);
            const ImVec2 ts = font->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, name.c_str());
            const float nameX = left ? 156.0f : 966.0f;
            const ImVec2 plo = c.p(nameX, 784.0f);
            const ImVec2 phi = c.p(nameX + 190.0f, 784.0f + 40.0f);
            dl->AddRectFilled(plo, phi, IM_COL32(90, 90, 120, 255), c.s(20.0f));
            dl->AddText(font, nameSize,
                ImVec2(plo.x + 22.0f * s, (plo.y + phi.y - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255),
                name.c_str());
        }
        const float size = c.s(27.0f);
        const float lineH = size * 1.36f;
        float y = c.y(846.0f);
        for (const Line& line : layout.lines)
        {
            if (line.begin >= revealBytes)
            {
                break;
            }
            textAt(dl, font, size, ImVec2(c.x(left ? 166.0f : 978.0f), y), ui::kText, body,
                line.begin, std::min(line.end, revealBytes));
            y += lineH;
        }
        if (showArrow)
        {
            const float bob = std::sin(time * 4.2f) * 3.0f * s;
            const float ax = left ? 884.0f : 1730.0f;
            dl->AddTriangleFilled(c.p(ax, 1010.0f + bob), c.p(ax + 22.0f, 1010.0f + bob),
                c.p(ax + 11.0f, 1026.0f + bob), IM_COL32(119, 238, 221, 255));
        }
        return;
    }
    const ImVec2 boxLo = c.p(kBoxL, kBoxT);
    const ImVec2 boxHi = c.p(kBoxR, kBoxB);

    // The asset's fill is translucent white; over a dark song-select it eats
    // the text. Lay the card colour down first, then let the frame's border,
    // texture and shadow sit on top of it (see STORY-UI.md section 3.1).
    dl->AddRectFilled(boxLo, boxHi, IM_COL32(235, 235, 242, 244), c.s(46.0f));
    if (gDialogue != 0)
    {
        dl->AddImage(gDialogue, c.p(0.0f, 0.0f), c.p(1920.0f, 1080.0f));
    }
    else
    {
        dl->AddRect(boxLo, boxHi, IM_COL32(190, 190, 210, 255), c.s(46.0f), 0, c.s(2.0f));
    }

    if (!name.empty() && font != nullptr)
    {
        const float nameSize = c.s(31.0f);
        const ImVec2 ts = font->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, name.c_str());
        const float cy = c.y((kNameT + kNameB) * 0.5f);
        dl->AddText(font, nameSize, ImVec2(c.x(kNameL + 52.0f), cy - ts.y * 0.5f),
            IM_COL32(255, 255, 255, 255), name.c_str());
    }

    const float size = c.s(kTextSize);
    const float lineH = size * kLineGap;
    float y = c.y(kTextT);
    for (const Line& line : layout.lines)
    {
        if (line.begin >= revealBytes)
        {
            break;
        }
        const std::size_t end = std::min(line.end, revealBytes);
        textAt(dl, font, size, ImVec2(c.x(kTextL), y), ui::kText, body, line.begin, end);
        y += lineH;
    }

    if (showArrow)
    {
        const float bob = std::sin(time * 4.2f) * 3.0f * s;
        const ImVec2 t(1701.0f, 1014.0f + bob);
        const float r = c.s(9.0f);
        const ImU32 col = IM_COL32(119, 238, 221, 255);
        dl->AddTriangleFilled(c.p(t.x - r, t.y - r * 0.6f), c.p(t.x + r, t.y - r * 0.6f),
            c.p(t.x, t.y + r * 0.9f), col);
        dl->AddCircleFilled(c.p(t.x - r * 1.9f, t.y - r * 0.1f), c.s(3.2f), col, 12);
    }
}

void sceneTitle(const Canvas& c, ImFont* font, const std::string& text, float alpha)
{
    if (alpha <= 0.001f)
    {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const int a = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    if (gSceneText != 0)
    {
        dl->AddImage(gSceneText, c.p(0.0f, 0.0f), c.p(1920.0f, 1080.0f), ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, a));
    }
    if (font == nullptr)
    {
        return;
    }
    const float size = c.s(31.0f);
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (true)
    {
        const std::size_t nl = text.find('\n', begin);
        lines.push_back(text.substr(begin, nl == std::string::npos ? std::string::npos : nl - begin));
        if (nl == std::string::npos)
        {
            break;
        }
        begin = nl + 1;
    }
    const float lineH = size * 1.5f;
    float y = c.y(540.0f) - static_cast<float>(lines.size()) * lineH * 0.5f;
    for (const std::string& line : lines)
    {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, line.c_str());
        shadowedText(dl, font, size, ImVec2(c.x(960.0f) - ts.x * 0.5f, y),
            IM_COL32(255, 255, 255, a), line.c_str());
        y += lineH;
    }
}

int choices(const Canvas& c, ImFont* font, const std::vector<std::string>& options, float time,
    int focus)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (options.empty() || dl == nullptr)
    {
        return -1;
    }
    int clicked = -1;

    // Two options: the shipped frame is exactly two pills (239..760 and
    // 1159..1680 at y 489..586), so it can be drawn whole. Any other count is
    // laid out by hand in the same style rather than stretching the art.
    if (options.size() == 2 && gChoices != 0)
    {
        dl->AddImage(gChoices, c.p(0.0f, 0.0f), c.p(1920.0f, 1080.0f));
    }
    const float pillW = 522.0f;
    const float pillH = 98.0f;
    for (int i = 0; i < static_cast<int>(options.size()); ++i)
    {
        float cx = 499.5f;
        float cy = 537.5f;
        if (options.size() == 2)
        {
            cx = i == 0 ? 499.5f : 1419.5f;
        }
        else
        {
            cy = 460.0f + static_cast<float>(i) * 116.0f;
        }
        ImVec2 lo = c.p(cx - pillW * 0.5f, cy - pillH * 0.5f);
        ImVec2 hi = c.p(cx + pillW * 0.5f, cy + pillH * 0.5f);
        if (options.size() != 2 || gChoices == 0)
        {
            dl->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 246), c.s(pillH * 0.5f));
            dl->AddRect(lo, hi, IM_COL32(185, 185, 207, 255), c.s(pillH * 0.5f), 0, c.s(2.0f));
        }
        ImGui::SetCursorScreenPos(lo);
        ImGui::PushID(i);
        ImGui::InvisibleButton("choice", ImVec2(hi.x - lo.x, hi.y - lo.y));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool hit = ImGui::IsItemClicked();
        ImGui::PopID();
        if (hovered)
        {
            dl->AddRectFilled(lo, hi,
                held ? IM_COL32(119, 238, 221, 110) : IM_COL32(119, 238, 221, 46), c.s(pillH * 0.5f));
            dl->AddRect(lo, hi, IM_COL32(119, 238, 221, 255), c.s(pillH * 0.5f), 0, c.s(2.5f));
        }
        if (i == focus)
        {
            // Gamepad focus ring: the pad has no pointer, so the option it is
            // on has to be visible without hover.
            dl->AddRect(ImVec2(lo.x - 5.0f, lo.y - 5.0f), ImVec2(hi.x + 5.0f, hi.y + 5.0f),
                IM_COL32(119, 238, 221, 255), c.s(pillH * 0.5f + 4.0f), 0, c.s(3.0f));
        }
        if (font != nullptr)
        {
            const float size = c.s(30.0f);
            const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, options[static_cast<std::size_t>(i)].c_str());
            dl->AddText(font, size, ImVec2(lo.x + (hi.x - lo.x - ts.x) * 0.5f,
                            lo.y + (hi.y - lo.y - ts.y) * 0.5f),
                ui::kText, options[static_cast<std::size_t>(i)].c_str());
        }
        if (hit)
        {
            clicked = i;
        }
    }
    (void)time;
    return clicked;
}

int cards(const Canvas& c, ImFont* font, const std::vector<Image>& images,
    const std::vector<std::string>& titles, const std::vector<std::string>& subtitles, float time,
    int focus)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (titles.empty() || dl == nullptr)
    {
        return -1;
    }
    const float s = c.scale;
    const int count = static_cast<int>(titles.size());
    const float cardW = count <= 2 ? 460.0f : 360.0f;
    const float gap = 60.0f;
    const float total = static_cast<float>(count) * cardW + static_cast<float>(count - 1) * gap;
    const float x0 = 960.0f - total * 0.5f;
    const float top = 132.0f;
    const float cardH = 556.0f;
    int clicked = -1;

    for (int i = 0; i < count; ++i)
    {
        const float left = x0 + static_cast<float>(i) * (cardW + gap);
        ImVec2 lo = c.p(left, top);
        ImVec2 hi = c.p(left + cardW, top + cardH);
        ImGui::SetCursorScreenPos(lo);
        ImGui::PushID(i);
        ImGui::InvisibleButton("card", ImVec2(hi.x - lo.x, hi.y - lo.y));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool hit = ImGui::IsItemClicked();
        ImGui::PopID();

        const float lift = hovered ? -8.0f * s : 0.0f;
        lo.y += lift;
        hi.y += lift;
        dl->AddRectFilled(ImVec2(lo.x + 4.0f * s, lo.y + 10.0f * s), ImVec2(hi.x + 4.0f * s, hi.y + 10.0f * s),
            IM_COL32(0, 0, 0, 40), c.s(20.0f));
        dl->AddRectFilled(lo, hi, IM_COL32(250, 250, 254, 250), c.s(18.0f));
        const bool focused = i == focus;
        dl->AddRect(lo, hi,
            (hovered || focused) ? IM_COL32(119, 238, 221, 255) : IM_COL32(200, 200, 216, 255),
            c.s(18.0f), 0, c.s((hovered || focused) ? 3.0f : 2.0f));

        const ImVec2 imgLo(lo.x + 14.0f * s, lo.y + 14.0f * s);
        const ImVec2 imgHi(hi.x - 14.0f * s, lo.y + 344.0f * s);
        if (i < static_cast<int>(images.size()) && images[static_cast<std::size_t>(i)].valid())
        {
            const Image& img = images[static_cast<std::size_t>(i)];
            ImVec2 uv0;
            ImVec2 uv1;
            coverUv(img, imgHi.x - imgLo.x, imgHi.y - imgLo.y, &uv0, &uv1);
            dl->AddImageRounded(img.tex, imgLo, imgHi, uv0, uv1, IM_COL32_WHITE, c.s(12.0f));
        }
        else
        {
            dl->AddRectFilled(imgLo, imgHi, IM_COL32(119, 238, 221, 60), c.s(12.0f));
        }
        if (font != nullptr)
        {
            const float titleSize = c.s(32.0f);
            const std::string& title = titles[static_cast<std::size_t>(i)];
            const ImVec2 ts = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title.c_str());
            dl->AddText(font, titleSize, ImVec2(lo.x + (hi.x - lo.x - ts.x) * 0.5f, lo.y + 374.0f * s),
                ui::kText, title.c_str());
            if (i < static_cast<int>(subtitles.size()) && !subtitles[static_cast<std::size_t>(i)].empty())
            {
                const float subSize = c.s(21.0f);
                const std::string& sub = subtitles[static_cast<std::size_t>(i)];
                const ImVec2 ss = font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, sub.c_str());
                dl->AddText(font, subSize,
                    ImVec2(lo.x + (hi.x - lo.x - ss.x) * 0.5f, lo.y + 426.0f * s), ui::kNotePink,
                    sub.c_str());
            }
            const float hintSize = c.s(19.0f);
            const char* hint = held ? "松手就选它" : "点一下选这个";
            const ImVec2 hs = font->CalcTextSizeA(hintSize, FLT_MAX, 0.0f, hint);
            dl->AddText(font, hintSize,
                ImVec2(lo.x + (hi.x - lo.x - hs.x) * 0.5f, hi.y - 52.0f * s),
                IM_COL32(140, 140, 165, 255), hint);
        }
        if (hit)
        {
            clicked = i;
        }
    }
    (void)time;
    return clicked;
}

void imageCard(const Canvas& c, ImFont* font, const Image& img, const std::string& caption)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float s = c.scale;
    // Fit inside 1000x450 without distortion; the box's centre is fixed so the
    // picture never jumps between steps.
    const float maxW = 1120.0f;
    const float maxH = 430.0f;
    float w = maxW;
    float h = maxH;
    if (img.valid())
    {
        const float aspect = static_cast<float>(img.w) / static_cast<float>(img.h);
        if (maxW / aspect <= maxH)
        {
            w = maxW;
            h = maxW / aspect;
        }
        else
        {
            h = maxH;
            w = maxH * aspect;
        }
    }
    const float cx = 960.0f;
    const float cy = 176.0f + h * 0.5f;
    ImVec2 lo = c.p(cx - w * 0.5f, cy - h * 0.5f);
    ImVec2 hi = c.p(cx + w * 0.5f, cy + h * 0.5f);

    dl->AddRectFilled(ImVec2(lo.x + 5.0f * s, lo.y + 12.0f * s), ImVec2(hi.x + 5.0f * s, hi.y + 12.0f * s),
        IM_COL32(0, 0, 0, 55), c.s(16.0f));
    if (img.valid())
    {
        dl->AddImageRounded(img.tex, lo, hi, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE,
            c.s(16.0f));
        dl->AddRect(lo, hi, IM_COL32(255, 255, 255, 230), c.s(16.0f), 0, c.s(3.0f));
    }
    else
    {
        dl->AddRectFilled(lo, hi, IM_COL32(60, 60, 80, 220), c.s(16.0f));
        if (font != nullptr)
        {
            const char* missing = "（图片没找到）";
            const float size = c.s(24.0f);
            const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, missing);
            dl->AddText(font, size, ImVec2((lo.x + hi.x - ts.x) * 0.5f, (lo.y + hi.y - ts.y) * 0.5f),
                IM_COL32(230, 230, 240, 255), missing);
        }
    }
    if (!caption.empty() && font != nullptr)
    {
        // The caption sits over whatever is behind the layer (the song list's
        // phone panel here), so it gets its own dark pill and wraps instead of
        // running off the right edge.
        float size = c.s(22.0f);
        const float maxTextW = c.s(1180.0f);
        const TextLayout wrap = layoutText(font, size, caption, maxTextW);
        const std::size_t lineCount = std::max<std::size_t>(wrap.lines.size(), 1);
        float widest = 0.0f;
        for (const Line& line : wrap.lines)
        {
            widest = std::max(widest, line.width);
        }
        const float lineH = size * 1.35f;
        const float pillW = std::min(widest + c.s(40.0f), maxTextW + c.s(40.0f));
        const float pillH = lineH * static_cast<float>(lineCount) + c.s(16.0f);
        const float top = hi.y + 14.0f * s;
        const ImVec2 plo(cx - pillW * 0.5f, top);
        const ImVec2 phi(cx + pillW * 0.5f, top + pillH);
        dl->AddRectFilled(plo, phi, IM_COL32(20, 20, 32, 165), c.s(14.0f));
        float ty = top + c.s(8.0f);
        for (const Line& line : wrap.lines)
        {
            dl->AddText(font, size, ImVec2(cx - line.width * 0.5f, ty),
                IM_COL32(232, 232, 242, 245), caption.data() + line.begin, caption.data() + line.end);
            ty += lineH;
        }
    }
}

void spotlight(ImDrawList* dl, const ImVec2& lo, const ImVec2& hi, float time,
    const std::string& label)
{
    if (dl == nullptr)
    {
        return;
    }
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const ImVec2 a(std::clamp(lo.x, 0.0f, screen.x), std::clamp(lo.y, 0.0f, screen.y));
    const ImVec2 b(std::clamp(hi.x, 0.0f, screen.x), std::clamp(hi.y, 0.0f, screen.y));
    if (b.x <= a.x || b.y <= a.y)
    {
        return;
    }
    const ImU32 dimCol = IM_COL32(0, 0, 0, 120);
    // Four rectangles around the hole rather than a clip: an AddRectFilled
    // with a hole needs a stencil, and four quads also let the hole keep its
    // rounded corners on the ring.
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(screen.x, a.y), dimCol);
    dl->AddRectFilled(ImVec2(0.0f, b.y), ImVec2(screen.x, screen.y), dimCol);
    dl->AddRectFilled(ImVec2(0.0f, a.y), ImVec2(a.x, b.y), dimCol);
    dl->AddRectFilled(ImVec2(b.x, a.y), ImVec2(screen.x, b.y), dimCol);

    const float pulse = 0.72f + 0.28f * std::sin(time * 3.4f);
    const ImU32 ring = IM_COL32(119, 238, 221, static_cast<int>(255.0f * pulse));
    const float pad = 6.0f;
    dl->AddRect(ImVec2(a.x - pad, a.y - pad), ImVec2(b.x + pad, b.y + pad), ring, 12.0f, 0, 3.0f);
    dl->AddRect(ImVec2(a.x - pad - 4.0f, a.y - pad - 4.0f), ImVec2(b.x + pad + 4.0f, b.y + pad + 4.0f),
        IM_COL32(119, 238, 221, 55), 15.0f, 0, 2.0f);

    if (!label.empty())
    {
        // The default atlas has no CJK glyphs: a Chinese label drawn with
        // ImGui::GetFont() comes out as a row of '?' (seen in the settings
        // spotlight). The story's own face is the game's body font.
        ImFont* font = game::bodyFont();
        if (font == nullptr)
        {
            return;
        }
        const float size = 22.0f;
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
        const float w = ts.x + 28.0f;
        const float h = 38.0f;
        float x = (a.x + b.x) * 0.5f - w * 0.5f;
        float y = a.y - pad - h - 12.0f;
        if (y < 8.0f)
        {
            y = b.y + pad + 12.0f;
        }
        x = std::clamp(x, 8.0f, screen.x - w - 8.0f);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(38, 38, 58, 242), h * 0.5f);
        dl->AddTriangleFilled(ImVec2(x + w * 0.5f - 7.0f, y + h), ImVec2(x + w * 0.5f + 7.0f, y + h),
            ImVec2(x + w * 0.5f, y + h + 9.0f), IM_COL32(38, 38, 58, 242));
        dl->AddText(font, size, ImVec2(x + 14.0f, y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255),
            label.c_str());
    }
}

void tip(ImDrawList* dl, ImFont* font, const ImVec2& center, const std::string& text)
{
    if (dl == nullptr || font == nullptr || text.empty())
    {
        return;
    }
    const float size = 20.0f;
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    const float w = ts.x + 26.0f;
    const float h = 34.0f;
    const ImVec2 lo(center.x - w * 0.5f, center.y - h * 0.5f);
    dl->AddRectFilled(lo, ImVec2(lo.x + w, lo.y + h), IM_COL32(38, 38, 58, 235), h * 0.5f);
    dl->AddText(font, size, ImVec2(lo.x + 13.0f, lo.y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255),
        text.c_str());
}

} // namespace storyui
