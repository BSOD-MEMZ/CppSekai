// CppSekai - song select screen.
// Scans a folder for SUS charts and pairs each one with the BGM / jacket
// that sits next to it, then draws a pjsk-ish list + detail panel.
#pragma once

#include "platform/Renderer.hpp"

#include "imgui.h" // ImVec2 in drawSongSelect's out-parameter

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

    // Best result for this chart, loaded from userdata.json next to the exe.
    bool cleared = false;
    bool fullCombo = false;
    // Highest score reached on this chart, 0 when it was never cleared. The
    // song select turns it into the score-rank badge next to the song info.
    double bestScore = 0.0;
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

// ---------------------------------------------------------------------------
// Account: a local, offline profile plus the pjsk player rank.
//
// Nothing here is sent anywhere - it is a player card for the UI. The name and
// the school/organisation are deliberately *not* on screen during normal play:
// the only thing that is always visible is the level chip (top right of the
// song select and on the result screen). The profile itself shows up on demand
// (clicking the chip) and in 设置 -> 账户.
struct AccountData
{
    std::string name; // 昵称
    std::string org;  // 学校 / 组织
    std::string note; // 个性签名

    int rank = 1;
    // Exp banked towards the *next* rank, i.e. addPlayerExp() already rolled
    // the excess over. rank 1 / exp 0 is a fresh account.
    double exp = 0.0;
    int plays = 0;             // runs played to the end (no autoplay)
    double totalScore = 0.0;   // sum of those runs' scores
};

// ---- Player rank ----------------------------------------------------------
// Curve taken from the official game (Sekaipedia "Player Rank"). There, a live
// grants `m_score * m_bonus`, and the exp a rank needs grows in bands. We have
// no live-bonus system, so a run grants exactly the score-rank multiplier and
// the curve is used as-is.
//
// Exp needed to go from `rank` to `rank + 1`. Bands: rank 1 is the tutorial
// hand-out, then a flat 8010, +500 per rank up to 12, +1000 up to 15 and +480
// from there on. Above kMaxPlayerRank the cap holds (0 = no next rank).
constexpr int kMaxPlayerRank = 900;
double expToNextRank(int rank);

// Score rank ('d' / 'c' / 'b' / 'a' / 's', as produced by
// game::scoreRankAndBar) -> the exp an official live would grant for it.
int scoreRankExp(char rank);

// Banks `amount` exp and rolls the rank over as often as it needs to.
// Returns the number of ranks gained (0 = none).
int addPlayerExp(AccountData& account, double amount);

// Settings + play results are persisted together in one userdata.json; see
// UserSettings / loadUserData below.

