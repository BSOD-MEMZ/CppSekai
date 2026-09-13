#include "Renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <GL/gl.h>
#include <SDL.h>

// Minimal OpenGL 3.3 core loader: functions beyond GL 1.1 are runtime-loaded
// via SDL_GL_GetProcAddress. glLoadAll() must be called once after the GL
// context is current.
namespace gl
{
// GL 1.2+ constants missing from MinGW's gl.h.
constexpr unsigned int GL_COMPILE_STATUS = 0x8B81;
constexpr unsigned int GL_LINK_STATUS = 0x8B82;
constexpr unsigned int GL_VERTEX_SHADER = 0x8B31;
constexpr unsigned int GL_FRAGMENT_SHADER = 0x8B30;
constexpr unsigned int GL_ARRAY_BUFFER = 0x8892;
constexpr unsigned int GL_DYNAMIC_DRAW = 0x88E8;
constexpr unsigned int GL_CLAMP_TO_EDGE = 0x812F;
constexpr unsigned int GL_TEXTURE0 = 0x84C0;
using GLsizeiptr = ptrdiff_t;

// MinGW's gl.h (GL 1.1) lacks the GL 3.x proc typedefs; declare the ones we use.
typedef unsigned int (*PFNGLCREATESHADERPROC)(unsigned int);
typedef void (*PFNGLSHADERSOURCEPROC)(unsigned int, int, const char**, const int*);
typedef void (*PFNGLCOMPILESHADERPROC)(unsigned int);
typedef void (*PFNGLGETSHADERIVPROC)(unsigned int, unsigned int, int*);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(unsigned int, int, int*, char*);
typedef void (*PFNGLDELETESHADERPROC)(unsigned int);
typedef unsigned int (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(unsigned int, unsigned int);
typedef void (*PFNGLLINKPROGRAMPROC)(unsigned int);
typedef void (*PFNGLGETPROGRAMIVPROC)(unsigned int, unsigned int, int*);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC)(unsigned int, int, int*, char*);
typedef void (*PFNGLDELETEPROGRAMPROC)(unsigned int);
typedef int (*PFNGLGETUNIFORMLOCATIONPROC)(unsigned int, const char*);
typedef void (*PFNGLUNIFORM1IPROC)(int, int);
typedef void (*PFNGLGENVERTEXARRAYSPROC)(int, unsigned int*);
typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(int, const unsigned int*);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(unsigned int);
typedef void (*PFNGLGENBUFFERSPROC)(int, unsigned int*);
typedef void (*PFNGLDELETEBUFFERSPROC)(int, const unsigned int*);
typedef void (*PFNGLBINDBUFFERPROC)(unsigned int, unsigned int);
typedef void (*PFNGLBUFFERDATAPROC)(unsigned int, ptrdiff_t, const void*, unsigned int);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(unsigned int);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(unsigned int, int, unsigned int, unsigned char, int, const void*);
typedef void (*PFNGLBLENDFUNCSEPARATEPROC)(unsigned int, unsigned int, unsigned int, unsigned int);
typedef void (*PFNGLACTIVETEXTUREPROC)(unsigned int);
typedef void (*PFNGLUSEPROGRAMPROC)(unsigned int);

#define GL_PROC_LIST(X) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)

#define GL_PROC_TYPEDEF(type, name) extern type name;
#define GL_PROC_DEF(type, name) type name = nullptr;
#define GL_PROC_LOAD(type, name) name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));
GL_PROC_LIST(GL_PROC_TYPEDEF)
GL_PROC_LIST(GL_PROC_DEF)

inline void loadAll()
{
    GL_PROC_LIST(GL_PROC_LOAD)
}
} // namespace gl

using namespace gl;

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/mmw_preview/vendor/stb_image.h"

namespace platform
{

// ---------------------------------------------------------------------------
// Stage geometry constants (mirrored from sekai-mmw-preview-web, AGPL-3.0).
// ---------------------------------------------------------------------------

namespace
{
    constexpr float STAGE_LANE_TOP = 47.0f;
    constexpr float STAGE_LANE_HEIGHT = 850.0f;
    constexpr float STAGE_LANE_WIDTH = 1420.0f;
    constexpr float STAGE_NUM_LANES = 12.0f;
    constexpr float STAGE_TEX_WIDTH = 2048.0f;
    constexpr float STAGE_TEX_HEIGHT = 1176.0f;
    constexpr float STAGE_TARGET_WIDTH = 1920.0f;
    constexpr float STAGE_TARGET_HEIGHT = 1080.0f;
    constexpr float STAGE_ASPECT_RATIO = STAGE_TARGET_WIDTH / STAGE_TARGET_HEIGHT;
    constexpr float STAGE_ZOOM = 927.0f / 800.0f;
    constexpr float STAGE_WIDTH_RATIO = STAGE_ZOOM * STAGE_LANE_WIDTH / (STAGE_TEX_HEIGHT * STAGE_ASPECT_RATIO) / STAGE_NUM_LANES;
    constexpr float STAGE_HEIGHT_RATIO = STAGE_ZOOM * STAGE_LANE_HEIGHT / STAGE_TEX_HEIGHT;
    constexpr float STAGE_TOP_RATIO = 0.5f + STAGE_ZOOM * STAGE_LANE_TOP / STAGE_TEX_HEIGHT;

