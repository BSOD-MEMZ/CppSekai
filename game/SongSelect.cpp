// CppSekai - song select screen (see SongSelect.hpp).
#include "SongSelect.hpp"

#include "Intro.hpp"
#include "Ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace game
{
namespace
{
    namespace fs = std::filesystem;

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
            const int musicId = musicIdFromStem(chartPath.stem().string());
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

    // Finds a sidecar file (jacket / bgm) next to the chart: exact stem first,
    // then the stem without a "_master"-style difficulty suffix, then any
    // file in the folder that looks like a jacket.
    std::string findSidecar(const fs::path& chartPath, const std::vector<std::string>& extensions,
        const std::vector<std::string>& keywords)
    {
        std::error_code ec;
        const fs::path dir = chartPath.parent_path();
        const std::string stem = chartPath.stem().string();

        std::vector<std::string> stems{stem};
        const size_t underscore = stem.rfind('_');
        if (underscore != std::string::npos) {
            stems.push_back(stem.substr(0, underscore));
        }

        for (const std::string& candidate : stems) {
            for (const std::string& ext : extensions) {
                const fs::path path = dir / (candidate + ext);
                if (fs::exists(path, ec)) {
                    return path.string();
                }
            }
        }
        if (keywords.empty()) {
            return {};
        }
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string name = toLower(entry.path().filename().string());
            if (!hasExtension(name, extensions)) {
                continue;
            }
            for (const std::string& keyword : keywords) {
                if (name.find(keyword) != std::string::npos) {
                    return entry.path().string();
                }
            }
        }
        return {};
    }

    ImU32 difficultyBadgeColor(const std::string& difficulty, int alpha)
    {
        if (difficulty == "EASY") return IM_COL32(75, 207, 138, alpha);
        if (difficulty == "NORMAL") return IM_COL32(90, 140, 255, alpha);
        if (difficulty == "HARD") return IM_COL32(242, 150, 77, alpha);
        if (difficulty == "EXPERT") return IM_COL32(239, 90, 102, alpha);
        if (difficulty == "MASTER") return IM_COL32(181, 91, 255, alpha);
        if (difficulty == "APPEND") return IM_COL32(179, 162, 255, alpha);
        if (difficulty == "ETERNAL") return IM_COL32(241, 192, 79, alpha);
        return IM_COL32(120, 130, 160, alpha);
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
} // namespace

std::string gSelectAssetDir; // set via setSelectAssetDir()

void setSelectAssetDir(const std::string& dir)
{
    gSelectAssetDir = dir;
}

// Blurred desktop wallpaper used as the screen backdrop (0 = draw the built-in
// gradient instead). Owned by the host - see setSelectBackdrop().
GLuint gSelectBackdropTex = 0;
int gSelectBackdropW = 0;
int gSelectBackdropH = 0;
float gSelectBackdropDim = 0.0f;

void setSelectBackdrop(GLuint texture, int texW, int texH, float dim)
{
    gSelectBackdropTex = texture;
    gSelectBackdropW = texW;
    gSelectBackdropH = texH;
    gSelectBackdropDim = dim;
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
    return fs::path(entry.susPath).filename().string();
}

void applyScores(std::vector<ChartEntry>& entries, const std::map<std::string, ScoreRecord>& scores)
{
    for (ChartEntry& e : entries) {
        const auto it = scores.find(scoreKey(e));
        e.cleared = it != scores.end() && it->second.cleared;
        e.fullCombo = it != scores.end() && it->second.fullCombo;
    }
}

std::string userDataPath(const std::string& exeDir)
{
    std::error_code ec;
    const fs::path parent = fs::path(exeDir) / "..";
    if (fs::exists(parent / "charts", ec)) {
        // build/ layout: keep the file next to charts/ so wiping build/ (or
        // copying the folder to a new machine) does not lose it.
        return (parent / "userdata.json").lexically_normal().string();
    }
    return (fs::path(exeDir) / "userdata.json").string();
}

void loadUserData(const std::string& path, UserSettings& settings,
    std::map<std::string, ScoreRecord>& scores)
{
    std::ifstream file(path, std::ios::binary);
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
            settings.offsetSec = s.value("offsetSec", settings.offsetSec);
            settings.leadInSec = s.value("leadInSec", settings.leadInSec);
            settings.windowMode = s.value("windowMode", settings.windowMode);
            settings.windowWidth = s.value("windowWidth", settings.windowWidth);
            settings.windowHeight = s.value("windowHeight", settings.windowHeight);
            settings.fpsLimit = s.value("fpsLimit", settings.fpsLimit);
            settings.showProgressBar = s.value("showProgressBar", settings.showProgressBar);
            settings.hideTouchFeedback = s.value("hideTouchFeedback", settings.hideTouchFeedback);
            settings.perfectMs = s.value("perfectMs", settings.perfectMs);
            settings.greatMs = s.value("greatMs", settings.greatMs);
            settings.goodMs = s.value("goodMs", settings.goodMs);
            settings.strictFlick = s.value("strictFlick", settings.strictFlick);
            settings.autoplay = s.value("autoplay", settings.autoplay);
            settings.autoPauseOnBlur = s.value("autoPauseOnBlur", settings.autoPauseOnBlur);
            settings.reportSmtc = s.value("reportSmtc", settings.reportSmtc);
            settings.splashStyle = s.value("splashStyle", settings.splashStyle);
            settings.bgStyle = s.value("bgStyle", settings.bgStyle);
            settings.bgBlur = s.value("bgBlur", settings.bgBlur);
            settings.bgDim = s.value("bgDim", settings.bgDim);
            settings.sortMode = s.value("sortMode", settings.sortMode);
            settings.groupMode = s.value("groupMode", settings.groupMode);
        }
    } catch (...) {
        // malformed file: keep the defaults
    }
    // Same ordering rules the settings UI enforces.
    settings.perfectMs = std::clamp(settings.perfectMs, 10.0f, 100.0f);
    settings.greatMs = std::max(settings.greatMs, settings.perfectMs + 10.0f);
    settings.goodMs = std::max(settings.goodMs, settings.greatMs + 10.0f);
    settings.windowWidth = std::clamp(settings.windowWidth, 320, 7680);
    settings.windowHeight = std::clamp(settings.windowHeight, 240, 4320);
    settings.bgStyle = std::clamp(settings.bgStyle, 0, 1);
    settings.bgBlur = std::clamp(settings.bgBlur, 0.0f, 1.0f);
    settings.bgDim = std::clamp(settings.bgDim, 0.0f, 1.0f);
    settings.sortMode = std::clamp(settings.sortMode, 0, 1);
    settings.groupMode = std::clamp(settings.groupMode, 0, 3);
}

