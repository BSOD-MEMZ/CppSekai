// CppSekai - story UI (see STORY-UI.md).
//
// The drawing half of the tutorial / story layer: a pjsk-style dialogue box,
// scene title, choice pills, device cards, picture cards and a spotlight ring.
// No state, no input decisions - game/Story.* owns those and calls in here.
//
// The art is 1920x1080 full-frame PNGs (assets/story/ui), drawn whole at the
// native canvas coordinates measured off the files. Do not slice them: the
// rounded corners and the soft shadow live in the alpha channel and a 9-slice
// would smear both.
#pragma once

#include "imgui.h"

#include <cstddef>
#include <string>
#include <vector>

namespace platform
{
class Renderer;
}

namespace storyui
{
// Virtual 1920x1080 canvas -> window pixels, letterboxed. Same shape as
// game/Result.cpp's Canvas; each screen keeps its own copy, and this one is
// shared by the whole story layer.
struct Canvas
{
    float scale = 1.0f;
    float ox = 0.0f;
    float oy = 0.0f;

    [[nodiscard]] float x(float v) const { return ox + v * scale; }
    [[nodiscard]] float y(float v) const { return oy + v * scale; }
    [[nodiscard]] float s(float v) const { return v * scale; }
    [[nodiscard]] ImVec2 p(float vx, float vy) const { return ImVec2(x(vx), y(vy)); }
};

Canvas makeCanvas(float windowW, float windowH);

// A loaded picture. `w`/`h` are the file's pixels, used for cover-cropping.
struct Image
{
    ImTextureID tex = 0;
    int w = 0;
    int h = 0;
    bool valid() const { return tex != 0 && w > 0 && h > 0; }
};

// Loads the three UI frames next to assets/story/ui. Missing files are not
// fatal: the matching draw call falls back to a plain shape (headless runs and
// a half-copied build still work).
bool loadTextures(platform::Renderer& renderer, const std::string& assetDir, std::string& err);
Image loadImage(platform::Renderer& renderer, const std::string& path);

// ---- text --------------------------------------------------------------
struct Line
{
    std::size_t begin = 0; // byte offset into the UTF-8 text
    std::size_t end = 0;
    float width = 0.0f;
};

struct TextLayout
{
    std::vector<Line> lines;
    // Byte offset of every UTF-8 code point, plus one sentinel at the end.
    // The typewriter reveals by index into this, because cutting a multi-byte
    // character in half is how Chinese text turns into question marks.
    std::vector<std::size_t> codepoints;
};

// Wraps UTF-8 into lines no wider than `width`.
//
// ImGui's own word wrap only breaks at spaces (imgui_draw.cpp calls it
// "simple word-wrapping for English"), so a Chinese paragraph is one giant
// "word" and the wrap lands wherever the width overflows - which puts 。、」
// at the start of the next line. This adds the line-start ban on top. The
// layout is computed once per line of dialogue and then cached by the caller:
// wrapping every frame would also make the typewriter's line count jump.
TextLayout layoutText(ImFont* font, float size, const std::string& utf8, float width);

// ---- frames ------------------------------------------------------------
// Fullscreen 45% black. Drawn before the box.
void dim(ImDrawList* dl, float alpha = 0.45f);

// The dialogue box, name plate and ▼. `revealBytes` clips the body at a byte
// offset; pass layout.codepoints.back() to show everything.
// Which slice of the bottom band to draw. The band is a full-width gradient, so
// clipping it left/right keeps the art intact while leaving the settings card
// (left) or the song-select phone (right) uncovered.
enum class Box
{
    Full,
    Left,
};

// pjsk story dialogue: the translucent band from Dialogue_Background.png, the
// speaker name just above its top rule, and white body text with a dark
// outline (the reference has no white card - see .workbuddy/shots/preview1.png).
void dialogue(const Canvas& c, ImFont* font, const std::string& name, const std::string& body,
    const TextLayout& layout, std::size_t revealBytes, float time, bool showArrow,
    Box box = Box::Full);

// Two-line black-out card ("19:30 / 教室"); `alpha` fades the whole thing.
void sceneTitle(const Canvas& c, ImFont* font, const std::string& text, float alpha);

// The two pjsk choice pills. Returns the clicked index, or -1.
int choices(const Canvas& c, ImFont* font, const std::vector<std::string>& options, float time,
    int focus = -1);

// Image + title + subtitle cards, side by side. Returns clicked index or -1.
// `images` must be the same length as `titles`; an invalid image is drawn as a
// mint placeholder so a missing file still lays out.
int cards(const Canvas& c, ImFont* font, const std::vector<Image>& images,
    const std::vector<std::string>& titles, const std::vector<std::string>& subtitles,
    float time, int focus = -1);

// A picture above the dialogue box (rounded, white rim). `caption` may be empty.
void imageCard(const Canvas& c, ImFont* font, const Image& img, const std::string& caption);

// Spotlight: dims everything except `lo..hi` and rings it. Coordinates are
// window pixels (what ui::anchorRect hands back), not virtual units.
// pjsk's round skip key (white disc + soft shadow + navy ">>"), the same key
// the opening card draws before a live starts. `hover` / `press` are 0..1 eased
// values; `icon` is the ui_skip HUD sprite (0 falls back to a plain disc).
void skipKey(ImDrawList* dl, const Canvas& c, const ImVec2& center, float hover, float press,
    ImTextureID icon);

// Ready to use after loadTextures(): the ui_skip sprite, or 0 when the HUD pack
// is missing (then the disc draws without its glyph).
ImTextureID skipIcon();

struct SpotlightLabel
{
    ImVec2 lo{0.0f, 0.0f};
    ImVec2 hi{0.0f, 0.0f};
    bool shown = false;
};

// Dims everything except `lo..hi`, rings the hole with an expanding ripple and
// (optionally) floats a label above it. Returns where that label landed, so the
// caller can hang a button off it.
SpotlightLabel spotlight(ImDrawList* dl, const ImVec2& lo, const ImVec2& hi, float time,
    const std::string& label, int dimAlpha = 120);

// Small dark pill of text, screen-pixel coordinates, anchored above `lo`.
void tip(ImDrawList* dl, ImFont* font, const ImVec2& center, const std::string& text);
} // namespace storyui
