// CppSekai - pjsk style UI component library (see Ui.hpp).
// Draws everything on ImGui windows with rounded cards + capsule buttons,
// matching the in-game pjsk dialog look (light card, dark backdrop, mint
// primary buttons, dark close X). Cards scale in/out and can be dragged by
// their header strip.
#include "Ui.hpp"

#include "Intro.hpp"
#include "platform/Audio.hpp"
#include "platform/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace ui
{
// Teal of the player level chip's exp fill / the standalone exp bar. Sampled off
// the official top bar (the green block there is the level progress, not an
// icon plate).
constexpr ImU32 kLevelExp = IM_COL32(0, 199, 181, 255);

// ----------------------------------------------------------------------
// UI sound requests (see SeKind in Ui.hpp). One flag per sound; flushSe()
// plays the highest one that was asked for this frame.
// ----------------------------------------------------------------------
namespace
{
    platform::AudioEngine* gSeAudio = nullptr;
    float gSeVolume = 0.8f;
    constexpr int kSeKindCount = 5; // ui::SeKind, in priority order
    bool gSeRequest[kSeKindCount] = {};
} // namespace

void bindSe(platform::AudioEngine* audio, float volume)
{
    gSeAudio = audio;
    gSeVolume = std::clamp(volume, 0.0f, 1.0f);
}

void se(SeKind kind)
{
    if (gSeAudio == nullptr) {
        return;
    }
    const int index = static_cast<int>(kind);
    if (index >= 0 && index < kSeKindCount) {
        gSeRequest[index] = true;
    }
}

void flushSe()
{
    int pick = -1;
    for (int i = 0; i < kSeKindCount; ++i) {
        if (gSeRequest[i]) {
            pick = i; // highest priority wins
        }
        gSeRequest[i] = false;
    }
    if (pick >= 0 && gSeAudio != nullptr) {
        gSeAudio->playUiSe(static_cast<platform::AudioEngine::UiSe>(pick), gSeVolume);
    }
}
namespace
{
    ImU32 withAlpha(ImU32 col, float alpha)
    {
        // Scale the color's own alpha (replace used to turn any translucent
        // color fully opaque at alpha == 1: kBackdrop's 84 became 255).
        const int srcA = static_cast<int>((col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT);
        const int a = static_cast<int>(srcA * std::clamp(alpha, 0.0f, 1.0f));
        return (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    }

    // Card geometry in "design pixels" (860p reference), scaled by scale().
    constexpr float kCardRadius = 14.0f;
    constexpr float kCapsuleH = 66.0f;
    constexpr float kCloseDraw = 26.0f; // drawn X size
    constexpr float kCloseHit = 38.0f;  // hitbox (bigger than the drawing)
    constexpr float kAnimSec = 0.16f;   // card scale in/out duration
    constexpr float kHeaderH = 44.0f;   // draggable strip height

    float easeInOut(float t)
    {
        return t * t * (3.0f - 2.0f * t);
    }

    struct CardState
    {
        float t = 0.0f;            // 0 = hidden, 1 = fully shown
        bool open = false;         // target state
        bool wasOpen = false;      // for reopening (reset drag)
        bool ended = false;        // close anim finished; next call reopens
        bool soundOpen = false;    // the entrance sound was already played
        ImVec2 drag{0.0f, 0.0f};   // accumulated header-drag offset
    };

    CardState& cardState(const char* id)
    {
        static std::unordered_map<ImGuiID, CardState> states;
        return states[ImGui::GetID(id)];
    }

    // Starts closing a card: window_close is reported here (same frame as the
    // press that dismissed it, so it outranks that press's click) and the
    // bookkeeping is cleared so beginCard does not report a second one.
    void requestClose(CardState& st)
    {
        st.open = false;
        st.soundOpen = false;
        se(SeWindowClose);
    }

    // ------------------------------------------------------------------
    // Animation helpers. Each widget owns one eased value in a map keyed by
    // its ImGui id. The easing is a plain exponential approach towards the
    // target - frame-rate independent (the step is derived from DeltaTime) and
    // it can never overshoot, so nothing ends up looking like it wobbles.
    // ------------------------------------------------------------------
    float animValue(ImGuiID key, float target, float rate)
    {
        static std::unordered_map<ImGuiID, float> values;
        float& v = values[key];
        const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
        v += (target - v) * (1.0f - std::exp(-rate * dt));
        if (std::fabs(target - v) < 0.0015f) {
            v = target; // snap, so nothing keeps repainting for a 0.1% change
        }
        return v;
    }

    // Shorthand for the usual "0 at rest, 1 while hovered / held".
    float animToggle(ImGuiID key, bool on, float rate = 18.0f)
    {
        return animValue(key, on ? 1.0f : 0.0f, rate);
    }

    ImU32 mixColor(ImU32 a, ImU32 b, float t)
    {
        const float k = std::clamp(t, 0.0f, 1.0f);
        const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
        const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(ca.x + (cb.x - ca.x) * k, ca.y + (cb.y - ca.y) * k,
            ca.z + (cb.z - ca.z) * k, ca.w + (cb.w - ca.w) * k));
    }
} // namespace

float scale()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // 860p reference: cards stay comfortably small (at 720p this is ~0.84).
    return std::clamp(display.y / 860.0f, 0.55f, 2.0f);
}

bool beginCard(const char* id, ImVec2* center, ImVec2* size, bool showClose, bool dimBackdrop,
    bool* closeClicked, bool open)
{
    const float s = scale();
    if (closeClicked != nullptr) {
        *closeClicked = false;
    }
    CardState& st = cardState(id);

    // Reopening after a finished close animation: start a fresh entrance.
    if (st.ended && open) {
        st.ended = false;
        st.t = 0.0f;
        st.drag = ImVec2(0.0f, 0.0f);
    }
    st.open = open;
    // Dialog sounds: the entrance one fires when the card is (re)opened, and a
    // plain open=false from the caller (H key, a state change) is caught here.
    if (open && !st.soundOpen) {
        st.soundOpen = true;
        se(SeWindowOpen);
    } else if (!open && st.soundOpen) {
        requestClose(st);
    }
    st.t = std::clamp(st.t + (open ? 1.0f : -1.0f) * ImGui::GetIO().DeltaTime / kAnimSec, 0.0f, 1.0f);
    const float k = easeInOut(st.t);

    // Animated geometry around the (dragged) center. Computed up front so the
    // hosting window can hug the card.
    const ImVec2 animCenter = ImVec2(center->x + st.drag.x, center->y + st.drag.y);
    const ImVec2 animSize = ImVec2(size->x * (0.92f + 0.08f * k), size->y * (0.92f + 0.08f * k));
    *center = animCenter;
    *size = animSize;

    // Non-modal cards (dimBackdrop == false) must not block mouse input on the
    // rest of the screen: a fullscreen invisible window makes
    // io.WantCaptureMouse true everywhere, so lanes can never be hit while the
    // settings card is open. Size the window to the card (+ margin) instead.
    // Modal dialogs keep the fullscreen window (the dim backdrop is itself a
    // click blocker).
    constexpr float kWindowMargin = 48.0f;
    if (dimBackdrop) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    } else {
        const ImVec2 margin(kWindowMargin * s, kWindowMargin * s);
        ImGui::SetNextWindowPos(ImVec2(animCenter.x - animSize.x * 0.5f - margin.x,
            animCenter.y - animSize.y * 0.5f - margin.y));
        ImGui::SetNextWindowSize(ImVec2(animSize.x + margin.x * 2.0f, animSize.y + margin.y * 2.0f));
    }
    // Don't steal focus while a popup (e.g. Combo dropdown) is open: the
    // dropdown is its own window and would end up behind the card otherwise.
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        ImGui::SetNextWindowFocus();
    }
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus * 0;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(id, nullptr, flags);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (st.t <= 0.0f && !open) {
        // Fully closed: draw the (invisible) backdrop only and report done.
        if (dimBackdrop && k > 0.0f) {
            dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, withAlpha(kBackdrop, k));
        }
        ImGui::Dummy(ImVec2(1.0f, 1.0f)); // keep ImGui happy: submit an item
        ImGui::End();
        st.ended = true;
        return false;
    }

    const ImVec2 lo = ImVec2(animCenter.x - animSize.x * 0.5f, animCenter.y - animSize.y * 0.5f);
    const ImVec2 hi = ImVec2(animCenter.x + animSize.x * 0.5f, animCenter.y + animSize.y * 0.5f);

    if (dimBackdrop) {
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, withAlpha(kBackdrop, k));
    }
    // Card + a faint drop shadow like the real dialog.
    dl->AddRectFilled(ImVec2(lo.x + 6.0f * s, lo.y + 10.0f * s), ImVec2(hi.x + 6.0f * s, hi.y + 10.0f * s),
        withAlpha(IM_COL32(40, 40, 60, 40), k), kCardRadius * s);
    dl->AddRectFilled(lo, hi, withAlpha(kCardBg, k), kCardRadius * s);

    // Close X: submitted BEFORE the header drag strip, and the strip below
    // excludes the close corner - zero overlapping items, so the click always
    // lands. (The old layout overlapped the full-width drag strip and hover
    // resolution between the two items made the X feel dead.)
    float headerRight = hi.x;
    if (showClose) {
        const float hit = kCloseHit * s;
        const ImVec2 closeLo(hi.x - hit - 8.0f * s, lo.y + 8.0f * s);
        const ImVec2 closeHi(closeLo.x + hit, closeLo.y + hit);
        ImGui::SetCursorScreenPos(closeLo);
        ImGui::PushID(id);
        ImGui::InvisibleButton("##close", ImVec2(hit, hit));
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        // The texture is a dark X on transparent; dim it slightly on hover.
        // Drawn smaller than its hitbox and centered in it.
        const float draw = kCloseDraw * s;
        const float inset = (hit - draw) * 0.5f;
        dl->AddImage(closeTexture(), ImVec2(closeLo.x + inset, closeLo.y + inset),
            ImVec2(closeHi.x - inset, closeHi.y - inset), ImVec2(0, 0), ImVec2(1, 1),
            withAlpha(IM_COL32(255, 255, 255, 255), (hovered ? 0.55f : 1.0f) * k));
        headerRight = closeLo.x - 2.0f * s;
        if (clicked) {
            // Dismissing via the X closes the card right away, so the close
            // sound replaces the click instead of queueing up behind it.
            requestClose(st);
        }
        if (closeClicked != nullptr) {
            *closeClicked = clicked;
        }
    }

    // Header strip: drag the card around.
    const ImVec2 headerLo = lo;
    const ImVec2 headerHi = ImVec2(headerRight, lo.y + kHeaderH * s);
    ImGui::SetCursorScreenPos(headerLo);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##drag", ImVec2(headerHi.x - headerLo.x, headerHi.y - headerLo.y));
    if (ImGui::IsItemActive()) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        st.drag.x += delta.x;
        st.drag.y += delta.y;
        *center = ImVec2(animCenter.x + delta.x, animCenter.y + delta.y);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(lo.x, lo.y + kHeaderH * s));
    // Content is positioned absolutely; still submit an item so the window
    // layout is valid (ImGui asserts on cursor moves without items).
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    return true;
}

