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
#include "game/SongSelect.hpp" // difficultyColor()
#include "game/Ui.hpp"         // player level chip + exp bar

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
constexpr ImU32 kDarkPill = IM_COL32(68, 68, 102, 255);    // 歌曲等级 pill
constexpr ImU32 kCardTitle = IM_COL32(68, 69, 100, 255);
constexpr ImU32 kDigitGray = IM_COL32(144, 162, 174, 255);
constexpr ImU32 kDigitPink = IM_COL32(255, 118, 170, 255);
constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kNewRecord = IM_COL32(244, 143, 249, 255);
constexpr ImU32 kMint = IM_COL32(119, 237, 221, 255);
constexpr ImU32 kMintHover = IM_COL32(140, 245, 231, 255);
constexpr ImU32 kMintText = IM_COL32(47, 70, 90, 255);

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
// Ink sizes (not file sizes) of the two plate sprites, scaled to match the
// reference: the letter 108x126, the wordmark 81x9.7.
constexpr float kRankGlyphInkH = 126.0f;
constexpr float kRankLabelInkH = 9.8f;
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
// The rows reuse the game's own judgement words (judge/v3/<1..5>.png, the
// same sprites the in-game judge text uses - PERFECT even carries the rainbow
// gradient). Only their solid ink matters: the sprites have a glow margin and
// were measured with ImageMagick, see kJudgeSprite below.
constexpr float kRowLabelInkH = 30.0f;
constexpr float kRowNumberRight = 785.0f;
// The digits of the rows and the combo are the condensed face: its numerals
// match the reference's ink proportions (0.59 wide/high) than the heavy CJK
// face does (0.62).
constexpr float kRowNumberFontSize = 40.0f;
// Digits sit slightly above the centre of ImGui's line box; nudge them back
// down so their ink is centred on the row.
constexpr float kDigitBaselineNudge = 0.08f;
constexpr float kRowNumberAdvance = 19.75f;
constexpr float kComboLabelCenterX = 951.0f;
constexpr float kComboLabelSize = 38.0f;
constexpr float kComboLabelTracking = 0.5f;
// The combo count is the plain UI face too, just larger than the row counts.
constexpr float kComboRight = 1225.0f;
constexpr float kComboFontSize = 72.0f;
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

// Draws an overlay sprite so that its *solid* (non-glowing) part lands on the
// given box. The pjsk word/letter sprites carry a soft glow margin, so their
// file size is bigger than the visible glyphs; the ink box of every sprite
// used here was measured with ImageMagick (`-channel A -threshold 55% -trim`)
// and is passed in `inkX/inkY/inKW/inkH` in the sprite's own pixels.
void drawSpriteInk(const Canvas& c, const platform::Renderer::HudSprite* sprite, float inkX,
    float inkY, float inkW, float inkH, float boxX, float boxCenterY, float boxH, ImU32 tint)
{
    if (sprite == nullptr || sprite->id == 0 || inkH <= 0.0f || boxH <= 0.0f) {
        return;
    }
    const float scale = boxH / inkH;
    const float w = static_cast<float>(sprite->width) * scale;
    const float h = static_cast<float>(sprite->height) * scale;
    const float x = boxX - inkX * scale;
    const float y = boxCenterY - (inkY + inkH * 0.5f) * scale;
    c.dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(sprite->id)),
        c.p(x, y), c.p(x + w, y + h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
}

// Same, but centred horizontally on `centerX` instead of left-aligned.
void drawSpriteInkCentered(const Canvas& c, const platform::Renderer::HudSprite* sprite, float inkX,
    float inkY, float inkW, float inkH, float centerX, float centerY, float boxH, ImU32 tint)
{
    // inkW/inkH are in the sprite's own pixels, so the half width has to go
    // through the same scale as the draw itself.
    const float scale = boxH / inkH;
    drawSpriteInk(c, sprite, inkX, inkY, inkW, inkH, centerX - inkW * scale * 0.5f, centerY, boxH, tint);
}

// Right-aligned run of plain-font digits. The reference's judge counts and
// the combo read-out are the ordinary UI face (only the 8-digit score uses
// the game's own score numerals), so they are drawn as text - leading zeros
// gray, the rest white, each glyph centred in its own fixed slot.
void drawFontDigits(const Canvas& c, ImFont* font, float size, const std::string& text,
    float rightVx, float centerVy, float advance, ImU32 gray, ImU32 main, float alpha)
{
    const int count = static_cast<int>(text.size());
    if (font == nullptr || count == 0) {
        return;
    }
    std::size_t firstSignificant = 0;
    while (firstSignificant < text.size() && text[firstSignificant] == '0') {
        ++firstSignificant;
    }
    const float px = size * c.scale;
    const float top = textTopForCenterPx(font, px, c.y(centerVy), text);
    for (int i = 0; i < count; ++i) {
        const std::string glyph(1, text[static_cast<std::size_t>(i)]);
        const float w = font->CalcTextSizeA(px, FLT_MAX, 0.0f, glyph.c_str()).x;
        const float slotCenterX = rightVx - (static_cast<float>(count - i) - 0.5f) * advance;
        const ImU32 col = withAlpha(static_cast<std::size_t>(i) < firstSignificant ? gray : main, alpha);
        c.dl->AddText(font, px, ImVec2(c.x(slotCenterX) - w * 0.5f, top), col, glyph.c_str());
    }
}

