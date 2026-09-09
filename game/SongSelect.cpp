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

    // Reads an optional <stem>.json sidecar: {"title", "lyricist", "composer",
    // "arranger", "vocal", ...}. SUS has no credit fields, so this is where
    // the intro card's 作詞/作曲/編曲/Vo. line comes from.
    std::map<std::string, std::string> readSidecarMetadata(const fs::path& chartPath)
    {
        std::map<std::string, std::string> out;
        std::error_code ec;
        fs::path jsonPath = chartPath;
        jsonPath.replace_extension(".json");
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
} // namespace

std::string gSelectAssetDir; // set via setSelectAssetDir()

void setSelectAssetDir(const std::string& dir)
{
    gSelectAssetDir = dir;
}

ScoreRecord mergeScore(const ScoreRecord& old, bool cleared, bool fullCombo)
{
    ScoreRecord out = old;
    out.cleared = out.cleared || cleared;
    out.fullCombo = out.fullCombo || fullCombo;
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

std::map<std::string, ScoreRecord> loadScores(const std::string& path)
{
    std::map<std::string, ScoreRecord> out;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return out;
    }
    try {
        const nlohmann::json doc = nlohmann::json::parse(file);
        if (!doc.is_object()) {
            return out;
        }
        for (auto it = doc.begin(); it != doc.end(); ++it) {
            if (!it.value().is_object()) {
                continue;
            }
            ScoreRecord rec;
            rec.cleared = it.value().value("cleared", false);
            rec.fullCombo = it.value().value("fullCombo", false);
            out[it.key()] = rec;
        }
    } catch (...) {
        // malformed scores file: start fresh
    }
    return out;
}