    constexpr float BACKGROUND_SIZE = 2462.25f;
    constexpr float WORLD_BACKGROUND_WIDTH = BACKGROUND_SIZE / (STAGE_TARGET_WIDTH * STAGE_WIDTH_RATIO);
    constexpr float WORLD_BACKGROUND_HEIGHT = BACKGROUND_SIZE / (STAGE_TARGET_HEIGHT * STAGE_HEIGHT_RATIO);
    constexpr float WORLD_BACKGROUND_LEFT = -WORLD_BACKGROUND_WIDTH / 2.0f;
    constexpr float WORLD_BACKGROUND_TOP =
        0.5f / STAGE_HEIGHT_RATIO + STAGE_LANE_TOP / STAGE_LANE_HEIGHT - WORLD_BACKGROUND_HEIGHT / 2.0f;

    constexpr float WORLD_STAGE_WIDTH = (STAGE_TEX_WIDTH / STAGE_LANE_WIDTH) * STAGE_NUM_LANES;
    constexpr float WORLD_STAGE_LEFT = -WORLD_STAGE_WIDTH / 2.0f;
    constexpr float WORLD_STAGE_TOP = STAGE_LANE_TOP / STAGE_LANE_HEIGHT;
    constexpr float WORLD_STAGE_HEIGHT = STAGE_TEX_HEIGHT / STAGE_LANE_HEIGHT;

    constexpr int FLOATS_PER_VERTEX = 9;

    constexpr int BLEND_NORMAL = 0;
    constexpr int BLEND_ADDITIVE = 1;

    constexpr const char* kVertexShader = R"GLSL(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUv;
layout (location = 2) in vec4 aColor;
layout (location = 3) in float aReciprocalW;
out vec2 vUv;
out vec4 vColor;
void main() {
    vUv = aUv;
    vColor = aColor;
    float w = aReciprocalW != 0.0 ? (1.0 / aReciprocalW) : 1.0;
    gl_Position = vec4(aPos * w, 0.0, w);
}
)GLSL";

    constexpr const char* kFragmentShader = R"GLSL(#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 outColor;
void main() {
    outColor = texture(uTexture, vUv) * vColor;
}
)GLSL";

    // Effect quads arrive premultiplied; the web renderer converts them in
    // the shader before blending.
    constexpr const char* kEffectFragmentShader = R"GLSL(#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 outColor;
void main() {
    vec4 texColor = texture(uTexture, vUv) * vColor;
    float alpha = texColor.a;
    outColor = vec4(texColor.rgb * texColor.aaa, alpha);
}
)GLSL";

    GLuint compileShader(GLenum type, const char* source, std::string& outError)
    {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint status = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status == GL_FALSE) {
            char log[1024];
            GLsizei length = 0;
            glGetShaderInfoLog(shader, sizeof(log), &length, log);
            outError = std::string("shader compile failed: ") + log;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint linkProgram(const char* vsSource, const char* fsSource, std::string& outError)
    {
        const GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource, outError);
        const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource, outError);
        if (vs == 0 || fs == 0) {
            return 0;
        }
        const GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint status = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &status);
        if (status == GL_FALSE) {
            char log[1024];
            GLsizei length = 0;
            glGetProgramInfoLog(program, sizeof(log), &length, log);
            outError = std::string("program link failed: ") + log;
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }
} // namespace

// ---------------------------------------------------------------------------

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    const auto deleteTexture = [](Texture& texture) {
        if (texture.id != 0) {
            glDeleteTextures(1, &texture.id);
            texture.id = 0;
        }
    };
    deleteTexture(mBackground);
    deleteTexture(mStage);
    deleteTexture(mNotes);
    deleteTexture(mLongNoteLine);
    deleteTexture(mTouchLine);
    deleteTexture(mEffect);
    deleteTexture(mWhite);
    if (mCover.id != 0) {
        glDeleteTextures(1, &mCover.id);
        mCover.id = 0;
    }
    if (mProgram != 0) {
        glDeleteProgram(mProgram);
    }
    if (mEffectProgram != 0) {
        glDeleteProgram(mEffectProgram);
    }
    if (mVbo != 0) {
        glDeleteBuffers(1, &mVbo);
    }
    if (mVao != 0) {
        glDeleteVertexArrays(1, &mVao);
    }
}

