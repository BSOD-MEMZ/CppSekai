// CppSekai - pjsk style UI component library.
// Reusable building blocks for in-game dialogs and panels:
//   beginCard / endCard     - rounded light card + dark backdrop + close X,
//                             scale-in/out animation, draggable header
//   tabBar                  - rounded-top tabs (active = card color)
//   slider                  - pjsk slider: dark -/+ buttons + teal track
//   infoRows                - gray box of label | pink value rows
//   capsuleButton           - pjsk capsule (pill) button, white or mint
//   cardTitle / caption     - left title with rule / centered text
//   checkBox                - pink rounded checkbox
//   stepper                 - -1/-0.1/-0.01 value +0.01/+0.1/+1 capsule row
//   messageDialog           - one-shot dialog with in/out animation
// Everything is immediate-mode on top of ImGui and safe to call every frame.
#pragma once

#include "imgui.h"

#include <string>
#include <utility>
#include <vector>

namespace platform
{
class Renderer;
}

namespace ui
{
// pjsk palette.
// Fullscreen dim behind dialogs. Kept light (~33%) on purpose: the stage and
// the notes stay readable while paused.
constexpr ImU32 kBackdrop = IM_COL32(8, 8, 16, 84);
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
constexpr ImU32 kNotePink = IM_COL32(255, 82, 141, 255);    // pink hint / value text
constexpr ImU32 kCheckPink = IM_COL32(255, 102, 158, 255);  // pink checkbox fill
constexpr ImU32 kPillBg = IM_COL32(199, 199, 212, 255);     // gray value pill (stepper)
constexpr ImU32 kTabIdle = IM_COL32(203, 204, 222, 255);    // inactive tab fill
constexpr ImU32 kDarkBtn = IM_COL32(96, 96, 110, 255);      // dark -/+ slider buttons
constexpr ImU32 kRowsBg = IM_COL32(222, 222, 232, 255);     // infoRows box

// UI scale factor relative to the 720p design resolution.
float scale();

// Must be called once after the renderer loads its HUD sprites: registers
// assets/mmw/ui/close.png (the dark dialog X).
void setCloseTexture(ImTextureID texture);
ImTextureID& closeTexture();

// Card scaffold: fullscreen dim + rounded card. The card scales in when it
// appears, scales out when `open` turns false, and can be dragged by its
// header strip. On return `center`/`size` hold the *animated* geometry -
// lay the content out relative to them.
//
// Returns true while drawing: add the content, then call endCard().
// Returns false once the close animation finished (the internal window is
// already ended - do NOT call endCard, and stop calling until reopened).
// *closeClicked is set on the frame the X is pressed (react by setting
// open=false).
bool beginCard(const char* id, ImVec2* center, ImVec2* size, bool showClose, bool dimBackdrop,
    bool* closeClicked, bool open = true);
void endCard();

// Rounded-top tab row. The active tab is card-colored and taller, inactive
// ones lavender. Clicking updates *active. Returns *active.
int tabBar(const char* id, const std::vector<std::string>& tabs, int* active, float rowWidth);

// pjsk slider: dark rounded -/+ buttons flanking a teal track with a white
// round thumb; the value is drawn above the track in pink. `step` is applied
// per button click, dragging is free. Returns true when *value changed.
bool slider(const char* id, float* value, float minV, float maxV, float step, const char* fmt,
    float width);

// Gray rounded box of "label | value" rows, the value in pink, each row with
// its own thin vertical divider. rowWidth <= 0 uses the remaining width.
void infoRows(const std::vector<std::pair<std::string, std::string>>& rows, float rowWidth = 0.0f);

// Pill button drawn at the current cursor position. primary = mint.
bool capsuleButton(const char* label, const ImVec2& size, bool primary);

// Centered one-line text advanced by one row. rowWidth <= 0 uses the
// remaining width at the cursor (pass the card interior width to center
// inside a card).
void caption(const char* text, float sizePx = 0.0f, ImU32 color = kTitleText, float rowWidth = 0.0f);

// Left-aligned card title with the thin divider rule underneath (the classic
// pjsk dialog header). interiorWidth is the usable card width for centering.
void cardTitle(const char* text, float interiorWidth, float sizePx = 24.0f);

// Pink rounded checkbox with a white check + label, the whole group centered
// in rowWidth. Toggles *value on click; returns the new value.
bool checkBox(const char* label, bool* value, float rowWidth = 0.0f);

// pjsk number stepper: a row of small white capsules with the +/- deltas
// around a gray pill showing the current value, all centered in rowWidth.
// Applies the pressed delta to *value and returns true when it changed.
bool stepper(const char* id, float* value, const std::vector<float>& deltas,
    const char* fmt = "%.2f", float rowWidth = 0.0f);

// Themed combo box: ImGui's popup plus an eased fade-in and a chevron that
// rotates while the list is open. `index` is read and written; returns true
// when the selection changed. `scaleHint` > 0 replaces the module scale, for
// callers that lay out in their own px-per-unit space (the song-select screen
// uses its own `k`).
bool combo(const char* id, const char* preview, const std::vector<std::string>& items, int* index,
    float width, ImGuiComboFlags flags = ImGuiComboFlags_HeightSmall, float scaleHint = 0.0f);

// Eased 0..1 value for a caller-owned id: approaches `target` with a step taken
// from DeltaTime (frame-rate independent) and never overshoots - the same easing
// the built-in components use. For screens that draw their own widgets (song list
// rows, the section index) and want their hover / selection blends to match the
// rest of the UI. The id space is private to this module, so plain constants such
// as `0x4a552000u + index` work fine as keys.
float anim(ImGuiID id, bool target, float rate = 18.0f);

// Linear RGBA blend of two IM_COL32 colours, t clamped to 0..1.
ImU32 mix(ImU32 from, ImU32 to, float t);

// Complete dialog: centered card, close X, left title + rule, and a row of
// capsule buttons (primary flags select the mint ones). Animates in/out.
// Returns:
//   >= 0  pressed button index (the dialog starts closing; keep calling)
//   -2    close animation just finished - stop calling (X = dismiss)
//   -3    close animation still running - keep calling
//   -1    nothing new
int messageDialog(platform::Renderer& renderer, const char* id, const char* title,
    const std::vector<std::string>& buttons, const std::vector<bool>& primary);

} // namespace ui
