// CppSekai - PJSK style global tap feedback. See TapEffect.hpp for the
// provenance of every number here (measured from pjsk 7.1.0's particle
// systems, not eyeballed).
//
// Draw order matters: the big soft flare goes underneath, then the thin ring,
// then the main double ring, then the shards on top.
#include "TapEffect.hpp"

#include "Ui.hpp"
#include "platform/Renderer.hpp"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>

namespace game
{
namespace
{
// --- measured constants -------------------------------------------------
// PT_01 startLifetime = 0.5s (0.45s for the held variant).
constexpr float kRingLife = 0.50f;
constexpr float kThinLife = 0.45f;
constexpr float kFlareLife = 0.50f;
// Frame counts actually used inside each 4x4 sheet.
constexpr int kMainFrames = 14;
constexpr int kThinFrames = 16;
// Quad diameter in design pixels. The original keeps startSize constant
// (startSpeed 0, no size-over-lifetime curve): all growth is baked into the
// art, so the quad never scales. 160/330/650 is the original ratio; 200 here
// maps the main ring to roughly the size it reads on a 1920x1080 canvas.
constexpr float kRingDiameter = 200.0f;
constexpr float kThinDiameter = 250.0f;
constexpr float kFlareDiameter = 520.0f;
constexpr int kTiles = 4;

// --- shards (mesh particles in the original) ----------------------------
constexpr float kTriangleDuration = 0.30f;
constexpr float kShardDistance = 38.0f;
constexpr float kShardSpawnRadius = 12.0f;
constexpr float kShardSizeStart = 18.0f;
constexpr float kShardSizeEnd = 11.0f;
constexpr int kShardCount = 10;

constexpr TapEffect::Piece kPieces[kShardCount] = {
    {-90.0f, 210.0f, false, 1.00f, 1.15f},
    {-54.0f, -180.0f, true, 0.82f, 0.90f},
    {-18.0f, 240.0f, false, 1.12f, 0.85f},
    {18.0f, -200.0f, true, 0.90f, 1.05f},
    {54.0f, 225.0f, false, 0.78f, 1.00f},
    {90.0f, -230.0f, true, 1.05f, 0.95f},
    {126.0f, 215.0f, false, 0.88f, 0.80f},
    {162.0f, -245.0f, true, 1.18f, 1.10f},
    {198.0f, 235.0f, false, 0.84f, 1.00f},
    {234.0f, -215.0f, true, 0.96f, 0.90f},
};

constexpr float kDegToRad = 3.14159265358979f / 180.0f;

// Tints for the legacy path and the shards. The sheets carry their own color,
// so only the fallback ring and the masks get tinted.
constexpr ImU32 kRingTint = IM_COL32(206, 250, 242, 255);    // near-white mint
constexpr ImU32 kFillTint = IM_COL32(106, 232, 208, 255);    // ui::kPrimary mint
constexpr ImU32 kOutlineTint = IM_COL32(255, 102, 158, 255); // ui::kCheckPink pink

ImU32 withAlpha(ImU32 color, float alpha)
{
    const auto a = static_cast<ImU32>(alpha * 255.0f + 0.5f);
    return (color & 0x00FFFFFFu) | (a << 24);
}

ImU32 fadeShadow(float alpha)
{
    const int a = static_cast<int>(alpha * 255.0f + 0.5f);
    return IM_COL32(48, 52, 72, a);
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

float easeOutQuad(float t)
{
    const float inv = 1.0f - t;
    return 1.0f - inv * inv;
}

// pjsk draws the ripple with an additive blend (it glows on top of the
// dimmed song-select backdrop). The ImGui GL3 backend sets blending once per
// draw data, so a draw-list callback has to switch it and put it back.
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
        {"tap_sheet.png", &mSheetA, true},   // 4x4 main ripple, generated art
        {"tap_sheet_1.png", &mSheetB, false}, // 4x4 thin ring
        {"tap_flare.png", &mFlare, false},    // single radial glow
        {"tap_ring.png", &mRing, false},      // legacy fallback ring
        {"tap_tri_2.png", &mFill, false},     // gradient-filled triangle mask
        {"tap_tri_1.png", &mOutline, false},  // gradient-outlined triangle mask
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
    instance.seed = mNextSeed++;
    mActive.push_back(instance);

    // Bursts are short; drop the ones that already finished so the vector
    // cannot grow without bound when the player hammers the mouse.
    if (mActive.size() > 64) {
        mActive.erase(mActive.begin(), mActive.begin() + static_cast<long>(mActive.size() - 64));
    }
}

void TapEffect::update(float dt)
{
    if (!mFrozen) {
        for (Instance& instance : mActive) {
            instance.age += dt;
        }
    }
    const float maxLife = std::max(kRingLife, std::max(kThinLife, kFlareLife));
    mActive.erase(std::remove_if(mActive.begin(), mActive.end(),
                      [maxLife](const Instance& instance) { return instance.age >= maxLife; }),
        mActive.end());
}

void TapEffect::layerList(FlipLayer (&out)[3]) const
{
    out[0] = FlipLayer{mFlare, 1, 1, kFlareLife, 0.0f, kFlareDiameter, 0.40f, true};
    out[1] = FlipLayer{mSheetB, kTiles, kThinFrames, kThinLife, 0.0f, kThinDiameter, 0.80f, false};
    out[2] = FlipLayer{mSheetA, kTiles, kMainFrames, kRingLife, 0.0f, kRingDiameter, 1.00f, false};
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

    FlipLayer layers[3];
    layerList(layers);

    if (mSheetA != 0) {
        // Flipbook path: constant-size quads + per-frame UV, exactly like the
        // original's Texture Sheet Animation module. Additive so the rings read
        // as light on top of the blurred song-select backdrop.
        drawList->AddCallback(&beginAdditive, nullptr);
        for (const FlipLayer& layer : layers) {
            for (const Instance& instance : mActive) {
                drawFlip(drawList, instance, layer, scale);
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

    // Shards on top of every ring layer.
    for (const Instance& instance : mActive) {
        for (int i = 0; i < kShardCount; ++i) {
            drawShard(drawList, instance, kPieces[i], scale, 0.25f, true);
        }
    }
    for (const Instance& instance : mActive) {
        for (int i = 0; i < kShardCount; ++i) {
            drawShard(drawList, instance, kPieces[i], scale, 1.0f, false);
        }
    }
}

void TapEffect::drawFlip(ImDrawList* drawList, const Instance& in, const FlipLayer& layer,
    float scale) const
{
    if (layer.tex == 0) {
        return;
    }
    const float t = clamp01((in.age - layer.delay) / layer.life);
    if (in.age < layer.delay || t >= 1.0f) {
        return;
    }

    ImVec2 uv0(0.0f, 0.0f);
    ImVec2 uv1(1.0f, 1.0f);
    float alpha = layer.alpha;
    if (layer.single) {
        // The flare has no baked fade, so it fades here.
        alpha *= std::pow(1.0f - t, 1.6f);
    } else {
        // frameOverTime is a linear 0 -> 1 ramp over the lifetime.
        int frame = static_cast<int>(t * static_cast<float>(layer.frames));
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

    const float diameter = layer.diameter * scale;
    const float half = diameter * 0.5f;
    drawList->AddImage(layer.tex, ImVec2(in.x - half, in.y - half), ImVec2(in.x + half, in.y + half),
        uv0, uv1, IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f + 0.5f)));
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
    drawList->AddImage(mRing, min, max, ImVec2(0, 0), ImVec2(1, 1),
        shadow ? fadeShadow(alpha) : withAlpha(kRingTint, alpha));
}

void TapEffect::drawShard(ImDrawList* drawList, const Instance& in, const Piece& piece, float scale,
    float alphaScale, bool shadow) const
{
    const ImTextureID tex = piece.outline ? mOutline : mFill;
    if (tex == 0) {
        return;
    }
    const float t = clamp01(in.age / kTriangleDuration);
    const float alpha = std::pow(1.0f - t, 1.1f) * alphaScale;
    if (alpha <= 0.004f) {
        return;
    }

    const float travel = (kShardSpawnRadius + easeOutQuad(t) * kShardDistance * piece.distanceScale)
        * scale;
    const float fanRotation = (static_cast<float>(in.seed % 10) / 10.0f) * 360.0f
        + static_cast<float>(in.seed % 7) * 5.0f;
    const float angle = (piece.angleDeg + fanRotation) * kDegToRad;

    const float size = (kShardSizeStart + (kShardSizeEnd - kShardSizeStart) * t) * piece.sizeScale
        * scale;
    const float half = size * 0.5f;

    const float cx = in.x + std::cos(angle) * travel;
    const float cy = in.y + std::sin(angle) * travel;
    const float rotation = (piece.spinDeg * easeOutQuad(t) + fanRotation) * kDegToRad;
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);

    const float offset = shadow ? 1.5f * scale : 0.0f;

    const ImVec2 local[4] = {
        {-half, -half}, {half, -half}, {half, half}, {-half, half},
    };
    const ImVec2 uv[4] = {
        ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
    };
    ImVec2 points[4];
    for (int i = 0; i < 4; ++i) {
        points[i].x = cx + local[i].x * c - local[i].y * s + offset;
        points[i].y = cy + local[i].x * s + local[i].y * c + offset;
    }
    const ImU32 tint = piece.outline ? kOutlineTint : kFillTint;
    drawList->AddImageQuad(tex, points[0], points[1], points[2], points[3], uv[0], uv[1], uv[2],
        uv[3], shadow ? fadeShadow(alpha) : withAlpha(tint, alpha));
}

} // namespace game
