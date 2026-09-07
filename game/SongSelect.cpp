// CppSekai - song select screen (see SongSelect.hpp).
#include "SongSelect.hpp"

#include "Intro.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
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
            for (const char* key : {"title", "artist", "lyricist", "composer", "arranger", "vocal", "difficulty", "level"}) {
                if (doc.contains(key) && doc[key].is_string()) {
                    out[key] = doc[key].get<std::string>();
                }
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
    if (entry.coverPath.empty()) {
        entry.coverPath = findSidecar(path, {".png", ".jpg", ".jpeg"}, {"jacket", "cover"});
    }
    if (entry.bgmPath.empty()) {
        entry.bgmPath = findSidecar(path, {".mp3", ".wav", ".ogg", ".flac", ".m4a"}, {});
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
        item.displayName = item.title.empty() ? prettyFileName(stem) : item.title;

        item.coverPath = findSidecar(path, imageExt, {"jacket", "cover"});
        item.bgmPath = findSidecar(path, audioExt, {});

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

int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec)
{
    int action = SelectNone;

    if (selected >= static_cast<int>(entries.size())) {
        selected = entries.empty() ? -1 : 0;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("CppSekaiSongSelect", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Background: dark blue wash with a slow moving highlight.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float w = viewport->WorkSize.x;
    const float h = viewport->WorkSize.y;
    drawList->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, h),
        IM_COL32(16, 18, 34, 255), IM_COL32(30, 24, 56, 255),
        IM_COL32(12, 14, 28, 255), IM_COL32(28, 22, 48, 255));
    const float glowX = w * (0.5f + 0.35f * std::sin(timeSec * 0.25f));
    drawList->AddCircleFilled(ImVec2(glowX, h * 0.35f), h * 0.6f, IM_COL32(90, 120, 220, 26), 64);

    ImFont* body = bodyFont();
    ImFont* title = titleFont();
    if (body != nullptr) {
        ImGui::PushFont(body);
    }

    const float listWidth = std::min(720.0f, w * 0.52f);
    const float pad = 28.0f;

    ImGui::SetCursorPos(ImVec2(pad, pad));
    if (title != nullptr) {
        ImGui::PushFont(title);
    }
    ImGui::TextColored(ImVec4(0.95f, 0.96f, 1.0f, 1.0f), "SELECT SONG");
    if (title != nullptr) {
        ImGui::PopFont();
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.8f, 1.0f), " (%d charts)", static_cast<int>(entries.size()));

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    ImGui::BeginChild("song_list", ImVec2(listWidth, h - pad * 2 - 70.0f), ImGuiChildFlags_Borders);

    if (entries.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1.0f), "没有找到谱面。把 .sus 放到 charts/ 目录下再按 F5 刷新。");
    }

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const ChartEntry& item = entries[static_cast<size_t>(i)];
        const bool isSelected = i == selected;
        ImGui::PushID(i);

        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImVec2 rowSize(listWidth - 16.0f, 62.0f);
        if (isSelected) {
            ImGui::GetWindowDrawList()->AddRectFilled(rowPos, ImVec2(rowPos.x + rowSize.x, rowPos.y + rowSize.y),
                IM_COL32(90, 120, 220, 60), 6.0f);
        }

        // Difficulty badge
        const std::string badge = item.difficulty.empty() ? "--" : item.difficulty.substr(0, 3);
        ImGui::GetWindowDrawList()->AddRectFilled(rowPos, ImVec2(rowPos.x + 64.0f, rowPos.y + rowSize.y),
            difficultyBadgeColor(item.difficulty, isSelected ? 235 : 170), 6.0f);
        ImGui::GetWindowDrawList()->AddText(
            body, 18.0f,
            ImVec2(rowPos.x + (64.0f - ImGui::CalcTextSize(badge.c_str()).x) * 0.5f, rowPos.y + 22.0f),
            IM_COL32(255, 255, 255, 240), badge.c_str());

        ImGui::SetCursorScreenPos(ImVec2(rowPos.x + 78.0f, rowPos.y + 8.0f));
        ImGui::TextColored(ImVec4(0.96f, 0.97f, 1.0f, 1.0f), "%s",
            item.title.empty() ? item.displayName.c_str() : item.title.c_str());
        ImGui::SetCursorScreenPos(ImVec2(rowPos.x + 78.0f, rowPos.y + 32.0f));
        ImGui::TextColored(ImVec4(0.6f, 0.64f, 0.78f, 1.0f), "%s%s%s",
            item.artist.empty() ? "" : item.artist.c_str(),
            item.level.empty() ? "" : "  Lv.",
            item.level.c_str());

        ImGui::SetCursorScreenPos(rowPos);
        ImGui::InvisibleButton("row", rowSize);
        if (ImGui::IsItemClicked()) {
            selected = i;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            selected = i;
            action = i;
        }
        ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + rowSize.y + 6.0f));
        ImGui::PopID();
    }
    ImGui::EndChild();

    // Keyboard navigation
    if (!entries.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            selected = std::min(selected + 1, static_cast<int>(entries.size()) - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            selected = std::max(selected - 1, 0);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            action = selected;
        }
    }

    // ------------------------------------------------------------------
    // Detail panel
    // ------------------------------------------------------------------
    ImGui::SameLine();
    ImGui::SetCursorPosX(listWidth + pad * 2.0f);
    ImGui::BeginGroup();
    const float detailLeft = listWidth + pad * 2.0f;
    const float jacketSize = std::min(300.0f, h * 0.36f);

    const platform::Renderer::HudSprite* cover = renderer.cover();
    const ImVec2 jacketPos(detailLeft, (h - jacketSize) * 0.5f - 90.0f);
    if (cover != nullptr && cover->id != 0) {
        ImGui::SetCursorScreenPos(jacketPos);
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(cover->id)),
            ImVec2(jacketSize, jacketSize));
    } else {
        ImGui::GetWindowDrawList()->AddRectFilled(jacketPos, ImVec2(jacketPos.x + jacketSize, jacketPos.y + jacketSize),
            IM_COL32(46, 50, 78, 200), 8.0f);
        ImGui::GetWindowDrawList()->AddText(body, 22.0f,
            ImVec2(jacketPos.x + 18.0f, jacketPos.y + jacketSize * 0.45f), IM_COL32(180, 190, 220, 200), "NO JACKET");
    }

    ImGui::SetCursorScreenPos(ImVec2(detailLeft, jacketPos.y + jacketSize + 26.0f));
    ImGui::PushTextWrapPos(w - pad);
    if (selected >= 0 && selected < static_cast<int>(entries.size())) {
        const ChartEntry& item = entries[static_cast<size_t>(selected)];
        if (title != nullptr) {
            ImGui::PushFont(title);
        }
        ImGui::TextColored(ImVec4(0.98f, 0.99f, 1.0f, 1.0f), "%s",
            item.title.empty() ? item.displayName.c_str() : item.title.c_str());
        if (title != nullptr) {
            ImGui::PopFont();
        }
        ImGui::TextColored(ImVec4(0.65f, 0.7f, 0.85f, 1.0f), "%s",
            item.artist.empty() ? "-" : item.artist.c_str());
        if (!item.difficulty.empty() || !item.level.empty()) {
            ImGui::TextColored(ImVec4(0.8f, 0.85f, 1.0f, 1.0f), "%s %s",
                item.difficulty.c_str(), item.level.empty() ? "" : ("Lv." + item.level).c_str());
        }
        ImGui::TextColored(ImVec4(0.45f, 0.5f, 0.62f, 1.0f), "%s", item.susPath.c_str());
        ImGui::TextColored(ImVec4(item.bgmPath.empty() ? 0.75f : 0.5f, item.bgmPath.empty() ? 0.45f : 0.8f, 0.5f, 1.0f),
            "%s", item.bgmPath.empty() ? "BGM: 未找到（将使用静音计时）" : "BGM: 已找到");
    }
    ImGui::PopTextWrapPos();

    ImGui::SetCursorScreenPos(ImVec2(detailLeft, h - pad - 96.0f));
    const bool canStart = selected >= 0 && selected < static_cast<int>(entries.size());
    if (!canStart) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("START  (Enter)", ImVec2(240.0f, 56.0f))) {
        action = selected;
    }
    if (!canStart) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    bool rescan = false;
    if (ImGui::Button("刷新 (F5)", ImVec2(140.0f, 56.0f))) {
        rescan = true;
    }
    ImGui::EndGroup();

    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        rescan = true;
    }
    if (body != nullptr) {
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    (void)windowW;
    (void)windowH;
    return rescan ? SelectRescan : action;
}

} // namespace game
