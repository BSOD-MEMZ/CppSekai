// CppSekai - "LIVE CLEAR!" banner, see LiveClearBanner.hpp for the measurements.
#include "game/LiveClearBanner.hpp"

#include "imgui.h"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace game
{
namespace
{

// ---------------------------------------------------------------------------
// Virtual canvas (identical transform to the HUD / result screen).
// ---------------------------------------------------------------------------
constexpr float kCanvasW = 1920.0f;
constexpr float kCanvasH = 1080.0f;

struct Canvas
{
    ImDrawList* dl = nullptr;
    float scale = 1.0f;
    float ox = 0.0f;
    float oy = 0.0f;

    ImVec2 p(float vx, float vy) const { return ImVec2(ox + vx * scale, oy + vy * scale); }
    float s(float v) const { return v * scale; }
};

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

float clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

// Progress of a sub-span of the clip, 0 before `start` and 1 after start+dur.
float span(float t, float start, float dur)
{
    if (dur <= 0.0f)
    {
        return t >= start ? 1.0f : 0.0f;
    }
    return clamp01((t - start) / dur);
}

float smoothstep(float u)
{
    return u * u * (3.0f - 2.0f * u);
}

ImU32 withAlpha(float a)
{
    const int v = static_cast<int>(clamp01(a) * 255.0f + 0.5f);
    return IM_COL32(255, 255, 255, v);
}

// ImDrawCallback hooks: ImGui batches its quads with plain alpha blending, while
// the light-emitting half of the original (text_ADD, the flash, the particles)
// is additive. These two callbacks bracket that pass; ImGui resets the blend
// state every frame, so restoring alpha here is the whole cleanup.
void blendAdditive(const ImDrawList*, const ImDrawCmd*)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
}

void blendAlpha(const ImDrawList*, const ImDrawCmd*)
{
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// ---------------------------------------------------------------------------
// Clip timings (seconds) - clipped straight out of clip_result_LiveClear.
// ---------------------------------------------------------------------------
constexpr float kOn = 0.016667f;      // everything switches on
constexpr float kLettersDone = 0.183333f;
constexpr float kAddPeakAt = 0.183333f;
constexpr float kAddMidAt = 0.266667f;
constexpr float kAddMid = 0.509804f;
constexpr float kAddGoneAt = 0.683333f;
constexpr float kBackFrom = 0.183333f;
constexpr float kBackFullAt = 0.683333f;

// ---------------------------------------------------------------------------
// Atlas layout. tex_LiveClear.png is 1236x182 and holds the ten glyphs in
// reading order; the y column is the full texture height, so only u varies.
//   u0/u1 = the glyph's cell in the atlas (sprite-geometry.csv)
//   dw/dh = the size the prefab gives that Image node (hand-tuned per letter,
//           not a uniform scale - copying it is what makes the spacing right)
//   tx    = anchoredPosition.x of the node
// ---------------------------------------------------------------------------
constexpr float kAtlasW = 1236.0f;

struct Glyph
{
    const char* key;
    float u0;
    float u1;
    float dw;
    float dh;
    float tx;
};

const Glyph kGlyphs[] = {
    {"L", 0.0f, 117.0f, 99.0f, 147.0f, -446.0f},
    {"I", 118.0f, 170.0f, 39.0f, 147.0f, -368.0f},
    {"V", 171.0f, 316.0f, 127.0f, 147.0f, -277.7f},
    {"E", 318.0f, 439.0f, 107.0f, 147.0f, -156.0f},
    {"C", 508.0f, 649.0f, 123.0f, 152.0f, 21.6f},
    {"L", 650.0f, 766.0f, 99.0f, 147.0f, 136.4f},
    {"E", 767.0f, 888.0f, 106.0f, 147.0f, 246.3f},
    {"A", 890.0f, 1042.0f, 134.0f, 147.0f, 369.0f},
    {"R", 1045.0f, 1179.0f, 115.0f, 148.0f, 497.6f},
    {"!", 1182.0f, 1236.0f, 54.0f, 149.0f, 589.9f},
};
constexpr int kGlyphCount = static_cast<int>(sizeof(kGlyphs) / sizeof(kGlyphs[0]));

// The prefab spaces the row around x=+60.7 (that is where the backlight strip
// sits too), so the whole group has to be shifted by that much to read as
// centred on a 1920 canvas. Canvas coordinates start at the top-left corner, so
// the group centre is the canvas centre (960, 474), not the origin.
constexpr float kGroupCenterX = 960.0f;
constexpr float kGroupBiasX = -60.7f;
constexpr float kGroupCenterY = 474.0f;

constexpr float kBackW = 1148.0f;
constexpr float kBackH = 188.0f;

// Deterministic pseudo-random so a headless screenshot always matches.
float hash01(int i, int salt)
{
    const float s = std::sin(static_cast<float>(i) * 12.9898f + static_cast<float>(salt) * 78.233f);
    return s - std::floor(s);
}

// One soft blob built out of concentric discs - ImGui has no radial gradient and
// this is cheap enough at 1080p. `radius` is in canvas units.
void softBlob(const Canvas& c, float cx, float cy, float radius, float alpha)
{
    constexpr int kSteps = 20;
    for (int i = kSteps; i >= 1; --i)
    {
        const float f = static_cast<float>(i) / static_cast<float>(kSteps);
        // Squared falloff: bright core, long soft edge.
        const float a = alpha * (1.0f - f) * (1.0f - f);
        if (a <= 0.002f)
        {
            continue;
        }
        c.dl->AddCircleFilled(c.p(cx, cy), c.s(radius * f), withAlpha(a), 48);
    }
}

// `light`: startSize 15 units (= 1500 px across at the 100x UI scale), one burst
// at t=0, no speed, lifetime 0.8 s. This is the big flash behind the word - the
// thing the first pass was missing entirely, and what made the rest look off.
void drawGlow(const Canvas& c, float t)
{
    constexpr float kLife = 0.8f;
    if (t >= kLife)
    {
        return;
    }
    // Grows fast, holds, then fades; the original drives size and alpha over
    // the particle's lifetime.
    const float grow = smoothstep(clamp01(t / 0.16f));
    const float fade = 1.0f - smoothstep(clamp01((t - 0.12f) / (kLife - 0.12f)));
    const float radius = 620.0f * (0.35f + 0.65f * grow);
    softBlob(c, kGroupCenterX + kGroupBiasX, kGroupCenterY, radius, 0.05f * fade);
}

// `pt`: burst of 30 at 0.05 s, startSize 0.02..0.07 units (2..7 px),
// startSpeed 3 units (300 px/s), startLifetime 0.25..1 s.
void drawBurst(const Canvas& c, float t)
{
    constexpr int kSparks = 30;
    constexpr float kT0 = 0.05f;
    for (int i = 0; i < kSparks; ++i)
    {
        const float life = 0.25f + hash01(i, 2) * 0.75f;
        const float age = (t - kT0) / life;
        if (age < 0.0f || age > 1.0f)
        {
            continue;
        }
        const float ang = hash01(i, 3) * 6.2831853f;
        const float speed = 300.0f * (0.75f + hash01(i, 4) * 0.5f);
        const float dist = speed * age;
        const float x = kGroupCenterX + kGroupBiasX + std::cos(ang) * dist;
        // Squashed vertically: the word is a wide band, so a circular burst
        // reads as a halo rather than a spray.
        const float y = kGroupCenterY + std::sin(ang) * dist * 0.5f;
        const float a = (1.0f - age) * (1.0f - age) * 0.85f;
        const float r = (1.0f + hash01(i, 5) * 2.5f) * (1.0f - age * 0.6f);
        c.dl->AddCircleFilled(c.p(x, y), c.s(std::max(r, 0.5f)), withAlpha(a), 10);
    }
}

// `pt_loop`: rateOverTime 10/s, startSize 0.03..0.08 units (3..8 px), startSpeed
// 0 - they sit still and twinkle - lifetime 1..2 s, enabled at 0.5167 s.
void drawTrickle(const Canvas& c, float t)
{
    constexpr float kStart = 0.516667f;
    if (t < kStart)
    {
        return;
    }
    constexpr int kSparks = 16;
    for (int i = 0; i < kSparks; ++i)
    {
        const float t0 = kStart + static_cast<float>(i) * 0.1f;
        const float life = 1.0f + hash01(i, 11) * 1.0f;
        const float age = (t - t0) / life;
        if (age < 0.0f || age > 1.0f)
        {
            continue;
        }
        // Scattered across the word's band, not at its centre.
        const float x = kGroupCenterX + kGroupBiasX + (hash01(i, 12) - 0.5f) * 1150.0f;
        const float y = kGroupCenterY + (hash01(i, 13) - 0.5f) * 210.0f;
        const float a = std::sin(age * 3.14159265f) * 0.65f;
        const float r = 1.5f + hash01(i, 14) * 2.5f;
        c.dl->AddCircleFilled(c.p(x, y), c.s(r), withAlpha(a), 10);
    }
}

} // namespace

// Debug hook: pin the clip clock (CPSEKAI_BANNER_T=0.19) so a headless
// --screenshot can catch one exact frame of the 1.5 s animation. Unset in any
// normal run, so the banner advances by itself.
float LiveClearBanner::frozenTime()
{
    static const float value = [] {
        const char* raw = std::getenv("CPSEKAI_BANNER_T");
        if (raw == nullptr)
        {
            return -1.0f;
        }
        return static_cast<float>(std::atof(raw));
    }();
    return value;
}

void LiveClearBanner::start()
{
    mActive = true;
    mTime = std::max(frozenTime(), 0.0f);
}

void LiveClearBanner::reset()
{
    mActive = false;
    mTime = 0.0f;
}

void LiveClearBanner::update(float dt)
{
    if (!mActive || frozenTime() >= 0.0f)
    {
        return;
    }
    mTime = std::min(mTime + std::max(dt, 0.0f), kDuration);
}

float LiveClearBanner::blackout() const
{
    if (!mActive)
    {
        return 0.0f;
    }
    // Two stages, like the original: a translucent scrim comes down with the
    // letters (the stage stays readable behind them), it holds, and only then
    // does the screen go fully black for the hand-over to the result view.
    constexpr float kScrim = 0.5f;
    const float dim = kScrim * smoothstep(span(mTime, kOn, 0.23f));
    // The tail picks up where the scrim left off (0.5 -> 1.0), so there is no
    // dead stretch where the screen neither brightens nor darkens.
    const float fade = kScrim + (1.0f - kScrim) * smoothstep(span(mTime, 1.25f, kDuration - 1.25f));
    return std::max(dim, fade);
}

void LiveClearBanner::draw(platform::Renderer& renderer, int windowW, int windowH,
    float uiScale) const
{
    if (!mActive || mTime < kOn)
    {
        return;
    }
    const platform::Renderer::HudSprite* tex = renderer.hud("result_liveclear");
    if (tex == nullptr || tex->id == 0)
    {
        return;
    }
    const Canvas c = makeCanvas(ImGui::GetForegroundDrawList(), windowW, windowH, uiScale);
    const float t = mTime;

    // Letters slide out of a single point: same start, same end, different
    // distances (the clip has no scale curve at all).
    const float spread = smoothstep(span(t, kOn, kLettersDone - kOn));

    // Backlight strip first - it sits behind everything in the prefab.
    const platform::Renderer::HudSprite* back = renderer.hud("result_liveclear_light");
    if (back != nullptr && back->id != 0)
    {
        const float a = span(t, kBackFrom, kBackFullAt - kBackFrom);
        if (a > 0.002f)
        {
            const ImVec2 lo = c.p(kGroupCenterX - kBackW * 0.5f, kGroupCenterY - kBackH * 0.5f);
            const ImVec2 hi = c.p(kGroupCenterX + kBackW * 0.5f, kGroupCenterY + kBackH * 0.5f);
            c.dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(back->id)), lo,
                hi, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), withAlpha(a));
        }
    }

    // Light that lives *behind* the letters: the flash and the outward burst.
    // Additive, so it brightens the (already dimmed) stage instead of painting
    // an opaque disc over it; drawn before the glyphs so the word stays on top.
    c.dl->AddCallback(blendAdditive, nullptr);
    drawGlow(c, t);
    drawBurst(c, t);
    c.dl->AddCallback(blendAlpha, nullptr);

    const auto texId = reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex->id));
    for (int i = 0; i < kGlyphCount; ++i)
    {
        const Glyph& g = kGlyphs[i];
        const float cx = kGroupCenterX + kGroupBiasX + g.tx * spread;
        const float halfW = g.dw * 0.5f;
        const float halfH = g.dh * 0.5f;
        c.dl->AddImage(texId, c.p(cx - halfW, kGroupCenterY - halfH),
            c.p(cx + halfW, kGroupCenterY + halfH), ImVec2(g.u0 / kAtlasW, 0.0f),
            ImVec2(g.u1 / kAtlasW, 1.0f), IM_COL32(255, 255, 255, 255));
    }

    // Over the letters: the duplicate additive glyph (the "bloom" the original
    // gets from text_ADD) and the twinkling loop particles. Stacking alpha quads
    // can only lift a glyph towards white, never blow it out past it, which is
    // why this pass flips the blend mode instead.
    float addA = 0.0f;
    if (t < kAddMidAt)
    {
        addA = span(t, kOn, kAddPeakAt - kOn);
    }
    else if (t < kAddGoneAt)
    {
        addA = kAddMid * (1.0f - span(t, kAddMidAt, kAddGoneAt - kAddMidAt));
    }

    c.dl->AddCallback(blendAdditive, nullptr);
    if (addA > 0.002f)
    {
        for (int i = 0; i < kGlyphCount; ++i)
        {
            const Glyph& g = kGlyphs[i];
            const float cx = kGroupCenterX + kGroupBiasX + g.tx * spread;
            const float halfW = g.dw * 0.5f;
            const float halfH = g.dh * 0.5f;
            c.dl->AddImage(texId, c.p(cx - halfW, kGroupCenterY - halfH),
                c.p(cx + halfW, kGroupCenterY + halfH), ImVec2(g.u0 / kAtlasW, 0.0f),
                ImVec2(g.u1 / kAtlasW, 1.0f), withAlpha(addA));
        }
    }
    drawTrickle(c, t);
    c.dl->AddCallback(blendAlpha, nullptr);
}

} // namespace game
