// CppSekai - story UI drawing (see StoryUi.hpp / STORY-UI.md).
#include "StoryUi.hpp"

// The name card needs ImGuiInputTextState to drop the select-all that ImGui
// applies when an input is activated by code (see nameCard).
#include "imgui_internal.h"

#include "Ui.hpp"
#include "Intro.hpp"
#include "platform/Renderer.hpp"
#include "../third_party/mmw_preview/vendor/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace storyui
{
namespace
{
    // The three frames, in window-pixel textures. 0 = not loaded.
    ImTextureID gDialogue = 0;
    ImTextureID gChoices = 0;
    ImTextureID gSceneText = 0;
    ImTextureID gSkipIcon = 0;
    // stage.png, the playfield itself: the keyboard step crops it to the half
    // below the judgement line instead of shipping a picture of a keyboard.
    ImTextureID gLane = 0;
    float gSkipAspect = 28.0f / 44.0f;


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
    if (const platform::Renderer::HudSprite* skip = renderer.hud("ui_skip");
        skip != nullptr && skip->id != 0 && skip->width > 0) {
        gSkipIcon = reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(skip->id));
        gSkipAspect = static_cast<float>(skip->height) / static_cast<float>(skip->width);
    }
    if (const GLuint lane = renderer.loadUiTexture(assetDir + "/mmw/stage.png", localError);
        lane != 0) {
        gLane = reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(lane));
    }
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
    float textL = 245.0f;
    float nameL = 225.0f;
    float size = 44.0f;
    float lineGap = 55.0f / 44.0f;
    if (box == Box::Left)
    {
        // The song-select phone panel is in the way: narrower column, but the
        // vertical rhythm (name 775 / body 845) stays identical.
        textL = 120.0f;
        nameL = 100.0f;
        size = 38.0f;
    }
    const float lineH = size * lineGap;

    // The band is drawn whole, at every layout: clipping it left a hard seam
    // across the gradient's hairline, and the settings card ends above it
    // anyway (the card is 780 tall, the band starts at 771).
    // Every layout uses the shipped band. An earlier version drew a bespoke
    // dark panel for the settings step; that read as "some other UI" next to
    // the reference, so it is gone - the band is the dialogue box, full stop.
    if (gDialogue != 0)
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
        for (int i = 0; i < 20; ++i)
        {
            const float t = static_cast<float>(i) / 19.0f;
            const float y0 = 760.0f + t * 320.0f;
            dl->AddRectFilled(c.p(0.0f, y0), c.p(1920.0f, y0 + 20.0f),
                IM_COL32(10, 10, 20, static_cast<int>(t * 165.0f)));
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

    // No ▼ prompt: the band already reads as "there is more", and the arrow
    // sat right on top of the last line of text on short boxes.
    (void)showArrow;
}

namespace
{
    // Dark pill under a picture: wraps at 1180 so a long sentence never runs off
    // the right edge, and stays centred on `cxVirtual`.
    // Returns the bottom edge in pixels so the caller can hang something under
    // the pill (the keyboard step's 修改键位 button does).
    float drawCaption(const Canvas& c, ImFont* font, const std::string& caption, float cxVirtual,
        float topPixel)
    {
        if (caption.empty() || font == nullptr)
        {
            return topPixel;
        }
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float size = c.s(22.0f);
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
        const float top = topPixel + 14.0f * c.scale;
        // cx is a virtual unit, top/pillW are already pixels: mixing the two put
        // the caption ~400px to the right of the picture.
        const float ccx = c.x(cxVirtual);
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
        return phi.y;
    }
} // namespace

