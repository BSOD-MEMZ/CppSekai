// CppSekai - song select screen.
// Scans a folder for SUS charts and pairs each one with the BGM / jacket
// that sits next to it, then draws a pjsk-ish list + detail panel.
#pragma once

#include "platform/Renderer.hpp"

#include <map>
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
    std::string mv;               // "2D" / "3D" MV tag from the sidecar (optional)
    std::string displayName; // fallback label when the chart has no #TITLE

    // Seconds of silence at the head of the BGM (pjsk's fillerSec). Chart time
    // 0 sits after it, so playback starts from this position in the file.
    // Set from the sidecar JSON ("fillerSec" / "offset"), otherwise detected.
    double audioStartSec = 0.0;

    // Best result for this chart, loaded from scores.json next to the exe.
    bool cleared = false;
    bool fullCombo = false;
};

// Persisted play results, keyed by the chart's file name (e.g.
// "0075_master.sus"). Cleared = the song was played to the end;
// fullCombo = cleared with no MISS.
struct ScoreRecord
{
    bool cleared = false;
    bool fullCombo = false;
};

// scores.json lives next to the exe (pass baseDir + "scores.json").
std::map<std::string, ScoreRecord> loadScores(const std::string& path);
void saveScores(const std::string& path, const std::map<std::string, ScoreRecord>& scores);

// Records the result of one chart (merges with the existing record) and
// returns the merged record.
ScoreRecord mergeScore(const ScoreRecord& old, bool cleared, bool fullCombo);

// Key used in scores.json: the chart's file name (e.g. "0075_master.sus").
std::string scoreKey(const ChartEntry& entry);

// Copies cleared / fullCombo from the map into the entries.
void applyScores(std::vector<ChartEntry>& entries, const std::map<std::string, ScoreRecord>& scores);

// Directory that holds assets/select/*.png (the shuffle / settings buttons,
// the phone frame and the clear indicators). Call once at startup.
void setSelectAssetDir(const std::string& dir);

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
    SelectSettings = -4, // the musicsetting button was pressed (open settings)
};

// Draws the screen. `selected` is kept between frames; returns the index of
// the chart to start, SelectNone, or SelectQuit.
int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec);

} // namespace game