// Draws a right-aligned run of pjsk score numerals (score/digit/*).
// Leading zeros go gray and the significant digits take `main` - the rule the
// reference follows for the score and the high score. `digitH` is the glyph
// height, `advance` the slot pitch; both come straight from the reference.
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
// `uiScale` is the user's UI scale (see UserSettings::uiScale): the 1920x1080
// canvas is zoomed about the centre of the window, so everything on this screen
// keeps its proportions. resultContinueHitTest has to be given the same value,
// otherwise the 继续 button stops matching what is drawn.
Canvas makeCanvas(ImDrawList* dl, int windowW, int windowH, float uiScale)
{
    Canvas c;
    c.dl = dl;
    c.scale = std::min(static_cast<float>(windowW) / kCanvasW,
        static_cast<float>(windowH) / kCanvasH) * std::clamp(uiScale, 0.5f, 2.0f);
    c.ox = (static_cast<float>(windowW) - kCanvasW * c.scale) * 0.5f;
    c.oy = (static_cast<float>(windowH) - kCanvasH * c.scale) * 0.5f;
    return c;
}

void drawResult(platform::Renderer& renderer, const ResultData& data, float elapsedSec,
    int windowW, int windowH, float uiScale)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const Canvas c = makeCanvas(dl, windowW, windowH, uiScale);

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
    //
    // The wash is deliberately not opaque: the stage plate that
    // renderer.renderFrame() drew below (the song's own backdrop, or the default
    // room) is supposed to stay visible through it. At 255 it buried the plate
    // completely; 0.7 keeps the screen readable and lets it read through.
    // -----------------------------------------------------------------------
    {
        const int a = static_cast<int>(appear * 255.0f * 0.70f);
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
        // The jacket ring is the difficulty colour too (it was sampled off an
        // EXPERT capture, so it used to be red for every difficulty).
        const ImU32 diffFill = difficultyColor(data.difficulty);
        const float jy = kJacketY + (1.0f - cardIn) * 6.0f;
        addRoundedRect(c, kJacketX, jy, kJacketSize, kJacketSize, kJacketRound,
            withAlpha(diffFill, alpha));
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

        // Difficulty capsule butted against the dark level capsule. Its colour
        // is the *actual* difficulty's (EASY green ... MASTER purple) - it used
        // to be a fixed pink, so an EASY clear still showed an EXPERT-red pill.
        const float pillY = kPillTop + (1.0f - cardIn) * 6.0f;
        const float round = kPillHeight * 0.5f;
        addRoundedRect(c, kCardTextX, pillY, kExpertPillW + kDiffPillW, kPillHeight, round,
            withAlpha(kDarkPill, alpha));
        addRoundedRect(c, kCardTextX, pillY, kExpertPillW, kPillHeight, round,
            withAlpha(diffFill, alpha));
        // Square off the coloured capsule's right end where it meets the dark one.
        dl->AddRectFilled(c.p(kCardTextX + kExpertPillW - round, pillY),
            c.p(kCardTextX + kExpertPillW, pillY + kPillHeight), withAlpha(diffFill, alpha));
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
        // Everything on the plate is the game's own art: score/rank/chr/<x>.png
        // for the big letter (224x266, solid 219x256 at +3+2) and
        // score/rank/txt/jp/<x>.png for the SCORERANK wordmark (1000x130,
        // solid 952x114 at +26+8). Placed by their *ink* boxes so the glow
        // margin does not shift them.
        const char letter = rank.rank >= 'a' && rank.rank <= 's' ? rank.rank : 'd';
        const std::string rankKey(1, letter);
        drawSpriteInkCentered(c, renderer.hud("rank_char_" + rankKey), 3.0f, 2.0f, 219.0f, 256.0f,
            plateCenterX, plateCenterY, kRankGlyphInkH * s, withAlpha(kWhite, pop));
        drawSpriteInkCentered(c, renderer.hud("rank_jp_" + rankKey), 26.0f, 8.0f, 952.0f, 114.0f,
            plateCenterX, kRankLabelCenterY, kRankLabelInkH * s, withAlpha(kWhite, pop));
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
            int value;
        };
        const Row rows[5] = {
            {data.perfect},
            {data.great},
            {data.good},
            {data.bad},
            {data.miss},
        };
        // Solid-ink boxes of judge/v3/<1..5>.png (measured with ImageMagick),
        // one per row: PERFECT GREAT GOOD BAD MISS.
        struct JudgeSprite
        {
            float inkX;
            float inkY;
            float inkW;
            float inkH;
        };
        const JudgeSprite judgeSprite[5] = {
            {17.0f, 16.0f, 277.0f, 49.0f},
            {16.0f, 16.0f, 206.0f, 49.0f},
            {16.0f, 16.0f, 185.0f, 50.0f},
            {17.0f, 17.0f, 128.0f, 47.0f},
            {17.0f, 16.0f, 150.0f, 49.0f},
        };
        for (int i = 0; i < 5; ++i) {
            const float alpha = easeOutCubic(span(t, 1.85f + static_cast<float>(i) * 0.07f, 0.35f));
            if (alpha <= 0.001f) {
                continue;
            }
            const float cy = kRowFirstCenterY + static_cast<float>(i) * kRowPitch;
            addRoundedRect(c, kTagX, cy - kTagH * 0.5f, kTagW, kTagH, kTagRound,
                withAlpha(kTagFill, alpha));
            // Label: the game's own judgement word (rainbow PERFECT included).
            const JudgeSprite& js = judgeSprite[i];
            drawSpriteInk(c, renderer.hud("judge_" + std::to_string(i + 1)), js.inkX, js.inkY,
                js.inkW, js.inkH, kRowLabelX, cy, kRowLabelInkH, withAlpha(kWhite, alpha));
            char value[16];
            std::snprintf(value, sizeof(value), "%04d", rows[i].value);
            drawFontDigits(c, cond, kRowNumberFontSize, value, kRowNumberRight,
                cy + kRowNumberFontSize * kDigitBaselineNudge, kRowNumberAdvance, kDigitGray, kWhite,
                alpha);

            if (i == 0) {
                textCenteredTracked(c, bold, kComboLabelSize, kComboLabelCenterX, cy,
                    withAlpha(kWhite, alpha), "COMBO", kComboLabelTracking);
                char combo[16];
                std::snprintf(combo, sizeof(combo), "%04d", data.maxCombo);
                drawFontDigits(c, cond, kComboFontSize, combo, kComboRight,
                    cy + kComboFontSize * kDigitBaselineNudge, kComboAdvance, kDigitGray, kWhite,
                    alpha);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Player level (the account). The chip is the same component the song
    // select draws, so the two screens cannot drift apart; the bar under it is
    // this run's progress towards the next rank.
    //
    // It sits on the free right-hand area below the strip panel - the phone
    // layout fills that with the character, the 16:9 one does not.
    // -----------------------------------------------------------------------
    {
        const float alpha = easeOutCubic(span(t, 1.80f, 0.35f));
        if (alpha > 0.004f) {
            const int vtxFirst = dl->VtxBuffer.Size;
            const float right = kCanvasW - kPlateRightInset;
            const float chipY = kPanelBottom + 62.0f + (1.0f - alpha) * 12.0f;
            ui::playerLevelChip(dl, bold, ImVec2(c.x(right), c.y(chipY)), data.playerRank, c.scale,
                true);

            constexpr float kBarW = 232.0f;
            const float barY = chipY + 76.0f;
            const float ratio = data.playerExpNeed > 0.0
                ? static_cast<float>(data.playerExp / data.playerExpNeed)
                : 1.0f;
            ui::expBar(dl, ImVec2(c.x(right - kBarW), c.y(barY)), c.s(kBarW), c.scale, ratio);

            char buf[64];
            ImU32 col = kMint;
            if (data.rankUps > 0) {
                std::snprintf(buf, sizeof(buf), "等级提升!  +%d EXP", data.expGain);
                col = kNewRecord;
            } else if (data.expGain > 0) {
                std::snprintf(buf, sizeof(buf), "+%d EXP", data.expGain);
            } else {
                std::snprintf(buf, sizeof(buf), "%d / %d", static_cast<int>(data.playerExp),
                    static_cast<int>(data.playerExpNeed));
            }
            textCentered(c, bold, 21.0f, right - kBarW * 0.5f, barY + 26.0f, col, buf);

            // Fade the whole block in by rewriting the alpha of the vertices it
            // just added (the chip has no alpha parameter of its own).
            const int a8 = static_cast<int>(alpha * 255.0f);
            if (a8 < 255) {
                for (int i = vtxFirst; i < dl->VtxBuffer.Size; ++i) {
                    ImU32& vcol = dl->VtxBuffer[i].col;
                    const ImU32 a = (vcol >> IM_COL32_A_SHIFT) & 0xFF;
                    vcol = (vcol & ~IM_COL32_A_MASK)
                        | (static_cast<ImU32>(a * a8 / 255) << IM_COL32_A_SHIFT);
                }
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

bool resultContinueHitTest(int windowW, int windowH, int x, int y, float uiScale)
{
    const Canvas c = makeCanvas(nullptr, windowW, windowH, uiScale);
    const float x0 = c.x(kCanvasW - kPanelRightInset - kBtnW);
    const float y0 = c.y(kBtnBottom - kBtnH);
    return static_cast<float>(x) >= x0 && static_cast<float>(x) <= x0 + c.s(kBtnW)
        && static_cast<float>(y) >= y0 && static_cast<float>(y) <= y0 + c.s(kBtnH);
}

} // namespace game
