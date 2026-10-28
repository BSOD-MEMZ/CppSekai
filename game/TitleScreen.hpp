// CppSekai - pjsk's title screen ("TAP TO START").
//
// Everything here comes out of the 7.1.0 asset pack; nothing was redrawn. The
// files live in `assets/title/` (git-ignored, local only - see COPYRIGHT.md):
//
//   tex_title_logo_mult_01.png  1968x416  the logo (Miku + wordmark, one image)
//   tex_title_logo_all_02.png    713x243   compact logo variant (unused for now)
//   txt_taptostart.png           560x48    "TAP TO START" (sprite, auto-cropped)
//   tex_title_copy_01/02.png     734x46    the copyright line (two variants)
//   tex_title_jasrac.png         698x64    JASRAC notice
//   tex_title_soft.png           268x80    soft glow behind the tap prompt
//   tex_title_sweep_01.png       128x128   light sweep mask (unused for now)
//   tex_title_btn_01.png         120x124   the round menu button
//   sub_title_logo_glow.png      256x128   small glow
//
// **What is NOT in the pack**: the title *background*. The original streams it
// (a character/room animation), so it is not inside UnityDataAssetPack. The
// caller supplies the backdrop; this module only draws the overlay art on top
// of whatever is already on screen.
//
// Layout is authored in the same 1920x1080 virtual canvas the HUD, result and
// song-select screens use, and every number below is a *destination* size for a
// known source size, so changing one does not silently distort the art.
#pragma once

#include "platform/Renderer.hpp"

#include <string>

namespace game
{

class TitleScreen
{
public:
    // Loads the title art from `<titleDir>`. Missing files are reported through
    // outError but are not fatal: whatever loaded is still drawn, so a pruned
    // assets folder degrades instead of breaking.
    bool load(platform::Renderer& renderer, const std::string& titleDir, std::string& outError);
    void release();

    [[nodiscard]] bool loaded() const { return mLogo != 0; }

    // Draws on ImGui's *foreground* list (same layer as the transition overlays),
    // so it covers the song select underneath. `elapsedSec` drives the pulse.
    //
    // `backdropTex` is the same wallpaper texture the song select uses (whatever
    // `refreshSelectBackdrop` produced; 0 when the user picked another background
    // style). It is drawn cover-fit with `backdropDim` on top, so the title reads
    // as its own screen instead of a sticker over the song list. 0 = draw no
    // backdrop at all, which leaves whatever is already on screen visible.
    void draw(int windowW, int windowH, float uiScale, float elapsedSec, unsigned backdropTex = 0,
        int backdropW = 0, int backdropH = 0, float backdropDim = 0.35f) const;

    // Debug hook: CPSEKAI_TITLE_T pins the animation clock so a headless
    // --screenshot can catch a deterministic frame. Negative = run normally.
    static float frozenTime();

private:
    unsigned mLogo = 0;
    int mLogoW = 0;
    int mLogoH = 0;
    unsigned mTap = 0;
    int mTapW = 0;
    int mTapH = 0;
    unsigned mCopy = 0;
    int mCopyW = 0;
    int mCopyH = 0;
    unsigned mJasrac = 0;
    int mJasracW = 0;
    int mJasracH = 0;
    unsigned mSoft = 0;
    int mSoftW = 0;
    int mSoftH = 0;
};

} // namespace game
