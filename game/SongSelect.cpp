// CppSekai - song select screen (see SongSelect.hpp).
#include "SongSelect.hpp"
#include "platform/Log.hpp"

#include "Hud.hpp"
#include "Intro.hpp"
#include "Ui.hpp"

// UTF-8 <-> fs::path: the narrow side of fs::path is the ANSI code page and
// throws on a file name it cannot represent (that aborted the chart scan).
#include "path_utf8.hpp"
#include "romaji_search.hpp"

#include "imgui.h"

#include <algorithm>

#include "mmw_preview/vendor/stb_image.h"
#include <array>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <set>
#include <unordered_map>
#include <vector>

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
    // Same table, song id -> its aliases. The search wants it turned around:
    // a query is matched against the aliases as a plain substring, so "smile"
    // finds スマイル*シンフォニー (aliases "smile symphony" / "smilesymphony").
    std::map<int, std::vector<std::string>> gAliasBySong;

    bool aliasMatches(int musicId, const std::string& needleLower)
    {
        const auto it = gAliasIndex.find(needleLower);
        if (it == gAliasIndex.end()) {
            return false;
        }
        const std::vector<int>& ids = it->second;
        return std::find(ids.begin(), ids.end(), musicId) != ids.end();
    }

    // Alias substring: "smile" has to find スマイル*シンフォニー, whose aliases
    // are "smile symphony" / "smiles symphony" / "smilesymphony" - none of which
    // is the query itself. Plain substring on purpose: the table is user data,
    // and a fragment is a perfectly good thing to type, so no length floor and
    // no "must be longer than the query" cleverness.
    bool aliasContains(int musicId, const std::string& needleLower)
    {
        const auto it = gAliasBySong.find(musicId);
        if (it == gAliasBySong.end()) {
            return false;
        }
        for (const std::string& alias : it->second) {
            if (alias.find(needleLower) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // -----------------------------------------------------------------
    // Vocal versions (music-vocals.json, generated from the official
    // musicVocals + gameCharacters tables). Only the *table* lives here; which
    // versions have their mp3 next to the chart is decided per entry by
    // availableVocals().
    // -----------------------------------------------------------------
    std::map<int, std::vector<VocalVersion>> gMusicVocals;

    // -----------------------------------------------------------------
    // 2D MV badges are derived from the mv/ folder next to the charts
    // (charts/mv/<id4>.usm = original, <id4>_sekai.usm = セカイ): the badge
    // set follows what the player downloaded, NOT the master categories -
    // we do not render 3D MVs, so the master's yellow 3D badge would lie.
    // Collaboration labels come from music-collabs.json (official
    // musicCollaborations table).
    // -----------------------------------------------------------------
    // mv/<id4>.usm lookups (songBadges) are relative to EVERY scanned chart
    // root: chartCandidates holds several folders (build/charts plus an older
    // ..\charts), and letting the last candidate overwrite one global path made
    // the badges vanish whenever a later empty candidate had no mv/ folder.
    std::set<std::string> gChartMvDirs;
    std::map<int, SongBadges> gLocalMvBadges;
    std::map<int, std::string> gCollabLabels;
    // Simplified-Chinese ribbon labels (国服译名). Filled from the optional
    // "collabZh" object of music-collabs.json; musicCollab() prefers these.
    std::map<int, std::string> gCollabLabelsZh;

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

    // 默认歌手偏好。main.cpp 改完设置就推过来（setVocalPreference），
    // defaultVocalIndex 每首歌都要读，所以不放参数里传。
    int gVocalPrefMode = 0;
    std::string gVocalPrefSinger;
    // 每次改偏好就 +1，选曲那边靠它判断"偏好变了，把每首歌记住的版本丢掉重挑"。
    int gVocalPrefStamp = 0;

    void setVocalPreference(int mode, const std::string& singer)
    {
        const int clamped = std::clamp(mode, 0, 2);
        if (clamped != gVocalPrefMode || singer != gVocalPrefSinger) {
            ++gVocalPrefStamp;
        }
        gVocalPrefMode = clamped;
        gVocalPrefSinger = singer;
    }
std::string cardArtPath(const std::string& bundle)
{
    if (gSelectAssetDir.empty() || bundle.empty()) {
        return {};
    }
    return gSelectAssetDir + "\\card\\" + bundle + ".png";
}

void setSelectAssetDir(const std::string& dir)
{
    gSelectAssetDir = dir;
}

// ---------------------------------------------------------------------------
// 界面语言（assets/select/<lang>/ 这一层）
//
// 有些贴图把文字**烤进了图里**，没法用字体替换：团体页签的「すべて / その他」
// 是官方 sprite，选中卡的「楽曲Lv.」牌也是。所以这些图按语言分目录：
//
//     assets/select/zh-CN/songlevel.png      歌曲等级
//     assets/select/ja-JP/songlevel.png      楽曲Lv.（官方 txt_musicLv）
//
// selectTex() 先找 <lang>/，再退到 ja-JP/，最后才是 select/ 根下 —— 所以
// 根目录只放与语言无关的图（手机框、按钮、图标），有文字的才进语言目录。
// 默认 zh-CN（本作品界面就是中文）；CPSEKAI_LANG=ja-JP 可切过去，以后接设置项
// 时直接调 setSelectLanguage()。
// ---------------------------------------------------------------------------
static std::string& selectLanguageRef()
{
    static std::string lang = [] {
        const char* env = std::getenv("CPSEKAI_LANG");
        return (env != nullptr && std::strcmp(env, "ja-JP") == 0) ? std::string("ja-JP")
                                                                 : std::string("zh-CN");
    }();
    return lang;
}

void setSelectLanguage(const std::string& lang)
{
    selectLanguageRef() = (lang == "ja-JP") ? "ja-JP" : "zh-CN";
}

static bool selectLangIsJa()
{
    return selectLanguageRef() == "ja-JP";
}

// Profile card (the account) is open. Module state so --profile can force it
// open in a headless run.
bool gProfileOpen = false;

// 右上角菜单抽屉（对应原作 ScreenLayerHeader 的 menuButton -> MenuDialog）。
// gMenuOpen 是目标态，menuUp 是"正在显示或正在收起"（动画期间也要挡住底下的
// 输入，否则关到一半还能点到手机面板）。--select-menu 可强制打开截图。
bool gMenuOpen = false;

bool menuDrawerOpen()
{
    return gMenuOpen;
}

void closeMenuDrawer()
{
    gMenuOpen = false;
}

void debugOpenMenuDrawer(bool open)
{
    gMenuOpen = open;
}

// 最终确认页（ScreenLayerFreeLiveFinalConfirmation 的轻量版）。确定不再直接
// 开始，而是先停在这里；Start 才把 action 交给 main。整个页面在
// drawSongSelect 顶部单独绘制，底下的列表/手机不参与命中测试。
bool gFinalConfirmOpen = false;
int gFinalEntry = -1;
int gFinalDifficulty = 3;
int gFinalVocal = 0;
std::string gFinalToast;
double gFinalToastUntil = 0.0;
void debugOpenProfileCard(bool open)
{
    gProfileOpen = open;
}

// ---------------------------------------------------------------------------
// 切换歌手 (vocal version picker).
//
// The phone panel's round singer button opens a dark panel that slides in from
// the right edge - full window height, only its left corners rounded - with one
// radio card per vocal version the song *has* (availableVocals(): a version
// without its mp3 next to the chart is simply not listed).
//
// This replaces the chip row that used to live in the phone panel: it could not
// fit the singer list of a five-person セカイver, and a radio list reads far
// better than a row of capsules once a song has more than two versions.
//
// The choice is kept per *song* (musicId -> index into availableVocals()), not
// per list cursor: scrolling away and coming back should keep the version the
// player picked here. The phone panel's "Vo." line and the audio hand-off in
// main.cpp both read the same index (drawSongSelect's `vocalIndex`).
// ---------------------------------------------------------------------------
std::map<int, int> gVocalChoiceBySong;
bool gVocalPanelOpen = false;
// 筛选 card (the official screen's 絞り込み popup). It carries 分组依据 now that
// the header row has no room for a second combo, and `alive` keeps it drawn for
// the length of its close animation - gating on `open` alone hides a card in the
// very frame it is dismissed (the bug the 猜歌 card hit).
bool gFilterOpen = false;
bool gFilterAlive = false;
float gVocalPanelScroll = 0.0f; // card list offset, in screen pixels
int gVocalPanelSong = -1;       // musicId the offset belongs to (a new song resets it)

void debugOpenSingerPanel(bool open)
{
    gVocalPanelOpen = open;
}

// zh-CN label of one vocal version, mirroring the official client's captions
// that the reference screenshot shows (虚拟歌手ver. / 「世界」ver. / Another Vocal
// ver.). The rarer types keep the Japanese caption rather than inventing a
// translation nobody would recognise.
std::string vocalVersionLabel(const VocalVersion& version)
{
    if (version.type == "sekai") {
        return "「世界」ver.";
    }
    if (version.type == "virtual_singer" || version.type == "original_song") {
        return "虚拟歌手ver.";
    }
    if (version.type == "another_vocal") {
        return "Another Vocal ver.";
    }
    return version.caption;
}

// Wraps a version's singers into "Vo. A、B、C" lines that fit `maxW` at `size`.
// Continuation lines start at the same left edge as the first one - the
// reference card does that, and it keeps the singer list reading as one block.
std::vector<std::string> vocalSingerLines(const std::vector<std::string>& singers, ImFont* font,
    float size, float maxW)
{
    std::vector<std::string> out;
    if (singers.empty()) {
        return out;
    }
    std::string line = "Vo. ";
    for (const std::string& singer : singers) {
        const std::string piece = line.size() > 4 ? "、" + singer : singer;
        const std::string trial = line + piece;
        if (line.size() > 4 && font->CalcTextSizeA(size, FLT_MAX, 0.0f, trial.c_str()).x > maxW) {
            out.push_back(line);
            line = singer;
        } else {
            line = trial;
        }
    }
    out.push_back(line);
    return out;
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
    // 题库还在建（见 tickGuessPool）：卡片先放上来显示"正在准备题库…"。
    // 以前是点击那一帧同步算完的 —— 12.9k 个别名 × 每首标题都 toLower 一遍，
    // 实测 175ms 一帧卡死（慢机 / 冷缓存上更久），表现就是"点猜歌会卡住"。
    bool loading = false;
    int loadingPercent = 0;
    // newGuessQuestion() 凑不出题（没有别名表 / 池子太小）时置真。卡片留着说明
    // 原因，别让它悄悄消失 —— 那看起来像按钮没反应。
    bool unavailable = false;
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
// 卡片是不是还在屏幕上（含出场动画）。只按 `open` 画的话，一关就是"啪一下消失"，
// 和 keymap / eula 一样要等 beginCard() 的关闭动画播完再停手。
bool gGuessAlive = false;
std::vector<std::pair<std::string, int>> gGuessPool;
bool gGuessPoolBuilt = false;

// 小写标题 -> 所有用这个标题的曲 id。原来"有没有别的歌的标题正好就是这个别名"
// 那条规则是 (别名 x 全部标题) 双层循环，每个组合都 toLower 一份临时 string ——
// 12.9k x ~500 = 6.4M 次，是整个构建里最贵的一段。建一次索引就变成一次哈希查找。
std::unordered_map<std::string, std::vector<int>> gTitleLowerIndex;
bool gTitleLowerIndexBuilt = false;

// 题库的增量构建。items 是别名的快照（gAliasIndex 之后不会再变，但拿 std::map
// 的迭代器没法按下标推进），cursor 就是进度。
struct GuessPoolBuild
{
    bool active = false;
    std::vector<std::pair<std::string, std::vector<int>>> items;
    std::size_t cursor = 0;
    std::vector<std::string> unreadable;
    std::chrono::steady_clock::time_point started{};
    int ticks = 0;        // how many frames it took (logged with the total, see below)
    double cpuMs = 0.0;   // time actually spent inside tickGuessPool(), summed over ticks
};
GuessPoolBuild gGuessBuild;

std::mt19937& guessRng()
{
    static std::mt19937 rng(static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    return rng;
}

void ensureTitleLowerIndex()
{
    if (gTitleLowerIndexBuilt) {
        return;
    }
    gTitleLowerIndexBuilt = true;
    gTitleLowerIndex.clear();
    for (const auto& song : gMusicTitles) {
        gTitleLowerIndex[toLower(song.second)].push_back(song.first);
    }
}

bool guessAliasIsUsable(const std::string& alias, int musicId)
{
    ensureTitleLowerIndex(); // 幂等，一次 bool 检查；漏了它那条曲名冲突规则会静默失效
    if (alias.size() < 2) {
        return false;
    }
    if (std::all_of(alias.begin(), alias.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    // The alias *is* the question, so it has to be readable: a character the
    // font cannot draw (Hangul, Thai, ...) comes out as '?' and turns the
    // question into a guess at what the boxes were. Plenty of the table is
    // Korean / rare-kanji nicknames ("텔유월", "霾儿歌"), and there are ~12k
    // aliases, so dropping them costs nothing.
    if (!fontCoversText(alias)) {
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
    // (Hash lookup instead of walking every title: see gTitleLowerIndex.)
    if (const auto titled = gTitleLowerIndex.find(alias); titled != gTitleLowerIndex.end()) {
        for (const int id : titled->second) {
            if (id != musicId) {
                return false;
            }
        }
    }
    return true;
}

// 开始（或重开）题库构建。真正干活的是 tickGuessPool()，这里只准备数据。
void newGuessQuestion(); // 构建收尾时要直接出一题

void startGuessPoolBuild()
{
    gGuessPool.clear();
    gGuessPoolBuilt = false;
    gGuess.loading = true;
    gGuess.loadingPercent = 0;
    gGuess.options.clear(); // 加载时别留着上一题的四个选项
    gGuessBuild = GuessPoolBuild{};
    gGuessBuild.active = true;
    gGuessBuild.items.reserve(gAliasIndex.size());
    for (const auto& entry : gAliasIndex) {
        gGuessBuild.items.push_back(entry);
    }
    gGuessBuild.started = std::chrono::steady_clock::now();
    // 曲名表是启动时载入的，但**重建索引**而不是沿用可能早于它的缓存：这个调用
    // 发生在用户第一次点猜歌（或 --guess 启动），那时 musics.json 一定已经读完。
    gTitleLowerIndexBuilt = false;
    ensureTitleLowerIndex();
    platform::logf(platform::LogSelect, "[guess] building pool: %zu alias(es)\n", gGuessBuild.items.size());
    std::fflush(stdout);
}

// 把构建往前推一小步（~3ms 一批），返回时卡片照画 —— 这样"点猜歌"那一帧永远不会
// 被题库占住。构建完成时直接出一题。
void tickGuessPool()
{
    if (!gGuessBuild.active) {
        return;
    }
    const std::size_t total = gGuessBuild.items.size();
    ++gGuessBuild.ticks;
    const auto tickStart = std::chrono::steady_clock::now();
    const auto deadline = tickStart + std::chrono::microseconds(3000);
    while (gGuessBuild.cursor < total) {
        // 64 个一批再看表：steady_clock::now() 本身也要几十纳秒，逐个看划算不来。
        const std::size_t stop = std::min(total, gGuessBuild.cursor + 64);
        for (; gGuessBuild.cursor < stop; ++gGuessBuild.cursor) {
            const auto& entry = gGuessBuild.items[gGuessBuild.cursor];
            if (entry.second.size() != 1) {
                continue;
            }
            const int musicId = entry.second.front();
            if (guessAliasIsUsable(entry.first, musicId)) {
                gGuessPool.emplace_back(entry.first, musicId);
            } else if (!fontCoversText(entry.first)) {
                gGuessBuild.unreadable.push_back(entry.first);
            }
        }
        gGuess.loadingPercent = total > 0 ? static_cast<int>(gGuessBuild.cursor * 100u / total) : 100;
        if (std::chrono::steady_clock::now() >= deadline) {
            gGuessBuild.cpuMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - tickStart).count();
            return;
        }
    }

    gGuessBuild.active = false;
    gGuessPoolBuilt = true;
    gGuess.loading = false;
    gGuess.loadingPercent = 100;
    gGuessBuild.cpuMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tickStart).count();
    // 两个数都给：cpu 是这个构建真正花掉的 CPU（每帧 ≤3ms，就是"不卡"的证据），
    // wall 是墙钟 —— 启动那几帧本来就要 200ms 加载资源，墙钟会被它拉得很长，
    // 单看墙钟会以为构建本身很慢。
    const double wallMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - gGuessBuild.started).count();
    platform::logf(platform::LogSelect, "[guess] pool built: cpu %.0f ms over %d frame(s), wall %.0f ms\n",
        gGuessBuild.cpuMs, gGuessBuild.ticks, wallMs);
    platform::logf(platform::LogSelect, "[guess] %zu usable alias(es) of %zu (%zu of them dropped: the font cannot "
                "draw part of the alias)\n",
        gGuessPool.size(), gAliasIndex.size(), gGuessBuild.unreadable.size());
    // A few examples, so "the pool shrank" can be told apart from "the font
    // broke": each line names the alias and the character that has no glyph.
    for (std::size_t i = 0; i < gGuessBuild.unreadable.size() && i < 3; ++i) {
        platform::logf(platform::LogSelect, "[guess]   unreadable: %s\n", gGuessBuild.unreadable[i].c_str());
    }
    if (!gGuessBuild.unreadable.empty() && !gGuessPool.empty()) {
        platform::logf(platform::LogSelect, "[guess]   example kept: %s\n", gGuessPool.front().first.c_str());
    }
    std::fflush(stdout);
    newGuessQuestion();
}

// Builds one question. Marks gGuess.unavailable when there is nothing to ask
// (no alias table next to the exe, or a pool too small to fill four options) -
// the card stays up and says so instead of vanishing.
void newGuessQuestion()
{
    gGuess.unavailable = false;
    if (gGuess.loading) {
        return; // 题库还没建完：等 tickGuessPool() 收尾时自己会来要题
    }
    if (!gGuessPoolBuilt) {
        startGuessPoolBuild();
        return;
    }
    if (gGuessPool.size() < 4 || gMusicTitles.size() < 4) {
        gGuess.unavailable = true;
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
        gGuess.unavailable = true;
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
    platform::logf(platform::LogSelect, "[guess] \"%s\" -> %04d %s |", gGuess.alias.c_str(), answerId,
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

// 打开猜歌卡片（header 那颗按钮）。**立刻**把卡片放上来，题库在随后的帧里建
// （tickGuessPool），所以点击那一帧不会被 12.9k 个别名占住。
void openGuessDialog()
{
    gGuess.open = true;
    gGuess.unavailable = false;
    gGuess.picked = -1;
    if (gGuessPoolBuilt) {
        if (gGuess.options.size() != 4) {
            newGuessQuestion(); // 上次没出成题（比如池子太小），再试一次
        }
        return;
    }
    if (!gGuessBuild.active) {
        startGuessPoolBuild();
    }
}

// Debug (`--guess`): open the quiz at boot, so a --screenshot run can look at
// it without a click to hit.
void debugOpenGuessDialog(bool open)
{
    if (open) {
        openGuessDialog();
    } else {
        gGuess.open = false;
    }
}

bool guessDialogOpen()
{
    return gGuess.open;
}

// 筛选 card: main.cpp's ESC chain asks this so ESC closes the card instead of
// dropping out of the song-select screen.
bool filterDialogOpen()
{
    return gFilterOpen;
}

void debugOpenFilterCard(bool open)
{
    gFilterOpen = open;
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

// 收藏是按 musicId 记的（同一首曲子的四个难度共享），所以这里按 id 匹配而不是按
// 文件名 —— 换一个难度重新下载谱面也不会掉收藏。
void applyFavorites(std::vector<ChartEntry>& entries, const std::vector<int>& favoriteMusicIds)
{
    for (ChartEntry& e : entries) {
        e.favorite = e.musicId > 0
            && std::find(favoriteMusicIds.begin(), favoriteMusicIds.end(), e.musicId)
                != favoriteMusicIds.end();
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
    // userDataPath() hands back UTF-8. Letting that std::string flow straight
    // into an fs::path (as this line used to do) makes libc++ decode it with the
    // *local ANSI code page*, which on a cp936 machine turns
    // `D:\...\中文安装目录\子目录` into a mojibake name - and if the bytes are not
    // valid GBK at all, it throws `filesystem_error: __char_to_wide: Illegal
    // byte sequence`, which nothing catches, so the process dies before the
    // window ever appears. Go back through path_utf8::toPath().
    const fs::path file = toFsPath(userDataPath(exeDir));
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

double liveBonusExpMultiplier(int bonus)
{
    // 官方表见头文件。这里的写法就是把那张表折成两段：
    //   0        -> 1
    //   1..5     -> 5 * n        （×5 ×10 ×15 ×20 ×25）
    //   n >= 6   -> 20 + n       （×26 ×27 ×28 ×29 ×30，官方数据到此为止）
    // 11 档以上照第二段继续，属于本项目自己的延伸 —— 官方没有这些档位。
    if (bonus <= 0) {
        return 1.0;
    }
    if (bonus <= 5) {
        return 5.0 * static_cast<double>(bonus);
    }
    return 20.0 + static_cast<double>(bonus);
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
            settings.padRumble = s.value("padRumble", settings.padRumble);
            settings.rumbleFlick = s.value("rumbleFlick", settings.rumbleFlick);
            settings.rumbleCritical = s.value("rumbleCritical", settings.rumbleCritical);
            settings.rumbleMiss = s.value("rumbleMiss", settings.rumbleMiss);
            settings.offsetSec = s.value("offsetSec", settings.offsetSec);
            settings.leadInSec = s.value("leadInSec", settings.leadInSec);
            settings.windowMode = s.value("windowMode", settings.windowMode);
            settings.windowWidth = s.value("windowWidth", settings.windowWidth);
            settings.windowHeight = s.value("windowHeight", settings.windowHeight);
            settings.windowX = s.value("windowX", settings.windowX);
            settings.windowY = s.value("windowY", settings.windowY);
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
            settings.simpleEffects = s.value("simpleEffects", settings.simpleEffects);
            settings.showFps = s.value("showFps", settings.showFps);
            settings.laneKeys = s.value("laneKeys", settings.laneKeys);
            settings.favoriteMusicIds = s.value("favoriteMusicIds", settings.favoriteMusicIds);
            settings.recentSusPaths = s.value("recentSusPaths", settings.recentSusPaths);
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
            settings.deckPower = s.value("deckPower", settings.deckPower);
            settings.mvMode = s.value("mvMode", settings.mvMode);
            settings.strictFlick = s.value("strictFlick", settings.strictFlick);
            settings.vocalPref = std::clamp(s.value("vocalPref", settings.vocalPref), 0, 2);
            settings.vocalPrefSinger = s.value("vocalPrefSinger", settings.vocalPrefSinger);
            settings.logMask = s.value("logMask", settings.logMask) & 0xFFu;
            settings.flickAsTap = s.value("flickAsTap", settings.flickAsTap);
            settings.debugLog = s.value("debugLog", settings.debugLog);
            settings.autoplay = s.value("autoplay", settings.autoplay);
            settings.autoPauseOnBlur = s.value("autoPauseOnBlur", settings.autoPauseOnBlur);
            settings.reportSmtc = s.value("reportSmtc", settings.reportSmtc);
            settings.muteWhenMinimized = s.value("muteWhenMinimized", settings.muteWhenMinimized);
            settings.switchImeOnStart = s.value("switchImeOnStart", settings.switchImeOnStart);
            settings.splashStyle = s.value("splashStyle", settings.splashStyle);
            settings.bgStyle = s.value("bgStyle", settings.bgStyle);
            settings.glassMode = s.value("glassMode", settings.glassMode);
            settings.nativeMenuBar = s.value("nativeMenuBar", settings.nativeMenuBar);
            settings.bgBlur = s.value("bgBlur", settings.bgBlur);
            settings.bgDim = s.value("bgDim", settings.bgDim);
            settings.uiScale = s.value("uiScale", settings.uiScale);
            settings.sortMode = s.value("sortMode", settings.sortMode);
            settings.groupMode = s.value("groupMode", settings.groupMode);
            settings.unitTab = s.value("unitTab", settings.unitTab);
            settings.liveBonus = s.value("liveBonus", settings.liveBonus);
            // Missing in an old profile: the ELUA shows once, which is exactly
            // what an existing install should see after this version lands.
            settings.eulaAccepted = s.value("eulaAccepted", settings.eulaAccepted);
            settings.tutorialDone = s.value("tutorialDone", settings.tutorialDone);
        }
        if (wrapped && doc.contains("account") && doc["account"].is_object()) {
            const nlohmann::json& a = doc["account"];
            account.name = a.value("name", account.name);
            account.org = a.value("org", account.org);
            account.note = a.value("note", account.note);
        account.card = a.value("card", account.card);
            account.rank = a.value("rank", account.rank);
            account.exp = a.value("exp", account.exp);
            account.plays = a.value("plays", account.plays);
            account.totalScore = a.value("totalScore", account.totalScore);
        }
    } catch (...) {
        // malformed file: keep the defaults
    }
    // Profiles written before 2026-11-01 carry the old "标准" preset
    // (40 / 90 / 140 / 200), which was never the official table - it let a
    // press 200 ms late count as BAD. Those four numbers are not a choice
    // anybody made, they were the default, so move them to the real one
    // (42 / 83 / 108 / 125) and say so once.
    if (std::fabs(settings.perfectMs - 40.0f) < 0.01f
        && std::fabs(settings.greatMs - 90.0f) < 0.01f
        && std::fabs(settings.goodMs - 140.0f) < 0.01f
        && std::fabs(settings.badMs - 200.0f) < 0.01f) {
        settings.perfectMs = 42.0f;
        settings.greatMs = 83.0f;
        settings.goodMs = 108.0f;
        settings.badMs = 125.0f;
        if (std::fabs(settings.missMs - 200.0f) < 0.01f) {
            settings.missMs = 125.0f;
        }
        platform::logf(platform::LogSystem, "[settings] judgement windows migrated to the official table "
                    "(42/83/108/125)\n");
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
    // 综合力: 1000 是「怎么都得不了几分」的下限, 500000 是四星满编的两倍.
    settings.deckPower = std::clamp(settings.deckPower, 1000, 500000);
    settings.mvMode = std::clamp(settings.mvMode, 0, 2);
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
    settings.groupMode = std::clamp(settings.groupMode, 0, kSelectGroupCount - 1);
    settings.unitTab = std::clamp(settings.unitTab, 0, kSelectUnitTabCount - 1);
    // 演出能量：手改过的档案（负数 / 1000）不该让结算经验飞出去。
    settings.liveBonus = std::clamp(settings.liveBonus, 0, 100);

    // Account: a rank past the cap (a hand-edited file) would make
    // expToNextRank() return 0 and freeze the bar, so clamp it. Exp is left
    // alone - addPlayerExp() rolls any excess over on the next run.
    account.rank = std::clamp(account.rank, 1, kMaxPlayerRank);
    account.exp = std::max(account.exp, 0.0);
    account.plays = std::max(account.plays, 0);
}

bool isUserDataFile(const std::string& path)
{
    std::ifstream file(toFsPath(path), std::ios::binary);
    if (!file) {
        return false;
    }
    try {
        const nlohmann::json doc = nlohmann::json::parse(file);
        return doc.is_object() && (doc.contains("settings") || doc.contains("scores"));
    } catch (const std::exception&) {
        return false; // not json at all, or truncated
    }
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
        {"padRumble", settings.padRumble},
        {"rumbleFlick", settings.rumbleFlick},
        {"rumbleCritical", settings.rumbleCritical},
        {"rumbleMiss", settings.rumbleMiss},
        {"offsetSec", settings.offsetSec},
        {"leadInSec", settings.leadInSec},
        {"windowMode", settings.windowMode},
        {"windowWidth", settings.windowWidth},
        {"windowHeight", settings.windowHeight},
        {"windowX", settings.windowX},
        {"windowY", settings.windowY},
        {"renderScale", settings.renderScale},
        {"instanceMode", settings.instanceMode},
        {"multiInstanceAccepted", settings.multiInstanceAccepted},
        {"fpsLimit", settings.fpsLimit},
        {"multiplayer", settings.multiplayer},
        {"showProgressBar", settings.showProgressBar},
        {"hideTouchFeedback", settings.hideTouchFeedback},
        {"simpleEffects", settings.simpleEffects},
        {"showFps", settings.showFps},
        {"laneKeys", settings.laneKeys},
        {"favoriteMusicIds", settings.favoriteMusicIds},
        {"recentSusPaths", settings.recentSusPaths},
        {"perfectMs", settings.perfectMs},
        {"greatMs", settings.greatMs},
        {"goodMs", settings.goodMs},
        {"badMs", settings.badMs},
        {"missMs", settings.missMs},
        {"linkBadMiss", settings.linkBadMiss},
        {"holdTailGraceMs", settings.holdTailGraceMs},
        {"holdStartGraceMs", settings.holdStartGraceMs},
        {"initialLife", settings.initialLife},
        {"deckPower", settings.deckPower},
        {"mvMode", settings.mvMode},
        {"strictFlick", settings.strictFlick},
        {"vocalPref", settings.vocalPref},
        {"vocalPrefSinger", settings.vocalPrefSinger},
        {"logMask", settings.logMask},
        {"flickAsTap", settings.flickAsTap},
        {"debugLog", settings.debugLog},
        {"autoplay", settings.autoplay},
        {"autoPauseOnBlur", settings.autoPauseOnBlur},
        {"reportSmtc", settings.reportSmtc},
        {"muteWhenMinimized", settings.muteWhenMinimized},
        {"switchImeOnStart", settings.switchImeOnStart},
        {"splashStyle", settings.splashStyle},
        {"bgStyle", settings.bgStyle},
        {"glassMode", settings.glassMode},
        {"nativeMenuBar", settings.nativeMenuBar},
        {"bgBlur", settings.bgBlur},
        {"bgDim", settings.bgDim},
        {"uiScale", settings.uiScale},
        {"sortMode", settings.sortMode},
        {"groupMode", settings.groupMode},
        {"unitTab", settings.unitTab},
        {"liveBonus", settings.liveBonus},
        {"eulaAccepted", settings.eulaAccepted},
        {"tutorialDone", settings.tutorialDone},
    };
    doc["scores"] = scoreDoc;
    doc["account"] = {
        {"name", account.name},
        {"org", account.org},
        {"note", account.note},
        {"card", account.card},
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
    // mv/<id4>.usm lookups (songBadges) are relative to the scan root - the
    // same convention the playback side uses (<chart dir>/mv/). Absolute so a
    // later cwd change cannot break the lookups. Only registered when the mv/
    // folder actually exists, so an empty candidate cannot shadow a real one.
    std::error_code absEc;
    const fs::path mvDir = fs::absolute(root, absEc) / "mv";
    if (fs::is_directory(mvDir, ec)) {
        gChartMvDirs.insert(fromFsPath(mvDir));
    }
    gLocalMvBadges.clear();

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
        platform::logf(platform::LogSelect, "[scan] %d chart(s): header %.0f ms, sidecar %.0f ms\n", scanned, headerMs, metadataMs);
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
    // Official difficulty tints, read off `ScreenLayerFreeLiveCellPhone` (704):
    // the pads' FrontGlowCover (eff_btn_circle_h80_a70_wh) carries exactly these
    // colours, so they are the game's own EASY/NORMAL/HARD/EXPERT/MASTER values -
    // not an approximation picked off a screenshot.
    const ImU32 kDiffColors[kDiffCount] = {
        IM_COL32(17, 221, 119, 255),   // EASY   #11DD77
        IM_COL32(51, 204, 255, 255),   // NORMAL #33CCFF
        IM_COL32(255, 204, 0, 255),    // HARD   #FFCC00
        IM_COL32(255, 68, 119, 255),   // EXPERT #FF4477
        IM_COL32(204, 51, 255, 255),   // MASTER #CC33FF
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
        int unit = 0;    // 团体页签 (CategoryTabType), see unitForMusic()
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
    // 团体页签 (CategoryTabType). The left strip of the official screen is
    // MusicSelectUnitTabList (prefab 1117: 136 wide, x=44, inset 84 top /
    // bottom) filled with one MusicSelectUnitTab (prefab 1118: cell 112x100,
    // logo 112x112) per tab. Which tab a song sits under is NOT a master
    // field: the game derives it from the singers of the song's セカイ
    // version (MasterMusicVocalCharacter.GetUnitType() -> gameCharacters.unit,
    // RVA 0x614DB60 / 0x6174F90), so what follows is that same 26-row
    // singer -> unit table plus the same fallback order:
    //   1. use the セカイ ver, else the original-song ver, else the VS-only
    //      ver (music-vocals.json, already loaded for the panel);
    //   2. the first singer with a real unit decides (a "初音ミク + 星乃一歌"
    //      duet is Leo/need, because the unit singer wins over the VS);
    //   3. only virtual singers -> バーチャル・シンガー;
    //   4. only outside characters (collab guests, "#35" in music-vocals) ->
    //      その他.
    // The numbers are CategoryTabType, i.e. the tab index itself.
    // -----------------------------------------------------------------
    constexpr int kUnitVocaloid = 1;
    constexpr int kUnitLightSound = 2;
    constexpr int kUnitIdol = 3;
    constexpr int kUnitStreet = 4;
    constexpr int kUnitThemePark = 5;
    constexpr int kUnitSchoolRefusal = 6;
    constexpr int kUnitOther = 7;

    int singerUnit(const std::string& name)
    {
        static const std::map<std::string, int> table = {
            {"初音ミク", kUnitVocaloid}, {"鏡音リン", kUnitVocaloid}, {"鏡音レン", kUnitVocaloid},
            {"巡音ルカ", kUnitVocaloid}, {"MEIKO", kUnitVocaloid}, {"KAITO", kUnitVocaloid},
            {"星乃一歌", kUnitLightSound}, {"天馬咲希", kUnitLightSound},
            {"望月穂波", kUnitLightSound}, {"日野森志歩", kUnitLightSound},
            {"花里みのり", kUnitIdol}, {"桐谷遥", kUnitIdol},
            {"桃井愛莉", kUnitIdol}, {"日野森雫", kUnitIdol},
            {"小豆沢こはね", kUnitStreet}, {"白石杏", kUnitStreet},
            {"東雲彰人", kUnitStreet}, {"青柳冬弥", kUnitStreet},
            {"天馬司", kUnitThemePark}, {"鳳えむ", kUnitThemePark},
            {"草薙寧々", kUnitThemePark}, {"神代類", kUnitThemePark},
            {"宵崎奏", kUnitSchoolRefusal}, {"朝比奈まふゆ", kUnitSchoolRefusal},
            {"東雲絵名", kUnitSchoolRefusal}, {"暁山瑞希", kUnitSchoolRefusal},
        };
        const auto it = table.find(name);
        return it == table.end() ? 0 : it->second;
    }

    int unitForMusic(int musicId)
    {
        static std::map<int, int> cache;
        const auto hit = cache.find(musicId);
        if (hit != cache.end()) {
            return hit->second;
        }
        int unit = kUnitOther;
        const auto voc = gMusicVocals.find(musicId);
        if (voc != gMusicVocals.end()) {
            // The three version kinds the game itself picks from, in its order.
            static const char* const kPreferred[3] = {"sekai", "original_song", "virtual_singer"};
            const VocalVersion* picked = nullptr;
            for (const char* want : kPreferred) {
                for (const VocalVersion& v : voc->second) {
                    if (v.type == want) {
                        picked = &v;
                        break;
                    }
                }
                if (picked != nullptr) {
                    break;
                }
            }
            if (picked == nullptr && !voc->second.empty()) {
                picked = &voc->second.front();
            }
            if (picked != nullptr) {
                bool vocaloid = false;
                unit = 0;
                for (const std::string& singer : picked->singers) {
                    const int u = singerUnit(singer);
                    if (u == kUnitVocaloid) {
                        vocaloid = true;
                    } else if (u != 0 && unit == 0) {
                        unit = u;
                    }
                }
                if (unit == 0) {
                    unit = vocaloid ? kUnitVocaloid : kUnitOther;
                }
            }
        }
        cache.emplace(musicId, unit);
        return unit;
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
    constexpr int kGroupFavorite = 4; // 收藏 / 其他（右键曲目切换）
    // 上限与公开常量同源（SongSelect.hpp）—— loadUserData 的 clamp 也用它。
    constexpr int kGroupCount = kSelectGroupCount;

    // Sort + optionally group the filtered songs into display rows. Rows are
    // what the (cyclic) scroll model walks: a header takes a slot like a song,
    // and the selection always lands on a song rather than a header.
    // 这个曲目在不在收藏夹里。收藏按 musicId 记，一个 group 的四个难度共享同一个
    // 值，所以取第一个存在的难度问一句就够。
    bool groupFavorite(const SongGroup& group, const std::vector<ChartEntry>& entries)
    {
        for (int d = 0; d < kDiffCount; ++d) {
            const int index = group.idx[d];
            if (index >= 0 && index < static_cast<int>(entries.size())) {
                return entries[static_cast<size_t>(index)].favorite;
            }
        }
        return false;
    }

    std::vector<ListRow> buildRows(const std::vector<SongGroup>& groups, const std::vector<int>& visible,
        const std::vector<ChartEntry>& entries, int diffIndex, int order, int groupMode)
    {
        std::vector<int> ordered = visible;
        const auto levelOf = [&](int gi) { return levelForDifficulty(groups[static_cast<size_t>(gi)], entries, diffIndex); };
        std::stable_sort(ordered.begin(), ordered.end(), [&](int a, int b) {
            if (groupMode == kGroupFavorite) {
                // 「收藏 / 其他」两段：收藏的那一段整体排前面，段内还是按下面那套。
                const bool fa = groupFavorite(groups[static_cast<size_t>(a)], entries);
                const bool fb = groupFavorite(groups[static_cast<size_t>(b)], entries);
                if (fa != fb) {
                    return fa;
                }
            }
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
                        : groupMode == kGroupFavorite
                            ? (groupFavorite(groups[static_cast<size_t>(gi)], entries) ? "收藏" : "其他")
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
                group.unit = unitForMusic(item.musicId);
                group.kana = item.kana.empty() ? pronunciationFor(item.musicId) : item.kana;
                groups.push_back(group);
                titleIsReal.push_back(!item.title.empty());
            }
            SongGroup& group = groups[static_cast<size_t>(it->second)];
            if (group.musicId <= 0) {
                group.musicId = item.musicId;
                group.unit = unitForMusic(item.musicId);
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

    // Song-list cover thumbnails.
    //
    // 2026-10-25 memory audit, two problems in one small function:
    //   ① The cover files are 740x740, and this used to upload them at full
    //     size: 2.09 MB of texture each. The row draws at 60*k px and the phone
    //     panel's jacket at ~200 px, so most of that never reaches a pixel.
    //     `maxDim` 256 box-downscales to 370x370 (0.55 MB) - still 1.8x the
    //     largest place it is drawn.
    //   ② The map only ever grew. Browsing a few hundred songs left every cover
    //     resident for the rest of the session; this was the only part of the
    //     process that kept climbing after boot. It is a capped LRU now.
    //
    // The cap only has to exceed "covers on screen at once" (about a dozen rows
    // plus the phone panel), otherwise the eviction could delete a texture that
    // is still being drawn this frame.
    constexpr std::size_t kThumbCacheLimit = 64;
    struct Thumb
    {
        GLuint id = 0;
        unsigned long long used = 0;
    };

    GLuint thumbFor(platform::Renderer& renderer, const std::string& path)
    {
        static std::map<std::string, Thumb> cache;
        static unsigned long long tick = 0;
        if (path.empty()) {
            return 0;
        }
        ++tick;
        const auto it = cache.find(path);
        if (it != cache.end()) {
            it->second.used = tick;
            return it->second.id;
        }
        if (cache.size() >= kThumbCacheLimit) {
            auto oldest = cache.begin();
            for (auto i = cache.begin(); i != cache.end(); ++i) {
                if (i->second.used < oldest->second.used) {
                    oldest = i;
                }
            }
            renderer.releaseUiTexture(oldest->second.id);
            cache.erase(oldest);
        }
        std::string err;
        const GLuint id = renderer.loadUiTexture(path, err, 256);
        cache.emplace(path, Thumb{id, tick});
        return id;
    }

    void addDiamond(ImDrawList* dl, const ImVec2& c, float r, ImU32 fill)
    {
        dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), fill);
        dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y),
            IM_COL32(25, 25, 40, 200), 1.5f);
    }

    // Select-background sprites (assets/mmw/overlay/select_bg, exported from
    // the UI atlas). Same caching scheme as badgeTex, different directory.
    GLuint selectBgTex(platform::Renderer& renderer, const char* name)
    {
        static std::map<std::string, GLuint> cache;
        auto it = cache.find(name);
        if (it == cache.end()) {
            std::string err;
            const GLuint id = gSelectAssetDir.empty()
                ? 0
                : renderer.loadUiTexture(gSelectAssetDir + "\\mmw\\overlay\\select_bg\\" + name + ".png", err);
            it = cache.emplace(name, id).first;
        }
        return it->second;
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
    // song info: the official RankBase disc + the official rank glyph.
    //   RankBase : btn_circle_h96_a20_wh  112x112, white at 20% **baked into the
    //              sprite** (m_Color is plain white) - not a 32% hand-tinted disc.
    //   Rank     : icon_scoreRank_s_<a..s>  68x76, pre-coloured per rank
    //              (A purple / S pink / ...), 68x76 inside the 112 disc.
    // `--` still stands in for "no record at all".
    GLuint selectTex(platform::Renderer& renderer, const char* name);
    void drawBestScoreBadge(ImDrawList* dl, platform::Renderer& renderer, ImFont* font,
        double bestScore, float chartRating, float diameter, float centerX, float centerY)
    {
        const ImVec2 center(centerX, centerY);
        const float k = diameter / 112.0f; // 112 = the prefab's RankBase size
        const bool played = bestScore > 0.0;

        const GLuint baseTex = selectTex(renderer, "btn_circle_h96_a20_wh");
        if (baseTex != 0) {
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(baseTex)),
                ImVec2(center.x - 56.0f * k, center.y - 56.0f * k),
                ImVec2(center.x + 56.0f * k, center.y + 56.0f * k));
        } else {
            dl->AddCircleFilled(center, diameter * 0.5f, IM_COL32(255, 255, 255, 51), 48);
        }

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
        const std::string texName = std::string("icon_scoreRank_s_") + letter;
        const GLuint glyph = selectTex(renderer, texName.c_str());
        if (glyph != 0) {
            // Rank node is 68x76 inside the 112 RankBase (pivot centred).
            const float gw = 68.0f * k;
            const float gh = 76.0f * k;
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(glyph)),
                ImVec2(center.x - gw * 0.5f, center.y - gh * 0.5f),
                ImVec2(center.x + gw * 0.5f, center.y + gh * 0.5f));
        }
    }

    // The handset's screen shape (rounded corners + the notch), used to keep the
    // enlarged jacket inside the bezel. assets/select/mask_smartphone.png is the
    // game's own mask (white = visible, alpha only). Loaded once, straight into
    // RAM - it is only ever sampled on the CPU, never drawn.
    struct PhoneMaskImage
    {
        int w = 0;
        int h = 0;
        std::vector<unsigned char> alpha;
    };
    const PhoneMaskImage* phoneMask()
    {
        static PhoneMaskImage mask;
        static bool tried = false;
        if (tried) {
            return mask.w > 0 ? &mask : nullptr;
        }
        tried = true;
        if (gSelectAssetDir.empty()) {
            return nullptr;
        }
        const std::string path = gSelectAssetDir + "\\select\\mask_smartphone.png";
        std::ifstream file(path_utf8::toPath(path), std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        int w = 0;
        int h = 0;
        unsigned char* px = bytes.empty()
            ? nullptr
            : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, nullptr, 4);
        if (px == nullptr || w <= 0 || h <= 0) {
            platform::logf(platform::LogSelect, "[select] phone mask MISSING (%s)\n", path.c_str());
            std::fflush(stdout);
            if (px != nullptr) {
                stbi_image_free(px);
            }
            return nullptr;
        }
        mask.w = w;
        mask.h = h;
        platform::logf(platform::LogSelect, "[select] phone mask %dx%d\n", w, h);
        std::fflush(stdout);
        mask.alpha.resize(static_cast<size_t>(w) * h);
        for (size_t i = 0; i < mask.alpha.size(); ++i) {
            mask.alpha[i] = px[i * 4 + 3];
        }
        stbi_image_free(px);
        return &mask;
    }

    // -------------------------------------------------------------------
    // Blurred jacket for the phone screen's backdrop
    // (ScreenLayerFreeLiveCellPhone > JacketContent > JacketMask > Jacket >
    //  UIPartsBlurMusicJacket - the same art, scaled up and smeared).
    //
    // Done on the CPU, once per song: stb decodes, a box average down to 24x24
    // *is* the blur (the GPU's linear filter turns that back into a smooth
    // gradient when it is stretched over ~330 px), and one more 3x3 pass takes
    // the box edges out. Re-decoding is wasteful, so the texture is cached per
    // path - switching back and forth between two songs costs nothing.
    // `u0,v0,u1,v1` = where this quad sits inside the handset, 0..1. They are
    // only used to sample the screen mask, so the smear stops at the bezel and
    // the notch instead of painting over them.
    GLuint blurredJacketTex(platform::Renderer& renderer, const std::string& path,
        float u0, float v0, float u1, float v1)
    {
        if (path.empty()) {
            return 0;
        }
        static std::map<std::string, GLuint> cache;
        char key[64];
        std::snprintf(key, sizeof(key), "|%.3f,%.3f,%.3f,%.3f", u0, v0, u1, v1);
        const std::string cacheKey = path + key;
        const auto hit = cache.find(cacheKey);
        if (hit != cache.end()) {
            return hit->second;
        }

        GLuint tex = 0;
        int w = 0;
        int h = 0;
        // Read the bytes through std::filesystem (it takes a UTF-8 path) and
        // hand stb the buffer: stbi_load() ends up in fopen(), which on Windows
        // cannot open anything outside the ANSI codepage, and a chart folder
        // with a Chinese name is an obvious case.
        std::ifstream file(path_utf8::toPath(path), std::ios::binary);
        std::vector<unsigned char> fileBytes((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        unsigned char* px = fileBytes.empty()
            ? nullptr
            : stbi_load_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()), &w, &h,
                nullptr, 4);
        if (px != nullptr && w > 0 && h > 0) {
            constexpr int kSize = 24;
            std::vector<unsigned char> small(static_cast<size_t>(kSize) * kSize * 4, 0);
            for (int y = 0; y < kSize; ++y) {
                const int y0 = y * h / kSize;
                const int y1 = std::max(y0 + 1, (y + 1) * h / kSize);
                for (int x = 0; x < kSize; ++x) {
                    const int x0 = x * w / kSize;
                    const int x1 = std::max(x0 + 1, (x + 1) * w / kSize);
                    unsigned int r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int sy = y0; sy < y1; ++sy) {
                        for (int sx = x0; sx < x1; ++sx) {
                            const unsigned char* p = px + (static_cast<size_t>(sy) * w + sx) * 4;
                            r += p[0];
                            g += p[1];
                            b += p[2];
                            a += p[3];
                            ++n;
                        }
                    }
                    unsigned char* o = &small[(static_cast<size_t>(y) * kSize + x) * 4];
                    o[0] = static_cast<unsigned char>(r / n);
                    o[1] = static_cast<unsigned char>(g / n);
                    o[2] = static_cast<unsigned char>(b / n);
                    o[3] = static_cast<unsigned char>(a / n);
                }
            }
            // 3x3 box over the small image, so the downsample's hard block edges
            // do not survive as visible squares once it is blown back up.
            std::vector<unsigned char> smooth = small;
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    unsigned int r = 0, g = 0, b = 0, a = 0, n = 0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int sx = std::clamp(x + dx, 0, kSize - 1);
                            const int sy = std::clamp(y + dy, 0, kSize - 1);
                            const unsigned char* p = &small[(static_cast<size_t>(sy) * kSize + sx) * 4];
                            r += p[0];
                            g += p[1];
                            b += p[2];
                            a += p[3];
                            ++n;
                        }
                    }
                    unsigned char* o = &smooth[(static_cast<size_t>(y) * kSize + x) * 4];
                    o[0] = static_cast<unsigned char>(r / n);
                    o[1] = static_cast<unsigned char>(g / n);
                    o[2] = static_cast<unsigned char>(b / n);
                    o[3] = static_cast<unsigned char>(a / n);
                }
            }
            // Bake a soft edge into the alpha. The jacket is drawn on top of
            // this, so only the band *outside* the art is ever visible - and a
            // quad with a hard edge reads as a pasted rectangle. Chevron
            // (max of |dx|,|dy|) distance keeps the shape rectangular while
            // fading the last ~45% out to nothing, which is what an enlarged,
            // blurred copy looks like once it has been through a blur radius.
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    const float nx = (static_cast<float>(x) + 0.5f) / kSize * 2.0f - 1.0f;
                    const float ny = (static_cast<float>(y) + 0.5f) / kSize * 2.0f - 1.0f;
                    const float d = std::max(std::fabs(nx), std::fabs(ny));
                    const float t = std::clamp((1.0f - d) / 0.38f, 0.0f, 1.0f);
                    const float fade = t * t * (3.0f - 2.0f * t);
                    unsigned char* o = &smooth[(static_cast<size_t>(y) * kSize + x) * 4];
                    o[3] = static_cast<unsigned char>(static_cast<float>(o[3]) * fade);
                }
            }
            // Blow the 24x24 blur back up on the CPU, and while doing it sample
            // the handset's screen mask. Two reasons for the extra pass: the
            // mask needs far more than 24 texels to describe the notch, and the
            // bilinear sample here is what the GPU would have done anyway.
            constexpr int kOut = 96;
            std::vector<unsigned char> out(static_cast<size_t>(kOut) * kOut * 4, 0);
            const PhoneMaskImage* mask = phoneMask();
            for (int y = 0; y < kOut; ++y) {
                const float v = (static_cast<float>(y) + 0.5f) / kOut;
                const float fy = v * kSize - 0.5f;
                const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, kSize - 1);
                const int y1 = std::min(y0 + 1, kSize - 1);
                const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
                // Screen-space v inside the handset: the quad only covers part
                // of it, so the mask lookup has to follow the quad, not the
                // whole texture.
                const float mv = v0 + v * (v1 - v0);
                for (int x = 0; x < kOut; ++x) {
                    const float u = (static_cast<float>(x) + 0.5f) / kOut;
                    const float fx = u * kSize - 0.5f;
                    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, kSize - 1);
                    const int x1 = std::min(x0 + 1, kSize - 1);
                    const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
                    const unsigned char* p00 = &smooth[(static_cast<size_t>(y0) * kSize + x0) * 4];
                    const unsigned char* p10 = &smooth[(static_cast<size_t>(y0) * kSize + x1) * 4];
                    const unsigned char* p01 = &smooth[(static_cast<size_t>(y1) * kSize + x0) * 4];
                    const unsigned char* p11 = &smooth[(static_cast<size_t>(y1) * kSize + x1) * 4];
                    unsigned char* o = &out[(static_cast<size_t>(y) * kOut + x) * 4];
                    for (int c = 0; c < 4; ++c) {
                        const float a0 = p00[c] + (p10[c] - p00[c]) * tx;
                        const float a1 = p01[c] + (p11[c] - p01[c]) * tx;
                        o[c] = static_cast<unsigned char>(a0 + (a1 - a0) * ty);
                    }
                    if (mask != nullptr) {
                        const float mu = u0 + u * (u1 - u0);
                        float cut = 0.0f;
                        if (mu >= 0.0f && mu <= 1.0f && mv >= 0.0f && mv <= 1.0f) {
                            const int mx = std::min(static_cast<int>(mu * mask->w), mask->w - 1);
                            const int my = std::min(static_cast<int>(mv * mask->h), mask->h - 1);
                            cut = static_cast<float>(mask->alpha[static_cast<size_t>(my) * mask->w + mx]) / 255.0f;
                        }
                        o[3] = static_cast<unsigned char>(static_cast<float>(o[3]) * cut);
                    }
                }
            }
            tex = renderer.createTextureFromRgba(out.data(), kOut, kOut);
        }
        if (px != nullptr) {
            stbi_image_free(px);
        }
        cache.emplace(cacheKey, tex);
        return tex;
    }

    // Cached textures from assets/select (phone frame, buttons, indicators).
    //
    // 语言目录优先：<select>\<lang>\<name>.png -> <select>\ja-JP\<name>.png ->
    // <select>\<name>.png。根目录只放与语言无关的图；把文字烤进图里的
    // （团体页签、楽曲Lv. 牌）放语言目录，见 selectLanguageRef() 上面的说明。
    GLuint selectTex(platform::Renderer& renderer, const char* name)
    {
        static std::map<std::string, GLuint> cache;
        const std::string key = selectLanguageRef() + '|' + name;
        auto it = cache.find(key);
        if (it == cache.end()) {
            GLuint id = 0;
            if (!gSelectAssetDir.empty()) {
                std::string err;
                const std::string stem = gSelectAssetDir + "\\select\\";
                const std::string candidates[] = {
                    stem + selectLanguageRef() + "\\" + name + ".png",
                    stem + "ja-JP\\" + name + ".png",
                    stem + name + ".png",
                };
                for (const std::string& path : candidates) {
                    id = renderer.loadUiTexture(path, err);
                    if (id != 0) {
                        break;
                    }
                }
            }
            it = cache.emplace(key, id).first;
        }
        return it->second;
    }

    // Cached textures for the final-confirmation page (assets/final).
    GLuint finalTex(platform::Renderer& renderer, const char* name)
    {
        static std::map<std::string, GLuint> cache;
        auto it = cache.find(name);
        if (it == cache.end()) {
            std::string err;
            const GLuint id = gSelectAssetDir.empty()
                ? 0
                : renderer.loadUiTexture(gSelectAssetDir + "\\final\\" + name + ".png", err);
            it = cache.emplace(name, id).first;
        }
        return it->second;
    }

    // Badge sprites for the song list rows (assets/mmw/overlay/select_badge,
    // exported from the UI atlas). Same caching scheme as selectTex but a
    // different directory.
    GLuint badgeTex(platform::Renderer& renderer, const char* name)
    {
        static std::map<std::string, GLuint> cache;
        auto it = cache.find(name);
        if (it == cache.end()) {
            std::string err;
            const GLuint id = gSelectAssetDir.empty()
                ? 0
                : renderer.loadUiTexture(gSelectAssetDir + "\\mmw\\overlay\\select_badge\\" + name + ".png", err);
            it = cache.emplace(name, id).first;
        }
        return it->second;
    }

    // 9-slice for the badge / ribbon sprites. The originals are drawn with
    // Unity's Sliced Image type - stretching the whole sprite (what AddImage
    // does) squashes the rounded corners, which is exactly the "wrong shape"
    // the badges showed. `bx`/`by` are the slice borders in source pixels,
    // `s` scales them with the row so a 0.58x row does not get chunky corners.
    void addNinePatch(ImDrawList* dl, GLuint tex, int texW, int texH, const ImVec2& p0,
        const ImVec2& p1, float bx, float by, float s, ImU32 tint)
    {
        if (tex == 0) {
            return;
        }
        float l = bx * s;
        float t = by * s;
        float r = bx * s;
        float b = by * s;
        const float w = p1.x - p0.x;
        const float h = p1.y - p0.y;
        l = std::min(l, w * 0.5f);
        r = std::min(r, w * 0.5f);
        t = std::min(t, h * 0.5f);
        b = std::min(b, h * 0.5f);
        const float u0 = 0.0f, u1 = static_cast<float>(bx) / texW, u2 = 1.0f - u1, u3 = 1.0f;
        const float v0 = 0.0f, v1 = static_cast<float>(by) / texH, v2 = 1.0f - v1, v3 = 1.0f;
        const float xs[4] = {p0.x, p0.x + l, p1.x - r, p1.x};
        const float ys[4] = {p0.y, p0.y + t, p1.y - b, p1.y};
        const float us[4] = {u0, u1, u2, u3};
        const float vs[4] = {v0, v1, v2, v3};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                    ImVec2(xs[col], ys[row]), ImVec2(xs[col + 1], ys[row + 1]),
                    ImVec2(us[col], vs[row]), ImVec2(us[col + 1], vs[row + 1]), tint);
            }
        }
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

    // 最终确认页：ScreenLayerFreeLiveFinalConfirmation（prefab 703，它继承
    // ScreenLayerDeckEditBase）+ 右侧同一台 ScreenLayerFreeLiveCellPhone（704）。
    // 左边是卡组确认（选卡组 / 综合力 / 5 个空槽），右边是选曲时那台手机
    // （曲绘 / 标题 / 难度 / 确定）。整页自绘，不创建 ImGui item。
    int drawFinalConfirm(platform::Renderer& renderer, const std::vector<ChartEntry>& entries,
        int entryIndex, int difficulty, int vocalIndex, float w, float h, float k, float timeSec,
        ImFont* title, ImFont* body, int deckPower, bool& autoLive, int& mvMode)
    {
        if (entryIndex < 0 || entryIndex >= static_cast<int>(entries.size())) {
            gFinalConfirmOpen = false;
            return SelectNone;
        }
        const ChartEntry& entry = entries[static_cast<size_t>(entryIndex)];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImGuiIO& io = ImGui::GetIO();
        // 最终确认页整页是 1920x1080 的绝对坐标。缩放调小以后窗口比画布大，
        // 把这张"画布"居中而不是贴左上角（选曲页是两栏，必须各自贴边；这一页
        // 是单栏，居中才是缩放该有的样子）。做法：内容顶点统一平移 (ox,oy)，
        // 命中测试把鼠标反向平移；背景图仍然铺满窗口（它不属于画布）。
        const float ox = (w - 1920.0f * k) * 0.5f;
        const float oy = (h - 1080.0f * k) * 0.5f;
        const ImVec2 mouse(io.MousePos.x - ox, io.MousePos.y - oy);
        const bool clicked = ui::mouseClicked(ImGuiMouseButton_Left);
        if (ui::keyPressed(ImGuiKey_Escape)) {
            ui::se(ui::SeWindowClose);
            gFinalConfirmOpen = false;
            return SelectNone;
        }
        const auto hit = [&](float x, float y, float ww, float hh) {
            return clicked && mouse.x >= x && mouse.x <= x + ww && mouse.y >= y && mouse.y <= y + hh;
        };

        // The final-confirmation prefab has no full-screen backdrop node; it
        // sits over the select background. Use the official bg_common asset
        // instead of an invented gradient.
        const GLuint bgCommon = selectBgTex(renderer, "bg_common");
        if (bgCommon != 0) {
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(bgCommon)),
                ImVec2(0.0f, 0.0f), ImVec2(w, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, 255));
        } else {
            dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(w, h), IM_COL32(35, 32, 60, 255));
        }

        // Absolute rects from prefab 703 (1920x1080 reference, computed from
        // RectTransform anchorMin/Max + anchoredPosition + pivot):
        //   BG        x=56.0  y=303.8 w=1546.2 h=555.3
        //   DeckBase  x=70.4  y=256.0 w=1080.0 h=624.0
        //   card row  x=78.4  y=346.0 w=200 h=510, gap 16（DeckCardList 的顶边）
        const float cx = 960.0f * k; // 画布中心（内容会被整体平移 (ox,oy)）
        const float bgW = 1546.1597f * k;
        const float bgH = 555.27515f * k;
        const float bgX = 56.0f * k;
        const float bgY = 303.8f * k;
        const float deckW = 1080.0f * k;
        const float deckH = 623.97f * k;
        const float deckX = 70.4f * k;
        const float deckY = 256.0f * k;

        // 画布内容从这层白底开始；上面的 bg_common 是"窗口背景"，不跟着平移。
        const int contentVtx0 = dl->VtxBuffer.Size;
        const GLuint bgTex = finalTex(renderer, "bg_base_r20_wh");
        if (bgTex != 0) {
            // 9-slice, not a stretched 50x50 sprite: stretching turns the
            // rounded corners into a giant ellipse.
            addNinePatch(dl, bgTex, 50, 50, ImVec2(bgX, bgY), ImVec2(bgX + bgW, bgY + bgH),
                24.0f, 24.0f, k, IM_COL32(255, 255, 255, 255));
        } else {
            dl->AddRectFilled(ImVec2(bgX, bgY), ImVec2(bgX + bgW, bgY + bgH),
                IM_COL32(255, 255, 255, 255), 20.0f * k);
        }
        const GLuint deckTex = finalTex(renderer, "bg_base_three_r16_wh");
        if (deckTex != 0) {
            addNinePatch(dl, deckTex, 36, 36, ImVec2(deckX, deckY),
                ImVec2(deckX + deckW, deckY + deckH), 16.0f, 16.0f, k,
                IM_COL32(223, 223, 234, 204));
        } else {
            dl->AddRectFilled(ImVec2(deckX, deckY), ImVec2(deckX + deckW, deckY + deckH),
                IM_COL32(223, 223, 234, 204), 16.0f * k);
        }

        // DeckSelector（prefab 703 顶部）：左/右箭头 + 720x56 的 #BCBCD0/90%
        // 胶囊。只有一套卡组，所以箭头画成禁用态，不假装能切。
        {
            const GLuint arrowTex = finalTex(renderer, "btn_arrow_switching_wh");
            // 左箭头 80x108 @ (0.8,104.6)，右箭头 @ (788.1,104.6)。
            for (int side = 0; side < 2; ++side) {
                const float ax = (side == 0 ? 0.8f : 788.1f) * k;
                const float ay = 104.6f * k;
                if (arrowTex != 0) {
                    dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(arrowTex)),
                        ImVec2(ax, ay), ImVec2(ax + 80.0f * k, ay + 108.0f * k),
                        ImVec2(side == 0 ? 1.0f : 0.0f, 0.0f), ImVec2(side == 0 ? 0.0f : 1.0f, 1.0f),
                        IM_COL32(255, 255, 255, 110));
                } else {
                    const ImVec2 c(ax + 40.0f * k, ay + 54.0f * k);
                    const float d = 18.0f * k;
                    if (side == 0) {
                        dl->AddTriangleFilled(ImVec2(c.x + d, c.y - d), ImVec2(c.x + d, c.y + d),
                            ImVec2(c.x - d, c.y), IM_COL32(255, 255, 255, 110));
                    } else {
                        dl->AddTriangleFilled(ImVec2(c.x - d, c.y - d), ImVec2(c.x - d, c.y + d),
                            ImVec2(c.x + d, c.y), IM_COL32(255, 255, 255, 110));
                    }
                }
            }
            const ImVec2 sel0(70.4f * k, 127.6f * k);
            const ImVec2 sel1(790.5f * k, 183.6f * k);
            const GLuint selTex = finalTex(renderer, "bg_base_round_h56_wh");
            if (selTex != 0) {
                addNinePatch(dl, selTex, 28, 28, sel0, sel1, 28.0f, 28.0f, k,
                    IM_COL32(188, 188, 208, 230));
            } else {
                dl->AddRectFilled(sel0, sel1, IM_COL32(188, 188, 208, 230), 28.0f * k);
            }
            addTextCentered(dl, body, 30.0f * k,
                ImVec2((sel0.x + sel1.x) * 0.5f, (sel0.y + sel1.y) * 0.5f),
                IM_COL32(68, 68, 102, 255), "默认队伍");
        }

        // DeckName 375.7x55.4 @ (70.4,200.7)：当前卡组的名字条。
        {
            const ImVec2 n0(70.4f * k, 200.7f * k);
            const ImVec2 n1(446.1f * k, 256.1f * k);
            const GLuint nameTex = finalTex(renderer, "bg_base_half_r16_wh");
            if (nameTex != 0) {
                addNinePatch(dl, nameTex, 28, 28, n0, n1, 16.0f, 16.0f, k,
                    IM_COL32(223, 223, 234, 204));
            } else {
                dl->AddRectFilled(n0, n1, IM_COL32(223, 223, 234, 204), 16.0f * k);
            }
            addTextLeft(dl, body, 28.0f * k, ImVec2(n0.x + 21.0f * k, (n0.y + n1.y) * 0.5f),
                IM_COL32(85, 85, 119, 255), "当前队伍");
            // 右侧的编辑铅笔（纯装饰，不假装能点）。
            const ImVec2 pc(n1.x - 24.0f * k, (n0.y + n1.y) * 0.5f);
            dl->AddLine(ImVec2(pc.x - 8.0f * k, pc.y + 8.0f * k),
                ImVec2(pc.x + 8.0f * k, pc.y - 8.0f * k), IM_COL32(148, 148, 175, 255), 3.0f * k);
        }

        // 综合力：Label 174x56 @ (51.1,272)（左对齐“综合力”）、icon 38x42、
        // 数值 280x32 右对齐到 x=430.2。之前把数值左对齐到 150，和标题叠在一起。
        {
            const GLuint icon = finalTex(renderer, "icon_deckPower_wh");
            char pwr[64];
            std::snprintf(pwr, sizeof(pwr), "%d", deckPower);
            addTextLeft(dl, body, 32.0f * k, ImVec2(73.3f * k, 300.0f * k),
                IM_COL32(68, 68, 102, 255), "综合力");
            const float iconS = 38.0f * k;
            if (icon != 0) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(icon)),
                    ImVec2(221.4f * k, 278.8f * k), ImVec2(221.4f * k + iconS, 278.8f * k + 42.0f * k),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(68, 68, 102, 255));
            }
            const ImVec2 vs = body->CalcTextSizeA(32.0f * k, FLT_MAX, 0.0f, pwr);
            addTextLeft(dl, body, 32.0f * k, ImVec2(430.2f * k - vs.x, 300.0f * k),
                IM_COL32(85, 85, 119, 255), pwr);
            // info 按钮 72x72 @ (441.8,263.5)：点一下说明综合力怎么来的。
            const ImVec2 ic(441.8f * k + 36.0f * k, 263.5f * k + 36.0f * k);
            dl->AddCircleFilled(ic, 36.0f * k, IM_COL32(255, 255, 255, 255), 40);
            addTextCentered(dl, body, 30.0f * k, ic, IM_COL32(68, 68, 102, 255), "i");
            if (hit(ic.x - 36.0f * k, ic.y - 36.0f * k, 72.0f * k, 72.0f * k)) {
                gFinalToast = "综合力 = 队伍里 5 张卡片的数值合计";
                gFinalToastUntil = static_cast<double>(timeSec) + 2.6;
                ui::se(ui::SeClick);
            }
        }

        // Five empty deck slots (DeckCard 200x510, gap 16).
        const float cardW = 200.0f * k;
        const float cardH = 510.0f * k;
        const float gap = 16.0f * k;
        const float cardsW = 5.0f * cardW + 4.0f * gap;
        const float cardsX = deckX + 8.0f * k;
        // 601 is the card centre (DeckCardList y=346 + 510/2), not the top.
        const float cardsY = 601.0f * k - cardH * 0.5f;
        (void)cardsW;
        const GLuint plusTex = finalTex(renderer, "icon_plus_h36_wh");
        const GLuint circleTex = finalTex(renderer, "bg_base_circle_h96_wh");
        for (int i = 0; i < 5; ++i) {
            const float x = cardsX + static_cast<float>(i) * (cardW + gap);
            const ImVec2 p0(x, cardsY);
            const ImVec2 p1(x + cardW, cardsY + cardH);
            dl->AddRectFilled(p0, p1, IM_COL32(176, 189, 200, 255), 16.0f * k);
            const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            const float d = 72.0f * k;
            if (circleTex != 0) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(circleTex)),
                    ImVec2(c.x - d * 0.5f, c.y - d * 0.5f), ImVec2(c.x + d * 0.5f, c.y + d * 0.5f),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(145, 159, 176, 255));
            } else {
                dl->AddCircleFilled(c, d * 0.5f, IM_COL32(145, 159, 176, 255), 40);
            }
            const float ps = 36.0f * k;
            if (plusTex != 0) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(plusTex)),
                    ImVec2(c.x - ps * 0.5f, c.y - ps * 0.5f), ImVec2(c.x + ps * 0.5f, c.y + ps * 0.5f),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(176, 189, 200, 255));
            } else {
                dl->AddLine(ImVec2(c.x - ps * 0.35f, c.y), ImVec2(c.x + ps * 0.35f, c.y),
                    IM_COL32(176, 189, 200, 255), 3.0f * k);
                dl->AddLine(ImVec2(c.x, c.y - ps * 0.35f), ImVec2(c.x, c.y + ps * 0.35f),
                    IM_COL32(176, 189, 200, 255), 3.0f * k);
            }
            if (hit(p0.x, p0.y, cardW, cardH)) {
                gFinalToast = "卡组编成功能尚未实现";
                gFinalToastUntil = static_cast<double>(timeSec) + 2.2;
                ui::se(ui::SeClick);
            }
        }

        bool phoneStartHit = false;
        // Right: ScreenLayerFreeLiveCellPhone (prefab 704) at the SAME geometry
        // as the select screen: 754.3x1416.7, right edge 1899.15, top 33.6,
        // tilted +3deg about its centre. The final page used to draw a 562-wide
        // copy with hand-tuned offsets (jacket 229px low, a green disc instead of
        // the 258x96 确定) - that is why it looked nothing like the real phone.
        {
            const float phoneW = 754.3f * k;
            const float phoneH = 1416.7f * k;
            const float phoneX = 1899.15f * k - phoneW;
            const float phoneY = 33.6f * k;
            const float ps = phoneW / 754.3f;
            const auto PX = [&](float x) { return phoneX + x * ps; };
            const auto PY = [&](float y) { return phoneY + y * ps; };
            const ImVec2 pivot(phoneX + phoneW * 0.5f, phoneY + phoneH * 0.5f);
            constexpr float kTiltDeg = 3.0f;
            const float tr = kTiltDeg * 3.14159265358979f / 180.0f;
            const float tc = std::cos(tr);
            const float ts = std::sin(tr);
            const int phoneVtx0 = dl->VtxBuffer.Size;
            const GLuint phoneFrame = selectTex(renderer, "img_smartphone");
            if (phoneFrame != 0) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(phoneFrame)),
                    ImVec2(phoneX, phoneY), ImVec2(phoneX + phoneW, phoneY + phoneH));
            } else {
                dl->AddRectFilledMultiColor(ImVec2(phoneX, phoneY),
                    ImVec2(phoneX + phoneW, phoneY + phoneH), IM_COL32(56, 48, 98, 255),
                    IM_COL32(38, 32, 72, 255), IM_COL32(30, 26, 58, 255), IM_COL32(46, 40, 84, 255));
            }
            const float jx = PX(186.5f);
            const float jy = PY(92.5f);
            const float jd = 376.0f * ps;
            addJacket(dl, thumbFor(renderer, entry.coverPath), ImVec2(jx, jy),
                ImVec2(jx + jd, jy + jd), 0.0f);
            const float contentL = PX(81.7f);
            const float textMaxW = 440.2f * ps;
            const std::string t = ellipsize(title, 36.0f * ps, entry.title, textMaxW);
            addTextLeft(dl, title, 36.0f * ps, ImVec2(contentL, PY(513.9f)),
                IM_COL32(255, 255, 255, 255), t.c_str());
            const std::string a = ellipsize(body, 28.0f * ps,
                entry.artist.empty() ? "-" : entry.artist, textMaxW);
            addTextLeft(dl, body, 28.0f * ps, ImVec2(contentL, PY(557.0f)),
                IM_COL32(215, 212, 235, 255), a.c_str());
            {
                std::string vocalLine;
                const std::vector<VocalVersion> versions = availableVocals(entry);
                if (vocalIndex >= 0 && vocalIndex < static_cast<int>(versions.size())) {
                    for (const std::string& singer : versions[static_cast<size_t>(vocalIndex)].singers) {
                        if (!vocalLine.empty()) {
                            vocalLine += "、";
                        }
                        vocalLine += singer;
                    }
                }
                if (vocalLine.empty()) {
                    vocalLine = entry.vocal;
                }
                if (!vocalLine.empty()) {
                    const std::string v = ellipsize(body, 28.0f * ps, "Vo. " + vocalLine, textMaxW);
                    addTextLeft(dl, body, 28.0f * ps, ImVec2(contentL, PY(594.9f)),
                        IM_COL32(215, 212, 235, 255), v.c_str());
                }
            }
            // Difficulty row (read-only on this page): the same five 96px pads
            // as the select phone, filled from the OTHER charts of this song in
            // `entries` (same musicId + difficulty name). Selecting a different
            // difficulty here would be a different chart, so the pads are not
            // clickable - they only show what the deck is about to play.
            {
                const ChartEntry* byDiff[kDiffCount] = {};
                for (const ChartEntry& e : entries) {
                    const bool sameSong = (entry.musicId != 0 && e.musicId == entry.musicId)
                        || (entry.musicId == 0 && e.title == entry.title);
                    if (!sameSong) {
                        continue;
                    }
                    const std::string dn = toUpper(e.difficulty);
                    for (int d = 0; d < kDiffCount; ++d) {
                        if (dn == kDiffNames[d]) {
                            byDiff[d] = &e;
                        }
                    }
                }
                static const char* kOff[kDiffCount] = {
                    "btn_difficulty_easy_off", "btn_difficulty_normal_off",
                    "btn_difficulty_hard_off", "btn_difficulty_expert_off",
                    "btn_difficulty_master_off"};
                static const char* kOn[kDiffCount] = {
                    "btn_difficulty_easy_on", "btn_difficulty_normal_on",
                    "btn_difficulty_hard_on", "btn_difficulty_expert_on",
                    "btn_difficulty_master_on"};
                const GLuint ringTex = selectTex(renderer, "eff_btn_difficulty_selected_wh");
                const GLuint glowTex = selectTex(renderer, "eff_btn_circle_h80_a70_wh");
                const float imgD = 112.0f * ps;
                const float selD = 136.0f * ps;
                float dx = PX(137.2f);
                for (int d = 0; d < kDiffCount; ++d) {
                    const bool active = (d == difficulty);
                    const ImVec2 c(dx, PY(710.2f));
                    if (active && ringTex != 0) {
                        dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(ringTex)),
                            ImVec2(c.x - selD * 0.5f, c.y - selD * 0.5f),
                            ImVec2(c.x + selD * 0.5f, c.y + selD * 0.5f));
                    }
                    if (active && glowTex != 0) {
                        const float gw = 104.0f * ps;
                        const ImU32 glowCol = (kDiffColors[d] & ~IM_COL32_A_MASK) | (150u << IM_COL32_A_SHIFT);
                        dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(glowTex)),
                            ImVec2(c.x - gw * 0.5f, c.y - gw * 0.5f),
                            ImVec2(c.x + gw * 0.5f, c.y + gw * 0.5f), ImVec2(0.0f, 0.0f),
                            ImVec2(1.0f, 1.0f), glowCol);
                    }
                    const GLuint dt = selectTex(renderer, active ? kOn[d] : kOff[d]);
                    if (dt != 0) {
                        dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(dt)),
                            ImVec2(c.x - imgD * 0.5f, c.y - imgD * 0.5f),
                            ImVec2(c.x + imgD * 0.5f, c.y + imgD * 0.5f), ImVec2(0.0f, 0.0f),
                            ImVec2(1.0f, 1.0f),
                            byDiff[d] != nullptr ? IM_COL32(255, 255, 255, 255)
                                                 : IM_COL32(120, 126, 145, 255));
                    } else {
                        dl->AddCircleFilled(c, 96.0f * ps * 0.5f,
                            byDiff[d] != nullptr ? kDiffColors[d] : IM_COL32(120, 126, 145, 255), 40);
                    }
                    char lvBuf[16];
                    const char* lv = byDiff[d] != nullptr ? levelText(*byDiff[d], lvBuf, sizeof(lvBuf)) : "-";
                    addTextCentered(dl, body, 46.0f * ps, c, IM_COL32(255, 255, 255, 255), lv);
                    addTextCentered(dl, body, 24.0f * ps,
                        ImVec2(c.x - 1.63f * ps, c.y + 66.77f * ps),
                        byDiff[d] != nullptr ? IM_COL32(255, 255, 255, 255)
                                             : IM_COL32(150, 150, 170, 255),
                        kDiffNames[d]);
                    dx += 120.0f * ps;
                }
            }

            // 确定 (PositiveButton): 258x96 @ (245.1,807.4), the official mint
            // outline + halo, same as the select screen.
            const float okW = 258.0f * ps;
            const float okH = 96.0f * ps;
            const ImVec2 okA(PX(245.1f), PY(807.4f));
            const ImVec2 okB(okA.x + okW, okA.y + okH);
            const GLuint okGlowTex = selectTex(renderer, "eff_btn_round_h80_a70_wh");
            const GLuint okLineTex = selectTex(renderer, "btn_round_line_h80_wh");
            if (okGlowTex != 0) {
                addNinePatch(dl, okGlowTex, 112, 104, ImVec2(okA.x - 5.0f * ps, okA.y - 5.0f * ps),
                    ImVec2(okB.x + 5.0f * ps, okB.y + 5.0f * ps), 56.0f, 52.0f, ps,
                    IM_COL32(255, 255, 255, 30));
            }
            if (okLineTex != 0) {
                addNinePatch(dl, okLineTex, 106, 96, okA, okB, 52.0f, 0.0f, ps,
                    IM_COL32(119, 238, 221, 255));
            } else {
                dl->AddRect(okA, okB, IM_COL32(119, 238, 221, 255), okH * 0.5f, 0, 2.0f * ps);
            }
            addTextCentered(dl, body, 32.0f * ps, ImVec2((okA.x + okB.x) * 0.5f, (okA.y + okB.y) * 0.5f),
                IM_COL32(119, 238, 221, 255), "确定");
            // Hit test: undo the tilt around the phone centre, then compare in
            // flat phone-local coordinates.
            {
                const ImVec2 lm(mouse.x - pivot.x, mouse.y - pivot.y);
                const ImVec2 lp(pivot.x + lm.x * tc + lm.y * ts, pivot.y - lm.x * ts + lm.y * tc);
                if (clicked && lp.x >= okA.x && lp.x <= okB.x && lp.y >= okA.y && lp.y <= okB.y) {
                    phoneStartHit = true;
                }
            }

            // Bottom utility row (same two icons as the select phone): gear at
            // local x 281.45, singer at 469.45, 80x80 at y 898.5. Decorative on
            // this page - the deck/confirm controls are the interactive ones.
            {
                const GLuint gearTex = finalTex(renderer, "icon_setting_wh");
                const GLuint singerTex = selectTex(renderer, "singer");
                const float iy = PY(898.5f);
                const float iw = 80.0f * ps;
                if (gearTex != 0) {
                    dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(gearTex)),
                        ImVec2(PX(281.45f) - iw * 0.5f, iy), ImVec2(PX(281.45f) + iw * 0.5f, iy + iw),
                        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(235, 235, 245, 210));
                }
                if (singerTex != 0) {
                    dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(singerTex)),
                        ImVec2(PX(469.45f) - iw * 0.5f, iy), ImVec2(PX(469.45f) + iw * 0.5f, iy + iw),
                        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(235, 235, 245, 210));
                }
            }

            // Tilt every vertex added for the phone (frame, art, text, button).
            for (int v = phoneVtx0; v < dl->VtxBuffer.Size; ++v) {
                ImVec2& p = dl->VtxBuffer[v].pos;
                const float dx = p.x - pivot.x;
                const float dy = p.y - pivot.y;
                p = ImVec2(pivot.x + dx * tc - dy * ts, pivot.y + dx * ts + dy * tc);
            }
        }

        // BottomLeft/UIPartsLiveModeButton: x=57 y=962 176x72.
        // BottomLeft/AutoLiveToggle/ToggleRoot: x=822.6 y=1014.4 152x56.
        // BottomRight/Decide, in the 2x BottomRight container:
        //   raw x=2489.4 y=1407.0 354.5x116 -> screen x=1724.7 y=973.5 177.3x58.
        const float mvX = 57.0f * k;
        const float mvY = 962.0f * k;
        const float mvW = 176.0f * k;
        const float mvH = 72.0f * k;
        const GLuint mvTex = finalTex(renderer, "btn_round_h80_wh");
        if (mvTex != 0) {
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(mvTex)),
                ImVec2(mvX, mvY), ImVec2(mvX + mvW, mvY + mvH), ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));
        } else {
            dl->AddRectFilled(ImVec2(mvX, mvY), ImVec2(mvX + mvW, mvY + mvH),
                IM_COL32(255, 255, 255, 235), 16.0f * k);
        }
        static const char* kMvNames[3] = {"无 MV", "原曲 MV", "2D MV"};
        addTextCentered(dl, body, 28.0f * k, ImVec2(mvX + mvW * 0.5f, mvY + mvH * 0.5f),
            IM_COL32(68, 68, 102, 255), kMvNames[std::clamp(mvMode, 0, 2)]);
        if (hit(mvX, mvY, mvW, mvH)) {
            mvMode = (std::clamp(mvMode, 0, 2) + 1) % 3;
            ui::se(ui::SeClick);
        }

        const float autoX = 822.6f * k;
        const float autoY = 1014.4f * k;
        const float autoW = 152.0f * k;
        const float autoH = 56.0f * k;
        const GLuint autoBg = finalTex(renderer, "bg_auto");
        if (autoBg != 0) {
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(autoBg)),
                ImVec2(autoX - 14.0f * k, autoY - 27.0f * k),
                ImVec2(autoX - 14.0f * k + 180.0f * k, autoY - 27.0f * k + 110.0f * k),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));
        }

        // UIPartsToggle: Bg On #FF77AA, Off #444466, knob toggle_btn_wh, text
        // ON/OFF 29px white.
        dl->AddRectFilled(ImVec2(autoX, autoY), ImVec2(autoX + autoW, autoY + autoH),
            autoLive ? IM_COL32(255, 119, 170, 255) : IM_COL32(68, 68, 102, 255),
            autoH * 0.5f);
        const float knob = autoH - 8.0f * k;
        const float knobX = autoX + 4.0f * k + (autoLive ? autoW - knob - 8.0f * k : 0.0f);
        dl->AddCircleFilled(ImVec2(knobX + knob * 0.5f, autoY + autoH * 0.5f), knob * 0.5f,
            IM_COL32(255, 255, 255, 255), 32);
        addTextLeft(dl, body, 22.0f * k,
            ImVec2(autoX + (autoLive ? 24.0f * k : 58.0f * k), autoY + autoH * 0.5f),
            IM_COL32(255, 255, 255, 255), autoLive ? "ON" : "OFF");
        if (hit(autoX, autoY, autoW, autoH)) {
            autoLive = !autoLive;
            ui::se(ui::SeClick);
        }

        int finalResult = SelectNone;
        if (phoneStartHit) {
            // The phone's play button is the original start control; no extra
            // "开始" capsule outside the handset.
            gFinalConfirmOpen = false;
            finalResult = entryIndex;
        }
        if (finalResult == SelectNone && timeSec < gFinalToastUntil && !gFinalToast.empty()) {
            const ImVec2 ts = body->CalcTextSizeA(22.0f * k, FLT_MAX, 0.0f, gFinalToast.c_str());
            const float tw = ts.x + 42.0f * k;
            const float th = 48.0f * k;
            const float tx = cx - tw * 0.5f;
            const float ty = 1080.0f * k - 156.0f * k;
            dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + th),
                IM_COL32(20, 19, 40, 235), 12.0f * k);
            addTextCentered(dl, body, 22.0f * k, ImVec2(cx, ty + th * 0.5f),
                IM_COL32(255, 255, 255, 255), gFinalToast.c_str());
        }
        if (ox != 0.0f || oy != 0.0f) {
            for (int v = contentVtx0; v < dl->VtxBuffer.Size; ++v) {
                dl->VtxBuffer[v].pos.x += ox;
                dl->VtxBuffer[v].pos.y += oy;
            }
        }
        return finalResult;
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
    // 0) 设置 > 演奏 > 默认歌手 里的偏好。只在这首歌真有那个版本、而且音频
    //    已经在本地时才生效（available 里只放得下"有文件"的），否则往下走。
    //
    //    这里**不能"含这个歌手就返回"**：セカイver 的 singers 里也有她
    //    （チームメイト 的 se_0374_01 是 5 个人，含花里みのり），那样永远不会
    //    选到 an_0374_01 那个独唱版。按"独唱度"打分：
    //      another_vocal +4、singers 只有一个 +2、sekai / virtual_singer -1。
    //    an_0374_01 = 6，se_0374_01 = -1 —— 独唱版胜出（2026-11-01 用户报的）。
    if (gVocalPrefMode == 2 && !gVocalPrefSinger.empty()) {
        int best = -1;
        int bestScore = -1;
        for (size_t i = 0; i < available.size(); ++i) {
            bool hasSinger = false;
            for (const std::string& singer : available[i].singers) {
                if (singer == gVocalPrefSinger) {
                    hasSinger = true;
                    break;
                }
            }
            if (!hasSinger) {
                continue;
            }
            int score = 0;
            if (available[i].type == "another_vocal") {
                score += 4;
            }
            if (available[i].singers.size() == 1) {
                score += 2;
            }
            if (available[i].type == "sekai" || available[i].type == "virtual_singer") {
                score -= 1;
            }
            if (score > bestScore) {
                bestScore = score;
                best = static_cast<int>(i);
            }
        }
        if (best >= 0) {
            return best;
        }
    }
    if (gVocalPrefMode == 1) {
        for (size_t i = 0; i < available.size(); ++i) {
            if (available[i].type == "virtual_singer") {
                return static_cast<int>(i);
            }
        }
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
    gAliasBySong.clear();
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
            gAliasBySong[musicId].push_back(alias);
        }
    }
    // Logged because a missing music-aliases.json is otherwise invisible: the
    // search just quietly loses its alias branch.
    platform::logf(platform::LogSelect, "[aliases] %zu aliases\n", gAliasIndex.size());
    std::fflush(stdout);
}