void endCard()
{
    ImGui::End();
}

int tabBar(const char* id, const std::vector<std::string>& tabs, int* active, float rowWidth)
{
    const float s = scale();
    if (tabs.empty() || active == nullptr) {
        return -1;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    const float fontSize = 24.0f * s;
    const float tabH = 50.0f * s;
    const float radius = 14.0f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = rowWidth > 0.0f ? rowWidth : ImGui::GetContentRegionAvail().x;
    const float tabW = rowW / static_cast<float>(tabs.size());

    int result = *active;
    ImGui::PushID(id);
    for (size_t i = 0; i < tabs.size(); ++i) {
        const bool isActive = static_cast<int>(i) == *active;
        // The selected tab slides up out of the row and the colours cross-fade
        // instead of snapping. Inactive tabs sit a bit lower (bottom aligned).
        const float act = animToggle(ImGui::GetID(tabs[i].c_str()), isActive, 15.0f);
        const float x0 = pos.x + static_cast<float>(i) * tabW;
        const float y0 = pos.y + (1.0f - act) * 8.0f * s;
        const ImVec2 lo(x0 + 4.0f * s, y0);
        const ImVec2 hi(x0 + tabW - 4.0f * s, pos.y + tabH);

        ImGui::SetCursorScreenPos(lo);
        ImGui::InvisibleButton(tabs[i].c_str(), ImVec2(hi.x - lo.x, hi.y - lo.y));
        const bool hovered = ImGui::IsItemHovered();
        const float hov = animToggle(ImGui::GetItemID() ^ 0x51u, hovered && !isActive, 14.0f);
        if (ImGui::IsItemClicked()) {
            se(SeClick);
            result = static_cast<int>(i);
            *active = result;
        }
        ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));

        constexpr ImU32 kTabHover = IM_COL32(238, 238, 246, 255);
        ImU32 fill = mixColor(kTabIdle, kCardBg, act);
        fill = mixColor(fill, kTabHover, hov);
        const ImU32 textColor = mixColor(
            mixColor(IM_COL32(125, 125, 148, 255), kTitleText, act), kTitleText, hov * 0.7f);
        dl->AddRectFilled(lo, hi, fill, radius, ImDrawFlags_RoundCornersTop);
        const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, tabs[i].c_str());
        dl->AddText(font, fontSize,
            ImVec2((lo.x + hi.x - ts.x) * 0.5f, (lo.y + hi.y - ts.y) * 0.5f), textColor, tabs[i].c_str());
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + tabH + 4.0f * s));
    return result;
}

