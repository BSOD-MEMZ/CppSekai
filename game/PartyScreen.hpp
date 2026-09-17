// CppSekai - the multiplayer room screens.
//   drawPartyBadge  - the small "who is in the room" pill on the song select
//   drawPartyScreen - the room itself: song card, difficulty picker, player
//                     list, ready / start / back, charge countdown
//   drawPartyScores - the other players' score / combo while a live runs
// Everything is laid out on a 1920x1080 virtual canvas (the same convention as
// the song select and the result screen) so two windows of different sizes
// show the same picture; see platform/Party.hpp for the transport.
#pragma once

#include "imgui.h"
#include "platform/Party.hpp"

#include <string>
#include <vector>

namespace game
{

// One difficulty the locked song offers, as found in this window's own chart
// list. `index` is the canonical order (0 EASY .. 6 ETERNAL) so two windows
// name a difficulty the same way even if one of them is missing a chart file.
struct PartyDifficultyOption
{
    int index = -1;
    std::string name; // "EXPERT"
    int level = 0;    // official level, 0 = unknown
    int entryIndex = -1; // index into the song-select entries
};

int difficultyIndex(const std::string& name); // -1 when unknown
const char* difficultyName(int index);        // "" when unknown

struct PartyScreenInput
{
    int windowW = 0;
    int windowH = 0;
    float uiScale = 1.0f;
    std::vector<platform::PartyPlayer> players;
    int mySlot = -1;
    bool host = false;
    std::string title;
    std::string songKey;
    std::string artist;
    std::string hostDifficultyName; // what the host picked, for the header line
    std::vector<PartyDifficultyOption> options;
    int myDifficulty = -1; // canonical index, -1 = not picked
    bool ready = false;
    int phase = platform::PartyLobby;
    // Seconds until the live starts while charging (< 0 = not charging).
    float countdownSec = -1.0f;
    std::string status; // status line under the buttons
    ImTextureID cover = 0;
};

struct PartyScreenOutput
{
    int difficulty = -1;  // the player picked this difficulty index
    bool readyToggle = false;
    bool startNow = false; // host: force the charge even if somebody is idle
    bool leave = false;    // back to the song select
};

PartyScreenOutput drawPartyScreen(const PartyScreenInput& in);

// Compact room pill for the song select (display only). `hint` is appended in
// a smaller, dimmer font (e.g. "按 F2 切换多人模式").
void drawPartyBadge(const std::vector<platform::PartyPlayer>& players, int mySlot, int windowW,
    int windowH, float uiScale, const std::string& hint);

// Others' live scoreboard, drawn over the playfield.
void drawPartyScores(const std::vector<platform::PartyPlayer>& players, int mySlot, int windowW,
    int windowH, const std::string& note, bool paused);

} // namespace game
