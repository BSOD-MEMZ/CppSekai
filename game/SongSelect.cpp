// CppSekai - song select screen (see SongSelect.hpp).
#include "SongSelect.hpp"

#include "Hud.hpp"
#include "Intro.hpp"
#include "Ui.hpp"

// UTF-8 <-> fs::path: the narrow side of fs::path is the ANSI code page and
// throws on a file name it cannot represent (that aborted the chart scan).
#include "path_utf8.hpp"
#include "romaji_search.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace game
{
namespace
{
    namespace fs = std::filesystem;

    // See path_utf8.hpp. Every std::string path in this file is UTF-8; these
    // are the only sanctioned ways across the fs::path boundary.
    fs::path toFsPath(const std::string& utf8) { return path_utf8::toPath(utf8); }
    std::string fromFsPath(const fs::path& path) { return path_utf8::fromPath(path); }

    std::string toLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::string trim(std::string value)
    {
        const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        return value;
    }

    std::string toUpper(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return value;
    }

    bool endsWith(const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    // #TITLE / #ARTIST / ... live in the header; stop scanning once the note
    // data starts (first line whose header looks like a data channel).
    std::map<std::string, std::string> readSusHeader(const fs::path& path)
    {
        std::map<std::string, std::string> fields;
        std::ifstream file(path);
        if (!file.is_open()) {
            return fields;
        }
        std::string line;
        int dataLines = 0;
        while (dataLines < 40 && std::getline(file, line)) {
            if (line.empty() || line[0] != '#') {
                continue;
            }
            const size_t space = line.find(' ');
            if (space == std::string::npos) {
                ++dataLines;
                continue;
            }
            std::string key = toUpper(line.substr(1, space - 1));
            std::string value = trim(line.substr(space + 1));
            if (!value.empty() && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            fields[key] = value;
        }
        return fields;
    }

    // "0075_master" -> 75. unipjsk names every score "<musicId>_<difficulty>",
    // so the id is the only way back to the song's official metadata (the SUS
    // files themselves ship with an empty #TITLE / #PLAYLEVEL). Requires at
    // least three leading digits so a chart named after its title is not
    // mistaken for an id.
    int musicIdFromStem(const std::string& stem)
    {
        int value = 0;
        int digits = 0;
        for (const char ch : stem) {
            if (ch < '0' || ch > '9') {
                break;
            }
            value = value * 10 + (ch - '0');
            ++digits;
        }
        return digits >= 3 ? value : 0;
    }

    // Reads an optional <stem>.json sidecar: {"title", "lyricist", "composer",
    // "arranger", "vocal", ...}. SUS has no credit fields, so this is where
    // the intro card's 作詞/作曲/編曲/Vo. line comes from.
    //
    // Falls back to a song-level sidecar named after the music id
    // ("0075.json"), which every difficulty of the same song shares - without
    // it, "0075_easy.sus" would only find its own (usually absent) sidecar and
    // show up as a second, untitled song next to "0075_master.sus".
    std::map<std::string, std::string> readSidecarMetadata(const fs::path& chartPath)
    {
        std::map<std::string, std::string> out;
        std::error_code ec;
        fs::path jsonPath = chartPath;
        jsonPath.replace_extension(".json");
        if (!fs::exists(jsonPath, ec)) {
            const int musicId = musicIdFromStem(fromFsPath(chartPath.stem()));
            if (musicId > 0) {
                char idName[16];
                std::snprintf(idName, sizeof(idName), "%04d.json", musicId);
                const fs::path shared = chartPath.parent_path() / idName;
                if (fs::exists(shared, ec)) {
                    jsonPath = shared;
                }
            }
        }
        if (!fs::exists(jsonPath, ec)) {
            return out;
        }
        std::ifstream file(jsonPath, std::ios::binary);
        if (!file) {
            return out;
        }
        try {
            const nlohmann::json doc = nlohmann::json::parse(file);
            if (!doc.is_object()) {
                return out;
            }
            for (const char* key : {"title", "artist", "lyricist", "composer", "arranger", "vocal", "difficulty", "level", "mv"}) {
                if (doc.contains(key) && doc[key].is_string()) {
                    out[key] = doc[key].get<std::string>();
                }
            }
            // Leading silence of the BGM. "fillerSec" matches the name in the
            // game's musics.json; "offset" (ms) is what the upstream web
            // preview uses. Either one wins over the automatic detection.
            if (doc.contains("fillerSec") && doc["fillerSec"].is_number()) {
                out["fillerSec"] = std::to_string(doc["fillerSec"].get<double>());
            } else if (doc.contains("offset") && doc["offset"].is_number()) {
                out["fillerSec"] = std::to_string(doc["offset"].get<double>() / 1000.0);
            }
        } catch (...) {
            // malformed sidecar: ignore, fall back to file-name metadata
        }
        return out;
    }

    std::string difficultyFromNumber(const std::string& raw)
    {
        static const std::array<const char*, 7> names{"EASY", "NORMAL", "HARD", "EXPERT", "MASTER", "APPEND", "ETERNAL"};
        if (raw.empty()) {
            return {};
        }
        const int index = std::atoi(raw.c_str());
        if (index >= 0 && index < static_cast<int>(names.size()) && (raw == std::to_string(index))) {
            return names[static_cast<size_t>(index)];
        }
        return toUpper(raw);
    }

    std::string difficultyFromName(const std::string& name)
    {
        static const std::array<const char*, 7> names{"ETERNAL", "APPEND", "MASTER", "EXPERT", "HARD", "NORMAL", "EASY"};
        const std::string upper = toUpper(name);
        for (const char* candidate : names) {
            if (upper.find(candidate) != std::string::npos) {
                return candidate;
            }
        }
        return {};
    }

    std::string prettyFileName(const std::string& stem)
    {
        std::string out;
        for (const char ch : stem) {
            out.push_back(ch == '_' || ch == '-' ? ' ' : ch);
        }
        return out;
    }

    bool hasExtension(const std::string& name, const std::vector<std::string>& extensions)
    {
        const std::string lower = toLower(name);
        return std::any_of(extensions.begin(), extensions.end(),
            [&](const std::string& ext) { return endsWith(lower, ext); });
    }

    // Folder listing for findSidecar's keyword fallback, sorted by name so the
    // pick is deterministic.
    //
    // Cached per directory on purpose: the fallback used to walk the whole
    // folder again for *every* chart, which is O(charts x files in folder). It
    // only bites when a chart has no <stem>.<ext> next to it (so the exact-name
    // candidates miss) - but that is the normal case for a chart whose jacket
    // lives under a different name, and it made a 700-chart library take 3.2 s
    // to scan instead of ~0.4 s. Cleared by scanChartFolder() so F5 still sees a
    // chart dropped in while the game is running.
    std::map<std::string, std::vector<std::pair<std::string, std::string>>>& folderListingCache()
    {
        static std::map<std::string, std::vector<std::pair<std::string, std::string>>> cache;
        return cache;
    }

    void clearFolderListingCache()
    {
        folderListingCache().clear();
    }

    const std::vector<std::pair<std::string, std::string>>& folderListing(const fs::path& dir)
    {
        auto& cache = folderListingCache();
        const std::string key = fromFsPath(dir);
        const auto cached = cache.find(key);
        if (cached != cache.end()) {
            return cached->second;
        }
        std::vector<std::pair<std::string, std::string>> files; // (lowercased name, full path)
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            files.emplace_back(toLower(fromFsPath(entry.path().filename())), fromFsPath(entry.path()));
        }
        std::sort(files.begin(), files.end());
        return cache.emplace(key, std::move(files)).first->second;
    }

    // Case-insensitive lookup in that listing. Windows' filesystem is
    // case-insensitive, so comparing lowercased names keeps fs::exists()'s
    // behaviour - without the syscall, which is the whole point.
    const std::string* folderFind(const fs::path& dir, const std::string& lowerName)
    {
        const std::vector<std::pair<std::string, std::string>>& files = folderListing(dir);
        const auto byName = [](const std::pair<std::string, std::string>& a,
                                const std::pair<std::string, std::string>& b) { return a.first < b.first; };
        const auto it = std::lower_bound(
            files.begin(), files.end(), std::make_pair(lowerName, std::string()), byName);
        if (it != files.end() && it->first == lowerName) {
            return &it->second;
        }
        return nullptr;
    }

    // Finds a sidecar file (jacket / bgm) next to the chart: exact stem first,
    // then the stem without a "_master"-style difficulty suffix, then any
    // file in the folder that looks like a jacket. All three steps answer from
    // the cached listing - this runs 2x for every chart (image + audio), and the
    // probes alone were ~12 stat calls per chart.
    std::string findSidecar(const fs::path& chartPath, const std::vector<std::string>& extensions,
        const std::vector<std::string>& keywords)
    {
        const fs::path dir = chartPath.parent_path();
        const std::string stem = fromFsPath(chartPath.stem());

        std::vector<std::string> stems{stem};
        const size_t underscore = stem.rfind('_');
        if (underscore != std::string::npos) {
            stems.push_back(stem.substr(0, underscore));
        }

        for (const std::string& candidate : stems) {
            for (const std::string& ext : extensions) {
                if (const std::string* hit = folderFind(dir, toLower(candidate + ext))) {
                    return *hit;
                }
            }
        }
        if (keywords.empty()) {
            return {};
        }
        for (const auto& file : folderListing(dir)) {
            if (!hasExtension(file.first, extensions)) {
                continue;
            }
            for (const std::string& keyword : keywords) {
                if (file.first.find(keyword) != std::string::npos) {
                    return file.second;
                }
            }
        }
        return {};
    }

    ImU32 difficultyBadgeColor(const std::string& difficulty, int alpha)
    {
        return difficultyColor(difficulty, alpha);
    }

    // -----------------------------------------------------------------
    // Official master table (musics.json): the reading (sorting / grouping)
    // and the title. unipjsk's SUS exports leave #TITLE empty, so the title
    // fallback is what keeps the list from reading "0018 master" everywhere.
    // -----------------------------------------------------------------
    std::map<int, std::string> gMusicKana;
    std::map<int, std::string> gMusicTitles;

    void storeKana(int musicId, const std::string& kana)
    {
        if (musicId > 0 && !kana.empty()) {
            gMusicKana[musicId] = kana;
        }
    }

    void storeTitle(int musicId, const std::string& title)
    {
        if (musicId > 0 && !title.empty()) {
            gMusicTitles[musicId] = title;
        }
    }

    std::string pronunciationFor(int musicId)
    {
        const auto it = gMusicKana.find(musicId);
        return it == gMusicKana.end() ? std::string() : it->second;
    }

    std::string titleFor(int musicId)
    {
        const auto it = gMusicTitles.find(musicId);
        return it == gMusicTitles.end() ? std::string() : it->second;
    }

    // -----------------------------------------------------------------
    // Community song aliases (music-aliases.json, exported from the public
    // HarukiBot API - see .workbuddy/tools/fetch_music_aliases.py). The
    // official table only knows one reading per song; this one carries what
    // people actually type: "tyw" / "梦开始的地方" for Tell Your World,
    // "mmj团歌" for アイドル新鋭隊, "即刻轮回" for いますぐ輪廻. Small-cased
    // alias -> song ids. Lookups are EXACT on purpose: the table is full of
    // two-letter entries ("hs", "kz", "emu") that as substrings would light up
    // half the list at once.
    // -----------------------------------------------------------------
    std::map<std::string, std::vector<int>> gAliasIndex;

    bool aliasMatches(int musicId, const std::string& needleLower)
    {
        const auto it = gAliasIndex.find(needleLower);
        if (it == gAliasIndex.end()) {
            return false;
        }
        const std::vector<int>& ids = it->second;
        return std::find(ids.begin(), ids.end(), musicId) != ids.end();
    }

    // -----------------------------------------------------------------
    // Vocal versions (music-vocals.json, generated from the official
    // musicVocals + gameCharacters tables). Only the *table* lives here; which
    // versions have their mp3 next to the chart is decided per entry by
    // availableVocals().
    // -----------------------------------------------------------------
    std::map<int, std::vector<VocalVersion>> gMusicVocals;

    // Existence check for one asset's audio file. Only ever called for the
    // selected song (a handful of stats per frame), so no caching: a stale
    // cache would hide a file the player just dropped in and pressed F5 for.
    bool audioFileExists(const fs::path& path)
    {
        std::error_code ec;
        return fs::exists(path, ec) && fs::is_regular_file(path, ec);
    }
} // namespace

std::string gSelectAssetDir; // set via setSelectAssetDir()
void setSelectAssetDir(const std::string& dir)
{
    gSelectAssetDir = dir;
}

// Profile card (the account) is open. Module state so --profile can force it
// open in a headless run.
bool gProfileOpen = false;
void debugOpenProfileCard(bool open)
{
    gProfileOpen = open;
}

// ---------------------------------------------------------------------------
// 猜歌 (guess the song): one community alias, four titles, pick the right one.
//
// The alias table is the one piece of *content* this game has that the official
// master data does not - it is what players actually call their songs ("tyw",
// "梦开始的地方", "mmj团歌") - and until now it only ever served as a search
// shortcut. The quiz reuses the same card + capsule chrome as 个人资料.
//
// Question rules, all of them there to keep a question *fair*:
//   * the alias must map to exactly one song (a group name or a series alias
//     has several right answers - guessable, not fair);
//   * at least two bytes long, and not pure digits (the table is full of
//     "hs" / "kz" / "emu", which are no signal at all);
//   * it must not equal the song's own title or reading (that is a freebie);
//   * and there must be no *other* song whose title is that alias, which would
//     make a decoy also correct.
// The pool is built once and cached - the checks below are O(all aliases x all
// titles) and the tables do not change while the game runs.
// ---------------------------------------------------------------------------
struct GuessState
{
    bool open = false;
    std::string alias;
    std::vector<int> options; // 4 song ids; options[answer] is the right one
    int answer = 0;
    int picked = -1; // -1 until the player answers
    int asked = 0;
    int correct = 0;
    int streak = 0;
    int bestStreak = 0;
    int lastAnswerId = 0; // never ask the same song twice in a row
};
GuessState gGuess;
std::vector<std::pair<std::string, int>> gGuessPool;
bool gGuessPoolBuilt = false;

std::mt19937& guessRng()
{
    static std::mt19937 rng(static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    return rng;
}

bool guessAliasIsUsable(const std::string& alias, int musicId)
{
    if (alias.size() < 2) {
        return false;
    }
    if (std::all_of(alias.begin(), alias.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    const std::string title = toLower(titleFor(musicId));
    if (title.empty() || title == alias) {
        return false;
    }
    if (toLower(pronunciationFor(musicId)) == alias) {
        return false;
    }
    // Someone else's title must not *be* the alias, or that decoy is right too.
    for (const auto& song : gMusicTitles) {
        if (song.first != musicId && toLower(song.second) == alias) {
            return false;
        }
    }
    return true;
}

void buildGuessPool()
{
    gGuessPool.clear();
    gGuessPoolBuilt = true;
    for (const auto& entry : gAliasIndex) {
        if (entry.second.size() != 1) {
            continue;
        }
        const int musicId = entry.second.front();
        if (guessAliasIsUsable(entry.first, musicId)) {
            gGuessPool.emplace_back(entry.first, musicId);
        }
    }
    std::printf("[guess] %zu usable alias(es) of %zu\n", gGuessPool.size(), gAliasIndex.size());
    std::fflush(stdout);
}

// Builds one question. Leaves gGuess.open false when there is nothing to ask
// (no alias table next to the exe, or a pool too small to fill four options).
void newGuessQuestion()
{
    if (!gGuessPoolBuilt) {
        buildGuessPool();
    }
    if (gGuessPool.size() < 4 || gMusicTitles.size() < 4) {
        gGuess.open = false;
        return;
    }
    std::uniform_int_distribution<std::size_t> pick(0, gGuessPool.size() - 1);
    std::size_t chosen = pick(guessRng());
    for (int attempt = 0; attempt < 12 && gGuessPool[chosen].second == gGuess.lastAnswerId;
         ++attempt) {
        chosen = pick(guessRng());
    }
    gGuess.alias = gGuessPool[chosen].first;
    const int answerId = gGuessPool[chosen].second;
    gGuess.lastAnswerId = answerId;

    gGuess.options.clear();
    gGuess.options.push_back(answerId);
    const std::string answerTitle = toLower(titleFor(answerId));
    // Decoys: any other song, as long as the four labels are distinguishable.
    std::vector<int> ids;
    ids.reserve(gMusicTitles.size());
    for (const auto& song : gMusicTitles) {
        if (song.first != answerId) {
            ids.push_back(song.first);
        }
    }
    std::shuffle(ids.begin(), ids.end(), guessRng());
    for (const int id : ids) {
        if (gGuess.options.size() >= 4) {
            break;
        }
        const std::string title = toLower(titleFor(id));
        if (title.empty() || title == answerTitle) {
            continue;
        }
        bool duplicate = false;
        for (const int already : gGuess.options) {
            duplicate = duplicate || toLower(titleFor(already)) == title;
        }
        if (!duplicate) {
            gGuess.options.push_back(id);
        }
    }
    if (gGuess.options.size() < 4) {
        gGuess.open = false;
        return;
    }
    std::shuffle(gGuess.options.begin(), gGuess.options.end(), guessRng());
    gGuess.answer = static_cast<int>(
        std::find(gGuess.options.begin(), gGuess.options.end(), answerId) - gGuess.options.begin());
    gGuess.picked = -1;
    gGuess.open = true;
    // Logged like [aliases]: a question is generated, not read, so a headless
    // check has no other way to see what was asked (or that the four options
    // are actually distinct).
    std::printf("[guess] \"%s\" -> %04d %s |", gGuess.alias.c_str(), answerId,
        titleFor(answerId).c_str());
    for (std::size_t i = 0; i < gGuess.options.size(); ++i) {
        std::printf(" %s%s", i == static_cast<std::size_t>(gGuess.answer) ? "*" : "",
            titleFor(gGuess.options[i]).c_str());
        if (i + 1 < gGuess.options.size()) {
            std::printf(" /");
        }
    }
    std::printf("\n");
    std::fflush(stdout);
}

// Debug (`--guess`): open the quiz at boot, so a --screenshot run can look at
// it without a click to hit.
void debugOpenGuessDialog(bool open)
{
    if (open) {
        newGuessQuestion();
    } else {
        gGuess.open = false;
    }
}

// Blurred desktop wallpaper used as the screen backdrop (0 = draw the built-in
// gradient instead). Owned by the host - see setSelectBackdrop().
GLuint gSelectBackdropTex = 0;
int gSelectBackdropW = 0;
int gSelectBackdropH = 0;
float gSelectBackdropDim = 0.0f;
bool gSelectFillDisabled = false;

void setSelectBackdrop(GLuint texture, int texW, int texH, float dim)
{
    gSelectBackdropTex = texture;
    gSelectBackdropW = texW;
    gSelectBackdropH = texH;
    gSelectBackdropDim = dim;
}

void setSelectTransparentBackground(bool enabled)
{
    gSelectFillDisabled = enabled;
}

ScoreRecord mergeScore(const ScoreRecord& old, bool cleared, bool fullCombo, double score)
{
    ScoreRecord out = old;
    out.cleared = out.cleared || cleared;
    out.fullCombo = out.fullCombo || fullCombo;
    out.bestScore = std::max(out.bestScore, score);
    return out;
}

std::string scoreKey(const ChartEntry& entry)
{
    return fromFsPath(toFsPath(entry.susPath).filename());
}

void applyScores(std::vector<ChartEntry>& entries, const std::map<std::string, ScoreRecord>& scores)
{
    for (ChartEntry& e : entries) {
        const auto it = scores.find(scoreKey(e));
        e.cleared = it != scores.end() && it->second.cleared;
        e.fullCombo = it != scores.end() && it->second.fullCombo;
        e.bestScore = it != scores.end() ? it->second.bestScore : 0.0;
    }
}

std::string userDataPath(const std::string& exeDir)
{
    std::error_code ec;
    const fs::path parent = toFsPath(exeDir) / "..";
    if (fs::exists(parent / "charts", ec)) {
        // build/ layout: keep the file next to charts/ so wiping build/ (or
        // copying the folder to a new machine) does not lose it.
        return fromFsPath((parent / "userdata.json").lexically_normal());
    }
    return fromFsPath(toFsPath(exeDir) / "userdata.json");
}

// ---------------------------------------------------------------------------
// Local profiles (multi-user).
//
// Settings, scores and the account all live in one JSON. With profiles there is
// one such file per user, under <dataDir>\profiles\<id>.json, plus a tiny index
// that lists the names and records which one is active:
//
//   <dataDir>\profiles\index.json   { "active": "<id>", "users": [ {id,name} ] }
//   <dataDir>\profiles\<id>.json    { settings, scores, account }
//
// The first run migrates the old single-file <dataDir>\userdata.json into the
// "default" profile, so an existing install keeps its scores and settings.
// <dataDir> is whatever folder userDataPath() points into (next to charts/ in
// the build layout, next to the exe in a packaged one).
// ---------------------------------------------------------------------------
namespace
{
    std::string profileIdFromName(const std::string& name)
    {
        std::string out;
        for (const char ch : name) {
            const unsigned char byte = static_cast<unsigned char>(ch);
            if (byte >= 0x80) {
                // Non-ASCII (a CJK nickname): keep the hashed digits below
                // honest and skip the byte - the id only has to be unique and
                // file-name safe, the label the user sees is the name itself.
                continue;
            }
            if ((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z')
                || (byte >= 'A' && byte <= 'Z')) {
                out.push_back(static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + 32 : byte));
            } else if (byte == '-' || byte == '_') {
                out.push_back('_');
            }
        }
        // A name that leaves nothing but digits ("用户3") would yield the id "3":
        // a legal file name, but it reads like a stray number in the profile
        // list and in the [instance] log, and collides with the next "…3"-ish
        // name. Hash those as well.
        const bool allDigits = !out.empty()
            && out.find_first_not_of("0123456789") == std::string::npos;
        if (out.empty() || allDigits) {
            // Fall back to a stable number derived from the name, so two
            // Chinese nicknames do not both become "user".
            unsigned hash = 2166136261u;
            for (const char ch : name) {
                hash = (hash ^ static_cast<unsigned char>(ch)) * 16777619u;
            }
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "user%08x", hash);
            out = buffer;
        }
        if (out.size() > 32) {
            out.resize(32);
        }
        return out;
    }
} // namespace

std::string userDataDir(const std::string& exeDir)
{
    const fs::path file = userDataPath(exeDir);
    return fromFsPath(file.parent_path());
}

std::string profileDataPath(const std::string& dataDir, const std::string& id)
{
    return fromFsPath(toFsPath(dataDir) / "profiles" / (id + ".json"));
}

std::string profileIndexPath(const std::string& dataDir)
{
    return fromFsPath(toFsPath(dataDir) / "profiles" / "index.json");
}

std::string makeProfileId(const std::string& name, const std::vector<UserProfile>& existing)
{
    const std::string base = profileIdFromName(name);
    std::string candidate = base;
    for (int suffix = 2; suffix < 1000; ++suffix) {
        const bool taken = std::any_of(existing.begin(), existing.end(),
            [&](const UserProfile& user) { return user.id == candidate; });
        if (!taken) {
            return candidate;
        }
        candidate = base + std::to_string(suffix);
    }
    return base + "999";
}

std::vector<UserProfile> loadProfiles(const std::string& dataDir, std::string& active)
{
    std::vector<UserProfile> profiles;
    std::error_code ec;
    fs::create_directories(toFsPath(dataDir) / "profiles", ec);

    const std::string index = profileIndexPath(dataDir);
    if (fs::exists(index, ec)) {
        std::ifstream file(index, std::ios::binary);
        if (file) {
            try {
                const nlohmann::json doc = nlohmann::json::parse(file);
                if (doc.is_object()) {
                    active = doc.value("active", std::string{});
                    if (doc.contains("users") && doc["users"].is_array()) {
                        for (const auto& row : doc["users"]) {
                            if (!row.is_object()) {
                                continue;
                            }
                            UserProfile user;
                            user.id = row.value("id", std::string{});
                            user.name = row.value("name", std::string{});
                            if (!user.id.empty()) {
                                profiles.push_back(std::move(user));
                            }
                        }
                    }
                }
            } catch (...) {
                // Malformed index: start over from whatever files are there.
            }
        }
    }

    if (profiles.empty()) {
        // First run with profiles: adopt the old single save as "default".
        // Copying (not moving) keeps an older build, still pointed straight at
        // userdata.json, working side by side.
        UserProfile user;
        user.id = "default";
        const std::string legacy = fromFsPath(toFsPath(dataDir) / "userdata.json");
        std::string label;
        if (fs::exists(legacy, ec)) {
            std::ifstream file(legacy, std::ios::binary);
            if (file) {
                try {
                    const nlohmann::json doc = nlohmann::json::parse(file);
                    label = doc.value("account", nlohmann::json::object()).value("name", std::string{});
                } catch (...) {
                }
            }
            std::error_code copyEc;
            fs::copy_file(legacy, profileDataPath(dataDir, user.id),
                fs::copy_options::overwrite_existing, copyEc);
        }
        user.name = label.empty() ? "默认用户" : label;
        profiles.push_back(user);
        active = user.id;
        saveProfiles(dataDir, profiles, active);
    }

    const bool activeOk = std::any_of(profiles.begin(), profiles.end(),
        [&](const UserProfile& user) { return user.id == active; });
    if (!activeOk) {
        active = profiles.front().id;
        saveProfiles(dataDir, profiles, active);
    }
    return profiles;
}

void saveProfiles(const std::string& dataDir, const std::vector<UserProfile>& profiles,
    const std::string& active)
{
    nlohmann::json users = nlohmann::json::array();
    for (const UserProfile& user : profiles) {
        users.push_back({{"id", user.id}, {"name", user.name}});
    }
    nlohmann::json doc;
    doc["active"] = active;
    doc["users"] = users;
    std::error_code ec;
    fs::create_directories(toFsPath(dataDir) / "profiles", ec);
    std::ofstream file(toFsPath(profileIndexPath(dataDir)), std::ios::binary);
    if (file) {
        file << doc.dump(2) << std::endl;
    }
}

double expToNextRank(int rank)
{
    if (rank < 1 || rank >= kMaxPlayerRank) {
        return 0.0; // fresh account / rank cap
    }
    if (rank == 1) {
        return 10.0; // the official table hands the first rank out immediately
    }
    if (rank == 2) {
        return 8010.0;
    }
    if (rank <= 12) {
        return 8000.0 + 500.0 * static_cast<double>(rank - 2);
    }
    if (rank <= 15) {
        return 13000.0 + 1000.0 * static_cast<double>(rank - 12);
    }
    return 16000.0 + 480.0 * static_cast<double>(rank - 15);
}

int scoreRankExp(char rank)
{
    switch (rank) {
    case 's':
        return 320;
    case 'a':
        return 280;
    case 'b':
        return 240;
    case 'c':
        return 200;
    default:
        return 20; // 'd'
    }
}

int addPlayerExp(AccountData& account, double amount)
{
    if (amount <= 0.0) {
        return 0;
    }
    if (account.rank < 1) {
        account.rank = 1;
    }
    account.exp += amount;
    int ups = 0;
    for (;;) {
        const double need = expToNextRank(account.rank);
        if (need <= 0.0 || account.exp < need) {
            break;
        }
        account.exp -= need;
        ++account.rank;
        ++ups;
    }
    return ups;
}

void loadUserData(const std::string& path, UserSettings& settings,
    std::map<std::string, ScoreRecord>& scores, AccountData& account)
{
    std::ifstream file(toFsPath(path), std::ios::binary);
    if (!file) {
        return;
    }
    try {
        const nlohmann::json doc = nlohmann::json::parse(file);
        if (!doc.is_object()) {
            return;
        }
        // Older files are a bare scores map; newer ones wrap {settings, scores}.
        const bool wrapped = doc.contains("settings") || doc.contains("scores");
        const nlohmann::json& scoreDoc = wrapped && doc.contains("scores") ? doc["scores"] : doc;
        if (scoreDoc.is_object()) {
            for (auto it = scoreDoc.begin(); it != scoreDoc.end(); ++it) {
                if (!it.value().is_object()
                    || (!it.value().contains("cleared") && !it.value().contains("fullCombo")
                        && !it.value().contains("bestScore"))) {
                    continue; // not a score record (e.g. the "settings" object)
                }
                ScoreRecord rec;
                rec.cleared = it.value().value("cleared", false);
                rec.fullCombo = it.value().value("fullCombo", false);
                rec.bestScore = it.value().value("bestScore", 0.0);
                scores[it.key()] = rec;
            }
        }
        if (wrapped && doc.contains("settings") && doc["settings"].is_object()) {
            const nlohmann::json& s = doc["settings"];
            settings.noteSpeed = s.value("noteSpeed", settings.noteSpeed);
            settings.seVolume = s.value("seVolume", settings.seVolume);
            settings.bgmVolume = s.value("bgmVolume", settings.bgmVolume);
            settings.offsetSec = s.value("offsetSec", settings.offsetSec);
            settings.leadInSec = s.value("leadInSec", settings.leadInSec);
            settings.windowMode = s.value("windowMode", settings.windowMode);
            settings.windowWidth = s.value("windowWidth", settings.windowWidth);
            settings.windowHeight = s.value("windowHeight", settings.windowHeight);
            settings.renderScale = s.value("renderScale", settings.renderScale);
            settings.instanceMode = s.value("instanceMode", settings.instanceMode);
            // An install that already had multi-open before this key existed
            // must not be asked again: treat "already multi-open" as accepted.
            // (Otherwise the prompt would show up for the very people who are
            // mid-way through a 多人游玩 session.)
            settings.multiInstanceAccepted = s.value("multiInstanceAccepted",
                settings.instanceMode == 1 || settings.multiplayer);
            settings.fpsLimit = s.value("fpsLimit", settings.fpsLimit);
            settings.multiplayer = s.value("multiplayer", settings.multiplayer);
            settings.showProgressBar = s.value("showProgressBar", settings.showProgressBar);
            settings.hideTouchFeedback = s.value("hideTouchFeedback", settings.hideTouchFeedback);
            settings.perfectMs = s.value("perfectMs", settings.perfectMs);
            settings.greatMs = s.value("greatMs", settings.greatMs);
            settings.goodMs = s.value("goodMs", settings.goodMs);
            // badMs / missMs are newer than the rest. Profiles written before
            // they existed carry no such keys, and the old engine derived both
            // from goodMs + 60 - so a missing key must keep deriving, not fall
            // back to the struct default (which would silently tighten the
            // window for everyone who already had a profile).
            settings.badMs = s.value("badMs", -1.0f);
            settings.missMs = s.value("missMs", -1.0f);
            settings.linkBadMiss = s.value("linkBadMiss", settings.linkBadMiss);
            if (settings.badMs < 0.0f) {
                settings.badMs = settings.goodMs + 60.0f;
            }
            if (settings.missMs < 0.0f) {
                settings.missMs = settings.goodMs + 60.0f;
            }
            settings.holdTailGraceMs = s.value("holdTailGraceMs", settings.holdTailGraceMs);
            settings.holdStartGraceMs = s.value("holdStartGraceMs", settings.holdStartGraceMs);
            settings.initialLife = s.value("initialLife", settings.initialLife);
            settings.strictFlick = s.value("strictFlick", settings.strictFlick);
            settings.flickAsTap = s.value("flickAsTap", settings.flickAsTap);
            settings.debugLog = s.value("debugLog", settings.debugLog);
            settings.autoplay = s.value("autoplay", settings.autoplay);
            settings.autoPauseOnBlur = s.value("autoPauseOnBlur", settings.autoPauseOnBlur);
            settings.reportSmtc = s.value("reportSmtc", settings.reportSmtc);
            settings.splashStyle = s.value("splashStyle", settings.splashStyle);
            settings.bgStyle = s.value("bgStyle", settings.bgStyle);
            settings.glassMode = s.value("glassMode", settings.glassMode);
            settings.bgBlur = s.value("bgBlur", settings.bgBlur);
            settings.bgDim = s.value("bgDim", settings.bgDim);
            settings.uiScale = s.value("uiScale", settings.uiScale);
            settings.sortMode = s.value("sortMode", settings.sortMode);
            settings.groupMode = s.value("groupMode", settings.groupMode);
            // Missing in an old profile: the ELUA shows once, which is exactly
            // what an existing install should see after this version lands.
            settings.eulaAccepted = s.value("eulaAccepted", settings.eulaAccepted);
        }
        if (wrapped && doc.contains("account") && doc["account"].is_object()) {
            const nlohmann::json& a = doc["account"];
            account.name = a.value("name", account.name);
            account.org = a.value("org", account.org);
            account.note = a.value("note", account.note);
            account.rank = a.value("rank", account.rank);
            account.exp = a.value("exp", account.exp);
            account.plays = a.value("plays", account.plays);
            account.totalScore = a.value("totalScore", account.totalScore);
        }
    } catch (...) {
        // malformed file: keep the defaults
    }
    // Same ordering rules the settings UI enforces.
    settings.perfectMs = std::clamp(settings.perfectMs, 10.0f, 100.0f);
    settings.greatMs = std::max(settings.greatMs, settings.perfectMs + 10.0f);
    settings.goodMs = std::max(settings.goodMs, settings.greatMs + 10.0f);
    // BAD and MISS both sit outside GOOD. Linked (the default) they are one
    // number; unlinked each keeps its own value, still outside GOOD.
    settings.badMs = std::max(settings.badMs, settings.goodMs + 10.0f);
    settings.missMs = std::max(settings.missMs, settings.goodMs + 10.0f);
    settings.holdTailGraceMs = std::clamp(settings.holdTailGraceMs, 20.0f, 300.0f);
    settings.holdStartGraceMs = std::clamp(settings.holdStartGraceMs, 20.0f, 300.0f);
    // 100 = one MISS from failing, 5000 = the practice-pool maximum. The HUD bar
    // is normalised against the value itself, so it always starts full.
    settings.initialLife = std::clamp(settings.initialLife, 100.0f, 5000.0f);
    settings.windowWidth = std::clamp(settings.windowWidth, 320, 7680);
    settings.windowHeight = std::clamp(settings.windowHeight, 240, 4320);
    settings.renderScale = std::clamp(settings.renderScale, 0, 1);
    settings.instanceMode = std::clamp(settings.instanceMode, 0, 1);
    // 多人游玩 needs several windows by definition, so keep the file honest:
    // main.cpp forces multi-open for such a run anyway, and the settings card
    // greys the checkbox out whenever the instance policy forbids a second
    // window (see 设置 -> 系统).
    if (settings.multiplayer) {
        settings.instanceMode = 1;
    }
    settings.bgStyle = std::clamp(settings.bgStyle, 0, 2);
    settings.glassMode = std::clamp(settings.glassMode, 0, 2);
    if (settings.glassMode == 1) {
        // 1 was "let DWM stop drawing the non-client area"; dropped in 2026-09-19
        // because on Windows 7 it drops Aero and falls back to the Basic frame.
        settings.glassMode = 0;
    }
    settings.bgBlur = std::clamp(settings.bgBlur, 0.0f, 1.0f);
    settings.bgDim = std::clamp(settings.bgDim, 0.0f, 1.0f);
    // Small range on purpose: this zooms the select / result canvas, and past
    // roughly +-50% the screen starts running out of window.
    settings.uiScale = std::clamp(settings.uiScale, 0.7f, 1.5f);
    settings.sortMode = std::clamp(settings.sortMode, 0, 1);
    settings.groupMode = std::clamp(settings.groupMode, 0, 3);

    // Account: a rank past the cap (a hand-edited file) would make
    // expToNextRank() return 0 and freeze the bar, so clamp it. Exp is left
    // alone - addPlayerExp() rolls any excess over on the next run.
    account.rank = std::clamp(account.rank, 1, kMaxPlayerRank);
    account.exp = std::max(account.exp, 0.0);
    account.plays = std::max(account.plays, 0);
}

void saveUserData(const std::string& path, const UserSettings& settings,
    const std::map<std::string, ScoreRecord>& scores, const AccountData& account)
{
    nlohmann::json scoreDoc = nlohmann::json::object();
    for (const auto& [name, rec] : scores) {
        scoreDoc[name] = {{"cleared", rec.cleared}, {"fullCombo", rec.fullCombo},
            {"bestScore", rec.bestScore}};
    }
    nlohmann::json doc;
    doc["settings"] = {
        {"noteSpeed", settings.noteSpeed},
        {"seVolume", settings.seVolume},
        {"bgmVolume", settings.bgmVolume},
        {"offsetSec", settings.offsetSec},
        {"leadInSec", settings.leadInSec},
        {"windowMode", settings.windowMode},
        {"windowWidth", settings.windowWidth},
        {"windowHeight", settings.windowHeight},
        {"renderScale", settings.renderScale},
        {"instanceMode", settings.instanceMode},
        {"multiInstanceAccepted", settings.multiInstanceAccepted},
        {"fpsLimit", settings.fpsLimit},
        {"multiplayer", settings.multiplayer},
        {"showProgressBar", settings.showProgressBar},
        {"hideTouchFeedback", settings.hideTouchFeedback},
        {"perfectMs", settings.perfectMs},
        {"greatMs", settings.greatMs},
        {"goodMs", settings.goodMs},
        {"badMs", settings.badMs},
        {"missMs", settings.missMs},
        {"linkBadMiss", settings.linkBadMiss},
        {"holdTailGraceMs", settings.holdTailGraceMs},
        {"holdStartGraceMs", settings.holdStartGraceMs},
        {"initialLife", settings.initialLife},
        {"strictFlick", settings.strictFlick},
        {"flickAsTap", settings.flickAsTap},
        {"debugLog", settings.debugLog},
        {"autoplay", settings.autoplay},
        {"autoPauseOnBlur", settings.autoPauseOnBlur},
        {"reportSmtc", settings.reportSmtc},
        {"splashStyle", settings.splashStyle},
        {"bgStyle", settings.bgStyle},
        {"glassMode", settings.glassMode},
        {"bgBlur", settings.bgBlur},
        {"bgDim", settings.bgDim},
        {"uiScale", settings.uiScale},
        {"sortMode", settings.sortMode},
        {"groupMode", settings.groupMode},
        {"eulaAccepted", settings.eulaAccepted},
    };
    doc["scores"] = scoreDoc;
    doc["account"] = {
        {"name", account.name},
        {"org", account.org},
        {"note", account.note},
        {"rank", account.rank},
        {"exp", account.exp},
        {"plays", account.plays},
        {"totalScore", account.totalScore},
    };
    std::ofstream file(toFsPath(path), std::ios::binary);
    if (file) {
        file << doc.dump(2) << std::endl;
    }
}

std::string inferDifficulty(const std::string& name)
{
    return difficultyFromName(name);
}

void resolveSidecars(ChartEntry& entry)
{
    const fs::path path = toFsPath(entry.susPath);
    const std::string stem = fromFsPath(path.stem());
    if (entry.musicId <= 0) {
        entry.musicId = musicIdFromStem(stem);
    }
    const std::map<std::string, std::string> sidecar = readSidecarMetadata(path);
    auto sideField = [&](const char* key) -> std::string {
        const auto it = sidecar.find(key);
        return it == sidecar.end() ? std::string{} : it->second;
    };
    if (entry.title.empty()) {
        entry.title = sideField("title");
    }
    if (entry.title.empty()) {
        // unipjsk SUS files ship an empty #TITLE: take the official name from
        // the master table before falling back to the file name.
        entry.title = titleFor(entry.musicId);
    }
    if (entry.title.empty()) {
        entry.title = prettyFileName(stem);
    }
    entry.displayName = entry.title;
    if (entry.artist.empty()) {
        entry.artist = sideField("artist");
    }
    entry.lyricist = sideField("lyricist");
    entry.composer = sideField("composer");
    entry.arranger = sideField("arranger");
    entry.vocal = sideField("vocal");
    if (entry.difficulty.empty()) {
        entry.difficulty = difficultyFromName(stem);
    }
    if (entry.difficulty.empty()) {
        entry.difficulty = difficultyFromNumber(sideField("difficulty"));
    }
    if (entry.level.empty()) {
        entry.level = sideField("level");
    }
    entry.mv = sideField("mv");
    if (entry.coverPath.empty()) {
        entry.coverPath = findSidecar(path, {".png", ".jpg", ".jpeg"}, {"jacket", "cover"});
    }
    if (entry.bgmPath.empty()) {
        entry.bgmPath = findSidecar(path, {".mp3", ".wav", ".ogg", ".flac", ".m4a"}, {});
    }
    // Charts that ship only the official vocal files (se_0374_01.mp3,
    // vs_0374_01.mp3 ...) have nothing for findSidecar() to match, so they
    // used to come out with no BGM at all - silent intro card, silent song
    // select preview. Fall back to the セカイver (the version the legacy
    // <id4>.mp3 download corresponds to).
    applyDefaultVocal(entry);
    if (entry.audioStartSec <= 0.0) {
        entry.audioStartSec = std::atof(sideField("fillerSec").c_str());
    }
}

std::vector<ChartEntry> scanChartFolder(const std::string& dir)
{
    const std::vector<std::string> audioExt{".mp3", ".wav", ".ogg", ".flac", ".m4a"};
    const std::vector<std::string> imageExt{".png", ".jpg", ".jpeg"};

    std::vector<ChartEntry> entries;
    std::error_code ec;
    // A fresh scan has to see the filesystem as it is now (this is what F5 and
    // the refresh button run, and what picks up a chart dropped in mid-session).
    clearFolderListingCache();
    const fs::path root = toFsPath(dir);
    if (dir.empty() || !fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        return entries;
    }

    // Diagnostic (CPSEKAI_SCAN_TIMING=1): where the scan time goes. The header
    // read is one file open per chart and the sidecar lookups are filesystem
    // probes, so a big library wants the split before anyone "optimises" the
    // wrong loop.
    const bool scanTiming = std::getenv("CPSEKAI_SCAN_TIMING") != nullptr;
    const auto nowClock = [] { return std::chrono::steady_clock::now(); };
    const auto msSince = [](const std::chrono::steady_clock::time_point& t0) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    };
    double headerMs = 0.0;
    double metadataMs = 0.0;
    int scanned = 0;

    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const fs::path path = entry.path();
        if (!hasExtension(fromFsPath(path.filename()), {".sus"})) {
            continue;
        }
        ++scanned;

        ChartEntry item;
        item.susPath = fromFsPath(path);
        const std::string stem = fromFsPath(path.stem());
        item.musicId = musicIdFromStem(stem);

        const auto headerT0 = scanTiming ? nowClock() : std::chrono::steady_clock::time_point{};
        const std::map<std::string, std::string> header = readSusHeader(path);
        if (scanTiming) {
            headerMs += msSince(headerT0);
        }
        auto field = [&](const char* key) -> std::string {
            const auto it = header.find(key);
            return it == header.end() ? std::string{} : it->second;
        };
        const auto metaT0 = scanTiming ? nowClock() : std::chrono::steady_clock::time_point{};
        const std::map<std::string, std::string> sidecar = readSidecarMetadata(path);
        if (scanTiming) {
            metadataMs += msSince(metaT0);
        }
        auto sideField = [&](const char* key) -> std::string {
            const auto it = sidecar.find(key);
            return it == sidecar.end() ? std::string{} : it->second;
        };

        item.title = field("TITLE");
        // Some unipjsk exports dump the *difficulty* into #TITLE (charts whose
        // header says "master"): that is not a song name, so treat it as empty
        // and let the file-name fallback below handle it.
        {
            static const char* kDifficultyWords[]
                = {"easy", "normal", "hard", "expert", "master", "append", "eternal"};
            const std::string lowerTitle = toLower(trim(item.title));
            for (const char* word : kDifficultyWords) {
                if (!lowerTitle.empty() && lowerTitle == word) {
                    item.title.clear();
                    break;
                }
            }
        }
        if (item.title.empty()) {
            item.title = sideField("title");
        }
        if (item.title.empty()) {
            item.title = titleFor(item.musicId); // official name (see above)
        }
        item.artist = field("ARTIST");
        if (item.artist.empty()) {
            item.artist = sideField("artist");
        }
        item.lyricist = sideField("lyricist");
        item.composer = sideField("composer");
        item.arranger = sideField("arranger");
        item.vocal = sideField("vocal");
        // File name wins: unipjsk charts always write "#DIFFICULTY 0".
        item.difficulty = difficultyFromName(stem);
        if (item.difficulty.empty()) {
            item.difficulty = difficultyFromNumber(field("DIFFICULTY"));
        }
        if (item.difficulty.empty()) {
            item.difficulty = difficultyFromNumber(sideField("difficulty"));
        }
        item.level = field("PLAYLEVEL");
        if (item.level.empty()) {
            item.level = sideField("level");
        }
        item.mv = sideField("mv");
        item.displayName = item.title.empty() ? prettyFileName(stem) : item.title;

        item.coverPath = findSidecar(path, imageExt, {"jacket", "cover"});
        item.bgmPath = findSidecar(path, audioExt, {});
        item.audioStartSec = std::atof(sideField("fillerSec").c_str());
        // See resolveSidecars(): a chart whose audio is only the official
        // per-version files needs the vocal table to find any BGM.
        applyDefaultVocal(item);

        entries.push_back(item);
    }

    std::sort(entries.begin(), entries.end(), [](const ChartEntry& a, const ChartEntry& b) {
        const std::string left = a.title.empty() ? a.displayName : a.title;
        const std::string right = b.title.empty() ? b.displayName : b.title;
        if (left != right) {
            return left < right;
        }
        return a.difficulty < b.difficulty;
    });
    if (scanTiming) {
        std::printf("[scan] %d chart(s): header %.0f ms, sidecar %.0f ms\n", scanned, headerMs, metadataMs);
        std::fflush(stdout);
    }
    return entries;
}
// ---------------------------------------------------------------------------
// pjsk-style song select: left song list + right phone panel with difficulty
// buttons. Drawn with raw ImDrawList in a 1080p virtual layout scaled by the
// window height; interactions use InvisibleButton hitboxes.
// ---------------------------------------------------------------------------

namespace
{
    constexpr int kDiffCount = 5;
    const char* kDiffNames[kDiffCount] = {"EASY", "NORMAL", "HARD", "EXPERT", "MASTER"};
    const ImU32 kDiffColors[kDiffCount] = {
        IM_COL32(75, 207, 138, 255),   // EASY   green
        IM_COL32(90, 140, 255, 255),   // NORMAL blue
        IM_COL32(242, 150, 77, 255),   // HARD   orange
        IM_COL32(239, 90, 102, 255),   // EXPERT red
        IM_COL32(181, 91, 255, 255),   // MASTER purple
    };

    // One song (grouped by title) and which entry provides each difficulty.
    struct SongGroup
    {
        std::string title;
        std::string artist;
        std::string vocal;
        std::string coverPath;
        std::string kana; // official reading for sorting / grouping
        int musicId = 0; // song id, for the official level table
        int idx[kDiffCount] = {-1, -1, -1, -1, -1};
    };

    int diffIndexOf(const std::string& difficulty)
    {
        if (difficulty.empty()) {
            return -1;
        }
        for (int d = 0; d < kDiffCount; ++d) {
            if (difficulty == kDiffNames[d]) {
                return d;
            }
        }
        return -1;
    }

    // -----------------------------------------------------------------
    // Official per-difficulty levels (see game::loadMusicLevels). unipjsk
    // charts ship with "#DIFFICULTY 0" and an empty "#PLAYLEVEL", so without
    // this table the song select can only show "-" for every level.
    // -----------------------------------------------------------------
    std::map<int, std::array<int, kDiffCount>> gMusicLevels;

    void storeLevel(int musicId, int diffIndex, int level)
    {
        if (musicId <= 0 || diffIndex < 0 || diffIndex >= kDiffCount || level <= 0) {
            return;
        }
        gMusicLevels[musicId][static_cast<size_t>(diffIndex)] = level;
    }

    // "0075_master" -> 75 (see the file-scope musicIdFromStem).
    int tableLevel(int musicId, int diffIndex)
    {
        const auto it = gMusicLevels.find(musicId);
        if (it == gMusicLevels.end() || diffIndex < 0 || diffIndex >= kDiffCount) {
            return 0;
        }
        return it->second[static_cast<size_t>(diffIndex)];
    }

    // The level of one chart: its own #PLAYLEVEL / sidecar first, then the
    // official table for its difficulty. 0 = unknown.
    int resolvedLevel(const ChartEntry& entry)
    {
        const int own = std::atoi(entry.level.c_str());
        if (!entry.level.empty() && own > 0) {
            return own;
        }
        return tableLevel(entry.musicId, diffIndexOf(entry.difficulty));
    }

    // Level shown next to a song in the list: the number of the *currently
    // selected* difficulty, not of whatever chart happens to exist. When the
    // song has no chart for that difficulty the official level table still
    // knows the number, so a missing chart is not a missing level. 0 = "-".
    int levelForDifficulty(const SongGroup& group, const std::vector<ChartEntry>& entries, int diffIndex)
    {
        if (diffIndex < 0 || diffIndex >= kDiffCount) {
            return 0;
        }
        if (group.idx[diffIndex] >= 0) {
            const int own = resolvedLevel(entries[static_cast<size_t>(group.idx[diffIndex])]);
            if (own > 0) {
                return own;
            }
        }
        return tableLevel(group.musicId, diffIndex);
    }

    // -----------------------------------------------------------------
    // Official song readings / titles (musics.json, see the storeKana /
    // storeTitle helpers above): sorted and grouped by the reading, so
    // "ウミユリ海底譚" lands under う / あ行 instead of somewhere random in the
    // code point order, and titled from the master table when the chart file
    // carries no name of its own.
    // -----------------------------------------------------------------

    // Folds katakana to hiragana (and ASCII to lower case) in place, so a
    // reading and a katakana title compare in the same order.
    std::string foldForSort(std::string value)
    {
        std::string out;
        out.reserve(value.size());
        for (std::size_t i = 0; i < value.size();) {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            if (c < 0x80) {
                out.push_back(static_cast<char>(std::tolower(c)));
                ++i;
                continue;
            }
            // Three byte sequence (the whole Japanese range we care about).
            if ((c & 0xF0) == 0xE0 && i + 2 < value.size()) {
                unsigned int cp = ((c & 0x0F) << 12)
                    | ((static_cast<unsigned char>(value[i + 1]) & 0x3F) << 6)
                    | (static_cast<unsigned char>(value[i + 2]) & 0x3F);
                if (cp >= 0x30A1 && cp <= 0x30F6) {
                    cp -= 0x60; // katakana -> hiragana
                }
                out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                i += 3;
                continue;
            }
            out.push_back(value[i]);
            ++i;
        }
        return out;
    }

    // First code point of a UTF-8 string, 0 when empty.
    unsigned int firstCodePoint(const std::string& value)
    {
        if (value.empty()) {
            return 0;
        }
        const unsigned char c = static_cast<unsigned char>(value[0]);
        if (c < 0x80) {
            return c;
        }
        if ((c & 0xE0) == 0xC0 && value.size() >= 2) {
            return (static_cast<unsigned int>(c & 0x1F) << 6)
                | (static_cast<unsigned char>(value[1]) & 0x3F);
        }
        if ((c & 0xF0) == 0xE0 && value.size() >= 3) {
            return (static_cast<unsigned int>(c & 0x0F) << 12)
                | ((static_cast<unsigned char>(value[1]) & 0x3F) << 6)
                | (static_cast<unsigned char>(value[2]) & 0x3F);
        }
        return c;
    }

    // UTF-8 encoding of one code point (the 1-3 byte forms cover everything we
    // build labels from).
    std::string utf8Encode(unsigned int cp)
    {
        std::string out;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return out;
    }

    // Fine-grained index key: the first character of a reading/title, so the
    // grouped list can offer an "A B C ... あ い う" jump index instead of the
    // coarse aiueo rows. Digits and latin letters keep their own key, small
    // kana fold onto their vowel.
    std::string initialLabel(const std::string& key)
    {
        const unsigned int cp = firstCodePoint(key);
        if (cp == 0) {
            return "その他";
        }
        if (cp >= 'a' && cp <= 'z') {
            return std::string(1, static_cast<char>(cp - 'a' + 'A'));
        }
        if ((cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9')) {
            return std::string(1, static_cast<char>(cp));
        }
        if (cp >= 0x3041 && cp <= 0x3049) { // ぁぃぅぇぉ -> あいうえお
            static const unsigned int kVowels[5] = {0x3042, 0x3044, 0x3046, 0x3048, 0x304A};
            return utf8Encode(kVowels[(cp - 0x3041) / 2]);
        }
        if (cp >= 0x3041 && cp <= 0x3096) { // hiragana
            return utf8Encode(cp);
        }
        return "その他";
    }

    // aiueo row of a reading/title (the list's section headers). Japanese
    // readings collapse to their row (あ か さ た な は ま や ら わ), while
    // latin titles keep one section PER LETTER (A, B, C ...) and digits get
    // their own "#" - lumping them into a single "A-Z 0-9" bucket would make
    // English-titled charts (custom charts without an official reading) look
    // like one giant section.
    std::string kanaRowLabel(const std::string& key)
    {
        const unsigned int cp = firstCodePoint(key);
        if (cp == 0) {
            return "その他";
        }
        if (cp >= 'a' && cp <= 'z') {
            return std::string(1, static_cast<char>(cp - 'a' + 'A'));
        }
        if (cp >= 'A' && cp <= 'Z') {
            return std::string(1, static_cast<char>(cp));
        }
        if (cp >= '0' && cp <= '9') {
            return "#";
        }
        if (cp >= 0x3041 && cp <= 0x3096) { // hiragana
            static const std::pair<unsigned int, const char*> rows[] = {
                {0x3042, "あ"}, {0x304B, "か"}, {0x3055, "さ"}, {0x305F, "た"},
                {0x306A, "な"}, {0x306F, "は"}, {0x307E, "ま"}, {0x3084, "や"},
                {0x3089, "ら"}, {0x308F, "わ"},
            };
            const char* label = "その他";
            for (const auto& row : rows) {
                if (cp >= row.first) {
                    label = row.second;
                }
            }
            // Lone small kana / ん / ー have no row of their own: the loop
            // above already folded them into the previous row, except for
            // ぁぃぅぇぉっゃゅょ which map to あ / た / や.
            if (cp == 0x3041 || cp == 0x3043 || cp == 0x3045 || cp == 0x3047 || cp == 0x3049) {
                return "あ";
            }
            if (cp == 0x3063) {
                return "た";
            }
            if (cp == 0x3083 || cp == 0x3085 || cp == 0x3087) {
                return "や";
            }
            return label;
        }
        return "その他";
    }

    // Difficulty band label for a level ("1-5" ... "36+", "-" when unknown).
    std::string levelBandLabel(int level)
    {
        if (level <= 0) {
            return "?";
        }
        if (level >= 36) {
            return "36+";
        }
        const int low = ((level - 1) / 5) * 5 + 1;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d-%d", low, low + 4);
        return buf;
    }

    // Reading used to sort / group one group's songs.
    std::string sortKeyOf(const SongGroup& group)
    {
        return foldForSort(group.kana.empty() ? group.title : group.kana);
    }

    // One row of the song list: either a section header or a song.
    struct ListRow
    {
        bool header = false;
        int group = -1;
        std::string label;
    };

    constexpr int kOrderByName = 0;
    constexpr int kOrderByDifficulty = 1;
    constexpr int kGroupOff = 0;
    constexpr int kGroupDifficulty = 1;
    constexpr int kGroupReading = 2; // one section per aiueo row (letters: one per letter)
    constexpr int kGroupInitial = 3; // one section per first character
    constexpr int kGroupCount = 4;

    // Sort + optionally group the filtered songs into display rows. Rows are
    // what the (cyclic) scroll model walks: a header takes a slot like a song,
    // and the selection always lands on a song rather than a header.
    std::vector<ListRow> buildRows(const std::vector<SongGroup>& groups, const std::vector<int>& visible,
        const std::vector<ChartEntry>& entries, int diffIndex, int order, int groupMode)
    {
        std::vector<int> ordered = visible;
        const auto levelOf = [&](int gi) { return levelForDifficulty(groups[static_cast<size_t>(gi)], entries, diffIndex); };
        std::stable_sort(ordered.begin(), ordered.end(), [&](int a, int b) {
            if (order == kOrderByDifficulty) {
                const int la = levelOf(a);
                const int lb = levelOf(b);
                if (la != lb) {
                    if (la <= 0) {
                        return false; // unknown levels sort last
                    }
                    if (lb <= 0) {
                        return true;
                    }
                    return la < lb;
                }
            }
            const std::string ka = sortKeyOf(groups[static_cast<size_t>(a)]);
            const std::string kb = sortKeyOf(groups[static_cast<size_t>(b)]);
            if (ka != kb) {
                return ka < kb;
            }
            return groups[static_cast<size_t>(a)].title < groups[static_cast<size_t>(b)].title;
        });

        std::vector<ListRow> rows;
        rows.reserve(ordered.size() + 8);
        std::string lastLabel;
        bool haveLabel = false;
        for (const int gi : ordered) {
            if (groupMode != kGroupOff) {
                const std::string label = groupMode == kGroupDifficulty
                    ? levelBandLabel(levelOf(gi))
                    : groupMode == kGroupInitial
                        ? initialLabel(sortKeyOf(groups[static_cast<size_t>(gi)]))
                        : kanaRowLabel(sortKeyOf(groups[static_cast<size_t>(gi)]));
                if (!haveLabel || label != lastLabel) {
                    ListRow header;
                    header.header = true;
                    header.label = label;
                    rows.push_back(header);
                    lastLabel = label;
                    haveLabel = true;
                }
            }
            ListRow row;
            row.group = gi;
            rows.push_back(row);
        }
        return rows;
    }

    std::vector<SongGroup> buildGroups(const std::vector<ChartEntry>& entries)
    {
        std::vector<SongGroup> groups;
        std::map<std::string, int> byKey;
        std::vector<bool> titleIsReal;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const ChartEntry& item = entries[static_cast<size_t>(i)];
            const std::string title = item.title.empty() ? item.displayName : item.title;
            // Group by music id when the file name carries one, so a difficulty
            // whose sidecar is missing still joins its song instead of showing
            // up as a separate "<id> <difficulty>" entry.
            const std::string key = item.musicId > 0 ? ("#" + std::to_string(item.musicId)) : title;
            auto it = byKey.find(key);
            if (it == byKey.end()) {
                it = byKey.emplace(key, static_cast<int>(groups.size())).first;
                SongGroup group;
                group.title = title;
                group.artist = item.artist;
                group.vocal = item.vocal;
                group.coverPath = item.coverPath;
                group.musicId = item.musicId;
                group.kana = item.kana.empty() ? pronunciationFor(item.musicId) : item.kana;
                groups.push_back(group);
                titleIsReal.push_back(!item.title.empty());
            }
            SongGroup& group = groups[static_cast<size_t>(it->second)];
            if (group.musicId <= 0) {
                group.musicId = item.musicId;
            }
            if (group.kana.empty()) {
                group.kana = item.kana.empty() ? pronunciationFor(item.musicId) : item.kana;
            }
            // A real chart title always beats the file-name fallback.
            if (!item.title.empty() && !titleIsReal[static_cast<size_t>(it->second)]) {
                group.title = item.title;
                titleIsReal[static_cast<size_t>(it->second)] = true;
            }
            if (group.artist.empty()) group.artist = item.artist;
            if (group.vocal.empty()) group.vocal = item.vocal;
            if (group.coverPath.empty()) group.coverPath = item.coverPath;
            const int d = diffIndexOf(item.difficulty);
            if (d >= 0 && group.idx[d] < 0) {
                group.idx[d] = i;
            } else if (d < 0) {
                // The chart's difficulty did not map to a slot - an empty or
                // unknown #DIFFICULTY, which unipjsk charts ship as a rule
                // ("#DIFFICULTY 0" with no name this table knows). Without a
                // slot the row is unreachable: the selection is derived from
                // groups[..].idx[diffIndex], so *every* difficulty reads -1 and
                // the list ends up with nothing selected - the song cannot be
                // started at all, and in a room the host can never publish it
                // (the whole round then waits forever on a song that is on
                // screen). Park it in the lowest free slot instead: the row
                // stays selectable, and difficultyIndex() below reports -1 for
                // it, so nothing pretends to know which difficulty it is.
                for (int slot = 0; slot < kDiffCount; ++slot) {
                    if (group.idx[slot] < 0) {
                        group.idx[slot] = i;
                        break;
                    }
                }
            }
        }
        return groups;
    }

    GLuint thumbFor(platform::Renderer& renderer, const std::string& path)
    {
        static std::map<std::string, GLuint> cache;
        if (path.empty()) {
            return 0;
        }
        auto it = cache.find(path);
        if (it == cache.end()) {
            std::string err;
            const GLuint id = renderer.loadUiTexture(path, err);
            it = cache.emplace(path, id).first;
        }
        return it->second;
    }

    void addDiamond(ImDrawList* dl, const ImVec2& c, float r, ImU32 fill)
    {
        dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), fill);
        dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y),
            IM_COL32(25, 25, 40, 200), 1.5f);
    }

    // ------------------------------------------------------------------
    // Floating background shapes, ported from the pjsk.moe background
    // pattern (BackgroundPattern component, chunk 1lvqppbmv_p-9.js):
    // three layers of mainly triangles (80% triangles / 20% circles),
    // laid out by a mulberry32 stream so the field is identical on every
    // run, then offset by a per-layer parallax factor while the song list
    // scrolls (-0.30 / -0.16 / -0.07 of the scroll distance).
    // ------------------------------------------------------------------
    struct BgShape
    {
        int layer = 1;
        float leftPct = 0.0f;
        float topPct = 0.0f;
        float size = 0.0f;     // svg viewBox is 100x100, mapped to `size` px
        float opacity = 1.0f;
        float rotate = 0.0f;   // degrees
        float skewX = 0.0f;    // degrees
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float phase = 0.0f;    // slow idle-float offset
        unsigned char r = 255;
        unsigned char g = 255;
        unsigned char b = 255;
        bool circle = false;
        bool outline = false;
    };

    // mulberry32, the exact stream the web component seeds with 0x9e3779b9.
    struct Mulberry32
    {
        std::uint32_t state = 0x9e3779b9u;

        float next()
        {
            state += 0x6d2b79f5u;
            std::uint32_t t = state;
            t = (t ^ (t >> 15)) * (1u | t);
            t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
            return static_cast<float>(t ^ (t >> 14)) / 4294967296.0f;
        }

        float range(float lo, float hi) { return lo + next() * (hi - lo); }
    };

    const std::vector<BgShape>& bgShapes()
    {
        static const std::vector<BgShape> shapes = [] {
            // pjsk.moe palette: theme (miku teal) / cyan / pink / yellow / white.
            static const unsigned char kColors[5][3] = {
                {51, 204, 187}, {119, 238, 227}, {255, 117, 168}, {255, 229, 138}, {255, 255, 255}};
            const int kCount[3] = {14, 12, 8};             // shapes per layer
            const float kSize[3][2] = {{8, 14}, {7, 12}, {6, 10}};      // circles only
            const float kOpacity[3][2] = {{.1f, .18f}, {.14f, .24f}, {.2f, .32f}};

            std::vector<BgShape> out;
            Mulberry32 rng;
            // The web code mirrors every other leftPct around the centre so the
            // field is not clustered on one side.
            float pairLeft = -1.0f;
            auto leftPct = [&]() {
                if (pairLeft >= 0.0f) {
                    const float v = pairLeft;
                    pairLeft = -1.0f;
                    return v;
                }
                const float r = 2.0f * rng.next() - 1.0f;
                const float t = 50.0f + (r < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(r), 0.6f) * 50.0f;
                pairLeft = 100.0f - t;
                return t;
            };
            for (int layer = 1; layer <= 3; ++layer) {
                for (int i = 0; i < kCount[layer - 1]; ++i) {
                    BgShape s;
                    s.layer = layer;
                    const unsigned char* c = kColors[static_cast<int>(rng.next() * 5.0f) % 5];
                    s.r = c[0];
                    s.g = c[1];
                    s.b = c[2];
                    s.leftPct = leftPct();
                    s.topPct = rng.next() * 100.0f;
                    s.phase = rng.next() * 6.2831853f;
                    if (rng.next() < 0.8f) {
                        // Triangle: 2/3 large faint, 1/3 small bold.
                        const bool large = rng.next() < 0.67f;
                        s.size = large ? rng.range(60.0f, 95.0f) : rng.range(22.0f, 38.0f);
                        s.opacity = large ? rng.range(0.08f, 0.13f) : rng.range(0.30f, 0.48f);
                        s.rotate = (2.0f * rng.next() - 1.0f) * 50.0f;
                        s.skewX = (rng.next() < 0.5f ? -1.0f : 1.0f) * (6.0f + 12.0f * rng.next());
                        s.scaleX = 0.38f + 0.16f * rng.next();
                        s.scaleY = s.scaleX * (1.3f + 0.4f * rng.next());
                        s.outline = rng.next() < 0.5f;
                    } else {
                        s.circle = true;
                        s.size = rng.range(kSize[layer - 1][0], kSize[layer - 1][1]);
                        s.opacity = rng.range(kOpacity[layer - 1][0], kOpacity[layer - 1][1]);
                    }
                    out.push_back(s);
                }
            }
            return out;
        }();
        return shapes;
    }

    void drawBgShapes(ImDrawList* dl, float w, float h, float k, double timeSec, float scroll)
    {
        // Parallax is relative to where the list was when the screen opened, so
        // the field is already in place on entry. Clamped, because the song list
        // is endless and sliding the art off-screen entirely would just look
        // empty.
        static float base = 0.0f;
        static bool haveBase = false;
        if (!haveBase) {
            base = scroll;
            haveBase = true;
        }
        const float rel = std::clamp(scroll - base, -900.0f, 900.0f);

        for (const BgShape& s : bgShapes()) {
            const float factor = s.layer == 1 ? 0.30f : (s.layer == 2 ? 0.16f : 0.07f);
            const float floatY = std::sin(static_cast<float>(timeSec) * 0.35f + s.phase) * 9.0f * k;
            const float cx = s.leftPct * 0.01f * w;
            const float cy = s.topPct * 0.01f * h - factor * rel + floatY;
            const float size = s.size * k;
            if (cx < -size || cx > w + size || cy < -size || cy > h + size) {
                continue;
            }
            const int alpha = static_cast<int>(std::clamp(s.opacity, 0.0f, 1.0f) * 255.0f);
            const ImU32 col = IM_COL32(s.r, s.g, s.b, alpha);
            if (s.circle) {
                dl->AddCircleFilled(ImVec2(cx, cy), size * 0.5f, col, 20);
                continue;
            }
            // svg polygon "10,0 0,100 100,85" with
            // translate(50 50) scale() skewX() rotate() translate(-50 -50).
            static const float kPts[3][2] = {{10.0f, 0.0f}, {0.0f, 100.0f}, {100.0f, 85.0f}};
            const float rad = s.rotate * 0.017453292f;
            const float cosR = std::cos(rad);
            const float sinR = std::sin(rad);
            const float tanSkew = std::tan(s.skewX * 0.017453292f);
            ImVec2 pts[3];
            for (int i = 0; i < 3; ++i) {
                float px = kPts[i][0] - 50.0f;
                float py = kPts[i][1] - 50.0f;
                const float rx = px * cosR - py * sinR;
                const float ry = px * sinR + py * cosR;
                px = (rx + ry * tanSkew) * s.scaleX + 50.0f;
                py = ry * s.scaleY + 50.0f;
                pts[i] = ImVec2(cx + (px * 0.01f - 0.5f) * size, cy + (py * 0.01f - 0.5f) * size);
            }
            if (s.outline) {
                dl->AddPolyline(pts, 3, col, ImDrawFlags_Closed, std::max(1.2f, 1.4f * k));
            } else {
                dl->AddTriangleFilled(pts[0], pts[1], pts[2], col);
            }
        }
    }

    // Rounded jacket image, or a purple placeholder when there is none.
    void addJacket(ImDrawList* dl, GLuint tex, const ImVec2& pmin, const ImVec2& pmax, float rounding)
    {
        if (tex != 0) {
            dl->AddImageRounded(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)), pmin, pmax,
                ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), rounding);
        } else {
            dl->AddRectFilled(pmin, pmax, IM_COL32(70, 60, 120, 220), rounding);
            dl->AddRect(pmin, pmax, IM_COL32(150, 140, 200, 120), rounding, 0, 1.5f);
        }
    }

    void addTextCentered(ImDrawList* dl, ImFont* font, float size, const ImVec2& center, ImU32 col, const char* text)
    {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        dl->AddText(font, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), col, text);
    }

    // Left-aligned twin of addTextCentered: `pos.x` is the left edge, `pos.y`
    // the vertical centre (the reference song panel left-aligns its metadata).
    void addTextLeft(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, ImU32 col, const char* text)
    {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        dl->AddText(font, size, ImVec2(pos.x, pos.y - ts.y * 0.5f), col, text);
    }

    // Drops whole UTF-8 characters off the end until `text` + "…" fits maxW.
    // Used by the song panel, where the title / artist / vocal lines must not
    // run under the score-rank badge on the right.
    std::string ellipsize(ImFont* font, float size, const std::string& text, float maxW)
    {
        if (maxW <= 0.0f || font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= maxW) {
            return text;
        }
        std::string out = text;
        while (!out.empty()) {
            size_t cut = out.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0u) == 0x80u) {
                --cut;
            }
            out.erase(cut);
            const std::string candidate = out + "…";
            if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x <= maxW) {
                return candidate;
            }
        }
        return "…";
    }

    // Chart level fed to the score-rank thresholds. Same priority as the play
    // screen (main.cpp): the official level table first, then the chart's own
    // #PLAYLEVEL / sidecar, then the upstream overlay player's hard-coded 26.
    // Keeping it identical is what makes the badge agree with the SCORE RANK
    // the HUD shows during the run.
    float chartRatingFor(const ChartEntry& entry)
    {
        int level = musicLevel(entry.musicId, entry.difficulty);
        if (level <= 0) {
            level = std::atoi(entry.level.c_str());
        }
        return level > 0 ? static_cast<float>(level) : 26.0f;
    }

    // The best-score badge the reference phone panel puts on the right of the
    // song info: a translucent white disc with the game's own
    // score/rank/chr/<x>.png letter in it (d/c/b/a/s, each PNG pre-coloured),
    // or "--" while the chart has no record at all.
    void drawBestScoreBadge(ImDrawList* dl, platform::Renderer& renderer, ImFont* font,
        double bestScore, float chartRating, float diameter, float centerX, float centerY)
    {
        const ImVec2 center(centerX, centerY);
        const float radius = diameter * 0.5f;
        const bool played = bestScore > 0.0;
        // Measured off the reference: white over the panel at ~32% (a plain
        // white disc reads the same there), dimmer when there is no record.
        dl->AddCircleFilled(center, radius, played ? IM_COL32(255, 255, 255, 82) : IM_COL32(255, 255, 255, 52), 48);

        if (!played) {
            const char* dashes = "--";
            const float size = diameter * 0.5f;
            const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, dashes);
            dl->AddText(font, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
                IM_COL32(226, 226, 240, 200), dashes);
            return;
        }

        const ScoreRank rank = scoreRankAndBar(bestScore, chartRating);
        const char letter = rank.rank >= 'a' && rank.rank <= 's' ? rank.rank : 'd';
        const auto* glyph = renderer.hud(std::string("rank_char_") + letter);
        if (glyph == nullptr || glyph->id == 0) {
            return;
        }
        // score/rank/chr/<x>.png is 224x266 with the letter's ink 185x242 at
        // +20+16; the reference draws that ink 0.686 of the badge across, so
        // the whole PNG is drawn a little larger and shifted by the ink offset.
        constexpr float kInkH = 242.0f / 266.0f;
        constexpr float kInkCx = 112.5f / 224.0f;
        constexpr float kInkCy = 137.0f / 266.0f;
        const float imgH = diameter * 0.686f / kInkH;
        const float imgW = imgH * (static_cast<float>(glyph->width) / static_cast<float>(glyph->height));
        const float imgCx = center.x + (0.5f - kInkCx) * imgW;
        const float imgCy = center.y + (0.5f - kInkCy) * imgH;
        dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(glyph->id)),
            ImVec2(imgCx - imgW * 0.5f, imgCy - imgH * 0.5f), ImVec2(imgCx + imgW * 0.5f, imgCy + imgH * 0.5f));
    }

    // Cached textures from assets/select (phone frame, buttons, indicators).
    GLuint selectTex(platform::Renderer& renderer, const char* name)
    {
        static std::map<std::string, GLuint> cache;
        auto it = cache.find(name);
        if (it == cache.end()) {
            std::string err;
            const GLuint id = gSelectAssetDir.empty()
                ? 0
                : renderer.loadUiTexture(gSelectAssetDir + "\\select\\" + name + ".png", err);
            it = cache.emplace(name, id).first;
        }
        return it->second;
    }

    // Level text: the chart's own #PLAYLEVEL / sidecar, else the official table
    // for its difficulty. Charts with no data at all show "-" instead of 0.
    const char* levelText(const ChartEntry& entry, char* buf, size_t bufSize)
    {
        const int lv = resolvedLevel(entry);
        if (lv <= 0) {
            return "-";
        }
        std::snprintf(buf, bufSize, "%d", lv);
        return buf;
    }

    // One slot of the 5-diamond difficulty strip: indicate_back_new.png with
    // the clear / full-combo mark drawn on top of it (both badges are the same
    // 38x38 diamond, so they cover the backing sprite exactly).
    void addDiamondSlot(ImDrawList* dl, GLuint backTex, const ImVec2& center, float size, bool present,
        GLuint badgeTex)
    {
        const ImVec2 p0(center.x - size * 0.5f, center.y - size * 0.5f);
        const ImVec2 p1(center.x + size * 0.5f, center.y + size * 0.5f);
        if (backTex != 0) {
            // Available difficulties keep the sprite's own colours, the ones
            // the song does not have are dimmed back.
            const ImU32 tint = present ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 84);
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(backTex)),
                p0, p1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
        } else {
            addDiamond(dl, center, size * 0.5f, present ? IM_COL32(120, 126, 156, 255) : IM_COL32(58, 58, 82, 255));
        }
        if (badgeTex != 0 && present) {
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(badgeTex)),
                p0, p1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));
        }
    }
} // namespace