bool slider(const char* id, float* value, float minV, float maxV, float step, const char* fmt,
    float width, bool enabled)
{
    if (value == nullptr) {
        return false;
    }
    const float s = scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    const float fontSize = 23.0f * s;
    const float btnSize = 31.0f * s;
    const float btnRadius = 8.0f * s;
    const float trackH = 6.0f * s;
    const float thumbR = 11.0f * s;
    const float rowH = 62.0f * s; // value text + track row (was 96/80/72/64)

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(rowW, rowH)); // reserve the block
    ImGui::PushID(id);
    // A disabled row is drawn washed out and eats every click: it is meant for
    // values that are currently derived from another setting (BAD while it is
    // slaved to MISS), where letting the user drag it would be a lie.
    if (!enabled) {
        ImGui::BeginDisabled();
    }

    const float trackY = pos.y + rowH * 0.68f;
    const float trackX0 = pos.x + btnSize + 16.0f * s;
    const float trackX1 = pos.x + rowW - btnSize - 16.0f * s;
    bool changed = false;

    // Value above the track, centered, in pink.
    char valueText[32];
    std::snprintf(valueText, sizeof(valueText), fmt, *value);
    const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, valueText);
    dl->AddText(font, fontSize,
        ImVec2(pos.x + (rowW - ts.x) * 0.5f, trackY - thumbR - ts.y - 8.0f * s),
        enabled ? kNotePink : kDisabledText, valueText);

    // Track + thumb.
    const float frac = std::clamp((*value - minV) / std::max(1e-6f, maxV - minV), 0.0f, 1.0f);
    const float thumbX = trackX0 + (trackX1 - trackX0) * frac;
    dl->AddRectFilled(ImVec2(trackX0, trackY - trackH * 0.5f), ImVec2(trackX1, trackY + trackH * 0.5f),
        enabled ? kPrimary : kDisabledFill, trackH * 0.5f);

    // Drag the thumb. The thumb itself is drawn further down, after the buttons,
    // so its hover growth is already known by the time it is painted.
    ImGui::SetCursorScreenPos(ImVec2(trackX0 - thumbR, trackY - thumbR * 2.0f));
    ImGui::InvisibleButton("##track", ImVec2(trackX1 - trackX0 + thumbR * 2.0f, thumbR * 4.0f));
    const bool trackHot = enabled && (ImGui::IsItemHovered() || ImGui::IsItemActive());
    if (ImGui::IsItemActive()) {
        if (ImGui::IsItemActivated()) {
            se(SeClick);
        }
        const float mx = ImGui::GetIO().MousePos.x;
        const float t = std::clamp((mx - trackX0) / std::max(1.0f, trackX1 - trackX0), 0.0f, 1.0f);
        const float v = minV + (maxV - minV) * t;
        if (v != *value) {
            *value = v;
            changed = true;
        }
    }

    // Dark -/+ buttons.
    auto darkButton = [&](const char* label, float cx) {
        const ImVec2 lo(cx, trackY - btnSize * 0.5f);
        const ImVec2 hi(cx + btnSize, trackY + btnSize * 0.5f);
        ImGui::SetCursorScreenPos(lo);
        ImGui::InvisibleButton(label, ImVec2(btnSize, btnSize));
        const bool clicked = ImGui::IsItemClicked();
        const bool held = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        const ImGuiID btnKey = ImGui::GetItemID();
        if (clicked) {
            se(SeClick);
        }
        ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
        ImU32 fill = enabled ? kDarkBtn : kDisabledFill;
        // Hover / press blend rather than snap, so the row does not flicker when
        // the pointer sweeps across the two buttons.
        if (enabled) {
            fill = mixColor(fill, IM_COL32(96, 96, 114, 255), animToggle(btnKey ^ 0x32u, hovered, 18.0f));
            fill = mixColor(fill, IM_COL32(72, 72, 86, 255), animToggle(btnKey ^ 0x33u, held, 26.0f));
        }
        dl->AddRectFilled(lo, hi, fill, btnRadius);
        // White glyph.
        const float c = btnSize * 0.5f;
        const float m = btnSize * 0.28f;
        const ImVec2 mid(lo.x + c, lo.y + c);
        dl->AddRectFilled(ImVec2(mid.x - m, mid.y - 2.0f * s), ImVec2(mid.x + m, mid.y + 2.0f * s),
            IM_COL32(255, 255, 255, 255), 1.5f * s);
        if (label[0] == '+') {
            dl->AddRectFilled(ImVec2(mid.x - 2.0f * s, mid.y - m), ImVec2(mid.x + 2.0f * s, mid.y + m),
                IM_COL32(255, 255, 255, 255), 1.5f * s);
        }
        return clicked;
    };
    if (darkButton("-", pos.x)) {
        *value = std::max(minV, *value - step);
        changed = true;
    }
    if (darkButton("+", trackX1 + 16.0f * s)) {
        *value = std::min(maxV, *value + step);
        changed = true;
    }

    // Thumb last: it swells a little while the row is hovered or dragged.
    const float hot = enabled ? animToggle(ImGui::GetID("##thumb"), trackHot, 16.0f) : 0.0f;
    const float thumbScale = 1.0f + 0.16f * hot;
    dl->AddCircleFilled(ImVec2(thumbX, trackY), (thumbR + 2.0f * s) * thumbScale,
        IM_COL32(150, 150, 170, static_cast<int>(60.0f + 60.0f * hot)));
    dl->AddCircleFilled(ImVec2(thumbX, trackY), thumbR * thumbScale, enabled ? kWhiteBtn : kDisabledFill);
    if (!enabled) {
        ImGui::EndDisabled();
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + rowH));
    return changed;
}