void saveScores(const std::string& path, const std::map<std::string, ScoreRecord>& scores)
{
    nlohmann::json doc = nlohmann::json::object();
    for (const auto& [name, rec] : scores) {
        doc[name] = {{"cleared", rec.cleared}, {"fullCombo", rec.fullCombo}};
    }
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
    const std::map<std::string, std::string> sidecar = readSidecarMetadata(path);
    auto sideField = [&](const char* key) -> std::string {
        const auto it = sidecar.find(key);
        return it == sidecar.end() ? std::string{} : it->second;
    };
    if (entry.title.empty()) {
        entry.title = sideField("title");
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
        if (item.title.empty()) {
            item.title = sideField("title");
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

    std::vector<SongGroup> buildGroups(const std::vector<ChartEntry>& entries)
    {
        std::vector<SongGroup> groups;
        std::map<std::string, int> byTitle;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const ChartEntry& item = entries[static_cast<size_t>(i)];
            const std::string key = item.title.empty() ? item.displayName : item.title;
            auto it = byTitle.find(key);
            if (it == byTitle.end()) {
                it = byTitle.emplace(key, static_cast<int>(groups.size())).first;
                SongGroup group;
                group.title = key;
                group.artist = item.artist;
                group.vocal = item.vocal;
                group.coverPath = item.coverPath;
                groups.push_back(group);
            }
            SongGroup& group = groups[static_cast<size_t>(it->second)];
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

    // Level text: charts without #PLAYLEVEL show "-" instead of 0.
    const char* levelText(const ChartEntry& entry, char* buf, size_t bufSize)
    {
        const int lv = std::atoi(entry.level.c_str());
        if (entry.level.empty() || lv <= 0) {
            return "-";
        }
        std::snprintf(buf, bufSize, "%d", lv);
        return buf;
    }

    // One slot of the indicate_back 5-diamond strip, tinted per difficulty.
    void addDiamondSlot(ImDrawList* dl, GLuint backTex, const ImVec2& center, float size, int diff,
        bool present, ImU32 color)
    {
        const ImVec2 p0(center.x - size * 0.5f, center.y - size * 0.5f);
        const ImVec2 p1(center.x + size * 0.5f, center.y + size * 0.5f);
        if (backTex != 0) {
            const float u0 = (static_cast<float>(diff) + 0.06f) / 5.0f;
            const float u1 = (static_cast<float>(diff) + 0.94f) / 5.0f;
            const ImU32 tint = present ? color : IM_COL32(58, 58, 82, 255);
            dl->AddImageRounded(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(backTex)),
                p0, p1, ImVec2(u0, 0.08f), ImVec2(u1, 0.92f), tint, 0.0f);
        } else {
            addDiamond(dl, center, size * 0.5f, present ? color : IM_COL32(58, 58, 82, 255));
        }
    }
} // namespace

int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec)
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

    // Background: dark blue wash with a slow moving highlight.
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, h),
        IM_COL32(30, 26, 58, 255), IM_COL32(52, 40, 88, 255),
        IM_COL32(20, 18, 40, 255), IM_COL32(46, 36, 78, 255));
    const float glowX = w * (0.5f + 0.35f * std::sin(timeSec * 0.25f));
    dl->AddCircleFilled(ImVec2(glowX, h * 0.35f), h * 0.6f, IM_COL32(120, 130, 230, 30), 64);

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

    const float listY = listTop + searchH + 14.0f * k;
    ImGui::SetCursorScreenPos(ImVec2(listX, listY));
    ImGui::BeginChild("song_list", ImVec2(listW + 24.0f * k, h - listY - 24.0f * k), ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground);
    ImDrawList* listDl = ImGui::GetWindowDrawList();
    const float rowX = ImGui::GetCursorScreenPos().x;
    float rowY = ImGui::GetCursorScreenPos().y;

    if (visible.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(rowX, rowY + 20.0f * k));
        ImGui::PushFont(body);
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.82f, 1.0f), "没有找到谱面。把 .sus 放到 charts/ 目录下再按 F5 刷新。");
        ImGui::PopFont();
    }

    const GLuint backTex = selectTex(renderer, "indicate_back");

    for (const int gi : visible) {
        const SongGroup& g = groups[static_cast<size_t>(gi)];
        const bool isSel = gi == groupIndex;
        const float rowH = (isSel ? 128.0f : 84.0f) * k;
        const float gap = 9.0f * k;

        ImGui::SetCursorScreenPos(ImVec2(rowX, rowY));
        ImGui::PushID(gi);
        ImGui::InvisibleButton("row", ImVec2(listW, rowH));
        if (ImGui::IsItemClicked()) {
            groupIndex = gi;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selected >= 0) {
            action = selected;
        }
        ImGui::PopID();

        const ImVec2 p0(rowX, rowY);
        const ImVec2 p1(rowX + listW, rowY + rowH);

        // The entry this row currently points at (for level + result badges).
        const int rowIdx = g.idx[diffIndex] >= 0 ? g.idx[diffIndex]
                                                 : (g.idx[3] >= 0 ? g.idx[3]
                                                                  : (g.idx[4] >= 0 ? g.idx[4] : g.idx[0]));
        const ChartEntry* rowEntry = rowIdx >= 0 ? &entries[static_cast<size_t>(rowIdx)] : nullptr;

        if (isSel) {
            // Expanded white card with the level badge, credits and diamonds.
            listDl->AddRectFilled(p0, p1, IM_COL32(250, 250, 253, 242), 10.0f * k);
            listDl->AddRect(p0, p1, IM_COL32(120, 130, 180, 90), 10.0f * k, 0, 2.0f);

            // Pink level badge: "歌曲等级" tag + big number.
            const float badgeL = p0.x + 14.0f * k;
            const float tagY = p0.y + 20.0f * k;
            listDl->AddRectFilled(ImVec2(badgeL, tagY), ImVec2(badgeL + 62.0f * k, tagY + 22.0f * k),
                IM_COL32(255, 90, 141, 255), 4.0f * k);
            addTextCentered(listDl, body, 13.0f * k, ImVec2(badgeL + 31.0f * k, tagY + 11.0f * k), white, "歌曲等级");
            if (rowEntry != nullptr && title != nullptr) {
                char levelBuf[16];
                listDl->AddText(title, 34.0f * k, ImVec2(badgeL, tagY + 30.0f * k), IM_COL32(70, 70, 88, 255),
                    levelText(*rowEntry, levelBuf, sizeof(levelBuf)));
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
                addDiamondSlot(listDl, backTex, ImVec2(dx, dy), 20.0f * k, d, g.idx[d] >= 0, kDiffColors[d]);
                dx += 20.0f * k;
            }
            // Clear / Full Combo badges (right end of the card).
            if (rowEntry != nullptr) {
                const float ib = 34.0f * k;
                float bx = p1.x - ib - 12.0f * k;
                if (rowEntry->cleared) {
                    const GLuint tex = selectTex(renderer, "clear_indicate");
                    if (tex != 0) {
                        listDl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                            ImVec2(bx, dy - ib * 0.5f), ImVec2(bx + ib, dy + ib * 0.5f));
                    }
                    bx -= ib + 6.0f * k;
                }
                if (rowEntry->fullCombo) {
                    const GLuint tex = selectTex(renderer, "fullcombo_indicate");
                    if (tex != 0) {
                        listDl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                            ImVec2(bx, dy - ib * 0.5f), ImVec2(bx + ib, dy + ib * 0.5f));
                    }
                }
            }
        } else {
            // Compact dark row: pink level circle, jacket thumb, title, diamonds.
            listDl->AddRectFilled(p0, p1, IM_COL32(20, 20, 44, 150), 8.0f * k);
            if (ImGui::IsItemHovered()) {
                listDl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 16), 8.0f * k);
            }

            const float cy = (p0.y + p1.y) * 0.5f;
            char levelBuf[16];
            listDl->AddCircleFilled(ImVec2(p0.x + 30.0f * k, cy), 24.0f * k, IM_COL32(255, 90, 141, 255));
            addTextCentered(listDl, body, 20.0f * k, ImVec2(p0.x + 30.0f * k, cy - 1.0f * k), white,
                rowEntry != nullptr ? levelText(*rowEntry, levelBuf, sizeof(levelBuf)) : "-");

            const float jx = p0.x + 62.0f * k;
            const float js = 60.0f * k;
            addJacket(listDl, thumbFor(renderer, g.coverPath), ImVec2(jx, cy - js * 0.5f),
                ImVec2(jx + js, cy + js * 0.5f), 6.0f * k);

            const float textL = jx + js + 14.0f * k;
            listDl->AddText(body, 20.0f * k, ImVec2(textL, cy - 26.0f * k), white, g.title.c_str());
            float dx = textL + 4.0f * k;
            for (int d = 0; d < kDiffCount; ++d) {
                addDiamondSlot(listDl, backTex, ImVec2(dx, cy + 6.0f * k), 18.0f * k, d, g.idx[d] >= 0, kDiffColors[d]);
                dx += 18.0f * k;
            }

            // Clear / Full Combo badges (right edge, vertically centered).
            if (rowEntry != nullptr && (rowEntry->cleared || rowEntry->fullCombo)) {
                const float ib = 30.0f * k;
                float bx = p1.x - ib - 12.0f * k;
                if (rowEntry->cleared) {
                    const GLuint tex = selectTex(renderer, "clear_indicate");
                    if (tex != 0) {
                        listDl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                            ImVec2(bx, cy - ib * 0.5f), ImVec2(bx + ib, cy + ib * 0.5f));
                    }
                    bx -= ib + 6.0f * k;
                }
                if (rowEntry->fullCombo) {
                    const GLuint tex = selectTex(renderer, "fullcombo_indicate");
                    if (tex != 0) {
                        listDl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                            ImVec2(bx, cy - ib * 0.5f), ImVec2(bx + ib, cy + ib * 0.5f));
                    }
                }
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

        rowY += rowH + gap;
    }

    // Keyboard navigation over the visible (filtered) rows.
    if (!visible.empty()) {
        const auto moveTo = [&](int step) {
            for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
                if (visible[static_cast<size_t>(i)] == groupIndex) {
                    const int next = std::clamp(i + step, 0, static_cast<int>(visible.size()) - 1);
                    groupIndex = visible[static_cast<size_t>(next)];
                    return;
                }
            }
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
    // difficulty buttons and 确定 / shuffle / settings.
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

            ImGui::SetCursorScreenPos(ImVec2(dx - dcD * 0.5f, dcY - dcD * 0.5f));
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
                dl->AddCircleFilled(c, dcD * 0.5f, IM_COL32(30, 27, 56, 235));
                dl->AddCircle(c, dcD * 0.5f, avail ? kDiffColors[d] : IM_COL32(110, 105, 140, 160), 48, 3.0f * k);
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

        // 确定 button
        const float okW = sw * 0.58f;
        const float okH = 56.0f * k;
        ImGui::SetCursorScreenPos(ImVec2(cx - okW * 0.5f, scrY0 + phoneH * 0.560f));
        if (ui::capsuleButton("确定", ImVec2(okW, okH), true)) {
            action = selected;
        }

        // Shuffle + music settings buttons (pjsk round dark buttons).
        const float ibD = 64.0f * k;
        const float ibY = scrY0 + phoneH * 0.640f;
        const float ibGap = ibD * 1.7f;
        for (int i = 0; i < 2; ++i) {
            const ImVec2 c(cx + (i == 0 ? -ibGap * 0.5f : ibGap * 0.5f), ibY);
            ImGui::SetCursorScreenPos(ImVec2(c.x - ibD * 0.5f, c.y - ibD * 0.5f));
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

