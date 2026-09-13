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
    std::string kana;             // official reading (musics.json), may be empty
    std::string mv;               // "2D" / "3D" MV tag from the sidecar (optional)
    std::string displayName; // fallback label when the chart has no #TITLE

    // Music id parsed from the file name ("0075_master.sus" -> 75). unipjsk
    // names every chart that way; 0 when the name has no leading number.
    int musicId = 0;

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
    // Highest score reached, used by the result screen for 最高得分 / 新纪录!.
    // Kept alongside the flags in the same file, so old saves stay valid.
    double bestScore = 0.0;
};

// Settings + play results are persisted together in one userdata.json; see
// UserSettings / loadUserData below.

// Everything the player keeps between sessions. Written next to the charts so
// it survives a rebuilt build/ directory and travels with them: the score keys
// are chart *file names*, so the same charts re-downloaded anywhere match.
struct UserSettings
{
    float noteSpeed = 8.0f;
    float seVolume = 0.8f;
    double offsetSec = 0.0; // audio offset; the UI shows it in ms
    double leadInSec = 6.0;
    int windowMode = 1; // 0=borderless 1=windowed 2=fullscreen
    // Windowed/borderless resolution (fullscreen always uses the desktop size).
    int windowWidth = 1366;
    int windowHeight = 768;
    int fpsLimit = 60;  // 0 = vsync only; >refresh rate auto-disables vsync
    // Subtle playback progress bar along the top edge of the play screen.
    bool showProgressBar = true;
    // Hide the Windows touch ripple over our window (per-window setting).
    bool hideTouchFeedback = true;
    float perfectMs = 40.0f;
    float greatMs = 90.0f;
    float goodMs = 140.0f;
    bool strictFlick = true;
    // Autoplay chart preview (all-PERFECT run, AUTO judge text, no records).
    bool autoplay = false;
    // Pause the run when the window loses keyboard focus (alt-tab, a popup
    // stealing focus). Off = the song keeps running in the background.
    bool autoPauseOnBlur = true;
    // Report the current song to Windows (SMTC: the volume flyout / taskbar
    // media widget). Off = nothing is announced, so a media widget showing
    // something else is left alone.
    bool reportSmtc = true;
    // Splash style: 0 = static centered image (assets\splashscreen.png,
    // default), 1 = classic dark screen with title + progress bar.
    int splashStyle = 0;
    // Song-select background: 0 = the built-in gradient, 1 = the user's
    // Windows desktop wallpaper (blurred) so the screen matches the desktop.
    int bgStyle = 0;
    float bgBlur = 0.5f; // 0..1 blur amount for the wallpaper
    float bgDim = 0.45f; // 0..1 darkening on top of it (keeps the list readable)
    // Song-list order / grouping, so the list comes back the way it was left
    // (see game::drawSongSelect, which owns the two values while it runs).
    int sortMode = 0;  // 0 = by name, 1 = by difficulty
    // 0 = off, 1 = by level band, 2 = by reading (aiueo row: あ か さ た ...,
    // latin letters one section each), 3 = by first character (one section per
    // kana).
    int groupMode = 0;
};

// Path of userdata.json: <exeDir>\.. \userdata.json when a charts\ folder sits
// there (the usual build/ layout - so the file lands next to charts/ and
// survives wiping build/), otherwise <exeDir>\userdata.json for a packaged
// build. exeDir must end with a path separator.
std::string userDataPath(const std::string& exeDir);

// Reads settings + scores. A legacy flat scores.json (a bare map, no
// "settings"/"scores" wrapper) is still accepted, so an old file migrates.
// Missing keys keep the defaults already in `settings`.
void loadUserData(const std::string& path, UserSettings& settings,
    std::map<std::string, ScoreRecord>& scores);

void saveUserData(const std::string& path, const UserSettings& settings,
    const std::map<std::string, ScoreRecord>& scores);

// Records the result of one chart (merges with the existing record) and
// returns the merged record. `score` only ever raises the stored best.
ScoreRecord mergeScore(const ScoreRecord& old, bool cleared, bool fullCombo, double score = 0.0);

// Key used in scores.json: the chart's file name (e.g. "0075_master.sus").
std::string scoreKey(const ChartEntry& entry);

// Copies cleared / fullCombo from the map into the entries.
void applyScores(std::vector<ChartEntry>& entries, const std::map<std::string, ScoreRecord>& scores);

// Assets root (the directory that contains the "select" subfolder, i.e.
// <exeDir>\assets). The select UI PNGs (shuffle / settings buttons, the phone
// frame and the clear indicators) load from <dir>\select\<name>.png.
// Call once at startup.
void setSelectAssetDir(const std::string& dir);

// Optional song-select backdrop (the blurred desktop wallpaper). `texture` 0
// falls back to the built-in gradient. texW/texH are the texture's pixel size
// (used to cover the screen without distorting it) and `dim` (0..1) is a dark
// overlay drawn on top so the white text stays readable. The host owns the
// texture and calls this again whenever the setting changes.
void setSelectBackdrop(GLuint texture, int texW, int texH, float dim);

// Official per-difficulty levels, keyed by song id. unipjsk exports have their
// SUS header stripped (no #TITLE / #PLAYLEVEL, "#DIFFICULTY 0"), so the level
// has to come from the game's own data. Two formats are accepted:
//   { "75": [6, 13, 17, 23, 28], ... }   // easy, normal, hard, expert, master
//   [ { "musicId": 75, "musicDifficulty": "master", "playLevel": 28 }, ... ]
// The second is the game's own musicDifficulties.json verbatim, so it can be
// dropped in unchanged. May be called more than once; later entries win.
void loadMusicLevels(const std::string& path);

// Level of `difficulty` ("EASY".."MASTER") for a song id, 0 when unknown.
int musicLevel(int musicId, const std::string& difficulty);

// Official song readings, for "sort by name" and the aiueo grouping. Accepts
// the game's own musics.json (an array of objects with "id" and
// "pronunciation", the fields unipjsk ships) or a plain {"75": "よんぴき..."}
// map. Songs without an entry fall back to their title. May be called more
// than once; later entries win.
void loadMusicPronunciations(const std::string& path);

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
// `sortMode` / `groupMode` are in/out: the list's sort and grouping live in the
// settings, so they survive a restart (the caller persists them when they
// change).
int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec, int& sortMode, int& groupMode);

} // namespace game
