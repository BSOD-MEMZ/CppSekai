#include "DecideBurst.hpp"

#include "platform/Renderer.hpp"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace game
{
namespace
{
// Timeline, seconds. The whole thing is over well before the white loading
// burst (main.cpp's confirmFlash) covers the screen, so the two never fight.
constexpr float kLife = 0.90f;          // burst lifetime
constexpr float kGlowRise = 0.07f;      // glow reaches full brightness here
constexpr float kGlowGone = 0.78f;      // ... and is gone by here
constexpr float kRingDelay = 0.03f;
constexpr float kRingLife = 0.52f;
constexpr float kShardLife = 0.42f;

constexpr int kShardCount = 12;

void blendAdditive(const ImDrawList*, const ImDrawCmd*)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
}

void blendAlpha(const ImDrawList*, const ImDrawCmd*)
{
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

float easeOut(float t)
{
    const float u = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - u) * (1.0f - u);
}

float easeIn(float t)
{
    const float u = std::clamp(t, 0.0f, 1.0f);
    return u * u;
}

float span(float t, float start, float dur)
{
    if (dur <= 0.0f)
    {
        return t >= start ? 1.0f : 0.0f;
    }
    return std::clamp((t - start) / dur, 0.0f, 1.0f);
}

unsigned mix(unsigned x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float rnd(unsigned& s)
{
    s = mix(s + 0x9e3779b9U);
    return static_cast<float>(s & 0xFFFFFFu) / 16777215.0f;
}

ImU32 white(float alpha)
{
    const int a = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return IM_COL32(255, 255, 255, a);
}
} // namespace

bool DecideBurst::load(platform::Renderer& renderer, const std::string& fxDir, std::string& outError)
{
    struct Entry
    {
        const char* file;
        ImTextureID* out;
    };
    // Both textures are already shipped for TapEffect (the game's own tex_tap_flare
    // and its triangle atlas) - the confirm burst reuses them instead of adding
    // two more files to the package.
    const Entry entries[] = {
        {"tap_flare.png", &mGlow},
        {"tap_tri.png", &mTri},
    };
    bool any = false;
    for (const Entry& entry : entries) {
        std::string error;
        const GLuint id = renderer.loadUiTexture(fxDir + "\\" + entry.file, error);
        *entry.out = static_cast<ImTextureID>(static_cast<std::uintptr_t>(id));
        if (id != 0) {
            any = true;
        } else {
            outError = error;
        }
    }
    return any;
}

void DecideBurst::spawn(float cx, float cy, float spreadRadius)
{
    Burst burst;
    burst.cx = cx;
    burst.cy = cy;
    burst.radius = std::max(120.0f, spreadRadius);
    burst.age = 0.0f;
    burst.shards.reserve(kShardCount);

    unsigned seed = 0x5eed1234U ^ static_cast<unsigned>(std::lround(cx * 7.0f + cy * 13.0f));
    seed ^= static_cast<unsigned>(mActive.size()) * 2654435761U;
    for (int i = 0; i < kShardCount; ++i) {
        Shard s;
        // Spread evenly around the circle, then jitter: the original emits from a
        // 40-degree cone at 8/s, which reads as a fan - a full ring reads better
        // at the speed this burst runs at.
        s.angle = (static_cast<float>(i) / kShardCount) * 6.2831853f + (rnd(seed) - 0.5f) * 0.55f;
        s.delay = 0.01f + rnd(seed) * 0.09f;
        // 从按钮边缘起飞：贴着中心的话整段飞行都被自己的光晕盖住，
        // 只有飞出去才看得见（原版的三角也是 emittter 在按钮上）。
        s.startRadius = 46.0f + rnd(seed) * 34.0f;
        s.speed = 190.0f + rnd(seed) * 230.0f;
        s.size = 34.0f + rnd(seed) * 30.0f;
        // startRotation 三轴各 ±2π（原版是 6.283185 / -6.283185 的随机区间）
        s.rot0x = (rnd(seed) * 2.0f - 1.0f) * 3.1415927f;
        s.rot0y = (rnd(seed) * 2.0f - 1.0f) * 3.1415927f;
        s.rot0z = (rnd(seed) * 2.0f - 1.0f) * 3.1415927f;
        // RotationModule：Z 轴 ±0.7854 rad/s 是实测值；X/Y 给一点，让它真的翻
        s.spinZ = (rnd(seed) * 2.0f - 1.0f) * 0.7854f;
        s.spinX = (rnd(seed) * 2.0f - 1.0f) * 1.3f;
        s.spinY = (rnd(seed) * 2.0f - 1.0f) * 1.3f;
        s.cell = i % 4;
        burst.shards.push_back(s);
    }
    mActive.push_back(burst);
}

void DecideBurst::update(float dt)
{
    if (mActive.empty()) {
        return;
    }
    for (Burst& burst : mActive) {
        burst.age += std::max(dt, 0.0f);
    }
    mActive.erase(std::remove_if(mActive.begin(), mActive.end(),
                      [](const Burst& b) { return b.age >= kLife; }),
        mActive.end());
}

void DecideBurst::draw()
{
    if (mActive.empty()) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    dl->AddCallback(blendAdditive, nullptr);
    for (const Burst& burst : mActive) {
        const float t = burst.age;

        // ---- the big glow (fx_live_transition_v2_02 > light) -------------
        // It starts at button size and spreads over the whole handset. Two
        // layers: a wide soft one and a tight bright core, which is what turns
        // a flat radial gradient into something that reads as a flash.
        if (mGlow != 0) {
            const float rise = span(t, 0.0f, kGlowRise);
            const float fall = 1.0f - span(t, kGlowRise, kGlowGone - kGlowRise);
            const float a = rise * fall;
            if (a > 0.002f) {
                const float grow = easeOut(span(t, 0.0f, 0.34f));
                const float wide = burst.radius * (0.55f + 1.25f * grow);
                const float core = burst.radius * (0.10f + 0.26f * grow);
                const ImVec2 u0(0.0f, 0.0f);
                const ImVec2 u1(1.0f, 1.0f);
                const ImVec2 c(burst.cx, burst.cy);
                dl->AddImage(mGlow, ImVec2(c.x - wide, c.y - wide), ImVec2(c.x + wide, c.y + wide),
                    u0, u1, white(a * 0.38f));
                dl->AddImage(mGlow, ImVec2(c.x - core, c.y - core), ImVec2(c.x + core, c.y + core),
                    u0, u1, white(a * 0.50f));
            }
        }

        // ---- the ring that leaves the button (botton_light + line_circle) --
        const float rt = span(t, kRingDelay, kRingLife);
        if (rt > 0.0f && rt < 1.0f) {
            const float r = burst.radius * (0.12f + 0.62f * easeOut(rt));
            const float a = (1.0f - rt) * (1.0f - rt);
            const float thick = 7.0f * (1.0f - rt) + 1.2f;
            dl->AddCircle(ImVec2(burst.cx, burst.cy), r, white(a * 0.85f), 96, thick);
            dl->AddCircle(ImVec2(burst.cx, burst.cy), r * 0.78f, white(a * 0.35f), 96,
                thick * 0.6f);
        }

        // ---- triangles thrown outward (tri_01 / tri_02) ------------------
        if (mTri != 0) {
            for (const Shard& s : burst.shards) {
                const float local = t - s.delay;
                if (local <= 0.0f || local >= kShardLife) {
                    continue;
                }
                const float k = local / kShardLife;
                // startSpeed 25 -> 10 in the original: fast out, then coast.
                const float travelled = s.startRadius
                    + s.speed * (1.0f - std::exp(-3.2f * local)) / 3.2f;
                const float px = burst.cx + std::cos(s.angle) * travelled;
                const float py = burst.cy + std::sin(s.angle) * travelled;
                const float size = s.size * (1.0f - 0.55f * easeIn(k));
                const float alpha = (1.0f - easeIn(k)) * 0.95f;
                // 三轴旋转后正交投影：X 轴转把 quad 压扁，Y 轴转把它翻面，
                // Z 轴转是原地自转。三样叠起来就是"翻滚的玻璃片"。
                const float rx = s.rot0x + s.spinX * local;
                const float ry = s.rot0y + s.spinY * local;
                const float rz = s.rot0z + s.spinZ * local;
                const float cxr = std::cos(rx);
                const float sxr = std::sin(rx);
                const float cyr = std::cos(ry);
                const float syr = std::sin(ry);
                const float czr = std::cos(rz);
                const float szr = std::sin(rz);
                const float half = size * 0.5f;
                ImVec2 pts[4];
                for (int k = 0; k < 4; ++k) {
                    const float lx = (k == 0 || k == 3) ? -half : half;
                    const float ly = (k < 2) ? -half : half;
                    const float y1 = ly * cxr;
                    const float z1 = ly * sxr;
                    const float x2 = lx * cyr + z1 * syr;
                    pts[k] = ImVec2(px + (x2 * czr - y1 * szr), py + (x2 * szr + y1 * czr));
                }
                const float u0 = static_cast<float>(s.cell & 1) * 0.5f;
                const float v0 = static_cast<float>(s.cell >> 1) * 0.5f;
                dl->AddImageQuad(mTri, pts[0], pts[1], pts[2], pts[3],
                    ImVec2(u0, v0), ImVec2(u0 + 0.5f, v0),
                    ImVec2(u0 + 0.5f, v0 + 0.5f), ImVec2(u0, v0 + 0.5f), white(alpha));
            }
        }
    }
    dl->AddCallback(blendAlpha, nullptr);
}

void DecideBurst::clear()
{
    mActive.clear();
}

} // namespace game
