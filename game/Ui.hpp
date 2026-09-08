// CppSekai - pjsk style UI component library.
// Reusable building blocks for in-game dialogs and panels:
//   beginCard / endCard     - rounded light card + dark backdrop + close X
//   capsuleButton           - pjsk capsule (pill) button, white or mint
//   caption                 - centered gray title text
//   messageDialog           - one-shot dialog: title + capsule row + X
// Everything is immediate-mode on top of ImGui and safe to call every frame;
// functions return what was pressed this frame (-1 = nothing).
#pragma once

#include "imgui.h"

#include <string>
#include <vector>

namespace platform
{
class Renderer;
}

namespace ui
{
// pjsk palette.
constexpr ImU32 kBackdrop = IM_COL32(10, 10, 20, 130);      // fullscreen dim
constexpr ImU32 kCardBg = IM_COL32(242, 242, 247, 252);     // light gray card
constexpr ImU32 kTitleText = IM_COL32(96, 96, 112, 255);    // gray title
constexpr ImU32 kBodyText = IM_COL32(70, 70, 88, 255);      // dark body text
constexpr ImU32 kBtnText = IM_COL32(58, 58, 78, 255);       // capsule label
constexpr ImU32 kPrimary = IM_COL32(106, 232, 208, 255);    // mint capsule
constexpr ImU32 kPrimaryHover = IM_COL32(128, 240, 219, 255);
constexpr ImU32 kPrimaryPress = IM_COL32(92, 214, 192, 255);
constexpr ImU32 kWhiteBtn = IM_COL32(255, 255, 255, 255);   // white capsule
constexpr ImU32 kWhiteHover = IM_COL32(243, 243, 249, 255);
constexpr ImU32 kWhitePress = IM_COL32(232, 232, 240, 255);
constexpr ImU32 kDivider = IM_COL32(206, 206, 218, 255);    // thin rule under titles
constexpr ImU32 kNotePink = IM_COL32(255, 82, 141, 255);    // pink hint / warning text
constexpr ImU32 kCheckPink = IM_COL32(255, 102, 158, 255);  // pink checkbox fill
constexpr ImU32 kPillBg = IM_COL32(199, 199, 212, 255);     // gray value pill (stepper)

// UI scale factor relative to the 720p design resolution.
float scale();

// Must be called once after the renderer loads its HUD sprites: registers
// assets/mmw/ui/close.png (the dark dialog X).
void setCloseTexture(ImTextureID texture);
ImTextureID& closeTexture();

// Card scaffold. Draws a fullscreen dim + rounded card centered at `center`.
// With showClose, an X button sits in the top-right corner; the caller learns
// about presses through *closeClicked. Call endCard() after adding content.
bool beginCard(const char* id, const ImVec2& center, const ImVec2& size, bool showClose, bool dimBackdrop,
    bool* closeClicked = nullptr);
void endCard();

// Pill button drawn at the current cursor position. primary = mint.
bool capsuleButton(const char* label, const ImVec2& size, bool primary);

// Centered one-line text advanced by one row. rowWidth <= 0 uses the
// remaining width at the cursor (pass the card interior width to center
// inside a card).
void caption(const char* text, float sizePx = 0.0f, ImU32 color = kTitleText, float rowWidth = 0.0f);

// Left-aligned card title with the thin divider rule underneath (the classic
// pjsk dialog header). interiorWidth is the usable card width for centering.
void cardTitle(const char* text, float interiorWidth, float sizePx = 34.0f);

// Pink rounded checkbox with a white check + label, the whole group centered
// in rowWidth. Toggles *value on click; returns the new value.
bool checkBox(const char* label, bool* value, float rowWidth = 0.0f);

// pjsk number stepper: a row of small white capsules with the +/- deltas
// around a gray pill showing the current value, all centered in rowWidth.
// Applies the pressed delta to *value and returns true when it changed.
bool stepper(const char* id, float* value, const std::vector<float>& deltas,
    const char* fmt = "%.2f", float rowWidth = 0.0f);

// Complete dialog: centered card, close X, centered gray title, and a row of
// capsule buttons (primary flags select the mint ones). Returns the pressed
// button index, -2 if the X was pressed, -1 when nothing was pressed.
int messageDialog(platform::Renderer& renderer, const char* id, const char* title,
    const std::vector<std::string>& buttons, const std::vector<bool>& primary);

} // namespace ui