void infoRows(const std::vector<std::pair<std::string, std::string>>& rows, float rowWidth)
{
    const float s = scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    const float fontSize = 28.0f * s;
    const float rowH = 46.0f * s;
    const float pad = 12.0f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = rowWidth > 0.0f ? rowWidth : ImGui::GetContentRegionAvail().x;
    const float boxH = static_cast<float>(rows.size()) * rowH + pad * 2.0f;
    ImGui::Dummy(ImVec2(rowW, boxH + 6.0f * s));

    dl->AddRectFilled(pos, ImVec2(pos.x + rowW, pos.y + boxH), kRowsBg, 14.0f * s);
    const float splitX = pos.x + rowW * 0.52f;
    float y = pos.y + pad;
    for (const auto& row : rows) {
        // Thin divider for this row (not drawn past the text block edges).
        dl->AddRectFilled(ImVec2(splitX, y + 6.0f * s), ImVec2(splitX + 2.0f * s, y + rowH - 6.0f * s),
            kDivider, 1.0f * s);
        const ImVec2 tls = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, row.first.c_str());
        dl->AddText(font, fontSize, ImVec2(pos.x + 30.0f * s, y + (rowH - tls.y) * 0.5f), kBodyText,
            row.first.c_str());
        const ImVec2 tvs = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, row.second.c_str());
        dl->AddText(font, fontSize, ImVec2(splitX + 24.0f * s, y + (rowH - tvs.y) * 0.5f), kNotePink,
            row.second.c_str());
        y += rowH;
    }
}

