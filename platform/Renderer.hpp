// CppSekai - native GL renderer
// Consumes packed quads from the chart core (mmw_preview.cpp) and draws them
// with desktop OpenGL 3.3 core. Layout-compatible with the WebGL renderer in
// sekai-mmw-preview-web (AGPL-3.0), so visuals match the web preview.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using GLuint = unsigned int;

namespace platform
{

class Renderer
{
  public:
    Renderer();
    ~Renderer();

    bool init(int width, int height, std::string& outError);
    void resize(int width, int height);

    bool loadAssets(const std::string& assetDir, std::string& outError);

    // HUD sprites (pjsk overlay textures) drawn with ImGui on top of the field.
    struct HudSprite
    {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };

    bool loadHud(const std::string& overlayDir, std::string& outError);
    [[nodiscard]] const HudSprite* hud(const std::string& name) const;

    // Album cover / jacket shown by the opening card and the song list.
    // Pass an empty path (or clearCover) when the chart has no jacket.
    bool loadCover(const std::string& path, std::string& outError);
    void clearCover();
    [[nodiscard]] const HudSprite* cover() const { return mCover.id != 0 ? &mCover : nullptr; }

    // Lane highlight: a soft additive glow drawn on the playfield for the
    // lanes the player is touching / hovering. Width is in lane units
    // (1.0 = one lane), intensity 0..1.
    struct LaneGlow
    {
        float center = 0.0f;
        float halfWidth = 0.5f;
        float intensity = 1.0f;
    };
    void setLaneGlows(const std::vector<LaneGlow>& glows) { mLaneGlows = glows; }
    void setLaneGlowTint(float r, float g, float b) { mLaneGlowTint = {r, g, b}; }

    // The chart core fires its note-hit effect timeline automatically
    // (autoplay-style). Player mode replaces it with judgement-driven
    // effects, so core effect quads (textureId >= 3) are opt-in.
    void setDrawCoreEffects(bool enabled) { mDrawCoreEffects = enabled; }

    // Projects a playfield world position to window pixel coordinates
    // (y down). Used to place judgement effects on the judge line.
    void worldToScreen(float worldX, float worldY, float& outX, float& outY) const;

    // Draws background + stage, then the packed runtime quads produced by
    // the chart core's render(chartTimeSec). playfieldVisibility (0..1) is
    // the opening-card fade: it scales the stage and note alpha, matching
    // GlRenderer::renderFrame() upstream.
    void renderFrame(const float* packedQuads, int quadCount, float backgroundBrightness,
        float playfieldVisibility = 1.0f);

    // World <-> clip conversions for input hit-testing.
    // Lane coordinates: world x in "lane units", note centers come from
    // getNoteCenter() which is (lane - 6) + width / 2.
    float clipToWorldX(float clipX) const;
    float clipToWorldY(float clipY) const;

    int width() const { return mWidth; }
    int height() const { return mHeight; }

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

  private:
    struct Texture
    {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };

    bool createPrograms(std::string& outError);
    Texture loadTextureFromFile(const std::string& path, std::string& outError);
    void drawVertices(const Texture& texture, const std::vector<float>& vertices, bool effectPass, int blendMode);
    void buildStaticVertices();
    void drawStaticScene(float backgroundBrightness, float playfieldVisibility);
    void drawLaneGlows();

    std::array<float, 2> worldToClip(float worldX, float worldY) const;

    int mWidth = 1;
    int mHeight = 1;

    GLuint mProgram = 0;
    GLuint mEffectProgram = 0;
    GLuint mVao = 0;
    GLuint mVbo = 0;

    Texture mBackground;
    Texture mStage;
    Texture mNotes;
    Texture mLongNoteLine;
    Texture mTouchLine;
    Texture mEffect;
    Texture mWhite; // 1x1 white, used for the lane highlight gradient
    HudSprite mCover;

    std::vector<LaneGlow> mLaneGlows;
    std::array<float, 3> mLaneGlowTint{0.55f, 0.85f, 1.0f};

    std::vector<float> mStaticBackgroundVertices;
    std::vector<float> mStaticStageVertices;

    std::map<std::string, HudSprite> mHudSprites;
    bool mDrawCoreEffects = false;
};

} // namespace platform
