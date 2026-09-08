// CppSekai - pjsk style UI component library (see Ui.hpp).
// Draws everything on ImGui windows with rounded cards + capsule buttons,
// matching the in-game pjsk dialog look (light card, dark backdrop, mint
// primary buttons, dark close X). Text uses the bundled Rodin fonts from
// game/Intro.cpp so CJK renders correctly.
#include "Ui.hpp"

#include "Intro.hpp"
#include "platform/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui
{
namespace
{
    ImU32 withAlpha(ImU32 col, float alpha)
    {
        const int a = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
        return (col & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, a);
    }

    // Card geometry in "design pixels" (720p reference), scaled by scale().
    constexpr float kCardRadius = 28.0f;
    constexpr float kCapsuleH = 78.0f;
    constexpr float kCloseSize = 52.0f;
} // namespace

float scale()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    return std::clamp(display.y / 720.0f, 0.5f, 3.0f);
}

bool beginCard(const char* id, const ImVec2& center, const ImVec2& size, bool showClose, bool dimBackdrop,
    bool* closeClicked)
{
    float s = scale();
    if (closeClicked != nullptr) {
        *closeClicked = false;
    }

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowFocus();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus * 0;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(id, nullptr, flags);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 lo = ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
    const ImVec2 hi = ImVec2(center.x + size.x * 0.5f, center.y + size.y * 0.5f);

    if (dimBackdrop) {
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, kBackdrop);
    }
    // Card + a faint drop shadow like the real dialog.
    dl->AddRectFilled(ImVec2(lo.x + 6.0f * s, lo.y + 10.0f * s), ImVec2(hi.x + 6.0f * s, hi.y + 10.0f * s),
        IM_COL32(40, 40, 60, 40), kCardRadius * s);
    dl->AddRectFilled(lo, hi, kCardBg, kCardRadius * s);

    if (showClose) {
        const ImVec2 closeLo = ImVec2(hi.x - (kCloseSize + 18.0f) * s, lo.y + 18.0f * s);
        const ImVec2 closeHi = ImVec2(hi.x - 18.0f * s, lo.y + (18.0f + kCloseSize) * s);
        ImGui::SetCursorScreenPos(closeLo);
        ImGui::PushID(id);
        ImGui::InvisibleButton("##close", ImVec2(closeHi.x - closeLo.x, closeHi.y - closeLo.y));
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
        // The texture is a dark X on transparent; dim it slightly on hover.
        dl->AddImage(closeTexture(), closeLo, closeHi, ImVec2(0, 0), ImVec2(1, 1),
            withAlpha(IM_COL32(255, 255, 255, 255), hovered ? 0.55f : 1.0f));
        if (closeClicked != nullptr) {
            *closeClicked = clicked;
        }
    }
    return true;
}

void endCard()
{
    ImGui::End();
}

bool capsuleButton(const char* label, const ImVec2& size, bool primary)
{
    const float s = scale();
    ImVec2 lo = ImGui::GetCursorScreenPos();
    ImVec2 hi = ImVec2(lo.x + size.x, lo.y + size.y);
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return false;
    }
    const ImGuiID btnId = ImGui::GetID(label);
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
    (void)btnId;
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
    const float ruleY = pos.y + textSize.y + 14.0f * s;
    dl->AddRectFilled(ImVec2(pos.x, ruleY), ImVec2(pos.x + interiorWidth, ruleY + 2.0f * s), kDivider, 1.0f * s);
    ImGui::Dummy(ImVec2(interiorWidth, ruleY - pos.y + 26.0f * s));
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

    const ImGuiID cbId = ImGui::GetID(label);
    ImGui::SetCursorScreenPos(boxLo);
    ImGui::InvisibleButton(label, ImVec2(groupW, boxSize));
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
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
    const float totalW = pillW + static_cast<float>(deltas.size()) * (btnW + gap) + gap;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowW = rowWidth > 0.0f ? rowWidth : ImGui::GetContentRegionAvail().x;
    float x = pos.x + std::max(0.0f, (rowW - totalW) * 0.5f);
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
        ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
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
    std::sort(neg.begin(), neg.end());                 // -1, -0.1, -0.01
    std::sort(posD.begin(), posD.end(), std::greater<float>()); // +1, +0.1, +0.01

    const float blockW = pillW + static_cast<float>(neg.size() + posD.size()) * (btnW + gap);
    float cx = x + std::max(0.0f, (rowW - blockW) * 0.5f);
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
    return changed;
}

int messageDialog(platform::Renderer& renderer, const char* id, const char* title,
    const std::vector<std::string>& buttons, const std::vector<bool>& primary)
{
    const float s = scale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float cardW = 900.0f * s;
    const float cardH = 400.0f * s;
    const ImVec2 center = ImVec2(display.x * 0.5f, display.y * 0.5f);

    bool closeClicked = false;
    beginCard(id, center, ImVec2(cardW, cardH), true, true, &closeClicked);
    int result = -1;

    ImGui::SetCursorScreenPos(ImVec2(center.x - cardW * 0.5f + 48.0f * s, center.y - cardH * 0.5f + 52.0f * s));
    ImGui::BeginGroup();
    cardTitle(title, cardW - 96.0f * s);
    ImGui::EndGroup();

    // Capsule row, centered, laid out bottom.
    const float btnH = kCapsuleH * s * 0.86f;
    const float btnW = 250.0f * s;
    const float gap = 36.0f * s;
    const float totalW = static_cast<float>(buttons.size()) * btnW + (static_cast<float>(buttons.size()) - 1) * gap;
    float x = center.x - totalW * 0.5f;
    const float y = center.y + cardH * 0.5f - btnH - 56.0f * s;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const bool isPrimary = i < primary.size() && primary[i];
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (capsuleButton(buttons[i].c_str(), ImVec2(btnW, btnH), isPrimary)) {
            result = static_cast<int>(i);
        }
        x += btnW + gap;
    }
    if (closeClicked) {
        result = -2; // X button
    }
    endCard();
    (void)renderer;
    return result;
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