bool capsuleButton(const char* label, const ImVec2& sizeIn, bool primary)
{
    const float s = scale();
    // Every capsule draws a notch smaller than the space its caller reserves.
    const ImVec2 size(sizeIn.x * 0.85f, sizeIn.y * 0.85f);
    ImVec2 lo = ImGui::GetCursorScreenPos();
    ImVec2 hi = ImVec2(lo.x + size.x, lo.y + size.y);
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return false;
    }
    ImGui::InvisibleButton(label, size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (clicked) {
        se(SeClick);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiID btnKey = ImGui::GetItemID();
    const float hov = animToggle(btnKey ^ 0x11u, hovered, 16.0f);
    const float press = animToggle(btnKey ^ 0x12u, held, 28.0f);
    // The capsule grows a hair on hover and gives a little while pressed.
    const float grow = 1.0f + 0.02f * hov - 0.03f * press;
    if (grow != 1.0f) {
        const ImVec2 mid((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
        const float hw = (hi.x - lo.x) * 0.5f * grow;
        const float hh = (hi.y - lo.y) * 0.5f * grow;
        lo = ImVec2(mid.x - hw, mid.y - hh);
        hi = ImVec2(mid.x + hw, mid.y + hh);
    }
    const float radius = (hi.y - lo.y) * 0.5f;
    ImU32 fill = primary ? kPrimary : kWhiteBtn;
    fill = mixColor(fill, primary ? kPrimaryHover : kWhiteHover, hov);
    fill = mixColor(fill, primary ? kPrimaryPress : kWhitePress, press);
    dl->AddRectFilled(ImVec2(lo.x, lo.y + 3.0f * s), ImVec2(hi.x, hi.y + 3.0f * s), IM_COL32(150, 150, 170, 60),
        radius); // soft shadow
    dl->AddRectFilled(lo, hi, fill, radius);

    ImFont* font = game::bodyFont();
    const float fontSize = std::min(27.0f * s, size.y * 0.48f);
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    dl->AddText(font, fontSize,
        ImVec2((lo.x + hi.x - textSize.x) * 0.5f, (lo.y + hi.y - textSize.y) * 0.5f), kBtnText, label);
    return clicked;
}

bool combo(const char* id, const char* preview, const std::vector<std::string>& items, int* index,
    float width, ImGuiComboFlags flags, float scaleHint)
{
    if (index == nullptr) {
        return false;
    }
    const float s = scaleHint > 0.0f ? scaleHint : scale();
    // ImGuiComboFlags_HeightSmall caps the popup at 4 rows, but its row estimate
    // ignores the caller's FramePadding while Selectable::height does not - with
    // the select screen's padding a 4-item list is ~4px taller than the cap, and
    // the last row gets shaved. HeightRegular only raises the ceiling; the popup
    // is AlwaysAutoResize, so it still hugs the content either way.
    if (flags & ImGuiComboFlags_HeightSmall) {
        flags = (flags & ~ImGuiComboFlags_HeightSmall) | ImGuiComboFlags_HeightRegular;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(id);
    const ImGuiID key = ImGui::GetID("##combo");
    // ImGui rebuilds the popup from scratch on every frame it is open, so an
    // eased 0..1 is what turns "it appeared" into "it eased in".
    //
    // The open flag is remembered from the *previous* frame on purpose:
    // ImGui::IsPopupOpen(<combo id>) can never be true here - it compares against
    // the popup *window*'s own id, and a combo's popup window is named
    // "##Combo_%02d" (recycled by depth), not after the combo. That is how the
    // slide animation ended up permanently stuck at t = 0: the popup sat 14px too
    // high forever and the first row (关闭 in the group-by combo) was clipped
    // away by the popup's own clip rectangle, which Begin() had already computed
    // from the un-moved position. Reading it one frame late gives the same easing
    // (t is ~0 on the frame the popup opens) without the lie.
    static std::unordered_map<ImGuiID, bool> comboOpen;
    const float t = animValue(key, comboOpen[key] ? 1.0f : 0.0f, 14.0f);
    ImGui::SetNextItemWidth(width);
    bool changed = false;
    const bool popupOpen = ImGui::BeginCombo("##combo", preview, flags | ImGuiComboFlags_NoArrowButton);
    comboOpen[key] = popupOpen;
    if (popupOpen) {
        // NOTE: never SetWindowPos() the popup to animate it in - moving the
        // window mid-frame leaves its content outside the clip rectangle that
        // Begin() already computed, so whichever row ends up outside simply
        // disappears. Fade the contents instead; the rows stay put.
        const float fade = std::clamp(t * 1.6f, 0.0f, 1.0f); // snap in fast, then settle
        if (fade < 0.999f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
        }
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (ImGui::Selectable(items[i].c_str(), *index == i)) {
                *index = i;
                changed = true;
                se(SeClick);
            }
        }
        if (fade < 0.999f) {
            ImGui::PopStyleVar();
        }
        ImGui::EndCombo();
    }
    // Own chevron: the built-in one is painted inside the widget and cannot be
    // animated, so it is suppressed above and drawn here instead - it rotates as
    // the list opens and back when it closes.
    const ImVec2 lo = ImGui::GetItemRectMin();
    const ImVec2 hi = ImGui::GetItemRectMax();
    const ImVec2 mid(hi.x - 13.0f * s, (lo.y + hi.y) * 0.5f);
    const float r = 5.5f * s;
    const float ang = t * 3.14159265f; // 0 = pointing down, pi = pointing up
    const float ca = std::cos(ang);
    const float sa = std::sin(ang);
    auto rot = [&](float x, float y) {
        return ImVec2(mid.x + x * ca - y * sa, mid.y + x * sa + y * ca);
    };
    const ImVec2 a = rot(-r, -r * 0.45f);
    const ImVec2 b = rot(r, -r * 0.45f);
    const ImVec2 c = rot(0.0f, r * 0.62f);
    dl->AddTriangleFilled(a, b, c, mixColor(IM_COL32(120, 120, 140, 255), kTitleText, t));
    ImGui::PopID();
    return changed;
}

float anim(ImGuiID id, bool target, float rate)
{
    return animToggle(id, target, rate);
}

ImU32 mix(ImU32 from, ImU32 to, float t)
{
    return mixColor(from, to, t);
}

void caption(const char* text, float sizePx, ImU32 color, float rowWidth)
{
    const float s = scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    const float fontSize = sizePx > 0.0f ? sizePx : 32.0f * s;
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = rowWidth > 0.0f ? rowWidth : ImGui::GetContentRegionAvail().x;
    dl->AddText(font, fontSize, ImVec2(pos.x + (rowW - textSize.x) * 0.5f, pos.y), color, text);
    ImGui::Dummy(ImVec2(rowW, textSize.y + 6.0f * s));
}

void cardTitle(const char* text, float interiorWidth, float sizePx)
{
    const float s = scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 textSize = font->CalcTextSizeA(sizePx * s, FLT_MAX, 0.0f, text);
    dl->AddText(font, sizePx * s, pos, kTitleText, text);
    // Thin rule spanning the interior, a little below the baseline.
    const float ruleY = pos.y + textSize.y + 8.0f * s;
    dl->AddRectFilled(ImVec2(pos.x, ruleY), ImVec2(pos.x + interiorWidth, ruleY + 2.0f * s), kDivider, 1.0f * s);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, ruleY + 12.0f * s));
}

bool checkBox(const char* label, bool* value, float rowWidth, bool enabled)
{
    const float s = scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    // Sized against the settings card's 23px body text: a 32px box with a 26px
    // label read as oversized next to the slider labels and the section rows.
    const float fontSize = 22.0f * s;
    const float boxSize = 24.0f * s;
    const float gap = 10.0f * s;
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    const float groupW = boxSize + gap + textSize.x;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = rowWidth > 0.0f ? rowWidth : ImGui::GetContentRegionAvail().x;
    const ImVec2 boxLo = ImVec2(pos.x + std::max(0.0f, (rowW - groupW) * 0.5f), pos.y);
    const ImVec2 boxHi = ImVec2(boxLo.x + boxSize, boxLo.y + boxSize);
    ImGui::Dummy(ImVec2(rowW, boxSize + 8.0f * s)); // reserve the row

    ImGui::SetCursorScreenPos(boxLo);
    if (!enabled) {
        ImGui::BeginDisabled(); // no hover, no click - the row is inert
    }
    ImGui::InvisibleButton(label, ImVec2(groupW, boxSize));
    const bool clicked = enabled && ImGui::IsItemClicked();
    const bool hovered = enabled && ImGui::IsItemHovered();
    if (!enabled) {
        ImGui::EndDisabled();
    }
    if (clicked && value != nullptr) {
        *value = !*value;
    }
    if (clicked) {
        se(SeClick);
    }
    const bool checked = value != nullptr && *value;

    const ImGuiID boxKey = ImGui::GetItemID();
    const float tick = animValue(boxKey ^ 0x21u, checked ? 1.0f : 0.0f, 20.0f);
    const float hov = animToggle(boxKey ^ 0x22u, hovered && !checked, 16.0f);
    ImU32 fill = mixColor(kWhiteBtn, IM_COL32(255, 235, 243, 255), hov);
    fill = mixColor(fill, kCheckPink, tick);
    if (!enabled) {
        // Greyed out: same shapes, just drained of colour, so a disabled row
        // still reads as "this is a setting" instead of disappearing.
        fill = mixColor(fill, IM_COL32(226, 226, 232, 255), 0.65f);
    }
    // Radius follows the box: the old 10px corner was tuned for a 32px box and
    // looked round-shouldered once the box shrank to 24.
    const float radius = boxSize * 0.26f;
    dl->AddRectFilled(ImVec2(boxLo.x, boxLo.y + 2.0f * s), ImVec2(boxHi.x, boxHi.y + 2.0f * s),
        IM_COL32(150, 150, 170, 50), radius); // shadow
    dl->AddRectFilled(boxLo, boxHi, fill, radius);
    if (tick > 0.02f) {
        // White check: two thick segments, growing out of the middle of the box
        // as the state flips (and fading with it).
        const float grow = 0.5f + 0.5f * tick;
        const ImVec2 mid((boxLo.x + boxHi.x) * 0.5f, (boxLo.y + boxHi.y) * 0.5f);
        auto scaled = [&](float x, float y) {
            return ImVec2(mid.x + (x - mid.x) * grow, mid.y + (y - mid.y) * grow);
        };
        const ImVec2 pts[3] = {scaled(boxLo.x + boxSize * 0.22f, boxLo.y + boxSize * 0.52f),
            scaled(boxLo.x + boxSize * 0.44f, boxLo.y + boxSize * 0.74f),
            scaled(boxLo.x + boxSize * 0.80f, boxLo.y + boxSize * 0.28f)};
        dl->AddPolyline(pts, 3, withAlpha(IM_COL32(255, 255, 255, 255), tick), 0, 4.0f * s);
    }
    if (tick < 0.99f) {
        dl->AddRect(boxLo, boxHi, withAlpha(kDivider, 1.0f - tick), radius, 0, 2.0f * s);
    }
    dl->AddText(font, fontSize, ImVec2(boxHi.x + gap, boxLo.y + (boxSize - textSize.y) * 0.5f),
        enabled ? kBodyText : withAlpha(kBodyText, 0.45f), label);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + boxSize + 8.0f * s));
    return checked;
}

