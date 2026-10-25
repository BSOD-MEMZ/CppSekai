// CppSekai - PJSK style global tap feedback.
//
// Reverse engineered from pjsk 7.1.0's `ScreenLayerTouchEffect`. That prefab
// holds THREE different effects, and they look nothing alike:
//
//   fx_Tap_single  a tap:        one 160px ripple + one big shard, then gone
//   Follow         a held press: 330px ripple looping + 650px glow + shards at 6/s
//   fx_Tap_flick   dragging:     no ripple at all (its particle scales are
//                                1 / 0.1 / 0.1, i.e. sub-pixel - the group is
//                                effectively invisible in the shipped build)
//
// So this class models two states, not three: Tap (a burst that dies on its own)
// and Hold (a live effect that follows the finger). Dragging is Hold with a
// moving position, which is what the original ends up looking like too.
//
// Measured values (raw YAML in D:\Tools\rea\work\jp\fx_touch\, grouped into fx by
// jp_fx_group.py; write-up in RE-report-jp.md section 11):
//
//   PT_01   single: startSize 160, life 0.5, uv 4x4 WholeSheet fps 30, rate 0
//           hold  : startSize 330, life 0.45, same sheet, looping
//   PT_02   hold only: startSize 650, life 0.5, rateOverTime 4/s
//   particle3 single: size 2.5..235 (random between two curves), life 0.3..0.7,
//                     startSpeed 35..100, one particle at t=0
//           hold  : size 42..147, life 0.3..1.5, startSpeed 0..0.45, 6/s
//   particle2 single: 15.7..18.2 ; hold: 10.5..21 at 6/s
//   particle2_1 single only: 1.2..11.7
//   particle1 life 30..35s in the original (clamped here): tiny specks
//
// All four particle nodes sit at RectTransform scale 157 in fx_Tap_single and
// 210 in Follow; scalingMode is Hierarchy, so world size = startSize * scale.
//
// The originals' startSpeed points along the disc normal (out of the screen),
// which an orthographic 2D view cannot show, so kRadialGain reinterprets it as
// outward drift. Everything else - sizes, lifetimes, rates, colour keyframes -
// is measured, apart from kFxScale (a global shrink, see below).
//
// Never spawned during the play state: there a pointer press is a note hit and
// the judgement input path must stay untouched (see main.cpp).
#pragma once

#include "imgui.h"

#include <string>
#include <vector>

namespace platform
{
class Renderer;
}

namespace game
{
class TapEffect
{
  public:
    // Loads assets/fx/tap_*.png through the renderer's UI texture loader.
    // Missing files degrade gracefully (the effect silently does nothing).
    bool load(platform::Renderer& renderer, const std::string& fxDir, std::string& outError);
    bool loaded() const { return mSheet != 0 || mRing != 0; }

    // One-shot burst (used by --tap-fx-test and kept for callers that only want
    // the "tap" half).
    void spawn(float x, float y);
    void spawnAt(float x, float y, float age);

    // Pointer went down: fires the tap burst and enters the held state.
    void press(float x, float y);
    // Pointer moved while held: drags the held effect along.
    void moveHold(float x, float y);
    // Pointer went up: drops the held state.
    void release();
    bool holding() const { return mHold.active; }

    // Headless only: build a held-press state that has already been running for
    // `age` seconds, so one screenshot can show what a long press looks like.
    void debugHoldAt(float x, float y, float age);

    // Freezes ageing (headless verification only): keeps the hold effect alive
    // and stops tickers so a screenshot is deterministic.
    void setFrozen(bool frozen) { mFrozen = frozen; }

    void update(float dt);
    // Draws on the foreground draw list (above every screen).
    void draw();

    void clear();

  private:
    // One mesh particle. All values are already randomised at spawn time so a
    // fixed seed replays exactly the same burst (needed by --tap-fx-test).
    struct Shard
    {
        float born = 0.0f;   // age at which it appears, seconds
        float life = 1.0f;   // seconds
        float radius = 0.0f; // spawn distance from the tap point, design px
        float angle = 0.0f;  // spawn direction, radians
        float speed = 0.0f;  // radial drift, design px/s (see kRadialGain)
        float size = 10.0f;  // quad diameter, design px
        // The original is a MESH particle with startRotation +-2pi and
        // RotationOverLifetime pi rad/s on all three axes, so the triangle
        // tumbles in 3D and foreshortens as it turns. Rotating in 2D only (the
        // old behaviour) misses that entirely.
        float rot0x = 0.0f, rot0y = 0.0f, rot0z = 0.0f;
        float spinX = 0.0f, spinY = 0.0f, spinZ = 0.0f;
        int cell = 0;        // 0..3, which 2x2 colour cell of the sheet
        int grad = -1;       // index into the ColorOverLifetime tables, -1 = none
    };

    struct Instance
    {
        float x = 0.0f;
        float y = 0.0f;
        float age = 0.0f;
        std::vector<Shard> shards;
    };

    // The held-press effect: one ripple looping forever, a steady glow, and
    // shards emitted at a fixed rate, all anchored to the pointer.
    struct HoldState
    {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float age = 0.0f;
        float emitAccum = 0.0f;
        std::vector<Shard> shards;
    };

    // One flipbook layer (the ripple).
    struct FlipLayer
    {
        ImTextureID tex = 0; // 0 = skip
        int tiles = 4;       // tiles per axis
        int frames = 16;     // frames actually used
        float life = 0.5f;   // seconds per loop
        float diameter = 160.0f; // design px
        float alpha = 1.0f;
    };

    void buildTapShards(Instance& instance, unsigned& rng) const;
    void emitHoldShard(HoldState& hold, unsigned& rng) const;
    void drawFlip(ImDrawList* dl, float cx, float cy, float age, const FlipLayer& layer,
        float scale) const;
    void drawShard(ImDrawList* dl, float originX, float originY, float instanceAge,
        const Shard& shard, float scale) const;
    void drawGlow(ImDrawList* dl, float cx, float cy, float alpha, float scale) const;
    void drawRingLegacy(ImDrawList* dl, float cx, float cy, float age, float scale,
        float alphaScale, bool shadow) const;

    ImTextureID mSheet = 0;  // 4x4 main ripple      (tex_tap_texturesheet)
    ImTextureID mTri = 0;    // 2x2 colour triangles (tex_sekai_01_2)
    ImTextureID mGlow = 0;   // single radial glow   (tex_tap_flare)
    ImTextureID mRing = 0;   // legacy fallback ring
    std::vector<Instance> mActive;
    HoldState mHold;
    int mNextSeed = 0;
    bool mFrozen = false;
};

} // namespace game
