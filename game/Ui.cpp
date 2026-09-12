// CppSekai - pjsk style UI component library (see Ui.hpp).
// Draws everything on ImGui windows with rounded cards + capsule buttons,
// matching the in-game pjsk dialog look (light card, dark backdrop, mint
// primary buttons, dark close X). Cards scale in/out and can be dragged by
// their header strip.
#include "Ui.hpp"

#include "Intro.hpp"
#include "platform/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace ui
{
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
    constexpr float kCardRadius = 24.0f;
    constexpr float kCapsuleH = 66.0f;
    constexpr float kCloseSize = 44.0f;
    constexpr float kAnimSec = 0.16f; // card scale in/out duration
    constexpr float kHeaderH = 44.0f; // draggable strip height

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
        ImVec2 drag{0.0f, 0.0f};   // accumulated header-drag offset
    };

    CardState& cardState(const char* id)
    {
        static std::unordered_map<ImGuiID, CardState> states;
        return states[ImGui::GetID(id)];
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

    // Header strip: drag the card around. The close X below wins in its own
    // rectangle because it is submitted later.
    const ImVec2 headerLo = lo;
    const ImVec2 headerHi = ImVec2(hi.x, lo.y + kHeaderH * s);
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

    if (showClose) {
        const ImVec2 closeLo = ImVec2(hi.x - (kCloseSize + 14.0f) * s, lo.y + 14.0f * s);
        const ImVec2 closeHi = ImVec2(hi.x - 14.0f * s, lo.y + (14.0f + kCloseSize) * s);
        ImGui::SetCursorScreenPos(closeLo);
        ImGui::PushID(id);
        ImGui::InvisibleButton("##close", ImVec2(closeHi.x - closeLo.x, closeHi.y - closeLo.y));
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        // The texture is a dark X on transparent; dim it slightly on hover.
        dl->AddImage(closeTexture(), closeLo, closeHi, ImVec2(0, 0), ImVec2(1, 1),
            withAlpha(IM_COL32(255, 255, 255, 255), (hovered ? 0.55f : 1.0f) * k));
        if (closeClicked != nullptr) {
            *closeClicked = clicked;
        }
    }
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
    const float fontSize = 28.0f * s;
    const float tabH = 58.0f * s;
    const float radius = 16.0f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = rowWidth > 0.0f ? rowWidth : ImGui::GetContentRegionAvail().x;
    const float tabW = rowW / static_cast<float>(tabs.size());

    int result = *active;
    ImGui::PushID(id);
    for (size_t i = 0; i < tabs.size(); ++i) {
        const bool isActive = static_cast<int>(i) == *active;
        const float x0 = pos.x + static_cast<float>(i) * tabW;
        // Inactive tabs sit a bit lower and are shorter (bottom aligned).
        const float y0 = isActive ? pos.y : pos.y + 8.0f * s;
        const ImVec2 lo(x0 + 4.0f * s, y0);
        const ImVec2 hi(x0 + tabW - 4.0f * s, pos.y + tabH);

        ImGui::SetCursorScreenPos(lo);
        ImGui::InvisibleButton(tabs[i].c_str(), ImVec2(hi.x - lo.x, hi.y - lo.y));
        if (ImGui::IsItemClicked()) {
            result = static_cast<int>(i);
            *active = result;
        }
        ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));

        const ImU32 fill = isActive ? kCardBg : kTabIdle;
        const ImU32 textColor = isActive ? kTitleText : IM_COL32(125, 125, 148, 255);
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
    float width)
{
    if (value == nullptr) {
        return false;
    }
    const float s = scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    const float fontSize = 30.0f * s;
    const float btnSize = 52.0f * s;
    const float btnRadius = 14.0f * s;
    const float trackH = 8.0f * s;
    const float thumbR = 15.0f * s;
    const float rowH = 72.0f * s; // value text + track row (was 96, then 80: cards shrank)

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(rowW, rowH)); // reserve the block
    ImGui::PushID(id);

    const float trackY = pos.y + rowH * 0.68f;
    const float trackX0 = pos.x + btnSize + 26.0f * s;
    const float trackX1 = pos.x + rowW - btnSize - 26.0f * s;
    bool changed = false;

    // Value above the track, centered, in pink.
    char valueText[32];
    std::snprintf(valueText, sizeof(valueText), fmt, *value);
    const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, valueText);
    dl->AddText(font, fontSize,
        ImVec2(pos.x + (rowW - ts.x) * 0.5f, trackY - thumbR - ts.y - 8.0f * s), kNotePink, valueText);

    // Track + thumb.
    const float frac = std::clamp((*value - minV) / std::max(1e-6f, maxV - minV), 0.0f, 1.0f);
    const float thumbX = trackX0 + (trackX1 - trackX0) * frac;
    dl->AddRectFilled(ImVec2(trackX0, trackY - trackH * 0.5f), ImVec2(trackX1, trackY + trackH * 0.5f),
        kPrimary, trackH * 0.5f);
    dl->AddCircleFilled(ImVec2(thumbX, trackY), thumbR + 2.0f * s, IM_COL32(150, 150, 170, 60));
    dl->AddCircleFilled(ImVec2(thumbX, trackY), thumbR, kWhiteBtn);

    // Drag the thumb.
    ImGui::SetCursorScreenPos(ImVec2(trackX0 - thumbR, trackY - thumbR * 2.0f));
    ImGui::InvisibleButton("##track", ImVec2(trackX1 - trackX0 + thumbR * 2.0f, thumbR * 4.0f));
    if (ImGui::IsItemActive()) {
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
        ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
        ImU32 fill = kDarkBtn;
        if (held) {
            fill = IM_COL32(72, 72, 86, 255);
        }
        dl->AddRectFilled(lo, hi, fill, btnRadius);
        // White glyph.
        const float c = btnSize * 0.5f;
        const float m = btnSize * 0.28f;
        const ImVec2 mid(lo.x + c, lo.y + c);
        dl->AddRectFilled(ImVec2(mid.x - m, mid.y - 2.5f * s), ImVec2(mid.x + m, mid.y + 2.5f * s),
            IM_COL32(255, 255, 255, 255), 2.0f * s);
        if (label[0] == '+') {
            dl->AddRectFilled(ImVec2(mid.x - 2.5f * s, mid.y - m), ImVec2(mid.x + 2.5f * s, mid.y + m),
                IM_COL32(255, 255, 255, 255), 2.0f * s);
        }
        return clicked;
    };
    if (darkButton("-", pos.x)) {
        *value = std::max(minV, *value - step);
        changed = true;
    }
    if (darkButton("+", trackX1 + 26.0f * s)) {
        *value = std::min(maxV, *value + step);
        changed = true;
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

bool capsuleButton(const char* label, const ImVec2& size, bool primary)
{
    const float s = scale();
    ImVec2 lo = ImGui::GetCursorScreenPos();
    ImVec2 hi = ImVec2(lo.x + size.x, lo.y + size.y);
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return false;
    }
    ImGui::InvisibleButton(label, size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = (hi.y - lo.y) * 0.5f;
    ImU32 fill = primary ? kPrimary : kWhiteBtn;
    if (held) {
        fill = primary ? kPrimaryPress : kWhitePress;
    } else if (hovered) {
        fill = primary ? kPrimaryHover : kWhiteHover;
    }
    dl->AddRectFilled(ImVec2(lo.x, lo.y + 3.0f * s), ImVec2(hi.x, hi.y + 3.0f * s), IM_COL32(150, 150, 170, 60),
        radius); // soft shadow
    dl->AddRectFilled(lo, hi, fill, radius);

    ImFont* font = game::bodyFont();
    const float fontSize = 34.0f * s;
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    dl->AddText(font, fontSize,
        ImVec2((lo.x + hi.x - textSize.x) * 0.5f, (lo.y + hi.y - textSize.y) * 0.5f), kBtnText, label);
    return clicked;
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

bool checkBox(const char* label, bool* value, float rowWidth)
{
    const float s = scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    const float fontSize = 30.0f * s;
    const float boxSize = 40.0f * s;
    const float gap = 16.0f * s;
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    const float groupW = boxSize + gap + textSize.x;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = rowWidth > 0.0f ? rowWidth : ImGui::GetContentRegionAvail().x;
    const ImVec2 boxLo = ImVec2(pos.x + std::max(0.0f, (rowW - groupW) * 0.5f), pos.y);
    const ImVec2 boxHi = ImVec2(boxLo.x + boxSize, boxLo.y + boxSize);
    ImGui::Dummy(ImVec2(rowW, boxSize + 8.0f * s)); // reserve the row

    ImGui::SetCursorScreenPos(boxLo);
    ImGui::InvisibleButton(label, ImVec2(groupW, boxSize));
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    if (clicked && value != nullptr) {
        *value = !*value;
    }
    const bool checked = value != nullptr && *value;

    ImU32 fill = checked ? kCheckPink : kWhiteBtn;
    if (hovered && !checked) {
        fill = IM_COL32(255, 235, 243, 255);
    }
    const float radius = 10.0f * s;
    dl->AddRectFilled(ImVec2(boxLo.x, boxLo.y + 2.0f * s), ImVec2(boxHi.x, boxHi.y + 2.0f * s),
        IM_COL32(150, 150, 170, 50), radius); // shadow
    dl->AddRectFilled(boxLo, boxHi, fill, radius);
    if (checked) {
        // White check: two thick segments.
        const ImVec2 c1(boxLo.x + boxSize * 0.22f, boxLo.y + boxSize * 0.52f);
        const ImVec2 c2(boxLo.x + boxSize * 0.44f, boxLo.y + boxSize * 0.74f);
        const ImVec2 c3(boxLo.x + boxSize * 0.80f, boxLo.y + boxSize * 0.28f);
        dl->AddPolyline(std::initializer_list<ImVec2>{c1, c2, c3}.begin(), 3, IM_COL32(255, 255, 255, 255),
            0, 6.0f * s);
    } else {
        dl->AddRect(boxLo, boxHi, kDivider, radius, 0, 2.0f * s);
    }
    dl->AddText(font, fontSize, ImVec2(boxHi.x + gap, boxLo.y + (boxSize - textSize.y) * 0.5f), kBodyText, label);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + boxSize + 8.0f * s));
    return checked;
}

