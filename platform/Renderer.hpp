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

    // The chart core fires its note-hit effect timeline automatically
    // (autoplay-style). Player mode replaces it with judgement-driven
    // effects, so core effect quads (textureId >= 3) are opt-in.
    void setDrawCoreEffects(bool enabled) { mDrawCoreEffects = enabled; }

    // Projects a playfield world position to window pixel coordinates
    // (y down). Used to place judgement effects on the judge line.
    void worldToScreen(float worldX, float worldY, float& outX, float& outY) const;

    // Draws background + stage, then the packed runtime quads produced by
    // the chart core's render(chartTimeSec).
    void renderFrame(const float* packedQuads, int quadCount, float backgroundBrightness);

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
    void drawStaticScene(float backgroundBrightness);

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

    std::vector<float> mStaticBackgroundVertices;
    std::vector<float> mStaticStageVertices;

    std::map<std::string, HudSprite> mHudSprites;
    bool mDrawCoreEffects = false;
};

} // namespace platform
