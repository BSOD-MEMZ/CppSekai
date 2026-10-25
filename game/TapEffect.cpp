// CppSekai - PJSK style global tap feedback. See TapEffect.hpp for the
// provenance of every number here, and RE-report-jp.md section 11 for the raw
// YAML these were read from.
#include "TapEffect.hpp"

#include "Ui.hpp"
#include "platform/Renderer.hpp"

#include <GL/gl.h>

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace game
{
namespace
{
// The only numbers in this file that are not measured. Literal 1:1 mapping
// (PT_01 = 160 design px) reads too big on our 1920x1080 canvas, and the shards
// cannot shrink as far as the ring or they turn into specks. So they scale
// separately: kRingScale for the ripple, the glow and the emission radius,
// kShardScale for the triangle diameters. Both 1.0 = literal values.
constexpr float kRingScale = 0.45f;
constexpr float kShardScale = 0.60f;

// --- tap (fx_Tap_single) -------------------------------------------------
// PT_01: startSize 160, life 0.5, 4x4 WholeSheet at 30 fps, rate 0, transform 1.
constexpr float kTapRing = 160.0f * kRingScale;
constexpr float kTapRingLife = 0.50f;

// --- hold (Follow) -------------------------------------------------------
// Follow also carries a PT_01 (330, looping every 0.45s) but its rateOverTime is
// 0 and the game never Play()s it, so a held press shows no ring at all - only
// the 650px glow and the orbiting shards. Verified against the real game.
constexpr float kHoldGlow = 650.0f * kRingScale;
// The original spawns a glow every 0.25s with a 0.5s life, so ~2 overlap. One
// steady quad at this alpha matches that without blowing out on a pale backdrop.
constexpr float kHoldGlowAlpha = 0.22f;
// All three shard systems run rateOverTime 6/s.
constexpr float kHoldShardRate = 6.0f;

constexpr int kTiles = 4;
constexpr int kFrames = 16;

// The originals' startSpeed points along the disc normal (out of the screen).
// An orthographic 2D view cannot show that, so this reinterprets it as outward
// drift; 0 keeps the particles perfectly still (the literal reading).
constexpr float kRadialGain = 1.0f;
// RotationOverLifetime has separateAxes = 0, so only Z actually spins. X and Y
// are just startRotation, i.e. a fixed 3D tilt per particle: that tilt is what
// makes the shards look like tumbling 3D cards instead of flat stickers.
constexpr float kSpinRate = 3.14159265f; // pi rad/s on Z

struct ColorKey
{
    float t;
    float r;
    float g;
    float b;
};

struct AlphaKey
{
    float t;
    float v;
};

struct ShardGradient
{
    const ColorKey* colors;
    int colorCount;
    const AlphaKey* alphas;
    int alphaCount;
};

// particle3: colour drifts pale slate -> deep slate across the whole life;
// alpha jumps to 0.5 by 25%, holds, then fades out over the last 30%.
constexpr ColorKey kBigColors[] = {
    {0.000f, 0.394f, 0.423f, 0.519f},
    {1.000f, 0.138f, 0.175f, 0.302f},
};
constexpr AlphaKey kBigAlphas[] = {
    {0.000f, 0.000f},
    {0.250f, 0.502f},
    {0.700f, 0.502f},
    {1.000f, 0.000f},
};
// particle2 / particle2_1: plain white, snaps opaque in the first 5%, fades at end.
constexpr ColorKey kMidColors[] = {
    {0.000f, 1.000f, 1.000f, 1.000f},
    {1.000f, 1.000f, 1.000f, 1.000f},
};
constexpr AlphaKey kMidAlphas[] = {
    {0.000f, 0.000f},
    {0.047f, 1.000f},
    {0.338f, 1.000f},
    {0.700f, 1.000f},
    {1.000f, 0.000f},
};

constexpr ShardGradient kGradBig = {kBigColors, 2, kBigAlphas, 4};
constexpr ShardGradient kGradMid = {kMidColors, 2, kMidAlphas, 5};

struct TriSpec
{
    // startSize is Random Between Two Curves in every shard system, so each
    // particle rolls its own diameter. sizeMin is the minCurve * minScalar side.
    float sizeMin;
    float sizeMax;
    float lifeMin;
    float lifeMax;
    float radius;   // emission ring radius (shape radius * transform scale)
    float speedMin; // startSpeed, world units/s
    float speedMax;
    int count;
    const ShardGradient* grad; // nullptr = no ColorModule (keep startColour)
};

// fx_Tap_single: one particle per system, all at t=0, thrown outward fast.
constexpr int kTapSpecCount = 4;
constexpr TriSpec kTapSpecs[kTapSpecCount] = {
    // particle3. Measured range is 2.5..235 px (minCurve 0.08 * minScalar 0.2 on
    // the low side). Both ends are trimmed by hand: 2.5px reads as a dirty speck
    // and 235px is 1.5x the ring, which looks oversized. 40..130 -> 24..78 px
    // after kFxScale, i.e. a quarter to four-fifths of the ripple.
    {40.0f, 130.0f, 0.30f, 0.70f, 157.0f, 35.0f, 100.0f, 1, &kGradBig},
    // particle2: 15.7..18.2
    {15.7f, 18.2f, 0.10f, 0.70f, 157.0f, 35.0f, 100.0f, 1, &kGradMid},
    // particle2_1: measured 1.2..11.7, floor raised to 6 for the same reason
    {6.0f, 11.7f, 0.20f, 0.70f, 157.0f, 35.0f, 100.0f, 1, &kGradMid},
    // particle1: 1.6..7.9, emits at the tap point (its shape module is off).
    // Original life is 30..35s; clamped to 2s, a 35s speck is not feedback.
    {1.6f, 7.9f, 0.60f, 2.00f, 0.0f, 1.0f, 20.0f, 2, nullptr},
};

// Follow / fx_Tap_loop: the same three shard systems at 6/s, but small, slow and
// long-lived (transform scale 210 instead of 157).
constexpr int kHoldSpecCount = 3;
constexpr TriSpec kHoldSpecs[kHoldSpecCount] = {
    {42.0f, 147.0f, 0.30f, 1.50f, 210.0f, 0.0f, 0.45f, 1, &kGradBig},
    {10.5f, 21.0f, 0.30f, 1.50f, 210.0f, 0.0f, 0.45f, 1, &kGradMid},
    {2.1f, 10.5f, 0.60f, 2.00f, 210.0f, 5.0f, 40.0f, 1, nullptr},
};

constexpr float kTwoPi = 6.28318530717959f;
constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);

ImU32 withAlpha(ImU32 color, float alpha)
{
    const auto a = static_cast<ImU32>(alpha * 255.0f + 0.5f);
    return (color & 0x00FFFFFFu) | (a << 24);
}

float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float easeOutCubic(float t)
{
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// Unity stores material/particle colours in linear space; our vertex colours are
// sRGB. Without this the big triangles come out as near-black slabs.
int srgbOf(float linear)
{
    const float c = linear <= 0.0031308f ? linear * 12.92f
                                         : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<int>(clamp01(c) * 255.0f + 0.5f);
}

// Linear interpolation over the measured ColorOverLifetime keys. Interpolation
// happens in linear space (Unity's own behaviour), conversion is last.
void evalGradient(const ShardGradient& g, float t, float& r, float& gr, float& b, float& a)
{
    r = gr = b = a = 1.0f;
    if (g.colorCount > 0) {
        const ColorKey* k = g.colors;
        if (t <= k[0].t || g.colorCount == 1) {
            r = k[0].r;
            gr = k[0].g;
            b = k[0].b;
        } else if (t >= k[g.colorCount - 1].t) {
            r = k[g.colorCount - 1].r;
            gr = k[g.colorCount - 1].g;
            b = k[g.colorCount - 1].b;
        } else {
            for (int i = 1; i < g.colorCount; ++i) {
                if (t <= k[i].t) {
                    const float span = k[i].t - k[i - 1].t;
                    const float f = span > 1e-5f ? (t - k[i - 1].t) / span : 1.0f;
                    r = k[i - 1].r + (k[i].r - k[i - 1].r) * f;
                    gr = k[i - 1].g + (k[i].g - k[i - 1].g) * f;
                    b = k[i - 1].b + (k[i].b - k[i - 1].b) * f;
                    break;
                }
            }
        }
    }
    if (g.alphaCount > 0) {
        const AlphaKey* k = g.alphas;
        if (t <= k[0].t || g.alphaCount == 1) {
            a = k[0].v;
        } else if (t >= k[g.alphaCount - 1].t) {
            a = k[g.alphaCount - 1].v;
        } else {
            for (int i = 1; i < g.alphaCount; ++i) {
                if (t <= k[i].t) {
                    const float span = k[i].t - k[i - 1].t;
                    const float f = span > 1e-5f ? (t - k[i - 1].t) / span : 1.0f;
                    a = k[i - 1].v + (k[i].v - k[i - 1].v) * f;
                    break;
                }
            }
        }
    }
}

// Deterministic PRNG so --tap-fx-test replays the exact same burst. xorshift32:
// an LCG's low bits have a short period and its first draws cluster near zero,
// which made every test burst roll the smallest possible shard.
unsigned nextRandom(unsigned& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float randomRange(unsigned& state, float lo, float hi)
{
    const float unit = static_cast<float>(nextRandom(state)) / 4294967295.0f;
    return lo + (hi - lo) * unit;
}

// pjsk draws the ripple additively (it glows over the dimmed backdrop). The
// ImGui GL3 backend sets blending once per draw data, so a draw-list callback
// has to switch it and put it back.
void beginAdditive(const ImDrawList*, const ImDrawCmd*)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
}

void endAdditive(const ImDrawList*, const ImDrawCmd*)
{
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

} // namespace

bool TapEffect::load(platform::Renderer& renderer, const std::string& fxDir, std::string& outError)
{
    struct Entry
    {
        const char* file;
        ImTextureID* out;
        bool required;
    };
    const Entry entries[] = {
        {"tap_sheet.png", &mSheet, true}, // 4x4 main ripple (tex_tap_texturesheet)
        {"tap_tri.png", &mTri, false},    // 2x2 colour triangles (tex_sekai_01_2)
        {"tap_flare.png", &mGlow, false}, // 650px hold glow (tex_tap_flare)
        {"tap_ring.png", &mRing, false},  // legacy fallback ring
    };

    bool anyLoaded = false;
    for (const Entry& entry : entries) {
        const std::string path = fxDir + "\\" + entry.file;
        std::string error;
        const GLuint id = renderer.loadUiTexture(path, error);
        std::fflush(stdout);
        if (id == 0) {
            if (entry.required) {
                outError = error;
            }
            *entry.out = 0;
            continue;
        }
        *entry.out = static_cast<ImTextureID>(id);
        anyLoaded = true;
    }
    return anyLoaded;
}

void TapEffect::buildTapShards(Instance& instance, unsigned& rng) const
{
    // Every shard system in fx_Tap_single runs rateOverTime 1/s over a 0.5s
    // lifetime, so Unity emits exactly one particle at t=0. Nothing is
    // staggered: the whole burst appears on the click.
    for (int specIndex = 0; specIndex < kTapSpecCount; ++specIndex) {
        const TriSpec& spec = kTapSpecs[specIndex];
        for (int i = 0; i < spec.count; ++i) {
            Shard shard;
            shard.born = 0.0f;
            shard.life = randomRange(rng, spec.lifeMin, spec.lifeMax);
            // Circle shape, radiusThickness 0: emitted on the ring itself.
            shard.radius =
                spec.radius * (0.70f + 0.30f * randomRange(rng, 0.0f, 1.0f)) * kRingScale;
            shard.angle = randomRange(rng, 0.0f, kTwoPi);
            shard.speed = randomRange(rng, spec.speedMin, spec.speedMax) * kRadialGain * kRingScale;
            shard.size = randomRange(rng, spec.sizeMin, spec.sizeMax) * kShardScale;
            // startRotation is a Vector3 with every axis at +-2pi.
            shard.rot0x = randomRange(rng, -kTwoPi, kTwoPi);
            shard.rot0y = randomRange(rng, -kTwoPi, kTwoPi);
            shard.rot0z = randomRange(rng, -kTwoPi, kTwoPi);
            // Only Z spins (separateAxes = 0); X/Y stay as the initial tilt.
            shard.spinX = 0.0f;
            shard.spinY = 0.0f;
            shard.spinZ = kSpinRate;
            shard.cell = static_cast<int>(nextRandom(rng) & 3u); // UV startFrame 0..0.9999
            shard.grad = spec.grad == &kGradBig ? 0 : (spec.grad != nullptr ? 1 : -1);
            instance.shards.push_back(shard);
        }
    }
}

void TapEffect::emitHoldShard(HoldState& hold, unsigned& rng) const
{
    // Follow runs all three systems at rateOverTime 6/s, so one particle of each
    // per 1/6 s tick.
    for (int specIndex = 0; specIndex < kHoldSpecCount; ++specIndex) {
        const TriSpec& spec = kHoldSpecs[specIndex];
        Shard shard;
        shard.born = hold.age;
        shard.life = randomRange(rng, spec.lifeMin, spec.lifeMax);
        shard.radius = spec.radius * (0.70f + 0.30f * randomRange(rng, 0.0f, 1.0f)) * kRingScale;
        shard.angle = randomRange(rng, 0.0f, kTwoPi);
        shard.speed = randomRange(rng, spec.speedMin, spec.speedMax) * kRadialGain * kRingScale;
        shard.size = randomRange(rng, spec.sizeMin, spec.sizeMax) * kShardScale;
        shard.rot0x = randomRange(rng, -kTwoPi, kTwoPi);
        shard.rot0y = randomRange(rng, -kTwoPi, kTwoPi);
        shard.rot0z = randomRange(rng, -kTwoPi, kTwoPi);
        shard.spinX = 0.0f;
        shard.spinY = 0.0f;
        shard.spinZ = kSpinRate;
        shard.cell = static_cast<int>(nextRandom(rng) & 3u);
        shard.grad = spec.grad == &kGradBig ? 0 : (spec.grad != nullptr ? 1 : -1);
        hold.shards.push_back(shard);
    }
}

void TapEffect::spawn(float x, float y)
{
    spawnAt(x, y, 0.0f);
}

void TapEffect::spawnAt(float x, float y, float age)
{
    if (!loaded()) {
        return;
    }
    Instance instance;
    instance.x = x;
    instance.y = y;
    instance.age = age;
    unsigned rng = static_cast<unsigned>(mNextSeed++ * 2654435761u + 1u);
    buildTapShards(instance, rng);
    mActive.push_back(std::move(instance));

    // Bursts are short; drop the oldest so the vector cannot grow without bound
    // when the player hammers the mouse.
    if (mActive.size() > 64) {
        mActive.erase(mActive.begin(), mActive.begin() + static_cast<long>(mActive.size() - 64));
    }
}

void TapEffect::press(float x, float y)
{
    // pjsk fires the tap burst and starts the follow effect on the same press.
    spawn(x, y);
    mHold = HoldState{};
    mHold.active = true;
    mHold.x = x;
    mHold.y = y;
    unsigned rng = static_cast<unsigned>(mNextSeed++ * 2246822519u + 7u);
    emitHoldShard(mHold, rng);
}

void TapEffect::moveHold(float x, float y)
{
    if (!mHold.active) {
        return;
    }
    mHold.x = x;
    mHold.y = y;
}

void TapEffect::release()
{
    // Matches StopTapEffect / EndFollowEffect: the ripple and glow stop at once,
    // the shards already in flight live out their own lifetimes.
    mHold.active = false;
}

void TapEffect::debugHoldAt(float x, float y, float age)
{
    press(x, y);
    // Replay the emission ticker at a fixed step so the result is deterministic.
    constexpr float kStep = 1.0f / 60.0f;
    const float interval = 1.0f / kHoldShardRate;
    unsigned rng = static_cast<unsigned>(mNextSeed++ * 747796405u + 1013904223u);
    float t = 0.0f;
    while (t < age) {
        const float dt = std::min(kStep, age - t);
        mHold.age += dt;
        mHold.emitAccum += dt;
        while (mHold.emitAccum >= interval) {
            mHold.emitAccum -= interval;
            emitHoldShard(mHold, rng);
        }
        t += dt;
    }
    const float holdAge = mHold.age;
    mHold.shards.erase(std::remove_if(mHold.shards.begin(), mHold.shards.end(),
                           [holdAge](const Shard& shard) {
                               return holdAge - shard.born >= shard.life;
                           }),
        mHold.shards.end());
}

void TapEffect::clear()
{
    mActive.clear();
    mHold = HoldState{};
}

void TapEffect::update(float dt)
{
    if (mFrozen) {
        return;
    }
    for (Instance& instance : mActive) {
        instance.age += dt;
    }
    float maxLife = kTapRingLife;
    for (const TriSpec& spec : kTapSpecs) {
        maxLife = std::max(maxLife, spec.lifeMax);
    }
    mActive.erase(std::remove_if(mActive.begin(), mActive.end(),
                      [maxLife](const Instance& instance) { return instance.age >= maxLife; }),
        mActive.end());

    if (mHold.active) {
        mHold.age += dt;
        mHold.emitAccum += dt;
        const float interval = 1.0f / kHoldShardRate;
        unsigned rng = static_cast<unsigned>(mNextSeed++ * 747796405u + 1013904223u);
        while (mHold.emitAccum >= interval) {
            mHold.emitAccum -= interval;
            emitHoldShard(mHold, rng);
        }
    } else {
        mHold.age += dt; // shards keep ageing after the finger lifts
    }

    const float holdAge = mHold.age;
    mHold.shards.erase(std::remove_if(mHold.shards.begin(), mHold.shards.end(),
                           [holdAge](const Shard& shard) {
                               return holdAge - shard.born >= shard.life;
                           }),
        mHold.shards.end());
    if (!mHold.active && mHold.shards.empty()) {
        mHold = HoldState{};
    }
}

void TapEffect::draw()
{
    const bool anyTap = !mActive.empty();
    const bool anyHold = mHold.active || !mHold.shards.empty();
    if (!anyTap && !anyHold) {
        return;
    }
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (drawList == nullptr) {
        return;
    }
    const float scale = ui::scale();

    if (mSheet != 0) {
        // Ripples and the hold glow are pure light, so they go through one
        // additive pass. The shards follow in the normal alpha-blended pass.
        drawList->AddCallback(&beginAdditive, nullptr);
        const FlipLayer tapLayer{mSheet, kTiles, kFrames, kTapRingLife, kTapRing, 1.0f};
        for (const Instance& instance : mActive) {
            drawFlip(drawList, instance.x, instance.y, instance.age, tapLayer, scale);
        }
        if (mHold.active) {
            // Only the glow: Follow's PT_01 has rateOverTime 0 and the game code
            // never Play()s it, so a held press really does show no ring - just
            // the white halo and the orbiting triangles.
            drawGlow(drawList, mHold.x, mHold.y, kHoldGlowAlpha, scale);
        }
        drawList->AddCallback(&endAdditive, nullptr);
    } else if (mRing != 0) {
        // Legacy fallback: scaled ring sprite, keeps old installs working.
        for (const Instance& instance : mActive) {
            drawRingLegacy(drawList, instance.x, instance.y, instance.age, scale, 0.30f, true);
            drawRingLegacy(drawList, instance.x, instance.y, instance.age, scale, 1.0f, false);
        }
    }

    // The mesh triangles are alpha blended, not additive: they carry a white
    // outline and a gradient fill, i.e. die-cut stickers rather than light.
    if (mTri != 0) {
        for (const Instance& instance : mActive) {
            for (const Shard& shard : instance.shards) {
                drawShard(drawList, instance.x, instance.y, instance.age, shard, scale);
            }
        }
        for (const Shard& shard : mHold.shards) {
            drawShard(drawList, mHold.x, mHold.y, mHold.age, shard, scale);
        }
    }
}

void TapEffect::drawFlip(ImDrawList* drawList, float cx, float cy, float age,
    const FlipLayer& layer, float scale) const
{
    if (layer.tex == 0 || age >= layer.life) {
        return;
    }
    const float localT = clamp01(age / layer.life);

    // frameOverTime is a linear 0 -> 0.9999 ramp; 16 frames over 0.5s of life
    // works out to the sheet's declared 30 fps.
    int frame = static_cast<int>(localT * static_cast<float>(layer.frames));
    frame = frame < 0 ? 0 : (frame >= layer.frames ? layer.frames - 1 : frame);
    const int tiles = layer.tiles > 0 ? layer.tiles : 1;
    const float tile = 1.0f / static_cast<float>(tiles);
    const ImVec2 uv0(static_cast<float>(frame % tiles) * tile,
        static_cast<float>(frame / tiles) * tile);
    const ImVec2 uv1(uv0.x + tile, uv0.y + tile);

    if (layer.alpha <= 0.004f) {
        return;
    }
    const float half = layer.diameter * scale * 0.5f;
    drawList->AddImage(layer.tex, ImVec2(cx - half, cy - half), ImVec2(cx + half, cy + half), uv0,
        uv1, withAlpha(kWhite, layer.alpha));
}

void TapEffect::drawGlow(ImDrawList* drawList, float cx, float cy, float alpha, float scale) const
{
    if (mGlow == 0 || alpha <= 0.004f) {
        return;
    }
    const float half = kHoldGlow * scale * 0.5f;
    drawList->AddImage(mGlow, ImVec2(cx - half, cy - half), ImVec2(cx + half, cy + half),
        ImVec2(0, 0), ImVec2(1, 1), withAlpha(kWhite, alpha));
}

void TapEffect::drawShard(ImDrawList* dl, float originX, float originY, float instanceAge,
    const Shard& shard, float scale) const
{
    const float localAge = instanceAge - shard.born;
    if (localAge < 0.0f || localAge >= shard.life) {
        return;
    }
    const float t = clamp01(localAge / shard.life);

    // ColorOverLifetime: see kBigAlphas / kMidAlphas.
    float lr = 1.0f;
    float lg = 1.0f;
    float lb = 1.0f;
    float la = 1.0f;
    if (shard.grad == 0) {
        evalGradient(kGradBig, t, lr, lg, lb, la);
    } else if (shard.grad == 1) {
        evalGradient(kGradMid, t, lr, lg, lb, la);
    }
    if (la <= 0.004f) {
        return;
    }

    const float travel = shard.speed * localAge * scale;
    const float px = originX + std::cos(shard.angle) * (shard.radius * scale + travel);
    const float py = originY + std::sin(shard.angle) * (shard.radius * scale + travel);

    // 3D pose: the quad starts tilted by startRotation (X and Y, fixed) and
    // spins about Z over its life. Projected orthographically, so a tilted quad
    // foreshortens - which is the "flipping glass card" read the flat 2D version
    // could not produce.
    const float rx = shard.rot0x + shard.spinX * localAge;
    const float ry = shard.rot0y + shard.spinY * localAge;
    const float rz = shard.rot0z + shard.spinZ * localAge;
    const float cxr = std::cos(rx);
    const float sxr = std::sin(rx);
    const float cyr = std::cos(ry);
    const float syr = std::sin(ry);
    const float czr = std::cos(rz);
    const float szr = std::sin(rz);
    const float half = shard.size * scale * 0.5f;

    const ImVec2 local[4] = {
        {-half, -half}, {half, -half}, {half, half}, {-half, half},
    };
    ImVec2 pts[4];
    for (int i = 0; i < 4; ++i) {
        const float lx = local[i].x;
        const float ly = local[i].y;
        // rotate about X (quad lives in z = 0)
        const float y1 = ly * cxr;
        const float z1 = ly * sxr;
        // rotate about Y
        const float x2 = lx * cyr + z1 * syr;
        // rotate about Z
        const float x3 = x2 * czr - y1 * szr;
        const float y3 = x2 * szr + y1 * czr;
        pts[i] = ImVec2(px + x3, py + y3);
    }

    // 2x2 colour cells, picked once per particle via UV startFrame.
    const float u0 = static_cast<float>(shard.cell & 1) * 0.5f;
    const float v0 = static_cast<float>(shard.cell >> 1) * 0.5f;
    const ImVec2 uv[4] = {
        ImVec2(u0, v0), ImVec2(u0 + 0.5f, v0), ImVec2(u0 + 0.5f, v0 + 0.5f), ImVec2(u0, v0 + 0.5f),
    };

    const ImU32 tint = IM_COL32(srgbOf(lr), srgbOf(lg), srgbOf(lb),
        static_cast<int>(clamp01(la) * 255.0f + 0.5f));
    dl->AddImageQuad(mTri, pts[0], pts[1], pts[2], pts[3], uv[0], uv[1], uv[2], uv[3], tint);
}

void TapEffect::drawRingLegacy(ImDrawList* drawList, float cx, float cy, float age, float scale,
    float alphaScale, bool shadow) const
{
    const float t = clamp01(age / kTapRingLife);
    const float alpha = std::pow(1.0f - t, 1.5f) * alphaScale;
    if (alpha <= 0.004f) {
        return;
    }
    const float diameter = kTapRing * scale * (0.15f + 0.85f * easeOutCubic(t));
    const float offset = shadow ? 1.5f * scale : 0.0f;
    const ImVec2 min(cx - diameter * 0.5f + offset, cy - diameter * 0.5f + offset);
    const ImVec2 max(cx + diameter * 0.5f + offset, cy + diameter * 0.5f + offset);
    const ImU32 tint = shadow ? IM_COL32(48, 52, 72, static_cast<int>(alpha * 255.0f + 0.5f))
                              : withAlpha(kWhite, alpha);
    drawList->AddImage(mRing, min, max, ImVec2(0, 0), ImVec2(1, 1), tint);
}

} // namespace game