bool stepper(const char* id, float* value, const std::vector<float>& deltas, const char* fmt, float rowWidth)
{
    if (value == nullptr || deltas.empty()) {
        return false;
    }
    const float s = scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = game::bodyFont();
    const float fontSize = 28.0f * s;
    const float btnW = 104.0f * s;
    const float btnH = 62.0f * s;
    const float pillW = 132.0f * s;
    const float gap = 14.0f * s;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = rowWidth > 0.0f ? rowWidth : ImGui::GetContentRegionAvail().x;
    const float y = pos.y;
    ImGui::Dummy(ImVec2(rowW, btnH + 8.0f * s)); // reserve the row
    ImGui::PushID(id);

    bool changed = false;
    auto drawCapsule = [&](const char* label, float cx, float w, ImU32 fill, ImU32 textColor) {
        const ImVec2 lo(cx, y);
        const ImVec2 hi(cx + w, y + btnH);
        ImGui::SetCursorScreenPos(lo);
        ImGui::InvisibleButton(label, ImVec2(w, btnH));
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        ImU32 f = fill;
        if (held) {
            f = IM_COL32(214, 214, 228, 255);
        } else if (hovered) {
            f = IM_COL32(240, 240, 247, 255);
        }
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
    for (float d : neg) {
        char label[16];
        std::snprintf(label, sizeof(label), "%+g", d);
        if (drawCapsule(label, cx, btnW, kWhiteBtn, kBtnText)) {
            *value += d;
            changed = true;
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
        std::snprintf(valueText, sizeof(valueText), fmt, *value);
        const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, valueText);
        dl->AddText(font, fontSize, ImVec2((lo.x + hi.x - ts.x) * 0.5f, (lo.y + hi.y - ts.y) * 0.5f),
            kBtnText, valueText);
        cx += pillW + gap;
    }
    for (float d : posD) {
        char label[16];
        std::snprintf(label, sizeof(label), "%+g", d);
        if (drawCapsule(label, cx, btnW, kWhiteBtn, kBtnText)) {
            *value += d;
            changed = true;
        }
        cx += btnW + gap;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + btnH + 8.0f * s));
    return changed;
}

int messageDialog(platform::Renderer& renderer, const char* id, const char* title,
    const std::vector<std::string>& buttons, const std::vector<bool>& primary)
{
    const float s = scale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float cardW = 500.0f * s;
    const float cardH = 200.0f * s;
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
    const float btnH = kCapsuleH * s * 0.72f;
    const float btnW = 160.0f * s;
    const float gap = 22.0f * s;
    const float totalW = static_cast<float>(buttons.size()) * btnW + (static_cast<float>(buttons.size()) - 1) * gap;
    float x = center.x - totalW * 0.5f;
    const float y = center.y + size.y * 0.5f - btnH - 24.0f * s;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const bool isPrimary = i < primary.size() && primary[i];
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (capsuleButton(buttons[i].c_str(), ImVec2(btnW, btnH), isPrimary)) {
            result = static_cast<int>(i);
            st.open = false; // button dismisses the dialog
        }
        x += btnW + gap;
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

} // namespace ui
