// CppSekai - pjsk style result screen (see game/Result.hpp).
//
// All geometry below was measured off a 2388x1080 reference capture of the
// official result screen with a pixel scanner, then re-anchored into the
// 1920x1080 virtual canvas the HUD uses:
//   * left cluster (RESULT watermark, song card, score counter, judge rows)
//     keeps the reference's absolute x,
//   * right cluster (score bar, SCORERANK plate, 继续 button) hangs off the
//     strip panel's right edge with the reference's offsets.
// Element sizes are 1:1 with the reference. The score / combo / judge-count
// numerals are drawn from the game's own overlay sprites (score/digit,
// combo/p*), so they are the exact same font as the official screen.
#include "game/Result.hpp"

#include "game/Hud.hpp"
#include "game/Intro.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace game
{
namespace
{

// ---------------------------------------------------------------------------
// Palette (sampled from the reference).
// ---------------------------------------------------------------------------
constexpr ImU32 kPanelFill = IM_COL32(255, 255, 255, 150); // white @ ~59%
constexpr ImU32 kTagFill = IM_COL32(255, 255, 255, 16);    // judge row plate
constexpr ImU32 kPlateFill = IM_COL32(50, 50, 76, 255);    // SCORERANK plate
constexpr ImU32 kTrackFill = IM_COL32(50, 50, 76, 255);    // score bar track
constexpr ImU32 kPink = IM_COL32(255, 69, 119, 255);       // jacket / EXPERT
constexpr ImU32 kDarkPill = IM_COL32(68, 68, 102, 255);    // 歌曲等级 pill
constexpr ImU32 kCardTitle = IM_COL32(68, 69, 100, 255);
constexpr ImU32 kDigitGray = IM_COL32(144, 162, 174, 255);
constexpr ImU32 kDigitPink = IM_COL32(255, 118, 170, 255);
constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kNewRecord = IM_COL32(244, 143, 249, 255);
constexpr ImU32 kRankGlyph = IM_COL32(225, 138, 255, 255);
constexpr ImU32 kRankLabel = IM_COL32(240, 112, 240, 255);
constexpr ImU32 kMint = IM_COL32(119, 237, 221, 255);
constexpr ImU32 kMintHover = IM_COL32(140, 245, 231, 255);
constexpr ImU32 kMintText = IM_COL32(47, 70, 90, 255);
constexpr ImU32 kGreat = IM_COL32(238, 153, 255, 255);
constexpr ImU32 kGood = IM_COL32(118, 204, 255, 255);
constexpr ImU32 kBad = IM_COL32(120, 255, 188, 255);
constexpr ImU32 kMiss = IM_COL32(225, 225, 225, 255);

// ---------------------------------------------------------------------------
// Layout, in 1920x1080 virtual units.
// ---------------------------------------------------------------------------
constexpr float kCanvasW = 1920.0f;
constexpr float kCanvasH = 1080.0f;

// Top strip panel. In the reference its top edge sits above the screen, so the
// top corners are clipped - hence the negative y.
constexpr float kPanelLeft = 413.0f;
constexpr float kPanelRightInset = 15.0f;
constexpr float kPanelTop = -45.0f;
constexpr float kPanelBottom = 177.0f;
constexpr float kPanelRound = 50.0f;

// Score bar + rank plate, measured from the panel's right edge.
constexpr float kBarRightInset = 196.0f;
constexpr float kBarWidth = 531.0f;
constexpr float kBarHeight = 24.0f;
constexpr float kBarY = 80.0f;
// The marker positions come straight out of the bar sprite's 1650x76 design
// space - the same thresholds scoreRankAndBar() uses.
constexpr float kMarkerPos[4] = {746.0f / 1650.0f, 990.0f / 1650.0f, 1234.0f / 1650.0f,
    1478.0f / 1650.0f};
constexpr const char* kMarkerLetter[4] = {"C", "B", "A", "S"};
constexpr float kMarkerLetterSize = 30.0f;
constexpr float kMarkerLetterCenterY = 48.0f;
constexpr float kPinWideW = 25.0f;
constexpr float kPinNarrowW = 9.0f;
constexpr float kPinTop = 63.0f;
constexpr float kPinTip = 82.0f;
constexpr float kPinBottom = 105.0f;

constexpr float kPlateWidth = 120.0f;
constexpr float kPlateRightInset = 55.0f;
// The plate is clipped by the screen top in the reference, like the strip panel.
constexpr float kPlateTop = -20.0f;
constexpr float kPlateBottom = 175.0f;
constexpr float kPlateRound = 10.0f;
constexpr float kRankGlyphSize = 205.0f;
constexpr float kRankGlyphCenterY = 77.0f;
constexpr float kRankLabelSize = 17.0f;
constexpr float kRankLabelCenterY = 160.0f;

// Song card.
constexpr float kJacketX = 453.0f;
constexpr float kJacketY = 21.0f;
constexpr float kJacketSize = 135.0f;
constexpr float kJacketRound = 8.0f;
constexpr float kJacketBorder = 11.0f;
constexpr float kCardTextX = 615.0f;
constexpr float kTitleCenterY = 53.5f;
constexpr float kTitleSize = 40.0f;
constexpr float kPillTop = 96.0f;
constexpr float kPillHeight = 54.0f;
constexpr float kExpertPillW = 191.0f; // 615..806 (pink capsule)
constexpr float kDiffPillW = 190.0f;   // 806..996 (dark capsule)
constexpr float kPillTextSize = 38.0f;
constexpr float kPillTracking = 2.0f;
constexpr float kDiffTextSize = 36.0f;

// Score counter.
constexpr float kScoreRight = 1233.0f;
constexpr float kScoreCenterY = 359.5f;
constexpr float kScoreDigitH = 86.0f;
constexpr float kScoreAdvance = 61.6f;
constexpr float kScoreLabelCenterX = 546.0f;
constexpr float kScoreLabelCenterY = 355.0f;
constexpr float kScoreLabelSize = 70.0f;
constexpr float kNewRecordCenterX = 1100.0f;
constexpr float kNewRecordCenterY = 291.0f;
constexpr float kNewRecordSize = 33.0f;
constexpr float kSparkleOffset = 115.0f;
constexpr float kSparkleSize = 11.0f;
constexpr float kHighScoreRight = 1230.5f;
constexpr float kHighScoreCenterY = 458.5f;
constexpr float kHighScoreDigitH = 39.5f;
constexpr float kHighScoreAdvance = 28.5f;
constexpr float kHighScoreLabelCenterX = 537.0f;
constexpr float kHighScoreLabelCenterY = 456.0f;
constexpr float kHighScoreLabelSize = 36.0f;

// Judge rows.
constexpr float kRowFirstCenterY = 656.0f;
constexpr float kRowPitch = 64.6f;
constexpr float kTagX = 450.0f;
constexpr float kTagW = 372.0f;
constexpr float kTagH = 55.0f;
constexpr float kTagRound = 18.0f;
constexpr float kRowLabelX = 472.0f;
constexpr float kRowLabelSize = 46.0f;
constexpr float kRowLabelTracking = 3.2f;
constexpr float kRowNumberRight = 785.0f;
constexpr float kRowNumberH = 29.7f;
constexpr float kRowNumberAdvance = 19.75f;
constexpr float kComboLabelCenterX = 951.0f;
constexpr float kComboLabelSize = 38.0f;
constexpr float kComboLabelTracking = 0.5f;
constexpr float kComboRight = 1225.0f;
constexpr float kComboDigitH = 52.0f;
constexpr float kComboAdvance = 38.33f;

// The official result screen draws the score / combo numerals about 18%
// narrower than the overlay sprites they come from (the same glyphs, just
// condensed), so every digit run is squeezed horizontally by this factor.
constexpr float kDigitSqueeze = 0.82f;

// 继续 button: right edge flush with the panel, matching the reference's
// bottom-right placement.
constexpr float kBtnW = 320.0f;
constexpr float kBtnH = 79.0f;
constexpr float kBtnBottom = 1039.0f;
constexpr float kBtnTextSize = 42.0f;

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------
struct Canvas
{
    ImDrawList* dl = nullptr;
    float scale = 1.0f;
    float ox = 0.0f;
    float oy = 0.0f;

    [[nodiscard]] float x(float v) const { return ox + v * scale; }
    [[nodiscard]] float y(float v) const { return oy + v * scale; }
    [[nodiscard]] float s(float v) const { return v * scale; }
    [[nodiscard]] ImVec2 p(float vx, float vy) const { return ImVec2(x(vx), y(vy)); }
};

float clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

float easeOutCubic(float t)
{
    const float inv = 1.0f - clamp01(t);
    return 1.0f - inv * inv * inv;
}

float easeOutBack(float t)
{
    constexpr float c = 1.70158f;
    const float u = clamp01(t) - 1.0f;
    return 1.0f + (c + 1.0f) * u * u * u + c * u * u;
}

// 0..1 progress of the segment starting at `start` and lasting `dur` seconds.
float span(float t, float start, float dur)
{
    return clamp01((t - start) / std::max(dur, 1e-4f));
}

ImU32 withAlpha(ImU32 col, float alpha)
{
    const int a = static_cast<int>(
        clamp01(alpha) * static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFF));
    return (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

ImU32 lerpColor(ImU32 a, ImU32 b, float t)
{
    const float u = clamp01(t);
    const auto channel = [&](int shift) {
        const float ca = static_cast<float>((a >> shift) & 0xFF);
        const float cb = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<ImU32>(ca + (cb - ca) * u + 0.5f) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT)
        | (static_cast<ImU32>(255) << IM_COL32_A_SHIFT);
}

// The PERFECT row is the only multi-colour one in the reference: mint at the
// left, then light blue, lilac and pale yellow at the right.
ImU32 perfectRamp(float t)
{
    const float u = clamp01(t);
    if (u < 0.35f) {
        return lerpColor(IM_COL32(126, 247, 228, 255), IM_COL32(172, 205, 255, 255), u / 0.35f);
    }
    if (u < 0.62f) {
        return lerpColor(IM_COL32(172, 205, 255, 255), IM_COL32(227, 194, 251, 255),
            (u - 0.35f) / 0.27f);
    }
    return lerpColor(IM_COL32(227, 194, 251, 255), IM_COL32(255, 252, 211, 255), (u - 0.62f) / 0.38f);
}

std::string utf8Glyph(const std::string& text, std::size_t& cursor)
{
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
    const std::string glyph = text.substr(cursor, len);
    cursor += len;
    return glyph;
}

float textWidth(ImFont* font, float size, const std::string& text)
{
    if (font == nullptr || text.empty()) {
        return 0.0f;
    }
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

// ImGui lays a line out by its box (ascent + descent); the visible glyphs of
// an all-caps / CJK line sit a little above that box's centre, so everything
// here nudges up by the same fraction to keep the measured centrelines true.
// The line box is (ascent + descent); the visible glyphs of an all-caps / CJK
// line sit slightly above its centre, hence the fixed nudge. `pixelSize` and
// the returned value are in *pixels* - callers pass virtual units.
float textTopForCenterPx(ImFont* font, float pixelSize, float centerYpx, const std::string& text)
{
    const float boxH = font != nullptr && !text.empty()
        ? font->CalcTextSizeA(pixelSize, FLT_MAX, 0.0f, text.c_str()).y
        : pixelSize;
    return centerYpx - boxH * 0.5f - pixelSize * 0.035f;
}

void textLeftVC(const Canvas& c, ImFont* font, float size, float vx, float centerVy, ImU32 col,
    const std::string& text)
{
    if (font == nullptr || text.empty()) {
        return;
    }
    const float px = size * c.scale;
    c.dl->AddText(font, px, ImVec2(c.x(vx), textTopForCenterPx(font, px, c.y(centerVy), text)), col,
        text.c_str());
}

void textCentered(const Canvas& c, ImFont* font, float size, float cx, float cy, ImU32 col,
    const std::string& text)
{
    if (font == nullptr || text.empty()) {
        return;
    }
    const float px = size * c.scale;
    const float w = font->CalcTextSizeA(px, FLT_MAX, 0.0f, text.c_str()).x;
    c.dl->AddText(font, px,
        ImVec2(c.x(cx) - w * 0.5f, textTopForCenterPx(font, px, c.y(cy), text)), col, text.c_str());
}

// pjsk's UI face is a little wider than the closest Windows stand-in, so the
// wide label runs are letter-spaced to land on the measured word width.
std::size_t utf8Count(const std::string& text)
{
    std::size_t cursor = 0;
    std::size_t count = 0;
    while (cursor < text.size()) {
        utf8Glyph(text, cursor);
        ++count;
    }
    return count;
}

float trackedWidth(ImFont* font, float size, const std::string& text, float tracking)
{
    if (font == nullptr || text.empty()) {
        return 0.0f;
    }
    const int gaps = static_cast<int>(utf8Count(text)) - 1;
    return textWidth(font, size, text) + tracking * static_cast<float>(std::max(gaps, 0));
}

// Left-aligned text with letter spacing; each glyph is optionally coloured by
// `ramp` (nullptr = solid `col`).
void textTracked(const Canvas& c, ImFont* font, float size, float vx, float centerVy, ImU32 col,
    const std::string& text, float tracking, ImU32 (*ramp)(float))
{
    if (font == nullptr || text.empty()) {
        return;
    }
    const float px = size * c.scale;
    const float trackPx = tracking * c.scale;
    const ImVec2 base = c.p(vx, 0.0f);
    const float top = textTopForCenterPx(font, px, c.y(centerVy), text);
    const float total = std::max(trackedWidth(font, px, text, trackPx), 1.0f);
    float x = base.x;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::string glyph = utf8Glyph(text, cursor);
        const float w = font->CalcTextSizeA(px, FLT_MAX, 0.0f, glyph.c_str()).x;
        const ImU32 use = ramp != nullptr ? ramp(clamp01((x - base.x + w * 0.5f) / total)) : col;
        c.dl->AddText(font, px, ImVec2(x, top), use, glyph.c_str());
        x += w + trackPx;
    }
}

void textCenteredTracked(const Canvas& c, ImFont* font, float size, float cx, float centerVy,
    ImU32 col, const std::string& text, float tracking, ImU32 (*ramp)(float) = nullptr)
{
    const float w = trackedWidth(font, size, text, tracking);
    textTracked(c, font, size, cx - w * 0.5f, centerVy, col, text, tracking, ramp);
}

// Draws `text` with a per-glyph colour from `ramp` (0..1 across the string) -
// that is how the reference builds its rainbow PERFECT row.
// The backdrop is a bilinear wash of four corner colours; the RESULT
// watermark's outline needs its interior filled with exactly that colour, so
// the same interpolation is reproduced here.
ImU32 bgColorAt(float vx, float vy)
{
    const float u = clamp01(vx / kCanvasW);
    const float v = clamp01(vy / kCanvasH);
    const ImU32 tl = IM_COL32(46, 47, 74, 255);
    const ImU32 tr = IM_COL32(47, 47, 78, 255);
    const ImU32 br = IM_COL32(74, 62, 124, 255);
    const ImU32 bl = IM_COL32(48, 48, 72, 255);
    return lerpColor(lerpColor(tl, tr, u), lerpColor(bl, br, u), v);
}

// Outline (stroke-only) text: the glyph silhouette is stamped around a circle
// in `stroke`, then punched out with the backdrop colour - the way the
// reference's giant RESULT watermark is built.
void textOutlined(const Canvas& c, ImFont* font, float size, float vx, float centerVy,
    const std::string& text, ImU32 stroke, float thickness)
{
    if (font == nullptr || text.empty()) {
        return;
    }
    const float px = size * c.scale;
    const float top = textTopForCenterPx(font, px, c.y(centerVy), text);
    const ImVec2 base(c.x(vx), top);
    const float r = c.s(thickness);
    const int steps = 12;
    for (int i = 0; i < steps; ++i) {
        const float a = (static_cast<float>(i) / steps) * 6.2831853f;
        c.dl->AddText(font, px, ImVec2(base.x + std::cos(a) * r, base.y + std::sin(a) * r), stroke,
            text.c_str());
    }
    c.dl->AddText(font, px, base, bgColorAt(vx, centerVy), text.c_str());
}

// Faux-bold: stamping the glyph around a small circle fattens its strokes,
// which is how the reference's very heavy SCORERANK letter is approximated.
void textCenteredFauxBold(const Canvas& c, ImFont* font, float size, float cx, float cy, ImU32 col,
    const std::string& text, float thickness)
{
    if (font == nullptr || text.empty()) {
        return;
    }
    const float px = size * c.scale;
    const float w = font->CalcTextSizeA(px, FLT_MAX, 0.0f, text.c_str()).x;
    const float top = textTopForCenterPx(font, px, c.y(cy), text);
    const ImVec2 base(c.x(cx) - w * 0.5f, top);
    const float r = c.s(thickness);
    const int steps = 8;
    for (int i = 0; i < steps; ++i) {
        const float a = (static_cast<float>(i) / steps) * 6.2831853f;
        c.dl->AddText(font, px, ImVec2(base.x + std::cos(a) * r, base.y + std::sin(a) * r), col,
            text.c_str());
    }
    c.dl->AddText(font, px, base, col, text.c_str());
}

void addRoundedRect(const Canvas& c, float vx, float vy, float vw, float vh, float vround,
    ImU32 fill)
{
    c.dl->AddRectFilled(c.p(vx, vy), c.p(vx + vw, vy + vh), fill, c.s(vround));
}

// pjsk's sparkle: a four-pointed star drawn as its four concave arms.
void addSparkle(const Canvas& c, float vx, float vy, float radius, ImU32 col)
{
    const ImVec2 center = c.p(vx, vy);
    const float r = c.s(radius);
    const float inner = r * 0.2f;
    const ImVec2 up(center.x, center.y - r);
    const ImVec2 down(center.x, center.y + r);
    const ImVec2 left(center.x - r, center.y);
    const ImVec2 right(center.x + r, center.y);
    const ImVec2 iUp(center.x, center.y - inner);
    const ImVec2 iDown(center.x, center.y + inner);
    const ImVec2 iLeft(center.x - inner, center.y);
    const ImVec2 iRight(center.x + inner, center.y);
    c.dl->AddTriangleFilled(up, iRight, iLeft, col);
    c.dl->AddTriangleFilled(down, iLeft, iRight, col);
    c.dl->AddTriangleFilled(left, iUp, iDown, col);
    c.dl->AddTriangleFilled(right, iDown, iUp, col);
}

// Draws a right-aligned run of pjsk numeral sprites (score/digit, combo/p*).
// Leading zeros go gray and the significant digits take `main` - the rule the
// reference follows for the score, the high score, the combo and every judge
// row count. `digitH` is the glyph height, `advance` the slot pitch; both come
// straight from the reference measurements.
void drawDigitRun(const Canvas& c, platform::Renderer& renderer, const std::string& spritePrefix,
    const std::string& text, float rightVx, float centerVy, float digitH, float advance,
    ImU32 gray, ImU32 main, float alpha)
{
    const int count = static_cast<int>(text.size());
    if (count == 0 || digitH <= 0.0f) {
        return;
    }
    std::size_t firstSignificant = 0;
    while (firstSignificant < text.size() && text[firstSignificant] == '0') {
        ++firstSignificant;
    }
    const float top = centerVy - digitH * 0.5f;
    for (int i = 0; i < count; ++i) {
        const std::string key(1, text[static_cast<std::size_t>(i)]);
        const platform::Renderer::HudSprite* sprite = renderer.hud(spritePrefix + key);
        if (sprite == nullptr || sprite->id == 0) {
            continue;
        }
        const float aspect = sprite->height > 0
            ? static_cast<float>(sprite->width) / static_cast<float>(sprite->height)
            : 0.75f;
        const float w = digitH * aspect * kDigitSqueeze;
        const float slotCenterX = rightVx - (static_cast<float>(count - i) - 0.5f) * advance;
        const ImU32 col = withAlpha(static_cast<std::size_t>(i) < firstSignificant ? gray : main, alpha);
        c.dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(sprite->id)),
            c.p(slotCenterX - w * 0.5f, top), c.p(slotCenterX + w * 0.5f, top + digitH),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), col);
    }
}

} // namespace