void loadMusicLevels(const std::string& path)
{
    std::error_code ec;
    if (path.empty() || !fs::exists(toFsPath(path), ec)) {
        return;
    }
    std::ifstream file(toFsPath(path), std::ios::binary);
    if (!file) {
        return;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(file);
    } catch (...) {
        return;
    }

    // Compact form: { "75": [6, 13, 17, 23, 28], ... } -> easy..master.
    if (doc.is_object()) {
        for (auto it = doc.begin(); it != doc.end(); ++it) {
            const int musicId = std::atoi(it.key().c_str());
            const nlohmann::json& levels = it.value();
            if (musicId <= 0 || !levels.is_array()) {
                continue;
            }
            for (size_t d = 0; d < levels.size() && d < static_cast<size_t>(kDiffCount); ++d) {
                if (levels[d].is_number_integer()) {
                    storeLevel(musicId, static_cast<int>(d), levels[d].get<int>());
                }
            }
        }
        return;
    }

    // Verbatim game data (musicDifficulties.json): one row per difficulty.
    if (doc.is_array()) {
        for (const auto& row : doc) {
            if (!row.is_object()) {
                continue;
            }
            const int musicId = row.value("musicId", 0);
            const std::string name = toUpper(row.value("musicDifficulty", std::string{}));
            const int level = row.value("playLevel", 0);
            for (int d = 0; d < kDiffCount; ++d) {
                if (name == kDiffNames[d]) {
                    storeLevel(musicId, d, level);
                    break;
                }
            }
        }
    }
}