bool keypad(const Canvas& c, ImFont* font, const std::vector<std::string>& keysIn,
    const std::string& caption, const std::string& actionLabel)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (dl == nullptr)
    {
        return false;
    }
    // The shipped default row, only used when the host has no bindings to hand
    // over (headless runs). Real runs always pass the player's own keys.
    static const char* const kDefault[12] = {
        "Z", "S", "X", "D", "C", "V", "G", "B", "H", "N", "J", "M"};
    std::vector<std::string> keys = keysIn;
    if (keys.size() != 12)
    {
        keys.clear();
        for (const char* key : kDefault)
        {
            keys.emplace_back(key);
        }
    }

    // The playfield itself, cropped to everything below the judgement line
    // (stage.png is 1024x588, lanes run from y~294 to y~575). Showing the real
    // lane strip means the picture can never disagree with the game, and a key
    // cap sitting in a lane is the most direct "this key plays this lane" there
    // is. The crop lands in a 880x242 box; wide pictures slide left so they do
    // not run under the song-select phone panel (same rule as imageCard).
    constexpr float kTexW = 1024.0f;
    constexpr float kTexH = 588.0f;
    constexpr float kCropTop = 294.0f;
    constexpr float kCropBot = 575.0f;
    const float destW = 880.0f;
    const float destH = destW * (kCropBot - kCropTop) / kTexW;
    const float cx = std::min(960.0f, 1352.0f - destW * 0.5f);
    const float top = 208.0f;
    const ImVec2 lo = c.p(cx - destW * 0.5f, top);
    const ImVec2 hi = c.p(cx + destW * 0.5f, top + destH);
    const float s = c.scale;

    // Same soft plate the picture cards use, so the two read as one family.
    dl->AddRectFilled(ImVec2(lo.x - 26.0f * s, lo.y - 18.0f * s),
        ImVec2(hi.x + 26.0f * s, hi.y + 26.0f * s), IM_COL32(8, 8, 16, 92), c.s(26.0f));
    // stage.png is a translucent white plate: the game lays it over a bright
    // stage backdrop, so on this dimmed song list alone the lanes came out
    // almost black. A light plate under it restores the in-game reading (and
    // doubles as the "screen" the strip is a crop of).
    dl->AddRectFilled(lo, hi, IM_COL32(228, 232, 243, 236), c.s(14.0f));
    if (gLane != 0)
    {
        dl->AddImage(gLane, lo, hi, ImVec2(0.0f, kCropTop / kTexH),
            ImVec2(1.0f, kCropBot / kTexH));
    }

    // Lane edges are linear in y (fake perspective): left/right measured off the
    // texture at y=300 and y=560. One key cap per lane, all on the same row.
    constexpr float kRow = 462.0f;
    const float edgeT = (kRow - 300.0f) * 0.8346f;
    const float laneLeft = 240.0f - edgeT;
    const float laneRight = 783.0f + edgeT;
    const float laneW = (laneRight - laneLeft) / 12.0f;
    const float texToDest = destW / kTexW;
    const float chipW = laneW * texToDest * 0.78f;
    const float chipH = 52.0f;
    for (int i = 0; i < 12; ++i)
    {
        const float laneCx = laneLeft + (static_cast<float>(i) + 0.5f) * laneW;
        const float vx = cx - destW * 0.5f + laneCx * texToDest;
        const float vy = top + (kRow - kCropTop) * (destH / (kCropBot - kCropTop));
        const ImVec2 a = c.p(vx - chipW * 0.5f, vy - chipH * 0.5f);
        const ImVec2 b = c.p(vx + chipW * 0.5f, vy + chipH * 0.5f);
        dl->AddRectFilled(ImVec2(a.x + 2.0f * s, a.y + 4.0f * s), ImVec2(b.x + 2.0f * s, b.y + 4.0f * s),
            IM_COL32(12, 14, 30, 90), c.s(12.0f));
        dl->AddRectFilled(a, b, IM_COL32(246, 247, 252, 242), c.s(12.0f));
        dl->AddRect(a, b, IM_COL32(178, 182, 206, 235), c.s(12.0f), 0, std::max(1.0f, c.s(1.5f)));
        if (font == nullptr)
        {
            continue;
        }
        const char* label = keys[static_cast<std::size_t>(i)].c_str();
        // SDL names can be long ("Left Ctrl"): shrink until it fits rather than
        // clipping the one thing the player has to read.
        float size = c.s(28.0f);
        const float room = (b.x - a.x) - c.s(10.0f);
        while (size > c.s(12.0f) && font->CalcTextSizeA(size, FLT_MAX, 0.0f, label).x > room)
        {
            size *= 0.9f;
        }
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        dl->AddText(font, size, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f),
            IM_COL32(61, 60, 92, 255), label);
    }

    const float capBottom = drawCaption(c, font, caption, cx, c.y(top + destH));
    if (actionLabel.empty())
    {
        return false;
    }

    // A real capsule under the caption, in the settings card's own painting
    // (ui::capsuleDraw): this is the only place in the tutorial that changes a
    // setting the player can see, so it needs the same affordance a button has.
    const float btnW = 176.0f * s;
    const float btnH = 48.0f * s;
    const ImVec2 blo(c.x(cx) - btnW * 0.5f, capBottom + 18.0f * s);
    const ImVec2 bhi(blo.x + btnW, blo.y + btnH);
    ImGui::SetCursorScreenPos(blo);
    ImGui::PushID("story-keys-action");
    ImGui::InvisibleButton("b", ImVec2(btnW, btnH));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked();
    ImGui::PopID();
    const float hover = ui::anim(0x5717d001u, hovered, 22.0f);
    const float press = ui::anim(0x5717d002u, held, 30.0f);
    ui::capsuleDraw(dl, blo, bhi, actionLabel.c_str(), false, hover, press);
    return clicked;
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

    // Painted with ui::capsuleDraw - the same function the settings card's own
    // buttons use - instead of tinting the shipped pill art mint on hover. The
    // art and a hand-rolled hover treatment drifted from the rest of the UI
    // (green glow the player had never seen anywhere else); a capsule is a
    // capsule, so both surfaces now share one painting.
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
        ImGui::SetCursorScreenPos(lo);
        ImGui::PushID(i);
        ImGui::InvisibleButton("choice", ImVec2(hi.x - lo.x, hi.y - lo.y));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool hit = ImGui::IsItemClicked();
        if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
            std::printf("[story] choice %d hovered=%d any=%d mouse=%.0f,%.0f\n", i,
                ImGui::IsItemHovered() ? 1 : 0, ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ? 1 : 0,
                ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
            std::fflush(stdout);
        }
        ImGui::PopID();
        // Hover and press are the settings capsule's own curve: a hair of
        // growth and #f3f3f9, then the inverted mint on press. Nothing here
        // invents a colour the rest of the UI does not use.
        const float hover = ui::anim(0x5717c001u + static_cast<unsigned>(i), hovered, 22.0f);
        const float press = ui::anim(0x5717c021u + static_cast<unsigned>(i), held, 30.0f);
        ui::capsuleDraw(dl, lo, hi, options[static_cast<std::size_t>(i)].c_str(), false, hover,
            press, c.s(32.0f));
        if (i == focus)
        {
            // Gamepad focus: the same ring the settings card draws around the
            // row the pad is on (ui::PadScope), not a thicker custom one.
            const float glow = ui::anim(0x5717c041u + static_cast<unsigned>(i), true, 22.0f);
            const float r = c.s(pillH * 0.5f + 3.0f);
            dl->AddRectFilled(ImVec2(lo.x - 4.0f * c.scale, lo.y - 4.0f * c.scale),
                ImVec2(hi.x + 4.0f * c.scale, hi.y + 4.0f * c.scale),
                IM_COL32(106, 232, 208, static_cast<int>(46.0f * glow)), r);
            dl->AddRect(ImVec2(lo.x - 4.0f * c.scale, lo.y - 4.0f * c.scale),
                ImVec2(hi.x + 4.0f * c.scale, hi.y + 4.0f * c.scale),
                IM_COL32(46, 186, 164, static_cast<int>(225.0f * glow)), r, 0, c.s(2.0f));
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
    drawCaption(c, font, caption, cx, hi.y);
}

