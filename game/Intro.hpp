// CppSekai - opening intro card (album jacket + metadata).
// 1:1 port of the intro overlay in sekai-mmw-preview-web (AGPL-3.0):
//   core/native/src/mmw_overlay_player.cpp
//     buildIntroCardState / introCardAlpha / openingPlayfieldVisibility
//     loadIntroFonts / drawOpeningIntroOverlay
// The canvas there is 1920x1080, which we keep as a virtual space here.
#pragma once

#include "platform/Renderer.hpp"

#include "imgui.h"

#include <string>

namespace game
{

// Upstream timing (mmw_overlay_player.cpp).
constexpr float kHudIntroDurationSec = 4.0f;
constexpr float kIntroCleanBgDurationSec = 0.0f;
constexpr float kIntroPlayfieldFadeInSec = 1.8f;

// Minimum lead-in that still shows the whole card plus the playfield fade.
constexpr float kMinLeadInSec = kHudIntroDurationSec + kIntroPlayfieldFadeInSec;

struct IntroInfo
{
    bool hasContent = false;
    bool hasCover = false;
    std::string title;
    std::string description1; // 作詞 / 作曲 / 編曲
    std::string description2; // Vo.
    std::string difficulty;   // EASY..MASTER / APPEND / ETERNAL
};

// Everything the card shows. lyricist/composer/arranger/vocal are not part of
// the SUS format - they come from the command line or a sidecar JSON.
struct IntroMetadata
{
    std::string title;
    std::string lyricist;
    std::string composer;
    std::string arranger;
    std::string vocal;
    std::string difficulty;
    std::string susPath; // used to infer the difficulty from the file name
};

// Loads the UI fonts into the ImGui atlas. With preferSystemFont (the
// default) the OS UI font is used so the game follows the system; otherwise
// the bundled pjsk faces from assets/mmw/font are loaded.
// Must run after ImGui::CreateContext() and before the first frame.
void loadIntroFonts(const std::string& fontDir, bool preferSystemFont = true);

// Fonts from loadIntroFonts(), reused by other UI (song list etc.) so CJK
// titles render instead of tofu.
ImFont* titleFont();
ImFont* bodyFont();
ImFont* difficultyFont();

// Builds the card content. Mirrors buildIntroCardState() upstream, including
// the 0..6 difficulty codes and the file-name fallback.
IntroInfo buildIntroInfo(const IntroMetadata& metadata, bool hasCover);

// Draws the opening card. outputTimeSec is the time since the session
// started (0 = first frame).
void drawIntro(platform::Renderer& renderer, const IntroInfo& info, float outputTimeSec,
    int windowW, int windowH);

// Upstream introCardAlpha(): 1 while the card is up, fading over the last
// INTRO_EXIT_FADE_SEC.
float introCardAlpha(float outputTimeSec, bool hasIntroContent);

// Upstream openingPlayfieldVisibility(): 0 while the card covers the stage,
// then fades in over INTRO_PLAYFIELD_FADE_IN_SEC.
float openingPlayfieldVisibility(float outputTimeSec, bool hasIntroContent);

} // namespace game
