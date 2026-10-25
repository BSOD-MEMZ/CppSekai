// CppSekai - PJSK style global tap feedback.
//
// Everything below is reverse engineered from pjsk 7.1.0's
// `ScreenLayerTouchEffect` prefab (Assets/Resources/screen/prefabs/), read out
// through AssetRipper. The real effect is a Unity ParticleSystem cluster:
//
//   root
//     PT_01      billboard, 4x4 texture sheet (tex_tap_texturesheet)
//     PT_02      billboard, single flare (tex_tap_flare)
//     particle3  MESH triangle, big      (tex_sekai_01_2, 2x2 colour cells)
//     particle2_1 MESH triangle, medium
//     particle1  MESH triangle, tiny flakes
//
// Measured values (see RE-report-jp.md section 9/10 for the raw YAML):
//   PT_01       life 0.5s, speed 0, startSize 160, UV 4x4 WholeSheet, fps 30,
//               frameOverTime linear 0->1. Transform scale 1.
//   PT_02       life 0.5s, speed 0, startSize 650, rate 4/s, no UV module.
//   particle3   startSize 0.2..0.7, Transform scale 210 -> 42..147 px,
//               life 0.3..1.5s, spawn ring radius 1*210 = 210 px (edge only),
//               startRotation +-2pi, rotate 180 deg/s, UV 2x2 with a random
//               start frame (so each triangle picks one of the four colours).
//   particle2_1 startSize 0.09, scale 157 -> 14 px, life 0.2..0.7s,
//               ring 157 px, speed 35..100.
//   particle1   startSize 0.01..0.05, scale 210 -> 2.1..10.5 px, life 1..30s
//               (clamped to 3s here: a 30s tail is meaningless as tap feedback).
//
// The mesh particles all share the same triangle mesh (sekai_PT_05) and draw
// with ColorOverLifetime FADING IN, not out:
//   particle3   alpha 0 -> 0.5, tint (0.138, 0.175, 0.302)  dark slate blue
//   particle2_1 alpha 0 -> 1.0, tint white
//   particle1   no colour module at all
// So they read as translucent coloured glass shards, not as glowing sparks.
//
// Their `startSpeed` points along the disc normal (out of the screen), which is
// invisible in an orthographic 2D view; the small radial drift we add is an
// explicit artistic translation of that speed, see kRadialGain.
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

    // Spawns one burst at a window-pixel position with age 0.
    void spawn(float x, float y);
    // Same, but starts at a given age - used by --tap-fx-test so a headless
    // screenshot can show several phases at once.
    void spawnAt(float x, float y, float age);

    // Freezes ageing (headless verification only).
    void setFrozen(bool frozen) { mFrozen = frozen; }

    void update(float dt);
    // Draws on the foreground draw list (above every screen).
    void draw();

    void clear() { mActive.clear(); }

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
        float rot0 = 0.0f;   // initial spin, radians
        float spin = 0.0f;   // radians/s (original: a constant pi)
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

    // One flipbook layer of a burst (PT_01 / PT_02).
    struct FlipLayer
    {
        ImTextureID tex = 0; // 0 = skip
        int tiles = 4;       // tiles per axis
        int frames = 16;     // frames actually used
        float life = 0.5f;   // seconds
        float diameter = 160.0f; // design px
        float alpha = 1.0f;
        bool single = false; // true = single image, no UV tiling (the flare)
    };

    void layerList(FlipLayer (&out)[2]) const;
    void buildShards(Instance& instance, unsigned& rng) const;
    void drawFlip(ImDrawList* dl, float cx, float cy, float t, const FlipLayer& layer,
        float scale) const;
    void drawShard(ImDrawList* dl, const Instance& in, const Shard& shard, float scale) const;
    void drawRingLegacy(ImDrawList* dl, const Instance& in, float scale, float alphaScale,
        bool shadow) const;

    ImTextureID mSheet = 0;    // 4x4 main ripple      (tex_tap_texturesheet)
    ImTextureID mFlare = 0;    // single radial glow   (tex_tap_flare)
    ImTextureID mTri = 0;      // 2x2 colour triangles (tex_sekai_01_2)
    ImTextureID mRing = 0;     // legacy fallback ring
    std::vector<Instance> mActive;
    int mNextSeed = 0;
    bool mFrozen = false;
};

} // namespace game
