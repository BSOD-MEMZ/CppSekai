// CppSekai - story / tutorial runtime (see STORY-UI.md).
#pragma once

#include "StoryUi.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace platform
{
class Renderer;
}

namespace story
{
// Everything the runtime needs to know about the live game. Main fills these
// in once; the runtime never reaches into main's locals.
struct Host
{
    std::function<bool()> settingsOpen;
    std::function<bool()> chartdlOpen;
    std::function<bool()> chartdlSearch; // the search box has text in it
    std::function<bool()> chartdlSong;   // a row is selected in the list
    std::function<bool()> chartdlDone;   // the downloader reported a finished job
    std::function<bool()> chartdlEasy;   // EASY is ticked in the downloader
    // The player's current 12 lane bindings, already turned into key names
    // ("Z", "Left Ctrl", ...). The keyboard step draws these, never a hard-coded
    // default: the old copy said Z S X D C V to everybody.
    std::function<std::vector<std::string>()> laneKeys;
    std::function<bool()> inPlay;
    std::function<int()> startedDifficulty; // 0 = EASY, -1 unknown
    std::function<void(const std::string&)> command; // "openSettings" / "launchChartdl" ...
    // The player's nickname, for the name step's card: the field starts on the
    // name they already have, and 确定 writes the new one back before the story
    // moves on. Both optional - a headless run without them still shows the
    // card and simply keeps the typed text in memory.
    std::function<std::string()> playerName;
    std::function<void(const std::string&)> commitName;
};

void setHost(Host host);

// Loads the frames, the pictures and assets/story/scripts/tutorial.json.
bool load(platform::Renderer& renderer, const std::string& assetDir, std::string& err);

bool hasScript(const char* id);
bool start(const char* id);
// Debug (--story-step N): jump straight to a step of the running script, so a
// headless --screenshot can photograph one page without playing the whole
// tutorial. Returns false for an out-of-range index.
bool debugJump(int stepIndex);
bool active();
bool suspended();
void setSuspended(bool on);

void update(float dt);
void draw(float windowW, float windowH);

// True while the layer owns the pointer / keyboard. A passthrough step (the
// one that sends the player into the settings card) leaves the rest of the UI
// usable and only draws.
bool blocksInput();

// Gamepad: A advances / confirms the focused option, left-right walks the
// options of a choice step. Mouse and keyboard keep working unchanged.
void padAccept();
void padMove(int delta);
bool passthrough();

// The external-control hint of the current step ("search" / "list" / "easy" /
// "queue"), plus its label. Empty when the step does not point into chartdl;
// main maps the key to a control id and paints platform::guideShow at it.
const std::string& guideKey();
const std::string& guideLabel();

// The script reached its last step (or was skipped). Main marks the tutorial
// as seen and reloads once; the flag self-clears.
bool consumeFinished();

void stop();
} // namespace story