void loadMusicCollabs(const std::string& path)
{
    gCollabLabels.clear();
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
    const auto loadMap = [&doc](const char* key, std::map<int, std::string>& out) {
        const auto obj = doc.find(key);
        if (obj != doc.end() && obj->is_object()) {
            for (auto it = obj->begin(); it != obj->end(); ++it) {
                const int musicId = std::atoi(it.key().c_str());
                if (musicId > 0 && it.value().is_string() && !it.value().get<std::string>().empty()) {
                    out[musicId] = it.value().get<std::string>();
                }
            }
        }
    };
    loadMap("collab", gCollabLabels);
    loadMap("collabZh", gCollabLabelsZh);
    platform::logf(platform::LogSelect, "[collabs] %zu labels (%zu zh)\n", gCollabLabels.size(),
        gCollabLabelsZh.size());
    std::fflush(stdout);
}

const SongBadges& songBadges(int musicId)
{
    static const SongBadges none;
    if (musicId <= 0 || gChartMvDirs.empty()) {
        return none;
    }
    const auto it = gLocalMvBadges.find(musicId);
    if (it != gLocalMvBadges.end()) {
        return it->second;
    }
    SongBadges b;
    char id4[8] = {0};
    std::snprintf(id4, sizeof(id4), "%04d", musicId);
    std::error_code ec;
    for (const std::string& dir : gChartMvDirs) {
        if (!b.original) {
            b.original = fs::exists(toFsPath(dir) / (std::string(id4) + ".usm"), ec);
        }
        if (!b.mv2d) {
            b.mv2d = fs::exists(toFsPath(dir) / (std::string(id4) + "_sekai.usm"), ec);
        }
        if (b.original && b.mv2d) {
            break;
        }
    }
    const auto inserted = gLocalMvBadges.emplace(musicId, b);
    return inserted.first->second;
}

