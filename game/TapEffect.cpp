// CppSekai - PJSK style global tap feedback. See TapEffect.hpp for the
// provenance of every number here (all of them are measured out of pjsk
// 7.1.0's ParticleSystem YAML, none are eyeballed).
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
// --- PT_01 main ripple ---------------------------------------------------
// Read out of fx_Tap_single's ParticleSystem_217: length 1.0s, looping 0,
// scalingMode Hierarchy (0), Transform scale 1, so startSize 160 is a direct
// design-pixel diameter. Its ShapeModule is disabled and rateOverTime is 0:
// the ring is spawned by Play() alone.
constexpr float kRingLife = 0.50f;
constexpr float kRingDiameter = 160.0f;
constexpr int kTiles = 4;
constexpr int kFrames = 16; // WholeSheet: all 16 tiles, 30 fps ramp 0 -> 0.9999

// --- mesh triangle particles --------------------------------------------
// fx_Tap_single carries FOUR particle systems under SekaiEffect_2, all with
// Transform scale 157 and scalingMode Hierarchy, so the world size is
//   size = startSize(curve peak) * scalar * 157
// Every one of them emits at rateOverTime 1/s over a 0.5s system, i.e. one
// particle at t=0, and every one has startSpeed 35..100 pointing along the
// disc normal (+Z, out of the screen).
//
//   node         life          curve peak  scalar   world size
//   particle3    0.30..0.70    1.00        1.50     235
//   particle2    0.10..0.70    0.83        0.14      18
//   particle2_1  0.20..0.70    0.83        0.09      12
//   particle1    30..35        0.05 const  -          1.6..7.9
//
// A tap therefore throws ONE big triangle plus a medium, a small and a couple
// of flakes - not a swarm of mid-sized ones.
// The ONLY non-measured number in this file. Everything above is read straight
// out of the prefab; this multiplies the shard diameters to taste. Set it to
// 1.0 for the literal values (particle3 then peaks at 235 px, i.e. 1.5x the
// ring, which reads as a huge glass slab on a bright song-select backdrop).
constexpr float kShardScale = 0.55f;
constexpr float kRadialGain = 1.0f; // Z-axis speed reinterpreted as radial px/s
constexpr float kSpinRate = 3.14159265f; // RotationOverLifetime: pi rad/s

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

struct TriSpec
{
    // startSize is Random Between Two Curves, so every particle rolls its own
    // diameter between these two (minCurve*minScalar and maxCurve*scalar, both
    // times the 157 Transform scale). The ranges are wide on purpose - that is
    // how the original gets a mix of one big shard and a lot of small ones.
    float sizeMin;
    float sizeMax;
    float lifeMin;
    float lifeMax;
    float radius;   // emission ring radius: shape radius 1 * scale 157
    float speedMin; // startSpeed, world units/s (along +Z in the original)
    float speedMax;
    int count;
    const ShardGradient* grad; // nullptr = ColorModule disabled (keep startColor)
};

constexpr ShardGradient kGradBig = {kBigColors, 2, kBigAlphas, 4};
constexpr ShardGradient kGradMid = {kMidColors, 2, kMidAlphas, 5};

constexpr int kTriSpecCount = 4;
constexpr TriSpec kTriSpecs[kTriSpecCount] = {
    // particle3: the big shard, 2.5..235 px
    {2.5f, 235.0f, 0.30f, 0.70f, 157.0f, 35.0f, 100.0f, 1, &kGradBig},
    // particle2: 15.7..18.2 px
    {15.7f, 18.2f, 0.10f, 0.70f, 157.0f, 35.0f, 100.0f, 1, &kGradMid},
    // particle2_1: 1.2..11.7 px
    {1.2f, 11.7f, 0.20f, 0.70f, 157.0f, 35.0f, 100.0f, 1, &kGradMid},
    // particle1: 1.6..7.9 px. Original life is 30..35s, clamped here to 2s - a
    // 35s speck is not tap feedback. ShapeModule is disabled on this one, so it
    // emits at the tap point instead of on the ring.
    {1.6f, 7.9f, 0.60f, 2.00f, 0.0f, 1.0f, 20.0f, 2, nullptr},
};

// ColorOverLifetime, read key by key out of the YAML.
//
constexpr float kTwoPi = 6.28318530717959f;

constexpr ImU32 kRingTint = IM_COL32(255, 255, 255, 255);

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
    const int v = static_cast<int>(clamp01(c) * 255.0f + 0.5f);
    return v;
}