// Canvas transform shared by the drawing code and resultContinueHitTest().
Canvas makeCanvas(ImDrawList* dl, int windowW, int windowH)
{
    Canvas c;
    c.dl = dl;
    c.scale = std::min(static_cast<float>(windowW) / kCanvasW,
        static_cast<float>(windowH) / kCanvasH);
    c.ox = (static_cast<float>(windowW) - kCanvasW * c.scale) * 0.5f;
    c.oy = (static_cast<float>(windowH) - kCanvasH * c.scale) * 0.5f;
    return c;
}

void drawResult(platform::Renderer& renderer, const ResultData& data, float elapsedSec,
    int windowW, int windowH)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const Canvas c = makeCanvas(dl, windowW, windowH);

    const float t = std::max(elapsedSec, 0.0f);
    const float appear = easeOutCubic(span(t, 0.0f, 0.30f));
    const float panelIn = easeOutCubic(span(t, 0.08f, 0.40f));
    const float panelDrop = (1.0f - panelIn) * -55.0f;
    const float cardIn = easeOutCubic(span(t, 0.20f, 0.30f));
    const float barFill = easeOutCubic(span(t, 0.45f, 0.90f));
    const float platePop = easeOutBack(span(t, 1.25f, 0.35f));
    const float scoreIn = easeOutCubic(span(t, 0.70f, 0.25f));
    const float countUp = easeOutCubic(span(t, 0.85f, 0.95f));
    const float recordIn = easeOutBack(span(t, 1.50f, 0.35f));
    const float highIn = easeOutBack(span(t, 1.62f, 0.35f));

    ImFont* bold = boldFont() != nullptr ? boldFont() : bodyFont();
    ImFont* cond = condensedFont() != nullptr ? condensedFont() : bodyFont();
    // The heavy CJK face also carries the latin labels: its cap width/height
    // ratio matches the reference's UI font (~0.83), which a latin-only
    // condensed face does not. `cond` stays in use for the RESULT watermark
    // and the score-bar marker letters, where the reference really is narrow.

    // -----------------------------------------------------------------------
    // Background: diagonal navy -> purple wash, the faint collage pattern and
    // the giant RESULT watermark.
    // -----------------------------------------------------------------------
    {
        const int a = static_cast<int>(appear * 255.0f);
        dl->AddRectFilledMultiColor(c.p(0.0f, 0.0f), c.p(kCanvasW, kCanvasH), IM_COL32(46, 47, 74, a),
            IM_COL32(47, 47, 78, a), IM_COL32(74, 62, 124, a), IM_COL32(48, 48, 72, a));
    }
    {
        // Decorative "photo frame" outlines + two wide diagonal bands: the
        // texture the official background art carries.
        const ImU32 line = IM_COL32(255, 255, 255, static_cast<int>(appear * 6.0f));
        const ImU32 band = IM_COL32(255, 255, 255, static_cast<int>(appear * 5.0f));
        const float frames[][4] = {
            {688.0f, 262.0f, 402.0f, 208.0f},
            {-80.0f, 250.0f, 360.0f, 390.0f},
            {1230.0f, 640.0f, 380.0f, 260.0f},
            {560.0f, 720.0f, 300.0f, 300.0f},
            {1500.0f, 120.0f, 300.0f, 420.0f},
            {120.0f, 860.0f, 420.0f, 240.0f},
        };
        for (const auto& f : frames) {
            dl->AddRect(c.p(f[0], f[1]), c.p(f[0] + f[2], f[1] + f[3]), line, c.s(18.0f), 0,
                c.s(2.0f));
        }
        dl->AddTriangleFilled(c.p(-120.0f, 620.0f), c.p(760.0f, -60.0f), c.p(980.0f, 300.0f), band);
        dl->AddTriangleFilled(c.p(1920.0f, 60.0f), c.p(1420.0f, 1080.0f), c.p(1920.0f, 1080.0f), band);
    }
    {
        // Giant RESULT watermark: outline-only letters on a fixed 166u advance
        // with their cap tops at y=20 and the letter bodies nearly touching.
        constexpr const char* kLetters[6] = {"R", "E", "S", "U", "L", "T"};
        constexpr float kLetterSize = 322.0f;
        constexpr float kAdvance = 166.0f;
        constexpr float kInkCenterY = 101.0f;
        const ImU32 stroke = IM_COL32(255, 255, 255, static_cast<int>(appear * 56.0f));
        for (int i = 0; i < 6; ++i) {
            textOutlined(c, bold, kLetterSize, -9.0f + static_cast<float>(i) * kAdvance,
                kInkCenterY + kLetterSize * 0.03f, kLetters[i], stroke, 3.0f);
        }
    }

    // -----------------------------------------------------------------------
    // Top strip panel.
    // -----------------------------------------------------------------------
    const float panelRight = kCanvasW - kPanelRightInset;
    addRoundedRect(c, kPanelLeft, kPanelTop + panelDrop, panelRight - kPanelLeft,
        kPanelBottom - kPanelTop, kPanelRound, withAlpha(kPanelFill, panelIn));

    // -----------------------------------------------------------------------
    // Song card: jacket + title + difficulty pill + level pill.
    // -----------------------------------------------------------------------
    {
        const float alpha = cardIn;
        const float jy = kJacketY + (1.0f - cardIn) * 6.0f;
        addRoundedRect(c, kJacketX, jy, kJacketSize, kJacketSize, kJacketRound,
            withAlpha(kPink, alpha));
        const platform::Renderer::HudSprite* cover = renderer.cover();
        if (cover != nullptr && cover->id != 0) {
            const float inset = kJacketBorder;
            const float iw = kJacketSize - inset * 2.0f;
            dl->AddImageRounded(
                reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(cover->id)),
                c.p(kJacketX + inset, jy + inset), c.p(kJacketX + inset + iw, jy + inset + iw),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), withAlpha(kWhite, alpha), c.s(3.0f));
        }
        textTracked(c, bold, kTitleSize, kCardTextX, kTitleCenterY, withAlpha(kCardTitle, alpha),
            data.title, 1.0f, nullptr);

        // Difficulty capsule butted against the dark level capsule.
        const float pillY = kPillTop + (1.0f - cardIn) * 6.0f;
        const float round = kPillHeight * 0.5f;
        addRoundedRect(c, kCardTextX, pillY, kExpertPillW + kDiffPillW, kPillHeight, round,
            withAlpha(kDarkPill, alpha));
        addRoundedRect(c, kCardTextX, pillY, kExpertPillW, kPillHeight, round,
            withAlpha(kPink, alpha));
        // Square off the pink capsule's right end where it meets the dark one.
        dl->AddRectFilled(c.p(kCardTextX + kExpertPillW - round, pillY),
            c.p(kCardTextX + kExpertPillW, pillY + kPillHeight), withAlpha(kPink, alpha));
        textCenteredTracked(c, bold, kPillTextSize, kCardTextX + kExpertPillW * 0.5f,
            pillY + kPillHeight * 0.5f, withAlpha(kWhite, alpha), data.difficulty, kPillTracking);

        const float levelCenterX = kCardTextX + kExpertPillW + kDiffPillW * 0.5f;
        const float levelCenterY = pillY + kPillHeight * 0.5f;
        const std::string levelLabel = "歌曲等级";
        const float labelW = textWidth(bold, kDiffTextSize, levelLabel);
        const float numberW = trackedWidth(bold, kPillTextSize, data.level, kPillTracking);
        constexpr float gap = 12.0f;
        const float startX = levelCenterX - (labelW + gap + numberW) * 0.5f;
        textLeftVC(c, bold, kDiffTextSize, startX, levelCenterY, withAlpha(kWhite, alpha * 0.92f),
            levelLabel);
        textTracked(c, bold, kPillTextSize, startX + labelW + gap, levelCenterY,
            withAlpha(kWhite, alpha), data.level, kPillTracking, nullptr);
    }

    // -----------------------------------------------------------------------
    // Score bar + rank markers + SCORERANK plate.
    // -----------------------------------------------------------------------
    const ScoreRank rank = scoreRankAndBar(data.score, data.chartRating);
    {
        const float alpha = panelIn;
        const float barRight = panelRight - kBarRightInset;
        const float barLeft = barRight - kBarWidth;
        addRoundedRect(c, barLeft, kBarY, kBarWidth, kBarHeight, kBarHeight * 0.5f,
            withAlpha(kTrackFill, alpha));
        const float fillRatio = clamp01(rank.bar) * barFill;
        const platform::Renderer::HudSprite* bar = renderer.hud("score_bar");
        if (bar != nullptr && bar->id != 0 && fillRatio > 0.001f) {
            // The sprite is a full rounded gradient bar, so squeezing it into
            // the filled width reproduces the reference exactly (the gradient
            // reaches magenta right where the fill stops).
            // The sprite's own caps are rounded, so it is drawn one cap wider
            // and clipped: the fill then ends on a straight edge exactly where
            // the reference's does.
            const float cap = kBarHeight * 0.5f;
            dl->PushClipRect(c.p(barLeft, kBarY - 1.0f),
                c.p(barLeft + kBarWidth * fillRatio, kBarY + kBarHeight + 1.0f), true);
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(bar->id)),
                c.p(barLeft, kBarY), c.p(barLeft + kBarWidth * fillRatio + cap, kBarY + kBarHeight),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), withAlpha(kWhite, alpha));
            dl->PopClipRect();
        }
        // Rank markers: a white pin (wide notch tapering into a stem) with the
        // letter above it.
        for (int i = 0; i < 4; ++i) {
            const float mx = barLeft + kBarWidth * kMarkerPos[i];
            const ImU32 col = withAlpha(kWhite, alpha);
            dl->AddQuadFilled(c.p(mx - kPinWideW * 0.5f, kPinTop), c.p(mx + kPinWideW * 0.5f, kPinTop),
                c.p(mx + kPinNarrowW * 0.5f, kPinTip), c.p(mx - kPinNarrowW * 0.5f, kPinTip), col);
            dl->AddRectFilled(c.p(mx - kPinNarrowW * 0.5f, kPinTip),
                c.p(mx + kPinNarrowW * 0.5f, kPinBottom), col);
            textCentered(c, cond, kMarkerLetterSize, mx, kMarkerLetterCenterY, col,
                kMarkerLetter[i]);
        }

        // SCORERANK plate (pops in once the bar has filled).
        const float plateRight = panelRight - kPlateRightInset;
        const float plateCenterX = plateRight - kPlateWidth * 0.5f;
        const float plateCenterY = (kPlateTop + kPlateBottom) * 0.5f;
        const float ph = kPlateBottom - kPlateTop;
        const float pop = platePop;
        const float s = 0.80f + 0.20f * pop;
        addRoundedRect(c, plateCenterX - kPlateWidth * 0.5f * s, plateCenterY - ph * 0.5f * s,
            kPlateWidth * s, ph * s, kPlateRound, withAlpha(kPlateFill, pop));
        // scoreRankAndBar() returns a lower-case letter; the plate shows it
        // upper-case like the official screen.
        const char letter = rank.rank >= 'a' && rank.rank <= 's' ? rank.rank : 'd';
        const std::string rankText(1, static_cast<char>(std::toupper(static_cast<unsigned char>(letter))));
        textCenteredFauxBold(c, bold, kRankGlyphSize * s, plateCenterX,
            plateCenterY + (kRankGlyphCenterY - plateCenterY) * s, withAlpha(kRankGlyph, pop),
            rankText, 2.2f);
        textCentered(c, bold, kRankLabelSize * s, plateCenterX,
            plateCenterY + (kRankLabelCenterY - plateCenterY) * s, withAlpha(kRankLabel, pop),
            "SCORERANK");
    }

    // -----------------------------------------------------------------------
    // Score counter: eight zero-padded digits. The padding is gray and the
    // significant digits are pink on a new record (white otherwise) - the
    // whole 新纪录! effect.
    // -----------------------------------------------------------------------
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%08lld",
            static_cast<long long>(data.score * countUp + 0.5));
        drawDigitRun(c, renderer, "digit_", std::string(buffer), kScoreRight, kScoreCenterY,
            kScoreDigitH, kScoreAdvance, kDigitGray, data.newRecord ? kDigitPink : kWhite, scoreIn);
        textCentered(c, bold, kScoreLabelSize, kScoreLabelCenterX, kScoreLabelCenterY,
            withAlpha(kWhite, scoreIn), "得分");

        if (data.newRecord) {
            const float pop = recordIn;
            const float s = 0.6f + 0.4f * pop;
            textCentered(c, bold, kNewRecordSize * s, kNewRecordCenterX, kNewRecordCenterY,
                withAlpha(kNewRecord, pop), "新纪录!");
            addSparkle(c, kNewRecordCenterX - kSparkleOffset, kNewRecordCenterY,
                kSparkleSize * s, withAlpha(kNewRecord, pop));
            addSparkle(c, kNewRecordCenterX + kSparkleOffset, kNewRecordCenterY,
                kSparkleSize * s, withAlpha(kNewRecord, pop));
        }

        std::snprintf(buffer, sizeof(buffer), "%08lld",
            static_cast<long long>(data.highScore + 0.5));
        drawDigitRun(c, renderer, "digit_", std::string(buffer), kHighScoreRight, kHighScoreCenterY,
            kHighScoreDigitH, kHighScoreAdvance, kDigitGray, kWhite, highIn);
        textCentered(c, bold, kHighScoreLabelSize, kHighScoreLabelCenterX,
            kHighScoreLabelCenterY, withAlpha(kWhite, highIn), "最高得分");
    }

    // -----------------------------------------------------------------------
    // Judge rows (+ the combo read-out on the first row).
    // -----------------------------------------------------------------------
    {
        struct Row
        {
            const char* label;
            int value;
            ImU32 color;
        };
        const Row rows[5] = {
            {"PERFECT", data.perfect, kWhite},
            {"GREAT", data.great, kGreat},
            {"GOOD", data.good, kGood},
            {"BAD", data.bad, kBad},
            {"MISS", data.miss, kMiss},
        };
        for (int i = 0; i < 5; ++i) {
            const float alpha = easeOutCubic(span(t, 1.85f + static_cast<float>(i) * 0.07f, 0.35f));
            if (alpha <= 0.001f) {
                continue;
            }
            const float cy = kRowFirstCenterY + static_cast<float>(i) * kRowPitch;
            addRoundedRect(c, kTagX, cy - kTagH * 0.5f, kTagW, kTagH, kTagRound,
                withAlpha(kTagFill, alpha));
            if (i == 0) {
                textTracked(c, bold, kRowLabelSize, kRowLabelX, cy, kWhite, rows[i].label,
                    kRowLabelTracking, perfectRamp);
            } else {
                textTracked(c, bold, kRowLabelSize, kRowLabelX, cy, withAlpha(rows[i].color, alpha),
                    rows[i].label, kRowLabelTracking, nullptr);
            }
            char value[16];
            std::snprintf(value, sizeof(value), "%04d", rows[i].value);
            drawDigitRun(c, renderer, "digit_", value, kRowNumberRight, cy, kRowNumberH,
                kRowNumberAdvance, kDigitGray, kWhite, alpha);

            if (i == 0) {
                textCenteredTracked(c, bold, kComboLabelSize, kComboLabelCenterX, cy,
                    withAlpha(kWhite, alpha), "COMBO", kComboLabelTracking);
                char combo[16];
                std::snprintf(combo, sizeof(combo), "%04d", data.maxCombo);
                drawDigitRun(c, renderer, "combo_digit_n_", combo, kComboRight, cy, kComboDigitH,
                    kComboAdvance, kDigitGray, kWhite, alpha);
            }
        }
    }

    // -----------------------------------------------------------------------
    // 继续 button (the press itself is handled by resultContinueHitTest() in
    // the SDL event path; here it only gets drawn, with a hover highlight).
    // -----------------------------------------------------------------------
    {
        const float alpha = easeOutCubic(span(t, 2.20f, 0.35f));
        const float x0 = panelRight - kBtnW;
        const float y0 = kBtnBottom - kBtnH;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool hovered = mouse.x >= c.x(x0) && mouse.x <= c.x(x0 + kBtnW) && mouse.y >= c.y(y0)
            && mouse.y <= c.y(y0 + kBtnH);
        addRoundedRect(c, x0, y0, kBtnW, kBtnH, kBtnH * 0.5f,
            withAlpha(hovered ? kMintHover : kMint, alpha));
        textCentered(c, bold, kBtnTextSize, x0 + kBtnW * 0.5f, y0 + kBtnH * 0.5f,
            withAlpha(kMintText, alpha), "继续");
    }
}

bool resultContinueHitTest(int windowW, int windowH, int x, int y)
{
    const Canvas c = makeCanvas(nullptr, windowW, windowH);
    const float x0 = c.x(kCanvasW - kPanelRightInset - kBtnW);
    const float y0 = c.y(kBtnBottom - kBtnH);
    return static_cast<float>(x) >= x0 && static_cast<float>(x) <= x0 + c.s(kBtnW)
        && static_cast<float>(y) >= y0 && static_cast<float>(y) <= y0 + c.s(kBtnH);
}

} // namespace game
