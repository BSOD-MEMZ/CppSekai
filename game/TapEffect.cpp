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
// --- PT_01 main ripple / PT_02 glow -------------------------------------
// Transform scale is 1 for both, so startSize is a design-pixel diameter.
constexpr float kRingLife = 0.50f;
constexpr float kFlareLife = 0.50f;
constexpr float kRingDiameter = 160.0f;
constexpr float kFlareDiameter = 650.0f;
constexpr int kTiles = 4;
constexpr int kFrames = 16; // WholeSheet: all 16 tiles, 30 fps ramp 0 -> 0.9999

// --- mesh triangle particles -------------------------------------------
// size = startSize * RectTransform.localScale, radius = shape radius * scale.
// The original emits from a Circle shape with radiusThickness 0 (edge only) and
// startSpeed along the disc normal. The normal points out of the screen, so in
// an orthographic 2D view the particles do not travel at all - they appear on a
// ring, spin and fade in. kRadialGain > 0 translates that speed into an outward
// drift so the burst reads as "scattering" on a flat canvas; 0 = literal.
constexpr float kRadialGain = 0.22f;
constexpr float kSpinRate = 3.14159265f; // RotationOverLifetime: pi rad/s on X and Y

// ColorOverLifetime, read key by key out of the YAML.
//
// Unity stores a Gradient as 8 keys sharing one array, but only the first
// m_NumColorKeys of them drive RGB (on the ctime axis) and only the first
// m_NumAlphaKeys drive alpha (on a SEPARATE atime axis). The leftover keys carry
// rgb (0,0,0); treating those as colour keyframes is what makes these shards
// look like they turn pure black halfway through, which they never do.
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

// particle3 (big): colour drifts pale slate -> deep slate across the whole life;
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
// particle2_1 (medium): plain white, snaps opaque in the first 5%, fades at end.
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
    float sizeMin;
    float sizeMax;
    float lifeMin;
    float lifeMax;
    float radius;   // emission ring radius, design px
    float speedMin; // startSpeed range (world units/s)
    float speedMax;
    int count;      // particles per tap
    const ShardGradient* grad; // nullptr = ColorModule disabled (keep startColor)
};

constexpr ShardGradient kGradBig = {kBigColors, 2, kBigAlphas, 4};
constexpr ShardGradient kGradMid = {kMidColors, 2, kMidAlphas, 5};

// particle3 / particle2_1 / particle1, in that order.
constexpr int kTriSpecCount = 3;
constexpr TriSpec kTriSpecs[kTriSpecCount] = {
    // big translucent triangles: 42..147 px, 6/s over a 1s system
    {42.0f, 147.0f, 0.30f, 1.50f, 210.0f, 0.00f, 0.45f, 6, &kGradBig},
    // medium: 14 px, one per tap
    {14.0f, 14.0f, 0.20f, 0.70f, 157.0f, 35.0f, 100.0f, 1, &kGradMid},
    // tiny flakes: 2.1..10.5 px (life clamped from the original 1..30s);
    // ColorModule is disabled on these, so they stay white at alpha 1
    {2.1f, 10.5f, 1.00f, 3.00f, 210.0f, 5.0f, 40.0f, 2, nullptr},
};

constexpr float kSpawnWindow = 0.90f; // system length is ~1.0s, spread births over it

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

// Deterministic PRNG so --tap-fx-test replays the exact same burst.
unsigned nextRandom(unsigned& state)
{
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

float randomRange(unsigned& state, float lo, float hi)
{
    const float unit = static_cast<float>(nextRandom(state) & 0xFFFFFFu) / 16777215.0f;
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
        {"tap_sheet.png", &mSheet, true},  // 4x4 main ripple (tex_tap_texturesheet)
        {"tap_flare.png", &mFlare, false}, // single radial glow (tex_tap_flare)
        {"tap_tri.png", &mTri, false},     // 2x2 colour triangles (tex_sekai_01_2)
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
    // The original emits continuously (rateOverTime 6/s for particle3), so the
    // particles are spread across the system's ~1s length instead of all being
    // born at once.
    for (int specIndex = 0; specIndex < kTriSpecCount; ++specIndex) {
        const TriSpec& spec = kTriSpecs[specIndex];
        for (int i = 0; i < spec.count; ++i) {
            Shard shard;
            shard.born = randomRange(rng, 0.0f, kSpawnWindow);
            shard.life = randomRange(rng, spec.lifeMin, spec.lifeMax);
            // Circle shape with radiusThickness 0 emits from the edge; a little
            // inward spread keeps the ring from looking like a perfect circle.
            shard.radius = spec.radius * (0.55f + 0.45f * randomRange(rng, 0.0f, 1.0f));
            shard.angle = randomRange(rng, 0.0f, kTwoPi);
            shard.speed = randomRange(rng, spec.speedMin, spec.speedMax) * kRadialGain;
            shard.size = randomRange(rng, spec.sizeMin, spec.sizeMax);
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
    // Longest path through a burst: last birth + longest life.
    float maxLife = std::max(kRingLife, kFlareLife);
    for (const TriSpec& spec : kTriSpecs) {
        maxLife = std::max(maxLife, kSpawnWindow + spec.lifeMax);
    }
    mActive.erase(std::remove_if(mActive.begin(), mActive.end(),
                      [maxLife](const Instance& instance) { return instance.age >= maxLife; }),
        mActive.end());
}

void TapEffect::layerList(FlipLayer (&out)[2]) const
{
    out[0] = FlipLayer{mFlare, 1, 1, kFlareLife, kFlareDiameter, 0.55f, true};
    out[1] = FlipLayer{mSheet, kTiles, kFrames, kRingLife, kRingDiameter, 1.0f, false};
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

    FlipLayer layers[2];
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

    ImVec2 uv0(0.0f, 0.0f);
    ImVec2 uv1(1.0f, 1.0f);
    float alpha = layer.alpha;
    if (layer.single) {
        // The flare art carries no fade of its own.
        alpha *= std::pow(1.0f - localT, 1.6f);
    } else {
        int frame = static_cast<int>(localT * static_cast<float>(layer.frames));
        frame = frame < 0 ? 0 : (frame >= layer.frames ? layer.frames - 1 : frame);
        const int tiles = layer.tiles > 0 ? layer.tiles : 1;
        const float tile = 1.0f / static_cast<float>(tiles);
        const int col = frame % tiles;
        const int row = frame / tiles;
        uv0 = ImVec2(col * tile, row * tile);
        uv1 = ImVec2((col + 1) * tile, (row + 1) * tile);
    }
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