namespace
{
    struct NameEditLimit
    {
        int maxLen = 10;
    };

    // InputText filter for the name field: counts UTF-8 code points (10 Chinese
    // characters is 30 bytes, a byte limit would cut it to three) and drops
    // whatever lands past the limit. CallbackAlways, so it also trims a paste.
    int nameLimitCallback(ImGuiInputTextCallbackData* data)
    {
        auto* limit = static_cast<NameEditLimit*>(data->UserData);
        if (limit == nullptr || limit->maxLen <= 0 || data->BufTextLen <= 0)
        {
            return 0;
        }
        int count = 0;
        int cut = -1;
        for (int i = 0; i < data->BufTextLen;)
        {
            if (count >= limit->maxLen)
            {
                cut = i;
                break;
            }
            int len = utf8Len(static_cast<unsigned char>(data->Buf[i]));
            if (len <= 0 || i + len > data->BufTextLen)
            {
                len = data->BufTextLen - i;
            }
            i += len;
            ++count;
        }
        if (cut >= 0 && cut < data->BufTextLen)
        {
            data->DeleteChars(cut, data->BufTextLen - cut);
        }
        return 0;
    }

    // The little pencil at the right end of the field. Drawn rather than shipped
    // as a sprite: the reference's icon is a 22px glyph that would blur at this
    // canvas size, and a shape has no file to go missing.
    void pencilIcon(ImDrawList* dl, const ImVec2& center, float size, ImU32 body, ImU32 tip)
    {
        constexpr float k = 0.70710678f;
        const float half = size * 0.5f;
        const float wide = size * 0.13f;
        const float tipLen = size * 0.32f;
        // u points at the eraser (up-right), n is across the pencil.
        const ImVec2 p0(center.x - k * half, center.y + k * half);              // writing tip
        const ImVec2 p1(center.x + k * half, center.y - k * half);              // eraser end
        const ImVec2 s0(center.x - k * (half - tipLen) + k * wide,
            center.y + k * (half - tipLen) + k * wide);
        const ImVec2 s1(center.x - k * (half - tipLen) - k * wide,
            center.y + k * (half - tipLen) - k * wide);
        const ImVec2 e0(p1.x + k * wide, p1.y + k * wide);
        const ImVec2 e1(p1.x - k * wide, p1.y - k * wide);
        dl->AddTriangleFilled(p0, s0, s1, tip);
        dl->AddQuadFilled(s0, e0, e1, s1, body);
    }
} // namespace