bool Renderer::init(int width, int height, std::string& outError)
{
    gl::loadAll();

    mWidth = std::max(1, width);
    mHeight = std::max(1, height);

    if (!createPrograms(outError)) {
        return false;
    }

    glGenVertexArrays(1, &mVao);
    glGenBuffers(1, &mVbo);
    glBindVertexArray(mVao);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * sizeof(float), reinterpret_cast<void*>(4 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * sizeof(float), reinterpret_cast<void*>(8 * sizeof(float)));

    glBindVertexArray(0);

    // 1x1 white pixel: the lane highlight is a coloured, alpha-graded quad.
    {
        const unsigned char whitePixel[4] = {255, 255, 255, 255};
        glGenTextures(1, &mWhite.id);
        glBindTexture(GL_TEXTURE_2D, mWhite.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        mWhite.width = 1;
        mWhite.height = 1;
    }

    glUseProgram(mProgram);
    glUniform1i(glGetUniformLocation(mProgram, "uTexture"), 0);
    glUseProgram(mEffectProgram);
    glUniform1i(glGetUniformLocation(mEffectProgram, "uTexture"), 0);
    glUseProgram(0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);

    return true;
}

void Renderer::resize(int width, int height)
{
    mWidth = std::max(1, width);
    mHeight = std::max(1, height);
    buildStaticVertices();
}

bool Renderer::createPrograms(std::string& outError)
{
    mProgram = linkProgram(kVertexShader, kFragmentShader, outError);
    if (mProgram == 0) {
        return false;
    }
    mEffectProgram = linkProgram(kVertexShader, kEffectFragmentShader, outError);
    return mEffectProgram != 0;
}

Renderer::Texture Renderer::loadTextureFromFile(const std::string& path, std::string& outError)
{
    Texture texture;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &texture.width, &texture.height, &channels, 4);
    if (pixels == nullptr) {
        outError = "failed to load texture: " + path;
        return texture;
    }

    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);
    return texture;
}

namespace
{
    // One sliding-window box pass (O(1) per pixel regardless of radius) with
    // clamped edges. Three of these in each direction approximate a Gaussian
    // closely enough for a background wash.
    void boxBlurH(const std::vector<unsigned char>& src, std::vector<unsigned char>& dst, int w, int h,
        int radius)
    {
        const int span = radius * 2 + 1;
        for (int y = 0; y < h; ++y) {
            const unsigned char* srow = src.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
            unsigned char* drow = dst.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
            int acc[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const int x = std::clamp(i, 0, w - 1);
                for (int c = 0; c < 4; ++c) {
                    acc[c] += srow[x * 4 + c];
                }
            }
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 4; ++c) {
                    drow[x * 4 + c] = static_cast<unsigned char>(acc[c] / span);
                }
                const int add = std::clamp(x + radius + 1, 0, w - 1);
                const int sub = std::clamp(x - radius, 0, w - 1);
                for (int c = 0; c < 4; ++c) {
                    acc[c] += srow[add * 4 + c] - srow[sub * 4 + c];
                }
            }
        }
    }

    void boxBlurV(const std::vector<unsigned char>& src, std::vector<unsigned char>& dst, int w, int h,
        int radius)
    {
        const int span = radius * 2 + 1;
        const size_t stride = static_cast<size_t>(w) * 4;
        for (int x = 0; x < w; ++x) {
            const unsigned char* scol = src.data() + static_cast<size_t>(x) * 4;
            unsigned char* dcol = dst.data() + static_cast<size_t>(x) * 4;
            int acc[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const int y = std::clamp(i, 0, h - 1);
                for (int c = 0; c < 4; ++c) {
                    acc[c] += scol[static_cast<size_t>(y) * stride + c];
                }
            }
            for (int y = 0; y < h; ++y) {
                for (int c = 0; c < 4; ++c) {
                    dcol[static_cast<size_t>(y) * stride + c] = static_cast<unsigned char>(acc[c] / span);
                }
                const int add = std::clamp(y + radius + 1, 0, h - 1);
                const int sub = std::clamp(y - radius, 0, h - 1);
                for (int c = 0; c < 4; ++c) {
                    acc[c] += scol[static_cast<size_t>(add) * stride + c]
                        - scol[static_cast<size_t>(sub) * stride + c];
                }
            }
        }
    }
} // namespace