// Everything the player keeps between sessions. Written next to the charts so
// it survives a rebuilt build/ directory and travels with them: the score keys
// are chart *file names*, so the same charts re-downloaded anywhere match.
struct UserSettings
{
    float noteSpeed = 8.0f;
    float seVolume = 0.8f;   // hit / UI sound effects
    float bgmVolume = 1.0f;  // music: chart track, select preview, result BGM
    double offsetSec = 0.0; // audio offset; the UI shows it in ms
    double leadInSec = 6.0;
    int windowMode = 1; // 0=borderless 1=windowed 2=fullscreen
    // Windowed/borderless resolution. Also the *render* resolution when
    // renderScale is 1 (then it is what the game draws at, whatever the window
    // size is). Fullscreen always uses the desktop size for the window, but in
    // fixed mode the render size is still this.
    int windowWidth = 1366;
    int windowHeight = 768;
    int fpsLimit = 60;  // 0 = vsync only; >refresh rate auto-disables vsync
    // Render size. 0 = the window size *is* the render size (dragging the
    // window relayouts the lanes and the HUD), 1 = always render at
    // windowWidth x windowHeight and scale that picture into the window with
    // the aspect ratio kept (letterbox) - dragging then only zooms the picture,
    // it never changes the layout.
    int renderScale = 0;
    // Subtle playback progress bar along the top edge of the play screen.
    bool showProgressBar = true;
    // Hide the Windows touch ripple over our window (per-window setting).
    bool hideTouchFeedback = true;
    float perfectMs = 40.0f;
    float greatMs = 90.0f;
    float goodMs = 140.0f;
    // Life a run starts with (100..1000; kMaxLife is 1000 and the HUD bar measures
    // against it, so a lower value simply starts the bar part-filled). Pushed to
    // the judgement engine, which seeds its stats from it on every reset.
    float initialLife = 1000.0f;
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
    // 1 is the default: a fresh install comes up with the desktop's own
    // picture behind the list (a missing wallpaper falls back to the gradient).
    int bgStyle = 1;
    float bgBlur = 0.5f; // 0..1 blur amount for the wallpaper
    float bgDim = 0.45f; // 0..1 darkening on top of it (keeps the list readable)
    // UI scale for the two screens laid out on a virtual canvas - song select and
    // the result screen (1.0 = fit the window). The play screen and the HUD
    // deliberately ignore it: those are played, not read, and a mis-scaled lane
    // would be worse than a small one.
    float uiScale = 1.0f;
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

// ---------------------------------------------------------------------------
// Local profiles (multi-user). Each user keeps their own settings / scores /
// account in <dataDir>\profiles\<id>.json; <dataDir>\profiles\index.json lists
// the users and which one is active. The first run copies the old single
// <dataDir>\userdata.json into the "default" profile, so nothing is lost.
// ---------------------------------------------------------------------------
struct UserProfile
{
    std::string id;   // file-name safe, unique
    std::string name; // what the UI shows
};

// Folder that holds userdata.json / profiles/ - i.e. dirname(userDataPath()).
std::string userDataDir(const std::string& exeDir);

// Reads the profile list, creating it (and importing userdata.json) when it is
// missing. `active` receives the id of the active profile and is never left
// empty.
std::vector<UserProfile> loadProfiles(const std::string& dataDir, std::string& active);

void saveProfiles(const std::string& dataDir, const std::vector<UserProfile>& profiles,
    const std::string& active);

// Path of one profile's data file.
std::string profileDataPath(const std::string& dataDir, const std::string& id);

// A free profile id derived from `name` ("初音" -> "user<hash>", "xxt" -> "xxt",
// a second "xxt" -> "xxt2").
std::string makeProfileId(const std::string& name, const std::vector<UserProfile>& existing);

// Reads settings + scores + account. A legacy flat scores.json (a bare map, no
// "settings"/"scores" wrapper) is still accepted, so an old file migrates.
// Missing keys keep the defaults already in `settings` / `account` - which is
// also what an existing userdata.json (written before the account existed)
// ends up as: defaults, no error.
void loadUserData(const std::string& path, UserSettings& settings,
    std::map<std::string, ScoreRecord>& scores, AccountData& account);

void saveUserData(const std::string& path, const UserSettings& settings,
    const std::map<std::string, ScoreRecord>& scores, const AccountData& account);

// Records the result of one chart (merges with the existing record) and
// returns the merged record. `score` only ever raises the stored best.
ScoreRecord mergeScore(const ScoreRecord& old, bool cleared, bool fullCombo, double score = 0.0);

// One vocal version of a song (the official musicVocals table). `asset` is the
// game's assetbundleName, which is also the audio file stem next to the chart:
// charts\se_0374_01.mp3 etc. (see music-vocals.json / CHARTS.md).
struct VocalVersion
{
    int id = 0;
    std::string type;     // "sekai" / "virtual_singer" / "another_vocal" / ...
    std::string caption;  // "セカイver." - the official label
    std::string asset;    // assetbundleName, e.g. "an_0374_02"
    std::vector<std::string> singers;
    // The version's audio file exists next to the chart. Only filled in by
    // availableVocals(); the table itself does not know about files.
    bool available = false;
};

// Vocal versions of a song, in official order (empty when unknown).
const std::vector<VocalVersion>& musicVocals(int musicId);

// Same, but annotated with `available` - and only the versions that actually
// have their mp3 next to the chart. This is what the song select shows: no
// file means no chip, so a song with a single <id4>.mp3 download keeps looking
// exactly like before.
std::vector<VocalVersion> availableVocals(const ChartEntry& entry);

// Short label for a chip in the song select ("セカイ" / "バーチャル" / "みのり").
std::string vocalShortLabel(const VocalVersion& version);

// Index of the version to start with: the セカイver when there is one (that is
// also what the legacy <id4>.mp3 download is), else the first.
int defaultVocalIndex(int musicId, const std::vector<VocalVersion>& available);

// Points `entry` at a version's audio (and its singers, for the "Vo." line).
// Returns false when that version has no file, in which case entry is left with
// whatever BGM the chart scan found.
bool applyVocalVersion(ChartEntry& entry, const std::vector<VocalVersion>& versions, int index);

// Absolute path of a version's mp3 (empty when it is not on disk). Used by the
// song select's music preview, which has to follow the chosen chip without
// mutating the (const) entry the list owns.
std::string vocalAudioPath(const ChartEntry& entry, int index);

// Gives `entry` a usable BGM when the plain chart scan found none: charts that
// ship only official vocal files (se_0374_01.mp3 / vs_0374_01.mp3 ...) have no
// <id4>.mp3 for findSidecar() to pick, and used to come out silent - including
// in the song select preview. Picks the セカイver like the version switcher's
// default. Does nothing when the entry already has a BGM.
void applyDefaultVocal(ChartEntry& entry);

// Official vocal table (music-vocals.json, generated by
// .workbuddy/tools/gen_music_vocals.py). Optional: without it the song select
// simply offers no version switcher.
void loadMusicVocals(const std::string& path);

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

// pjsk difficulty colour (EASY green / NORMAL blue / HARD orange / EXPERT red /
// MASTER purple / APPEND / ETERNAL), with `alpha`. Unknown difficulties fall
// back to a neutral slate. Shared by the song select badges and the result
// screen's difficulty capsule, which has to follow the actual difficulty.
ImU32 difficultyColor(const std::string& difficulty, int alpha = 255);

// Official song readings and titles from the game's musics.json: the reading
// drives "sort by name" and the aiueo grouping, the title fills in charts whose
// #TITLE is empty (every unipjsk export). Accepts the verbatim table (an array
// of objects with "id" / "pronunciation" / "title") or a compact map, either
// {"75": "よんぴき..."} (readings only) or
// {"75": {"kana": "...", "title": "..."}}. Songs without an entry fall back to
// whatever the chart itself carries. May be called more than once; later
// entries win.
void loadMusicMaster(const std::string& path);

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
// change). `vocalIndex` is out-only: the index (into availableVocals()) of the
// version currently picked for the selected song, so the caller can point the
// entry at that audio before starting. -1 = the song has no chooser at all.
// `uiScale` scales the whole 1080p canvas this screen is laid out on (1.0 =
// exactly fit the window, see UserSettings::uiScale). `confirmCenter` receives
// the on-screen centre of the 确定 button, which main.cpp uses as the origin of
// the white confirm flash (the button is drawn tilted, so this is the tilted
// position, not the layout one).
int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec, int& sortMode, int& groupMode, int& vocalIndex,
    float uiScale = 1.0f, ImVec2* confirmCenter = nullptr, const AccountData* account = nullptr);

// Debug (`--profile`): force the profile card open. It normally only appears
// when the level chip is clicked, which a --screenshot run cannot do.
void debugOpenProfileCard(bool open);

} // namespace game