int musicLevel(int musicId, const std::string& difficulty)
{
    return tableLevel(musicId, diffIndexOf(difficulty));
}

ImU32 difficultyColor(const std::string& difficulty, int alpha)
{
    if (difficulty == "EASY") {
        return IM_COL32(75, 207, 138, alpha);
    }
    if (difficulty == "NORMAL") {
        return IM_COL32(90, 140, 255, alpha);
    }
    if (difficulty == "HARD") {
        return IM_COL32(242, 150, 77, alpha);
    }
    if (difficulty == "EXPERT") {
        return IM_COL32(239, 90, 102, alpha);
    }
    if (difficulty == "MASTER") {
        return IM_COL32(181, 91, 255, alpha);
    }
    if (difficulty == "APPEND") {
        return IM_COL32(179, 162, 255, alpha);
    }
    if (difficulty == "ETERNAL") {
        return IM_COL32(241, 192, 79, alpha);
    }
    return IM_COL32(120, 130, 160, alpha);
}

void loadMusicMaster(const std::string& path)
{
    std::error_code ec;
    if (path.empty() || !fs::exists(toFsPath(path), ec)) {
        return;
    }
    std::ifstream file(toFsPath(path), std::ios::binary);
    if (!file) {
        return;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(file);
    } catch (...) {
        return;
    }

    // Verbatim game data (musics.json): one object per song.
    if (doc.is_array()) {
        for (const auto& row : doc) {
            if (!row.is_object()) {
                continue;
            }
            const int musicId = row.value("id", 0);
            storeKana(musicId, row.value("pronunciation", std::string{}));
            storeTitle(musicId, row.value("title", std::string{}));
        }
        return;
    }
    // Compact form: { "75": "ほしをつなぐ..." } for the reading only, or
    // { "75": { "kana": "...", "title": "..." } } when both are wanted.
    if (doc.is_object()) {
        for (auto it = doc.begin(); it != doc.end(); ++it) {
            const int musicId = std::atoi(it.key().c_str());
            if (it.value().is_string()) {
                storeKana(musicId, it.value().get<std::string>());
            } else if (it.value().is_object()) {
                storeKana(musicId, it.value().value("kana", it.value().value("pronunciation", std::string{})));
                storeTitle(musicId, it.value().value("title", std::string{}));
            }
        }
    }
}