bool nameCard(const Canvas& c, ImFont* font, const std::string& title, const std::string& note,
    const std::string& hint, const std::string& confirmLabel, char* buf, int bufSize, int maxLen,
    float time, bool requestFocus, bool confirmRequested)
{
    if (buf == nullptr || bufSize <= 1)
    {
        return false;
    }
    // The window draw list, deliberately: the text field is a real ImGui item and
    // its glyphs land on this same list. Painting the plate on the foreground
    // list (what the dialogue box does) would cover the text with the card.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (dl == nullptr)
    {
        return false;
    }
    const float s = c.scale;
    const ImVec2 screen = ImGui::GetIO().DisplaySize;

    // pjsk pops cards in: a 0.94 -> 1.0 grow over ~0.16s. Everything below is
    // laid out off this rect, so the field and the button move with it.
    const float t = std::clamp(time / 0.16f, 0.0f, 1.0f);
    const float ease = t * t * (3.0f - 2.0f * t);
    const float grow = 0.94f + 0.06f * ease;
    constexpr float kCardW = 1080.0f;
    constexpr float kCardH = 600.0f;
    const float w = kCardW * grow;
    const float h = kCardH * grow;
    const ImVec2 lo = c.p(960.0f - w * 0.5f, 540.0f - h * 0.5f);
    const ImVec2 hi = c.p(960.0f + w * 0.5f, 540.0f + h * 0.5f);
    const float radius = c.s(18.0f);

    dl->AddRectFilled(ImVec2(0.0f, 0.0f), screen, IM_COL32(0, 0, 0, 150));
    ui::dropShadow(dl, lo, hi, radius, s);
    dl->AddRectFilled(lo, hi, ui::kCardBg, radius);

    ImU32 textCol = ui::kBodyText;

    // ---- hint line ----
    if (font != nullptr && !title.empty())
    {
        const float size = c.s(40.0f);
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, title.c_str());
        dl->AddText(font, size, ImVec2((lo.x + hi.x - ts.x) * 0.5f, lo.y + h * 0.19f * s),
            textCol, title.c_str());
    }

    // ---- the field ----
    const float fieldW = w * 0.46f * s;
    const float fieldH = h * 0.13f * s;
    const ImVec2 fLo((lo.x + hi.x - fieldW) * 0.5f, lo.y + h * 0.33f * s);
    const ImVec2 fHi(fLo.x + fieldW, fLo.y + fieldH);
    dl->AddRectFilled(fLo, fHi, ui::kRowsBg, c.s(12.0f));

    // The caret's own text: 42px (the atlas size) times the canvas scale, so a
    // small window keeps the field proportional instead of overflowing it.
    const float fontSize = c.s(42.0f);
    const ImVec2 textSize
        = font != nullptr ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf) : ImVec2(0.0f, 0.0f);
    // Centred like the reference, but never so far in that the pencil collides.
    const float pencilRoom = c.s(56.0f);
    const float padX = std::max((fieldW - textSize.x - pencilRoom) * 0.5f, c.s(18.0f));
    const float padY = std::max((fieldH - fontSize) * 0.5f, 0.0f);

    ImGui::PushID("story-name-field");
    if (font != nullptr)
    {
        ImGui::PushFont(font, fontSize);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, c.s(12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, IM_COL32(150, 150, 178, 255));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, IM_COL32(119, 238, 221, 110));
    ImGui::SetCursorScreenPos(fLo);
    ImGui::SetNextItemWidth(fieldW);
    if (requestFocus)
    {
        ImGui::SetKeyboardFocusHere();
    }
    NameEditLimit limit;
    limit.maxLen = maxLen;
    const bool entered = ImGui::InputTextWithHint("##name", hint.c_str(), buf, static_cast<size_t>(bufSize),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways,
        nameLimitCallback, &limit);
    // Taking focus by code makes ImGui select the whole text, which turned the
    // prefilled name into a highlight - and the first keystroke would have wiped
    // it. The selection only appears a frame *after* the focus request lands, so
    // the step that clears it has to stay armed until it sees one: `requestFocus`
    // arms it, the frame that finds a selection disarms it.
    static ImGuiID sClearNameSelection = 0;
    const ImGuiID fieldId = ImGui::GetItemID();
    if (requestFocus)
    {
        sClearNameSelection = fieldId;
    }
    if (sClearNameSelection == fieldId && fieldId != 0)
    {
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(fieldId))
        {
            if (state->HasSelection())
            {
                state->ClearSelection(); // caret stays at the end, like the reference
                sClearNameSelection = 0;
            }
        }
    }
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
    if (font != nullptr)
    {
        ImGui::PopFont();
    }
    ImGui::PopID();

    if (font != nullptr)
    {
        pencilIcon(dl, ImVec2(fHi.x - c.s(34.0f), (fLo.y + fHi.y) * 0.5f), c.s(30.0f), textCol,
            IM_COL32(38, 38, 70, 255));
    }

    // ---- the pink note ----
    if (font != nullptr && !note.empty())
    {
        const float size = c.s(30.0f);
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, note.c_str());
        dl->AddText(font, size, ImVec2((lo.x + hi.x - ts.x) * 0.5f, lo.y + h * 0.55f * s),
            ui::kNotePink, note.c_str());
    }

    // ---- 确定 ----
    const float btnW = w * 0.26f * s;
    const float btnH = h * 0.125f * s;
    const ImVec2 bLo((lo.x + hi.x - btnW) * 0.5f, lo.y + h * 0.80f * s);
    const ImVec2 bHi(bLo.x + btnW, bLo.y + btnH);
    ImGui::PushID("story-name-ok");
    ImGui::SetCursorScreenPos(bLo);
    ImGui::InvisibleButton("ok", ImVec2(btnW, btnH));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked();
    ImGui::PopID();
    const float hover = ui::anim(0x5717e001u, hovered, 22.0f);
    const float press = ui::anim(0x5717e002u, held, 30.0f);
    ui::capsuleDraw(dl, bLo, bHi, confirmLabel.c_str(), true, hover, press, c.s(34.0f));

    return entered || clicked || confirmRequested;
}