bool stepper(const char* id, float* value, const std::vector<float>& deltas, const char* fmt, float rowWidth,
    const std::vector<std::string>& presets)
{
    if (value == nullptr || deltas.empty()) {
        return false;
    }
    // Pick-one mode (see Ui.hpp): the value is a selected slot, not a number.
    const bool pickOne = presets.size() == deltas.size();
    const float s = scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    const float fontSize = 28.0f * s;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = rowWidth > 0.0f ? rowWidth : ImGui::GetContentRegionAvail().x;
    const float y = pos.y;
    ImGui::Dummy(ImVec2(rowW, 62.0f * s + 8.0f * s)); // reserve the row
    ImGui::PushID(id);

    // Capsule widths. Numeric mode is fixed (a "+1" label is narrow). Pick-one
    // measures the widest preset name and splits whatever is left over, so
    // three Chinese labels plus the value pill always fit inside `rowW`
    // instead of running off the card at small UI scales.
    const float btnH = 62.0f * s;
    float btnW = 104.0f * s;
    float pillW = 132.0f * s;
    float gap = 14.0f * s;
    if (pickOne) {
        gap = 10.0f * s;
        float widest = 0.0f;
        for (const std::string& label : presets) {
            widest = std::max(widest, font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str()).x);
        }
        const float n = static_cast<float>(presets.size());
        // total = n * btnW + pillW + n * gap  (the pill sits in slot 0's place)
        const float usable = rowW - gap * n;
        pillW = std::clamp(widest + 40.0f * s, 70.0f * s, usable / (n + 1.0f));
        btnW = std::clamp((usable - pillW) / n, widest + 22.0f * s, 200.0f * s);
    }

    bool changed = false;
    auto drawCapsule = [&](const char* label, float cx, float w, ImU32 fill, ImU32 textColor) {
        const ImVec2 lo(cx, y);
        const ImVec2 hi(cx + w, y + btnH);
        ImGui::SetCursorScreenPos(lo);
        ImGui::InvisibleButton(label, ImVec2(w, btnH));
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const ImGuiID key = ImGui::GetItemID();
        if (clicked) {
            se(SeClick);
        }
        ImU32 f = mixColor(fill, IM_COL32(240, 240, 247, 255), animToggle(key ^ 0x41u, hovered, 16.0f));
        f = mixColor(f, IM_COL32(214, 214, 228, 255), animToggle(key ^ 0x42u, held, 28.0f));
        dl->AddRectFilled(ImVec2(lo.x, lo.y + 2.0f * s), ImVec2(hi.x, hi.y + 2.0f * s),
            IM_COL32(150, 150, 170, 50), btnH * 0.5f); // shadow
        dl->AddRectFilled(lo, hi, f, btnH * 0.5f);
        const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
        dl->AddText(font, fontSize, ImVec2((lo.x + hi.x - ts.x) * 0.5f, (lo.y + hi.y - ts.y) * 0.5f), textColor,
            label);
        return clicked;
    };

    // Value pill in the middle, deltas around it: negative ones on the left
    // (largest magnitude first, like the reference dialog).
    std::vector<float> neg, posD;
    for (float d : deltas) {
        (d < 0.0f ? neg : posD).push_back(d);
    }
    std::sort(neg.begin(), neg.end());                          // -1, -0.1, -0.01
    std::sort(posD.begin(), posD.end(), std::greater<float>()); // +1, +0.1, +0.01

    const float blockW = pillW + static_cast<float>(neg.size() + posD.size()) * (btnW + gap);
    float cx = pos.x + std::max(0.0f, (rowW - blockW) * 0.5f);
    // The selection is carried as a *slot*: -1 = nothing selected (the value
    // was customised), 0..N-1 = that preset. Capsules are drawn in the same
    // visual order the slots were stored in (deltas order), so slot i is always
    // the capsule `deltas[i]` - no negative/positive reordering in pick-one
    // mode, which would make "left capsule = last preset" for no reason.
    int pickIdx = static_cast<int>(std::lround(*value));
    if (pickIdx < -1 || pickIdx >= static_cast<int>(deltas.size())) {
        pickIdx = -1;
    }
    const auto press = [&](float d, int slot, bool* selectedOut) {
        if (!pickOne) {
            se(SeClick);
            *value += d;
            return true;
        }
        // Pick-one: a capsule *is* its preset, so pressing it selects that slot
        // outright - the deltas are only used for their signs, which is why
        // they no longer move the selection.
        const int cur = static_cast<int>(std::lround(*value));
        if (cur == slot) {
            return false; // already selected
        }
        se(SeClick);
        *value = static_cast<float>(slot);
        if (selectedOut != nullptr) {
            *selectedOut = true;
        }
        return true;
    };

    int slot = 0;
    // Pick-one walks presets in list order; the value-based layout keeps the
    // -N..+N split (negatives left of the pill).
    std::vector<float> order;
    if (pickOne) {
        order = deltas;
    } else {
        order.insert(order.end(), neg.begin(), neg.end());
    }
    for (float d : order) {
        char label[16];
        std::snprintf(label, sizeof(label), "%+g", d);
        const int mySlot = slot++;
        const bool selected = pickOne && pickIdx == mySlot;
        if (drawCapsule(pickOne ? presets[mySlot].c_str() : label, cx, btnW,
                selected ? kPrimary : kWhiteBtn, kBtnText)) {
            changed |= press(d, mySlot, nullptr);
        }
        cx += btnW + gap;
    }
    {
        const ImVec2 lo(cx, y);
        const ImVec2 hi(cx + pillW, y + btnH);
        dl->AddRectFilled(ImVec2(lo.x, lo.y + 2.0f * s), ImVec2(hi.x, hi.y + 2.0f * s),
            IM_COL32(150, 150, 170, 50), btnH * 0.5f);
        dl->AddRectFilled(lo, hi, kPillBg, btnH * 0.5f);
        char valueText[32];
        if (pickOne) {
            const int idx = static_cast<int>(std::lround(*value));
            std::snprintf(valueText, sizeof(valueText), "%s",
                (idx >= 0 && idx < static_cast<int>(presets.size())) ? presets[idx].c_str() : "--");
        } else {
            std::snprintf(valueText, sizeof(valueText), fmt, *value);
        }
        const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, valueText);
        dl->AddText(font, fontSize, ImVec2((lo.x + hi.x - ts.x) * 0.5f, (lo.y + hi.y - ts.y) * 0.5f),
            kBtnText, valueText);
        cx += pillW + gap;
    }
    std::vector<float> order2;
    if (!pickOne) {
        order2 = posD;
    }
    for (float d : order2) {
        char label[16];
        std::snprintf(label, sizeof(label), "%+g", d);
        const int mySlot = slot++;
        const bool selected = pickOne && pickIdx == mySlot;
        if (drawCapsule(pickOne ? presets[mySlot].c_str() : label, cx, btnW,
                selected ? kPrimary : kWhiteBtn, kBtnText)) {
            changed |= press(d, mySlot, nullptr);
        }
        cx += btnW + gap;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + btnH + 8.0f * s));
    return changed;
}