void saveUserData(const std::string& path, const UserSettings& settings,
    const std::map<std::string, ScoreRecord>& scores)
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
        {"offsetSec", settings.offsetSec},
        {"leadInSec", settings.leadInSec},
        {"windowMode", settings.windowMode},
        {"windowWidth", settings.windowWidth},
        {"windowHeight", settings.windowHeight},
        {"fpsLimit", settings.fpsLimit},
        {"showProgressBar", settings.showProgressBar},
        {"hideTouchFeedback", settings.hideTouchFeedback},
        {"perfectMs", settings.perfectMs},
        {"greatMs", settings.greatMs},
        {"goodMs", settings.goodMs},
        {"strictFlick", settings.strictFlick},
        {"autoplay", settings.autoplay},
        {"autoPauseOnBlur", settings.autoPauseOnBlur},
        {"reportSmtc", settings.reportSmtc},
        {"splashStyle", settings.splashStyle},
        {"bgStyle", settings.bgStyle},
        {"bgBlur", settings.bgBlur},
        {"bgDim", settings.bgDim},
        {"sortMode", settings.sortMode},
        {"groupMode", settings.groupMode},
    };
    doc["scores"] = scoreDoc;
    std::ofstream file(path, std::ios::binary);
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
    const fs::path path(entry.susPath);
    const std::string stem = path.stem().string();
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
    if (dir.empty() || !fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return entries;
    }

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const fs::path path = entry.path();
        if (!hasExtension(path.filename().string(), {".sus"})) {
            continue;
        }

        ChartEntry item;
        item.susPath = path.string();
        const std::string stem = path.stem().string();
        item.musicId = musicIdFromStem(stem);

        const std::map<std::string, std::string> header = readSusHeader(path);
        auto field = [&](const char* key) -> std::string {
            const auto it = header.find(key);
            return it == header.end() ? std::string{} : it->second;
        };
        const std::map<std::string, std::string> sidecar = readSidecarMetadata(path);
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
    if (path.empty() || !fs::exists(path, ec)) {
        return;
    }
    std::ifstream file(path, std::ios::binary);
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

void loadMusicMaster(const std::string& path)
{
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) {
        return;
    }
    std::ifstream file(path, std::ios::binary);
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

