// CppSekai - the burst that leaves the 确定 button.
//
// Reverse engineered from pjsk 7.1.0: `ScreenLayerFreeLiveCellPhone.PlayButtonEffect()`
// (dump.cs) drives `SoloLiveTransitionEffectContent`, which parents three prefabs:
//
//   fx_live_transition_v2_02         light 1532x2357 (a glow bigger than the whole
//                                    handset), back_light, flare/flare_white, tri_02,
//                                    all under a `cellphone_mask` (1016x2221)
//   fx_live_transition_botton_v2_01  at (17,189) - i.e. on the button itself:
//                                    botton_light_01/02 + line_circle
//   fx_live_transition_v2_01         tri_botton, botton_light, line_circle, circle,
//                                    tri_01/tri_02 (again under cellphone_mask)
//
// Measured particle values (native ParticleSystem, one per node):
//
//   node              life      startSpeed   startSize
//   back_light        2 .. 4    8 -> 0       2
//   line_circle       0.8 .. 4  0            4
//   botton_light_01   1 .. 4    0            2.77
//   tri_01            2 .. 4    25 -> 10     1
//
// Every one of them is pure white (1,1,1,1); the colour comes from an additive
// material, and the triangles are the ONLY nodes with a start speed - which is
// why "光晕 + 射出去的三角形" is exactly what it looks like.
//
// This class models the readable part of that (the original runs for seconds,
// this is a 0.9 s burst): one big additive glow that spreads over the handset,
// a ring that expands off the button, and a dozen triangles thrown outward.
// Everything is drawn on the foreground draw list, like TapEffect.
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
class DecideBurst
{
  public:
    // Loads assets/fx/tap_flare.png (glow) and assets/fx/tap_tri.png (triangles)
    // through the renderer's UI texture loader. Missing files just skip that layer.
    bool load(platform::Renderer& renderer, const std::string& fxDir, std::string& outError);
    bool loaded() const { return mGlow != 0 || mTri != 0; }

    // cx,cy = the 确定 button's centre in screen pixels (already tilted with the
    // handset); spreadRadius = how far the glow reaches, roughly the panel width.
    void spawn(float cx, float cy, float spreadRadius);

    void update(float dt);
    void draw();
    void clear();

    [[nodiscard]] bool active() const { return !mActive.empty(); }

  private:
    struct Shard
    {
        float angle = 0.0f;   // launch direction, radians
        float delay = 0.0f;   // seconds after the press
        float startRadius = 0.0f; // where it leaves the button, px
        float speed = 0.0f;   // px/s at t=0, decays
        float size = 20.0f;   // quad edge, design px
        // 3D pose. Original: startRotationX/Y/Z are each a random +-2pi, and
        // RotationModule only spins Z (separateAxes, +-0.7854 rad/s). A quad
        // that starts at a random 3D angle and is projected orthographically
        // foreshortens into anything from a full triangle to a thin sliver -
        // that spread is what the original actually looks like. We also nudge
        // X/Y so each shard keeps turning on screen instead of freezing its
        // silhouette (the only place this departs from the measured values).
        float rot0x = 0.0f, rot0y = 0.0f, rot0z = 0.0f;
        float spinX = 0.0f, spinY = 0.0f, spinZ = 0.0f;
        int cell = 0;         // 2x2 atlas cell
    };

    struct Burst
    {
        float cx = 0.0f;
        float cy = 0.0f;
        float radius = 400.0f;
        float age = 0.0f;
        std::vector<Shard> shards;
    };

    ImTextureID mGlow = 0;
    ImTextureID mTri = 0;
    std::vector<Burst> mActive;
};

} // namespace game
