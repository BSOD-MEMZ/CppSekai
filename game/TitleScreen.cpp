// CppSekai - pjsk title screen. See TitleScreen.hpp for where every file came from.
#include "game/TitleScreen.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace game
{
namespace
{

constexpr float kCanvasW = 1920.0f;
constexpr float kCanvasH = 1080.0f;

// --- destination geometry ---------------------------------------------------
// Each entry is a *target width* for a known source image, so the aspect ratio
// can never drift: the height is derived from the source size at draw time.
constexpr float kLogoWidth = 1150.0f;
constexpr float kLogoCenterY = 392.0f;

constexpr float kSoftWidth = 520.0f;
constexpr float kTapWidth = 340.0f;
constexpr float kTapCenterY = 884.0f;

constexpr float kCopyWidth = 330.0f;
constexpr float kJasracWidth = 214.0f;
constexpr float kFinePrintRight = 1888.0f;
constexpr float kFinePrintY = 1020.0f;
constexpr float kFinePrintGap = 26.0f;

// --- animation --------------------------------------------------------------
// The official title pulses the tap prompt; the logo only breathes a little.
constexpr float kPulsePeriod = 1.6f;
constexpr float kPulseMin = 0.30f;
constexpr float kBreathPeriod = 4.6f;
constexpr float kBreathAmount = 0.006f;

struct Canvas
{
    ImDrawList* dl = nullptr;
    float scale = 1.0f;
    float ox = 0.0f;
    float oy = 0.0f;

    [[nodiscard]] ImVec2 p(float vx, float vy) const
    {
        return ImVec2(ox + vx * scale, oy + vy * scale);
    }
};

Canvas makeCanvas(ImDrawList* dl, int windowW, int windowH, float uiScale)
{
    Canvas c;
    c.dl = dl;
    c.scale = std::min(static_cast<float>(windowW) / kCanvasW,
        static_cast<float>(windowH) / kCanvasH) * std::clamp(uiScale, 0.5f, 2.0f);
    c.ox = (static_cast<float>(windowW) - kCanvasW * c.scale) * 0.5f;
    c.oy = (static_cast<float>(windowH) - kCanvasH * c.scale) * 0.5f;
    return c;
}

ImU32 tint(float alpha)
{
    const int a = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return IM_COL32(255, 255, 255, a);
}

// Draws `tex` at `destWidth`, keeping the source aspect, centred on (cx, cy).
void drawArt(ImDrawList* dl, unsigned tex, int srcW, int srcH, float destWidth,
    const Canvas& c, float cx, float cy, ImU32 color)
{
    if (tex == 0 || srcW <= 0 || srcH <= 0) {
        return;
    }
    const float w = destWidth;
    const float h = destWidth * static_cast<float>(srcH) / static_cast<float>(srcW);
    dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
        c.p(cx - w * 0.5f, cy - h * 0.5f), c.p(cx + w * 0.5f, cy + h * 0.5f),
        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), color);
}

// Draws `tex` with its right edge at `rightEdge`.
void drawArtRight(ImDrawList* dl, unsigned tex, int srcW, int srcH, float destWidth,
    const Canvas& c, float rightEdge, float cy, ImU32 color)
{
    if (tex == 0 || srcW <= 0 || srcH <= 0) {
        return;
    }
    const float w = destWidth;
    const float h = destWidth * static_cast<float>(srcH) / static_cast<float>(srcW);
    dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
        c.p(rightEdge - w, cy - h * 0.5f), c.p(rightEdge, cy + h * 0.5f),
        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), color);
}

} // namespace

float TitleScreen::frozenTime()
{
    static const float value = [] {
        const char* raw = std::getenv("CPSEKAI_TITLE_T");
        if (raw == nullptr) {
            return -1.0f;
        }
        return static_cast<float>(std::atof(raw));
    }();
    return value;
}

bool TitleScreen::load(platform::Renderer& renderer, const std::string& titleDir, std::string& outError)
{
    outError.clear();

    struct Entry
    {
        const char* file;
        unsigned* id;
        int* w;
        int* h;
        int srcW;
        int srcH;
        bool required;
    };
    // srcW/srcH are the real pixel sizes of the files in assets/title/ - they are
    // recorded here (rather than read back at runtime) so the aspect ratio of
    // every element is visible in one table.
    const Entry entries[] = {
        {"tex_title_logo_mult_01.png", &mLogo, &mLogoW, &mLogoH, 1968, 416, true},
        {"txt_taptostart.png", &mTap, &mTapW, &mTapH, 560, 48, true},
        {"tex_title_copy_01.png", &mCopy, &mCopyW, &mCopyH, 734, 46, false},
        {"tex_title_jasrac.png", &mJasrac, &mJasracW, &mJasracH, 698, 64, false},
        {"tex_title_soft.png", &mSoft, &mSoftW, &mSoftH, 268, 80, false},
        // Boot splash cards. They are plain Texture2D in globalgamemanagers (not
        // sprites), which is why a sprite-name scan never found them.
        {"splash_sega.png", &mSega, &mSegaW, &mSegaH, 766, 256, false},
        {"splash_cp.png", &mCp, &mCpW, &mCpH, 1080, 151, false},
    };

    for (const Entry& entry : entries) {
        const std::string path = titleDir + "\\" + entry.file;
        std::string error;
        const unsigned id = renderer.loadUiTexture(path, error);
        if (id == 0) {
            if (entry.required) {
                outError = error;
            }
            *entry.id = 0;
            continue;
        }
        *entry.id = id;
        *entry.w = entry.srcW;
        *entry.h = entry.srcH;
    }
    return mLogo != 0;
}

