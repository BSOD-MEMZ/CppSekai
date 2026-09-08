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

    ImGui::SetCursorScreenPos(ImVec2(center.x - cardW * 0.5f + 40.0f * s, center.y - cardH * 0.5f + 70.0f * s));
    ImGui::BeginGroup();
    caption(title, 34.0f * s, kTitleText, cardW - 80.0f * s);
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