GLuint Renderer::loadBackdropTexture(const std::string& path, float blur01, int& outW, int& outH,
    std::string& outError)
{
    outW = 0;
    outH = 0;
    int srcW = 0;
    int srcH = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &srcW, &srcH, &channels, 4);
    if (pixels == nullptr || srcW <= 0 || srcH <= 0) {
        outError = "failed to load background image: " + path;
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return 0;
    }

    // Downscale first (box filter). A desktop wallpaper is easily 4K, the blur
    // is the expensive part and it destroys the detail anyway, so the result
    // is blurred from a much smaller source.
    constexpr int kMaxSide = 1024;
    int dstW = srcW;
    int dstH = srcH;
    if (std::max(srcW, srcH) > kMaxSide) {
        const float scale = static_cast<float>(kMaxSide) / static_cast<float>(std::max(srcW, srcH));
        dstW = std::max(1, static_cast<int>(static_cast<float>(srcW) * scale));
        dstH = std::max(1, static_cast<int>(static_cast<float>(srcH) * scale));
    }
    std::vector<unsigned char> image(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
    for (int y = 0; y < dstH; ++y) {
        const int sy0 = y * srcH / dstH;
        const int sy1 = std::max(sy0 + 1, (y + 1) * srcH / dstH);
        for (int x = 0; x < dstW; ++x) {
            const int sx0 = x * srcW / dstW;
            const int sx1 = std::max(sx0 + 1, (x + 1) * srcW / dstW);
            int acc[4] = {0, 0, 0, 0};
            int count = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const unsigned char* row = pixels + (static_cast<size_t>(sy) * static_cast<size_t>(srcW) + sx0) * 4;
                for (int sx = sx0; sx < sx1; ++sx, row += 4) {
                    acc[0] += row[0];
                    acc[1] += row[1];
                    acc[2] += row[2];
                    acc[3] += row[3];
                    ++count;
                }
            }
            unsigned char* dst = image.data() + (static_cast<size_t>(y) * static_cast<size_t>(dstW) + x) * 4;
            for (int c = 0; c < 4; ++c) {
                dst[c] = static_cast<unsigned char>(acc[c] / std::max(1, count));
            }
        }
    }
    stbi_image_free(pixels);

    const int radius = static_cast<int>(std::clamp(blur01, 0.0f, 1.0f) * 26.0f);
    if (radius > 0) {
        std::vector<unsigned char> scratch(image.size());
        for (int pass = 0; pass < 3; ++pass) {
            boxBlurH(image, scratch, dstW, dstH, radius);
            boxBlurV(scratch, image, dstW, dstH, radius);
        }
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dstW, dstH, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    outW = dstW;
    outH = dstH;
    return id;
}

bool Renderer::loadSplash(const std::string& assetDir, std::string& outError){
    // Only what drawStaticScene() needs for the very first frame.
    if (mBackground.id == 0) {
        mBackground = loadTextureFromFile(assetDir + "/background_overlay.png", outError);
        if (mBackground.id == 0) {
            return false;
        }
    }
    if (mStage.id == 0) {
        mStage = loadTextureFromFile(assetDir + "/stage.png", outError);
        if (mStage.id == 0) {
            return false;
        }
    }
    buildStaticVertices();
    outError.clear();
    return true;
}

bool Renderer::loadAssets(const std::string& assetDir, std::string& outError)
{
    if (mBackground.id == 0) {
        mBackground = loadTextureFromFile(assetDir + "/background_overlay.png", outError);
        if (mBackground.id == 0) {
            return false;
        }
    }
    if (mStage.id == 0) {
        mStage = loadTextureFromFile(assetDir + "/stage.png", outError);
        if (mStage.id == 0) {
            return false;
        }
    }
    mNotes = loadTextureFromFile(assetDir + "/notes_01.png", outError);
    if (mNotes.id == 0) {
        return false;
    }
    mLongNoteLine = loadTextureFromFile(assetDir + "/longNoteLine_01.png", outError);
    if (mLongNoteLine.id == 0) {
        return false;
    }
    mTouchLine = loadTextureFromFile(assetDir + "/touchLine_eff_01.png", outError);
    if (mTouchLine.id == 0) {
        return false;
    }
    mEffect = loadTextureFromFile(assetDir + "/effect.png", outError);
    if (mEffect.id == 0) {
        return false;
    }

    buildStaticVertices();
    return true;
}