std::string musicCollab(int musicId)
{
    // Chinese label first (国服出处), falling back to the Japanese one.
    const auto zh = gCollabLabelsZh.find(musicId);
    if (zh != gCollabLabelsZh.end()) {
        return zh->second;
    }
    const auto it = gCollabLabels.find(musicId);
    return it == gCollabLabels.end() ? std::string() : it->second;
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

// One 猜歌 option: the same capsule as ui::capsuleButton (halo shadow,
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
    // Same halo as every other floating control (ui::dropShadow), not the old
    // +3px navy smear.
    ui::dropShadow(dl, lo, hi, radius, s);
    dl->AddRectFilled(lo, hi, fill, radius);
    ImFont* font = game::bodyFont();
    const float fontSize = std::min(22.0f * s, size.y * 0.42f);
    // 长曲名（"ワールド・ランプシェード [reunion]"）以前会直接冲出胶囊、横穿整张
    // 卡片：胶囊是网格里的固定格子，标签得裁到格子内（和歌曲列表同一个 helper）。
    const std::string fitted = ellipsize(font, fontSize, label, size.x - 18.0f * s);
    const ImVec2 text = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fitted.c_str());
    dl->AddText(font, fontSize,
        ImVec2((lo.x + hi.x - text.x) * 0.5f, (lo.y + hi.y - text.y) * 0.5f), ui::kBtnText, fitted.c_str());
    return clicked;
}