int messageDialog(platform::Renderer& renderer, const char* id, const char* title,
    const std::vector<std::string>& buttons, const std::vector<bool>& primary, int forcedChoice)
{
    const float s = scale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // Button geometry first: the card must be wide enough for the capsule row
    // (the 3-button pause dialog needs 524*s - a fixed 500*s card clipped it).
    const float btnH = kCapsuleH * s * 0.62f;
    const float btnW = 146.0f * s;
    const float gap = 20.0f * s;
    const float buttonsW = static_cast<float>(buttons.size()) * btnW
        + (static_cast<float>(buttons.size()) - 1) * gap;
    const float cardW = std::max(500.0f * s, buttonsW + 56.0f * s);
    const float cardH = 184.0f * s;
    ImVec2 center = ImVec2(display.x * 0.5f, display.y * 0.5f);
    ImVec2 size = ImVec2(cardW, cardH);

    CardState& st = cardState(id);
    if (st.ended) {
        // Previous run finished closing: start a fresh entrance.
        st.ended = false;
        st.t = 0.0f;
        st.drag = ImVec2(0.0f, 0.0f);
        st.open = true;
    } else if (!st.open && st.t <= 0.0f) {
        st.open = true; // fresh dialog
    }
    bool closeClicked = false;
    if (!beginCard(id, &center, &size, true, true, &closeClicked, st.open)) {
        return -2; // close animation finished
    }
    int result = -1;
    if (closeClicked) {
        st.open = false; // animate out; caller sees -2 when done
    }
    ImGui::SetCursorScreenPos(ImVec2(center.x - size.x * 0.5f + 30.0f * s, center.y - size.y * 0.5f + 20.0f * s));
    cardTitle(title, size.x - 60.0f * s);

    // Capsule row, centered, laid out bottom.
    const float totalW = buttonsW;
    float x = center.x - totalW * 0.5f;
    const float y = center.y + size.y * 0.5f - btnH - 24.0f * s;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const bool isPrimary = i < primary.size() && primary[i];
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (capsuleButton(buttons[i].c_str(), ImVec2(btnW, btnH), isPrimary)) {
            result = static_cast<int>(i);
            // A button dismisses the dialog: window_close wins over the click
            // the capsule just queued, so only one sound is heard.
            requestClose(st);
        }
        x += btnW + gap;
    }
    // A choice the mouse did not make (the game controller): identical to a
    // click, close animation included, so the caller's action handling stays
    // one path and the card cannot be left open behind a running game.
    if (result < 0 && forcedChoice >= 0 && forcedChoice < static_cast<int>(buttons.size())) {
        result = forcedChoice;
        se(SeClick);
        requestClose(st);
    }
    endCard();
    (void)renderer;
    return result >= 0 ? result : -3;
}

