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

// ImDrawCallback hooks: ImGui batches its quads with plain alpha blending, and
// the "text_ADD" layer in the original is a straight additive (SRC_ALPHA, ONE)
// composite. These two callbacks bracket the extra pass; the renderer resets the
// blend state every frame, so restoring alpha here is the whole cleanup.
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
//   x/w  = the glyph's cell in the atlas (sprite-geometry.csv)
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
// centred on a 1920 canvas. The vertical anchor is the `text` node's y=66.
// Canvas coordinates start at the top-left corner, so the group centre is the
// canvas centre (960, 474), not the origin.
constexpr float kGroupCenterX = 960.0f;
constexpr float kGroupBiasX = -60.7f;
constexpr float kGroupCenterY = 474.0f;

constexpr float kBackW = 1148.0f;
constexpr float kBackH = 188.0f;

// Deterministic pseudo-random so a headless screenshot always matches.
float hash01(int i, int salt){
    const float s = std::sin(static_cast<float>(i) * 12.9898f + static_cast<float>(salt) * 78.233f);
    return s - std::floor(s);
}

// One-off burst at 0.05 s: specks thrown outward from the word's centre. The
// original drives this with a ParticleSystem; the shipped effect is small
// enough that a deterministic radial spray reads the same at 1080p.
void drawBurst(const Canvas& c, float t)
{
    if (t < 0.05f || t > 1.05f)
    {
        return;
    }
    constexpr int kSparks = 26;
    for (int i = 0; i < kSparks; ++i)
    {
        const float t0 = 0.05f + hash01(i, 1) * 0.04f;
        const float life = 0.42f + hash01(i, 2) * 0.22f;
        const float age = (t - t0) / life;
        if (age < 0.0f || age > 1.0f)
        {
            continue;
        }
        const float ang = hash01(i, 3) * 6.2831853f;
        const float speed = 420.0f + hash01(i, 4) * 830.0f;
        // Fast out, then drifting: the original's particles have drag.
        const float dist = speed * (age - age * age * 0.35f);
        const float x = kGroupCenterX + kGroupBiasX + std::cos(ang) * dist;
        const float y = kGroupCenterY + std::sin(ang) * dist * 0.55f;
        const float a = (1.0f - age) * (1.0f - age);
        const float r = (2.6f + hash01(i, 5) * 5.2f) * (1.0f - age * 0.55f);
        c.dl->AddCircleFilled(c.p(x, y), c.s(std::max(r, 0.4f)), withAlpha(a), 12);
    }
}

// `pt_loop` from 0.5167 s: a slow trickle of tiny specks that keeps the word
// alive until the fade. Only a couple are on screen at once.
void drawTrickle(const Canvas& c, float t)
{
    if (t < 0.516667f)
    {
        return;
    }
    constexpr int kSparks = 8;
    for (int i = 0; i < kSparks; ++i)
    {
        const float t0 = 0.516667f + static_cast<float>(i) * 0.11f;
        constexpr float life = 0.62f;
        const float age = (t - t0) / life;
        if (age < 0.0f || age > 1.0f)
        {
            continue;
        }
        const float ang = hash01(i, 11) * 6.2831853f;
        const float speed = 60.0f + hash01(i, 12) * 90.0f;
        const float dist = speed * age;
        const float x = kGroupCenterX + kGroupBiasX + std::cos(ang) * dist;
        const float y = kGroupCenterY + std::sin(ang) * dist * 0.5f - age * 18.0f;
        const float a = (1.0f - age) * 0.55f;
        c.dl->AddCircleFilled(c.p(x, y), c.s(1.5f + hash01(i, 13) * 2.2f), withAlpha(a), 8);
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
    // Holds clear until 1.25 s, then hands the screen over in the clip's tail.
    return span(mTime, 1.25f, kDuration - 1.25f);
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

    drawBurst(c, t);
    drawTrickle(c, t);

    // Additive duplicate layer: peaks the moment the letters land, halves by
    // 0.267 s, gone by 0.683 s. ImGui draws every quad with plain alpha
    // blending, so the "add" read is produced by stacking the white glyph two
    // more times instead of switching the GL blend mode mid-list.
    float addA = 0.0f;
    if (t < kAddMidAt)
    {
        addA = span(t, kOn, kAddPeakAt - kOn);
    }
    else if (t < kAddGoneAt)
    {
        addA = kAddMid * (1.0f - span(t, kAddMidAt, kAddGoneAt - kAddMidAt));
    }

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

    // The additive duplicate: a real GL additive pass (ImDrawCallback flips the
    // blend mode for exactly these quads), because stacking alpha quads can only
    // lift the glyph towards white, never blow it out past it.
    if (addA > 0.002f)
    {
        c.dl->AddCallback(blendAdditive, nullptr);
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
        c.dl->AddCallback(blendAlpha, nullptr);
    }
}

} // namespace game