int drawSongSelect(platform::Renderer& renderer, const std::vector<ChartEntry>& entries, int& selected,
    int windowW, int windowH, float timeSec, int& sortMode, int& groupMode, int& unitTab,
    int& vocalIndex,
    float uiScale, ImVec2* confirmCenter, ImVec4* confirmPhoneScreen, int deckPower, bool* autoLive,
    int* mvMode, const AccountData* account, const SelectPartyInfo* party, SelectPartyResult* partyOut,
    int* favoriteToggle, bool nativeMenuBar, int* deleteRequest, const float* previewBands)
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
    // 画布基准：原作 CanvasScaler 是 **ScaleWithScreenSize / 1920x1080 /
    // matchWidthOrHeight = 0（按宽度匹配）**（SELECT-UI.md §2 的 YAML 原文）。
    // 也就是说窗口变宽变窄时，UI 的"设计单位"始终是 1920 宽，只有高度会多出来
    // 或裁掉 —— 这就是原作在 4:3 / 21:9 上都不散架的原因。我们以前用的是
    // h/1080（按高度匹配）：16:9 两者相同，但窗口一窄（1024x768）k 反而变大，
    // 右侧 1522k 的手机直接被推出屏幕，这就是"响应式糟糕"的根因。
    //
    // uiScale 是**用户缩放**，乘在 fit 之后的画布比例上。<1 时整个界面变小，
    // 左右两块各自贴边（列表贴左、手机贴右），中间留白随窗口变宽；这一点和
    // 原作的锚点规则一致（列表锚左、手机锚在右侧内容区、顶栏零件锚右）。
    //
    // >100% 不乘进去：选曲页是"列表 912 + 间隙 21 + 手机 754 + 边距 20.8 = 1920"
    // 的固定两栏，画布一旦窄于 1920 两栏必然重叠（Unity 里也是这个结果：它只是
    // 把 UI 画到屏幕外）。所以选曲/最终确认页把缩放上限锁在 100%，结算页不受
    // 影响（那是单栏版式，Result.cpp 自己算 canvas）。
    const float fit = std::clamp(w / 1920.0f, 0.4f, 4.0f);
    const float scale = std::clamp(uiScale, 0.5f, 1.0f);
    const float k = fit * scale;

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
    // The list row the room is on, if this window has it (-1 = nothing locked
    // yet, or this window has no such chart). Kept out here because the search
    // filter below must never hide it.
    int lockedGroup = -1;
    if (partyReadOnly && party->songLocked && party->lockedEntry >= 0
        && party->lockedEntry < static_cast<int>(entries.size())) {
        selected = party->lockedEntry;
        lockedGroup = groupOf(party->lockedEntry);
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

    // Debug (`CPSEKAI_FINAL_CONFIRM=1`): open the final page for screenshots.
    {
        static bool debugOpened = false;
        if (!debugOpened && !gFinalConfirmOpen && std::getenv("CPSEKAI_FINAL_CONFIRM") != nullptr
            && selected >= 0 && selected < static_cast<int>(entries.size())) {
            debugOpened = true;
            gFinalConfirmOpen = true;
            gFinalEntry = selected;
            gFinalDifficulty = diffIndex;
            gFinalVocal = vocalIndex;
        }
    }

    // Debug (`CPSEKAI_SELECT_MENU=1`): open the right-hand menu for screenshots.
    {
        static bool menuDebugOpened = false;
        if (!menuDebugOpened && std::getenv("CPSEKAI_SELECT_MENU") != nullptr) {
            menuDebugOpened = true;
            gMenuOpen = true;
        }
    }

    // 最终确认页：确定之后停在这里，Start 才把 action 交回 main。整页自绘，
    // 所以这一帧不会让底下的列表/手机拿到输入。
    if (gFinalConfirmOpen) {
        bool autoValue = autoLive != nullptr ? *autoLive : false;
        int mvValue = mvMode != nullptr ? *mvMode : 0;
        const int finalAction = drawFinalConfirm(renderer, entries, gFinalEntry, gFinalDifficulty,
            gFinalVocal, w, h, k, static_cast<float>(timeSec), title, body, deckPower, autoValue,
            mvValue);
        if (autoLive != nullptr) {
            *autoLive = autoValue;
        }
        if (mvMode != nullptr) {
            *mvMode = mvValue;
        }
        if (finalAction >= 0) {
            selected = gFinalEntry;
            vocalIndex = gFinalVocal;
            action = finalAction;
        }
        ImGui::End();
        if (gSelectFillDisabled) {
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar(2);
        return action;
    }

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
        // Official pattern sheet over the wallpaper, semi-transparent (the
        // wallpaper stays readable underneath).
        const GLuint patternTex = selectBgTex(renderer, "bg_common");
        if (patternTex != 0) {
            constexpr float kPatternAspect = 2048.0f / 1261.0f; // measured sprite size
            const float dstAspect = w / h;
            float dw = w;
            float dh = h;
            if (kPatternAspect > dstAspect) {
                dw = h * kPatternAspect;
            } else {
                dh = w / kPatternAspect;
            }
            const ImVec2 p0((w - dw) * 0.5f, (h - dh) * 0.5f);
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(patternTex)),
                p0, ImVec2(p0.x + dw, p0.y + dh), ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, 110));
        }
    } else if (drawFill) {
        // Official built-in fill (2026-11-15): the bg_common.png pattern sheet
        // (2048x1261 halftone gradient from the UI atlas), cover-fit like the
        // wallpaper branch above. Falls back to the old wash if the sprite is
        // missing.
        const GLuint patternTex = selectBgTex(renderer, "bg_common");
        if (patternTex != 0) {
            // Measured sprite size; loadUiTexture does not report dimensions.
            constexpr float kPatternAspect = 2048.0f / 1261.0f;
            const float dstAspect = w / h;
            float dw = w;
            float dh = h;
            if (kPatternAspect > dstAspect) {
                dw = h * kPatternAspect;
            } else {
                dh = w / kPatternAspect;
            }
            const ImVec2 p0((w - dw) * 0.5f, (h - dh) * 0.5f);
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(patternTex)),
                p0, ImVec2(p0.x + dw, p0.y + dh));
        } else {
            dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, h),
                IM_COL32(30, 26, 58, 255), IM_COL32(52, 40, 88, 255),
                IM_COL32(20, 18, 40, 255), IM_COL32(46, 36, 78, 255));
            const float glowX = w * (0.5f + 0.35f * std::sin(timeSec * 0.25f));
            dl->AddCircleFilled(ImVec2(glowX, h * 0.35f), h * 0.6f, IM_COL32(120, 130, 230, 30), 64);
        }
    }

    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const ImU32 grayText = IM_COL32(178, 178, 198, 255);

    // ------------------------------------------------------------------
    // 切换歌手 state, resolved before anything is drawn.
    //
    // The selected song's vocal versions, once per frame: the phone panel's
    // "Vo." line and the version panel both need them, and availableVocals()
    // stats the filesystem once per version of the song. This list *is* what the
    // panel shows - only the versions whose mp3 sits next to the chart, so a
    // song the player has one download of looks exactly like before.
    //
    // The panel's slide (eased 0..1, off-screen right at 0) is computed here too
    // rather than next to the panel's drawing code, because everything the panel
    // covers has to go inert while it is up: the top bar, the level chip and all
    // of the phone panel's controls sit *under* it, and ImGui gives the hover to
    // whichever item was submitted first - i.e. to them, not to the panel.
    // ------------------------------------------------------------------
    const ChartEntry* vocalSong = (!groups.empty() && selected >= 0
                                       && selected < static_cast<int>(entries.size()))
        ? &entries[static_cast<size_t>(selected)]
        : nullptr;
    std::vector<VocalVersion> vocalVersions;
    if (vocalSong != nullptr) {
        vocalVersions = availableVocals(*vocalSong);
    }
    if (vocalSong == nullptr) {
        gVocalPanelOpen = false;
    }
    const float vocalPanelT = ui::anim(0x4a576000u, gVocalPanelOpen, 15.0f);
    const bool vocalPanelUp = gVocalPanelOpen || vocalPanelT > 0.0005f;
    // The drawer's own geometry, hoisted for the same reason: its left edge is
    // what tells a click on the blank area (close me) from a click on the drawer
    // itself, and that test has to run before the frame's widgets are built.
    // NOTE: the panel is a plain translucent mask - whatever it covers keeps its
    // own brightness, on purpose (it shows through the fill). Only the *input*
    // is taken away from it, below.
    const auto easeOutCubic = [](float t) {
        const float inv = 1.0f - std::clamp(t, 0.0f, 1.0f);
        return 1.0f - inv * inv * inv;
    };
    const float vocalEase = easeOutCubic(vocalPanelT);
    // ChangeVocalSubWindowDialog prefab: Bg 956 wide on the 1920 canvas
    // (956/1920 = 0.4979), full height, flush with the right edge.
    const float vocalPW = std::min(956.0f * k, w * 0.4979f);
    const float vocalPX0 = w + (1.0f - vocalEase) * (vocalPW + 44.0f * k) - vocalPW;

    // 右上角菜单抽屉（原作 ScreenLayerHeader 的 menuButton -> MenuDialog，见
    // SELECT-UI.md §「右上角菜单」）。它和 切换歌手 是同一类右侧 SubWindow，
    // 两个叠在一起没有意义，所以谁开就收另一个。
    const float menuT = ui::anim(0x4a57b000u, gMenuOpen, 20.0f);
    const bool menuUp = gMenuOpen || menuT > 0.0005f;
    const float menuEase = easeOutCubic(menuT);
    // MenuDialog prefab 1775：Bg x=56..960（1920 画布）→ 904 宽、满高、贴右缘；
    // 内容区 856 宽（左右各 24）；关闭钮 44x44、右缘 18.1、顶 18.5。
    const float menuW = 904.0f * k;
    const float menuX0 = w - menuW * menuEase;
    // 抽屉/卡片任意一个立着的时候，底下那一层（列表、手机、顶栏）的点击都要
    // 关掉。抽屉是半透明的，视觉上还看得见，但输入归它。
    const bool overlayUp = vocalPanelUp || menuUp;
    if (gMenuOpen && gVocalPanelOpen) {
        gVocalPanelOpen = false;
    }
    // Esc 关抽屉。main 的 escape 链会先问 menuDrawerOpen()，所以这里拿到的是
    // 没被别人吃掉的那一次按键。
    if (gMenuOpen && ui::keyPressed(ImGuiKey_Escape, false)) {
        ui::se(ui::SeWindowClose);
        gMenuOpen = false;
    }

    // ------------------------------------------------------------------
    // Left: 团体页签 + song list
    //
    // Official geometry (prefab 702 / 1117 / 1129, see SELECT-UI.md): the unit
    // strip is MusicSelectUnitTabList at x=44, 136 wide, inset 84 top and
    // bottom; the list column starts at x=212 and is 912 wide, its rows are
    // 895x144 and grow to 895x230 when selected. Rows are 144 apart, so a grown
    // card pushes its neighbours out by (230-144)/2 instead of overlapping
    // them - see pushExtra() below.
    // ------------------------------------------------------------------
    const float unitStripX = 44.0f * k;
    const float unitStripW = 136.0f * k;
    const float listX = 212.0f * k;
    const float listW = 912.0f * k;
    const float listRowInset = (listW - 895.0f * k) * 0.5f; // rows sit centred

    // 顶栏 —— 按原作 prefab 的**绝对坐标**（1920×1080 画布，数值来自
    // ScreenLayerFreeLiveMusicSelect 的解算，见 SELECT-UI.md）：
    //   UIPartsSearchField  760.8 x 72  @ (196, 28)   圆角胶囊，放大镜在**右端**
    //   FilterButton          72 x 72   @ (1380.3, 27) 圆钮，漏斗 29x28 居中
    //   UIPartsDropdown      280 x 56   @ (1476.5, 36) 排序下拉
    // 三者的顶边不同（28 / 27 / 36）、高度也不同（72 / 72 / 56）：原作就是让它们
    // 垂直居中对齐在 y=64 这条线上，不是拉成一样高。
    // 2026-11-22（SELECT-HEADER.md）：原作**有**菜单钮，它在全局顶栏 prefab
    // `ScreenLayerHeader` 里，96x96、右缘留 16（x 1808..1904）、图标是
    // `icon_menu_wh` 40x34 染 #444466。我们以前把它塞在筛选左边（1296.3）并缩到
    // 72，那是自己发明的；现在按原作摆回最右。
    // 同一份数据里顶栏还有「返回钮 96x96 @(16,16)」和「SOLO LIVE + ▶楽曲選択」标题块，
    // **这两个我们故意不做**（用户 2026-11-22 明确要求）：左边那一格空着，标题块也
    // 不画 —— 搜索框保持原作的 x=196，中间这一条就不会被顶栏挤掉。
    static char searchBuf[64] = "";
    const float searchX = 196.0f * k;
    const float searchW = 760.8f * k;
    const float searchH = 72.0f * k;
    const float headerRowY = 28.0f * k;
    const float filterBtnD = 72.0f * k;
    // 筛选 / 排序贴右缘，不是写死 x：按宽度匹配之后（k 可能小于 1）绝对坐标
    // 会让它们离开菜单钮几百像素。原作的这三个零件也是同一个
    // `TopRIght` 容器里的（MusicSelectListContent 右端），这里保持"离右缘
    // 467.7 / 163.5"这两个间距，菜单钮再往右 51.5。
    //   筛选右缘 = w - (1920 - 1452.3)k = w - 467.7k
    //   排序左缘 = w - (1920 - 1476.5)k = w - 443.5k
    const float filterBtnX = w - 539.7f * k; // 右缘 w-467.7k（宽 72k）
    const float filterBtnY = 27.0f * k;
    // 菜单钮：96x96，右缘 16（= 1920-96-16 = 1808），顶边 16（比筛选/排序高，原作如此）。
    // 用 `w -` 而不是写死 1808，和等级牌（`w - 26k`）同一套算法，窗口不是 16:9 时也贴右。
    const float menuBtnD = 96.0f * k;
    const float menuBtnX = w - 16.0f * k - menuBtnD;
    const float menuBtnY = 16.0f * k;
    const float comboW = 280.0f * k;
    const float comboX0 = w - 443.5f * k;
    const float comboY = 36.0f * k;
    const float sortH = 56.0f * k;
    // 多人游玩 member: the header row stays fully usable. Only the *list* is
    // the host's (see `listHovered` below and the veil at the end of the list);
    // the tools above it are per-window and were never the room's business -
    // searching, sorting and 刷新 a mirror list does not change what is played.
    ImGui::SetCursorScreenPos(ImVec2(searchX, headerRowY));
    // Same translucent-white pill as ui::combo's box, so the search field and
    // the two selectors beside it read as one row (see ui::combo for the look).
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 244));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 235));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(70, 70, 90, 255));
    // Radius comes from the official sprite: `btn_round_r16_wh` is 56x56 with
    // m_Border 28 on all four sides, so a 72-tall search field keeps **28** -r
    // corners (a 16px flat run in the middle), it is NOT a full 36r capsule.
    // The old `searchH * 0.5f` made it rounder than the reference.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 28.0f * k);
    // Text Area in the prefab starts 10px inside the pill and the magnifier
    // sits at its RIGHT end (IconRoot centre x=914.8, 34x34) - the opposite of
    // the usual convention, and the one thing that makes the field read as the
    // official one. The rounded cap eats more than 10px visually, so the text
    // gets 28 and the icon is drawn separately at its prefab x.
    const float searchTextPadX = 28.0f * k;
    const float searchFontSize = 30.0f * k;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        ImVec2(searchTextPadX, (searchH - searchFontSize) * 0.5f));
    ImGui::PushFont(body, searchFontSize);
    // Full width: the icon lives inside the field, so the field has to be the
    // whole pill. (It used to be shortened by searchH and the magnifier drawn
    // past its right edge, which left it hanging outside the box.)
    // Same faint shadow the combo boxes get, so the whole header row reads as
    // one lifted row instead of "one field with a shadow, two without".
    // 2026-11-22: rounding follows the shape (28, not 36) and the shadow is
    // softened - the official pill (`btn_round_r16_wh`) has no baked shadow at
    // all, so a full-strength one read as a grey smear under a flatter pill.
    ui::dropShadow(dl, ImVec2(searchX, headerRowY), ImVec2(searchX + searchW, headerRowY + searchH),
        28.0f * k, k, 0.55f);
    // 搜索框的右键菜单（复制 / 剪切 / 粘贴 / 清空）。ImGui 不公开 InputTextState，
    // 所以用一个 CallbackAlways 把选区两端抄出来 —— 剪切和复制要知道选了哪一段。
    struct SearchEditSnapshot
    {
        int selStart = 0;
        int selEnd = 0;
        bool valid = false;
    };
    static SearchEditSnapshot searchSel;
    const auto searchEditCallback = [](ImGuiInputTextCallbackData* data) -> int {
        SearchEditSnapshot* snap = static_cast<SearchEditSnapshot*>(data->UserData);
        snap->selStart = data->SelectionStart;
        snap->selEnd = data->SelectionEnd;
        snap->valid = true;
        return 0;
    };
    ImGui::SetNextItemWidth(searchW);
    // 抽屉/卡片立着的时候搜索框只负责显示：用 BeginDisabled 拦住输入，但把
    // DisabledAlpha 顶到 1，所以外观不变（原作那层后面照样是亮的）。
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
    ImGui::BeginDisabled(overlayUp);
    ImGui::InputTextWithHint("##search", "搜索", searchBuf, sizeof(searchBuf),
        ImGuiInputTextFlags_CallbackAlways, searchEditCallback, &searchSel);
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    const bool searchRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    if (searchRightClicked) {
        ImGui::OpenPopup("##searchedit");
    }
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(250, 250, 253, 252));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(70, 70, 90, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(226, 236, 246, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(210, 224, 240, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * k);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * k, 8.0f * k));
        ImGui::PushFont(body, 17.0f * k);
        if (ImGui::BeginPopup("##searchedit")) {
            const int selA = std::min(searchSel.selStart, searchSel.selEnd);
            const int selB = std::max(searchSel.selStart, searchSel.selEnd);
            const size_t bufLen = std::strlen(searchBuf);
            const bool hasSel = searchSel.valid && selB > selA
                && selB <= static_cast<int>(bufLen);
            char picked[128] = {};
            if (hasSel) {
                const size_t count = std::min(static_cast<size_t>(selB - selA), sizeof(picked) - 1);
                std::memcpy(picked, searchBuf + selA, count);
                picked[count] = '\0';
            }
            if (ImGui::MenuItem("剪切", nullptr, false, hasSel)) {
                ImGui::SetClipboardText(picked);
                // 连同结尾的 '\0' 一起往前挪。
                std::memmove(searchBuf + selA, searchBuf + selB, bufLen - static_cast<size_t>(selB) + 1);
            }
            if (ImGui::MenuItem("复制", nullptr, false, hasSel)) {
                ImGui::SetClipboardText(picked);
            }
            if (ImGui::MenuItem("粘贴")) {
                const char* clip = ImGui::GetClipboardText();
                if (clip != nullptr && clip[0] != '\0') {
                    const size_t clipLen = std::strlen(clip);
                    const size_t head = hasSel ? static_cast<size_t>(selA) : bufLen;
                    const size_t tail = hasSel ? static_cast<size_t>(selB) : bufLen;
                    // 留一个字节给结尾的 '\0'；放不下就整个不贴（比截一半好）。
                    if (bufLen - (tail - head) + clipLen < sizeof(searchBuf)) {
                        std::memmove(searchBuf + head + clipLen, searchBuf + tail, bufLen - tail + 1);
                        std::memcpy(searchBuf + head, clip, clipLen);
                    }
                }
            }
            if (ImGui::MenuItem("清空", nullptr, false, searchBuf[0] != '\0')) {
                searchBuf[0] = '\0';
            }
            ImGui::EndPopup();
        }
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
    }
    {
        const GLuint searchIcon = selectTex(renderer, "search");
        if (searchIcon != 0) {
            // IconRoot: 34x34, centre 718.8 from the field's left edge, i.e.
            // 42 from its right edge - the magnifier sits at the far right.
            const float iconSize = 34.0f * k;
            const ImVec2 c(searchX + 718.8f * k, headerRowY + searchH * 0.5f);
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(searchIcon)),
                ImVec2(c.x - iconSize * 0.5f, c.y - iconSize * 0.5f),
                ImVec2(c.x + iconSize * 0.5f, c.y + iconSize * 0.5f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, searchBuf[0] == '\0' ? 150 : 210));
        }
    }

    // 排序下拉 / 筛选 / 菜单 这三样**要盖在手机上面**（原作也是浮在机身上沿的），
    // 而手机是在后面才画的，所以它们统一挪到倾斜处理之后，见本函数末尾。

    // ------------------------------------------------------------------
    // 团体页签: MusicSelectUnitTabList (prefab 1117) is 136 wide at x=44 with an
    // 84px inset top and bottom, filled with one MusicSelectUnitTab (prefab 1118:
    // cell 112x100, logo 112x112 centred) per CategoryTabType. The plate behind
    // them is bg_tab_playlist (136x1024), drawn as a vertical 3-slice so the
    // rounded caps keep their radius whatever the window height is.
    // ------------------------------------------------------------------
    if (unitTab < 0 || unitTab >= kSelectUnitTabCount) {
        unitTab = 0;
    }
    const float stripTop = 84.0f * k;
    const float stripBot = h - 84.0f * k;
    {
        const GLuint plate = selectTex(renderer, "bg_tab_playlist");
        const ImVec2 p0(unitStripX, stripTop);
        const ImVec2 p1(unitStripX + unitStripW, stripBot);
        if (plate != 0) {
            const ImTextureID tid = reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(plate));
            const float cap = 68.0f * k;      // source cap radius (68 of 1024 rows)
            const float vcap = 68.0f / 1024.0f;
            dl->AddImage(tid, p0, ImVec2(p1.x, p0.y + cap), ImVec2(0.0f, 0.0f), ImVec2(1.0f, vcap));
            dl->AddImage(tid, ImVec2(p0.x, p0.y + cap), ImVec2(p1.x, p1.y - cap),
                ImVec2(0.0f, vcap), ImVec2(1.0f, 1.0f - vcap));
            dl->AddImage(tid, ImVec2(p0.x, p1.y - cap), p1, ImVec2(0.0f, 1.0f - vcap), ImVec2(1.0f, 1.0f));
        } else {
            dl->AddRectFilled(p0, p1, IM_COL32(168, 176, 200, 235), unitStripW * 0.5f);
        }
    }
    {
        // Native sizes of the shipped logos: the ALL / その他 wordmarks are short
        // and wide, the six unit logos are square. `_on` is the coloured, glowing
        // version and is also the bigger one for the two wordmarks - that size
        // jump is the official "this tab is selected" cue, so the numbers are
        // taken as-is instead of being normalised.
        struct UnitTabIcon
        {
            const char* off;
            const char* on;
            float w;
            float h;
            float wOn;
            float hOn;
        };
        // 中文版的两个文字页签是 112x112 / 128x112 的方画布（字在中间，透明
        // 留白让它们和六个团体 logo 一样居中）；日文原版是 77x29 / 128x85 的
        // 长条。尺寸跟着语言走，不能统一 —— 见 assets/select/{ja-JP,zh-CN}/。
        static const UnitTabIcon kIconsJa[kSelectUnitTabCount] = {
            {"icon_tab_all_off", "icon_tab_all_on", 77.0f, 29.0f, 128.0f, 85.0f},
            {"icon_tab_unit_vs_off", "icon_tab_unit_vs_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_ln_off", "icon_tab_unit_ln_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_mj_off", "icon_tab_unit_mj_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_vb_off", "icon_tab_unit_vb_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_ws_off", "icon_tab_unit_ws_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_25_off", "icon_tab_unit_25_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_other_off", "icon_tab_other_on", 82.0f, 30.0f, 128.0f, 86.0f},
        };
        static const UnitTabIcon kIconsZh[kSelectUnitTabCount] = {
            {"icon_tab_all_off", "icon_tab_all_on", 112.0f, 112.0f, 128.0f, 112.0f},
            {"icon_tab_unit_vs_off", "icon_tab_unit_vs_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_ln_off", "icon_tab_unit_ln_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_mj_off", "icon_tab_unit_mj_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_vb_off", "icon_tab_unit_vb_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_ws_off", "icon_tab_unit_ws_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_unit_25_off", "icon_tab_unit_25_on", 112.0f, 112.0f, 112.0f, 112.0f},
            {"icon_tab_other_off", "icon_tab_other_on", 112.0f, 112.0f, 128.0f, 112.0f},
        };
        const UnitTabIcon* kIcons = selectLangIsJa() ? kIconsJa : kIconsZh;
        // 16:9 时每个格子 100k、整条 800k，居中后上下各余 56k（和 prefab 一致）。
        // 更扁的窗口（21:9 / 20:9）画布高度只有 800 多，8x100 会顶出底板之外
        // —— 原作那条 strip 也会挤，但 Tab 会跑到圆角外面。这里把格子按
        // "底板高度 / 8" 收一下，图标跟着缩，底板永远是完整的。
        const float stripH = stripBot - stripTop;
        const float cellH = std::min(100.0f * k, stripH / static_cast<float>(kSelectUnitTabCount));
        const float iconScale = cellH / (100.0f * k);
        const float stripCx = unitStripX + unitStripW * 0.5f;
        const float cellsTop = (stripTop + stripBot) * 0.5f
            - cellH * static_cast<float>(kSelectUnitTabCount) * 0.5f;
        for (int i = 0; i < kSelectUnitTabCount; ++i) {
            const float cellY = cellsTop + cellH * static_cast<float>(i);
            const bool on = (i == unitTab);
            // Hitbox first so the click is consumed before any row behind it.
            ImGui::SetCursorScreenPos(ImVec2(unitStripX, cellY));
            const bool clicked = ImGui::InvisibleButton(
                (std::string("##unittab") + std::to_string(i)).c_str(), ImVec2(unitStripW, cellH));
            const bool hovered = !overlayUp && ImGui::IsItemHovered();
            const UnitTabIcon& ic = kIcons[i];
            const GLuint tex = selectTex(renderer, on ? ic.on : ic.off);
            const float iw = (on ? ic.wOn : ic.w) * k * iconScale;
            const float ih = (on ? ic.hOn : ic.h) * k * iconScale;
            if (tex != 0) {
                const ImVec2 c(stripCx, cellY + cellH * 0.5f);
                // Off tabs are plain white; the hover lifts them a little so the
                // strip still reads as clickable.
                const int alpha = on ? 255 : (hovered ? 235 : 190);
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                    ImVec2(c.x - iw * 0.5f, c.y - ih * 0.5f), ImVec2(c.x + iw * 0.5f, c.y + ih * 0.5f),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, alpha));
            } else {
                addTextCentered(dl, body, 18.0f * k, ImVec2(stripCx, cellY + cellH * 0.5f),
                    on ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 190),
                    i == 0 ? "ALL" : "UNIT");
            }
            if (clicked && !overlayUp && unitTab != i) {
                ui::se(ui::SeClick);
                unitTab = i;
                platform::logf(platform::LogSelect, "[select] 团体页签 -> %d\n", unitTab);
                std::fflush(stdout);
            }
        }
    }

    // Groups matching the search filter. The box is usable in 多人游玩 too (it is
    // a per-window tool, like 排序 / 分组), so the filter applies there as well -
    // but it must never hide the row the room is on: that row is what the phone
    // panel is showing, and a filter typed for some other song would leave the
    // list and the panel telling different stories. So the locked group is
    // unioned back in below.
    // Two filters now: the 团体页签 (unitTab, 0 = 全部) and the search box. Both
    // are per-window tools - including in 多人游玩, where they must never hide
    // the row the room is parked on (that row is what the phone panel shows), so
    // the locked group is unioned back in below either way.
    const bool searchOn = searchBuf[0] != 0;
    const bool unitOn = unitTab > 0;
    std::vector<int> visible;
    if (!searchOn) {
        for (int gi = 0; gi < static_cast<int>(groups.size()); ++gi) {
            if (unitOn && groups[static_cast<size_t>(gi)].unit != unitTab) {
                continue;
            }
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
            if (unitOn && g.unit != unitTab) {
                continue;
            }
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
            if (aliasMatches(g.musicId, needle) || aliasContains(g.musicId, needle)) {
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
    }
    // The room's own row always survives the filters (see the note above).
    if (partyReadOnly && lockedGroup >= 0
        && std::find(visible.begin(), visible.end(), lockedGroup) == visible.end()) {
        visible.push_back(lockedGroup);
    }
    if (std::find(visible.begin(), visible.end(), groupIndex) == visible.end() && !visible.empty()) {
        groupIndex = visible.front();
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
    // MusicSelectListCell (prefab 1129): 895x144, growing to 895x230 when
    // selected, with the cells stacked 144 apart. The two numbers are the
    // prefab's own sizeDelta / highlightSize, not a ratio of some older row.
    const float compactH = 144.0f * k;
    const float cardH = 230.0f * k;
    const float cardRadius = 20.0f * k; // bg_base_r20_wh, radius 20
    const float pitch = compactH;       // cell pitch == cell height
    // MusicSelectListCell prefab: neutral row 895x144, selected 895x230
    // (highlightSizeX/Y), highlight sprite bg_base_r20_wh (50x50, border 24,
    // radius 20) at white 60% with NO border. Scale the two numbers by our
    // compact row height so the selected/compact ratio stays 230/144.
    // The field's bottom edge is at 100; the official list column runs the full
    // height behind it, ours is clipped, so it starts just under the header.
    const float listY = 114.0f * k;
    const float listH = h - listY - 24.0f * k;
    ui::setAnchor("songlist", ImVec2(listX, listY), ImVec2(listX + listW, listY + listH));

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
    // Animated height per **visual slot**: the selected row grows into a card
    // and the one it replaced shrinks back, instead of snapping.
    //
    // Keyed by the raw slot, not by `wrapSlot(slot)`. The list is cyclic, so a
    // short library draws the same song at several slots at once; those copies
    // are separate visuals and must not share one height. They used to, and the
    // copies then pulled the value in opposite directions every frame - the
    // selected card never reached `cardH` (it sat at ~98 px of 128) and its
    // position kept flipping by a fraction of a pixel, which is what the
    // "selected row trembles" report was. Entries are pruned to the slots that
    // are on screen, so the map stays tiny.
    static std::unordered_map<int, float> slotHeights;

    // The grown card pushes the rows above / below it outwards instead of
    // overlapping them: the official list re-flows (a cell going 144 -> 230
    // moves each neighbour by 43). Reading the *animated* height out of
    // slotHeights means the push rides the same 0.1s linear tween as the card,
    // and it decays to zero by itself when the card shrinks back - no extra
    // state, and nothing to desync when the list starts moving.
    const auto pushRef = [&]() {
        int slot = 1000000;
        float extra = 0.0f;
        for (const auto& kv : slotHeights) {
            const float e = (kv.second - compactH) * 0.5f;
            if (e > extra + 0.01f) {
                extra = e;
                slot = kv.first;
            }
        }
        return std::pair<int, float>(slot, extra);
    };
    const auto pushFor = [&](int slot) {
        const std::pair<int, float> ref = pushRef();
        if (ref.second <= 0.01f || slot == ref.first) {
            return 0.0f;
        }
        return slot > ref.first ? ref.second : -ref.second;
    };

    // Sorting / grouping decides the row layout. Grouping wins over the sort
    // combo because a section's songs have to stay together.
    const int order = groupMode == kGroupDifficulty ? kOrderByDifficulty
        : (groupMode == kGroupReading || groupMode == kGroupInitial)
            ? kOrderByName
            : sortMode;
    const std::string listSignature = std::string(searchBuf) + "|" + std::to_string(order) + "|"
        + std::to_string(groupMode) + "|" + std::to_string(diffIndex) + "|" + std::to_string(entries.size())
        + "|ro" + std::to_string(partyReadOnly ? 1 : 0)
        // The room's row is unioned into the filtered list (see above), so which
        // row that is has to be part of the cache key - otherwise a host moving
        // to a song the filter excludes would leave the old rows on screen.
        + "|lg" + std::to_string(lockedGroup);
    if (listSignature != lastSignature) {
        lastSignature = listSignature;
        platform::logf(platform::LogSelect, "[select] list rebuilt: %d visible of %d (unit %d)\n",
            static_cast<int>(visible.size()), static_cast<int>(groups.size()), unitTab);
        std::fflush(stdout);
        cachedRows = buildRows(groups, visible, entries, diffIndex, order, groupMode);
        listInit = false; // the list changed shape: recentre without gliding
        slotHeights.clear();
        listIn = 0.0f; // and let the new rows rise in (see the vertex pass below)
    }
    // Grouping off means no section headers exist, so the panel has nothing to
    // show any more.
    if (groupMode == kGroupOff) {
        indexOpen = false;
    }

    // Floating shape field: drawn on the parent draw list here, i.e. above the
    // backdrop but below the list child window.
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
        // Rows around the grown card sit 43px further out than their slot index
        // says, so the naive round() would land one row off near it. The extra
        // is symmetric about the centre - subtract it above, add it below.
        const float extra = pushRef().second;
        const float dy = y - viewCenterY;
        const float eff = dy >= 0.0f ? dy - extra : dy + extra;
        return static_cast<int>(std::lround((scroll + eff) / pitch));
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
    const bool listHovered = !partyReadOnly && !indexOpen && !overlayUp
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
    // (The `easeOutCubic` helper lives up with the 切换歌手 drawer, because the
    // drawer's geometry has to be known before the header row is drawn.)
    // Whether the section index was already open when the current mouse gesture
    // started. This is the safe gate for the index panel: a press and its
    // release can land in the SAME frame, and then the list's release-commit
    // and the panel's press handling would both act on one click - a header tap
    // would open the panel AND pick a letter in the same gesture.
    static bool indexOpenAtPress = false;
    if (ui::mouseClicked(ImGuiMouseButton_Left)) {
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
            && ui::mouseDoubleClicked(ImGuiMouseButton_Left)) {
            const SongGroup& g = groups[static_cast<size_t>(rows[static_cast<size_t>(hoverRow)].group)];
            if (g.idx[diffIndex] >= 0) {
                ui::se(ui::SeClick);
                action = g.idx[diffIndex];
            }
        }
        // 右键一行 → 收藏夹菜单（触摸屏上是长按，main.cpp 会把长按合成成这个右键）。
        // 做成菜单而不是直接切换：长按的误触概率比鼠标高，多一步确认更稳。
        static int menuGroup = -1;
        if (listHovered && hoverRow >= 0 && !rows[static_cast<size_t>(hoverRow)].header
            && ui::mouseClicked(ImGuiMouseButton_Right)) {
            menuGroup = rows[static_cast<size_t>(hoverRow)].group;
            ImGui::OpenPopup("##songmenu");
        }
        if (menuGroup >= 0 && menuGroup < static_cast<int>(groups.size())) {
            // 与 ui::combo 一样的白底弹层（这版 ImGui 的默认弹层是深色的）。
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(250, 250, 253, 252));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(70, 70, 90, 255));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(226, 236, 246, 255));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(210, 224, 240, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * k);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * k, 8.0f * k));
            ImGui::PushFont(body, 17.0f * k);
            if (ImGui::BeginPopup("##songmenu")) {
                const SongGroup& g = groups[static_cast<size_t>(menuGroup)];
                if (ImGui::MenuItem(groupFavorite(g, entries) ? "取消收藏" : "加入收藏夹")) {
                    if (favoriteToggle != nullptr) {
                        for (int d = 0; d < kDiffCount; ++d) {
                            if (g.idx[d] >= 0) {
                                *favoriteToggle = g.idx[d];
                                break;
                            }
                        }
                    }
                }
                ImGui::Separator();
                // 删除谱面文件：**只把请求交出去**，真正删（以及先问一句）在 main.cpp。
                // 这里只认得"这一组是哪首"，删除要动磁盘、还要弹确认卡，都不是选曲界面
                // 该干的事。菜单栏 编辑 > 删除谱面文件 走的是同一个出口。
                if (ImGui::MenuItem("删除谱面文件…")) {
                    if (deleteRequest != nullptr) {
                        for (int d = 0; d < kDiffCount; ++d) {
                            if (g.idx[d] >= 0) {
                                *deleteRequest = g.idx[d];
                                break;
                            }
                        }
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopFont();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }
        // Press starts a drag. The row is only committed on release, and only
        // when the gesture did not travel - dragging never selects.
        if (listHovered && ui::mouseClicked(ImGuiMouseButton_Left)) {
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
                        platform::logf(platform::LogSelect, "[select] section index %s\n", indexOpen ? "opened" : "closed");
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
        // A filter that hides everything is not an empty library: saying "put a
        // .sus in charts/" there would be a lie, and offering the downloader
        // would send the player off to fetch a song they already have. So the
        // two cases say different things and only the real one offers 下载谱面.
        const bool filteredOut = !groups.empty();
        ImGui::PushFont(body, 19.0f * k);
        emptyText(rowX, rowY + 20.0f * k, ImVec4(0.75f, 0.75f, 0.82f, 1.0f),
            filteredOut ? "这个筛选条件下没有谱面。换个团体，或者点「筛选」把条件放开"
                        : "没有找到谱面。把 .sus 放到 charts/ 目录下，再按 F5 重新扫描");
        ImGui::PopFont();
        if (filteredOut) {
            ImGui::SetCursorScreenPos(ImVec2(rowX, rowY + 78.0f * k));
            ImGui::PushFont(body, 19.0f * k);
            if (ui::capsuleButton("全部团体", ImVec2(268.0f * k, 46.0f * k), true)) {
                ui::se(ui::SeClick);
                unitTab = 0;
                searchBuf[0] = '\0';
            }
            ImGui::PopFont();
            ImGui::PushFont(body, 17.0f * k);
            emptyText(rowX, rowY + 138.0f * k, ImVec4(0.6f, 0.6f, 0.68f, 1.0f),
                "这会清掉团体页签和搜索框。");
            ImGui::PopFont();
        }
        // A dead end otherwise: the player just installed the game and has no
        // charts at all, so offer the bundled downloader right here instead of
        // making them find chartdl.exe in the folder by hand. The host launches
        // it (main.cpp polls the process and re-scans when it exits).
        if (!filteredOut) {
            ImGui::SetCursorScreenPos(ImVec2(rowX, rowY + 78.0f * k));
            ImGui::PushFont(body, 19.0f * k);
            if (ui::capsuleButton("下载谱面", ImVec2(268.0f * k, 46.0f * k), true)) {
                emptyAction = SelectDownload;
                platform::logf(platform::LogSelect, "[select] 下载谱面 pressed\n");
                std::fflush(stdout);
            }
            ImGui::PopFont();
            ImGui::PushFont(body, 17.0f * k);
            emptyText(rowX, rowY + 138.0f * k, ImVec4(0.6f, 0.6f, 0.68f, 1.0f),
                "这会跳转到音乐商店。");
            ImGui::PopFont();
        }
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
        const float centerY = viewCenterY + (static_cast<float>(slot) * pitch - scroll) + pushFor(slot);
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
        // one the selection left shrinks back. Per slot - see the declaration.
        float& rowHAnim = slotHeights.try_emplace(slot, compactH).first->second;
        // Original MusicSelectListView tween: DOTween.To(sizeDelta, target,
        // HIGHLIGHT_TWEEN_SEC = 0.1), SetDelay(0), SetEase(Ease.Linear).
        // So the height moves at a constant rate, not exponentially. Both
        // directions take the same 0.1s for a full compact<->card transition.
        const float rowSpeed = (cardH - compactH) / 0.1f;
        const float rowTarget = isSel ? cardH : compactH;
        if (rowHAnim < rowTarget) {
            rowHAnim = std::min(rowTarget, rowHAnim + rowSpeed * frameDt);
        } else {
            rowHAnim = std::max(rowTarget, rowHAnim - rowSpeed * frameDt);
        }
        const float rowH = rowHAnim;
        const ImVec2 p0(rowX + listRowInset, centerY - rowH * 0.5f);
        const ImVec2 p1(rowX + listRowInset + 895.0f * k, centerY + rowH * 0.5f);
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

        // Level of the *selected* difficulty (official table when this song
        // has no chart for it), so switching difficulty renumbers the list.
        char levelBuf[16];
        const char* rowLevel = levelLabel(levelForDifficulty(g, entries, diffIndex), levelBuf, sizeof(levelBuf));

        // MV badges, right-aligned. Layout numbers from the MusicSelectListCell
        // prefab (895x144, RE'd 2026-11-15): MVMode is a HorizontalLayoutGroup,
        // ChildAlignment=MiddleRight, Spacing=8, ReverseArrangement=1 (fills
        // right->left), children 78.98x48. The prefab has TWO variants of the
        // MVMode node: Neutral (apos -248.4,66.4 -> centre y 77.6/144 = 53.9%,
        // right edge inset 28.7px) for compact rows, Highlight (apos
        // -237.2,39.4 -> centre y 104.6/144 = 72.6%, clearly LOWER, inset
        // 17.5px) for the selected card. Scaled by row height (48/144).
        // WHICH badges show is decided by the mv/ folder, not the master
        // categories: original <id4>.usm -> blue 原曲, <id4>_sekai.usm ->
        // green 2D MV. No 3D badge - we do not render 3D MVs.
        // Label style per the sprites: text filled with the badge border
        // colour (bl 102,204,255 / gn 119,238,221), sized ~0.56 of the badge.
        // MV badges, right-aligned. Numbers from prefab 1129: MVMode is a
        // 439.3x100 HorizontalLayoutGroup (ChildAlignment = 5 MiddleRight,
        // Spacing 8, ReverseArrangement 1) whose children are 79x48.
        // Neutral MVMode is at x 426.9..866.3 y_cell 51.6..151.6 -> centre
        // y_cell 101.6, right edge 28.7 inside the cell and 42.4 above its
        // bottom; Highlight MVMode is x 438.1..877.5 y_cell 97.6..197.6 ->
        // centre y_cell 147.6, right edge 17.5 inside and 39.4 above the
        // 230-tall card's bottom. Label TMP is 28.
        // WHICH badges show is decided by the mv/ folder, not the master
        // categories: original <id4>.usm -> blue 原曲, <id4>_sekai.usm ->
        // green 2D MV. No 3D badge - we do not render 3D MVs.
        const auto drawMvBadges = [&](const ImVec2& r0, const ImVec2& r1, bool highlight) {
            const SongBadges& badges = songBadges(g.musicId);
            if (!badges.original && !badges.mv2d) {
                return;
            }
            const float bs = compactH / 144.0f;
            const float bhh = 48.0f * bs;
            const float bww = 79.0f * bs;
            const float gap = 8.0f * bs;
            // Absolute offsets from the row's bottom / right edge, exactly as
            // the prefab has them (a *fraction* of the row height would drift,
            // since the card is 230 tall and the compact row 144).
            const float bcy = r1.y - (highlight ? 39.4f : 42.4f) * bs;
            const float bx0 = r1.x - (highlight ? 17.5f : 28.7f) * bs;
            struct BadgeDef
            {
                bool present;
                const char* tex;
                const char* label;
                ImU32 labelCol;
            };
            const BadgeDef defs[] = {
                {badges.original, "label_line_m_dbl_bl", "原曲", IM_COL32(102, 204, 255, 255)},
                {badges.mv2d, "label_line_m_dbl_gn", "2D", IM_COL32(119, 238, 221, 255)},
            };
            float bx = bx0;
            for (const BadgeDef& def : defs) {
                if (!def.present) {
                    continue;
                }
                bx -= bww;
                const float by = bcy - bhh * 0.5f;
                // The sprites are 32x32 sliced images (4px coloured frame,
                // #444466 fill); nine-patching keeps the corner radii. The
                // slice border is 12 source px so a bit more of the rounded
                // corner survives scaling.
                addNinePatch(listDl, badgeTex(renderer, def.tex), 32, 32, ImVec2(bx, by),
                    ImVec2(bx + bww, by + bhh), 12.0f, 12.0f, bs, IM_COL32(255, 255, 255, 255));
                addTextCentered(listDl, body, 28.0f * bs,
                    ImVec2(bx + bww * 0.5f, bcy - 1.0f * bs), def.labelCol, def.label);
                bx -= gap;
            }
        };

        // ------------------------------------------------------------------
        // Row geometry, every number from prefab 1129 (MusicSelectListCell).
        //
        // Which prefab: prefab 702's MusicSelectListView.listViewItemPrefab is
        // {m_FileID 21, m_PathID 506}. File 21 of that serialized file is the
        // bundle db02094472f894a81a8d59032b4fe140, and 506 there is the
        // MusicSelectListCell MonoBehaviour - the field's type is ListViewItem,
        // i.e. a component reference, not a GameObject. So the cells are
        // 895x144 (230 when selected), centred in the 912-wide list column.
        // MusicSelectSingleListCell (912 wide) is NOT this list.
        //
        // Both contents are CENTRE-anchored and larger than the 144 cell:
        // Highlight 895x230 (y_cell -43..187) and Neutral 895x192 (-24..168).
        // So every y below is quoted as an offset from the cell centre
        // (centre = y_cell 72):  offset = y_cell - 72.
        // ------------------------------------------------------------------
        if (isSel) {
            // Highlight: bg_base_r20_wh, sliced, white 60%, no outline.
            listDl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 153), cardRadius);

            // PlayLv 96x96 at x 19.2..115.2 (centre 67.2), vertically centred,
            // sprite bg_base_circle_h96_wh, TMP 48.
            const float lvCx = p0.x + 67.2f * k;
            listDl->AddCircleFilled(ImVec2(lvCx, centerY), 48.0f * k, diffColor);
            addTextCentered(listDl, title, 48.0f * k, ImVec2(lvCx, centerY - 2.0f * k), white, rowLevel);
            // PlayLvTitle = txt_musicLv 102x38 at x 16.2, y_cell 10..48
            // (= -62 .. -24 from the centre): the plate hangs over the disc's
            // upper edge rather than sitting above it.
            const float plateW = 102.0f * k;
            const float plateH = 38.0f * k;
            const ImVec2 plate0(p0.x + 16.2f * k, centerY - 62.0f * k);
            if (levelTex != 0) {
                listDl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(levelTex)),
                    plate0, ImVec2(plate0.x + plateW, plate0.y + plateH),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));
            } else {
                // Fallback when the sprite is missing: the drawn plate.
                listDl->AddRectFilled(plate0, ImVec2(plate0.x + plateW, plate0.y + plateH),
                    diffColor, 6.0f * k);
                addTextCentered(listDl, body, 20.0f * k,
                    ImVec2(plate0.x + plateW * 0.5f, plate0.y + plateH * 0.5f), white,
                    selectLangIsJa() ? "楽曲Lv." : "歌曲等级");
            }

            // AudioSpectrumAnalyzer: this is what sits where the compact row
            // puts its jacket. Prefab values (Highlight branch): six Bar_N
            // 8x72 at x 133/147/161/175/189/203 (pitch 14), bottom edge at
            // y_cell 107.7 (= +35.7 from the cell centre), pivot (0.5, 0) - so
            // a bar grows UPWARD from that fixed base. No sprite, m_Color
            // (0.2667, 0.2667, 0.4) = #444466 opaque.
            // Height: UIPartsSpectrumAnalyzer.Update scales y by
            // clamp(|20*log10(level)|/32, 0..1) and collapses the bar to
            // zeroScale (0,0,1) below BASE_LEVEL_THRESHOLD 0.02, reading
            // levels 0/3/6/9/12/14 of BGMPlayer's 16 bands.
            // Without a measurement (headless, muted, no device) the slot would
            // be empty again, so it sits at a neutral half height instead.
            {
                const float barBase = centerY + 35.7f * k;
                const float barFull = 72.0f * k;
                for (int b = 0; b < 6; ++b) {
                    const float bx = p0.x + (133.0f + 14.0f * b) * k;
                    const float lvl = previewBands != nullptr ? previewBands[b] : 0.5f;
                    const float h = barFull * std::clamp(lvl, 0.0f, 1.0f);
                    if (h <= 0.5f) {
                        continue;
                    }
                    listDl->AddRectFilled(ImVec2(bx, barBase - h), ImVec2(bx + 8.0f * k, barBase),
                        IM_COL32(68, 68, 102, 255));
                }
            }

            // Text column: Title 617x56 at x 231.8 (TMP 40), Artist 617x50 at
            // x 236.7 (TMP 28), Vocal 617x50 at x 237.7 (TMP 28); their y_cell
            // centres are 0.3 / 48 / 88.2.
            const std::string vo = g.vocal.empty() ? std::string() : "Vo. " + g.vocal;
            if (title != nullptr) {
                addTextLeft(listDl, title, 40.0f * k, ImVec2(p0.x + 231.8f * k, centerY - 71.7f * k),
                    IM_COL32(60, 60, 80, 255), g.title.c_str());
            }
            addTextLeft(listDl, body, 28.0f * k, ImVec2(p0.x + 236.7f * k, centerY - 24.0f * k),
                IM_COL32(120, 120, 140, 255), g.artist.c_str());
            if (!vo.empty()) {
                addTextLeft(listDl, body, 28.0f * k, ImVec2(p0.x + 237.7f * k, centerY + 16.2f * k),
                    IM_COL32(140, 140, 160, 255), vo.c_str());
            }
            // Clear status: UIPartsGroupMusicDifficultyClear is 230x29.1 at
            // x 233.8, y_cell 138.7..167.7, HorizontalLayoutGroup
            // (ChildAlignment = 6 LowerLeft, spacing 1, padding 0, 38x38
            // children) -> first diamond's left edge is the group's left.
            const float diaS = 38.0f * k;
            const float diaPitch = 39.0f * k; // 38 + spacing 1
            float dx = p0.x + 233.8f * k + diaS * 0.5f;
            const float dy = centerY + 81.2f * k;
            for (int d = 0; d < kDiffCount; ++d) {
                addDiamondSlot(listDl, backTex, ImVec2(dx, dy), diaS, g.idx[d] >= 0, badgeFor(d));
                dx += diaPitch;
            }
            drawMvBadges(p0, p1, true);
        } else {
            // Compact row: no card fill - the entries are separated by the
            // prefab's own UnderLine (see the bottom of this branch).
            // Hover tint eases in - and, keyed by the slot, eases out again on the
            // row the pointer just left instead of vanishing.
            const float rowHot = ui::anim(0x4a552000u + static_cast<ImGuiID>(slot + 0x100000),
                slot == hoverSlot, 20.0f);
            if (rowHot > 0.01f) {
                listDl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, static_cast<int>(20.0f * rowHot)),
                    8.0f * k);
            }

            // PlayLv 80x80 at x 26.3..106.3 (centre 66.3), vertically centred,
            // sprite bg_base_circle_h96_wh, TMP 36.
            listDl->AddCircleFilled(ImVec2(p0.x + 66.3f * k, centerY), 40.0f * k, diffColor);
            addTextCentered(listDl, title, 36.0f * k, ImVec2(p0.x + 66.3f * k, centerY - 2.0f * k),
                white, rowLevel);

            // JacketThumbnail 96x96 at x 122.7..218.7 y_cell 23.3..119.3 ->
            // centre (170.7, -0.7). SQUARE: the RawImage has no material (only
            // the loading placeholder behind it is r10), so no rounding.
            const float js = 96.0f * k;
            const ImVec2 jc(p0.x + 170.7f * k, centerY - 0.7f * k);
            addJacket(listDl, thumbFor(renderer, g.coverPath),
                ImVec2(jc.x - js * 0.5f, jc.y - js * 0.5f),
                ImVec2(jc.x + js * 0.5f, jc.y + js * 0.5f), 0.0f);

            // Title 617x56 (TMP 32), y_cell centre 47.5. The compact cell has
            // NO artist line - Neutral's only text is Title.
            //
            // The left edge is 243.6, not the 293.4 the wrapper chain gives:
            // `Title`(the RectMask2D show-area) sits at x 293.4 with its TMP
            // child at 243.6, i.e. the mask is 49.8 px to the RIGHT of the text
            // it is supposed to clip, and 293.4 + its 617 width = 910.4 pokes
            // out of the 895 cell. Every sibling cell agrees with the text, not
            // the mask: MusicSelectSingleListCell (1121) puts mask and TMP at
            // the same x (238), the Highlight branch here does the same (231.8),
            // and this row's own jacket slot ends at 218.7 - a 24.9 px gap to
            // 243.6, matching Highlight's 13.1. With the mask's x you would get
            // a 74.7 px hole between the jacket and the title.
            addTextLeft(listDl, body, 32.0f * k, ImVec2(p0.x + 243.6f * k, centerY - 24.5f * k),
                white, g.title.c_str());
            // Clear status: 230x29.1 at x 236.4, y_cell 88.1..117.1,
            // layout group with spacing 0 and padding L1/B1 -> diamonds 28x28
            // back to back from x 237.4 (the 6th slot's 8px `Padding` spacer
            // is for the Append difficulty, which we do not have).
            const float diaS = 28.0f * k;
            float dx = p0.x + 237.4f * k + diaS * 0.5f;
            const float dy = centerY + 30.6f * k;
            for (int d = 0; d < kDiffCount; ++d) {
                addDiamondSlot(listDl, backTex, ImVec2(dx, dy), diaS, g.idx[d] >= 0, badgeFor(d));
                dx += diaS; // spacing 0
            }

            // MV badges (see drawMvBadges above for the prefab numbers).
            drawMvBadges(p0, p1, false);

            // UnderLine: base_wh 856.64x4, white 20%, x 19.2..875.8. It is a
            // child of Neutral (not of Base), so it exists on compact rows only,
            // and it straddles the cell's bottom edge: y_cell 142..146.
            listDl->AddRectFilled(ImVec2(p0.x + 19.2f * k, centerY + 70.0f * k),
                ImVec2(p0.x + 875.8f * k, centerY + 74.0f * k), IM_COL32(255, 255, 255, 51));
        }
    }
    // Drop the animated heights of slots that left the screen: a slot is a
    // position in the cyclic list, so they would otherwise accumulate (and the
    // value a returning slot inherits would be meaningless anyway).
    for (auto it = slotHeights.begin(); it != slotHeights.end();) {
        it = (it->first < firstSlot - 4 || it->first > lastSlot + 4) ? slotHeights.erase(it) : std::next(it);
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
        const bool clicked = interactive && ui::mouseClicked(ImGuiMouseButton_Left);
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
                platform::logf(platform::LogSelect, "[select] jump to section '%s'\n", keys[static_cast<size_t>(i)].label.c_str());
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
        if (!partyReadOnly && !overlayUp
            && (ui::keyPressed(ImGuiKey_DownArrow)
                || ui::keyPressed(ImGuiKey_GamepadDpadDown))) {
            ui::se(ui::SeSelect);
            moveTo(1);
        }
        if (!partyReadOnly && !overlayUp
            && (ui::keyPressed(ImGuiKey_UpArrow) || ui::keyPressed(ImGuiKey_GamepadDpadUp))) {
            ui::se(ui::SeSelect);
            moveTo(-1);
        }
        if (!overlayUp && (ui::keyPressed(ImGuiKey_Enter) || ui::keyPressed(ImGuiKey_KeypadEnter))) {
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
    }
    ImGui::EndChild();

    // ------------------------------------------------------------------
    // Right: tilted-phone panel (img_smartphone.png) with the jacket, the
    // difficulty buttons and 确定 / 随机 / 切换歌手. The whole block is laid out
    // flat and then rotated about the phone's centre, like the reference UI.
    // ------------------------------------------------------------------
    // ScreenLayerFreeLiveCellPhone (prefab 704, RE'd 2026-11-15): 754.3 x
    // 1416.7. 1920 画布上它的右缘在 1899.15（右留 20.8）、上缘在 33.65；容器
    // 本身是中心锚点 + anchoredPosition (562, -202)，所以画布高度变化时它会
    // 跟着中心走。我们这里改成**贴右上**：
    //   * 右缘写 `w - 20.8k`，缩放调小（uiScale < 1）时手机跟着窗口右缘，
    //     不会像以前那样留在 1522k 处、和右边空出几百像素（用户报的"手机面板
    //     直接分离了"）；
    //   * 上缘写 `33.6k` 而不是"窗口中心 +202k"：缩放调小以后画布会高于窗口，
    //     中心锚点会把手机往下推，顶部反而空一大块；原作的静态上缘就是 33.6。
    // 宽度大于画布时（窗口很矮/很窄）仍然按 33.6k 贴顶、底部被切，和原作
    // "手机比屏幕高"的观感一致。
    const float phoneW = 754.3f * k;
    const float phoneH = 1416.7f * k;
    const float phoneX = w - 20.8f * k - phoneW;
    // 垂直：原作是中心锚点（中心 = 画布中心 + 202），画布矮（21:9）时整台手机上移，
    // 把"确定"留在屏内；但缩放调小以后画布比 1080 高，中心锚点会把手机往下推，
    // 顶上反而空一大块。所以取"上缘 33.6k"和"原作中心锚点"里更靠上的那个：
    //   16:9 100% → 两者都是 33.6；16:9 70% → 33.6*0.7 贴顶；
    //   21:9 → 中心锚点更靠上，和原作一致（顶部被切、确定可见）。
    const float phoneYTop = 33.6f * k;
    const float phoneYCentered = h * 0.5f + 202.0f * k - phoneH * 0.5f;
    const float phoneY = std::min(phoneYTop, phoneYCentered);
    // Tutorial anchor: the phone panel is the "pick a song here" surface, and a
    // spotlight around the whole panel reads far better than one around the
    // EASY circle (which also used to dim 确定).
    ui::setAnchor("phone", ImVec2(phoneX - 10.0f * k, phoneY - 10.0f * k),
        ImVec2(phoneX + phoneW + 10.0f * k, phoneY + phoneH + 10.0f * k));

    // Tilt: the prefab's RectTransform stores z = -0.026176924, w = 0.99965733
    // (rt 752), i.e. exactly -3.00 deg by 2*atan2(z, w). Unity's +z is
    // counter-clockwise in a y-UP frame, so -3 deg drops the top edge's RIGHT
    // end and lifts the left one: the handset leans RIGHT (top-left corner
    // sticking out, top-right corner tucked in) - which is what the reference
    // screenshots show, and what the row of difficulty pads measures
    // (dy/dx = +0.054 => +3.1 deg with the right pad lower).
    //
    // ⚠ The sibling `m_LocalEulerAnglesHint: {z: +2.9999971}` is STALE - it
    // contradicts the quaternion, and the quaternion is what Unity renders
    // with. Quoting that hint is what once sent this constant the wrong way.
    // Our rotation happens in a y-DOWN frame, so "right end lower" is a
    // POSITIVE angle here - hence +3.0, not -3.0.
    constexpr float kTiltDeg = 3.0f;
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
    const float scrY1 = phoneY + phoneH * 0.970f;
    const float cx = (scrX0 + scrX1) * 0.5f;
    const float sw = scrX1 - scrX0;

    if (!groups.empty() && selected >= 0) {
        const ChartEntry& item = entries[static_cast<size_t>(selected)];
        const SongGroup& group = groups[static_cast<size_t>(groupIndex)];

        // Jacket (top of the phone screen). All numbers are the official
        // prefab's (704, CellPhone = 754.3 x 1416.7), measured 2026-11-15:
        // JacketContent anchors 42px below the phone's top edge; inside it the
        // *sharp* jacket is the square "Jacket" node (376x376, centre 238.5px
        // below the content top, x offset -2.6) - so the jacket's top edge
        // sits 92.5px below the phone top, clear of the notch band, whose
        // bottom is at 67px. The 700x574 rect (JacketMask > Jacket, with
        // UIPartsBlurMusicJacket inside) is only the BLUR BLEED area: it starts
        // at the content top, i.e. tucks under the notch on purpose.
        // Everything below is written in the phone's OWN pixel grid (prefab
        // 704: 754.3 x 1416.7, origin at the top-left) and scaled by `ps`.
        // The numbers are measured off the resolved RectTransform tree, not
        // eyeballed: Jacket 376x376 at (186.5, 92.5); MusicTitle x=81.7
        // y=488.9 (440.2x50); ArtistName y=543.9; VocalName y=580.9;
        // DifficultyLabel 144x40 at (303.0, 637.3); the difficulty row
        // 576x133.38 at (89.1, 662.1) with 96px discs 24 apart;
        // PositiveButton (確定) 258x96 at (245.1, 807.4); the common-button
        // row 702x100 at (24.4, 898.5) with 80px buttons; RankBase 112x112
        // centred at (613.8, 555.2).
        const float ps = phoneW / 754.3f; // one prefab pixel in screen px
        const auto PX = [&](float x) { return phoneX + x * ps; };
        const auto PY = [&](float y) { return phoneY + y * ps; };
        const float jw = 376.0f * ps;
        const float jh = jw;
        const ImVec2 j0(PX(186.5f), PY(92.5f));

        // UIPartsBlurMusicJacket: the *same* jacket, blown up and smeared,
        // sitting immediately behind the art - it does NOT fill the phone.
        //
        // Prefab 704, all measured (2026-11-22), phone-local px:
        //   JacketContent
        //     Positions / JacketMask(700x700 @24.5,42)      <- r20 sprite Mask
        //       Jacket (anchors 0,1-1,1, sizeDelta 0x574)   <- 700x574 + RectMask2D
        //         UIPartsBlurMusicJacket(231)
        //           JacketThumbnail(190) = CustomRawImage   <- alpha 0.29803923
        //     Jacket(299) 376x376 @186.55,92.45             <- sharp art
        // So: ONE quad, 700x574, hung from the mask's top edge (y 42..616),
        // and the widening comes from UIPartsBlurMusicJacket.Blur
        // (RVA 0x4D1AB88): it renders the jacket into a RenderTexture of the
        // *source* texture's size through Sekai/UI/UIGaussianBlur with
        // _SamplingDistance 2.8 and assigns it to the RawImage - i.e. the art
        // is stretched to fill 700x574 (aspect deliberately distorted), no
        // extra layers, and the quad's own colour alpha is 0.29803923 = 76/255.
        //
        // The 700x574 node also carries RectMask2D with m_Softness (0, 389):
        // the quad's TOP and BOTTOM edges fade over 389px each, and since
        // 389 > 574/2 the two ramps overlap, so the alpha peaks where the two
        // distances are equal - in the middle, at (287/389) * 76 = 56/255 - and
        // is 0 at both edges. Left/right softness is 0, i.e. hard vertical
        // edges. That is what the old "three concentric copies at falling
        // alpha" was trying to fake; this draws the real profile instead, with
        // the four vertices of each half recoloured (AddImage takes one colour
        // for the whole quad).
        {
            const float bleedW = 700.0f * ps;
            const float bleedH = 574.0f * ps;
            const float hw = bleedW * 0.5f;
            const float hh = bleedH * 0.5f;
            const ImVec2 jc(PX(374.5f), PY(42.0f + 287.0f));
            const float u0 = (jc.x - hw - scrX0) / (scrX1 - scrX0);
            const float v0 = (jc.y - hh - scrY0) / (scrY1 - scrY0);
            const float u1 = (jc.x + hw - scrX0) / (scrX1 - scrX0);
            const float v1 = (jc.y + hh - scrY0) / (scrY1 - scrY0);
            const GLuint blurTex = blurredJacketTex(renderer, group.coverPath, u0, v0, u1, v1);
            if (blurTex != 0) {
                const float soft = 389.0f * ps;
                const int peak = static_cast<int>(std::min(1.0f, hh / soft) * 76.0f + 0.5f);
                const auto halfQuad = [&](float y0, float y1, float tv0, float tv1, int aTop, int aBot) {
                    const int first = dl->VtxBuffer.Size;
                    dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(blurTex)),
                        ImVec2(jc.x - hw, y0), ImVec2(jc.x + hw, y1),
                        ImVec2(0.0f, tv0), ImVec2(1.0f, tv1), IM_COL32(255, 255, 255, 255));
                    // AddImage appends its four corners in TL, TR, BR, BL order.
                    for (int v = 0; v < 4; ++v) {
                        const bool isTop = (v == 0 || v == 1);
                        const int a = isTop ? aTop : aBot;
                        ImU32& col = dl->VtxBuffer[first + v].col;
                        col = (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
                    }
                };
                const float mid = jc.y;
                halfQuad(jc.y - hh, mid, 0.0f, 0.5f, 0, peak);
                halfQuad(mid, jc.y + hh, 0.5f, 1.0f, peak, 0);
            }
        }

        // The sharp art has SQUARE corners: prefab 704's Jacket(299) is a
        // sibling of JacketMask(262), not a child of it, so the r20 mask never
        // touches it. What the real JacketImage carries is the
        // 'JacketEdgeTransparency' material (Sekai/UI/EdgeTransparency) with
        // _EdgeWidth 0.005 / _EdgeAlpha 0.1 - a ~1.9px fade to 10% alpha, not
        // a radius. The r20 claim in SELECT-CELLPHONE.md was wrong.
        addJacket(dl, renderer.cover() != nullptr ? renderer.cover()->id : thumbFor(renderer, group.coverPath),
            j0, ImVec2(j0.x + jw, j0.y + jh), 0.0f);
        // No outline either: the old 2px white ring + r12 was hand-drawn.

        // Collaboration ribbon (歌曲出处, e.g. 「劇場版プロジェクトセカイ」).
        // Geometry from the phone-cell prefab (704): the Collaboration node is
        // a child of the 376x376 jacket, anchored to its top-left corner with
        // pivot (0,1) and anchoredPosition (-8, +8). In Unity's y-up space the
        // +8 lifts the ribbon's TOP edge 8px above the jacket top, and the
        // rect then extends 40.42 DOWN - so it lies across the jacket's
        // top-left corner, ~32px inside the jacket (the first version floated
        // entirely above the jacket, which is not what the prefab says).
        // Background: bg_base_r8_wh (24x24, r8) tinted (0.267, 0.267, 0.4) =
        // #444466, opaque; Text child at x=12, height 40. All offsets scale
        // with the jacket (jh / 376).
        const std::string collab = musicCollab(group.musicId);
        if (!collab.empty()) {
            const float rs = jh / 376.0f;
            const float rh = 40.42f * rs;
            const float pad = 12.0f * rs;
            const float fs = rh * 0.58f;
            const ImVec2 ts = body->CalcTextSizeA(fs, FLT_MAX, 0.0f, collab.c_str());
            const float rw = pad * 2.0f + ts.x;
            const ImVec2 r0(j0.x - 8.0f * rs, j0.y - 8.0f * rs);
            const ImU32 ribbonTint = IM_COL32(68, 68, 102, 255);
            const GLuint ribbonTex = badgeTex(renderer, "bg_base_r8_wh");
            if (ribbonTex != 0) {
                addNinePatch(dl, ribbonTex, 24, 24, r0, ImVec2(r0.x + rw, r0.y + rh), 8.0f, 8.0f,
                    rs, ribbonTint);
            } else {
                dl->AddRectFilled(r0, ImVec2(r0.x + rw, r0.y + rh), ribbonTint, 8.0f * rs);
            }
            dl->AddText(body, fs, ImVec2(r0.x + pad, r0.y + (rh - ts.y) * 0.5f),
                IM_COL32(255, 255, 255, 255), collab.c_str());
        }

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
        // The metadata block is NOT centred on the phone: the prefab puts it
        // left-aligned at x=81.7 in a 440.2-wide box, with the rank badge to
        // its right (RankBase centre x=613.8). Sizes follow the Text nodes'
        // own heights (50 / 28 / 28); TMP's fontSize was not captured, so
        // 34 / 26 / 26 is what fills those boxes without clipping.
        const float contentL = PX(81.7f);
        const float textMaxW = 440.2f * ps;
        // TMP fontSize straight off the prefab (2026-11-22): MusicTitle > Text is
        // 36, ArtistName/VocalName > Text are 28 - not "34 / 26 guessed from the
        // box height", which is what this used to be.
        const float titleSize = 36.0f * ps;
        const float metaSize = 28.0f * ps;
        const float badgeD = 112.0f * ps;
        // The three metadata lines are TMP boxes whose TEXT child is centred
        // inside them (VerticalAlignment = 512 = Middle). addTextLeft takes the
        // vertical CENTRE, so the box tops (488.9 / 543.9 / 580.9) must be
        // offset by half the *text* box: MusicTitle > Text is 488.9+7 = 495.9
        // tall 36 -> centre 513.9; ArtistName > Text 543.0 tall 28 -> 557.0;
        // VocalName > Text 580.9 tall 28 -> 594.9. Passing the box top straight
        // in (what this used to do) put every line 14-25px too high.
        float ty = PY(513.9f);
        if (title != nullptr) {
            const std::string text = ellipsize(title, titleSize, group.title, textMaxW);
            addTextLeft(dl, title, titleSize, ImVec2(contentL, ty), white, text.c_str());
        }
        ty = PY(557.0f);
        if (!item.artist.empty()) {
            const std::string text = ellipsize(body, metaSize, item.artist, textMaxW);
            addTextLeft(dl, body, metaSize, ImVec2(contentL, ty), grayText, text.c_str());
        }
        // The "Vo." line follows the version picked in the 切换歌手 panel (the
        // chip row that used to sit here is gone - the panel replaced it, see
        // the round singer button at the bottom of this panel).
        {
            const std::vector<VocalVersion>& versions = vocalVersions;
            // 换歌（或者刚改过默认歌手偏好）就把手上这个版本号丢掉，否则它会
            // 一路挂着 —— 上一首选的是虚拟歌手 ver，换到下一首还是它，
            // defaultVocalIndex 永远轮不到（2026-11-01 用户报的"设置没生效"）。
            // 丢掉之后先看这首歌自己记过没有（在 切换歌手 面板里手动点过的），
            // 没有才交给偏好。
            static int vocalSongId = -1;
            static int vocalPrefStamp = -1;
            if (vocalPrefStamp != gVocalPrefStamp) {
                vocalPrefStamp = gVocalPrefStamp;
                gVocalChoiceBySong.clear();
                vocalSongId = -1;
                vocalIndex = -1;
            }
            bool vocalResolved = false;
            if (item.musicId != vocalSongId) {
                vocalSongId = item.musicId;
                vocalResolved = true;
                const auto memo = gVocalChoiceBySong.find(item.musicId);
                vocalIndex = memo != gVocalChoiceBySong.end() ? memo->second : -1;
            }
            // The caller's value wins when it is valid for this song, so the
            // version can be driven from outside (--select-vocal, a hand-off,
            // or a test); otherwise fall back to what the player last picked
            // in the panel, then to the default vocal preference.
            int choice = defaultVocalIndex(item.musicId, versions);
            if (vocalIndex >= 0 && vocalIndex < static_cast<int>(versions.size())) {
                choice = vocalIndex;
                gVocalChoiceBySong[item.musicId] = choice;
            } else if (const auto it = gVocalChoiceBySong.find(item.musicId); it != gVocalChoiceBySong.end()) {
                choice = it->second;
            }
            if (vocalResolved) {
                // 每首歌唱一次：有几个可用版本、按偏好挑中哪个。排查"设置没生效"
                // 先看这行 —— 版本数 1 就说明库里只有那一个文件，跟偏好无关。
                platform::logf(platform::LogSelect, "[select] vocal song=%d versions=%d choice=%d", item.musicId,
                    static_cast<int>(versions.size()), choice);
                if (choice >= 0 && choice < static_cast<int>(versions.size())) {
                    std::printf(" type=%s", versions[static_cast<size_t>(choice)].type.c_str());
                }
                std::printf(" pref=%d", gVocalPrefMode);
                std::puts("");
                std::fflush(stdout);
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
                ty = PY(594.9f);
                std::string vo = "Vo. " + vocalText;
                float voSize = metaSize;
                ImVec2 ts = body->CalcTextSizeA(voSize, FLT_MAX, 0.0f, vo.c_str());
                if (ts.x > textMaxW) {
                    voSize *= textMaxW / ts.x;
                }
                vo = ellipsize(body, voSize, vo, textMaxW);
                addTextLeft(dl, body, voSize, ImVec2(contentL, ty), grayText, vo.c_str());
                ty += 32.0f * k;
            }
            // Best-score rank badge: right-aligned to the content box and
            // centred on the metadata block that just ended. In the reference
            // it straddles the artist and Vo. rows, which is where centring on
            // the whole block lands anyway.
            // RankBase: 112x112 centred at (613.8, 555.2) - the right edge of
            // the handset, on the artist / Vo. band.
            drawBestScoreBadge(dl, renderer, body, item.bestScore, chartRatingFor(item), badgeD,
                PX(613.8f), PY(555.2f));
        }

        // Difficulty row. 多人游玩: this row is the member's difficulty
        // picker - it *is* the room screen the panel replaces - so the filled
        // ring follows the player's own pick instead of the list cursor, and a
        // click is reported to the caller rather than moving the (read-only)
        // list. Only the five difficulties the list can name are offered.
        //
        // Geometry is the prefab's, including the bit the static dump hides:
        // `Difficulty` (576 x 133.38) carries a HorizontalLayoutGroup with
        // m_Spacing = 24 and m_ChildAlignment = 0 (UpperLeft), so the five 96px
        // pads sit at x = 0,120,240,360,480 and are TOP-aligned in the box -
        // NOT centred. Container top is 662.17, so pad tops are 662.17 and the
        // centres are 710.2 (the old 728.8 was the container's vertical centre,
        // 18.6px too low).
        const float diffScale = ps;
        const bool partyPick = partyReadOnly;
        const float dcD = 96.0f * diffScale;
        const float dcGap = 24.0f * diffScale;
        // First disc's centre: container left 89.2 + 48.
        float dx = PX(137.2f);
        const float dcY = PY(710.2f);

        static const char* kDiffOffSprites[kDiffCount] = {
            "btn_difficulty_easy_off", "btn_difficulty_normal_off", "btn_difficulty_hard_off",
            "btn_difficulty_expert_off", "btn_difficulty_master_off"};
        static const char* kDiffOnSprites[kDiffCount] = {
            "btn_difficulty_easy_on", "btn_difficulty_normal_on", "btn_difficulty_hard_on",
            "btn_difficulty_expert_on", "btn_difficulty_master_on"};
        const GLuint diffSelectedTex = selectTex(renderer, "eff_btn_difficulty_selected_wh");
        const GLuint diffLockTex = selectTex(renderer, "icon_lock_wh");
        // FrontGlowCover: the official 104x104 halo each pad carries at alpha 0
        // until it is the selected one (prefab: eff_btn_circle_h80_a70_wh tinted
        // with that pad's colour).
        const GLuint diffGlowTex = selectTex(renderer, "eff_btn_circle_h80_a70_wh");
        const float imgD = 112.0f * diffScale;
        const float selD = 136.0f * diffScale;
        for (int d = 0; d < kDiffCount; ++d) {
            const bool avail = group.idx[d] >= 0;
            const bool active = avail && (partyPick ? party->myDifficulty == d : d == diffIndex);
            const ImVec2 c(dx, dcY);

            ImGui::SetCursorScreenPos(tiltedItemPos(c, ImVec2(dcD, dcD)));
            ImGui::PushID(d);
            if (avail && (!partyPick || partyPickable) && !overlayUp) {
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
            {
                char anchor[24];
                std::snprintf(anchor, sizeof(anchor), "diff%d", d);
                ui::setAnchor(anchor, ImVec2(dx - selD * 0.5f, dcY - selD * 0.5f),
                    ImVec2(dx + selD * 0.5f, dcY + selD * 0.5f + 22.0f * k));
            }

            // Selected: 136px white ring behind the 112px difficulty disc.
            if (active && diffSelectedTex != 0) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(diffSelectedTex)),
                    ImVec2(c.x - selD * 0.5f, c.y - selD * 0.5f),
                    ImVec2(c.x + selD * 0.5f, c.y + selD * 0.5f), ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));
            }
            // Halo behind the disc, only on the active pad (FrontGlowCover).
            if (active && diffGlowTex != 0) {
                const float gw = 104.0f * diffScale;
                const ImU32 glowCol = (kDiffColors[d] & ~IM_COL32_A_MASK) | (150u << IM_COL32_A_SHIFT);
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(diffGlowTex)),
                    ImVec2(c.x - gw * 0.5f, c.y - gw * 0.5f),
                    ImVec2(c.x + gw * 0.5f, c.y + gw * 0.5f), ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f), glowCol);
            }
            const GLuint diffTex = selectTex(renderer, active ? kDiffOnSprites[d] : kDiffOffSprites[d]);
            if (diffTex != 0) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(diffTex)),
                    ImVec2(c.x - imgD * 0.5f, c.y - imgD * 0.5f),
                    ImVec2(c.x + imgD * 0.5f, c.y + imgD * 0.5f), ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    avail ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 126, 145, 255));
            } else {
                dl->AddCircleFilled(c, dcD * 0.5f,
                    avail ? kDiffColors[d] : IM_COL32(120, 126, 145, 255), 40);
            }
            if (!avail) {
                dl->AddCircleFilled(c, imgD * 0.5f, IM_COL32(0, 0, 34, 128), 48);
                if (diffLockTex != 0) {
                    const float lw = 40.0f * diffScale;
                    const float lh = 46.0f * diffScale;
                    dl->AddImage(
                        reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(diffLockTex)),
                        ImVec2(c.x - lw * 0.5f, c.y - lh * 0.5f),
                        ImVec2(c.x + lw * 0.5f, c.y + lh * 0.5f), ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));
                }
            }

            char lvBuf[16];
            const char* lvText = "-";
            if (avail) {
                lvText = levelText(entries[static_cast<size_t>(group.idx[d])], lvBuf, sizeof(lvBuf));
            }
            // The level number goes INSIDE the pad: `DifficultyLevel` is a
            // 200x50 box at anchoredPosition (0,0) with fontSize 46, centred.
            addTextCentered(dl, body, 46.0f * diffScale, ImVec2(c.x, c.y), white, lvText);
            // ...and the difficulty NAME goes OUTSIDE, below the circle:
            // `DifficultyText` sits at anchoredPosition (-1.63, -66.77) inside the
            // 96px pad, fontSize 24, a 182.3-wide centred box. 66.77 > 48, so the
            // label is clear of the ring - that is exactly what the reference
            // screenshot shows ("EASY" under the ring, wider than the circle).
            // Colour is m_fontColor white in the prefab for every state.
            float nameSize = 24.0f * diffScale;
            const float nameMaxW = 182.3f * diffScale;
            const ImVec2 nts = body->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, kDiffNames[d]);
            if (nts.x > nameMaxW) {
                nameSize *= nameMaxW / nts.x;
            }
            addTextCentered(dl, body, nameSize,
                ImVec2(c.x - 1.63f * diffScale, c.y + 66.77f * diffScale),
                avail ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 170, 255),
                kDiffNames[d]);
            dx += dcD + dcGap;
        }

        // 确定 button (mint capsule, drawn by hand so it tilts with the phone).
        // 多人游玩: this is the room's button, not a local "play this" - the
        // press goes to the caller (the host locks the round, a member confirms
        // itself) and nothing is loaded here. It is drawn dimmed while there is
        // nothing to confirm yet (no song from the host, or no difficulty
        // picked), so the rule is visible instead of just rejected.
        const bool okReady = !partyPick || (partyPickable && party->myDifficulty >= 0);
        // PositiveButton: 258x96 at (245.1, 807.4). Much wider and taller than
        // the old "58% of the screen" capsule - that is what the prefab has.
        const float okW = 258.0f * ps;
        const float okH = 96.0f * ps;
        const ImVec2 okA(PX(245.1f), PY(807.4f));
        const ImVec2 okB(okA.x + okW, okA.y + okH);
        const float okCx = (okA.x + okB.x) * 0.5f;
        ImGui::SetCursorScreenPos(tiltedItemPos(ImVec2(okCx, (okA.y + okB.y) * 0.5f), ImVec2(okW, okH)));
        ImGui::PushID("ok");
        if (overlayUp) {
            // 切换歌手 / 菜单 抽屉盖着这颗按钮；Dummy 只占位，不抢 hover。
            ImGui::Dummy(ImVec2(okW, okH));
        } else {
            ImGui::InvisibleButton("ok", ImVec2(okW, okH));
        }
        const bool okHovered = !overlayUp && ImGui::IsItemHovered();
        ui::setAnchor("confirm", okA, okB);
        const bool okPressed = !overlayUp && ImGui::IsItemClicked();
        ImGui::PopID();
        // ScreenLayerFreeLiveCellPhone > MainButtonContent > PositiveButton, read
        // off the prefab (2026-11-22):
        //   GlowCover  eff_btn_round_h80_a70_wh (112x104, soft white halo)
        //   ButtonImage btn_round_line_h80_wh  (106x96, border X=52 -> capsule)
        //               tinted (119,238,221) = #77EEDD
        //   Text       white
        //   Cover      bg_base_round_h80_wh tinted (0,0,34,128), -16 sizeDelta
        //              = the pressed state, inset 8 on each side
        // The old ui::capsuleDraw drew a *filled* mint capsule, which is not what
        // this prefab has: ButtonImage is the `line` variant (a mint ring with a
        // 10% mint wash inside, m_Color carries the mint). Both sprites are used
        // as-is now, so the radius (48 caps) and the halo are the official ones.
        const GLuint okGlowTex = selectTex(renderer, "eff_btn_round_h80_a70_wh");
        const GLuint okLineTex = selectTex(renderer, "btn_round_line_h80_wh");
        const GLuint okCoverTex = selectTex(renderer, "bg_base_round_h80_wh");
        if (okReady && okGlowTex != 0) {
            // GlowCover is a full-stretch child with sizeDelta (10,10), i.e. the
            // 96-tall shape grown by 5px on every side. The sprite is 70% white
            // by design, so it only ever gets a *low* alpha here - at 150 the
            // mint halo flooded the button's inside and the outline vanished.
            addNinePatch(dl, okGlowTex, 112, 104,
                ImVec2(okA.x - 5.0f * ps, okA.y - 5.0f * ps),
                ImVec2(okB.x + 5.0f * ps, okB.y + 5.0f * ps), 56.0f, 52.0f, ps,
                IM_COL32(255, 255, 255, okHovered ? 80 : 22));
        }
        if (okLineTex != 0) {
            addNinePatch(dl, okLineTex, 106, 96, okA, okB, 52.0f, 0.0f, ps,
                okReady ? IM_COL32(119, 238, 221, 255) : IM_COL32(150, 156, 178, 220));
        } else if (okReady) {
            dl->AddRect(okA, okB, IM_COL32(119, 238, 221, 255), okH * 0.5f, 0, 2.0f * ps);
        }
        if (okReady && okPressed && okCoverTex != 0) {
            addNinePatch(dl, okCoverTex, 88, 80, ImVec2(okA.x + 8.0f * ps, okA.y + 8.0f * ps),
                ImVec2(okB.x - 8.0f * ps, okB.y - 8.0f * ps), 44.0f, 0.0f, ps,
                IM_COL32(0, 0, 34, 128));
        }
        // PositiveButton > Text: fontSize 32, m_fontColor (0.4667, 0.9333, 0.8667)
        // = #77EEDD - the SAME mint as the ButtonImage ring, not white. (White
        // here was a guess; the prefab paints the label mint.)
        addTextCentered(dl, body, 32.0f * ps, ImVec2(okCx, (okA.y + okB.y) * 0.5f),
            okReady ? IM_COL32(119, 238, 221, 255) : IM_COL32(150, 156, 178, 220), "确定");
        // Where the button actually lands on screen (it is tilted with the phone),
        // so main.cpp can start the confirm flash from it.
        if (confirmCenter != nullptr) {
            *confirmCenter = tiltPoint(ImVec2(okCx, (okA.y + okB.y) * 0.5f));
        }
        if (confirmPhoneScreen != nullptr) {
            // Axis-aligned bounds of the tilted phone screen; the confirm light
            // uses this so it washes the handset rather than the whole window.
            const ImVec2 p0 = tiltPoint(ImVec2(scrX0, scrY0));
            const ImVec2 p1 = tiltPoint(ImVec2(scrX1, scrY0));
            const ImVec2 p2 = tiltPoint(ImVec2(scrX1, scrY1));
            const ImVec2 p3 = tiltPoint(ImVec2(scrX0, scrY1));
            *confirmPhoneScreen = ImVec4(
                std::min({p0.x, p1.x, p2.x, p3.x}), std::min({p0.y, p1.y, p2.y, p3.y}),
                std::max({p0.x, p1.x, p2.x, p3.x}), std::max({p0.y, p1.y, p2.y, p3.y}));
        }
        if (okPressed) {
            ui::se(ui::SeClick);
            if (partyRoom) {
                if (partyOut != nullptr) {
                    partyOut->confirm = true;
                }
            } else if (selected >= 0) {
                // 原作：确定 → ScreenLayerFreeLiveFinalConfirmation（卡组 /
                // 自动演出 / MV）→ Start 才进 live。不要把 selected 直接返回。
                gFinalConfirmOpen = true;
                gFinalEntry = selected;
                gFinalDifficulty = diffIndex;
                gFinalVocal = vocalIndex;
                action = SelectNone;
            }
        }

        // 随机 + 切换歌手: the two icons sit straight on the phone panel - no dark
        // disc behind them (the reference screen has none either): the pointer
        // only makes the glyph swell a little and come up to full white.
        // 多人游玩: the shuffle decides *which song the room plays*, so a member
        // gets a dimmed, inert one. 切换歌手 stays live - it only opens the
        // version list, and a member's window does not play the BGM anyway.
        //
        // 切换歌手 replaced the gear that used to sit here: 设置 moved up to the
        // header row with the other buttons (a per-window dialog, not a property
        // of the song), and the spot it left is exactly where a song-level
        // control belongs.
        // CommonButtonContent: a 702x100 strip at (24.45, 898.55) holding three
        // 80x80 buttons under a HorizontalLayoutGroup (m_Spacing = 108,
        // m_ChildAlignment = 4 = MiddleCentre, m_ChildControlWidth/Height = 0,
        // m_ChildForceExpand* = 0). Children in hierarchy order:
        //   SettingButton(83) / ShuffleButton(104, localScale 1.2) /
        //   ChangeVocalButton(298)
        //
        // **The setting button is NOT there on the select screen.** The prefab
        // only gives it `enabled`, and CommonButtonContent.SetupButton
        // (RVA 0x62459D4) is:
        //   randomButton.SetActive(info.ShouldShowRandomButton);   // [0x13]
        //   randomButton.enabled     = info.EnableRandomButton;    // [0x10]
        //   changeVocalButton.enabled = info.EnableChangeVocalButton; // [0x11]
        //   settingButton.SetActive(info.EnableSettingButton);     // [0x12]
        // (0x50E1548 = CustomButton.SetActive -> gameObject.SetActive;
        //  0xAD619F0 = Behaviour.set_enabled.) SetupCommonButtonContent
        // (RVA 0x655574) forwards its own bool arg as EnableSettingButton, and
        // BOTH select-screen entry points pass false - SetupMusicSelect
        // (0x6555034) at 0x65551AC and SetMusicData (0x6555914) at 0x655559BC
        // are `mov w2, wzr`. Only SetupFinalConfirm (0x6555620) passes 1.
        // So settingButton's GameObject is inactive, and HorizontalLayoutGroup
        // skips inactive children when it builds m_RectChildren - the two
        // survivors are laid out as a PAIR, not left in slots 1 and 2:
        //   3 children (final-confirm): total 456 -> left 123 -> 163/351/539
        //     (= SettingButton's authored anchoredPosition.x 163, a free check)
        //   2 children (this screen):   total 268 -> left 217 -> 257/445
        // Strip-local 257/445 + strip left 24.45 = phone-local 281.45/469.45,
        // pitch 188. Slot 1 must NOT stay empty.
        const float ibD = 80.0f * ps;
        const float ibY = PY(948.55f);
        // Glyph scale inside the 80x80 slot, straight off the prefab:
        // ShuffleButton > Base is 60x52 and ShuffleButton carries
        // m_LocalScale 1.2, so it renders 72x62.4;
        // ChangeVocalButton > Base is 68x68 at scale 1.
        static const float kIconW[2] = {72.0f, 68.0f};
        static const float kIconH[2] = {62.4f, 68.0f};
        static const float kIconX[2] = {281.45f, 469.45f};
        for (int i = 0; i < 2; ++i) {
            const bool lockedOut = partyReadOnly && i == 0;
            const ImVec2 c(PX(kIconX[i]), ibY);
            ImGui::SetCursorScreenPos(tiltedItemPos(c, ImVec2(ibD, ibD)));
            ImGui::PushID(i);
            if (overlayUp) {
                ImGui::Dummy(ImVec2(ibD, ibD));
            } else {
                ImGui::InvisibleButton("iconbtn", ImVec2(ibD, ibD));
            }
            const bool pressed = !lockedOut && !overlayUp && ImGui::IsItemClicked();
            const bool hovered = !lockedOut && !overlayUp && ImGui::IsItemHovered();
            const float hot = ui::anim(ImGui::GetItemID() ^ 0x71u, hovered, 18.0f);
            ImGui::PopID();
            // Official glyphs: CommonButtonContent > ShuffleButton > Base is
            // btn_random_lgr (60x52, drawn at the button's 1.2 scale = 72x62.4)
            // and > ChangeVocalButton > Base is btn_vocalChange_lgr (68x68),
            // both plain white with no disc behind them.
            const char* texName = i == 0 ? "btn_random_lgr" : "btn_vocalChange_lgr";
            const GLuint tex = selectTex(renderer, texName);
            if (tex != 0) {
                const float grow = 1.0f + 0.10f * hot;
                float iw = kIconW[i] * ps * grow;
                float ih = kIconH[i] * ps * grow;
                const int alpha = lockedOut ? 90 : static_cast<int>(226.0f + 29.0f * hot);
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                    ImVec2(c.x - iw * 0.5f, c.y - ih * 0.5f), ImVec2(c.x + iw * 0.5f, c.y + ih * 0.5f),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, alpha));
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
                    gVocalPanelOpen = !gVocalPanelOpen;
                    gVocalPanelScroll = 0.0f;
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

    // Tilt the phone: rotate every vertex the block above produced about the
    // phone's centre. Text, images, rounded shapes - all rotate together. The
    // same pass runs the intro animation: on entry the whole phone slides in
    // from the right and fades up (the reference screen does the same), which
    // is free here because every vertex is already being rewritten.
    const float enter = easeOutCubic(enterAnim);
    const float phoneSlide = (1.0f - enter) * 300.0f * k;
    const int enterAlpha = static_cast<int>(std::clamp(enter, 0.0f, 1.0f) * 255.0f);
    // The 切换歌手 drawer slides over the phone. It is a translucent mask, not an
    // opaque one: the phone's content keeps its own brightness and shows through
    // it, which is the point. Only the phone's *controls* go inert while the
    // drawer is up (the Dummy gates below).
    for (int i = phoneVtxFirst; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& p = dl->VtxBuffer[i].pos;
        // No per-song animation: the original swaps the phone's contents in
        // place, with no rise and no fade (only the screen's own entry - the
        // phone sliding in - is animated). Changing songs feels a lot snappier
        // for it, and identical to the reference.
        const float dx = p.x + phoneSlide - tiltPivot.x;
        const float dy = p.y - tiltPivot.y;
        p = ImVec2(tiltPivot.x + dx * tiltCos - dy * tiltSin, tiltPivot.y + dx * tiltSin + dy * tiltCos);
        const int vtxAlpha = enterAlpha;
        if (vtxAlpha < 255) {
            ImU32& col = dl->VtxBuffer[i].col;
            const ImU32 a = (col >> IM_COL32_A_SHIFT) & 0xFF;
            col = (col & ~IM_COL32_A_MASK)
                | (static_cast<ImU32>(a * static_cast<ImU32>(vtxAlpha) / 255u) << IM_COL32_A_SHIFT);
        }
    }

    // 排序 combo, in the same row as the search pill. The official screen puts a
    // sort FAB there; a combo is easier to hit on a touchscreen and it is the one
    // control in this row that has to show its current value.
    if (sortMode < 0 || sortMode > 1) {
        sortMode = 0;
    }
    if (groupMode < 0 || groupMode >= kGroupCount) {
        groupMode = 0;
    }
    {
        const char* kSortLabels[2] = {"按名称", "按难度"};
        // 原生菜单栏开着的时候（设置 > 系统）排序也在 视图 菜单里 —— 收起来，
        // 但值还是 settings.sortMode，两边读写的是同一个字段。
        if (!nativeMenuBar && !overlayUp) {
            const std::string sortPreview = std::string("排序：") + kSortLabels[sortMode];
            // No colours here: ui::combo brings the pjsk look itself (white box,
            // translucent white list, faint shadow). Only the metrics are ours -
            // rounding is what the shadow follows, padding is the height.
            // UIPartsDropdown: 280x56, pill radius (28). Font 26 so the label
            // fills the box the way the prefab's does.
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, sortH * 0.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(22.0f * k, (sortH - 26.0f * k) * 0.5f));
            ImGui::PushFont(body, 26.0f * k);
            ImGui::SetCursorScreenPos(ImVec2(comboX0, comboY));
            ImGui::SetNextItemWidth(comboW);
            // `k` is handed over because this screen has its own px-per-unit scale.
            const std::vector<std::string> sortItems{kSortLabels[0], kSortLabels[1]};
            ui::combo("##sortby", sortPreview.c_str(), sortItems, &sortMode, comboW,
                ImGuiComboFlags_HeightSmall, k);
            ImGui::PopFont();
            ImGui::PopStyleVar(2);
        }
    }

    // 筛选 / 菜单: two round header buttons instead of the old four capsules.
    // Everything that used to sit in this row - 刷新 / 音乐商店 / 猜歌 / 设置 - is
    // inside the menu now, and 分组依据 moved into the 筛选 card, so the row stays
    // one line and the list under it can have the official 912px width.
    const GLuint filterIconTex = selectTex(renderer, "icon_refine_wh");
    const GLuint menuIconTex = selectTex(renderer, "icon_menu_wh");
    // `iconW/iconH` are the official sprite sizes (筛选 29x28, 菜单 40x34) - they are
    // NOT a fraction of the disc, which is exactly why the lambda takes them per call.
    // `iconTint` carries only RGB: the alpha is driven by hover/on inside (220 -> 255),
    // which is the old behaviour every caller already relied on.
    auto headerRoundButton = [&](const char* id, float x, float y, float d, GLuint icon,
                                 bool on, bool dot = false, float iconW = 29.0f, float iconH = 28.0f,
                                 ImU32 iconTint = IM_COL32(96, 96, 120, 255)) -> bool {
        const ImVec2 c(x + d * 0.5f, y + d * 0.5f);
        ui::dropShadow(dl, ImVec2(x, y), ImVec2(x + d, y + d), d * 0.5f, k);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 236));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(232, 232, 242, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, d * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        const bool clicked = ImGui::Button(id, ImVec2(d, d));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        // "Dot" (48x36 in the prefab, centred 6px above the button's centre):
        // the official mark for "a filter is on". Drawn between the white disc
        // and the funnel, which is the prefab's own order (Cover -> Dot ->
        // NotFilteredIcon), so the icon stays readable on top of it.
        if (dot) {
            dl->AddCircleFilled(ImVec2(c.x, c.y - 3.0f * k), 22.0f * k,
                IM_COL32(236, 96, 128, 255), 36);
        }
        // The shipped icons are white on transparent, so the navy comes from the
        // tint (same trick as the old capsule labels).
        if (icon != 0) {
            // NotFilteredIcon / FilteredIcon are 29x28, the hamburger 40x34 - 1:1 at
            // 1080p, not a fraction of the button.
            const float iw = iconW * k;
            const float ih = iconH * k;
            const ImU32 tint = (iconTint & ~IM_COL32_A_MASK)
                | ((hovered || on) ? (255u << IM_COL32_A_SHIFT) : (220u << IM_COL32_A_SHIFT));
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(icon)),
                ImVec2(c.x - iw * 0.5f, c.y - ih * 0.5f),
                ImVec2(c.x + iw * 0.5f, c.y + ih * 0.5f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                tint);
        }
        // The drawer paints over this row without being opaque, so the button
        // stays visible through it - swallow the click, the drawer is on top.
        return clicked && !overlayUp;
    };
    // No SetTooltip on these: ImGui's default atlas has no CJK glyphs, so a
    // Chinese tooltip renders as a row of '?'.
    // "Dot" badge (48x36) behind the funnel: the official mark for "a filter is
    // on". Ours is on when the unit tab is not 全部 or the search box has text.
    if (headerRoundButton("##unitfilter", filterBtnX, filterBtnY, filterBtnD, filterIconTex,
            gFilterOpen, unitTab != 0 || searchBuf[0] != 0)) {
        ui::se(ui::SeClick);
        gFilterOpen = true;
    }
    // 菜单钮照原作：96 白圆、汉堡 40x34、染 #444466（= 68,68,102）。
    // 按一下打开右侧抽屉（原作 MenuDialog，见函数末尾）；抽屉里有 刷新 / 音乐商店 /
    // 猜歌 / 设置 / 个人资料 / 随机 / 筛选 / 排序，原来那颗小 Popup 退休了。
    // 抽屉打开时这颗钮**收起来** —— 原作的 ScreenLayerHeader 就是这么做的
    // （HideMenuButton / ShowMenuAndHideMenuButton），否则它会隔着 80% 的面板
    // 透出来，看起来像没盖住。
    if (!menuUp && headerRoundButton("##selectmenu", menuBtnX, menuBtnY, menuBtnD, menuIconTex,
            false, false, 40.0f, 34.0f, IM_COL32(68, 68, 102, 255))) {
        ui::se(ui::SeClick);
        gMenuOpen = true;
        // 抽屉和这些浮层互斥，别叠两层。
        gVocalPanelOpen = false;
        gFilterOpen = false;
    }
    // Tutorial anchors: the four header buttons are gone, so their spotlights
    // point at the menu that now holds them (the story keeps working, and the
    // highlighted control is a real one you can open).
    const ImVec2 menuBtnMin(menuBtnX, menuBtnY);
    const ImVec2 menuBtnMax(menuBtnX + menuBtnD, menuBtnY + menuBtnD);
    ui::setAnchor("rescan", menuBtnMin, menuBtnMax);
    ui::setAnchor("store", menuBtnMin, menuBtnMax);
    ui::setAnchor("guess", menuBtnMin, menuBtnMax);
    ui::setAnchor("abssettings", menuBtnMin, menuBtnMax);

    // F5 rescan is handled by main; Enter handled above. Left/right switch
    // difficulty when the phone panel is showing - and for a 多人游玩 member this
    // is the *only* meaning the arrows have, since the list does not move, so
    // the pick is reported to the caller as well.
    // Keyboard arrows use the committed group, not the phone block's local
    // `group` (the phone block is only drawn when a song is selected).
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
    if (!overlayUp && !groups.empty() && selected >= 0 && (!partyReadOnly || partyPickable)) {
        if (ui::keyPressed(ImGuiKey_LeftArrow)) {
            nudgeDifficulty(-1);
        }
        if (ui::keyPressed(ImGuiKey_RightArrow)) {
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
    // 2026-11-21: the chip is off for now. 2026-11-22 起菜单钮按原作挪到了窗口右缘
    // （1808..1904），等级牌再打开会和它抢同一个角 —— 真要恢复就先决定谁让位。
    // Kept compiled (and reachable via --profile) instead of deleted so the profile
    // card does not lose its only entry point.
    constexpr bool kShowLevelChip = false;
    if (kShowLevelChip && account != nullptr) {
        const double need = game::expToNextRank(account->rank);
        const float expRatio = need > 0.0 ? static_cast<float>(account->exp / need) : 1.0f;
        const ImVec2 chipAnchor(w - 26.0f * k, headerRowY);
        const ImVec4 chip = ui::playerLevelChip(dl, body, chipAnchor, account->rank, k, true, expRatio);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        // Manual hit test (the chip is not an ImGui item), so it needs its own
        // "something is over me" test: the 切换歌手 drawer covers it. (It still
        // *shows through* the drawer - the drawer is a translucent mask - it just
        // does not react to the pointer.)
        const bool hot = !profileOpen && !overlayUp && mouse.x >= chip.x && mouse.x <= chip.x + chip.z
            && mouse.y >= chip.y && mouse.y <= chip.y + chip.w;
        if (hot) {
            // Hover: a soft ring, so the chip reads as clickable.
            dl->AddRect(ImVec2(chip.x - 2.0f, chip.y - 2.0f),
                ImVec2(chip.x + chip.z + 2.0f, chip.y + chip.w + 2.0f), IM_COL32(255, 255, 255, 120),
                (chip.w + 4.0f) * 0.5f, 0, 2.0f * k);
            if (ui::mouseClicked(ImGuiMouseButton_Left)) {
                profileOpen = true;
                ui::se(ui::SeClick);
            }
        }
    }

    // ------------------------------------------------------------------
    // 切换歌手 panel (state + rationale at the top of this file). A dark panel
    // that slides in from the right edge over everything: one radio card per
    // vocal version the song actually has.
    //
    // Drawn after the tilt pass on purpose: it is a screen-space overlay, not
    // part of the phone, so it must not rotate with it. Its slide + fade is the
    // same vertex-rewrite trick the phone uses for a song change.
    // ------------------------------------------------------------------
    if (vocalSong != nullptr && vocalPanelUp) {
        if (gVocalPanelSong != vocalSong->musicId) {
            gVocalPanelSong = vocalSong->musicId;
            gVocalPanelScroll = 0.0f;
        }
        // 2026-11-21: rebuilt from prefab 1960/1961. The original is a
        // right-side SubWindowDialog, not a narrow rounded drawer:
        //   Bg 956xfull, bg_base_wh, #444466 at 80%;
        //   full-screen Cover #000022 at 50%;
        //   ListView 828 wide (956-128), rows 828x164;
        //   row card bg_base_r20_wh #DFDFEB, radius 20, no border;
        //   radio 72x72 at +16, text starts at +102 (name 36 / members 32).
        // Its prefab has no header text (`header: null`), so no title row.
        {
            const ImVec2 vpMouse = ImGui::GetIO().MousePos;
            if (vpMouse.x < vocalPX0 && ui::mouseClicked(ImGuiMouseButton_Left)) {
                ui::se(ui::SeWindowClose);
                gVocalPanelOpen = false;
            }
        }
        const float vpEase = vocalEase;
        const float vpW = vocalPW;
        const float vpX0 = vocalPX0;
        const float vpX1 = vpX0 + vpW;
        const float vpY0 = 0.0f;
        const float vpY1 = h;
        const int vpVtxFirst = dl->VtxBuffer.Size;

        // Full-screen dim (CloseArea/Cover) then the opaque 956-wide panel.
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(w, h),
            IM_COL32(0, 0, 34, static_cast<int>(128.0f * vpEase)));
        dl->AddRectFilled(ImVec2(vpX0, vpY0), ImVec2(vpX1, vpY1),
            IM_COL32(68, 68, 102, static_cast<int>(204.0f * vpEase)));

        // Close button: 44x44, inset 18.1/18.5 from the top-right corner.
        {
            const float closeD = 44.0f * k;
            const ImVec2 closeC(vpX1 - 18.1f * k - closeD * 0.5f, 18.5f * k + closeD * 0.5f);
            ImGui::SetCursorScreenPos(
                ImVec2(closeC.x - closeD * 0.5f, closeC.y - closeD * 0.5f));
            ImGui::PushID("vocalclose");
            ImGui::InvisibleButton("x", ImVec2(closeD, closeD));
            const bool xHot = ImGui::IsItemHovered();
            const bool xHit = ImGui::IsItemClicked();
            ImGui::PopID();
            const GLuint closeTex = selectTex(renderer, "btn_close_wh");
            if (closeTex != 0) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(closeTex)),
                    ImVec2(closeC.x - closeD * 0.5f, closeC.y - closeD * 0.5f),
                    ImVec2(closeC.x + closeD * 0.5f, closeC.y + closeD * 0.5f), ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, xHot ? 255 : 230));
            } else {
                const float d = closeD * 0.25f;
                dl->AddLine(ImVec2(closeC.x - d, closeC.y - d), ImVec2(closeC.x + d, closeC.y + d),
                    IM_COL32(240, 240, 248, 255), 2.6f * k);
                dl->AddLine(ImVec2(closeC.x + d, closeC.y - d), ImVec2(closeC.x - d, closeC.y + d),
                    IM_COL32(240, 240, 248, 255), 2.6f * k);
            }
            if (xHit) {
                ui::se(ui::SeWindowClose);
                gVocalPanelOpen = false;
            }
        }

        // ListView geometry: 828 wide, top 40, bottom inset 95 (the prefab's
        // -128 / -135 sizeDelta and -27.5 offset resolve to those numbers at
        // 1080p). Row height 164, 12 gap.
        const float vpListL = vpX0 + 64.0f * k;
        const float vpListR = vpX1 - 64.0f * k;
        const float vpListTop = 40.0f * k;
        const float vpListBot = vpY1 - 95.0f * k;
        const float vpCardW = vpListR - vpListL;
        const float vpCardH = 164.0f * k;
        const float vpCardGap = 12.0f * k;
        const float vpNameSize = 36.0f * k;
        const float vpLineSize = 32.0f * k;
        const float vpTextXOff = 102.0f * k;
        const float vpTextMaxW = vpCardW - vpTextXOff - 24.0f * k;

        std::vector<std::vector<std::string>> vpLines(vocalVersions.size());
        for (size_t i = 0; i < vocalVersions.size(); ++i) {
            vpLines[i] = vocalSingerLines(vocalVersions[i].singers, body, vpLineSize, vpTextMaxW);
            if (vpLines[i].size() > 2) {
                vpLines[i].resize(2);
            }
        }
        const float vpTotal = vocalVersions.empty()
            ? 0.0f
            : static_cast<float>(vocalVersions.size()) * vpCardH
                + static_cast<float>(vocalVersions.size() - 1) * vpCardGap;
        const float vpViewH = vpListBot - vpListTop;
        const float vpMaxScroll = std::max(0.0f, vpTotal - vpViewH);
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (mouse.x >= vpListL && mouse.x <= vpListR && mouse.y >= vpListTop
                && mouse.y <= vpListBot) {
                gVocalPanelScroll -= ImGui::GetIO().MouseWheel * 62.0f * k;
            }
            gVocalPanelScroll = std::clamp(gVocalPanelScroll, 0.0f, vpMaxScroll);
        }

        int vpCurrent = defaultVocalIndex(vocalSong->musicId, vocalVersions);
        if (const auto it = gVocalChoiceBySong.find(vocalSong->musicId);
            it != gVocalChoiceBySong.end()) {
            vpCurrent = it->second;
        }

        ImGui::PushClipRect(ImVec2(vpListL, vpListTop), ImVec2(vpListR, vpListBot), true);
        float vpY = vpListTop - gVocalPanelScroll;
        for (size_t i = 0; i < vocalVersions.size(); ++i) {
            const float vpTop = vpY;
            const float vpBot = vpY + vpCardH;
            vpY = vpBot + vpCardGap;
            if (vpBot <= vpListTop || vpTop >= vpListBot) {
                continue;
            }
            const bool vpPicked = static_cast<int>(i) == vpCurrent;
            const ImVec2 c0(vpListL, vpTop);
            const ImVec2 c1(vpListR, vpBot);

            ImGui::SetCursorScreenPos(c0);
            ImGui::PushID(static_cast<int>(i) + 7100);
            ImGui::InvisibleButton("vocalrow", ImVec2(vpCardW, vpCardH));
            const bool vpHot = ImGui::IsItemHovered();
            const bool vpHit = ImGui::IsItemClicked();
            ImGui::PopID();
            const float vpGlow = ui::anim(0x4a577000u + static_cast<ImGuiID>(i), vpHot, 18.0f);

            // #DFDFEB fill, radius 20, no outline (bg_base_r20_wh).
            dl->AddRectFilled(c0, c1,
                ui::mix(IM_COL32(223, 223, 235, 255), IM_COL32(234, 235, 246, 255), vpGlow),
                20.0f * k);

            // Radio: UIPartsRadioToggle 72x72 at left 16, vertically centred.
            const float vpRadioD = 72.0f * k;
            const ImVec2 vpRc(c0.x + 16.0f * k + vpRadioD * 0.5f, (c0.y + c1.y) * 0.5f);
            dl->AddCircleFilled(vpRc, vpRadioD * 0.5f, IM_COL32(255, 255, 255, 255), 48);
            if (vpPicked) {
                const GLuint checkTex = selectTex(renderer, "icon_radio_gn");
                if (checkTex != 0) {
                    const float checkD = 36.0f * k;
                    dl->AddImage(
                        reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(checkTex)),
                        ImVec2(vpRc.x - checkD * 0.5f, vpRc.y - checkD * 0.5f),
                        ImVec2(vpRc.x + checkD * 0.5f, vpRc.y + checkD * 0.5f), ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));
                }
            }

            // Name centre at +40, member block at +107 with 40px lines.
            const float vpTextX = c0.x + vpTextXOff;
            const std::string vpLabel = vocalVersionLabel(vocalVersions[i]);
            addTextLeft(dl, body, vpNameSize, ImVec2(vpTextX, c0.y + 40.0f * k),
                IM_COL32(85, 85, 119, 255), vpLabel.c_str());
            float vpLineY = c0.y + 87.0f * k;
            for (const std::string& vpLine : vpLines[i]) {
                addTextLeft(dl, body, vpLineSize, ImVec2(vpTextX, vpLineY),
                    IM_COL32(68, 68, 102, 255), vpLine.c_str());
                vpLineY += 40.0f * k;
            }

            if (vpHit) {
                ui::se(ui::SeClick);
                gVocalChoiceBySong[vocalSong->musicId] = static_cast<int>(i);
                vocalIndex = static_cast<int>(i);
            }
        }
        if (vocalVersions.empty()) {
            addTextCentered(dl, body, 22.0f * k,
                ImVec2((vpListL + vpListR) * 0.5f, (vpListTop + vpListBot) * 0.5f),
                IM_COL32(220, 220, 235, 255), "暂无其他演唱版本");
        }
        ImGui::PopClipRect();

        // Scrollbar: 6 wide at the right inset, like the prefab.
        if (vpMaxScroll > 0.0f) {
            const float sx = vpListR + 38.0f * k;
            const float thumbH = std::max(40.0f * k, vpViewH * (vpViewH / vpTotal));
            const float thumbY = vpListTop + (vpViewH - thumbH) * (gVocalPanelScroll / vpMaxScroll);
            dl->AddRectFilled(ImVec2(sx, vpListTop), ImVec2(sx + 6.0f * k, vpListBot),
                IM_COL32(223, 223, 234, 51), 3.0f * k);
            dl->AddRectFilled(ImVec2(sx, thumbY), ImVec2(sx + 6.0f * k, thumbY + thumbH),
                IM_COL32(235, 235, 242, 255), 3.0f * k);
        }

        // Slide-in fade: drop the alpha of everything the panel just added.
        const int vpFade = static_cast<int>(std::clamp(vpEase, 0.0f, 1.0f) * 255.0f);
        if (vpFade < 255) {
            for (int v = vpVtxFirst; v < dl->VtxBuffer.Size; ++v) {
                ImU32& col = dl->VtxBuffer[v].col;
                const ImU32 a = (col >> IM_COL32_A_SHIFT) & 0xFF;
                col = (col & ~IM_COL32_A_MASK)
                    | (static_cast<ImU32>(a * static_cast<ImU32>(vpFade) / 255u) << IM_COL32_A_SHIFT);
            }
        }
    }

    // ------------------------------------------------------------------
    // 右上角菜单抽屉（原作 ScreenLayerHeader 的 menuButton -> MenuDialog）
    //
    // 原作实测（prefab 1775，见 SELECT-UI.md）：
    //   Bg   x=56..960（1920 画布）→ 904 宽、满高、贴右缘，bg_base_wh 染
    //        #444466 / 80%（和 切换歌手 的 Bg 同色，只是窄 52）；
    //   内容 856 宽（面板内左右各 24）；
    //   大格 392x184，两列（x 56 / 456），行距 8；小格 192x152（大格高 184 的
    //        那一排也在用），四列（x 56 / 256 / 456 / 656）；
    //   关闭钮 44x44，右缘 18.1、顶 18.5。
    //
    // 原作的格子里，标题是**烤进图里**的日文（btn_menu_shop 那一套），所以
    // 我们用同一套几何自己画：白卡 + 左上角深蓝标题牌 + 右下角官方图标。
    // 文字走字体，中文/日文切换时不用换图。图标全部是官方的白色单色图（
    // assets/select/*.png 与解包 sprite），按 #444466 染。
    //
    // 我们放进去的是本作真正有的功能（用户的四颗顶栏按钮 + 筛选/排序/资料/
    // 随机）：刷新列表、音乐商店、猜歌、设置、个人资料、随机选曲、筛选、排序。
    // ------------------------------------------------------------------
    if (menuUp) {
        const float openX = w - menuW; // 完全展开后的左缘（"点外面"用）
        // 点抽屉左边 = 关（原作 CloseArea）。只在基本展开后判定，否则打开它的
        // 那一下点击会立刻把它关掉。
        if (gMenuOpen && menuT > 0.9f && ui::mouseClicked(ImGuiMouseButton_Left)
            && ImGui::GetIO().MousePos.x < openX) {
            ui::se(ui::SeWindowClose);
            gMenuOpen = false;
        }
        const int menuAlpha = static_cast<int>(204.0f * menuEase); // 原作 80%
        // Bg：纯色 #444466 / 80%、满高、贴右（原作 MenuDialog 的 Bg）。左缘一条
        // 2px 的淡白边把抽屉和背景分开（原作贴图没有投影）。
        dl->AddRectFilled(ImVec2(menuX0, 0.0f), ImVec2(menuX0 + menuW, h),
            IM_COL32(68, 68, 102, menuAlpha));
        dl->AddRectFilled(ImVec2(menuX0, 0.0f), ImVec2(menuX0 + 2.0f * k, h),
            IM_COL32(255, 255, 255, static_cast<int>(26.0f * menuEase)));

        // 关闭钮：44x44，距右缘 18.1、距顶 18.5（prefab 的 UIPartsCloseButton）。
        {
            const float closeD = 44.0f * k;
            const ImVec2 closeC(menuX0 + menuW - 18.1f * k - closeD * 0.5f,
                18.5f * k + closeD * 0.5f);
            ImGui::SetCursorScreenPos(
                ImVec2(closeC.x - closeD * 0.5f, closeC.y - closeD * 0.5f));
            ImGui::PushID("menuclose");
            ImGui::InvisibleButton("x", ImVec2(closeD, closeD));
            const bool xHot = ImGui::IsItemHovered();
            const bool xHit = ImGui::IsItemClicked();
            ImGui::PopID();
            const GLuint closeTex = selectTex(renderer, "btn_close_wh");
            const int xA = static_cast<int>((xHot ? 255.0f : 230.0f) * menuEase);
            if (closeTex != 0) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(closeTex)),
                    ImVec2(closeC.x - closeD * 0.5f, closeC.y - closeD * 0.5f),
                    ImVec2(closeC.x + closeD * 0.5f, closeC.y + closeD * 0.5f),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, xA));
            } else {
                const float d = closeD * 0.25f;
                dl->AddLine(ImVec2(closeC.x - d, closeC.y - d), ImVec2(closeC.x + d, closeC.y + d),
                    IM_COL32(240, 240, 248, xA), 2.6f * k);
                dl->AddLine(ImVec2(closeC.x + d, closeC.y - d), ImVec2(closeC.x - d, closeC.y + d),
                    IM_COL32(240, 240, 248, xA), 2.6f * k);
            }
            if (xHit) {
                ui::se(ui::SeWindowClose);
                gMenuOpen = false;
            }
        }

        struct MenuCellDef
        {
            const char* label;
            const char* icon;
            float iconW; // 官方 sprite 的原尺寸，按比例塞进右下角的方框
            float iconH;
            float x;
            float y;
            float w;
            float h;
            bool big;
            int command;
        };
        enum MenuCommand
        {
            CmdRescan = 1,
            CmdDownload,
            CmdSettings,
            CmdGuess,
            CmdProfile,
            CmdShuffle,
            CmdFilter,
            CmdSort,
        };
        // 坐标全部相对抽屉左缘，单位是 1920 画布（再乘 k）。y 用的是 1920x1080
        // 屏上的绝对 y（= 1080 - prefab 的 y-up 值），和 rect_resolve 的输出对齐。
        static const MenuCellDef kMenuCells[] = {
            {"音乐商店", "store", 40.0f, 40.0f, 56.0f, 96.4f, 392.0f, 184.0f, true, CmdDownload},
            {"猜歌", "guess", 40.0f, 40.0f, 456.0f, 96.4f, 392.0f, 184.0f, true, CmdGuess},
            {"刷新列表", "refresh", 40.0f, 40.0f, 56.0f, 287.4f, 392.0f, 184.0f, true, CmdRescan},
            {"设置", "musicsetting", 40.0f, 40.0f, 456.0f, 287.4f, 392.0f, 184.0f, true, CmdSettings},
            {"个人资料", "icon_menu_profile", 70.0f, 48.0f, 56.0f, 480.4f, 192.0f, 184.0f, false, CmdProfile},
            {"随机选曲", "btn_random_lgr", 60.0f, 52.0f, 256.0f, 480.4f, 192.0f, 184.0f, false, CmdShuffle},
            {"筛选", "icon_refine_wh", 29.0f, 28.0f, 456.0f, 480.4f, 192.0f, 184.0f, false, CmdFilter},
            {"排序", "icon_menu_text", 94.0f, 34.0f, 656.0f, 480.4f, 192.0f, 184.0f, false, CmdSort},
        };
        int menuCommand = 0;
        const int kMenuCellCount = static_cast<int>(sizeof(kMenuCells) / sizeof(kMenuCells[0]));
        for (int i = 0; i < kMenuCellCount; ++i) {
            const MenuCellDef& def = kMenuCells[i];
            const ImVec2 c0(menuX0 + def.x * k, def.y * k);
            const ImVec2 c1(c0.x + def.w * k, c0.y + def.h * k);
            ImGui::SetCursorScreenPos(c0);
            ImGui::PushID(8100 + i);
            ImGui::InvisibleButton("cell", ImVec2(c1.x - c0.x, c1.y - c0.y));
            const bool hot = ImGui::IsItemHovered();
            const bool hitCell = ImGui::IsItemClicked();
            ImGui::PopID();
            const float glow = ui::anim(0x4a57c000u + static_cast<ImGuiID>(i), hot, 18.0f);
            if (hitCell && gMenuOpen) {
                menuCommand = def.command;
            }

            // 卡片：白 80%（官方 btn_menu_* 的底），悬停提亮；圆角 20、无描边。
            ui::dropShadow(dl, c0, c1, 20.0f * k, k, 0.8f);
            dl->AddRectFilled(c0, c1,
                ui::mix(IM_COL32(235, 235, 242, static_cast<int>(204.0f * menuEase)),
                    IM_COL32(247, 247, 252, static_cast<int>(235.0f * menuEase)), glow),
                20.0f * k);
            // 悬停时的白色光晕（官方 Effect 是 eff_cell_r20_wh 的 0→可见）。
            if (glow > 0.01f) {
                dl->AddRect(c0, c1,
                    IM_COL32(255, 255, 255, static_cast<int>(70.0f * glow * menuEase)),
                    20.0f * k, 0, 2.0f * k);
            }

            // 左上角深蓝标题牌 + 白字（官方 btn_menu_* 的标题就是一块 #444466）。
            // 用 title（粗体）而不是 body：官方的标题是粗圆体，细字压不住那块深蓝。
            // 排序格子把当前模式写进标签（和顶栏下拉是同一个 sortMode），
            // 否则点一下只看到列表顺序变了、不知道切到了哪种。
            const std::string sortLabel = sortMode == 0 ? "排序：名称" : "排序：难度";
            const char* cellLabel = def.command == CmdSort ? sortLabel.c_str() : def.label;
            ImFont* plateFont = title != nullptr ? title : body;
            const float textSize = (def.big ? 32.0f : 24.0f) * k;
            const float plateH = (def.big ? 50.0f : 40.0f) * k;
            const float platePadX = (def.big ? 16.0f : 12.0f) * k;
            const float plateInset = 10.0f * k;
            const ImVec2 labelSize = plateFont->CalcTextSizeA(textSize, FLT_MAX, 0.0f, cellLabel);
            const float maxPlateW = (c1.x - c0.x) - plateInset * 2.0f;
            const float plateW = std::min(maxPlateW, labelSize.x + platePadX * 2.0f);
            const ImVec2 p0(c0.x + plateInset, c0.y + plateInset);
            dl->AddRectFilled(p0, ImVec2(p0.x + plateW, p0.y + plateH),
                IM_COL32(68, 68, 102, static_cast<int>(255.0f * menuEase)), 10.0f * k);
            dl->AddText(plateFont, textSize,
                ImVec2(p0.x + platePadX, p0.y + (plateH - labelSize.y) * 0.5f),
                IM_COL32(255, 255, 255, static_cast<int>(255.0f * menuEase)), cellLabel);

            // 右下角图标：官方白色单色图，染 #444466；大格 84（MenuCell 的 Icon
            // 尺寸），小格 56。按原图长宽比塞进方框，别把 ABC / 资料卡压成正方形。
            const float maxBox = (def.big ? 84.0f : 56.0f) * k;
            const float iconAspect = def.iconW / def.iconH;
            const float drawW = iconAspect >= 1.0f ? maxBox : maxBox * iconAspect;
            const float drawH = iconAspect >= 1.0f ? maxBox / iconAspect : maxBox;
            const float iconMargin = (def.big ? 22.0f : 16.0f) * k;
            const GLuint iconTex = selectTex(renderer, def.icon);
            const ImVec2 ic1(c1.x - iconMargin, c1.y - iconMargin);
            const ImVec2 ic0(ic1.x - drawW, ic1.y - drawH);
            if (iconTex != 0) {
                dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(iconTex)),
                    ic0, ic1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                    IM_COL32(68, 68, 102, static_cast<int>((190.0f + 50.0f * glow) * menuEase)));
            }
        }

        if (menuCommand != 0) {
            ui::se(ui::SeClick);
            gMenuOpen = false;
            switch (menuCommand) {
            case CmdRescan:
                action = SelectRescan;
                break;
            case CmdDownload:
                action = SelectDownload;
                break;
            case CmdSettings:
                action = SelectSettings;
                break;
            case CmdGuess:
                // 题库分帧构建，点击不会卡（openGuessDialog 自己处理）。
                openGuessDialog();
                break;
            case CmdProfile:
                gProfileOpen = true;
                break;
            case CmdFilter:
                gFilterOpen = true;
                break;
            case CmdSort:
                sortMode = (sortMode == 0) ? 1 : 0;
                break;
            case CmdShuffle:
                if (!groups.empty()) {
                    int next = groupIndex;
                    for (int tries = 0; tries < 8 && next == groupIndex; ++tries) {
                        next = std::rand() % static_cast<int>(groups.size());
                    }
                    groupIndex = next;
                }
                break;
            default:
                break;
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
    //
    // Two things this block gets right that it used to get wrong (2026-11-02):
    //   * `alive` — the card has to keep being drawn while beginCard() plays the
    //     close animation. Gating on `gGuess.open` alone hid it in the very frame
    //     it was dismissed (`st.t` never ran down and `st.ended` was never set),
    //     so the card popped out with no exit, and because the entrance only
    //     replays after a finished close, the *next* open had no entrance either.
    //   * the pool is built by tickGuessPool() across frames, so the frame that
    //     opens the card draws "正在准备题库" instead of freezing on 12.9k aliases.
    // ------------------------------------------------------------------
    if (gGuess.open) {
        gGuessAlive = true;
    }
    if (gGuessBuild.active) {
        tickGuessPool(); // 卡片关了也继续建，下次点就不用等
    }
    if (gGuessAlive) {
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

            if (gGuess.loading || gGuess.unavailable) {
                // 加载 / 没题库：卡片照常立着，只换中间那一段。加载期间题库在
                // 后台按 ~3ms/帧推进，所以进度是真的会走的，不是装饰。
                ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 92.0f * s));
                ImGui::PushFont(titleFont(), 30.0f * s);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kNotePink), "%s",
                    gGuess.loading ? "正在准备题库…" : "没有可用的题库");
                ImGui::PopFont();

                ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 152.0f * s));
                ui::caption(gGuess.loading ? "首次加载请耐心等待。"
                                           : "找不到 music-aliases.json，或者别名表过小。",
                    17.0f * s, ui::kTitleText, interior);

                if (gGuess.loading) {
                    const float pct = static_cast<float>(std::clamp(gGuess.loadingPercent, 0, 100)) / 100.0f;
                    ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 200.0f * s));
                    const ImVec2 barPos = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(interior, 12.0f * s));
                    ui::expBar(ImGui::GetWindowDrawList(), barPos, interior, s, pct);
                    char pctLine[32];
                    std::snprintf(pctLine, sizeof(pctLine), "%d%%", gGuess.loadingPercent);
                    ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 218.0f * s));
                    ui::caption(pctLine, 16.0f * s, ui::kTitleText, interior);
                }

                const float footY = topY + cardSize.y - 66.0f * s;
                ImGui::SetCursorScreenPos(ImVec2(leftX, footY));
                if (ui::capsuleButton("关闭", ImVec2(interior, 50.0f * s), false)) {
                    gGuess.open = false; // 题库还在后台继续建，下次点就直接能玩
                }
                ui::endCard();
            } else {
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
                // One answer, two ways in (the pointer and the keyboard). Both have
                // to land in exactly the same place - the tally and the streak are
                // the whole point of the mode - so they share this instead of
                // duplicating the bookkeeping.
                auto pickOption = [&](int i) {
                    if (gGuess.picked >= 0 || i < 0 || i >= 4) {
                        return;
                    }
                    gGuess.picked = i;
                    ++gGuess.asked;
                    if (i == gGuess.answer) {
                        ++gGuess.correct;
                        ++gGuess.streak;
                        gGuess.bestStreak = std::max(gGuess.bestStreak, gGuess.streak);
                    } else {
                        gGuess.streak = 0;
                    }
                };

                bool answered = gGuess.picked >= 0;
                const float optW = (interior - 14.0f * s) * 0.5f;
                const float optH = 52.0f * s;
                for (int i = 0; i < 4; ++i) {
                    const int col = i % 2;
                    const int row = i / 2;
                    ImGui::SetCursorScreenPos(ImVec2(leftX + static_cast<float>(col) * (optW + 14.0f * s),
                        topY + (146.0f + static_cast<float>(row) * 62.0f) * s));
                    std::string label = std::to_string(i + 1) + ". " + titleFor(gGuess.options[i]);
                    ImU32 fill = ui::kWhiteBtn;
                    if (answered && i == gGuess.answer) {
                        fill = ui::kPrimary;
                    } else if (answered && i == gGuess.picked) {
                        fill = IM_COL32(255, 138, 150, 255);
                    }
                    if (guessOptionPill(label.c_str(), ImVec2(optW, optH), fill, s, !answered)) {
                        pickOption(i);
                    }
                }

                // ---- Keyboard: 1/2/3/4 选, 回车下一题, Esc 关闭 ------------------
                // ImGui's key state is global - IsKeyPressed does not care which
                // window holds focus - so these work wherever the pointer is. The
                // `false` is "on the press, not on auto-repeat": holding 1 must not
                // answer this question and the next one with it.
                //
                // ESC is also handled by the main loop, which runs *before* this
                // function in a frame; it asks game::guessDialogOpen() first and
                // stands down while the card is up, so the close lands here.
                {
                    static const ImGuiKey kOptionKeys[4] = {
                        ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4};
                    for (int i = 0; i < 4; ++i) {
                        if (ui::keyPressed(kOptionKeys[i], false)) {
                            pickOption(i);
                        }
                    }
                    // Enter does the job of the button under the grid: 下一题 once
                    // answered, 跳过 before that.
                    if (ui::keyPressed(ImGuiKey_Enter, false)
                        || ui::keyPressed(ImGuiKey_KeypadEnter, false)) {
                        newGuessQuestion();
                    }
                    if (ui::keyPressed(ImGuiKey_Escape, false)) {
                        gGuess.open = false;
                    }
                    // A keyboard answer landed in this very frame, above the
                    // drawing already done - re-read so the verdict line and the
                    // button label show it now instead of blinking a frame later.
                    answered = gGuess.picked >= 0;
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
            } // 题目 / 加载态二选一
        } else {
            // 关闭动画播完了（或者从一开始就没开过）：next open 会重新走一遍入场。
            gGuessAlive = false;
        }
    }

    // ------------------------------------------------------------------
    // 筛选 card. The official screen has a 絞り込み popup in that spot; ours
    // carries the two list filters, because the header row is now just
    // 排序 + 筛选 + 菜单:
    //   * 团体  - the same value as the left strip (nice on a narrow window,
    //             where the strip and the list fight for the width);
    //   * 分组依据 - moved out of the header row.
    // The card is `alive` for the length of its close animation; gating on
    // `open` alone hides a card in the frame it was dismissed.
    // ------------------------------------------------------------------
    if (gFilterOpen) {
        gFilterAlive = true;
    }
    if (gFilterAlive) {
        const float s = ui::scale();
        ImVec2 cardSize(560.0f * s, 470.0f * s);
        ImVec2 cardCenter(w * 0.5f, h * 0.5f);
        bool closeClicked = false;
        if (ui::beginCard("##sselectfilter", &cardCenter, &cardSize, true, true, &closeClicked, gFilterOpen)) {
            if (closeClicked) {
                gFilterOpen = false;
            }
            const float padX = 30.0f * s;
            const float interior = cardSize.x - padX * 2.0f;
            const float leftX = cardCenter.x - cardSize.x * 0.5f + padX;
            const float topY = cardCenter.y - cardSize.y * 0.5f;

            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 16.0f * s));
            ui::cardTitle("筛选", interior);

            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 62.0f * s));
            ui::caption("团体", 18.0f * s, ui::kTitleText, interior);
            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 90.0f * s));
            // Short labels on purpose: 8 options in one row, and the official
            // tabs are logos rather than names anyway.
            const std::vector<std::string> unitLabels = {
                "全部", "VS", "L/N", "MMJ", "VBS", "W×S", "25時", "其他"};
            ui::radioRow("##filterunit", unitLabels, &unitTab, interior);

            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 196.0f * s));
            ui::caption("分组依据", 18.0f * s, ui::kTitleText, interior);
            ImGui::SetCursorScreenPos(ImVec2(leftX, topY + 224.0f * s));
            const std::vector<std::string> groupLabels = {"关闭", "按难度段", "按读音", "按首字", "按收藏"};
            if (ui::radioRow("##filtergroup", groupLabels, &groupMode, interior)) {
                ui::se(ui::SeClick);
            }

            const float footY = topY + cardSize.y - 66.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(leftX, footY));
            if (ui::capsuleButton("关闭", ImVec2(interior, 50.0f * s), false)) {
                gFilterOpen = false;
            }
            ui::endCard();
        } else {
            gFilterAlive = false;
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