void setCloseTexture(ImTextureID texture)
{
    closeTexture() = texture;
}

ImTextureID& closeTexture()
{
    static ImTextureID texture = 0;
    return texture;
}

namespace
{
ImTextureID& levelIconTexture()
{
    static ImTextureID texture = 0;
    return texture;
}
} // namespace

void setLevelIconTexture(ImTextureID texture)
{
    levelIconTexture() = texture;
}

ImVec4 playerLevelChip(ImDrawList* dl, ImFont* font, ImVec2 anchor, int rank, float unit,
    bool alignRight, float expRatio)
{
    if (dl == nullptr) {
        return ImVec4(anchor.x, anchor.y, 0.0f, 0.0f);
    }
    ImFont* f = font != nullptr ? font : ImGui::GetFont();
    const float u = std::max(unit, 1e-3f);

    // 1080p-era numbers, all scaled by `u`. The shape is measured off the
    // official top bar (the chip is 126 x 26 px in a 502 x 52 crop of it), so
    // the aspects match; the *size* is set by whatever the caller lines the chip
    // up with - 33u tall is a ui::combo of the same unit.
    const float h = 33.0f * u;
    const float round = h * 0.5f;
    const float pad = 4.0f * u;      // icon inset inside the pill
    const float icon = h - pad * 2.0f;
    const float capSize = 15.5f * u; // 等级, ~0.42 h cap like the official chip
    const float numSize = 23.0f * u; // the rank digits are the bigger of the two
    const float gapIcon = 8.0f * u;
    const float gapText = 18.0f * u;
    const float padRight = 26.0f * u;
    const char* cap = "等级";
    char num[16];
    std::snprintf(num, sizeof(num), "%d", rank > 0 ? rank : 1);

    const float capW = f->CalcTextSizeA(capSize, FLT_MAX, 0.0f, cap).x;
    const float numW = f->CalcTextSizeA(numSize, FLT_MAX, 0.0f, num).x;
    // Fixed width, like the official chip; only a rank wide enough to run into
    // the caption stretches it.
    float w = 158.0f * u;
    const float needed = pad + icon + gapIcon + capW + gapText + numW + padRight;
    if (needed > w) {
        w = needed;
    }

    const float x0 = alignRight ? anchor.x - w : anchor.x;
    const float y0 = anchor.y;

    // Pill: a dark translucent gray (the official chip sits on top of the live
    // background, so it has to hold its own contrast).
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(38, 38, 52, 208), round);

    // The green left end is the exp bar towards the next rank: it starts at the
    // pill's left cap and its width is the ratio, so a fresh account shows
    // (almost) none of it while a nearly leveled-up one is filled to the brim.
    const float fill = std::clamp(expRatio, 0.0f, 1.0f) * w;
    if (fill > 1.0f) {
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + fill, y0 + h), kLevelExp, round);
        if (fill > h) {
            // Square off the right end: a rounded end would read as a bubble
            // rather than as a bar that is still filling up.
            dl->AddRectFilled(ImVec2(x0 + fill - round, y0), ImVec2(x0 + fill, y0 + h), kLevelExp);
        }
    }

    // Note glyph, drawn straight on top: the sprite is a gold note on a
    // transparent background, so it reads over both the green and the dark pill.
    const float ix = x0 + pad;
    const float iy = y0 + pad;
    const ImTextureID tex = levelIconTexture();
    if (tex != 0) {
        dl->AddImage(tex, ImVec2(ix, iy), ImVec2(ix + icon, iy + icon));
    } else {
        // Fallback: an eighth note, so a missing sprite still reads as 等级.
        const float cx = ix + icon * 0.52f;
        const float cy = iy + icon * 0.58f;
        const float r = icon * 0.17f;
        dl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(255, 236, 120, 255), 20);
        dl->AddRectFilled(ImVec2(cx + r * 0.55f, iy + icon * 0.18f),
            ImVec2(cx + r * 1.15f, cy + r * 0.2f), IM_COL32(255, 236, 120, 255), r * 0.3f);
        dl->AddRectFilled(ImVec2(cx + r * 0.55f, iy + icon * 0.18f),
            ImVec2(cx + r * 1.15f, iy + icon * 0.42f), IM_COL32(255, 236, 120, 255), r * 0.3f);
    }

    const ImU32 textCol = IM_COL32(238, 238, 248, 255);
    dl->AddText(f, capSize, ImVec2(ix + icon + gapIcon, y0 + (h - capSize) * 0.5f - 1.0f * u),
        textCol, cap);
    // The rank is right-aligned in its slot, so the chip keeps its width while
    // the number grows.
    dl->AddText(f, numSize, ImVec2(x0 + w - padRight - numW, y0 + (h - numSize) * 0.5f - 1.0f * u),
        textCol, num);
    return ImVec4(x0, y0, w, h);
}

void expBar(ImDrawList* dl, ImVec2 pos, float width, float unit, float ratio)
{
    if (dl == nullptr || width <= 0.0f) {
        return;
    }
    const float u = std::max(unit, 1e-3f);
    const float h = 10.0f * u;
    const float r = h * 0.5f;
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), IM_COL32(30, 30, 44, 200), r);
    const float fill = std::clamp(ratio, 0.0f, 1.0f) * width;
    if (fill > h) {
        // Full-round left end, square right end: a rounded rect narrower than
        // its own height turns into a blob, so the fill is drawn as a rounded
        // rect plus a small square capping the right edge.
        dl->AddRectFilled(pos, ImVec2(pos.x + fill, pos.y + h), kLevelExp, r);
        dl->AddRectFilled(ImVec2(pos.x + fill - r, pos.y), ImVec2(pos.x + fill, pos.y + h), kLevelExp);
    } else if (fill > 0.0f) {
        dl->AddRectFilled(pos, ImVec2(pos.x + fill, pos.y + h), kLevelExp, r);
    }
}

} // namespace ui