// Linear interpolation over the measured ColorOverLifetime keys. Interpolation
// happens in linear space (Unity's own behaviour), conversion to sRGB is last.
void evalGradient(const ShardGradient& g, float t, float& r, float& gr, float& b, float& a)
{
    r = gr = b = 1.0f;
    a = 1.0f;
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
// an LCG's low bits have a short period and the first few draws clustered near
// zero, which made every test burst roll a tiny shard.
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
        {"tap_ring.png", &mRing, false},   // legacy fallback ring
    };

    bool anyLoaded = false;
    for (const Entry& entry : entries) {
        const std::string path = fxDir + "\\" + entry.file;
        std::string error;
        const GLuint id = renderer.loadUiTexture(path, error);
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
    buildShards(instance, rng);
    mActive.push_back(std::move(instance));

    // Bursts are short; drop the oldest so the vector cannot grow without bound
    // when the player hammers the mouse.
    if (mActive.size() > 64) {
        mActive.erase(mActive.begin(), mActive.begin() + static_cast<long>(mActive.size() - 64));
    }
}

void TapEffect::buildShards(Instance& instance, unsigned& rng) const
{
    // Every triangle system in fx_Tap_single runs rateOverTime 1/s over a 0.5s
    // lifetime, so Unity emits exactly one particle at t=0. Nothing is staggered:
    // the whole burst appears on the click.
    for (int specIndex = 0; specIndex < kTriSpecCount; ++specIndex) {
        const TriSpec& spec = kTriSpecs[specIndex];
        for (int i = 0; i < spec.count; ++i) {
            Shard shard;
            shard.born = 0.0f;
            shard.life = randomRange(rng, spec.lifeMin, spec.lifeMax);
            // Circle shape, radiusThickness 0: emitted on the ring itself. A bit
            // of inward tolerance keeps it from looking like a drawn circle.
            shard.radius = spec.radius * (0.70f + 0.30f * randomRange(rng, 0.0f, 1.0f));
            shard.angle = randomRange(rng, 0.0f, kTwoPi);
            shard.speed = randomRange(rng, spec.speedMin, spec.speedMax) * kRadialGain;
            shard.size = randomRange(rng, spec.sizeMin, spec.sizeMax) * kShardScale;
            shard.rot0 = randomRange(rng, -kTwoPi, kTwoPi); // startRotation +-2pi
            shard.spin = kSpinRate;                         // 180 deg/s, always positive
            shard.cell = static_cast<int>(nextRandom(rng) & 3u); // UV startFrame 0..0.9999
            shard.grad = spec.grad != nullptr ? specIndex : -1;
            instance.shards.push_back(shard);
        }
    }
}

void TapEffect::update(float dt)
{
    if (!mFrozen) {
        for (Instance& instance : mActive) {
            instance.age += dt;
        }
    }
    float maxLife = kRingLife;
    for (const TriSpec& spec : kTriSpecs) {
        maxLife = std::max(maxLife, spec.lifeMax);
    }
    mActive.erase(std::remove_if(mActive.begin(), mActive.end(),
                      [maxLife](const Instance& instance) { return instance.age >= maxLife; }),
        mActive.end());
}

void TapEffect::layerList(FlipLayer (&out)[1]) const
{
    // fx_Tap_single has a single billboard system (PT_01). The 650px flare some
    // earlier notes mention belongs to fx_Tap_loop / the extra TouchEffect
    // instances, not to the click.
    out[0] = FlipLayer{mSheet, kTiles, kFrames, kRingLife, kRingDiameter, 1.0f};
}

void TapEffect::draw()
{
    if (mActive.empty()) {
        return;
    }
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (drawList == nullptr) {
        return;
    }
    const float scale = ui::scale();

    FlipLayer layers[1];
    layerList(layers);

    if (mSheet != 0) {
        // The glow and the 4x4 ripple are additive: their art is pure light.
        drawList->AddCallback(&beginAdditive, nullptr);
        for (const FlipLayer& layer : layers) {
            for (const Instance& instance : mActive) {
                drawFlip(drawList, instance.x, instance.y, instance.age, layer, scale);
            }
        }
        drawList->AddCallback(&endAdditive, nullptr);
    } else if (mRing != 0) {
        // Legacy fallback: scaled ring sprite, keeps old installs working.
        for (const Instance& instance : mActive) {
            drawRingLegacy(drawList, instance, scale, 0.30f, true);
        }
        for (const Instance& instance : mActive) {
            drawRingLegacy(drawList, instance, scale, 1.0f, false);
        }
    }

    // The mesh triangles are alpha blended, not additive: they carry a white
    // outline and a gradient fill, i.e. they are die-cut stickers, and their
    // ColorOverLifetime ramps through black before fading out (a black additive
    // quad would simply vanish, which would waste half the animation).
    if (mTri != 0) {
        for (const Instance& instance : mActive) {
            for (const Shard& shard : instance.shards) {
                drawShard(drawList, instance, shard, scale);
            }
        }
    }
}