const std::vector<VocalVersion>& musicVocals(int musicId)
{
    static const std::vector<VocalVersion> empty;
    const auto it = gMusicVocals.find(musicId);
    return it == gMusicVocals.end() ? empty : it->second;
}

std::vector<VocalVersion> availableVocals(const ChartEntry& entry)
{
    std::vector<VocalVersion> out;
    if (entry.musicId <= 0) {
        return out;
    }
    const fs::path dir = toFsPath(entry.susPath).parent_path();
    for (const VocalVersion& version : musicVocals(entry.musicId)) {
        VocalVersion copy = version;
        copy.available = audioFileExists(dir / (version.asset + ".mp3"));
        if (copy.available) {
            out.push_back(std::move(copy));
        }
    }
    return out;
}

std::string vocalShortLabel(const VocalVersion& version)
{
    if (version.type == "sekai") {
        return "セカイ";
    }
    if (version.type == "virtual_singer" || version.type == "original_song") {
        return "バーチャル";
    }
    if (version.singers.empty()) {
        return version.caption;
    }
    if (version.singers.size() == 1) {
        return version.singers.front();
    }
    return version.singers.front() + " 他";
}

int defaultVocalIndex(int musicId, const std::vector<VocalVersion>& available)
{
    if (available.empty()) {
        return -1;
    }
    // The セカイver is what the plain <id4>.mp3 download is (unipjsk serves
    // se_<id>_01 there), so it keeps an existing library sounding the same.
    for (size_t i = 0; i < available.size(); ++i) {
        if (available[i].type == "sekai") {
            return static_cast<int>(i);
        }
    }
    (void)musicId;
    return 0;
}

bool applyVocalVersion(ChartEntry& entry, const std::vector<VocalVersion>& versions, int index)
{
    if (index < 0 || index >= static_cast<int>(versions.size())) {
        return false;
    }
    const VocalVersion& version = versions[static_cast<size_t>(index)];
    const fs::path file = toFsPath(entry.susPath).parent_path() / (version.asset + ".mp3");
    if (!audioFileExists(file)) {
        return false;
    }
    entry.bgmPath = fromFsPath(file);
    entry.vocal.clear();
    for (const std::string& singer : version.singers) {
        if (!entry.vocal.empty()) {
            entry.vocal += "、";
        }
        entry.vocal += singer;
    }
    return true;
}

std::string vocalAudioPath(const ChartEntry& entry, int index)
{
    const std::vector<VocalVersion> versions = availableVocals(entry);
    if (index < 0 || index >= static_cast<int>(versions.size())) {
        return {};
    }
    const fs::path file =
        toFsPath(entry.susPath).parent_path() / (versions[static_cast<size_t>(index)].asset + ".mp3");
    return audioFileExists(file) ? fromFsPath(file) : std::string();
}

void applyDefaultVocal(ChartEntry& entry)
{
    if (!entry.bgmPath.empty()) {
        return;
    }
    const std::vector<VocalVersion> versions = availableVocals(entry);
    if (versions.empty()) {
        return;
    }
    applyVocalVersion(entry, versions, defaultVocalIndex(entry.musicId, versions));
}

void loadMusicAliases(const std::string& path)
{
    gAliasIndex.clear();
    std::error_code ec;
    if (path.empty() || !fs::exists(toFsPath(path), ec)) {
        return;
    }
    std::ifstream file(toFsPath(path), std::ios::binary);
    if (!file) {
        return;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(file);
    } catch (...) {
        return;
    }
    if (!doc.is_object()) {
        return;
    }
    for (auto it = doc.begin(); it != doc.end(); ++it) {
        const int musicId = std::atoi(it.key().c_str());
        if (musicId <= 0 || !it.value().is_array()) {
            continue;
        }
        for (const auto& row : it.value()) {
            if (!row.is_string()) {
                continue;
            }
            const std::string alias = toLower(row.get<std::string>());
            if (alias.empty()) {
                continue;
            }
            std::vector<int>& ids = gAliasIndex[alias];
            if (std::find(ids.begin(), ids.end(), musicId) == ids.end()) {
                ids.push_back(musicId);
            }
        }
    }
    // Logged because a missing music-aliases.json is otherwise invisible: the
    // search just quietly loses its alias branch.
    std::printf("[aliases] %zu aliases\n", gAliasIndex.size());
    std::fflush(stdout);
}

void loadMusicVocals(const std::string& path)
{
    std::error_code ec;
    if (path.empty() || !fs::exists(toFsPath(path), ec)) {
        return;
    }
    std::ifstream file(toFsPath(path), std::ios::binary);
    if (!file) {
        return;
    }
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(file);
    } catch (...) {
        return;
    }
    if (!doc.is_object()) {
        return;
    }
    for (auto it = doc.begin(); it != doc.end(); ++it) {
        const int musicId = std::atoi(it.key().c_str());
        if (musicId <= 0 || !it.value().is_array()) {
            continue;
        }
        std::vector<VocalVersion> list;
        for (const auto& row : it.value()) {
            if (!row.is_object()) {
                continue;
            }
            VocalVersion version;
            version.id = row.value("id", 0);
            version.type = row.value("type", std::string{});
            version.caption = row.value("caption", std::string{});
            version.asset = row.value("asset", std::string{});
            if (row.contains("singers") && row["singers"].is_array()) {
                for (const auto& singer : row["singers"]) {
                    if (singer.is_string()) {
                        version.singers.push_back(singer.get<std::string>());
                    }
                }
            }
            if (!version.asset.empty()) {
                list.push_back(std::move(version));
            }
        }
        if (!list.empty()) {
            gMusicVocals[musicId] = std::move(list);
        }
    }
}