void TitleScreen::release()
{
    // The renderer owns the GL textures (they are dropped with it), so all this
    // does is forget the handles - load() will look them up again.
    mLogo = mTap = mCopy = mJasrac = mSoft = 0;
    mSega = mCp = 0;
    mLogoW = mLogoH = mTapW = mTapH = 0;
    mCopyW = mCopyH = mJasracW = mJasracH = 0;
    mSoftW = mSoftH = 0;
    mSegaW = mSegaH = mCpW = mCpH = 0;
}

void TitleScreen::draw(int windowW, int windowH, float uiScale, float elapsedSec,
    unsigned backdropTex, int backdropW, int backdropH, float backdropDim) const
{
    if (mLogo == 0) {
        return;
    }
    const float frozen = frozenTime();
    const float raw = frozen >= 0.0f ? frozen : std::max(elapsedSec, 0.0f);
    const Canvas c = makeCanvas(ImGui::GetForegroundDrawList(), windowW, windowH, uiScale);
    ImDrawList* dl = c.dl;

    // --- boot splash: SEGA (1 s) -> Colorful Palette (2 s), on black ---------
    // The official sequence is Unity's own splash followed by these two cards;
    // we are not Unity, so it starts at SEGA. Both cards are cross-faded against
    // black because the pack has no animation clips for them (TitleController
    // only exposes the two display times).
    if (raw < kSplashDur) {
        dl->AddRectFilled(c.p(0.0f, 0.0f), c.p(kCanvasW, kCanvasH), IM_COL32(0, 0, 0, 255));
        const bool sega = raw < kSegaDur;
        const float local = sega ? raw : raw - kSegaDur;
        const float dur = sega ? kSegaDur : kCpDur;
        const float fadeIn = std::min(1.0f, local / 0.15f);
        const float fadeOut = std::min(1.0f, std::max(0.0f, (dur - local) / 0.15f));
        const float alpha = std::min(fadeIn, fadeOut);
        const float w = sega ? 320.0f : 440.0f;
        drawArt(dl, sega ? mSega : mCp, sega ? mSegaW : mCpW, sega ? mSegaH : mCpH, w, c,
            kCanvasW * 0.5f, kCanvasH * 0.5f, tint(alpha));
        return;
    }

    const float t = raw - kSplashDur;

    // Backdrop first: the song select's own wallpaper, cover-fit so it never
    // stretches, then a flat wash so white art stays readable on a bright photo.
    if (backdropTex != 0 && backdropW > 0 && backdropH > 0) {
        const float winW = kCanvasW;
        const float winH = kCanvasH;
        const float srcAspect = static_cast<float>(backdropW) / static_cast<float>(backdropH);
        const float dstAspect = winW / winH;
        float uw = 1.0f;
        float uh = 1.0f;
        if (srcAspect > dstAspect) {
            uw = dstAspect / srcAspect; // crop left/right
        } else {
            uh = srcAspect / dstAspect; // crop top/bottom
        }
        const float u0 = (1.0f - uw) * 0.5f;
        const float v0 = (1.0f - uh) * 0.5f;
        dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(backdropTex)),
            c.p(0.0f, 0.0f), c.p(winW, winH), ImVec2(u0, v0), ImVec2(u0 + uw, v0 + uh),
            IM_COL32(255, 255, 255, 255));
        dl->AddRectFilled(c.p(0.0f, 0.0f), c.p(winW, winH),
            IM_COL32(0, 0, 0, static_cast<int>(std::clamp(backdropDim, 0.0f, 1.0f) * 255.0f)));
    }

    const float pulse =
        0.5f + 0.5f * std::sin(t * 6.2831853f / kPulsePeriod);
    const float breath = 1.0f + kBreathAmount * std::sin(t * 6.2831853f / kBreathPeriod);

    // Soft glow behind the prompt: wide and very faint, so it reads as light on
    // the backdrop rather than a disc.
    drawArt(dl, mSoft, mSoftW, mSoftH, kSoftWidth, c, kCanvasW * 0.5f, kTapCenterY, tint(0.16f));

    drawArt(dl, mLogo, mLogoW, mLogoH, kLogoWidth * breath, c, kCanvasW * 0.5f, kLogoCenterY,
        tint(1.0f));

    drawArt(dl, mTap, mTapW, mTapH, kTapWidth, c, kCanvasW * 0.5f, kTapCenterY,
        tint(kPulseMin + (1.0f - kPulseMin) * pulse));

    // Fine print, right-aligned: JASRAC notice to the left of the copyright line.
    const float copyW = kCopyWidth;
    drawArtRight(dl, mCopy, mCopyW, mCopyH, copyW, c, kFinePrintRight, kFinePrintY, tint(0.72f));
    drawArtRight(dl, mJasrac, mJasracW, mJasracH, kJasracWidth, c,
        kFinePrintRight - copyW - kFinePrintGap, kFinePrintY, tint(0.72f));
}

} // namespace game
