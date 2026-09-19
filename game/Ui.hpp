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
class AudioEngine;
}

namespace ui
{
// ---- UI sound effects --------------------------------------------------
// Every widget reports what the player did; the requests are resolved once per
// frame by flushSe() (called from the main loop), which plays the single
// highest-priority sound of that frame. That is what keeps a click from
// doubling up with the dialog it opened: window_open.mp3 already contains a
// click, so the click is dropped. Order matters - later = stronger.
enum SeKind
{
    SeClick = 0,   // any UI component press
    SeSelect,      // song list moved one slot / a section jump
    SeLevelChoose, // difficulty button
    SeWindowOpen,  // a card / dialog appeared
    SeWindowClose, // a card / dialog started closing
    SeStart,       // the white burst when 确定 launches a live (start.mp3)
    // Sentinel: the number of real kinds. Keep it last, and let Ui.cpp size its
    // request array from it - a hand-written count there is what silently
    // dropped start.mp3 for a while.
    SeKindCount,
};

// Wires the component library to the audio engine (call once after audio init).
// Nothing is played until this is called, so headless builds stay silent.
void bindSe(platform::AudioEngine* audio, float volume = 0.8f);
// Queues one sound for this frame (see the priority note above).
void se(SeKind kind);
// Plays the winning request and clears the queue. Once per frame, after all UI.
void flushSe();

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
constexpr ImU32 kDisabledFill = IM_COL32(198, 198, 210, 255); // greyed-out control body
constexpr ImU32 kDisabledText = IM_COL32(158, 158, 175, 255); // greyed-out label / value

// UI scale factor relative to the 720p design resolution.
float scale();

// Must be called once after the renderer loads its HUD sprites: registers
// assets/mmw/ui/close.png (the dark dialog X).
void setCloseTexture(ImTextureID texture);
ImTextureID& closeTexture();

// Registers assets/select/level.png - the little note glyph of the player level
// chip. Missing (0) is fine: the chip falls back to a drawn note.
void setLevelIconTexture(ImTextureID texture);

// Player level chip, the one piece of the account that is always on screen.
// Rounded dark pill, [exp fill][note icon][等级][NN]. `expRatio` (0..1) is the
// progress towards the next rank and is what the green block at the pill's left
// end is - an exp bar, not an icon plate (see `level.png` usage: the note sprite
// is drawn straight on top of whatever the fill leaves behind).
// The height is 33 `unit`, i.e. exactly what a ui::combo of the same unit is
// tall (17u glyph + 8u frame padding), so the chip lines up with the song
// select's 排序 / 分组 boxes. `unit` is pixels per 1080p layout unit (the song
// select's `k` / the result screen's scale).
// `anchor` is the chip's top-left, or its top-right when `alignRight` is set.
// Returns the drawn rect (x, y, w, h in screen pixels) for hit tests.
ImVec4 playerLevelChip(ImDrawList* dl, ImFont* font, ImVec2 anchor, int rank, float unit,
    bool alignRight = false, float expRatio = 0.0f);

// Thin "exp towards the next rank" bar: dark groove + teal fill, `ratio` 0..1.
void expBar(ImDrawList* dl, ImVec2 pos, float width, float unit, float ratio);

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
// Child windows (ImGui::BeginChild) get their own ImDrawList, so their vertices
// are NOT part of the card's own list and endCard() would leave them unscaled
// and unfaded - the "the card animates but its contents pop in" bug. If a card
// body lives in a child, hand that child's list over with this right after
// BeginChild() and endCard() moves and fades it with everything else.
// Registration is per frame: beginCard() starts a fresh list.
void cardSubList(ImDrawList* list);
void endCard();

// Rounded-top tab row. The active tab is card-colored and taller, inactive
// ones lavender. Clicking updates *active. Returns *active.
int tabBar(const char* id, const std::vector<std::string>& tabs, int* active, float rowWidth);

// pjsk slider: dark rounded -/+ buttons flanking a teal track with a white
// round thumb; the value is drawn above the track in pink. `step` is applied
// per button click, dragging is free. Returns true when *value changed.
// `enabled = false` greys the whole row out and swallows every click - for a
// value that is currently derived from another setting.
bool slider(const char* id, float* value, float minV, float maxV, float step, const char* fmt,
    float width, bool enabled = true);

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
// `enabled = false` draws it greyed out and swallows the click - for settings
// that only make sense under another one (多人游玩 needs 允许多开).
bool checkBox(const char* label, bool* value, float rowWidth = 0.0f, bool enabled = true);

// pjsk number stepper: a row of small white capsules with the +/- deltas
// around a gray pill showing the current value, all centered in rowWidth.
// Applies the pressed delta to *value and returns true when it changed.
//
// Pick-one mode: pass `presets` (same length as `deltas`) and the value is a
// *selected slot* instead of a number - `deltas[i]` labels capsule i, and
// pressing that capsule selects slot i (the delta signs are ignored). The row
// then reads left to right in `deltas` order and the pill shows `presets[slot]`
// rather than a number. Pass -1 to start with nothing selected; the pill then
// reads "--". Every capsule is sized to fit `rowWidth`, so labels cannot run
// off the card.
bool stepper(const char* id, float* value, const std::vector<float>& deltas,
    const char* fmt = "%.2f", float rowWidth = 0.0f,
    const std::vector<std::string>& presets = {});

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
// `forcedChoice` (>= 0, otherwise -1) is a choice made *outside* the mouse - the
// game controller, whose bindings main.cpp owns. It behaves exactly like
// clicking that button, close animation included, so the caller needs only one
// action path and the pad cannot leave a dialog stuck open.
int messageDialog(platform::Renderer& renderer, const char* id, const char* title,
    const std::vector<std::string>& buttons, const std::vector<bool>& primary,
    int forcedChoice = -1);

// The ELUA card: messageDialog with a scroll-free paragraph of body text and a
// "以后不再显示" checkbox pinned above the button row. Same return contract as
// messageDialog, plus:
//   `accepted` mirrors the checkbox every frame (in and out). `accepted` is
//   *in/out*: the caller seeds it from the profile and stores it when the
//   dialog is dismissed, so the flag survives no matter which button closed it.
// `forcedChoice` is the controller's pick, as above.
int eulaDialog(platform::Renderer& renderer, const char* id, const char* title,
    const std::vector<std::string>& lines, const char* checkLabel, bool* accepted,
    const std::vector<std::string>& buttons, const std::vector<bool>& primary,
    int forcedChoice = -1);

} // namespace ui
