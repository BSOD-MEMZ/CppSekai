// CppSekai - PJSK style global tap feedback.
// An expanding double ring plus a fan of gradient-filled irregular triangles
// spawned wherever the player clicks on a non-play screen. Textures are the
// original pjsk white masks (assets/fx/), tinted per-instance.
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
    // Two rings per burst, staggered: pjsk's ripple reads as a double ring.
    struct RingPhase
    {
        float delay;       // start offset, seconds
        float startScale;  // diameter at t=0, relative to kRingDiameter
        float endScale;    // diameter at the end
        float alphaScale;  // brightness relative to the outer ring
    };

    // One flying shard.
    struct Piece
    {
        float angleDeg;    // flight direction
        float spinDeg;     // total rotation over its life
        bool outline;      // true = gradient outline mask, false = gradient fill
        float distanceScale;
        float sizeScale;
        ImU32 tint;        // per-piece color: the original fans out many hues
    };

    // Loads assets/fx/tap_*.png through the renderer's UI texture loader.
    // Missing files degrade to "not loaded" (the effect silently does nothing).
    bool load(platform::Renderer& renderer, const std::string& fxDir, std::string& outError);
    bool loaded() const { return mRing != 0; }

    // Spawns one burst at a window-pixel position.
    void spawn(float x, float y);

    void update(float dt);
    // Draws on the foreground draw list (above every screen).
    void draw();

    void clear() { mActive.clear(); }

  private:
    struct Instance
    {
        float x = 0.0f;
        float y = 0.0f;
        float age = 0.0f;
        int seed = 0; // rotates the whole fan, keeps bursts varied
    };

    void drawRing(ImDrawList* dl, const Instance& in, float scale, float alphaScale,
        bool shadow) const;
    void drawShard(ImDrawList* dl, const Instance& in, const Piece& piece, float scale,
        float alphaScale, bool shadow) const;

    ImTextureID mRing = 0;      // ImTextureID == ImU64, 0 = not loaded
    ImTextureID mFill = 0;      // gradient-filled irregular triangle
    ImTextureID mOutline = 0;   // gradient-outlined irregular triangle
    std::vector<Instance> mActive;
    int mNextSeed = 0;
};

} // namespace game