void Renderer::worldToScreen(float worldX, float worldY, float& outX, float& outY) const
{
    const std::array<float, 2> clip = worldToClip(worldX, worldY);
    outX = (clip[0] * 0.5f + 0.5f) * static_cast<float>(mWidth);
    outY = (1.0f - (clip[1] * 0.5f + 0.5f)) * static_cast<float>(mHeight);
}

const Renderer::HudSprite* Renderer::hud(const std::string& name) const
{
    const auto it = mHudSprites.find(name);
    return it == mHudSprites.end() ? nullptr : &it->second;
}

bool Renderer::loadHud(const std::string& overlayDir, std::string& outError)
{
    // Prefer the pre-shrunk copies (generated offline by
    // .workbuddy/tools/shrink_hud.cpp into "<overlay>_opt") when they exist;
    // fall back to the originals so a missing _opt folder still works.
    const std::string optDir = overlayDir + "_opt";
    auto fileExists = [](const std::string& path) {
        std::FILE* probe = std::fopen(path.c_str(), "rb");
        if (probe == nullptr) {
            return false;
        }
        std::fclose(probe);
        return true;
    };
    auto add = [&](const std::string& key, const std::string& relativePath) {
        std::string path = optDir + "/" + relativePath;
        if (!fileExists(path)) {
            path = overlayDir + "/" + relativePath;
        }
        const auto t0 = std::chrono::steady_clock::now();
        const Texture texture = loadTextureFromFile(path, outError);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        static const bool traceTextures = std::getenv("CPSEKAI_ASSET_TIMING") != nullptr;
        if (traceTextures && ms > 15.0) {
            std::printf("[hud] %-28s %6.1f ms (%dx%d)\n", relativePath.c_str(), ms,
                texture.width, texture.height);
        }
        if (texture.id == 0) {
            return false;
        }
        mHudSprites[key] = HudSprite{texture.id, texture.width, texture.height};
        return true;
    };

    add("score_bg", "score/bg.png");
    add("score_fg", "score/fg.png");
    add("score_bar", "score/bar.png");
    add("combo_tag", "combo/pt.png");
    add("combo_tag_glow", "combo/pe.png");
    add("life_bg", "life/v3/bg.png");
    add("life_fill", "life/v3/normal.png");
    add("life_danger", "life/v3/danger.png");
    add("life_overflow", "life/v3/overflow.png");
    add("ui_close", "../ui/close.png");

    for (int i = 1; i <= 6; ++i) {
        add("judge_" + std::to_string(i), "judge/v3/" + std::to_string(i) + ".png");
    }
    for (const char* rank : {"d", "c", "b", "a", "s"}) {
        add(std::string("rank_char_") + rank, std::string("score/rank/chr/") + rank + ".png");
        add(std::string("rank_txt_") + rank, std::string("score/rank/txt/en/") + rank + ".png");
    }

    // Score digits: "0-9", "n"(?) and plus sign come in normal + shadow sets.
    for (int d = 0; d <= 9; ++d) {
        add("digit_" + std::to_string(d), "score/digit/" + std::to_string(d) + ".png");
        add("digit_s" + std::to_string(d), "score/digit/s" + std::to_string(d) + ".png");
        add("life_digit_" + std::to_string(d), "life/v3/digit/" + std::to_string(d) + ".png");
        add("life_digit_s" + std::to_string(d), "life/v3/digit/s" + std::to_string(d) + ".png");
        add("combo_digit_n_" + std::to_string(d), "combo/p" + std::to_string(d) + ".png");
        add("combo_digit_b_" + std::to_string(d), "combo/b" + std::to_string(d) + ".png");
    }
    add("digit_plus", "score/digit/plus.png");
    add("digit_splus", "score/digit/splus.png");
    // "n" is the empty digit slot: scores print without leading zeros.
    add("digit_n", "score/digit/n.png");
    add("digit_sn", "score/digit/sn.png");

    add("effect_hit", "../effect.png");
    add("start_grad", "start_grad.png");

    outError.clear();
    return !mHudSprites.empty();
}

bool Renderer::loadCover(const std::string& path, std::string& outError)
{
    clearCover();
    if (path.empty()) {
        return false;
    }
    const Texture texture = loadTextureFromFile(path, outError);
    if (texture.id == 0) {
        return false;
    }
    mCover = HudSprite{texture.id, texture.width, texture.height};
    outError.clear();
    return true;
}

GLuint Renderer::loadUiTexture(const std::string& path, std::string& outError)
{
    const Texture texture = loadTextureFromFile(path, outError);
    return texture.id;
}

