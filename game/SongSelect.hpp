// CppSekai - song select screen.
// Scans a folder for SUS charts and pairs each one with the BGM / jacket
// that sits next to it, then draws a pjsk-ish list + detail panel.
#pragma once

#include "platform/Renderer.hpp"

#include <string>
#include <vector>

namespace game
{

struct ChartEntry
{
    std::string susPath;
    std::string bgmPath;
    std::string coverPath;
    std::string title;
    std::string artist;
    std::string lyricist;         // from a <stem>.json sidecar (SUS has no field)
    std::string composer;         // sidecar composer, falls back to SUS artist
    std::string arranger;         // sidecar arranger, falls back to SUS designer
    std::string vocal;            // "初音ミク、KAITO" style list
    std::string difficulty;       // EASY..MASTER / APPEND / ETERNAL
    std::string level;
    std::string displayName; // fallback label when the chart has no #TITLE

    // Seconds of silence at the head of the BGM (pjsk's fillerSec). Chart time
    // 0 sits after it, so playback starts from this position in the file.
    // Set from the sidecar JSON ("fillerSec" / "offset"), otherwise detected.
    double audioStartSec = 0.0;
};

// Recursively collects *.sus under dir (bounded depth). Entries are sorted
// by title then file name.
std::vector<ChartEntry> scanChartFolder(const std::string& dir);

// "0075_master" -> "MASTER", "easy" -> "EASY"; empty when nothing matches.
std::string inferDifficulty(const std::string& name);

// Fills in the sidecar paths (bgm / jacket), difficulty and display name for
// a chart we already know about (e.g. one passed on the command line).
void resolveSidecars(ChartEntry& entry);

enum SelectAction
{
    SelectNone = -1,
    SelectQuit = -2,
    SelectRescan = -3,
};

// Draws the screen. `selected` is kept between frames; returns the index of
// the chart to start, SelectNone, or SelectQuit.
int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec);

} // namespace game
