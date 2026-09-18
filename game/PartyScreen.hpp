// CppSekai - the multiplayer overlays.
//   drawPartyBadge  - the small "who is in the room" pill on the song select
//   drawPartyScores - the other players' score / combo while a live runs
//
// The room itself has no screen of its own: it lives on the song select (the
// host owns the list, a member's list is read-only and picks its difficulty in
// the phone panel that is already there, see game/SongSelect.hpp). What is left
// here is the chrome around it.
//
// Everything is laid out on a 1920x1080 virtual canvas (the same convention as
// the song select and the result screen) so two windows of different sizes show
// the same picture; see platform/Party.hpp for the transport.
#pragma once

#include "imgui.h"
#include "platform/Party.hpp"

#include <string>
#include <vector>

namespace game
{

// Canonical difficulty index, shared by the room protocol and the phone panel:
// 0 EASY .. 4 MASTER, 5 APPEND, 6 ETERNAL. The song select can only offer the
// first five (its chart grouping has no slot for the other two).
int difficultyIndex(const std::string& name); // -1 when unknown
const char* difficultyName(int index);        // "" when unknown

// Compact room pill for the song select (display only). `hint` is appended in
// a smaller, dimmer font (e.g. "按 F2 切换多人模式").
void drawPartyBadge(const std::vector<platform::PartyPlayer>& players, int mySlot, int windowW,
    int windowH, float uiScale, const std::string& hint);

// Others' live scoreboard, drawn over the playfield.
void drawPartyScores(const std::vector<platform::PartyPlayer>& players, int mySlot, int windowW,
    int windowH, const std::string& note, bool paused);

} // namespace game