void Renderer::clearCover()
{
    if (mCover.id != 0) {
        glDeleteTextures(1, &mCover.id);
        mCover.id = 0;
    }
    mCover.width = 0;
    mCover.height = 0;
}

namespace
{
    // The playfield is a fake-perspective space: a lane coordinate x at
    // height y is drawn at world (x * y, y). y = 1 is the judge line,
    // y -> 0 is the vanishing point at the top of the stage.
    constexpr float LANE_GLOW_BOTTOM_Y = 1.06f;
    constexpr float LANE_GLOW_TOP_Y = 0.16f;
} // namespace

void Renderer::drawLaneGlows()
{
    if (mLaneGlows.empty() || mWhite.id == 0) {
        return;
    }

    std::vector<float> vertices;
    vertices.reserve(mLaneGlows.size() * 6 * FLOATS_PER_VERTEX);

    for (const LaneGlow& glow : mLaneGlows) {
        if (glow.intensity <= 0.001f) {
            continue;
        }
        const float left = glow.center - glow.halfWidth;
        const float right = glow.center + glow.halfWidth;
        // bottom -> top, alpha fades out towards the vanishing point
        const std::array<std::array<float, 3>, 4> world{{
            {right * LANE_GLOW_BOTTOM_Y, LANE_GLOW_BOTTOM_Y, 0.85f},
            {right * LANE_GLOW_TOP_Y, LANE_GLOW_TOP_Y, 0.0f},
            {left * LANE_GLOW_TOP_Y, LANE_GLOW_TOP_Y, 0.0f},
            {left * LANE_GLOW_BOTTOM_Y, LANE_GLOW_BOTTOM_Y, 0.85f},
        }};
        std::array<std::array<float, 2>, 4> clip{};
        for (int i = 0; i < 4; ++i) {
            clip[i] = worldToClip(world[i][0], world[i][1]);
        }

        const float peak = std::clamp(glow.intensity, 0.0f, 1.4f) * 0.55f;
        auto push = [&](int index) {
            const float alpha = world[index][2] * peak;
            vertices.push_back(clip[index][0]);
            vertices.push_back(clip[index][1]);
            vertices.push_back(0.5f);
            vertices.push_back(0.5f);
            vertices.push_back(mLaneGlowTint[0]);
            vertices.push_back(mLaneGlowTint[1]);
            vertices.push_back(mLaneGlowTint[2]);
            vertices.push_back(alpha);
            vertices.push_back(1.0f);
        };
        push(0);
        push(1);
        push(2);
        push(0);
        push(2);
        push(3);
    }

    drawVertices(mWhite, vertices, false, BLEND_ADDITIVE);
}

std::array<float, 2> Renderer::worldToClip(float worldX, float worldY) const
{
    const float sourceAspect = static_cast<float>(mWidth) / static_cast<float>(mHeight);
    const float targetAspect = STAGE_TARGET_WIDTH / STAGE_TARGET_HEIGHT;
    float fillWidth = STAGE_TARGET_WIDTH;
    float fillHeight = STAGE_TARGET_HEIGHT;
    if (targetAspect < sourceAspect) {
        fillWidth = sourceAspect * STAGE_TARGET_HEIGHT;
        fillHeight = STAGE_TARGET_HEIGHT;
    } else {
        fillWidth = STAGE_TARGET_WIDTH;
        fillHeight = STAGE_TARGET_WIDTH / sourceAspect;
    }

    const float scaledWidth = STAGE_TARGET_WIDTH * STAGE_WIDTH_RATIO;
    const float scaledHeight = STAGE_TARGET_HEIGHT * STAGE_HEIGHT_RATIO;
    const float screenTop = STAGE_TARGET_HEIGHT * STAGE_TOP_RATIO;
    const float x = (2.0f * worldX * scaledWidth) / fillWidth;
    const float y = (-2.0f * (worldY * scaledHeight - screenTop)) / fillHeight;
    return {x, y};
}

float Renderer::clipToWorldX(float clipX) const
{
    const float sourceAspect = static_cast<float>(mWidth) / static_cast<float>(mHeight);
    const float targetAspect = STAGE_TARGET_WIDTH / STAGE_TARGET_HEIGHT;
    float fillWidth = STAGE_TARGET_WIDTH;
    if (targetAspect < sourceAspect) {
        fillWidth = sourceAspect * STAGE_TARGET_HEIGHT;
    }
    const float scaledWidth = STAGE_TARGET_WIDTH * STAGE_WIDTH_RATIO;
    return clipX * fillWidth / (2.0f * scaledWidth);
}