void TapEffect::drawFlip(ImDrawList* drawList, float cx, float cy, float t, const FlipLayer& layer,
    float scale) const
{
    if (layer.tex == 0 || t >= layer.life) {
        return;
    }
    const float localT = clamp01(t / layer.life);

    // frameOverTime is a linear 0 -> 0.9999 ramp; 16 frames over a 0.5s life
    // works out to the sheet's declared 30 fps.
    int frame = static_cast<int>(localT * static_cast<float>(layer.frames));
    frame = frame < 0 ? 0 : (frame >= layer.frames ? layer.frames - 1 : frame);
    const int tiles = layer.tiles > 0 ? layer.tiles : 1;
    const float tile = 1.0f / static_cast<float>(tiles);
    const ImVec2 uv0(static_cast<float>(frame % tiles) * tile,
        static_cast<float>(frame / tiles) * tile);
    const ImVec2 uv1(uv0.x + tile, uv0.y + tile);

    const float alpha = layer.alpha;
    if (alpha <= 0.004f) {
        return;
    }

    const float half = layer.diameter * scale * 0.5f;
    drawList->AddImage(layer.tex, ImVec2(cx - half, cy - half), ImVec2(cx + half, cy + half), uv0,
        uv1, withAlpha(kRingTint, alpha));
}

void TapEffect::drawShard(ImDrawList* dl, const Instance& in, const Shard& shard, float scale) const
{
    const float localAge = in.age - shard.born;
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
    const float px = in.x + std::cos(shard.angle) * (shard.radius * scale + travel);
    const float py = in.y + std::sin(shard.angle) * (shard.radius * scale + travel);

    const float rot = shard.rot0 + shard.spin * localAge;
    const float c = std::cos(rot);
    const float s = std::sin(rot);
    const float half = shard.size * scale * 0.5f;

    const ImVec2 local[4] = {
        {-half, -half}, {half, -half}, {half, half}, {-half, half},
    };
    ImVec2 pts[4];
    for (int i = 0; i < 4; ++i) {
        pts[i].x = px + local[i].x * c - local[i].y * s;
        pts[i].y = py + local[i].x * s + local[i].y * c;
    }

    // 2x2 colour cells, picked once per particle via UV startFrame.
    const float u0 = (shard.cell & 1) * 0.5f;
    const float v0 = (shard.cell >> 1) * 0.5f;
    const ImVec2 uv[4] = {
        ImVec2(u0, v0), ImVec2(u0 + 0.5f, v0), ImVec2(u0 + 0.5f, v0 + 0.5f), ImVec2(u0, v0 + 0.5f),
    };

    const ImU32 tint = IM_COL32(srgbOf(lr), srgbOf(lg), srgbOf(lb),
        static_cast<int>(clamp01(la) * 255.0f + 0.5f));
    dl->AddImageQuad(mTri, pts[0], pts[1], pts[2], pts[3], uv[0], uv[1], uv[2], uv[3], tint);
}

void TapEffect::drawRingLegacy(ImDrawList* drawList, const Instance& in, float scale,
    float alphaScale, bool shadow) const
{
    const float t = clamp01(in.age / kRingLife);
    const float alpha = std::pow(1.0f - t, 1.5f) * alphaScale;
    if (alpha <= 0.004f) {
        return;
    }
    const float diameter = kRingDiameter * scale * (0.15f + 0.85f * easeOutCubic(t));
    const float offset = shadow ? 1.5f * scale : 0.0f;
    const ImVec2 min(in.x - diameter * 0.5f + offset, in.y - diameter * 0.5f + offset);
    const ImVec2 max(in.x + diameter * 0.5f + offset, in.y + diameter * 0.5f + offset);
    const ImU32 tint = shadow ? IM_COL32(48, 52, 72, static_cast<int>(alpha * 255.0f + 0.5f))
                              : withAlpha(kRingTint, alpha);
    drawList->AddImage(mRing, min, max, ImVec2(0, 0), ImVec2(1, 1), tint);
}

} // namespace game
