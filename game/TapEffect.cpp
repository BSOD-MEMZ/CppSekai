// CppSekai - PJSK style global tap feedback. See TapEffect.hpp.
//
// Reproduces the pjsk button tap feedback: two staggered rings expand from the
// press point while roughly ten gradient-filled irregular triangles fly
// outward, spin down and fade. All pieces are white masks, so the color comes
// from the tint tables below - the same textures serve every screen.
//
// NOTE: assets/fx/tap_tri_0.png is deliberately NOT loaded. It looks like a
// single triangle but is actually a 10-sprite atlas; rendering it whole
// stretches the entire sheet into one blob.
#include "TapEffect.hpp"

#include "Ui.hpp"
#include "platform/Renderer.hpp"

#include <algorithm>
#include <cmath>

namespace game
{
namespace
{
// Total life of the outer ring, in seconds. pjsk's ripple is quick and snappy.
constexpr float kDuration = 0.32f;
// Shards live shorter than the ring: they are gone while it still expands.
constexpr float kTriangleDuration = 0.24f;
// Outer ring diameter in design pixels at the end of the animation.
constexpr float kRingDiameter = 140.0f;
// How far the shards travel beyond their spawn radius, in design pixels.
constexpr float kShardDistance = 46.0f;
// Shards start slightly off the tap point, not dead centre.
constexpr float kShardSpawnRadius = 14.0f;
// Shard edge length in design pixels at spawn / at the end.
constexpr float kShardSizeStart = 20.0f;
constexpr float kShardSizeEnd = 12.0f;

constexpr int kShardCount = 10;

// One flying shard: flight direction, total spin, fill vs outline mask,
// distance and size jitter. Angles cover the full circle in 36 degree steps
// with a wobble so the fan does not look like a clock face. Fixed values (no
// RNG) so a screenshot is reproducible.
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

// Tap feedback tints, taken from the ui:: pjsk palette so the effect matches
// the rest of the screens. The textures are white masks, so this is where the
// color actually comes from - edit here to retune the whole effect.
constexpr ImU32 kRingTint = IM_COL32(206, 250, 242, 255); // near-white mint
constexpr ImU32 kFillTint = IM_COL32(106, 232, 208, 255); // ui::kPrimary mint
constexpr ImU32 kOutlineTint = IM_COL32(255, 102, 158, 255); // ui::kCheckPink pink

// Keeps the RGB of a pjsk palette color and swaps in the fade alpha.
ImU32 withAlpha(ImU32 color, float alpha)
{
    const auto a = static_cast<ImU32>(alpha * 255.0f + 0.5f);
    return (color & 0x00FFFFFFu) | (a << 24);
}

ImU32 fadeShadow(float alpha)
{
    const int a = static_cast<int>(alpha * 255.0f + 0.5f);
    // Dark blue-gray, close to the pjsk text shadow color.
    return IM_COL32(48, 52, 72, a);
}

float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Slow-out easing. Used for the ring scale (pops out, settles gently).
float easeOutCubic(float t)
{
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// Gentler slow-out. Shard travel and spin use this: cubic reaches full
// distance at ~60% of the life and then freezes, which reads as mechanical.
float easeOutQuad(float t)
{
    const float inv = 1.0f - t;
    return 1.0f - inv * inv;
}

} // namespace

bool TapEffect::load(platform::Renderer& renderer, const std::string& fxDir, std::string& outError)
{
    struct Entry
    {
        const char* file;
        ImTextureID* out;
    };
    const Entry entries[] = {
        {"tap_ring.png", &mRing},
        {"tap_tri_2.png", &mFill},    // gradient-filled irregular triangle
        {"tap_tri_1.png", &mOutline}, // gradient-outlined irregular triangle
    };

    bool anyLoaded = false;
    for (const Entry& entry : entries) {
        const std::string path = fxDir + "\\" + entry.file;
        std::string error;
        const GLuint id = renderer.loadUiTexture(path, error);
        if (id == 0) {
            // Missing pieces are not fatal: the effect just skips that layer.
            outError = error;
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
    if (mRing == 0) {
        return;
    }
    Instance instance;
    instance.x = x;
    instance.y = y;
    instance.age = 0.0f;
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
    for (Instance& instance : mActive) {
        instance.age += dt;
    }
    mActive.erase(std::remove_if(mActive.begin(), mActive.end(),
                      [](const Instance& instance) { return instance.age >= kDuration; }),
        mActive.end());
}

void TapEffect::draw()
{
    if (mActive.empty() || mRing == 0) {
        return;
    }
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (drawList == nullptr) {
        return;
    }
    const float scale = ui::scale();
    // Shadow pass first so the white pieces sit cleanly on light cards.
    for (const Instance& instance : mActive) {
        drawRing(drawList, instance, scale, 0.30f, true);
        for (int i = 0; i < kShardCount; ++i) {
            drawShard(drawList, instance, kPieces[i], scale, 0.30f, true);
        }
    }
    // Color pass.
    for (const Instance& instance : mActive) {
        drawRing(drawList, instance, scale, 1.0f, false);
        for (int i = 0; i < kShardCount; ++i) {
            drawShard(drawList, instance, kPieces[i], scale, 1.0f, false);
        }
    }
}

void TapEffect::drawRing(ImDrawList* drawList, const Instance& in, float scale, float alphaScale,
    bool shadow) const
{
    // Outer ring first, then the tighter / fainter inner one a beat later.
    const RingPhase phases[2] = {
        {0.00f, 0.15f, 1.00f, 1.00f},
        {0.05f, 0.10f, 0.70f, 0.55f},
    };
    for (const RingPhase& phase : phases) {
        const float life = kDuration - phase.delay;
        const float t = clamp01((in.age - phase.delay) / life);
        if (t <= 0.0f) {
            continue;
        }
        const float alpha = std::pow(1.0f - t, 1.5f) * phase.alphaScale * alphaScale;
        if (alpha <= 0.004f) {
            continue;
        }
        const float diameter =
            kRingDiameter * scale * (phase.startScale + (phase.endScale - phase.startScale) * easeOutCubic(t));

        // The shadow copy sits slightly down-right, like the pjsk text shadow.
        const float offset = shadow ? 1.5f * scale : 0.0f;
        const ImVec2 min(in.x - diameter * 0.5f + offset, in.y - diameter * 0.5f + offset);
        const ImVec2 max(in.x + diameter * 0.5f + offset, in.y + diameter * 0.5f + offset);
        drawList->AddImage(mRing, min, max, ImVec2(0, 0), ImVec2(1, 1),
            shadow ? fadeShadow(alpha) : withAlpha(kRingTint, alpha));
    }
}

void TapEffect::drawShard(ImDrawList* drawList, const Instance& in, const Piece& piece, float scale,
    float alphaScale, bool shadow) const
{
    const ImTextureID tex = piece.outline ? mOutline : mFill;
    if (tex == 0) {
        return;
    }
    // Shards fade on their own, shorter clock than the ring, and nearly linear
    // so they are visible for most of their life instead of blinking.
    const float t = clamp01(in.age / kTriangleDuration);
    const float alpha = std::pow(1.0f - t, 1.1f) * alphaScale;
    if (alpha <= 0.004f) {
        return;
    }

    const float travel = (kShardSpawnRadius + easeOutQuad(t) * kShardDistance * piece.distanceScale)
        * scale;
    // The whole fan is rotated a little per burst so repeated clicks vary.
    const float fanRotation = (static_cast<float>(in.seed % 10) / 10.0f) * 360.0f
        + static_cast<float>(in.seed % 7) * 5.0f;
    const float angle = (piece.angleDeg + fanRotation) * kDegToRad;

    const float size = (kShardSizeStart + (kShardSizeEnd - kShardSizeStart) * t) * piece.sizeScale
        * scale;
    const float half = size * 0.5f;

    const float cx = in.x + std::cos(angle) * travel;
    const float cy = in.y + std::sin(angle) * travel;
    // Spin decelerates along with the travel, not linear - a constant spin
    // keeps rotating after the piece has visibly stopped and looks wrong.
    const float rotation = (piece.spinDeg * easeOutQuad(t) + fanRotation) * kDegToRad;
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);

    // Shadow copy drifts down-right, then the colored piece on top.
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