float Renderer::clipToWorldY(float clipY) const
{
    const float sourceAspect = static_cast<float>(mWidth) / static_cast<float>(mHeight);
    const float targetAspect = STAGE_TARGET_WIDTH / STAGE_TARGET_HEIGHT;
    float fillHeight = STAGE_TARGET_HEIGHT;
    if (targetAspect >= sourceAspect) {
        fillHeight = STAGE_TARGET_WIDTH / sourceAspect;
    }
    const float scaledHeight = STAGE_TARGET_HEIGHT * STAGE_HEIGHT_RATIO;
    const float screenTop = STAGE_TARGET_HEIGHT * STAGE_TOP_RATIO;
    return (screenTop - clipY * fillHeight * 0.5f) / scaledHeight;
}

void Renderer::buildStaticVertices()
{
    mStaticBackgroundVertices.clear();
    mStaticStageVertices.clear();

    auto buildQuad = [&](std::vector<float>& out, const Texture& texture, const std::array<std::array<float, 2>, 4>& worldPoints,
                         const std::array<float, 4>& sprite, const std::array<float, 4>& color) {
        std::array<std::array<float, 2>, 4> clip{};
        for (int i = 0; i < 4; ++i) {
            clip[i] = worldToClip(worldPoints[i][0], worldPoints[i][1]);
        }
        const std::array<std::array<float, 2>, 4> uv{{
            {sprite[2] / texture.width, sprite[1] / texture.height},
            {sprite[2] / texture.width, sprite[3] / texture.height},
            {sprite[0] / texture.width, sprite[3] / texture.height},
            {sprite[0] / texture.width, sprite[1] / texture.height},
        }};
        auto push = [&](int index) {
            out.push_back(clip[index][0]);
            out.push_back(clip[index][1]);
            out.push_back(uv[index][0]);
            out.push_back(uv[index][1]);
            out.push_back(color[0]);
            out.push_back(color[1]);
            out.push_back(color[2]);
            out.push_back(color[3]);
            out.push_back(1.0f);
        };
        push(0);
        push(1);
        push(2);
        push(0);
        push(2);
        push(3);
    };

    const std::array<std::array<float, 2>, 4> backgroundPoints{{
        {WORLD_BACKGROUND_LEFT + WORLD_BACKGROUND_WIDTH, WORLD_BACKGROUND_TOP},
        {WORLD_BACKGROUND_LEFT + WORLD_BACKGROUND_WIDTH, WORLD_BACKGROUND_TOP + WORLD_BACKGROUND_HEIGHT},
        {WORLD_BACKGROUND_LEFT, WORLD_BACKGROUND_TOP + WORLD_BACKGROUND_HEIGHT},
        {WORLD_BACKGROUND_LEFT, WORLD_BACKGROUND_TOP},
    }};
    buildQuad(mStaticBackgroundVertices, mBackground, backgroundPoints,
              {0.0f, 0.0f, static_cast<float>(mBackground.width), static_cast<float>(mBackground.height)},
              {1.0f, 1.0f, 1.0f, 1.0f});

    const std::array<std::array<float, 2>, 4> stagePoints{{
        {WORLD_STAGE_LEFT + WORLD_STAGE_WIDTH, WORLD_STAGE_TOP},
        {WORLD_STAGE_LEFT + WORLD_STAGE_WIDTH, WORLD_STAGE_TOP + WORLD_STAGE_HEIGHT},
        {WORLD_STAGE_LEFT, WORLD_STAGE_TOP + WORLD_STAGE_HEIGHT},
        {WORLD_STAGE_LEFT, WORLD_STAGE_TOP},
    }};
    buildQuad(mStaticStageVertices, mStage, stagePoints, {0.0f, 0.0f, 2048.0f, 1176.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
}

void Renderer::drawStaticScene(float backgroundBrightness, float playfieldVisibility)
{
    const float visibility = std::max(0.0f, std::min(1.0f, playfieldVisibility));
    std::vector<float> background = mStaticBackgroundVertices;
    for (size_t i = 4; i < background.size(); i += FLOATS_PER_VERTEX) {
        background[i + 0] *= backgroundBrightness;
        background[i + 1] *= backgroundBrightness;
        background[i + 2] *= backgroundBrightness;
    }
    drawVertices(mBackground, background, false, BLEND_NORMAL);
    if (visibility <= 0.001f) {
        return;
    }
    std::vector<float> stage = mStaticStageVertices;
    for (size_t i = 7; i < stage.size(); i += FLOATS_PER_VERTEX) {
        stage[i] *= visibility;
    }
    drawVertices(mStage, stage, false, BLEND_NORMAL);
}

void Renderer::drawVertices(const Texture& texture, const std::vector<float>& vertices, bool effectPass, int blendMode)
{
    if (vertices.empty() || texture.id == 0) {
        return;
    }

    if (effectPass) {
        glUseProgram(mEffectProgram);
        if (blendMode == BLEND_ADDITIVE) {
            glBlendFunc(GL_ONE, GL_ONE);
        } else {
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
    } else {
        glUseProgram(mProgram);
        if (blendMode == BLEND_ADDITIVE) {
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
        } else {
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    glBindVertexArray(mVao);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / FLOATS_PER_VERTEX));
    glBindVertexArray(0);
}

void Renderer::renderFrame(const float* packedQuads, int quadCount, float backgroundBrightness,
    float playfieldVisibility)
{
    const float visibility = std::max(0.0f, std::min(1.0f, playfieldVisibility));
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    glClearColor(0.03f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, mWidth, mHeight);

    drawStaticScene(backgroundBrightness, visibility);
    if (visibility <= 0.001f) {
        return;
    }
    drawLaneGlows();

    // Iterate runtime quads and batch consecutive quads sharing the same
    // texture bucket + blend mode, preserving the core's z order.
    std::vector<float> batch;
    int currentBucket = -1;
    int currentBlend = -1;
    auto flush = [&](const Texture& texture, bool effectPass, int blend) {
        if (!batch.empty()) {
            drawVertices(texture, batch, effectPass, blend);
            batch.clear();
        }
        (void)effectPass;
        (void)blend;
    };

    for (int quad = 0; quad < quadCount; ++quad) {
        const int offset = quad * 25;
        const int rawTextureId = static_cast<int>(std::lround(packedQuads[offset + 24]));
        const bool isEffect = rawTextureId >= 3;
        if (isEffect && !mDrawCoreEffects) {
            continue; // autoplay effects are replaced by judgement-driven ones
        }
        const int bucket = rawTextureId <= 2 ? rawTextureId : 3;
        const int blend = rawTextureId == 4 ? BLEND_ADDITIVE : BLEND_NORMAL;

        if (bucket != currentBucket || blend != currentBlend) {
            if (currentBucket >= 0) {
                const Texture& texture = currentBucket == 0 ? mNotes : currentBucket == 1 ? mLongNoteLine
                                                       : currentBucket == 2                ? mTouchLine
                                                                                           : mEffect;
                flush(texture, currentBucket == 3, currentBlend);
            }
            currentBucket = bucket;
            currentBlend = blend;
        }

        const Texture& texture = bucket == 0 ? mNotes : bucket == 1 ? mLongNoteLine
                                                    : bucket == 2   ? mTouchLine
                                                                    : mEffect;
        const float alphaMultiplier = visibility;

        std::array<std::array<float, 2>, 4> clip{};
        std::array<float, 4> reciprocalW{};
        std::array<std::array<float, 2>, 4> uv{};
        for (int i = 0; i < 4; ++i) {
            const float px = packedQuads[offset + i * 3 + 0];
            const float py = packedQuads[offset + i * 3 + 1];
            const float rw = packedQuads[offset + i * 3 + 2];
            reciprocalW[i] = isEffect ? rw : 1.0f;
            if (isEffect) {
                clip[i] = {px, py};
            } else {
                clip[i] = worldToClip(px, py);
            }
            uv[i] = {
                packedQuads[offset + 12 + i * 2 + 0] / static_cast<float>(texture.width),
                packedQuads[offset + 12 + i * 2 + 1] / static_cast<float>(texture.height),
            };
        }

        const float cr = packedQuads[offset + 20];
        const float cg = packedQuads[offset + 21];
        const float cb = packedQuads[offset + 22];
        const float ca = packedQuads[offset + 23] * alphaMultiplier;

        auto push = [&](int index) {
            batch.push_back(clip[index][0]);
            batch.push_back(clip[index][1]);
            batch.push_back(uv[index][0]);
            batch.push_back(uv[index][1]);
            batch.push_back(cr);
            batch.push_back(cg);
            batch.push_back(cb);
            batch.push_back(ca);
            batch.push_back(reciprocalW[index]);
        };
        push(0);
        push(1);
        push(2);
        push(0);
        push(2);
        push(3);
    }

    if (currentBucket >= 0) {
        const Texture& texture = currentBucket == 0 ? mNotes : currentBucket == 1 ? mLongNoteLine
                                                       : currentBucket == 2                ? mTouchLine
                                                                                           : mEffect;
        flush(texture, currentBucket == 3, currentBlend);
    }

    (void)viewport;
}

} // namespace platform
