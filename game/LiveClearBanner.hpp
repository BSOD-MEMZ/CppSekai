// CppSekai - the "LIVE CLEAR!" banner that lands on the last note.
//
// Reverse engineered from pjsk 7.1.0: it is NOT a line of text and NOT a single
// picture. `Assets/Resources/effects/result/prefab/fx_result_LiveClear` holds ten
// separate Image nodes - one per letter - plus a full additive duplicate layer
// ("text_ADD"), a 1148x188 backlight strip and three particle systems. The
// letters are cut from one atlas (tex_LiveClear, 1236x182, 108 px/unit) so each
// one can be animated on its own.
//
// `clip_result_LiveClear` (1.5167 s, baked at 60 fps) only touches three
// properties: m_AnchoredPosition.x on the ten letters, m_Color.a on text_ADD /
// tex_back, and m_IsActive. Measured keyframes:
//
//   0.0167  everything switches on
//   0.0167 -> 0.1833  every letter slides from x=0 to its slot; text_ADD 0 -> 1
//   0.05    `pt` fires once
//   0.1833 -> 0.6833  tex_back fades 0 -> 1
//   0.2667  text_ADD alpha 0.51
//   0.5167  `pt_loop` starts looping
//   0.6833  text_ADD alpha ~0 (the glow has burnt out, the plain layer stays)
//   1.5167  done
//
// There is no scale curve anywhere: the "slam outward" read comes from all ten
// letters starting at the same point and arriving at the same time over
// different distances.
//
// Geometry (1920x1080 canvas, same virtual space the HUD and result screen use):
// the group as a whole is centred on the screen; letter heights are 147-152 and
// the row spans 1112 px from L1's left edge to the "!"'s right edge.
#pragma once

#include "platform/Renderer.hpp"

namespace game
{

class LiveClearBanner
{
public:
    // Total length of the original clip. The caller uses it to delay the switch
    // to the result screen.
    static constexpr float kDuration = 1.5166667f;

    // Frame where the blackout takes over, matching the clip's tail: the banner
    // is still fully drawn at 1.25 s and the screen is black at 1.5167 s.
    void start();
    void reset();
    void update(float dt);

    [[nodiscard]] bool active() const { return mActive; }
    [[nodiscard]] float time() const { return mTime; }
    [[nodiscard]] bool finished() const { return mActive && mTime >= kDuration; }

    // 0 until 1.25 s, then ramps to 1 at the end of the clip - the fade that
    // hands the screen to the result view.
    [[nodiscard]] float blackout() const;

    // Draws on ImGui's *foreground* draw list (the same layer the transition
    // overlays use), so it sits above the HUD and the playfield.
    void draw(platform::Renderer& renderer, int windowW, int windowH, float uiScale) const;

    // Debug only: CPSEKAI_BANNER_T pins the clock at that many seconds (see
    // .cpp). Returns a negative value when the banner should run normally.
    static float frozenTime();

private:
    bool mActive = false;
    float mTime = 0.0f;
};

} // namespace game