int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec, int& sortMode, int& groupMode)
{
    int action = SelectNone;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float w = viewport->WorkSize.x;
    const float h = viewport->WorkSize.y;
    // 1080p reference layout.
    const float k = std::clamp(h / 1080.0f, 0.5f, 2.0f);

    static std::vector<SongGroup> groups;
    static int groupIndex = 0;
    static int diffIndex = 3; // EXPERT, like the reference UI
    static size_t lastCount = 0;
    if (entries.size() != lastCount) {
        lastCount = entries.size();
        groups = buildGroups(entries);
        groupIndex = 0;
    } else if (groups.empty() && !entries.empty()) {
        groups = buildGroups(entries);
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
    } else {
        selected = -1;
    }

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("CppSekaiSongSelect", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* title = titleFont();
    ImFont* body = bodyFont() != nullptr ? bodyFont() : ImGui::GetFont();

    // Background: the blurred desktop wallpaper when the player picked it,
    // otherwise the built-in dark blue wash with a slow moving highlight.
    if (gSelectBackdropTex != 0 && gSelectBackdropW > 0 && gSelectBackdropH > 0) {
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
    } else {
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
    ImGui::SetCursorScreenPos(ImVec2(listX, listTop));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(250, 250, 253, 210));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 235));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(70, 70, 90, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, searchH * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f * k, searchH * 0.28f));
    ImGui::PushFont(body, 19.0f * k);
    ImGui::SetNextItemWidth(searchW - searchH);
    ImGui::InputTextWithHint("##search", "根据歌曲名·作者名查找", searchBuf, sizeof(searchBuf));
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    // Magnifier icon at the right end of the box.
    {
        const float mx = listX + searchW - searchH * 0.55f;
        const float my = listTop + searchH * 0.5f;
        const float mr = 8.0f * k;
        dl->AddCircle(ImVec2(mx, my), mr, IM_COL32(120, 120, 140, 255), 24, 2.0f * k);
        dl->AddLine(ImVec2(mx + mr * 0.75f, my + mr * 0.75f), ImVec2(mx + mr * 1.5f, my + mr * 1.5f),
            IM_COL32(120, 120, 140, 255), 2.0f * k);
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
    {
        const char* kSortLabels[2] = {"按名称", "按难度"};
        const char* kGroupLabels[kGroupCount] = {"关闭", "按难度段", "按读音", "按首字"};
        const float comboW = 168.0f * k;
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
        ImGui::SetCursorScreenPos(ImVec2(listX + searchW + 18.0f * k, listTop + 4.0f * k));
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##sortby", sortPreview.c_str(), ImGuiComboFlags_HeightSmall)) {
            for (int i = 0; i < 2; ++i) {
                if (ImGui::Selectable(kSortLabels[i], sortMode == i)) {
                    sortMode = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetCursorScreenPos(ImVec2(listX + searchW + 30.0f * k + comboW, listTop + 4.0f * k));
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##groupby", groupPreview.c_str(), ImGuiComboFlags_HeightSmall)) {
            for (int i = 0; i < kGroupCount; ++i) {
                if (ImGui::Selectable(kGroupLabels[i], groupMode == i)) {
                    groupMode = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(8);
    }

    // Groups matching the search filter.
    std::vector<int> visible;
    if (searchBuf[0] == '\0') {
        for (int gi = 0; gi < static_cast<int>(groups.size()); ++gi) {
            visible.push_back(gi);
        }
    } else {
        const std::string needle = toLower(searchBuf);
        for (int gi = 0; gi < static_cast<int>(groups.size()); ++gi) {
            const SongGroup& g = groups[static_cast<size_t>(gi)];
            if (toLower(g.title).find(needle) != std::string::npos
                || toLower(g.artist).find(needle) != std::string::npos) {
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
        + std::to_string(groupMode) + "|" + std::to_string(diffIndex) + "|" + std::to_string(entries.size());
    if (listSignature != lastSignature) {
        lastSignature = listSignature;
        cachedRows = buildRows(groups, visible, entries, diffIndex, order, groupMode);
        listInit = false; // the list changed shape: recentre without gliding
        slotHeights.assign(cachedRows.size(), 0.0f);
    }
    // Grouping off means no section headers exist, so the panel has nothing to
    // show any more.
    if (groupMode == kGroupOff) {
        indexOpen = false;
    }
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
    const bool listHovered = !indexOpen && (dragging || (inViewRect && ImGui::IsWindowHovered()));

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
            // order and the sorted order differ, so group 0 is not it).
            rowsBuilt = true;
            for (const ListRow& r : rows) {
                if (!r.header) {
                    groupIndex = r.group;
                    break;
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
                        indexOpen = !indexOpen;
                        std::printf("[select] section index %s\n", indexOpen ? "opened" : "closed");
                        std::fflush(stdout);
                    } else {
                        // Tap: pick that row (it glides to the centre).
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

    if (rowCount == 0) {
        ImGui::SetCursorScreenPos(ImVec2(rowX, rowY + 20.0f * k));
        ImGui::PushFont(body);
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.82f, 1.0f),
            "没有找到谱面。把 .sus 放到 charts/ 目录下，再按 F5 重新扫描（命名规则见 CHARTS.md）。");
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
            const float headX = rowX + 30.0f * k;
            if (hovered) {
                listDl->AddRectFilled(ImVec2(rowX + 6.0f * k, centerY - pitch * 0.34f),
                    ImVec2(rowX + listW, centerY + pitch * 0.34f), IM_COL32(255, 255, 255, 26), 8.0f * k);
                listDl->AddRectFilled(ImVec2(rowX, centerY - pitch * 0.30f), ImVec2(rowX + 3.0f * k, centerY + pitch * 0.30f),
                    IM_COL32(255, 255, 255, 220), 2.0f * k);
            }
            listDl->AddText(headFont, headSize, ImVec2(headX, centerY - headSize * 0.6f),
                hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 210), view.label.c_str());
            const float labelW = headFont->CalcTextSizeA(headSize, FLT_MAX, 0.0f, view.label.c_str()).x;
            const float chevX = headX + labelW + 12.0f * k;
            const float chevA = hovered ? 220.0f : 110.0f;
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
            if (slot == hoverSlot) {
                listDl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 20), 8.0f * k);
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
    if (indexAnim > 0.0f) {
        const int fade = static_cast<int>(std::clamp(1.0f - indexAnim, 0.0f, 1.0f) * 255.0f);
        for (int i = listVtxFirst; i < listDl->VtxBuffer.Size; ++i) {
            ImU32& col = listDl->VtxBuffer[i].col;
            const ImU32 a = (col >> IM_COL32_A_SHIFT) & 0xFF;
            col = (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a * fade / 255u) << IM_COL32_A_SHIFT);
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
            const float size = (active ? 34.0f : 30.0f) * k * (0.88f + 0.12f * lt);
            const int alpha = static_cast<int>((hovered || active ? 255.0f : 205.0f) * lt);
            addTextCentered(listDl, letterFont, size, pos,
                active ? IM_COL32(255, 255, 255, alpha)
                       : (hovered ? IM_COL32(255, 255, 255, alpha) : IM_COL32(232, 232, 244, alpha)),
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


    // Keyboard navigation over the (cyclically) listed rows.
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
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown)) {
            moveTo(1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp)) {
            moveTo(-1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            if (selected >= 0) {
                action = selected;
            }
        }
    }
    ImGui::EndChild();

    // ------------------------------------------------------------------
    // Right: tilted-phone panel (img_smartphone.png) with the jacket, the
    // difficulty buttons and 确定 / shuffle / settings. The whole block is
    // laid out flat and then rotated about the phone's centre, like the
    // reference UI.
    // ------------------------------------------------------------------
    const float phoneAspect = 1034.0f / 1942.0f;
    float phoneH = h - 24.0f * k;
    float phoneW = phoneH * phoneAspect;
    if (phoneW > w * 0.46f) {
        phoneW = w * 0.46f;
        phoneH = phoneW / phoneAspect;
    }
    const float phoneX = w - phoneW - 30.0f * k;
    const float phoneY = (h - phoneH) * 0.5f;

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

        // Title / artist / vocal
        float ty = j0.y + jh + phoneH * 0.014f;
        if (title != nullptr) {
            addTextCentered(dl, title, 26.0f * k, ImVec2(cx, ty + 13.0f * k), white, group.title.c_str());
        }
        ty += 40.0f * k;
        if (!item.artist.empty()) {
            addTextCentered(dl, body, 17.0f * k, ImVec2(cx, ty), grayText, item.artist.c_str());
            ty += 26.0f * k;
        }
        if (!item.vocal.empty()) {
            const std::string vo = "Vo. " + item.vocal;
            addTextCentered(dl, body, 15.0f * k, ImVec2(cx, ty), grayText, vo.c_str());
            ty += 24.0f * k;
        }

        // Difficulty buttons
        const float dcD = 60.0f * k;
        const float dcGap = 9.0f * k;
        const float rowW = kDiffCount * dcD + (kDiffCount - 1) * dcGap;
        float dx = cx - rowW * 0.5f + dcD * 0.5f;
        const float dcY = scrY0 + phoneH * 0.475f;
        for (int d = 0; d < kDiffCount; ++d) {
            const bool avail = group.idx[d] >= 0;
            const bool active = d == diffIndex && avail;
            const ImVec2 c(dx, dcY);

            ImGui::SetCursorScreenPos(tiltedItemPos(c, ImVec2(dcD, dcD)));
            ImGui::PushID(d);
            if (avail) {
                ImGui::InvisibleButton("diff", ImVec2(dcD, dcD));
                if (ImGui::IsItemClicked()) {
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
        const float okW = sw * 0.58f;
        const float okH = 56.0f * k;
        const ImVec2 okA(cx - okW * 0.5f, scrY0 + phoneH * 0.560f);
        const ImVec2 okB(okA.x + okW, okA.y + okH);
        ImGui::SetCursorScreenPos(tiltedItemPos(ImVec2(cx, (okA.y + okB.y) * 0.5f), ImVec2(okW, okH)));
        ImGui::PushID("ok");
        ImGui::InvisibleButton("ok", ImVec2(okW, okH));
        const bool okHovered = ImGui::IsItemHovered();
        const bool okPressed = ImGui::IsItemClicked();
        ImGui::PopID();
        dl->AddRectFilled(okA, okB, okHovered ? ui::kPrimaryHover : ui::kPrimary, okH * 0.5f);
        addTextCentered(dl, body, 22.0f * k, ImVec2(cx, (okA.y + okB.y) * 0.5f), ui::kBtnText, "确定");
        if (okPressed) {
            action = selected;
        }

        // Shuffle + music settings buttons (pjsk round dark buttons).
        const float ibD = 64.0f * k;
        const float ibY = scrY0 + phoneH * 0.640f;
        const float ibGap = ibD * 1.7f;
        for (int i = 0; i < 2; ++i) {
            const ImVec2 c(cx + (i == 0 ? -ibGap * 0.5f : ibGap * 0.5f), ibY);
            ImGui::SetCursorScreenPos(tiltedItemPos(c, ImVec2(ibD, ibD)));
            ImGui::PushID(i);
            ImGui::InvisibleButton("iconbtn", ImVec2(ibD, ibD));
            const bool pressed = ImGui::IsItemClicked();
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            dl->AddCircleFilled(c, ibD * 0.5f,
                hovered ? IM_COL32(122, 116, 168, 255) : IM_COL32(74, 68, 112, 255));
            const char* texName = i == 0 ? "shufflebutton" : "musicsetting";
            const GLuint tex = selectTex(renderer, texName);
            if (tex != 0) {
                float iw = ibD * 0.62f;
                float ih = iw;
                // keep each image's own aspect
                if (i == 0) {
                    ih = iw * (52.0f / 60.0f);
                }
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                    ImVec2(c.x - iw * 0.5f, c.y - ih * 0.5f), ImVec2(c.x + iw * 0.5f, c.y + ih * 0.5f));
            }
            if (pressed) {
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
    }

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
        const float dx = p.x + phoneSlide - tiltPivot.x;
        const float dy = p.y - tiltPivot.y;
        p = ImVec2(tiltPivot.x + dx * tiltCos - dy * tiltSin, tiltPivot.y + dx * tiltSin + dy * tiltCos);
        if (enterAlpha < 255) {
            ImU32& col = dl->VtxBuffer[i].col;
            const ImU32 a = (col >> IM_COL32_A_SHIFT) & 0xFF;
            col = (col & ~IM_COL32_A_MASK)
                | (static_cast<ImU32>(a * static_cast<ImU32>(enterAlpha) / 255u) << IM_COL32_A_SHIFT);
        }
    }

    // F5 rescan is handled by main; Enter handled above. Left/right switch
    // difficulty when the phone panel is showing.
    if (!groups.empty() && selected >= 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            for (int d = diffIndex - 1; d >= 0; --d) {
                if (groups[static_cast<size_t>(groupIndex)].idx[d] >= 0) {
                    diffIndex = d;
                    selected = groups[static_cast<size_t>(groupIndex)].idx[d];
                    break;
                }
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            for (int d = diffIndex + 1; d < kDiffCount; ++d) {
                if (groups[static_cast<size_t>(groupIndex)].idx[d] >= 0) {
                    diffIndex = d;
                    selected = groups[static_cast<size_t>(groupIndex)].idx[d];
                    break;
                }
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    (void)windowW;
    (void)windowH;
    return action;
}

} // namespace game