// One 猜歌 option: the same capsule as ui::capsuleButton (soft drop shadow,
// half-height rounding, body font sized to the button), but with the fill
// handed in - the quiz needs a mint "right answer" and a red "you picked this",
// which capsuleButton's mint-or-white cannot express. `clickable` is false once
// the question has been answered, so the verdict cannot be clicked away.
bool guessOptionPill(const char* label, const ImVec2& size, ImU32 fill, float s, bool clickable)
{
    ImVec2 lo = ImGui::GetCursorScreenPos();
    ImVec2 hi(lo.x + size.x, lo.y + size.y);
    ImGui::InvisibleButton(label, size);
    const bool clicked = clickable && ImGui::IsItemClicked();
    if (clicked) {
        ui::se(ui::SeClick);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = (hi.y - lo.y) * 0.5f;
    if (clickable && ImGui::IsItemHovered()) {
        fill = ui::mix(fill, ui::kWhiteHover, 0.35f);
    }
    dl->AddRectFilled(ImVec2(lo.x, lo.y + 3.0f * s), ImVec2(hi.x, hi.y + 3.0f * s),
        IM_COL32(150, 150, 170, 60), radius);
    dl->AddRectFilled(lo, hi, fill, radius);
    ImFont* font = game::bodyFont();
    const float fontSize = std::min(22.0f * s, size.y * 0.42f);
    const ImVec2 text = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    dl->AddText(font, fontSize,
        ImVec2((lo.x + hi.x - text.x) * 0.5f, (lo.y + hi.y - text.y) * 0.5f), ui::kBtnText, label);
    return clicked;
}

int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec, int& sortMode, int& groupMode, int& vocalIndex,
    float uiScale, ImVec2* confirmCenter, const AccountData* account, const SelectPartyInfo* party,
    SelectPartyResult* partyOut)
{
    int action = SelectNone;
    // 多人游玩: the room owns the song. The host keeps a normal, fully
    // interactive list (it is the one choosing); every other window gets a
    // greyed-out, read-only list whose phone panel shows whatever the host is
    // parked on, so the difficulty can be picked without a second screen.
    const bool partyRoom = party != nullptr && party->active;
    const bool partyReadOnly = partyRoom && !party->host;
    // A member can only pick a difficulty once the room actually has a song its
    // window can play - before that there is nothing to pick for.
    const bool partyPickable = partyReadOnly && party->songLocked && party->lockedEntry >= 0;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float w = viewport->WorkSize.x;
    const float h = viewport->WorkSize.y;
    // 1080p reference layout. The user's UI scale multiplies it, which zooms the
    // whole screen: kBase stays "how big is the window", k is what everything is
    // laid out with. The phone panel below is the one piece sized from the window
    // itself, so it multiplies the scale as well - otherwise the list would grow
    // while the panel stayed put.
    const float scale = std::clamp(uiScale, 0.5f, 2.0f);
    const float kBase = std::clamp(h / 1080.0f, 0.5f, 2.0f);
    const float k = kBase * scale;

    static std::vector<SongGroup> groups;
    static int groupIndex = 0;
    static int diffIndex = 3; // EXPERT, like the reference UI
    static size_t lastCount = 0;
    static bool firstFrame = true;
    // Group to park the list on for the very first layout (-1 = derive it from
    // the first row, the historical behaviour).
    static int startGroup = -1;
    if (entries.size() != lastCount) {
        lastCount = entries.size();
        groups = buildGroups(entries);
        groupIndex = 0;
        firstFrame = true;
    } else if (groups.empty() && !entries.empty()) {
        groups = buildGroups(entries);
        firstFrame = true;
    }
    // First frame: start on the caller's `selected` instead of always parking on
    // the first song (the CLI --select-id hand-off; the difficulty is left as
    // configured so the default stays EXPERT).
    if (firstFrame) {
        firstFrame = false;
        startGroup = -1;
        if (selected > 0 && selected < static_cast<int>(entries.size())) {
            for (size_t gi = 0; gi < groups.size(); ++gi) {
                for (int d = 0; d < kDiffCount; ++d) {
                    if (groups[gi].idx[d] == selected) {
                        groupIndex = static_cast<int>(gi);
                        startGroup = static_cast<int>(gi);
                        break;
                    }
                }
            }
        }
    }
    if (groupIndex >= static_cast<int>(groups.size())) {
        groupIndex = groups.empty() ? 0 : static_cast<int>(groups.size()) - 1;
    }

    // Pick the current difficulty inside the group; fall back to the nearest
    // available one (preferring harder charts).
    if (!groups.empty()) {
        const SongGroup& g = groups[static_cast<size_t>(groupIndex)];
        if (g.idx[diffIndex] < 0) {
            int best = -1;
            int bestDist = 99;
            for (int d = 0; d < kDiffCount; ++d) {
                if (g.idx[d] >= 0 && std::abs(d - diffIndex) < bestDist) {
                    best = d;
                    bestDist = std::abs(d - diffIndex);
                }
            }
            if (best < 0) {
                for (int d = 0; d < kDiffCount; ++d) {
                    if (g.idx[d] >= 0) {
                        best = d;
                        break;
                    }
                }
            }
            diffIndex = best < 0 ? 3 : best;
        }
        selected = groups[static_cast<size_t>(groupIndex)].idx[diffIndex];
        if (selected < 0) {
            // Last resort: the fallback above picks a *slot* only from ones that
            // hold an entry, so this can only be reached with a group that has
            // no chart at all - which the group builder does not produce. Kept
            // anyway so a future slot layout can never silently leave the list
            // with nothing selected (that state starts no song and, in a room,
            // stalls the host).
            for (int d = 0; d < kDiffCount; ++d) {
                if (groups[static_cast<size_t>(groupIndex)].idx[d] >= 0) {
                    selected = groups[static_cast<size_t>(groupIndex)].idx[d];
                    break;
                }
            }
        }
    } else {
        selected = -1;
    }

    // 多人游玩 member: the room decides what is on screen. Park the list (and
    // therefore the phone panel, the jacket and the score badge) on the chart
    // the host is sitting on, whatever this window's own cursor would say - the
    // list then glides over to it like any other outside selection.
    //
    // The group a chart belongs to is read off the difficulty slots, with the
    // song id as a fallback for a chart no slot claims (an APPEND / ETERNAL
    // one): the list has no row of its own for those.
    const auto groupOf = [&](int entryIndex) {
        for (std::size_t gi = 0; gi < groups.size(); ++gi) {
            for (int d = 0; d < kDiffCount; ++d) {
                if (groups[gi].idx[d] == entryIndex) {
                    return static_cast<int>(gi);
                }
            }
        }
        if (entryIndex >= 0 && entryIndex < static_cast<int>(entries.size())) {
            const int wantedId = entries[static_cast<size_t>(entryIndex)].musicId;
            if (wantedId > 0) {
                for (std::size_t gi = 0; gi < groups.size(); ++gi) {
                    if (groups[gi].musicId == wantedId) {
                        return static_cast<int>(gi);
                    }
                }
            }
        }
        return -1;
    };
    if (partyReadOnly && party->songLocked && party->lockedEntry >= 0
        && party->lockedEntry < static_cast<int>(entries.size())) {
        selected = party->lockedEntry;
        const int lockedGroup = groupOf(party->lockedEntry);
        if (lockedGroup >= 0) {
            groupIndex = lockedGroup;
        }
    }

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    // Glass mode: this window is the last opaque thing between the player and
    // the desktop (ImGui's default WindowBg is 0x06/0.94), so it has to go
    // transparent too. Cards, buttons and list rows inside keep their own
    // backgrounds - only the full-screen wash is dropped.
    if (gSelectFillDisabled) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    }
    ImGui::Begin("CppSekaiSongSelect", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* title = titleFont();
    ImFont* body = bodyFont() != nullptr ? bodyFont() : ImGui::GetFont();

    // Background: the blurred desktop wallpaper when the player picked it,
    // otherwise the built-in dark blue wash with a slow moving highlight.
    // In glass mode (gSelectFillDisabled) neither is drawn - the window is left
    // transparent there on purpose, and the floating shapes below are all that
    // remains of the backdrop.
    const bool drawFill = !gSelectFillDisabled;
    if (drawFill && gSelectBackdropTex != 0 && gSelectBackdropW > 0 && gSelectBackdropH > 0) {
        // "Cover" the window: scale so both sides are filled, centre it and let
        // ImGui clip the overflow (the aspect of a wallpaper rarely matches).
        const float srcAspect = static_cast<float>(gSelectBackdropW) / static_cast<float>(gSelectBackdropH);
        const float dstAspect = w / h;
        float dw = w;
        float dh = h;
        if (srcAspect > dstAspect) {
            dw = h * srcAspect;
        } else {
            dh = w / srcAspect;
        }
        const ImVec2 p0((w - dw) * 0.5f, (h - dh) * 0.5f);
        dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(gSelectBackdropTex)),
            p0, ImVec2(p0.x + dw, p0.y + dh));
        if (gSelectBackdropDim > 0.0f) {
            const int alpha = static_cast<int>(std::clamp(gSelectBackdropDim, 0.0f, 1.0f) * 255.0f);
            dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(8, 10, 24, alpha));
        }
    } else if (drawFill) {
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, h),
            IM_COL32(30, 26, 58, 255), IM_COL32(52, 40, 88, 255),
            IM_COL32(20, 18, 40, 255), IM_COL32(46, 36, 78, 255));
        const float glowX = w * (0.5f + 0.35f * std::sin(timeSec * 0.25f));
        dl->AddCircleFilled(ImVec2(glowX, h * 0.35f), h * 0.6f, IM_COL32(120, 130, 230, 30), 64);
    }

    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const ImU32 grayText = IM_COL32(178, 178, 198, 255);

    // ------------------------------------------------------------------
    // Left: song list
    // ------------------------------------------------------------------
    const float listX = 150.0f * k;
    const float listTop = 30.0f * k;
    const float listW = 450.0f * k;

    // Search bar (top of the list, like the reference UI). Filters by
    // title / artist substring.
    static char searchBuf[64] = "";
    const float searchW = listW;
    const float searchH = 46.0f * k;
    // 多人游玩 member: the header row (search box, sort / grouping, 刷新) all
    // work on the list, and the list is the host's. Everything is disabled
    // rather than skipped so the geometry below stays where it is, and the room
    // banner is drawn over it (see after the 刷新 button).
    ImGui::BeginDisabled(partyReadOnly);
    ImGui::SetCursorScreenPos(ImVec2(listX, listTop));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(250, 250, 253, 210));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 235));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(70, 70, 90, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, searchH * 0.5f);
    // Frame padding doubles as "leave room for the magnifier": the text starts
    // this far in, and the icon (assets/select/search.png) is drawn at the
    // box's left end, *inside* the pill.
    const float searchIconBox = searchH * 0.86f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(searchIconBox, searchH * 0.28f));
    ImGui::PushFont(body, 19.0f * k);
    // Full width: the icon lives inside the field, so the field has to be the
    // whole pill. (It used to be shortened by searchH and the magnifier drawn
    // past its right edge, which left it hanging outside the box.)
    ImGui::SetNextItemWidth(searchW);
    ImGui::InputTextWithHint("##search", "根据歌曲名·作者名查找", searchBuf, sizeof(searchBuf));
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    {
        const GLuint searchIcon = selectTex(renderer, "search");
        if (searchIcon != 0) {
            const float iconSize = searchH * 0.44f;
            const ImVec2 c(listX + searchIconBox * 0.5f + 2.0f * k, listTop + searchH * 0.5f);
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(searchIcon)),
                ImVec2(c.x - iconSize * 0.5f, c.y - iconSize * 0.5f),
                ImVec2(c.x + iconSize * 0.5f, c.y + iconSize * 0.5f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, searchBuf[0] == '\0' ? 150 : 210));
        }
    }

    // Sort / grouping selectors, to the right of the search box (the official
    // screen has a sort FAB there - a combo is easier to hit on a touchscreen).
    // Both live in the caller's settings, so the list comes back as it was.
    if (sortMode < 0 || sortMode > 1) {
        sortMode = 0;
    }
    if (groupMode < 0 || groupMode >= kGroupCount) {
        groupMode = 0;
    }
    const float comboW = 168.0f * k;
    const float comboX0 = listX + searchW + 18.0f * k;
    const float comboGap = 12.0f * k;
    const float headerRowY = listTop + 4.0f * k;
    {
        const char* kSortLabels[2] = {"按名称", "按难度"};
        const char* kGroupLabels[kGroupCount] = {"关闭", "按难度段", "按读音", "按首字"};
        const std::string sortPreview = std::string("排序：") + kSortLabels[sortMode];
        const std::string groupPreview = std::string("分组：") + kGroupLabels[groupMode];
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(58, 52, 92, 235));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(76, 68, 118, 245));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(90, 80, 138, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(238, 238, 248, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(46, 40, 76, 250));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(110, 106, 190, 200));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(140, 136, 225, 220));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(170, 166, 255, 240));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * k);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * k, 8.0f * k));
        ImGui::PushFont(body, 17.0f * k);
        ImGui::SetCursorScreenPos(ImVec2(comboX0, headerRowY));
        ImGui::SetNextItemWidth(comboW);
        // ui::combo is the same popup plus a fade-in and a rotating chevron;
        // `k` is handed over because this screen has its own px-per-unit scale.
        const std::vector<std::string> sortItems{kSortLabels[0], kSortLabels[1]};
        ui::combo("##sortby", sortPreview.c_str(), sortItems, &sortMode, comboW,
            ImGuiComboFlags_HeightSmall, k);
        ImGui::SetCursorScreenPos(ImVec2(comboX0 + comboW + comboGap, headerRowY));
        const std::vector<std::string> groupItems(kGroupLabels, kGroupLabels + kGroupCount);
        ui::combo("##groupby", groupPreview.c_str(), groupItems, &groupMode, comboW,
            ImGuiComboFlags_HeightSmall, k);
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(8);
    }

    // Rescan button, in the same row as the selectors. F5 has always done this
    // but nothing on screen said so; a chart dropped into charts/ while the
    // game is running is exactly what a player reaches for.
    //
    // The geometry is computed out here because the 多人游玩 banner has to cover
    // the whole header row, this button included.
    const float headerRowH = 36.0f * k;
    const float rescanW = 104.0f * k;
    const float rescanX = comboX0 + (comboW + comboGap) * 2.0f + 4.0f * k;
    // 下载谱面 sits right of 刷新: the downloader was reachable from the empty-list
    // state and from the settings, but on a full list there was nothing.
    const float storeW = 144.0f * k;
    const float storeX = rescanX + rescanW + 10.0f * k;
    // 猜歌 sits right of 音乐商店. It is the one button here that does not touch
    // the filesystem - it opens a card, so it does not queue an action for
    // main.cpp to run (see the 猜歌 card at the end of this function).
    const float guessW = 104.0f * k;
    const float guessX = storeX + storeW + 10.0f * k;
    {
        const float rowH = headerRowH;
        const float btnW = rescanW;
        const float btnX = rescanX;
        const ImU32 fg = IM_COL32(238, 238, 248, 255);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(58, 52, 92, 235));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(76, 68, 118, 245));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(90, 80, 138, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rowH * 0.5f);
        ImGui::SetCursorScreenPos(ImVec2(btnX, headerRowY));
        if (ImGui::Button("##rescan", ImVec2(btnW, rowH))) {
            ui::se(ui::SeClick);
            action = SelectRescan;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        // No SetTooltip here on purpose. ImGui's default font atlas has no CJK
        // glyphs, so a Chinese tooltip renders as a row of '?'; and the button
        // already says 刷新 with F5 documented in the empty-state hint.
        (void)hovered;

        // Official icon (assets/select/refresh.png - a white circular arrow, so
        // it needs no tint). Falls back to nothing if the file is missing; the
        // label still says what the button does.
        const ImVec2 c(btnX + 26.0f * k, headerRowY + rowH * 0.5f);
        const GLuint refreshIcon = selectTex(renderer, "refresh");
        if (refreshIcon != 0) {
            const float iconSize = 20.0f * k;
            const int iconAlpha = hovered ? 255 : 232; // the icon is already white
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(refreshIcon)),
                ImVec2(c.x - iconSize * 0.5f, c.y - iconSize * 0.5f),
                ImVec2(c.x + iconSize * 0.5f, c.y + iconSize * 0.5f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, iconAlpha));
        }
        addTextLeft(dl, body, 17.0f * k, ImVec2(c.x + 18.0f * k, c.y), fg, "刷新");
    }
    {
        // Same chrome as 刷新, with the store icon (assets/select/store.png) and
        // SelectDownload - the action the empty list already used (main.cpp).
        const float rowH = headerRowH;
        const float btnW = storeW;
        const float btnX = storeX;
        const ImU32 fg = IM_COL32(238, 238, 248, 255);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(58, 52, 92, 235));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(76, 68, 118, 245));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(90, 80, 138, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rowH * 0.5f);
        ImGui::SetCursorScreenPos(ImVec2(btnX, headerRowY));
        if (ImGui::Button("##store", ImVec2(btnW, rowH))) {
            ui::se(ui::SeClick);
            action = SelectDownload;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        const ImVec2 c(btnX + 26.0f * k, headerRowY + rowH * 0.5f);
        const GLuint storeIcon = selectTex(renderer, "store");
        if (storeIcon != 0) {
            const float iconSize = 20.0f * k;
            const int iconAlpha = hovered ? 255 : 232;
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(storeIcon)),
                ImVec2(c.x - iconSize * 0.5f, c.y - iconSize * 0.5f),
                ImVec2(c.x + iconSize * 0.5f, c.y + iconSize * 0.5f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, iconAlpha));
        }
        addTextLeft(dl, body, 17.0f * k, ImVec2(c.x + 18.0f * k, c.y), fg, "音乐商店");
    }
    {
        // 猜歌: same chrome, guess.png, opens the alias quiz.
        const float rowH = headerRowH;
        const float btnW = guessW;
        const float btnX = guessX;
        const ImU32 fg = IM_COL32(238, 238, 248, 255);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(58, 52, 92, 235));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(76, 68, 118, 245));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(90, 80, 138, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rowH * 0.5f);
        ImGui::SetCursorScreenPos(ImVec2(btnX, headerRowY));
        if (ImGui::Button("##guess", ImVec2(btnW, rowH))) {
            ui::se(ui::SeClick);
            newGuessQuestion();
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        const ImVec2 c(btnX + 26.0f * k, headerRowY + rowH * 0.5f);
        const GLuint guessIcon = selectTex(renderer, "guess");
        if (guessIcon != 0) {
            const float iconSize = 20.0f * k;
            const int iconAlpha = hovered ? 255 : 232;
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(guessIcon)),
                ImVec2(c.x - iconSize * 0.5f, c.y - iconSize * 0.5f),
                ImVec2(c.x + iconSize * 0.5f, c.y + iconSize * 0.5f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, iconAlpha));
        }
        addTextLeft(dl, body, 17.0f * k, ImVec2(c.x + 18.0f * k, c.y), fg, "猜歌");
    }
    ImGui::EndDisabled();

    // ------------------------------------------------------------------
    // 多人游玩: the banner takes the header row over on a member's window.
    // Drawn after the (disabled) widgets, so it covers them - which is also how
    // a member never sees a search filter it could not have typed.
    // ------------------------------------------------------------------
    if (partyReadOnly) {
        const float bannerH = headerRowH + 12.0f * k;
        const ImVec2 b0(listX, headerRowY - 6.0f * k);
        const ImVec2 b1(guessX + guessW, b0.y + bannerH);
        const float cy = (b0.y + b1.y) * 0.5f;
        dl->AddRectFilled(b0, b1, IM_COL32(18, 20, 38, 246), 12.0f * k);
        dl->AddRect(b0, b1, IM_COL32(255, 255, 255, 46), 12.0f * k, 0, 1.5f * k);
        // The same teal dot the room badge carries: "the room is live".
        dl->AddCircleFilled(ImVec2(b0.x + 22.0f * k, cy), 6.0f * k, IM_COL32(106, 232, 208, 255));
        float textX = b0.x + 40.0f * k;
        const char* head = "多人游玩 · 列表只读";
        addTextLeft(dl, body, 17.0f * k, ImVec2(textX, cy), IM_COL32(236, 238, 248, 255), head);
        textX += body->CalcTextSizeA(17.0f * k, FLT_MAX, 0.0f, head).x + 14.0f * k;

        // 旁观 / 加入 (right end of the banner): out of this round without
        // leaving the room, so an idle window never blocks the host's start.
        const float pillW = 88.0f * k;
        const float pillH = 30.0f * k;
        const ImVec2 p0(b1.x - pillW - 10.0f * k, cy - pillH * 0.5f);
        const ImVec2 p1(p0.x + pillW, p0.y + pillH);
        const bool spectating = party->spectating;
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID("spectate");
        ImGui::InvisibleButton("spectate", ImVec2(pillW, pillH));
        const bool pillHovered = ImGui::IsItemHovered();
        const bool pillPressed = ImGui::IsItemClicked();
        ImGui::PopID();
        dl->AddRectFilled(p0, p1,
            spectating ? IM_COL32(56, 62, 92, 255) : IM_COL32(32, 36, 60, 255), pillH * 0.5f);
        dl->AddRect(p0, p1,
            spectating ? IM_COL32(255, 255, 255, 60)
                       : (pillHovered ? IM_COL32(255, 255, 255, 90) : IM_COL32(255, 255, 255, 36)),
            pillH * 0.5f, 0, 1.5f * k);
        addTextCentered(dl, body, 15.0f * k, ImVec2((p0.x + p1.x) * 0.5f, cy),
            spectating ? IM_COL32(178, 178, 198, 255) : IM_COL32(236, 238, 248, 255),
            spectating ? "加入" : "旁观");
        if (pillPressed && partyOut != nullptr) {
            ui::se(ui::SeClick);
            partyOut->spectate = true;
        }

        // Room status, between the title and the pill ("等待房主选曲" /
        // "已确定，等待其他玩家" / "本窗口没有这首曲子").
        const float statusMax = p0.x - textX - 16.0f * k;
        if (statusMax > 40.0f * k) {
            const std::string line = ellipsize(body, 15.0f * k, party->status, statusMax);
            addTextLeft(dl, body, 15.0f * k, ImVec2(textX, cy), grayText, line.c_str());
        }
        if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
            std::printf("[ui] room banner %.0f,%.0f - %.0f,%.0f (spectate pill %.0f,%.0f)\n", b0.x,
                b0.y, b1.x, b1.y, (p0.x + p1.x) * 0.5f, cy);
        }
    }

    // Groups matching the search filter. A 多人游玩 member has no search box (it
    // is disabled and covered by the room banner), and a stale filter would be
    // free to hide the very song the room is on - so the filter is ignored
    // there, not just un-editable.
    const bool filterOn = !partyReadOnly && searchBuf[0] != '\0';
    std::vector<int> visible;
    if (!filterOn) {
        for (int gi = 0; gi < static_cast<int>(groups.size()); ++gi) {
            visible.push_back(gi);
        }
    } else {
        const std::string needle = toLower(searchBuf);
        // A query typed without a Japanese IME ("gurume") is folded to kana and
        // matched against the official reading, which is what the grouping and
        // the name sort already use - see romaji_search.hpp. `useKana` keeps
        // that from double-matching a query that is already kana or kanji.
        const std::string kanaNeedle = romaji::toKana(needle);
        const bool useKana = !kanaNeedle.empty() && kanaNeedle != needle;
        for (int gi = 0; gi < static_cast<int>(groups.size()); ++gi) {
            const SongGroup& g = groups[static_cast<size_t>(gi)];
            if (toLower(g.title).find(needle) != std::string::npos
                || toLower(g.artist).find(needle) != std::string::npos) {
                visible.push_back(gi);
                continue;
            }
            // Community alias ("tyw" / "梦开始的地方" / "mmj团歌" / "即刻轮回"):
            // exact match, see aliasMatches(). Kept as its own branch rather
            // than folded into the substring tests - a two-letter alias is not
            // a fragment of the title, and treating it as one would match
            // everything.
            if (aliasMatches(g.musicId, needle)) {
                visible.push_back(gi);
                continue;
            }
            // Original-text match on the reading too: the CN-only songs carry
            // pinyin there (see .workbuddy/tools/update_music_db.py) because the
            // official table has no kana for them, and pinyin does not fold to
            // kana - "yiyang" only finds 「一样」 through this line.
            if (g.kana.find(needle) != std::string::npos
                || (useKana && g.kana.find(kanaNeedle) != std::string::npos)) {
                visible.push_back(gi);
            }
        }
        if (std::find(visible.begin(), visible.end(), groupIndex) == visible.end() && !visible.empty()) {
            groupIndex = visible.front();
        }
    }

    // ------------------------------------------------------------------
    // Song list: cyclic ("endless") list, no scrollbar. It scrolls by moving
    // the content coordinate that sits at the vertical centre of the viewport;
    // the row closest to that centre becomes the selection once the list stops
    // moving (while it moves nothing is highlighted - the official screen
    // behaves the same). Past the last row the list simply continues with the
    // first one, so there is no bottom to hit. Wheel, mouse drag and touch
    // drag all feed the same momentum model.
    // ------------------------------------------------------------------
    const float pitch = 104.0f * k;    // distance between two row centres
    const float compactH = 84.0f * k;  // compact row height
    const float cardH = 128.0f * k;    // expanded (selected) card height
    const float listY = listTop + searchH + 14.0f * k;
    const float listH = h - listY - 24.0f * k;

    static float scroll = 0.0f;       // content y that sits at the view centre
    static float scrollTarget = 0.0f; // where the view is heading
    static float flingVel = 0.0f;     // px/s momentum kept after a release
    static float dragVel = 0.0f;      // smoothed drag velocity (px/s)
    static float dragDistance = 0.0f; // travelled while the pointer is down
    static float dragLastY = 0.0f;
    static int pressSlot = -1000000;  // row (slot) the gesture started on
    static bool dragging = false;
    static bool scrolling = false;    // list is moving => no highlight
    static bool listInit = false;
    static int lastGroup = -1;
    static double lastInputTime = -100.0;
    static std::string lastSignature;
    static float listIn = 1.0f; // list rebuild animation (0 = just rebuilt)
    static std::vector<ListRow> cachedRows;
    static bool rowsBuilt = false;
    // Section jump panel: tapping a section header swaps the list for an index
    // of the sections (tap one to fly there). `indexAnim` drives the open /
    // close transition, `enterAnim` the on-entry animation (the phone slides
    // in from the right like the reference screen).
    static bool indexOpen = false;
    static float indexAnim = 0.0f;
    static float enterAnim = 0.0f;
    static double lastFrameTime = -1.0;
    // Animated height per row slot: the selected row grows into a card and the
    // one it replaced shrinks back, instead of snapping.
    static std::vector<float> slotHeights;

    // Sorting / grouping decides the row layout. Grouping wins over the sort
    // combo because a section's songs have to stay together.
    const int order = groupMode == kGroupDifficulty ? kOrderByDifficulty
        : (groupMode == kGroupReading || groupMode == kGroupInitial)
            ? kOrderByName
            : sortMode;
    const std::string listSignature = std::string(searchBuf) + "|" + std::to_string(order) + "|"
        + std::to_string(groupMode) + "|" + std::to_string(diffIndex) + "|" + std::to_string(entries.size())
        + "|ro" + std::to_string(partyReadOnly ? 1 : 0);
    if (listSignature != lastSignature) {
        lastSignature = listSignature;
        cachedRows = buildRows(groups, visible, entries, diffIndex, order, groupMode);
        listInit = false; // the list changed shape: recentre without gliding
        slotHeights.assign(cachedRows.size(), 0.0f);
        listIn = 0.0f; // and let the new rows rise in (see the vertex pass below)
    }
    // Grouping off means no section headers exist, so the panel has nothing to
    // show any more.
    if (groupMode == kGroupOff) {
        indexOpen = false;
    }

    // Floating shape field: drawn on the parent draw list here, i.e. above the
    // backdrop but below the list child window.
    drawBgShapes(dl, w, h, k, timeSec, scroll);
    const std::vector<ListRow>& rows = cachedRows;
    const int rowCount = static_cast<int>(rows.size());

    ImGui::SetCursorScreenPos(ImVec2(listX, listY));
    ImGui::BeginChild("song_list", ImVec2(listW + 24.0f * k, listH), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* listDl = ImGui::GetWindowDrawList();
    const ImVec2 viewPos = ImGui::GetCursorScreenPos();
    const float rowX = viewPos.x;
    float rowY = viewPos.y;
    const float viewCenterY = viewPos.y + listH * 0.5f;
    const float viewBottom = viewPos.y + listH;

    const ImGuiIO& io = ImGui::GetIO();
    const float frameDt = std::clamp(io.DeltaTime, 0.0005f, 0.05f);
    const float mouseY = io.MousePos.y;

    // A "slot" is a position in the endless row sequence; it wraps every
    // rowCount so the list never ends.
    const auto wrapSlot = [&](int slot) {
        return rowCount > 0 ? ((slot % rowCount) + rowCount) % rowCount : 0;
    };
    const auto slotAtY = [&](float y) {
        return static_cast<int>(std::lround((y - viewCenterY + scroll) / pitch));
    };
    const auto slotIsHeader = [&](int slot) {
        return rowCount <= 0 || rows[static_cast<size_t>(wrapSlot(slot))].header;
    };
    // Nearest slot holding a song: a section header must not become the
    // selection when the list settles with one in the middle.
    const auto nearestSongSlot = [&](int slot) {
        for (int d = 0; d <= rowCount; ++d) {
            if (!slotIsHeader(slot + d)) {
                return slot + d;
            }
            if (!slotIsHeader(slot - d)) {
                return slot - d;
            }
        }
        return slot;
    };
    // Slot showing song group `gi`, taken from the copy closest to the current
    // scroll position so a shuffle never spins through the whole list.
    const auto slotOfGroup = [&](int gi) {
        int item = -1;
        for (int i = 0; i < rowCount; ++i) {
            if (!rows[static_cast<size_t>(i)].header && rows[static_cast<size_t>(i)].group == gi) {
                item = i;
                break;
            }
        }
        if (item < 0) {
            return 0;
        }
        const float period = pitch * static_cast<float>(rowCount);
        const float base = static_cast<float>(item) * pitch;
        return item + static_cast<int>(std::lround((scroll - base) / period)) * rowCount;
    };
    // Same, for an arbitrary row index: the cyclic copy closest to the current
    // position, so a jump never spins the list around.
    const auto nearestSlotOfRow = [&](int rowIndex) {
        if (rowCount <= 0) {
            return 0;
        }
        const float period = pitch * static_cast<float>(rowCount);
        const float base = static_cast<float>(rowIndex) * pitch;
        return rowIndex + static_cast<int>(std::lround((scroll - base) / period)) * rowCount;
    };

    const bool inViewRect = io.MousePos.x >= rowX && io.MousePos.x <= rowX + listW
        && mouseY >= viewPos.y && mouseY <= viewBottom;
    // A touch contact arrives as a synthetic mouse event (SDL's touch->mouse
    // synthesis), so this one path serves mouse and finger alike.
    // While the jump panel is up the list behind it must not react.
    //
    // 多人游玩: a member's list is a read-only mirror of the host's song, not a
    // control surface - gating the one flag that every pointer path below asks
    // for is what turns "no drag" into "no wheel, no tap, no fling" as well.
    const bool listHovered = !partyReadOnly && !indexOpen
        && (dragging || (inViewRect && ImGui::IsWindowHovered()));

    // Intro animation: this function redraws every frame while the Select state
    // is active, so a long gap since the previous frame means we just entered
    // it (fresh start, or coming back from a song) - replay the intro then.
    if (lastFrameTime < 0.0 || static_cast<double>(timeSec) - lastFrameTime > 0.5) {
        enterAnim = 0.0f;
    }
    lastFrameTime = timeSec;
    enterAnim = std::min(1.0f, enterAnim + frameDt / 0.38f);
    const float indexTarget = indexOpen ? 1.0f : 0.0f;
    indexAnim += (indexTarget - indexAnim) * (1.0f - std::exp(-frameDt * 18.0f));
    if (std::fabs(indexTarget - indexAnim) < 0.002f) {
        indexAnim = indexTarget;
    }
    const auto easeOutCubic = [](float t) {
        const float inv = 1.0f - std::clamp(t, 0.0f, 1.0f);
        return 1.0f - inv * inv * inv;
    };
    // Whether the section index was already open when the current mouse gesture
    // started. This is the safe gate for the index panel: a press and its
    // release can land in the SAME frame, and then the list's release-commit
    // and the panel's press handling would both act on one click - a header tap
    // would open the panel AND pick a letter in the same gesture.
    static bool indexOpenAtPress = false;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        indexOpenAtPress = indexOpen;
    }

    if (!listInit && rowCount > 0) {
        if (!rowsBuilt) {
            // Very first layout: start on the first row of the list (the scan
            // order and the sorted order differ, so group 0 is not it) - unless
            // the caller parked the list on a specific song, in which case the
            // group was already resolved and must not be thrown away here.
            rowsBuilt = true;
            if (startGroup < 0) {
                for (const ListRow& r : rows) {
                    if (!r.header) {
                        groupIndex = r.group;
                        break;
                    }
                }
            }
        }
        const int sel = std::max(0, slotOfGroup(groupIndex));
        scroll = scrollTarget = static_cast<float>(sel) * pitch;
        flingVel = 0.0f;
        dragging = false;
        scrolling = false;
        listInit = true;
    }
    const int hoverSlot = listHovered && rowCount > 0 ? slotAtY(mouseY) : 1000000;
    const int hoverRow = hoverSlot < 1000000 ? wrapSlot(hoverSlot) : -1;
    if (rowCount > 0) {
        // Wheel: one notch = one row.
        if (listHovered && io.MouseWheel != 0.0f) {
            ui::se(ui::SeSelect);
            scrollTarget -= io.MouseWheel * pitch;
            flingVel = 0.0f;
            scrolling = true;
            lastInputTime = timeSec;
        }
        // Double click / double tap: play the row straight away.
        if (listHovered && hoverRow >= 0 && !rows[static_cast<size_t>(hoverRow)].header
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const SongGroup& g = groups[static_cast<size_t>(rows[static_cast<size_t>(hoverRow)].group)];
            if (g.idx[diffIndex] >= 0) {
                ui::se(ui::SeClick);
                action = g.idx[diffIndex];
            }
        }
        // Press starts a drag. The row is only committed on release, and only
        // when the gesture did not travel - dragging never selects.
        if (listHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dragging = true;
            dragDistance = 0.0f;
            dragVel = 0.0f;
            flingVel = 0.0f;
            dragLastY = mouseY;
            pressSlot = hoverSlot;
            scrolling = true;
            lastInputTime = timeSec;
        }
        if (dragging) {
            const float dy = mouseY - dragLastY;
            dragLastY = mouseY;
            dragDistance += std::fabs(dy);
            if (dy != 0.0f) {
                scroll -= dy;
                dragVel = dragVel * 0.6f + (-dy / frameDt) * 0.4f;
                lastInputTime = timeSec;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                dragging = false;
                // A quick release keeps its speed (fling); a slow one settles
                // right where it stopped.
                flingVel = std::fabs(dragVel) > 260.0f ? std::clamp(dragVel, -7000.0f, 7000.0f) : 0.0f;
                lastInputTime = timeSec;
                if (dragDistance < 8.0f && pressSlot > -1000000) {
                    if (slotIsHeader(pressSlot)) {
                        // Tapping a section head opens the jump index (tapping
                        // it again closes it) - the official screen turns the
                        // list into a key panel the same way.
                        ui::se(ui::SeClick);
                        indexOpen = !indexOpen;
                        std::printf("[select] section index %s\n", indexOpen ? "opened" : "closed");
                        std::fflush(stdout);
                    } else {
                        // Tap: pick that row (it glides to the centre).
                        if (rows[static_cast<size_t>(wrapSlot(pressSlot))].group != groupIndex) {
                            ui::se(ui::SeSelect);
                        }
                        groupIndex = rows[static_cast<size_t>(wrapSlot(pressSlot))].group;
                        scrollTarget = static_cast<float>(pressSlot) * pitch;
                        scrolling = false;
                    }
                }
                pressSlot = -1000000;
            }
            scrollTarget = scroll;
        } else {
            if (std::fabs(flingVel) > 1.0f) {
                scroll += flingVel * frameDt;
                scrollTarget = scroll;
                flingVel *= std::exp(-frameDt * 9.0f);
                if (std::fabs(flingVel) < 100.0f) {
                    flingVel = 0.0f;
                }
                lastInputTime = timeSec;
            }
            // Nothing moved for a moment: commit the row sitting in the middle
            // (skipping section headers).
            if (flingVel == 0.0f && scrolling && (timeSec - lastInputTime) > 0.20) {
                const int slot = nearestSongSlot(slotAtY(viewCenterY));
                groupIndex = rows[static_cast<size_t>(wrapSlot(slot))].group;
                scrollTarget = static_cast<float>(slot) * pitch;
                scrolling = false;
            }
        }

        if (!dragging) {
            // Glide towards the target (snap / wheel / tap). No clamping: the
            // list is cyclic.
            scroll += (scrollTarget - scroll) * (1.0f - std::exp(-frameDt * 16.0f));
            if (std::fabs(scrollTarget - scroll) < 0.4f) {
                scroll = scrollTarget;
            }
        }
    }

    // A selection made outside the list (shuffle button, arrow keys, the
    // phone panel) glides the list over to that row.
    if (!scrolling && rowCount > 0 && groupIndex != lastGroup) {
        scrollTarget = static_cast<float>(slotOfGroup(groupIndex)) * pitch;
    }
    lastGroup = groupIndex;
    // While the list is moving nothing is highlighted: the row that ends up in
    // the middle is only picked (and highlighted) once it stops.
    const int cardSlot = scrolling || rowCount == 0 ? 1000000 : slotOfGroup(groupIndex);

    const auto levelLabel = [](int level, char* buf, size_t bufSize) -> const char* {
        if (level <= 0) {
            return "-";
        }
        std::snprintf(buf, bufSize, "%d", level);
        return buf;
    };

    int emptyAction = SelectNone;
    if (rowCount == 0) {
        // The list column is narrow (450 virtual units) and these two lines are
        // long, so they wrap with ImGui's own wrapper against an explicit ItemWidth
        // - TextColored does not wrap, which is what used to cut the sentence off
        // at the right edge of the column.
        const auto emptyText = [&](float x, float y, ImVec4 color, const char* text) {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            // Flush the wrapped block against the left edge of the column: the
            // default centering would scatter the two lines against each other.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * k, 4.0f * k));
            ImGui::PushTextWrapPos(x + listW);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::PopStyleVar();
        };
        ImGui::PushFont(body, 19.0f * k);
        emptyText(rowX, rowY + 20.0f * k, ImVec4(0.75f, 0.75f, 0.82f, 1.0f),
            "没有找到谱面。把 .sus 放到 charts/ 目录下，再按 F5 重新扫描（命名规则见 CHARTS.md）。");
        ImGui::PopFont();
        // A dead end otherwise: the player just installed the game and has no
        // charts at all, so offer the bundled downloader right here instead of
        // making them find chartdl.exe in the folder by hand. The host launches
        // it (main.cpp polls the process and re-scans when it exits).
        ImGui::SetCursorScreenPos(ImVec2(rowX, rowY + 78.0f * k));
        ImGui::PushFont(body, 19.0f * k);
        if (ui::capsuleButton("下载谱面（chartdl）", ImVec2(268.0f * k, 46.0f * k), true)) {
            emptyAction = SelectDownload;
            std::printf("[select] 下载谱面 pressed\n");
            std::fflush(stdout);
        }
        ImGui::PopFont();
        ImGui::PushFont(body, 17.0f * k);
        emptyText(rowX, rowY + 138.0f * k, ImVec4(0.6f, 0.6f, 0.68f, 1.0f),
            "会打开独立的下载器；关掉它之后这里会自动重新扫描。");
        ImGui::PopFont();
    }

    const GLuint backTex = selectTex(renderer, "indicate_back_new");
    const GLuint clearTex = selectTex(renderer, "clear_indicate");
    const GLuint fcTex = selectTex(renderer, "fullcombo_indicate");
    // "歌曲等级" caption plate (128x48 sprite from assets/select).
    const GLuint levelTex = selectTex(renderer, "songlevel");

    // Only the slots that can be on screen are walked.
    const int firstSlot = rowCount > 0 ? slotAtY(viewPos.y) - 1 : 0;
    const int lastSlot = rowCount > 0 ? slotAtY(viewBottom) + 1 : -1;
    // The leading level indicator follows the selected difficulty instead of
    // always being pink.
    const ImU32 diffColor = kDiffColors[static_cast<size_t>(std::clamp(diffIndex, 0, kDiffCount - 1))];

    // Everything the row loop adds is captured so it can fade out while the
    // section index is up - the letters sit on a cleared area, not on top of
    // the song rows.
    const int listVtxFirst = listDl->VtxBuffer.Size;
    for (int slot = firstSlot; slot <= lastSlot; ++slot) {
        const ListRow& view = rows[static_cast<size_t>(wrapSlot(slot))];
        const float centerY = viewCenterY + (static_cast<float>(slot) * pitch - scroll);
        if (view.header) {
            // Section header: a caption with a rule running to the right, so a
            // grouped list reads like "あ ---" / "16-20 ---". It doubles as the
            // button that turns the list into the jump panel, so it lights up
            // on hover and carries a small chevron.
            ImFont* headFont = title != nullptr ? title : body;
            const float headSize = 22.0f * k;
            const bool hovered = indexAnim < 0.5f && slot == hoverSlot;
            // Same eased hover as the song rows, so the plate, the left marker bar
            // and the chevron all come up together.
            const float headHot = ui::anim(0x4a551000u + static_cast<ImGuiID>(slot + 0x100000), hovered, 18.0f);
            const float headX = rowX + 30.0f * k;
            if (headHot > 0.01f) {
                listDl->AddRectFilled(ImVec2(rowX + 6.0f * k, centerY - pitch * 0.34f),
                    ImVec2(rowX + listW, centerY + pitch * 0.34f),
                    IM_COL32(255, 255, 255, static_cast<int>(26.0f * headHot)), 8.0f * k);
                listDl->AddRectFilled(ImVec2(rowX, centerY - pitch * 0.30f), ImVec2(rowX + 3.0f * k, centerY + pitch * 0.30f),
                    IM_COL32(255, 255, 255, static_cast<int>(220.0f * headHot)), 2.0f * k);
            }
            listDl->AddText(headFont, headSize, ImVec2(headX, centerY - headSize * 0.6f),
                ui::mix(IM_COL32(255, 255, 255, 210), IM_COL32(255, 255, 255, 255), headHot), view.label.c_str());
            const float labelW = headFont->CalcTextSizeA(headSize, FLT_MAX, 0.0f, view.label.c_str()).x;
            const float chevX = headX + labelW + 12.0f * k;
            const float chevA = 110.0f + 110.0f * headHot;
            listDl->AddTriangleFilled(ImVec2(chevX, centerY - 6.0f * k),
                ImVec2(chevX + 11.0f * k, centerY - 6.0f * k), ImVec2(chevX + 5.5f * k, centerY + 4.0f * k),
                IM_COL32(255, 255, 255, static_cast<int>(chevA)));
            const float lineX = chevX + 18.0f * k;
            const float lineY = centerY;
            if (lineX < rowX + listW - 8.0f * k) {
                listDl->AddLine(ImVec2(lineX, lineY), ImVec2(rowX + listW - 8.0f * k, lineY),
                    IM_COL32(255, 255, 255, 70), 2.0f * k);
            }
            continue;
        }
        const int gi = view.group;
        const SongGroup& g = groups[static_cast<size_t>(gi)];
        // While the list is moving nothing is highlighted; the row that ends
        // up in the middle is picked once it stops.
        const bool isSel = slot == cardSlot;
        // Animated height: a row grows into a card instead of snapping, and the
        // one the selection left shrinks back.
        float& rowHAnim = slotHeights[static_cast<size_t>(wrapSlot(slot))];
        if (rowHAnim <= 0.0f) {
            rowHAnim = compactH; // first time this slot is on screen
        }
        rowHAnim += ((isSel ? cardH : compactH) - rowHAnim) * (1.0f - std::exp(-frameDt * 16.0f));
        const float rowH = rowHAnim;
        const ImVec2 p0(rowX, centerY - rowH * 0.5f);
        const ImVec2 p1(rowX + listW, centerY + rowH * 0.5f);
        // Clear / full-combo mark of one diamond slot (0 = nothing to draw).
        const auto badgeFor = [&](int d) -> GLuint {
            if (d < 0 || d >= kDiffCount || g.idx[d] < 0) {
                return 0;
            }
            const ChartEntry& e = entries[static_cast<size_t>(g.idx[d])];
            if (e.fullCombo) {
                return fcTex;
            }
            return e.cleared ? clearTex : 0;
        };

        // The entry this row currently points at (jacket / MV tag / diamonds).
        const int rowIdx = g.idx[diffIndex] >= 0 ? g.idx[diffIndex]
                                                 : (g.idx[3] >= 0 ? g.idx[3]
                                                                  : (g.idx[4] >= 0 ? g.idx[4] : g.idx[0]));
        const ChartEntry* rowEntry = rowIdx >= 0 ? &entries[static_cast<size_t>(rowIdx)] : nullptr;
        // Level of the *selected* difficulty (official table when this song
        // has no chart for it), so switching difficulty renumbers the list.
        char levelBuf[16];
        const char* rowLevel = levelLabel(levelForDifficulty(g, entries, diffIndex), levelBuf, sizeof(levelBuf));

        if (isSel) {
            // Expanded card: a translucent white rounded rectangle over the
            // list background, with the level badge, credits and diamonds.
            listDl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 190), 10.0f * k);
            listDl->AddRect(p0, p1, IM_COL32(255, 255, 255, 120), 10.0f * k, 0, 2.0f);

            // Level badge: the level number of the selected difficulty in a
            // filled circle, with the "歌曲等级" caption plate resting on the
            // circle's upper edge (as in the official list).
            const float badgeL = p0.x + 14.0f * k;
            const float tagY = p0.y + 22.0f * k;
            const float tagW = 62.0f * k;
            const float tagH = tagW * 48.0f / 128.0f;
            const float ccx = badgeL + tagW * 0.5f;
            const float ccy = tagY + tagH + 16.0f * k;
            listDl->AddCircleFilled(ImVec2(ccx, ccy), 27.0f * k, diffColor);
            addTextCentered(listDl, title, 28.0f * k, ImVec2(ccx, ccy - 1.0f * k), white, rowLevel);
            if (levelTex != 0) {
                listDl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(levelTex)),
                    ImVec2(badgeL, tagY), ImVec2(badgeL + tagW, tagY + tagH),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));
            } else {
                // Fallback when the sprite is missing: the drawn plate.
                listDl->AddRectFilled(ImVec2(badgeL, tagY), ImVec2(badgeL + tagW, tagY + 22.0f * k),
                    diffColor, 4.0f * k);
                addTextCentered(listDl, body, 13.0f * k, ImVec2(ccx, tagY + 11.0f * k), white, "歌曲等级");
            }

            const float textL = p0.x + 96.0f * k;
            const std::string vo = g.vocal.empty() ? std::string() : "Vo. " + g.vocal;
            if (title != nullptr) {
                listDl->AddText(title, 26.0f * k, ImVec2(textL, p0.y + 14.0f * k), IM_COL32(60, 60, 80, 255),
                    g.title.c_str());
            }
            listDl->AddText(body, 16.0f * k, ImVec2(textL, p0.y + 48.0f * k), IM_COL32(120, 120, 140, 255),
                g.artist.c_str());
            if (!vo.empty()) {
                listDl->AddText(body, 15.0f * k, ImVec2(textL, p0.y + 70.0f * k), IM_COL32(140, 140, 160, 255),
                    vo.c_str());
            }
            float dx = textL + 4.0f * k;
            const float dy = p1.y - 18.0f * k;
            for (int d = 0; d < kDiffCount; ++d) {
                addDiamondSlot(listDl, backTex, ImVec2(dx, dy), 20.0f * k, g.idx[d] >= 0, badgeFor(d));
                dx += 20.0f * k;
            }
        } else {
            // Compact row: no card fill - the entries are separated by a thin
            // translucent rule, like the reference UI. The rule under the row
            // above the card would land inside the card, so it is skipped.
            // Hover tint eases in - and, keyed by the slot, eases out again on the
            // row the pointer just left instead of vanishing.
            const float rowHot = ui::anim(0x4a552000u + static_cast<ImGuiID>(slot + 0x100000),
                slot == hoverSlot, 20.0f);
            if (rowHot > 0.01f) {
                listDl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, static_cast<int>(20.0f * rowHot)),
                    8.0f * k);
            }
            if (slot != cardSlot - 1) {
                const float ruleY = centerY + pitch * 0.5f;
                listDl->AddLine(ImVec2(p0.x, ruleY), ImVec2(p1.x, ruleY), IM_COL32(255, 255, 255, 48), 1.0f * k);
            }

            const float cy = centerY;
            listDl->AddCircleFilled(ImVec2(p0.x + 30.0f * k, cy), 24.0f * k, diffColor);
            addTextCentered(listDl, body, 20.0f * k, ImVec2(p0.x + 30.0f * k, cy - 1.0f * k), white, rowLevel);

            const float jx = p0.x + 62.0f * k;
            const float js = 60.0f * k;
            addJacket(listDl, thumbFor(renderer, g.coverPath), ImVec2(jx, cy - js * 0.5f),
                ImVec2(jx + js, cy + js * 0.5f), 6.0f * k);

            const float textL = jx + js + 14.0f * k;
            listDl->AddText(body, 20.0f * k, ImVec2(textL, cy - 26.0f * k), white, g.title.c_str());
            float dx = textL + 4.0f * k;
            for (int d = 0; d < kDiffCount; ++d) {
                addDiamondSlot(listDl, backTex, ImVec2(dx, cy + 6.0f * k), 18.0f * k, g.idx[d] >= 0, badgeFor(d));
                dx += 18.0f * k;
            }

            // Optional 2D / 3D MV tag from the sidecar.
            if (rowEntry != nullptr && !rowEntry->mv.empty()) {
                const std::string& mv = rowEntry->mv;
                const ImVec2 ts = ImGui::CalcTextSize(mv.c_str());
                const float bw = ts.x + 16.0f * k;
                const float bx = p1.x - bw - 12.0f * k;
                const float by = p1.y - 30.0f * k;
                const bool is3d = mv.find('3') != std::string::npos;
                listDl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 22.0f * k),
                    is3d ? IM_COL32(242, 200, 70, 255) : IM_COL32(90, 140, 255, 255), 4.0f * k);
                listDl->AddText(body, 14.0f * k, ImVec2(bx + 8.0f * k, by + 3.0f * k),
                    IM_COL32(40, 36, 20, 255), mv.c_str());
            }
        }
    }

    // ------------------------------------------------------------------
    // List rebuild (search / sort / grouping change) and the section index both
    // work by rewriting the vertices the list just produced. The rebuild case
    // makes the new rows rise into place and fade up, so filtering reads as "the
    // list settles" instead of a hard swap.
    listIn = std::min(1.0f, listIn + frameDt / 0.20f);
    const float listEase = easeOutCubic(listIn);
    const float listRise = (1.0f - listEase) * 12.0f * k;
    const int listFade = static_cast<int>(std::clamp(listEase, 0.0f, 1.0f) * 255.0f);
    if (indexAnim > 0.0f || listFade < 255) {
        const int fade = static_cast<int>(std::clamp(1.0f - indexAnim, 0.0f, 1.0f) * 255.0f);
        for (int i = listVtxFirst; i < listDl->VtxBuffer.Size; ++i) {
            if (listRise > 0.0f) {
                listDl->VtxBuffer[i].pos.y += listRise;
            }
            ImU32& col = listDl->VtxBuffer[i].col;
            const ImU32 a = (col >> IM_COL32_A_SHIFT) & 0xFF;
            const ImU32 cut = static_cast<ImU32>(std::min(fade, listFade));
            col = (col & ~IM_COL32_A_MASK) | ((a * cut / 255u) << IM_COL32_A_SHIFT);
        }
    }

    // ------------------------------------------------------------------
    // Section jump index: replaces the list while it is open. Deliberately
    // bare - no plate, no title, no close button - just the section initials
    // (the header labels, which is why tapping a header opens it). Tapping a
    // letter flies the list there; tapping anywhere else restores the list.
    // The letters scatter outward while fading in and gather back on the way
    // out (one curve drives both directions).
    // ------------------------------------------------------------------
    if (indexAnim > 0.002f && rowCount > 0) {
        struct SectionKey
        {
            std::string label;
            int headerRow = 0;
            int group = -1;
        };
        std::vector<SectionKey> keys;
        for (int i = 0; i < rowCount; ++i) {
            if (!rows[static_cast<size_t>(i)].header) {
                continue;
            }
            int firstGroup = -1;
            if (i + 1 < rowCount && !rows[static_cast<size_t>(i + 1)].header) {
                firstGroup = rows[static_cast<size_t>(i + 1)].group;
            }
            keys.push_back({rows[static_cast<size_t>(i)].label, i, firstGroup});
        }

        const float t = easeOutCubic(indexAnim);
        const float panelW = listW + 24.0f * k;
        const float panelH = listH;
        const ImVec2 panelC(viewPos.x + panelW * 0.5f, viewPos.y + panelH * 0.5f);

        // Hit-testing is done by hand, exactly like the list rows: an
        // InvisibleButton over the whole area would claim the press before the
        // letters (ImGui locks the active id on mouse-down, so anything
        // submitted after it never sees the click). Interaction only counts
        // once the panel is really up - a click during the fade must fall
        // through to the list instead of picking a letter that is flying in.
        const bool interactive = indexOpenAtPress && indexAnim > 0.65f;
        const ImVec2 mousePos = ImGui::GetIO().MousePos;
        const bool clicked = interactive && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool hitLetter = false;

        // Which section the list is sitting on, so its letter can be marked.
        std::string currentLabel;
        if (cardSlot < 1000000) {
            for (int i = wrapSlot(cardSlot); i >= 0; --i) {
                if (rows[static_cast<size_t>(i)].header) {
                    currentLabel = rows[static_cast<size_t>(i)].label;
                    break;
                }
            }
        }

        const int keyCount = static_cast<int>(keys.size());
        const int cols = std::clamp(static_cast<int>(panelW / (96.0f * k)), 3, 8);
        const int gridRows = std::max(1, (keyCount + cols - 1) / cols);
        const float cellW = panelW / static_cast<float>(cols);
        // Keep the whole grid inside the panel however many sections there are.
        const float cellH = std::clamp(panelH / static_cast<float>(gridRows + 1), 44.0f * k, 96.0f * k);
        const float gridH = cellH * static_cast<float>(gridRows);
        const float gridTop = panelC.y - gridH * 0.5f;
        const float spread = std::min(panelW, panelH) * 0.42f; // scatter distance

        for (int i = 0; i < keyCount; ++i) {
            const int col = i % cols;
            const int rowI = i / cols;
            const float cellCx = viewPos.x + cellW * (static_cast<float>(col) + 0.5f);
            const float cellCy = gridTop + cellH * (static_cast<float>(rowI) + 0.5f);
            if (cellCy > viewPos.y + panelH) {
                break; // no room left for further rows
            }
            // Per-letter timing: the ones further from the middle arrive a
            // touch later, which reads as the group gathering into place.
            const float dx0 = cellCx - panelC.x;
            const float dy0 = cellCy - panelC.y;
            const float dist = std::sqrt(dx0 * dx0 + dy0 * dy0);
            const float stagger = 0.35f * std::clamp(dist / std::max(1.0f, spread), 0.0f, 1.0f);
            const float lt = std::clamp((t - stagger * (1.0f - t)) / std::max(0.15f, 1.0f - stagger * (1.0f - t)),
                0.0f, 1.0f);
            // Outward scatter: at lt = 0 the letter sits far from the centre and
            // slides in; on the way out (t shrinking) it flies back out again.
            const float push = (1.0f - lt) * spread;
            const float dlen = std::max(1.0f, dist);
            const ImVec2 pos(cellCx + dx0 / dlen * push, cellCy + dy0 / dlen * push);

            // Half-open rect (max exclusive): the cells tile the area, so a
            // click exactly on a shared edge must belong to ONE cell only -
            // with <= on both ends two letters were picked in the same frame
            // and the list jumped to the wrong section.
            const bool hovered = mousePos.x >= cellCx - cellW * 0.5f && mousePos.x < cellCx + cellW * 0.5f
                && mousePos.y >= cellCy - cellH * 0.5f && mousePos.y < cellCy + cellH * 0.5f;
            if (hovered) {
                hitLetter = true;
            }

            const bool active = keys[static_cast<size_t>(i)].label == currentLabel;
            ImFont* letterFont = title != nullptr ? title : body;
            // Hover eases the letter up in size and brightness; the section the
            // list is currently in stays hard on even when the pointer is away.
            const float hot = ui::anim(0x4a553000u + static_cast<ImGuiID>(i), hovered, 20.0f);
            const float on = active ? 1.0f : hot;
            const float size = (active ? 34.0f : 30.0f) * k * (0.88f + 0.12f * lt) * (1.0f + 0.06f * hot);
            const int alpha = static_cast<int>((205.0f + 50.0f * on) * lt);
            addTextCentered(listDl, letterFont, size, pos,
                ui::mix(IM_COL32(232, 232, 244, alpha), IM_COL32(255, 255, 255, alpha), on),
                keys[static_cast<size_t>(i)].label.c_str());
            if (active) {
                // A short bar under the current section instead of a box.
                const float half = size * 0.34f;
                listDl->AddRectFilled(ImVec2(pos.x - half, pos.y + size * 0.62f),
                    ImVec2(pos.x + half, pos.y + size * 0.62f + 3.0f * k),
                    IM_COL32(255, 255, 255, static_cast<int>(230.0f * lt)), 1.5f * k);
            }

            if (hitLetter && hovered && clicked && keys[static_cast<size_t>(i)].group >= 0) {
                // Land with this section's first song in the middle, so its
                // header ends up one row above.
                ui::se(ui::SeSelect);
                const int slot = nearestSlotOfRow(keys[static_cast<size_t>(i)].headerRow + 1);
                groupIndex = keys[static_cast<size_t>(i)].group;
                scrollTarget = static_cast<float>(slot) * pitch;
                flingVel = 0.0f;
                dragVel = 0.0f;
                scrolling = false;
                lastInputTime = timeSec;
                indexOpen = false;
                std::printf("[select] jump to section '%s'\n", keys[static_cast<size_t>(i)].label.c_str());
                std::fflush(stdout);
            }
        }

        // A click that was not on a letter restores the list (the letters are
        // not ImGui items, so this is the only place that can decide).
        if (clicked && !hitLetter) {
            indexOpen = false;
            std::puts("[select] index closed (blank area)");
            std::fflush(stdout);
        }
    }


    // Keyboard navigation over the (cyclically) listed rows. A 多人游玩 member
    // has no say over the list (the room's song is what it shows), so nothing
    // here moves it - but 确定 is still theirs to press, and that is the whole
    // point of the read-only screen.
    if (rowCount > 0) {
        const auto moveTo = [&](int step) {
            int slot = (scrolling || cardSlot >= 1000000 ? slotOfGroup(groupIndex) : cardSlot) + step;
            for (int guard = 0; guard <= rowCount && slotIsHeader(slot); ++guard) {
                slot += step; // section headers are not selectable
            }
            groupIndex = rows[static_cast<size_t>(wrapSlot(slot))].group;
            scrollTarget = static_cast<float>(slot) * pitch;
            flingVel = 0.0f;
            scrolling = false;
            lastInputTime = timeSec;
        };
        if (!partyReadOnly
            && (ImGui::IsKeyPressed(ImGuiKey_DownArrow)
                || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown))) {
            ui::se(ui::SeSelect);
            moveTo(1);
        }
        if (!partyReadOnly
            && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp))) {
            ui::se(ui::SeSelect);
            moveTo(-1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            if (partyReadOnly) {
                if (partyOut != nullptr) {
                    partyOut->confirm = true;
                }
            } else if (selected >= 0) {
                action = selected;
            }
        }
    }

    // 多人游玩: the list is already drawn - a translucent veil over the whole
    // viewport says "you cannot work this" far more clearly than dimming the
    // rows one by one would, and it is the honest picture: the highlight, the
    // scroll position and the section index all belong to the host now.
    // Drawn into the child's own draw list, which is rendered *after* its
    // parent's: anything put on `dl` here would end up under the rows.
    if (partyReadOnly) {
        const ImVec2 v0(rowX, viewPos.y);
        const ImVec2 v1(rowX + listW + 24.0f * k, viewBottom);
        listDl->AddRectFilled(v0, v1, IM_COL32(8, 10, 22, 130));
        const float chipH = 30.0f * k;
        const float chipW = 150.0f * k;
        const ImVec2 c0((v0.x + v1.x) * 0.5f - chipW * 0.5f, v0.y + 14.0f * k);
        const ImVec2 c1(c0.x + chipW, c0.y + chipH);
        listDl->AddRectFilled(c0, c1, IM_COL32(14, 16, 30, 210), chipH * 0.5f);
        listDl->AddRect(c0, c1, IM_COL32(255, 255, 255, 40), chipH * 0.5f, 0, 1.5f * k);
        addTextCentered(listDl, body, 15.0f * k, ImVec2((c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f),
            IM_COL32(200, 204, 224, 255), "只读 · 由房主选曲");
    }
    ImGui::EndChild();

    // ------------------------------------------------------------------
    // Right: tilted-phone panel (img_smartphone.png) with the jacket, the
    // difficulty buttons and 确定 / shuffle / settings. The whole block is
    // laid out flat and then rotated about the phone's centre, like the
    // reference UI.
    // ------------------------------------------------------------------
    const float phoneAspect = 1034.0f / 1942.0f;
    float phoneH = (h - 24.0f * kBase) * scale;
    float phoneW = phoneH * phoneAspect;
    if (phoneW > w * 0.46f * scale) {
        phoneW = w * 0.46f * scale;
        phoneH = phoneW / phoneAspect;
    }
    const float phoneX = w - phoneW - 30.0f * k;
    // Nudged down on purpose: in the reference UI the phone runs off the
    // bottom of the screen (its home bar is never visible), which also buys
    // the content room for the vocal chip row.
    const float phoneY = (h - phoneH) * 0.5f + 78.0f * k;

    // Tilt: -5 degrees, i.e. the right edge rides up (screen y grows down).
    constexpr float kTiltDeg = -5.0f;
    const float tiltRad = kTiltDeg * 3.14159265358979f / 180.0f;
    const ImVec2 tiltPivot(phoneX + phoneW * 0.5f, phoneY + phoneH * 0.5f);
    const float tiltCos = std::cos(tiltRad);
    const float tiltSin = std::sin(tiltRad);
    const auto tiltPoint = [&](const ImVec2& p) {
        const float dx = p.x - tiltPivot.x;
        const float dy = p.y - tiltPivot.y;
        return ImVec2(tiltPivot.x + dx * tiltCos - dy * tiltSin, tiltPivot.y + dx * tiltSin + dy * tiltCos);
    };
    // Top-left corner for an InvisibleButton of `size` that should sit where a
    // tilted item is drawn (hitboxes cannot rotate, so they follow the centre).
    const auto tiltedItemPos = [&](const ImVec2& center, const ImVec2& size) {
        const ImVec2 c = tiltPoint(center);
        return ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.5f);
    };
    // Every vertex added from here on belongs to the phone; the pass at the
    // bottom of this function tilts them all at once.
    const int phoneVtxFirst = dl->VtxBuffer.Size;

    const GLuint phoneTex = selectTex(renderer, "img_smartphone");
    if (phoneTex != 0) {
        dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(phoneTex)),
            ImVec2(phoneX, phoneY), ImVec2(phoneX + phoneW, phoneY + phoneH));
    } else {
        dl->AddRectFilledMultiColor(ImVec2(phoneX, phoneY), ImVec2(phoneX + phoneW, phoneY + phoneH),
            IM_COL32(56, 48, 98, 255), IM_COL32(38, 32, 72, 255), IM_COL32(30, 26, 58, 255),
            IM_COL32(46, 40, 84, 255));
    }

    // Screen area inside the phone frame (bezel is baked into the image).
    const float scrX0 = phoneX + phoneW * 0.034f;
    const float scrX1 = phoneX + phoneW * 0.966f;
    const float scrY0 = phoneY + phoneH * 0.030f;
    const float scrY1 = phoneY + phoneH * 0.972f;
    const float cx = (scrX0 + scrX1) * 0.5f;
    const float sw = scrX1 - scrX0;

    // Vertex index where the content block starts (jacket, metadata, difficulty
    // pads, rank badge). The frame drawn above stays outside it, so a song change
    // can animate the content while the phone itself holds still.
    const int phoneContentVtxFirst = dl->VtxBuffer.Size;

    if (!groups.empty() && selected >= 0) {
        const ChartEntry& item = entries[static_cast<size_t>(selected)];
        const SongGroup& group = groups[static_cast<size_t>(groupIndex)];

        // Jacket (top of the phone screen)
        const float jw = sw * 0.62f;
        const float jh = jw;
        const ImVec2 j0(cx - jw * 0.5f, scrY0 + phoneH * 0.020f);
        addJacket(dl, renderer.cover() != nullptr ? renderer.cover()->id : thumbFor(renderer, group.coverPath),
            j0, ImVec2(j0.x + jw, j0.y + jh), 12.0f * k);
        dl->AddRect(j0, ImVec2(j0.x + jw, j0.y + jh), IM_COL32(255, 255, 255, 60), 12.0f * k, 0, 2.0f);

        // Title / artist / vocal. Everything below is laid out in flow: the
        // vocal chip row is conditional, and the old fixed fractions let the
        // difficulty circles land on top of it.
        //
        // The metadata is left-aligned to the panel's content box (the same box
        // the difficulty row below occupies) and the song's best-score rank
        // badge sits on the right of the block - that is how the reference
        // phone panel lays this area out. The lines are clipped to the room
        // left over for them so they can never run under the badge.
        //
        // Panel content width: the difficulty pads below use it, and the
        // credits / chips have to stay inside it (a 5-singer セカイver line
        // is easily wider than the phone screen).
        const float panelW = 336.0f * k;
        const float contentL = cx - panelW * 0.5f;
        const float contentR = cx + panelW * 0.5f;
        const float badgeD = 64.0f * k;
        const float textMaxW = contentR - contentL - badgeD - 12.0f * k;

        float ty = j0.y + jh + phoneH * 0.022f;
        const float metaTop = ty;
        if (title != nullptr) {
            const std::string text = ellipsize(title, 26.0f * k, group.title, textMaxW);
            addTextLeft(dl, title, 26.0f * k, ImVec2(contentL, ty + 13.0f * k), white, text.c_str());
        }
        ty += 46.0f * k;
        if (!item.artist.empty()) {
            const std::string text = ellipsize(body, 17.0f * k, item.artist, textMaxW);
            addTextLeft(dl, body, 17.0f * k, ImVec2(contentL, ty), grayText, text.c_str());
            ty += 31.0f * k;
        }
        // Vocal versions: only when the song actually ships more than one
        // version's audio next to the chart. The chip row sits between the
        // credits and the difficulty pads; the "Vo." line follows the chosen
        // chip (that is the whole point of the switch).
        {
            const std::vector<VocalVersion> versions = availableVocals(item);
            static std::map<int, int> vocalChoiceBySong; // musicId -> index
            // The caller's value wins when it is valid for this song, so the
            // version can be driven from outside (--select-vocal, a hand-off,
            // or a test); otherwise fall back to what the player last picked
            // here, then to the セカイver.
            int choice = defaultVocalIndex(item.musicId, versions);
            if (vocalIndex >= 0 && vocalIndex < static_cast<int>(versions.size())) {
                choice = vocalIndex;
                vocalChoiceBySong[item.musicId] = choice;
            } else if (const auto it = vocalChoiceBySong.find(item.musicId); it != vocalChoiceBySong.end()) {
                choice = it->second;
            }
            // The panel shows the *chosen* version's singers, not the sidecar's
            // (that one is the sekai ver by construction).
            std::string vocalText = item.vocal;
            if (!versions.empty() && choice >= 0 && choice < static_cast<int>(versions.size())) {
                vocalIndex = choice;
                const VocalVersion& picked = versions[static_cast<size_t>(choice)];
                if (!picked.singers.empty()) {
                    vocalText.clear();
                    for (const std::string& singer : picked.singers) {
                        if (!vocalText.empty()) {
                            vocalText += "、";
                        }
                        vocalText += singer;
                    }
                }
            }

            // The "Vo." line shrinks first and is then ellipsized, so a
            // 5-singer セカイver line still fits next to the badge.
            if (!vocalText.empty()) {
                std::string vo = "Vo. " + vocalText;
                float voSize = 15.0f * k;
                ImVec2 ts = body->CalcTextSizeA(voSize, FLT_MAX, 0.0f, vo.c_str());
                if (ts.x > textMaxW) {
                    voSize *= textMaxW / ts.x;
                }
                vo = ellipsize(body, voSize, vo, textMaxW);
                addTextLeft(dl, body, voSize, ImVec2(contentL, ty), grayText, vo.c_str());
                ty += 29.0f * k;
            }
            // Best-score rank badge: right-aligned to the content box and
            // centred on the metadata block that just ended. In the reference
            // it straddles the artist and Vo. rows, which is where centring on
            // the whole block lands anyway. Clamped so it can never reach down
            // into the vocal chip row (only possible for a song that has no
            // artist and no singers line).
            const float blockMid = (metaTop + ty) * 0.5f;
            const float badgeCy = versions.size() > 1
                ? std::min(blockMid, ty - badgeD * 0.5f - 4.0f * k)
                : blockMid;
            drawBestScoreBadge(dl, renderer, body, item.bestScore, chartRatingFor(item), badgeD,
                contentR - badgeD * 0.5f, badgeCy);

            if (versions.size() > 1) {
                // Chip row, centred. Width comes from the label so "バーチャル"
                // and "花里みのり" both look right.
                float fontSize = 14.0f * k;
                float chipH = 26.0f * k;
                float pad = 12.0f * k;
                float gap = 6.0f * k;
                std::vector<float> widths;
                const auto measure = [&]() {
                    widths.clear();
                    float sum = 0.0f;
                    for (const VocalVersion& version : versions) {
                        const std::string label = vocalShortLabel(version);
                        const ImVec2 ts = body->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
                        widths.push_back(ts.x + pad * 2.0f);
                        sum += widths.back();
                    }
                    return sum + gap * static_cast<float>(versions.size() - 1);
                };
                float total = measure();
                if (total > panelW) {
                    // Shrink the whole row (font, padding and gaps together) so
                    // four chips still fit the phone screen.
                    const float scale = std::clamp(panelW / total, 0.62f, 1.0f);
                    fontSize *= scale;
                    chipH *= scale;
                    pad *= scale;
                    gap *= scale;
                    total = measure();
                }
                float chipX = cx - total * 0.5f;
                const float chipCy = ty + chipH * 0.5f + 2.0f * k;
                for (size_t i = 0; i < versions.size(); ++i) {
                    const std::string label = vocalShortLabel(versions[i]);
                    const ImVec2 lo(chipX, chipCy - chipH * 0.5f);
                    const ImVec2 hi(chipX + widths[i], chipCy + chipH * 0.5f);
                    ImGui::SetCursorScreenPos(tiltedItemPos(ImVec2((lo.x + hi.x) * 0.5f, chipCy),
                        ImVec2(widths[i], chipH)));
                    ImGui::PushID(static_cast<int>(i) + 9000);
                    ImGui::InvisibleButton("vocal", ImVec2(widths[i], chipH));
                    const bool clicked = ImGui::IsItemClicked();
                    ImGui::PopID();
                    const bool active = static_cast<int>(i) == (choice < 0 ? 0 : choice);
                    if (clicked) {
                        ui::se(ui::SeClick);
                        vocalChoiceBySong[item.musicId] = static_cast<int>(i);
                        vocalIndex = static_cast<int>(i);
                    }
                    dl->AddRectFilled(lo, hi, active ? ui::kPrimary : IM_COL32(255, 255, 255, 34),
                        chipH * 0.5f);
                    addTextCentered(dl, body, fontSize, ImVec2((lo.x + hi.x) * 0.5f, chipCy),
                        active ? ui::kBtnText : IM_COL32(238, 238, 248, 235), label.c_str());
                    chipX += widths[i] + gap;
                }
                ty += chipH + 16.0f * k;
            }
        }

        // Difficulty buttons. 多人游玩: this row is the member's difficulty
        // picker - it *is* the room screen the panel replaces - so the filled
        // ring follows the player's own pick instead of the list cursor, and a
        // click is reported to the caller rather than moving the (read-only)
        // list. Only the five difficulties the list can name are offered.
        const bool partyPick = partyReadOnly;
        const float dcD = 60.0f * k;
        const float dcGap = 9.0f * k;
        const float rowW = kDiffCount * dcD + (kDiffCount - 1) * dcGap;
        float dx = cx - rowW * 0.5f + dcD * 0.5f;
        // Anchored to the content flow (with the old fraction as the floor for
        // songs without a chip row), so the chips can never overlap it.
        const float dcY = std::max(scrY0 + phoneH * 0.500f, ty + dcD * 0.5f + 34.0f * k);
        for (int d = 0; d < kDiffCount; ++d) {
            const bool avail = group.idx[d] >= 0;
            const bool active = avail && (partyPick ? party->myDifficulty == d : d == diffIndex);
            const ImVec2 c(dx, dcY);

            ImGui::SetCursorScreenPos(tiltedItemPos(c, ImVec2(dcD, dcD)));
            ImGui::PushID(d);
            if (avail && (!partyPick || partyPickable)) {
                ImGui::InvisibleButton("diff", ImVec2(dcD, dcD));
                if (ImGui::IsItemClicked()) {
                    ui::se(ui::SeLevelChoose);
                    if (partyPick && partyOut != nullptr) {
                        partyOut->difficulty = d;
                    }
                    diffIndex = d;
                    selected = group.idx[d];
                }
            } else {
                ImGui::Dummy(ImVec2(dcD, dcD));
            }
            ImGui::PopID();

            if (active) {
                dl->AddCircleFilled(c, dcD * 0.5f, kDiffColors[d]);
                dl->AddCircle(c, dcD * 0.5f + 2.5f * k, white, 48, 3.0f * k);
            } else {
                // Unselected difficulties are hollow rings - no dark fill, just
                // the difficulty colour (dimmed for the ones this song lacks).
                dl->AddCircle(c, dcD * 0.5f, avail ? kDiffColors[d] : IM_COL32(150, 145, 175, 110), 48, 3.0f * k);
            }
            char lvBuf[16];
            const char* lvText = "-";
            if (avail) {
                lvText = levelText(entries[static_cast<size_t>(group.idx[d])], lvBuf, sizeof(lvBuf));
            }
            addTextCentered(dl, body, 24.0f * k, c, white, lvText);
            addTextCentered(dl, body, 15.0f * k, ImVec2(dx, dcY + dcD * 0.5f + 16.0f * k),
                active ? white : (avail ? kDiffColors[d] : IM_COL32(130, 126, 156, 255)), kDiffNames[d]);
            dx += dcD + dcGap;
        }

        // 确定 button (mint capsule, drawn by hand so it tilts with the phone).
        // 多人游玩: this is the room's button, not a local "play this" - the
        // press goes to the caller (the host locks the round, a member confirms
        // itself) and nothing is loaded here. It is drawn dimmed while there is
        // nothing to confirm yet (no song from the host, or no difficulty
        // picked), so the rule is visible instead of just rejected.
        const bool okReady = !partyPick || (partyPickable && party->myDifficulty >= 0);
        const float okW = sw * 0.58f;
        const float okH = 56.0f * k;
        const float okTop = std::max(scrY0 + phoneH * 0.600f, dcY + dcD * 0.5f + 44.0f * k);
        const ImVec2 okA(cx - okW * 0.5f, okTop);
        const ImVec2 okB(okA.x + okW, okA.y + okH);
        ImGui::SetCursorScreenPos(tiltedItemPos(ImVec2(cx, (okA.y + okB.y) * 0.5f), ImVec2(okW, okH)));
        ImGui::PushID("ok");
        ImGui::InvisibleButton("ok", ImVec2(okW, okH));
        const bool okHovered = ImGui::IsItemHovered();
        const bool okPressed = ImGui::IsItemClicked();
        ImGui::PopID();
        dl->AddRectFilled(okA, okB,
            okReady ? (okHovered ? ui::kPrimaryHover : ui::kPrimary) : IM_COL32(94, 108, 116, 255),
            okH * 0.5f);
        addTextCentered(dl, body, 22.0f * k, ImVec2(cx, (okA.y + okB.y) * 0.5f),
            okReady ? ui::kBtnText : IM_COL32(206, 210, 220, 255), "确定");
        // Where the button actually lands on screen (it is tilted with the phone),
        // so main.cpp can start the confirm flash from it.
        if (confirmCenter != nullptr) {
            *confirmCenter = tiltPoint(ImVec2(cx, (okA.y + okB.y) * 0.5f));
        }
        if (okPressed) {
            ui::se(ui::SeClick);
            if (partyRoom) {
                if (partyOut != nullptr) {
                    partyOut->confirm = true;
                }
            } else {
                action = selected;
            }
        }

        // Shuffle + music settings buttons (pjsk round dark buttons).
        // 多人游玩: the shuffle decides *which song the room plays*, so a member
        // gets a dimmed, inert one. 设置 stays live - volume and judgement
        // windows are this window's own business.
        const float ibD = 64.0f * k;
        const float ibY = std::max(scrY0 + phoneH * 0.700f, okB.y + 62.0f * k);
        const float ibGap = ibD * 1.7f;
        for (int i = 0; i < 2; ++i) {
            const bool lockedOut = partyReadOnly && i == 0;
            const ImVec2 c(cx + (i == 0 ? -ibGap * 0.5f : ibGap * 0.5f), ibY);
            ImGui::SetCursorScreenPos(tiltedItemPos(c, ImVec2(ibD, ibD)));
            ImGui::PushID(i);
            ImGui::InvisibleButton("iconbtn", ImVec2(ibD, ibD));
            const bool pressed = !lockedOut && ImGui::IsItemClicked();
            const bool hovered = !lockedOut && ImGui::IsItemHovered();
            const float hot = ui::anim(ImGui::GetItemID() ^ 0x71u, hovered, 18.0f);
            ImGui::PopID();
            // The disc swells a little and brightens as the pointer comes over it.
            dl->AddCircleFilled(c, ibD * 0.5f * (1.0f + 0.06f * hot),
                lockedOut ? IM_COL32(52, 50, 74, 255)
                          : ui::mix(IM_COL32(74, 68, 112, 255), IM_COL32(122, 116, 168, 255), hot));
            const char* texName = i == 0 ? "shufflebutton" : "musicsetting";
            const GLuint tex = selectTex(renderer, texName);
            if (tex != 0) {
                float iw = ibD * 0.62f * (1.0f + 0.06f * hot);
                float ih = iw;
                // keep each image's own aspect
                if (i == 0) {
                    ih = iw * (52.0f / 60.0f);
                }
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                    ImVec2(c.x - iw * 0.5f, c.y - ih * 0.5f), ImVec2(c.x + iw * 0.5f, c.y + ih * 0.5f));
            }
            if (lockedOut) {
                // A dark wash over the icon: "this one is not yours to press".
                dl->AddCircleFilled(c, ibD * 0.5f, IM_COL32(10, 12, 24, 150));
            }
            if (pressed) {
                ui::se(ui::SeClick);
                if (i == 0) {
                    // Shuffle: jump to a random (different) song.
                    if (!groups.empty()) {
                        int next = groupIndex;
                        for (int tries = 0; tries < 8 && next == groupIndex; ++tries) {
                            next = std::rand() % static_cast<int>(groups.size());
                        }
                        groupIndex = next;
                    }
                } else {
                    action = SelectSettings;
                }
            }
        }

        // 多人游玩 status line, under both round buttons so it can never land on
        // one: what the room is waiting for ("等待房主选曲" / "已确定 · 等待其他
        // 玩家" / "本窗口没有这首曲子"). Mint once this window has confirmed.
        if (partyRoom && !party->status.empty()) {
            const float statusY = ibY + ibD * 0.5f + 26.0f * k;
            const std::string line = ellipsize(body, 16.0f * k, party->status, sw * 0.96f);
            addTextCentered(dl, body, 16.0f * k, ImVec2(cx, statusY),
                party->confirmed ? IM_COL32(106, 232, 208, 255) : IM_COL32(178, 178, 198, 255),
                line.c_str());
        }
    }

    // Song change: the content block rises into place and fades up while the
    // phone frame stays where it is. Same trick as the entry animation below -
    // it only needs the vertex index the content started at.
    static int lastPanelSong = -1;
    static float panelIn = 1.0f;
    if (groupIndex != lastPanelSong) {
        lastPanelSong = groupIndex;
        panelIn = 0.0f;
    }
    panelIn = std::min(1.0f, panelIn + static_cast<float>(ImGui::GetIO().DeltaTime) / 0.22f);
    const float panelEase = easeOutCubic(panelIn);
    const float panelRise = (1.0f - panelEase) * 16.0f * k;
    const int panelAlpha = static_cast<int>(std::clamp(panelEase, 0.0f, 1.0f) * 255.0f);

    // Tilt the phone: rotate every vertex the block above produced about the
    // phone's centre. Text, images, rounded shapes - all rotate together. The
    // same pass runs the intro animation: on entry the whole phone slides in
    // from the right and fades up (the reference screen does the same), which
    // is free here because every vertex is already being rewritten.
    const float enter = easeOutCubic(enterAnim);
    const float phoneSlide = (1.0f - enter) * 300.0f * k;
    const int enterAlpha = static_cast<int>(std::clamp(enter, 0.0f, 1.0f) * 255.0f);
    for (int i = phoneVtxFirst; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& p = dl->VtxBuffer[i].pos;
        const bool isContent = i >= phoneContentVtxFirst;
        const float dx = p.x + phoneSlide - tiltPivot.x;
        const float dy = p.y + (isContent ? panelRise : 0.0f) - tiltPivot.y;
        p = ImVec2(tiltPivot.x + dx * tiltCos - dy * tiltSin, tiltPivot.y + dx * tiltSin + dy * tiltCos);
        const int vtxAlpha = isContent ? std::min(enterAlpha, panelAlpha) : enterAlpha;
        if (vtxAlpha < 255) {
            ImU32& col = dl->VtxBuffer[i].col;
            const ImU32 a = (col >> IM_COL32_A_SHIFT) & 0xFF;
            col = (col & ~IM_COL32_A_MASK)
                | (static_cast<ImU32>(a * static_cast<ImU32>(vtxAlpha) / 255u) << IM_COL32_A_SHIFT);
        }
    }

    // F5 rescan is handled by main; Enter handled above. Left/right switch
    // difficulty when the phone panel is showing - and for a 多人游玩 member this
    // is the *only* meaning the arrows have, since the list does not move, so
    // the pick is reported to the caller as well.
    const auto nudgeDifficulty = [&](int step) {
        for (int d = diffIndex + step; d >= 0 && d < kDiffCount; d += step) {
            if (groups[static_cast<size_t>(groupIndex)].idx[d] >= 0) {
                ui::se(ui::SeLevelChoose);
                diffIndex = d;
                selected = groups[static_cast<size_t>(groupIndex)].idx[d];
                if (partyReadOnly && partyOut != nullptr) {
                    partyOut->difficulty = d;
                }
                return;
            }
        }
    };
    if (!groups.empty() && selected >= 0 && (!partyReadOnly || partyPickable)) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            nudgeDifficulty(-1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            nudgeDifficulty(1);
        }
    }

    // Player level chip, top right like the official screen. Drawn after the
    // tilt pass on purpose: the phone block is rotated about its own centre,
    // this is not part of it.
    //
    // It lines up with the 排序 / 分组 combos: same height (33k) and same top
    // edge, which is what the official top bar does with its own widgets.
    bool& profileOpen = gProfileOpen; // module state, so --profile can force it
    if (account != nullptr) {
        const double need = game::expToNextRank(account->rank);
        const float expRatio = need > 0.0 ? static_cast<float>(account->exp / need) : 1.0f;
        const ImVec2 chipAnchor(w - 26.0f * k, headerRowY);
        const ImVec4 chip = ui::playerLevelChip(dl, body, chipAnchor, account->rank, k, true, expRatio);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool hot = !profileOpen && mouse.x >= chip.x && mouse.x <= chip.x + chip.z
            && mouse.y >= chip.y && mouse.y <= chip.y + chip.w;
        if (hot) {
            // Hover: a soft ring, so the chip reads as clickable.
            dl->AddRect(ImVec2(chip.x - 2.0f, chip.y - 2.0f),
                ImVec2(chip.x + chip.z + 2.0f, chip.y + chip.w + 2.0f), IM_COL32(255, 255, 255, 120),
                (chip.w + 4.0f) * 0.5f, 0, 2.0f * k);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                profileOpen = true;
                ui::se(ui::SeClick);
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    if (gSelectFillDisabled) {
        ImGui::PopStyleColor();
    }

    // ------------------------------------------------------------------
    // Profile card. The account is deliberately invisible during normal play -
    // this card (opened from the level chip) and 设置 -> 账户 are the only two
    // places the name / school are ever drawn.
    // ------------------------------------------------------------------
    if (profileOpen && account != nullptr) {
        const float s = ui::scale();
        ImVec2 cardSize(360.0f * s, 470.0f * s);
        ImVec2 cardCenter(w * 0.5f, h * 0.5f);
        bool closeClicked = false;
        if (ui::beginCard("##profile", &cardCenter, &cardSize, true, true, &closeClicked, profileOpen)) {
            if (closeClicked) {
                profileOpen = false;
            }
            const float interior = cardSize.x - 56.0f * s;
            const float padX = 28.0f * s;
            const auto left = [&](float extra) {
                ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - cardSize.x * 0.5f + padX,
                    ImGui::GetCursorScreenPos().y + extra));
            };
            ImGui::SetCursorScreenPos(
                ImVec2(cardCenter.x - cardSize.x * 0.5f + padX, cardCenter.y - cardSize.y * 0.5f + 16.0f * s));
            ui::cardTitle("个人资料", interior);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kBodyText);
            ImGui::PushFont(body, 22.0f * s);
            ImGui::PushItemWidth(interior);

            left(6.0f * s);
            ImGui::Text("昵称");
            left(2.0f * s);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kNotePink), "%s",
                account->name.empty() ? "（未设置）" : account->name.c_str());
            left(14.0f * s);
            ImGui::Text("学校 / 组织");
            left(2.0f * s);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kNotePink), "%s",
                account->org.empty() ? "（未设置）" : account->org.c_str());
            if (!account->note.empty()) {
                left(14.0f * s);
                ImGui::Text("签名");
                left(2.0f * s);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kNotePink), "%s",
                    account->note.c_str());
            }

            // 等级 + 进度: everything else on this card is flavour, this is the
            // part that moves when a song is played.
            const double need = game::expToNextRank(account->rank);
            char rankLine[64];
            std::snprintf(rankLine, sizeof(rankLine), "%d", account->rank);
            char playLine[64];
            std::snprintf(playLine, sizeof(playLine), "%d 次   平均 %.0f", account->plays,
                account->plays > 0 ? account->totalScore / account->plays : 0.0);
            left(18.0f * s);
            std::vector<std::pair<std::string, std::string>> rows = {
                {"等级", rankLine},
                {"游玩 / 平均分", playLine},
            };
            ui::infoRows(rows, interior);

            // The chip on the song select already carries this as its green fill,
            // but there it is a few pixels wide - the card is where the exact
            // number gets a full-width bar of its own.
            const float expRatio = need > 0.0
                ? static_cast<float>(account->exp / need)
                : 1.0f;
            left(12.0f * s);
            const ImVec2 barPos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(interior, 12.0f * s));
            ui::expBar(ImGui::GetWindowDrawList(), barPos, interior, s, expRatio);
            left(6.0f * s);
            char expLine[64];
            std::snprintf(expLine, sizeof(expLine), "到下一级 %d / %d EXP",
                static_cast<int>(account->exp), static_cast<int>(need));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kNotePink), "%s", expLine);

            ImGui::PopItemWidth();
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(
                ImVec2(cardCenter.x - cardSize.x * 0.5f + padX, cardCenter.y + cardSize.y * 0.5f - 68.0f * s));
            if (ui::capsuleButton("关闭", ImVec2(132.0f * s, 46.0f * s), false)) {
                profileOpen = false;
            }
            ui::endCard();
        } else {
            profileOpen = false;
        }
    }

    // ------------------------------------------------------------------
    // 猜歌. Drawn after the list window (like 个人资料) so it is a modal card
    // of its own and nothing behind it can be clicked.
    // ------------------------------------------------------------------
    if (gGuess.open && gGuess.options.size() == 4) {
        const float s = ui::scale();
        ImVec2 cardSize(560.0f * s, 424.0f * s);
        ImVec2 cardCenter(w * 0.5f, h * 0.5f);
        bool closeClicked = false;
        if (ui::beginCard("##guess", &cardCenter, &cardSize, true, true, &closeClicked, gGuess.open)) {
            if (closeClicked) {
                gGuess.open = false;
            }
            const float padX = 30.0f * s;
            const float interior = cardSize.x - padX * 2.0f;
            const float leftX = cardCenter.x - cardSize.x * 0.5f + padX;
            const float topY = cardCenter.y - cardSize.y * 0.5f;
            const ImDrawList* dl2 = ImGui::GetWindowDrawList();

            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 16.0f * s));
            ui::cardTitle("猜歌", interior);

            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 64.0f * s));
            ui::caption("这个别名指的是哪首歌？", 18.0f * s, ui::kTitleText, interior);

            // The alias, big and pink - the whole question.
            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 92.0f * s));
            ImGui::PushFont(titleFont(), 34.0f * s);
            const std::string aliasText = "「" + gGuess.alias + "」";
            const ImVec2 aliasSize = ImGui::CalcTextSize(aliasText.c_str());
            ImGui::SetCursorScreenPos(
                ImVec2(cardCenter.x - aliasSize.x * 0.5f, topY + 92.0f * s));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kNotePink), "%s",
                aliasText.c_str());
            ImGui::PopFont();

            // Four options in a 2x2 grid. The fill carries the verdict after a
            // pick (mint = right, red = the one you took) - capsuleButton only
            // knows mint-or-white, hence the local pill.
            const bool answered = gGuess.picked >= 0;
            const float optW = (interior - 14.0f * s) * 0.5f;
            const float optH = 52.0f * s;
            for (int i = 0; i < 4; ++i) {
                const int col = i % 2;
                const int row = i / 2;
                ImGui::SetCursorScreenPos(ImVec2(leftX + static_cast<float>(col) * (optW + 14.0f * s),
                    topY + (146.0f + static_cast<float>(row) * 62.0f) * s));
                std::string label = std::to_string(i + 1) + ". " + titleFor(gGuess.options[i]);
                if (answered && i == gGuess.answer) {
                    label = "✓ " + label;
                } else if (answered && i == gGuess.picked) {
                    label = "✗ " + label;
                }
                ImU32 fill = ui::kWhiteBtn;
                if (answered && i == gGuess.answer) {
                    fill = ui::kPrimary;
                } else if (answered && i == gGuess.picked) {
                    fill = IM_COL32(255, 138, 150, 255);
                }
                if (guessOptionPill(label.c_str(), ImVec2(optW, optH), fill, s, !answered)) {
                    gGuess.picked = i;
                    ++gGuess.asked;
                    if (i == gGuess.answer) {
                        ++gGuess.correct;
                        ++gGuess.streak;
                        gGuess.bestStreak = std::max(gGuess.bestStreak, gGuess.streak);
                    } else {
                        gGuess.streak = 0;
                    }
                }
            }

            // Verdict + tally.
            const std::string verdict = !answered
                ? std::string()
                : (gGuess.picked == gGuess.answer
                          ? std::string("答对了！")
                          : std::string("答错了，正确答案是「") + titleFor(gGuess.options[gGuess.answer])
                                + "」");
            char tally[96];
            std::snprintf(tally, sizeof(tally), "答对 %d / %d    连对 %d（最佳 %d）", gGuess.correct,
                gGuess.asked, gGuess.streak, gGuess.bestStreak);
            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 276.0f * s));
            ui::caption(verdict.empty() ? " " : verdict.c_str(), 18.0f * s,
                gGuess.picked == gGuess.answer ? IM_COL32(46, 168, 140, 255)
                                               : IM_COL32(214, 74, 104, 255),
                interior);
            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 302.0f * s));
            ui::caption(tally, 16.0f * s, ui::kTitleText, interior);

            const float footY = topY + cardSize.y - 66.0f * s;
            const float btnW = (interior - 14.0f * s) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(leftX, footY));
            if (ui::capsuleButton(answered ? "下一题" : "跳过", ImVec2(btnW, 50.0f * s), true)) {
                newGuessQuestion();
            }
            ImGui::SetCursorScreenPos(ImVec2(leftX + btnW + 14.0f * s, footY));
            if (ui::capsuleButton("关闭", ImVec2(btnW, 50.0f * s), false)) {
                gGuess.open = false;
            }
            (void)dl2;
            ui::endCard();
        }
    }

    (void)windowW;
    (void)windowH;
    // The empty-list 下载谱面 button outranks anything the (non-existent) list
    // could have produced this frame.
    if (emptyAction != SelectNone) {
        return emptyAction;
    }
    return action;
}

} // namespace game

