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

    // Pixi's `strokeThickness: 8` is a stroke centred on the glyph outline, so
    // it reaches 4px past the glyph. ImDrawList has no stroke, so the same ring
    // is approximated with 16 offset copies. `radius` is that 4px.
    void outlineText(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 fill,
        const char* begin, const char* end, float radius, ImU32 stroke)
    {
        constexpr int kSteps = 16;
        for (int i = 0; i < kSteps; ++i)
        {
            const float a = 6.28318530718f * static_cast<float>(i) / static_cast<float>(kSteps);
            dl->AddText(font, size, ImVec2(pos.x + std::cos(a) * radius, pos.y + std::sin(a) * radius),
                stroke, begin, end);
        }
        dl->AddText(font, size, pos, fill, begin, end);
    }

    void outlineText(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 fill,
        const char* text, float radius, ImU32 stroke)
    {
        outlineText(dl, font, size, pos, fill, text, text + std::strlen(text), radius, stroke);
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
    const GLuint dialogue = renderer.loadUiTexture(base + "Dialogue_Background.png", localError);
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
    const TextLayout& layout, std::size_t revealBytes, float time, bool showArrow, Box box)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (dl == nullptr)
    {
        return;
    }

    // The band spans the full width at y 771..1080; `box` picks which slice to
    // paint. Clipping is why this is not three different layouts: the gradient
    // is horizontally uniform, so a clipped band still looks like a band, while
    // the settings card (left) or the phone panel (right) stays uncovered.
    // Numbers below are the editor's own setup table, copied 1:1 (1920x1080
    // canvas, Pixi top-left anchored text):
    //   name : (225,775) 44px  fill #EBEBEF  stroke #5D5D79 / 8
    //   body : (245,845) 44px  fill #FFFFFF  lineHeight 55  wrap 1300
    // The hairline under the name in the reference is *the band's own rule* at
    // y~840, not something the UI draws - so there is no underline here.
    float clipL = 0.0f;
    float clipR = 1920.0f;
    float textL = 245.0f;
    float nameL = 225.0f;
    float size = 44.0f;
    float lineGap = 55.0f / 44.0f;
    if (box == Box::Left)
    {
        // The song-select phone panel is in the way: narrower column, but the
        // vertical rhythm (name 775 / body 845) stays identical.
        clipR = 780.0f;
        textL = 120.0f;
        nameL = 100.0f;
        size = 38.0f;
    }
    else if (box == Box::Right)
    {
        clipL = 944.0f;
        textL = 1000.0f;
        nameL = 980.0f;
        size = 38.0f;
    }
    const float lineH = size * lineGap;

    // The band is drawn whole, at every layout: clipping it left a hard seam
    // across the gradient's hairline, and the settings card ends above it
    // anyway (the card is 780 tall, the band starts at 771).
    float rightPanelBottom = 0.0f;
    if (box == Box::Right)
    {
        // Settings card is open on the left. The band's hairline would run right
        // under the card's bottom edge and read as "the card is cut", so this
        // one uses a self-contained panel instead - and the panel hugs the text:
        // a fixed 748..1070 rectangle left a 300px empty slab under two lines.
        const float lines = static_cast<float>(std::max<std::size_t>(layout.lines.size(), 1));
        rightPanelBottom = 862.0f + lineH * lines + 18.0f;
        dl->AddRectFilled(c.p(962.0f, 748.0f), c.p(1904.0f, rightPanelBottom),
            IM_COL32(18, 18, 30, 210), c.s(22.0f));
        dl->AddRect(c.p(962.0f, 748.0f), c.p(1904.0f, rightPanelBottom),
            IM_COL32(140, 140, 175, 90), c.s(22.0f), 0, c.s(2.0f));
    }
    else if (gDialogue != 0)
    {
        // Twice on purpose: the band is only 50% at its densest (2,0,32,128),
        // which is right over the bright story backgrounds the art was made for
        // but nearly invisible over this game's dark song list. Stacking keeps
        // the same shape and colour while landing at ~75% at the bottom.
        dl->AddImage(gDialogue, c.p(0.0f, 0.0f), c.p(1920.0f, 1080.0f));
        dl->AddImage(gDialogue, c.p(0.0f, 0.0f), c.p(1920.0f, 1080.0f));
    }
    else
    {
        // Fallback: the same vertical fade, drawn by hand (headless runs with a
        // half-copied build still get readable text).
        if (box != Box::Right)
        {
            for (int i = 0; i < 20; ++i)
            {
                const float t = static_cast<float>(i) / 19.0f;
                const float y0 = 760.0f + t * 320.0f;
                dl->AddRectFilled(c.p(0.0f, y0), c.p(1920.0f, y0 + 20.0f),
                    IM_COL32(10, 10, 20, static_cast<int>(t * 165.0f)));
            }
        }
    }

    if (font != nullptr && !name.empty())
    {
        // The name sits just above the band's hairline rule (y ~840), exactly
        // where the reference puts it, in the heavier face when the system has
        // one (boldFont falls back to the body face).
        ImFont* nameFont = game::boldFont();
        if (nameFont == nullptr)
        {
            nameFont = font;
        }
        const float nameSize = c.s(size);
        const float nameY = box == Box::Full ? 775.0f : 782.0f;
        outlineText(dl, nameFont, nameSize, ImVec2(c.x(nameL), c.y(nameY)),
            IM_COL32(235, 235, 239, 255), name.c_str(), nameSize * 0.09f,
            IM_COL32(93, 93, 121, 255));
    }

    for (const Line& line : layout.lines)
    {
        if (line.begin >= revealBytes)
        {
            break;
        }
        const std::size_t end = std::min(line.end, revealBytes);
        const ImVec2 pos(c.x(textL), c.y(845.0f) + static_cast<float>(&line - layout.lines.data()) * c.s(lineH));
        outlineText(dl, font, c.s(size), pos, IM_COL32(255, 255, 255, 255),
            body.data() + line.begin, body.data() + end, c.s(size) * 0.09f,
            IM_COL32(93, 93, 121, 255));
    }

    if (showArrow)
    {
        const float bob = std::sin(time * 4.2f) * 3.0f * c.scale;
        const float ax = c.x(clipR - 26.0f);
        const float ay = (box == Box::Right ? c.y(rightPanelBottom - 40.0f) : c.y(1030.0f)) + bob;
        dl->AddTriangleFilled(ImVec2(ax - 11.0f * c.scale, ay - 6.0f * c.scale),
            ImVec2(ax + 11.0f * c.scale, ay - 6.0f * c.scale),
            ImVec2(ax, ay + 9.0f * c.scale), IM_COL32(0, 0, 0, 190));
        dl->AddTriangleFilled(ImVec2(ax - 9.0f * c.scale, ay - 7.0f * c.scale),
            ImVec2(ax + 9.0f * c.scale, ay - 7.0f * c.scale),
            ImVec2(ax, ay + 7.0f * c.scale), IM_COL32(255, 255, 255, 235));
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
    // SceneText_Background spec: 44px, centred on (960,540).
    const float size = c.s(44.0f);
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
    // The band is only 120 virtual units tall (y 480..600): 1.5x on 44px would
    // push a two-line title outside it, so the scene title leads tighter.
    const float lineH = size * 1.2f;
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
            const float size = c.s(32.0f);
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
            // The pictures are transparent cut-outs, so they are fitted inside
            // the slot instead of cover-cropped: cropping a keyboard would cut
            // keys off, and there is no background rectangle to trim.
            const Image& img = images[static_cast<std::size_t>(i)];
            const float areaW = imgHi.x - imgLo.x;
            const float areaH = imgHi.y - imgLo.y;
            const float fit = std::min(areaW / static_cast<float>(img.w),
                areaH / static_cast<float>(img.h));
            const float dw = static_cast<float>(img.w) * fit;
            const float dh = static_cast<float>(img.h) * fit;
            const ImVec2 at((imgLo.x + imgHi.x - dw) * 0.5f, (imgLo.y + imgHi.y - dh) * 0.5f);
            dl->AddImage(img.tex, at, ImVec2(at.x + dw, at.y + dh));
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
    const float maxW = 1040.0f;
    const float maxH = 420.0f;
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
    // Centre unless the picture would run under the song-select phone panel on
    // the right (it starts at ~1383): a wide picture slides left instead of
    // being half-covered by it.
    const float cx = std::min(960.0f, 1352.0f - w * 0.5f);
    const float cy = 176.0f + h * 0.5f;
    ImVec2 lo = c.p(cx - w * 0.5f, cy - h * 0.5f);
    ImVec2 hi = c.p(cx + w * 0.5f, cy + h * 0.5f);

    // A very soft plate instead of a photo frame: both pictures are cut-outs now,
    // so a white rim would just be a rectangle around a transparent PNG.
    dl->AddRectFilled(ImVec2(lo.x - 26.0f * s, lo.y - 18.0f * s), ImVec2(hi.x + 26.0f * s, hi.y + 26.0f * s),
        IM_COL32(8, 8, 16, 92), c.s(26.0f));
    if (img.valid())
    {
        dl->AddImage(img.tex, lo, hi);
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
        // cx is a virtual unit, top/pillW are already pixels: mixing the two put
        // the caption ~400px to the right of the picture.
        const float ccx = c.x(cx);
        const ImVec2 plo(ccx - pillW * 0.5f, top);
        const ImVec2 phi(ccx + pillW * 0.5f, top + pillH);
        dl->AddRectFilled(plo, phi, IM_COL32(20, 20, 32, 165), c.s(14.0f));
        float ty = top + c.s(8.0f);
        for (const Line& line : wrap.lines)
        {
            dl->AddText(font, size, ImVec2(ccx - line.width * 0.5f, ty),
                IM_COL32(232, 232, 242, 245), caption.data() + line.begin, caption.data() + line.end);
            ty += lineH;
        }
    }
}

void spotlight(ImDrawList* dl, const ImVec2& lo, const ImVec2& hi, float time,
    const std::string& label, int dimAlpha)
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
    const ImU32 dimCol = IM_COL32(0, 0, 0, std::clamp(dimAlpha, 0, 255));
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