ImTextureID skipIcon()
{
    return gSkipIcon;
}

void skipKey(ImDrawList* dl, const Canvas& c, const ImVec2& center, float hover, float press,
    ImTextureID icon)
{
    if (dl == nullptr)
    {
        return;
    }
    // Same shape as game/Intro.cpp's opening-card key (SKIP_BTN_*): 40px disc in
    // 1920x1080 units, three stacked shadow discs, white face, navy glyph. Kept
    // in sync by hand - if one changes, change both.
    const float base = 40.0f * c.scale;
    const float radius = base * (1.0f + 0.05f * hover - 0.04f * press);
    for (int layer = 3; layer >= 1; --layer) {
        const float grow = static_cast<float>(layer) * 1.5f * c.scale;
        const float drop = (1.2f + 0.5f * static_cast<float>(layer)) * c.scale;
        const float alpha = layer == 3 ? 0.07f : (layer == 2 ? 0.11f : 0.15f);
        dl->AddCircleFilled(ImVec2(center.x, center.y + drop), radius + grow,
            IM_COL32(22, 24, 46, static_cast<int>(alpha * 255.0f)), 64);
    }
    dl->AddCircleFilled(center, radius,
        IM_COL32(255, 255, 255, static_cast<int>((0.94f + 0.06f * hover) * 255.0f)), 64);

    if (icon != 0)
    {
        const float iconW = 40.0f * c.scale * (1.0f + 0.05f * hover - 0.04f * press);
        const float iconH = iconW * gSkipAspect;
        dl->AddImage(icon, ImVec2(center.x - iconW * 0.5f, center.y - iconH * 0.5f),
            ImVec2(center.x + iconW * 0.5f, center.y + iconH * 0.5f), ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            IM_COL32(61, 60, 92, static_cast<int>((0.90f + 0.10f * hover) * 255.0f)));
    }
}

SpotlightLabel spotlight(ImDrawList* dl, const ImVec2& lo, const ImVec2& hi, float time,
    const std::string& label, int dimAlpha)
{
    SpotlightLabel out;
    if (dl == nullptr)
    {
        return out;
    }
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const ImVec2 a(std::clamp(lo.x, 0.0f, screen.x), std::clamp(lo.y, 0.0f, screen.y));
    const ImVec2 b(std::clamp(hi.x, 0.0f, screen.x), std::clamp(hi.y, 0.0f, screen.y));
    if (b.x <= a.x || b.y <= a.y)
    {
        return out;
    }
    const ImU32 dimCol = IM_COL32(0, 0, 0, std::clamp(dimAlpha, 0, 255));
    // Four rectangles around the hole rather than a clip: an AddRectFilled
    // with a hole needs a stencil, and four quads also let the hole keep its
    // rounded corners on the ring.
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(screen.x, a.y), dimCol);
    dl->AddRectFilled(ImVec2(0.0f, b.y), ImVec2(screen.x, screen.y), dimCol);
    dl->AddRectFilled(ImVec2(0.0f, a.y), ImVec2(a.x, b.y), dimCol);
    dl->AddRectFilled(ImVec2(b.x, a.y), ImVec2(screen.x, b.y), dimCol);

    const float pad = 6.0f;
    const auto ring = [&](float grow, int alpha, float thick, float radius) {
        dl->AddRect(ImVec2(a.x - pad - grow, a.y - pad - grow),
            ImVec2(b.x + pad + grow, b.y + pad + grow),
            IM_COL32(119, 238, 221, std::clamp(alpha, 0, 255)), radius, 0, thick);
    };
    // Soft halo hugging the hole: three stacked outlines, each wider and fainter.
    ring(2.0f, 60, 9.0f, 15.0f);
    ring(1.0f, 95, 5.0f, 13.5f);
    // Water ripples: three rings that leave the hole every 1.8s, 1/3 phase
    // apart, fading quadratically as they travel. The old version just pulsed
    // the keyline's alpha, which read as a blink rather than a wave.
    for (int i = 0; i < 2; ++i)
    {
        const float phase = std::fmod(time * 0.42f + static_cast<float>(i) * 0.5f, 1.0f);
        const float grow = phase * 26.0f;
        const float fade = 1.0f - phase;
        // Subtle on purpose: this sits on top of a live settings card, so it
        // has to read as a highlight, not as an effect over the UI.
        ring(grow, static_cast<int>(fade * 26.0f), 8.0f, 12.0f + grow * 0.5f);
        ring(grow, static_cast<int>(fade * 74.0f), 1.8f, 12.0f + grow * 0.5f);
    }
    // The steady keyline on the hole itself.
    ring(0.0f, 232, 3.0f, 12.0f);

    if (!label.empty())
    {
        // The default atlas has no CJK glyphs: a Chinese label drawn with
        // ImGui::GetFont() comes out as a row of '?' (seen in the settings
        // spotlight). The story's own face is the game's body font.
        ImFont* font = game::bodyFont();
        if (font == nullptr)
        {
            return out;
        }
        const float size = 22.0f;
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
        const float w = ts.x + 28.0f;
        const float h = 38.0f;
        float x = (a.x + b.x) * 0.5f - w * 0.5f;
        float y = a.y - pad - h - 12.0f;
        bool above = true;
        if (y < 8.0f)
        {
            y = b.y + pad + 12.0f;
            above = false;
        }
        x = std::clamp(x, 8.0f, screen.x - w - 8.0f);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(38, 38, 58, 242), h * 0.5f);
        if (above)
        {
            dl->AddTriangleFilled(ImVec2(x + w * 0.5f - 7.0f, y + h), ImVec2(x + w * 0.5f + 7.0f, y + h),
                ImVec2(x + w * 0.5f, y + h + 9.0f), IM_COL32(38, 38, 58, 242));
        }
        dl->AddText(font, size, ImVec2(x + 14.0f, y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255),
            label.c_str());
        out.lo = ImVec2(x, y);
        out.hi = ImVec2(x + w, y + h);
        out.shown = true;
    }
    return out;
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
