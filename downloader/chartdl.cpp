// CppSekai chart downloader (chartdl) - a separate, standalone tool.
//
// Downloads charts / BGM / jackets for Project SEKAI songs from unipjsk into the
// game's charts\ folder, including every vocal version of a song, and writes the
// metadata sidecar (<id4>.json) the game reads.
//
// It shares nothing with the game: the UI is plain Win32 common controls
// (ListView + edit + progress bar), Chinese, no skin - a utility, not the game.
//
//   chartdl.exe                       # the GUI
//   chartdl.exe --list [filter]       # print the song table and exit
//   chartdl.exe --download 374,75 --diffs all --vocals all --out ..\charts
//   chartdl.exe --screenshot shot.png [--screenshot-time 1.5]
//
// HTTP comes from winhttp.dll loaded at runtime (the toolchain has no import
// library for it, same trick platform/SystemMedia.cpp uses for combase.dll).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// Unicode build: the ListView / button macros then resolve to their W variants
// (and IDC_ARROW & friends become wide resource pointers).
#define UNICODE
#define _UNICODE
#include <windows.h>

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include "nlohmann/json.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// SDL pulls in the screen saver API through winmm; the toolchain has no import
// library for the three symbols (same stubs as the game's main.cpp).
// ---------------------------------------------------------------------------
extern "C" long ScreenSaverProc() { return 0; }
extern "C" long ScreenSaverConfigureDialog() { return 0; }
extern "C" long RegisterDialogClasses() { return 0; }

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Minimal WinHTTP wrapper
// ---------------------------------------------------------------------------
namespace http
{
constexpr unsigned long kSecure = 0x00800000; // WINHTTP_FLAG_SECURE
constexpr unsigned long kQueryContentLength = 2;
constexpr unsigned long kQueryFlagNumber = 0x20000000;

struct Api
{
    void* dll = nullptr;
    void* (*open)(const wchar_t*, unsigned long, const wchar_t*, const wchar_t*, unsigned long) = nullptr;
    void* (*connect)(void*, const wchar_t*, unsigned short, unsigned long) = nullptr;
    void* (*openRequest)(void*, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*,
        const wchar_t**, unsigned long) = nullptr;
    int (*sendRequest)(void*, const wchar_t*, unsigned long, void*, unsigned long, unsigned long,
        unsigned long long) = nullptr;
    int (*receiveResponse)(void*, void*) = nullptr;
    int (*queryHeaders)(void*, unsigned long, const wchar_t*, void*, unsigned long*, unsigned long*) = nullptr;
    int (*queryDataAvailable)(void*, unsigned long*) = nullptr;
    int (*readData)(void*, void*, unsigned long, unsigned long*) = nullptr;
    int (*closeHandle)(void*) = nullptr;
    int (*setTimeouts)(void*, int, int, int, int) = nullptr;

    bool valid() const { return dll != nullptr && readData != nullptr && openRequest != nullptr; }
};

Api& api()
{
    static Api instance = [] {
        Api a;
        a.dll = reinterpret_cast<void*>(LoadLibraryW(L"winhttp.dll"));
        if (a.dll == nullptr) {
            return a;
        }
        auto sym = [&](const char* name) { return GetProcAddress(static_cast<HMODULE>(a.dll), name); };
        a.open = reinterpret_cast<decltype(a.open)>(sym("WinHttpOpen"));
        a.connect = reinterpret_cast<decltype(a.connect)>(sym("WinHttpConnect"));
        a.openRequest = reinterpret_cast<decltype(a.openRequest)>(sym("WinHttpOpenRequest"));
        a.sendRequest = reinterpret_cast<decltype(a.sendRequest)>(sym("WinHttpSendRequest"));
        a.receiveResponse = reinterpret_cast<decltype(a.receiveResponse)>(sym("WinHttpReceiveResponse"));
        a.queryHeaders = reinterpret_cast<decltype(a.queryHeaders)>(sym("WinHttpQueryHeaders"));
        a.queryDataAvailable = reinterpret_cast<decltype(a.queryDataAvailable)>(sym("WinHttpQueryDataAvailable"));
        a.readData = reinterpret_cast<decltype(a.readData)>(sym("WinHttpReadData"));
        a.closeHandle = reinterpret_cast<decltype(a.closeHandle)>(sym("WinHttpCloseHandle"));
        a.setTimeouts = reinterpret_cast<decltype(a.setTimeouts)>(sym("WinHttpSetTimeouts"));
        return a;
    }();
    return instance;
}

std::wstring widen(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len);
    return out;
}

// Splits "https://host/path" into the pieces WinHTTP wants.
bool splitUrl(const std::string& url, std::wstring& host, std::wstring& object, bool& secure)
{
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return false;
    }
    secure = url.compare(0, scheme, "https") == 0;
    const size_t pathStart = url.find('/', scheme + 3);
    const std::string hostPart = pathStart == std::string::npos ? url.substr(scheme + 3)
                                                                : url.substr(scheme + 3, pathStart - scheme - 3);
    const std::string pathPart = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    host = widen(hostPart);
    object = widen(pathPart);
    return !host.empty();
}

// Streaming GET. `onChunk(data, len, received, total)` returning false aborts.
// Writes nothing itself - the caller owns the file.
bool get(const std::string& url, const std::function<bool(const char*, size_t, long long, long long)>& onChunk,
    std::string& error)
{
    Api& a = api();
    if (!a.valid()) {
        error = "winhttp.dll not available";
        return false;
    }
    std::wstring host;
    std::wstring object;
    bool secure = true;
    if (!splitUrl(url, host, object, secure)) {
        error = "bad url";
        return false;
    }
    void* session = a.open(L"CppSekaiDownloader/1.0", 0, nullptr, nullptr, 0);
    if (session == nullptr) {
        error = "WinHttpOpen failed";
        return false;
    }
    if (a.setTimeouts != nullptr) {
        a.setTimeouts(session, 15000, 15000, 30000, 30000);
    }
    const unsigned short port = secure ? 443 : 80;
    void* connection = a.connect(session, host.c_str(), port, 0);
    void* request = nullptr;
    if (connection != nullptr) {
        request = a.openRequest(connection, L"GET", object.c_str(), nullptr, nullptr, nullptr,
            secure ? kSecure : 0);
    }
    bool ok = false;
    if (request != nullptr && a.sendRequest(request, nullptr, 0, nullptr, 0, 0, 0) != 0
        && a.receiveResponse(request, nullptr) != 0) {
        unsigned long status = 0;
        unsigned long statusLen = sizeof(status);
        a.queryHeaders(request, 19 /*WINHTTP_QUERY_STATUS_CODE*/ | kQueryFlagNumber, nullptr, &status,
            &statusLen, nullptr);
        if (status != 200) {
            error = "HTTP " + std::to_string(status);
        } else {
            long long total = 0;
            unsigned long lengthLen = sizeof(total);
            unsigned long lengthValue = 0;
            if (a.queryHeaders(request, kQueryContentLength | kQueryFlagNumber, nullptr, &lengthValue,
                    &lengthLen, nullptr)
                != 0) {
                total = static_cast<long long>(lengthValue);
            }
            std::vector<char> buffer(128 * 1024);
            long long received = 0;
            ok = true;
            while (true) {
                unsigned long available = 0;
                if (a.queryDataAvailable(request, &available) == 0) {
                    ok = false;
                    error = "connection lost";
                    break;
                }
                if (available == 0) {
                    break; // end of response
                }
                const unsigned long want = std::min<unsigned long>(available, static_cast<unsigned long>(buffer.size()));
                unsigned long read = 0;
                if (a.readData(request, buffer.data(), want, &read) == 0 || read == 0) {
                    ok = false;
                    error = "read failed";
                    break;
                }
                received += static_cast<long long>(read);
                if (!onChunk(buffer.data(), read, received, total)) {
                    error = "cancelled";
                    ok = false;
                    break;
                }
            }
        }
    } else if (error.empty()) {
        error = "request failed";
    }
    if (request != nullptr) {
        a.closeHandle(request);
    }
    if (connection != nullptr) {
        a.closeHandle(connection);
    }
    a.closeHandle(session);
    return ok;
}
} // namespace http

// ---------------------------------------------------------------------------
// Text / path conversion. Everything the UI shows or compares is UTF-8, every
// path stays native (UTF-16).
//
// Text must NEVER be routed through std::filesystem::path to get UTF-8 out of
// it: path's narrow form on Windows is the *ANSI code page* (Shift-JIS / cp936
// on a Japanese or Chinese machine) and libc++ throws filesystem_error on any
// byte sequence that is not valid ANSI. That is what used to kill this tool
// when a kana was typed into the search box - the kana's UTF-8 is E3 81 82,
// whose first two bytes are a valid Shift-JIS char and whose third is a lone
// lead byte, so the conversion failed inside windowText() and the uncaught
// exception ended the process (0xc0000409 in ucrtbase, via std::terminate).
// ---------------------------------------------------------------------------
std::string toUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, utf8.data(), size, nullptr, nullptr);
    }
    return utf8;
}

// Path as UTF-8 text, for logs and notes. path::string() would narrow through
// the ANSI code page instead, which throws on anything it cannot represent.
std::string pathText(const fs::path& path)
{
    return toUtf8(path.native());
}

// ---------------------------------------------------------------------------
// Song data (musics.json / music-vocals.json / music-levels.json)
// ---------------------------------------------------------------------------
struct VocalVersion
{
    int id = 0;
    std::string type;
    std::string caption;
    std::string asset;
    std::string singers;
};

struct Song
{
    int id = 0;
    std::string title;
    std::string kana;
    std::string lyricist;
    std::string composer;
    std::string arranger;
    std::string jacket; // assetbundleName, e.g. jacket_s_374
    double fillerSec = 0.0;
    std::array<int, 5> levels{0, 0, 0, 0, 0};
    std::vector<VocalVersion> vocals;
};

const char* kDiffNames[5] = {"easy", "normal", "hard", "expert", "master"};

std::vector<Song> gSongs;
std::string gDataError;
// How many rows in musics.json were dropped because their id was already taken.
// Reported on the log so a broken table is visible instead of silently "just
// working" (see loadData).
int gDuplicateIds = 0;

std::string id4(int id)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04d", id);
    return buf;
}

std::string jacketStem(const std::string& assetbundleName, int id)
{
    // Official assetbundle is "jacket_s_374"; unipjsk serves the *3 digit* stem.
    if (assetbundleName.rfind("jacket_s_", 0) == 0) {
        return assetbundleName;
    }
    return "jacket_s_" + std::to_string(id);
}

bool fileExists(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

// Reads one of the JSON tables from next to the exe / repo root.
bool readJson(const std::string& fileName, nlohmann::json& out)
{
    for (const fs::path& candidate : {fs::path(fileName), fs::path("..") / fileName,
             fs::path("../..") / fileName}) {
        std::ifstream file(candidate, std::ios::binary);
        if (!file) {
            continue;
        }
        try {
            out = nlohmann::json::parse(file);
            return true;
        } catch (const std::exception& e) {
            gDataError = std::string(fileName) + ": " + e.what();
            return false;
        }
    }
    gDataError = fileName + " not found (run it from the repo or drop it next to the exe)";
    return false;
}

void loadData()
{
    nlohmann::json musics;
    if (!readJson("musics.json", musics) || !musics.is_array()) {
        return;
    }
    // One id = one row. Every file name the downloader builds comes from the id
    // ("0374_normal.sus"), so a duplicated id would list the same song twice and
    // point two jobs at the same path. The upstream tables have shipped one id
    // twice, so the later copy is dropped here - and counted, so the log says it
    // instead of hiding the problem.
    std::set<int> seenIds;
    for (const auto& row : musics) {
        if (!row.is_object()) {
            continue;
        }
        Song song;
        song.id = row.value("id", 0);
        song.title = row.value("title", std::string{});
        song.kana = row.value("pronunciation", std::string{});
        song.lyricist = row.value("lyricist", std::string{});
        song.composer = row.value("composer", std::string{});
        song.arranger = row.value("arranger", std::string{});
        song.jacket = row.value("assetbundleName", std::string{});
        song.fillerSec = row.value("fillerSec", 0.0);
        if (song.id > 0 && !song.title.empty()) {
            if (!seenIds.insert(song.id).second) {
                ++gDuplicateIds;
                continue;
            }
            gSongs.push_back(std::move(song));
        }
    }

    nlohmann::json vocals;
    if (readJson("music-vocals.json", vocals) && vocals.is_object()) {
        std::map<int, size_t> byId;
        for (size_t i = 0; i < gSongs.size(); ++i) {
            byId[gSongs[i].id] = i;
        }
        for (auto it = vocals.begin(); it != vocals.end(); ++it) {
            const auto found = byId.find(std::atoi(it.key().c_str()));
            if (found == byId.end() || !it.value().is_array()) {
                continue;
            }
            for (const auto& row : it.value()) {
                VocalVersion version;
                version.id = row.value("id", 0);
                version.type = row.value("type", std::string{});
                version.caption = row.value("caption", std::string{});
                version.asset = row.value("asset", std::string{});
                if (row.contains("singers") && row["singers"].is_array()) {
                    for (const auto& singer : row["singers"]) {
                        if (!singer.is_string()) {
                            continue;
                        }
                        if (!version.singers.empty()) {
                            version.singers += "、";
                        }
                        version.singers += singer.get<std::string>();
                    }
                }
                if (!version.asset.empty()) {
                    gSongs[found->second].vocals.push_back(std::move(version));
                }
            }
        }
    }

    nlohmann::json levels;
    if (readJson("music-levels.json", levels) && levels.is_object()) {
        std::map<int, size_t> byId;
        for (size_t i = 0; i < gSongs.size(); ++i) {
            byId[gSongs[i].id] = i;
        }
        for (auto it = levels.begin(); it != levels.end(); ++it) {
            const auto found = byId.find(std::atoi(it.key().c_str()));
            if (found == byId.end() || !it.value().is_array()) {
                continue;
            }
            const auto& arr = it.value();
            for (size_t d = 0; d < arr.size() && d < 5; ++d) {
                if (arr[d].is_number()) {
                    gSongs[found->second].levels[d] = arr[d].get<int>();
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Download jobs
// ---------------------------------------------------------------------------
enum class JobState
{
    Waiting,
    Active,
    Done,
    Failed,
    Skipped,
};

struct Job
{
    std::string url;
    fs::path path;
    JobState state = JobState::Waiting;
    long long bytes = 0;
    long long total = 0;
    std::string note;
};

// Everything worth keeping also goes to chartdl.log: the GUI has no console
// attached when it is started from Explorer, so stdout is a black hole there
// (the game solves the same problem the same way).
std::ofstream& logFile()
{
    static std::ofstream file("chartdl.log", std::ios::out | std::ios::trunc);
    return file;
}

void note(const std::string& text)
{
    std::printf("%s\n", text.c_str());
    std::fflush(stdout);
    logFile() << text << '\n';
    logFile().flush();
}

std::mutex gJobMutex;
std::vector<Job> gJobs;
std::atomic<bool> gCancel{false};
std::atomic<bool> gRunning{false};
std::vector<std::string> gLog;
std::mutex gLogMutex;

void logLine(const std::string& text)
{
    std::lock_guard<std::mutex> lock(gLogMutex);
    gLog.push_back(text);
    note(text);
}

// Files the game looks for, per song. `wantAudio` controls the BGM variants.
fs::path chartPath(const fs::path& dir, int id, const char* diff)
{
    return dir / (id4(id) + "_" + diff + ".sus");
}

fs::path audioPath(const fs::path& dir, const VocalVersion& version, int id)
{
    if (!version.asset.empty()) {
        return dir / (version.asset + ".mp3");
    }
    return dir / (id4(id) + ".mp3");
}

fs::path jacketPath(const fs::path& dir, const Song& song)
{
    return dir / (id4(song.id) + ".png");
}

std::string chartUrl(const Song& song, const char* diff)
{
    return "https://assets.unipjsk.com/startapp/music/music_score/" + id4(song.id) + "_01/" + diff;
}

std::string audioUrl(const VocalVersion& version, int id)
{
    const std::string asset = version.asset.empty() ? id4(id) + "_01" : version.asset;
    return "https://assets.unipjsk.com/ondemand/music/long/" + asset + "/" + asset + ".mp3";
}

std::string jacketUrl(const Song& song)
{
    const std::string stem = jacketStem(song.jacket, song.id);
    return "https://assets.unipjsk.com/startapp/music/jacket/" + stem + "/" + stem + ".png";
}

// Sidecar the game reads next to the chart (<id4>.json).
void writeSidecar(const fs::path& dir, const Song& song, const std::vector<VocalVersion>& vocals)
{
    nlohmann::json doc;
    doc["title"] = song.title;
    doc["artist"] = song.composer.empty() ? song.lyricist : song.composer;
    if (!song.lyricist.empty()) {
        doc["lyricist"] = song.lyricist;
    }
    if (!song.composer.empty()) {
        doc["composer"] = song.composer;
    }
    if (!song.arranger.empty()) {
        doc["arranger"] = song.arranger;
    }
    // The game derives the singers from the chosen version when it has the
    // vocal table; this keeps the file useful on its own too (first version).
    if (!vocals.empty() && !vocals.front().singers.empty()) {
        doc["vocal"] = vocals.front().singers;
    }
    if (song.fillerSec > 0.0) {
        doc["fillerSec"] = song.fillerSec;
    }
    std::ofstream out(dir / (id4(song.id) + ".json"), std::ios::binary);
    if (out) {
        out << doc.dump(2) << std::endl;
    }
}

struct Request
{
    bool diffs[5] = {true, true, true, true, true};
    std::vector<size_t> vocalIndexes; // indexes into Song::vocals
    bool jacket = true;
    bool sidecar = true;
};

// Builds jobs for one song. Existing files become Skipped jobs (shown as such in
// the UI) unless `force`.
void queueSong(const Song& song, const Request& request, const fs::path& dir, bool force)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto add = [&](const std::string& url, const fs::path& path) {
        Job job;
        job.url = url;
        job.path = path;
        if (!force && fileExists(path)) {
            job.state = JobState::Skipped;
            job.note = "already there";
        }
        std::lock_guard<std::mutex> lock(gJobMutex);
        gJobs.push_back(std::move(job));
    };

    for (int d = 0; d < 5; ++d) {
        if (request.diffs[d]) {
            add(chartUrl(song, kDiffNames[d]), chartPath(dir, song.id, kDiffNames[d]));
        }
    }
    for (const size_t index : request.vocalIndexes) {
        if (index < song.vocals.size()) {
            add(audioUrl(song.vocals[index], song.id), audioPath(dir, song.vocals[index], song.id));
        }
    }
    if (request.jacket) {
        add(jacketUrl(song), jacketPath(dir, song));
    }
    if (request.sidecar) {
        writeSidecar(dir, song, [&] {
            std::vector<VocalVersion> picked;
            for (const size_t index : request.vocalIndexes) {
                if (index < song.vocals.size()) {
                    picked.push_back(song.vocals[index]);
                }
            }
            return picked;
        }());
    }
}

void worker()
{
    while (true) {
        size_t index = gJobs.size();
        {
            std::lock_guard<std::mutex> lock(gJobMutex);
            for (size_t i = 0; i < gJobs.size(); ++i) {
                if (gJobs[i].state == JobState::Waiting) {
                    index = i;
                    gJobs[i].state = JobState::Active;
                    break;
                }
            }
        }
        if (index == gJobs.size()) {
            break;
        }
        if (gCancel.load()) {
            std::lock_guard<std::mutex> lock(gJobMutex);
            gJobs[index].state = JobState::Failed;
            gJobs[index].note = "cancelled";
            continue;
        }
        // Copy the addresses: the UI thread can still push_back into gJobs,
        // which would invalidate any reference held here.
        const std::string url = gJobs[index].url;
        const fs::path path = gJobs[index].path;
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        const fs::path partPath = path.native() + L".part";
        std::ofstream out(partPath, std::ios::binary);
        if (!out) {
            std::lock_guard<std::mutex> lock(gJobMutex);
            gJobs[index].state = JobState::Failed;
            gJobs[index].note = "cannot write " + pathText(partPath);
            continue;
        }
        std::string error;
        const bool ok = http::get(url,
            [&](const char* data, size_t len, long long received, long long total) {
                out.write(data, static_cast<std::streamsize>(len));
                std::lock_guard<std::mutex> lock(gJobMutex);
                gJobs[index].bytes = received;
                gJobs[index].total = total;
                return !gCancel.load();
            },
            error);
        out.close();
        {
            std::lock_guard<std::mutex> lock(gJobMutex);
            if (ok && !gCancel.load()) {
                gJobs[index].state = JobState::Done;
            } else {
                gJobs[index].state = JobState::Failed;
                gJobs[index].note = error.empty() ? "download failed" : error;
            }
        }
        if (ok && !gCancel.load()) {
            std::error_code renameEc;
            fs::remove(path, renameEc);
            fs::rename(partPath, path, renameEc);
            logLine("[ok]   " + pathText(path.filename()));
        } else {
            std::error_code removeEc;
            fs::remove(partPath, removeEc);
            logLine("[fail] " + pathText(path.filename()) + "  " + error);
        }
    }
    gRunning.store(false);
    logLine("[done] queue finished");
}

void startWorker()
{
    if (gRunning.exchange(true)) {
        return;
    }
    gCancel.store(false);
    std::thread(worker).detach();
}

std::string humanBytes(long long bytes)
{
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1024 * 1024) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
void printUsage()
{
    std::printf(
        "chartdl - CppSekai chart downloader\n"
        "\n"
        "  chartdl.exe                          GUI\n"
        "  chartdl.exe --list [filter]          print the song table\n"
        "  chartdl.exe --download <ids>         ids: 374 or 374,75,127\n"
        "              [--diffs all|easy,normal,hard,expert,master]\n"
        "              [--vocals all|0,1,2]      indexes into the song's version list\n"
        "              [--out <dir>]             default: ..\\charts\n"
        "              [--no-jacket] [--no-sidecar] [--force]\n"
        "  chartdl.exe --screenshot <png> [--screenshot-time <sec>]\n"
        "              [--dpi <96|120|144|192>]   force the layout scale (default:\n"
        "                                          the system DPI)\n"
        "\n"
        "Source: assets.unipjsk.com (charts, BGM per vocal version, jackets).\n");
}

int runJobQueue(std::string& error)
{
    if (gJobs.empty()) {
        error = "nothing to download";
        return 1;
    }
    gRunning.store(true);
    std::thread(worker).join();
    size_t done = 0;
    size_t failed = 0;
    size_t skipped = 0;
    for (const Job& job : gJobs) {
        done += job.state == JobState::Done ? 1 : 0;
        failed += job.state == JobState::Failed ? 1 : 0;
        skipped += job.state == JobState::Skipped ? 1 : 0;
    }
    std::printf("downloaded %zu, skipped %zu, failed %zu\n", done, skipped, failed);
    return failed == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Win32 UI
//
// Plain common controls on purpose: a ListView with checkboxes, an edit box, a
// progress bar and a log. Nothing is skinned, nothing is drawn by hand, and the
// text is Chinese - it is a small utility, not the game.
// ---------------------------------------------------------------------------
namespace
{
    // widen() lives in namespace http above.
    using http::widen;

    constexpr int kIdSearch = 1000;
    constexpr int kIdOutDir = 1001;
    constexpr int kIdBrowse = 1002;
    constexpr int kIdOpenDir = 1003;
    constexpr int kIdQueue = 1004;
    constexpr int kIdCancel = 1005;
    constexpr int kIdCheckAll = 1006;
    constexpr int kIdList = 1007;
    constexpr int kIdProgress = 1008;
    constexpr int kIdStatus = 1009;
    constexpr int kIdLog = 1010;
    constexpr int kIdDetailGroup = 1011;
    constexpr int kIdDetailTitle = 1012;
    constexpr int kIdJacket = 1013;
    constexpr int kIdSidecar = 1014;
    constexpr int kIdDiffBase = 1100;  // 1100..1104 = EASY..MASTER
    constexpr int kIdVocalBase = 1120; // 1120.. = one per vocal version

    HWND gList = nullptr;
    HWND gOutDirLabel = nullptr;
    HWND gSearchLabel = nullptr;
    HWND gSearch = nullptr;
    HWND gOutDirEdit = nullptr;
    HWND gQueueButton = nullptr;
    HWND gCancelButton = nullptr;
    HWND gProgress = nullptr;
    HWND gStatus = nullptr;
    HWND gLogList = nullptr;
    HWND gDetailTitle = nullptr;
    HWND gJacketCheck = nullptr;
    HWND gSidecarCheck = nullptr;
    HWND gDiffChecks[5] = {};
    std::vector<HWND> gVocalChecks;
    HFONT gFont = nullptr;

    // -----------------------------------------------------------------------
    // DPI. chartdl declares DPI awareness (see chartdl.manifest), which means
    // Windows no longer renders the window at 96 DPI and stretches the result -
    // it hands us a real-pixel client area instead, and everything here has to
    // lay itself out in those pixels. Every hard-coded size in the layout goes
    // through dp(), so the window keeps its proportions at any scale.
    // `--dpi <n>` overrides the value (headless checks: it is the only way to
    // see a 150% layout on a 100% machine).
    int gDpi = 96;
    int gDpiOverride = 0;

    int dp(int value)
    {
        return MulDiv(value, gDpi, 96);
    }

    // The system DPI - which is exactly what the manifest's "true" (system aware)
    // promises. A per-monitor process would have to ask the window instead.
    int systemDpi()
    {
        HDC screen = GetDC(nullptr);
        if (screen == nullptr) {
            return 96;
        }
        const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(nullptr, screen);
        return dpi > 0 ? dpi : 96;
    }

    // SetWindowPos with every argument passed through dp().
    void place(HWND child, int x, int y, int w, int h)
    {
        SetWindowPos(child, nullptr, dp(x), dp(y), dp(w), dp(h), SWP_NOZORDER);
    }

    // Asks GDI for a font, but CreateFontW never fails - it substitutes silently
    // when the family is missing. So ask the font we got back what it actually is
    // (GetTextFaceW) and keep looking when it is not the one we asked for.
    HFONT createUiFont(int pointSize)
    {
        static const wchar_t* kFaces[] = {
            L"Microsoft YaHei UI", // Win8+, the "msyh" family (modern default)
            L"Microsoft YaHei",    // Win7 name for the same face
            L"Segoe UI",           // latin-only fallback, still much newer than MS Shell Dlg
            L"Tahoma",
        };
        HDC screen = GetDC(nullptr);
        if (screen == nullptr) {
            return reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        const int height = -MulDiv(pointSize, gDpi, 72);
        for (const wchar_t* face : kFaces) {
            HFONT font = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
            if (font == nullptr) {
                continue;
            }
            HGDIOBJ previous = SelectObject(screen, font);
            wchar_t actual[LF_FACESIZE] = {};
            GetTextFaceW(screen, LF_FACESIZE, actual);
            SelectObject(screen, previous);
            if (_wcsicmp(actual, face) == 0) {
                ReleaseDC(nullptr, screen);
                return font;
            }
            DeleteObject(font); // substituted: not the family we asked for
        }
        ReleaseDC(nullptr, screen);
        return reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    // -----------------------------------------------------------------------
    // List columns. The five difficulties get one column each (instead of the old
    // packed "E24 N30 H32 X36 M38" cell) so the header can be clicked to sort by
    // level - which is the whole point of splitting them.
    // -----------------------------------------------------------------------
    constexpr int kColId = 0;
    constexpr int kColTitle = 1;
    constexpr int kColKana = 2;
    constexpr int kColDiffFirst = 3; // EASY .. MASTER, in kDiffNames order
    constexpr int kColVersions = kColDiffFirst + 5;
    constexpr int kColumnCount = kColVersions + 1;

    const wchar_t* const kColumnTitles[kColumnCount] = {
        L"ID", L"曲名", L"读音", L"EASY", L"NORMAL", L"HARD", L"EXPERT", L"MASTER", L"演唱版本"};
    // Widths in 96-DPI units; dp() scales them on the way into the header.
    const int kColumnWidths[kColumnCount] = {52, 230, 150, 58, 68, 56, 66, 66, 84};

    // Click a header to sort by that column; click it again to flip. Starts on
    // the id, which is the order of the upstream table.
    int gSortColumn = kColId;
    bool gSortAscending = true;

    std::vector<int> gRowSong;   // list row -> gSongs index (after filtering)
    int gDetailSong = -1;        // song the right panel describes
    std::size_t gLogShown = 0;   // log lines already appended to the listbox
    double gGuiStartSec = 0.0;   // for --screenshot-time

    // Output directory in the native Windows form (UTF-16). Keeping it narrow
    // would mean converting it back with fs::path(), which uses the ANSI code
    // page and mangles (or rejects) any non-ASCII character.
    std::wstring gOutDir;

    // Raw text of a control (UTF-16), no conversion at all.
    std::wstring windowTextW(HWND control)
    {
        const int length = GetWindowTextLengthW(control);
        std::wstring buffer(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
        GetWindowTextW(control, buffer.data(), length + 1);
        buffer.resize(static_cast<std::size_t>(std::max(0, length)));
        return buffer;
    }

    // Same, as UTF-8: what the user typed, compared against the UTF-8 song data.
    std::string windowText(HWND control)
    {
        return toUtf8(windowTextW(control));
    }

    void setStatus(const std::string& text)
    {
        SetWindowTextW(gStatus, widen(text).c_str());
    }

    void appendLog(const std::string& text)
    {
        if (gLogList == nullptr) {
            return;
        }
        std::wstring wide = widen(text);
        // Drop the leading header when the engine sends one.
        for (wchar_t& ch : wide) {
            if (ch == L'\r' || ch == L'\n') {
                ch = L' ';
            }
        }
        const int index = static_cast<int>(
            SendMessageW(gLogList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str())));
        SendMessageW(gLogList, LB_SETTOPINDEX, static_cast<WPARAM>(index), 0);
    }

    bool matchesFilter(const Song& song, const std::string& filter)
    {
        if (filter.empty()) {
            return true;
        }
        if (std::to_string(song.id).find(filter) != std::string::npos) {
            return true;
        }
        if (song.title.find(filter) != std::string::npos) {
            return true;
        }
        return song.kana.find(filter) != std::string::npos;
    }

    // Fills the list from gSongs, keeping the current search filter and the
    // check state of the songs that stay visible.
    // Marks the column the list is sorted by with the header's own sort arrow
    // (HDF_SORTUP/DOWN, needs comctl32 v6 - chartdl.manifest asks for it). No text
    // marker: the arrow is the native affordance and keeps the titles readable.
    //
    // Written straight to the header control: the ListView keeps its own copy of
    // the column text and (under v6) does not push a later LVM_SETCOLUMN through
    // to the header, so going that way leaves the visible header unchanged.
    void updateHeaderSortMarks()
    {
        HWND header = ListView_GetHeader(gList);
        if (header == nullptr) {
            return;
        }
        for (int i = 0; i < kColumnCount; ++i) {
            HDITEMW item{};
            item.mask = HDI_FORMAT;
            if (Header_GetItem(header, i, &item) == FALSE) {
                continue;
            }
            item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
            if (i == gSortColumn) {
                item.fmt |= gSortAscending ? HDF_SORTUP : HDF_SORTDOWN;
            }
            Header_SetItem(header, i, &item);
        }
    }

    void rebuildList(const std::string& filter)
    {
        // Remember what was ticked.
        std::map<int, bool> checked;
        const int rows = static_cast<int>(gRowSong.size());
        for (int row = 0; row < rows; ++row) {
            const bool isChecked =
                ListView_GetCheckState(gList, row) != 0;
            checked[gRowSong[static_cast<std::size_t>(row)]] = isChecked;
        }

        SendMessageW(gList, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(gList);
        gRowSong.clear();

        // Which songs to show, in the order the header asks for.
        std::vector<int> order;
        order.reserve(gSongs.size());
        {
            // Second guard on top of loadData's: whatever ended up in gSongs, one
            // id gets exactly one row here.
            std::set<int> listedIds;
            for (std::size_t i = 0; i < gSongs.size(); ++i) {
                const Song& song = gSongs[i];
                if (!matchesFilter(song, filter)) {
                    continue;
                }
                if (!listedIds.insert(song.id).second) {
                    continue;
                }
                order.push_back(static_cast<int>(i));
            }
        }
        const auto& songs = gSongs;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const Song& lhs = songs[static_cast<std::size_t>(a)];
            const Song& rhs = songs[static_cast<std::size_t>(b)];
            int cmp = 0;
            switch (gSortColumn) {
                case kColTitle:
                    cmp = lhs.title.compare(rhs.title);
                    break;
                case kColKana:
                    cmp = lhs.kana.compare(rhs.kana);
                    break;
                case kColVersions:
                    cmp = static_cast<int>(lhs.vocals.size()) - static_cast<int>(rhs.vocals.size());
                    break;
                case kColId:
                    cmp = lhs.id - rhs.id;
                    break;
                default: {
                    // The five difficulty columns; a missing level (-1 / 0) sorts
                    // to the bottom either way, which keeps the songs that have
                    // this difficulty on top.
                    const int d = std::clamp(gSortColumn - kColDiffFirst, 0, 4);
                    const int lv = lhs.levels[static_cast<std::size_t>(d)];
                    const int rv = rhs.levels[static_cast<std::size_t>(d)];
                    const bool lvSet = lv > 0;
                    const bool rvSet = rv > 0;
                    if (lvSet != rvSet) {
                        cmp = lvSet ? -1 : 1;
                    } else {
                        cmp = lv - rv;
                    }
                    break;
                }
            }
            if (cmp == 0) {
                cmp = lhs.id - rhs.id; // stable tiebreak: the upstream order
            }
            return gSortAscending ? cmp < 0 : cmp > 0;
        });

        for (const int index : order) {
            const Song& song = gSongs[static_cast<std::size_t>(index)];
            wchar_t id[16];
            std::swprintf(id, 16, L"%04d", song.id);
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(gRowSong.size());
            item.pszText = id;
            const int row = static_cast<int>(SendMessageW(gList, LVM_INSERTITEMW, 0,
                reinterpret_cast<LPARAM>(&item)));
            auto setColumn = [&](int column, const std::wstring& text) {
                ListView_SetItemText(gList, row, column, const_cast<wchar_t*>(text.c_str()));
            };
            setColumn(kColTitle, widen(song.title));
            setColumn(kColKana, widen(song.kana));
            for (int d = 0; d < 5; ++d) {
                const int level = song.levels[static_cast<std::size_t>(d)];
                setColumn(kColDiffFirst + d, widen(level > 0 ? std::to_string(level) : std::string("-")));
            }
            setColumn(kColVersions, widen(std::to_string(song.vocals.size()) + " 版本"));
            const auto state = checked.find(index);
            ListView_SetCheckState(gList, row, state != checked.end() && state->second ? TRUE : FALSE);
            gRowSong.push_back(index);
        }
        SendMessageW(gList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(gList, nullptr, TRUE);
        // The header is a child window of the list and does not come back on its
        // own after WM_SETREDRAW - without this the sort marker stays invisible
        // until something else happens to repaint it.
        if (HWND header = ListView_GetHeader(gList)) {
            RedrawWindow(header, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
        }
    }

    void updateDetailPanel(int songIndex);

    LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message) {
            case WM_CREATE: {
                gDpi = gDpiOverride > 0 ? gDpiOverride : systemDpi();
                // "Microsoft YaHei UI" (msyh) when the system has it, and 10pt
                // instead of the 9pt MS Shell Dlg that DEFAULT_GUI_FONT hands out.
                gFont = createUiFont(10);
                auto create = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
                    HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
                    return control;
                };

                gOutDirLabel = create(L"STATIC", L"输出目录", SS_LEFT, -1);
                gOutDirEdit = create(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kIdOutDir);
                create(L"BUTTON", L"浏览…", BS_PUSHBUTTON, kIdBrowse);
                create(L"BUTTON", L"打开目录", BS_PUSHBUTTON, kIdOpenDir);

                gSearchLabel = create(L"STATIC", L"搜索", SS_LEFT, -1);
                gSearch = create(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kIdSearch);
                gQueueButton = create(L"BUTTON", L"下载勾选的歌曲", BS_PUSHBUTTON | BS_DEFPUSHBUTTON, kIdQueue);
                create(L"BUTTON", L"全选 / 全不选", BS_PUSHBUTTON, kIdCheckAll);
                gCancelButton = create(L"BUTTON", L"取消", BS_PUSHBUTTON, kIdCancel);

                gList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdList)), nullptr, nullptr);
                SendMessageW(gList, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
                ListView_SetExtendedListViewStyle(gList,
                    LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
                for (int i = 0; i < kColumnCount; ++i) {
                    LVCOLUMNW column{};
                    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                    column.pszText = const_cast<wchar_t*>(kColumnTitles[i]);
                    column.cx = dp(kColumnWidths[i]);
                    column.iSubItem = i;
                    ListView_InsertColumn(gList, i, &column);
                }
                updateHeaderSortMarks();

                create(L"BUTTON", L"下载内容", BS_GROUPBOX, kIdDetailGroup);
                gDetailTitle = create(L"STATIC", L"（在左边选一首歌）", SS_LEFT, kIdDetailTitle);
                for (int d = 0; d < 5; ++d) {
                    gDiffChecks[d] =
                        create(L"BUTTON", widen(kDiffNames[d]).c_str(), BS_AUTOCHECKBOX, kIdDiffBase + d);
                    SendMessageW(gDiffChecks[d], BM_SETCHECK, BST_CHECKED, 0);
                }
                gJacketCheck = create(L"BUTTON", L"曲绘", BS_AUTOCHECKBOX, kIdJacket);
                gSidecarCheck = create(L"BUTTON", L"元数据 (sidecar json)", BS_AUTOCHECKBOX, kIdSidecar);
                SendMessageW(gJacketCheck, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(gSidecarCheck, BM_SETCHECK, BST_CHECKED, 0);

                gProgress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdProgress)), nullptr, nullptr);
                SendMessageW(gProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
                gStatus = create(L"STATIC", L"就绪", SS_LEFT, kIdStatus);
                gLogList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_DISABLENOSCROLL, 0, 0, 10, 10,
                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLog)), nullptr, nullptr);
                SendMessageW(gLogList, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);

                SetWindowTextW(gOutDirEdit, gOutDir.c_str());
                SetTimer(hwnd, 1, 150, nullptr);
                return 0;
            }
            case WM_SIZE: {
                const int width = LOWORD(lParam); // real pixels already
                const int height = HIWORD(lParam);
                // The numbers below are 96-DPI units; dp() turns them into real
                // pixels, and the widths that mix in the client size use
                // SetWindowPos directly (that size is not a design unit).
                const int margin = 10;
                const int panelWidth = 330;
                const int bottomHeight = 188;
                const int listWidth = width - dp(margin) * 3 - dp(panelWidth);
                const int listHeight = height - dp(76) - dp(bottomHeight) - dp(margin);
                const int panelX = dp(margin) * 2 + listWidth;

                SetWindowPos(GetDlgItem(hwnd, kIdDetailGroup), nullptr, panelX, dp(68), listWidth,
                    listHeight + dp(4), SWP_NOZORDER);
                SetWindowPos(gList, nullptr, dp(margin), dp(68), listWidth, listHeight + dp(4), SWP_NOZORDER);

                place(gOutDirLabel, margin, 15, 62, 20);
                SetWindowPos(gOutDirEdit, nullptr, dp(76), dp(12), width - dp(76) - dp(220), dp(24),
                    SWP_NOZORDER);
                SetWindowPos(GetDlgItem(hwnd, kIdBrowse), nullptr, width - dp(208), dp(12), dp(90), dp(24),
                    SWP_NOZORDER);
                SetWindowPos(GetDlgItem(hwnd, kIdOpenDir), nullptr, width - dp(112), dp(12), dp(102), dp(24),
                    SWP_NOZORDER);
                place(gSearchLabel, margin, 45, 62, 20);
                place(gSearch, 76, 42, 240, 24);
                place(gQueueButton, 330, 42, 150, 24);
                place(GetDlgItem(hwnd, kIdCheckAll), 488, 42, 120, 24);
                place(gCancelButton, 616, 42, 80, 24);

                const int progressY = height - dp(bottomHeight) + dp(4);
                SetWindowPos(gProgress, nullptr, dp(margin), progressY, width - dp(margin) * 2, dp(20),
                    SWP_NOZORDER);
                SetWindowPos(gStatus, nullptr, dp(margin), progressY + dp(24), width - dp(margin) * 2, dp(18),
                    SWP_NOZORDER);
                SetWindowPos(gLogList, nullptr, dp(margin), progressY + dp(46), width - dp(margin) * 2,
                    dp(bottomHeight) - dp(56), SWP_NOZORDER);

                SetWindowPos(gDetailTitle, nullptr, panelX + dp(14), dp(92), dp(panelWidth) - dp(28), dp(18),
                    SWP_NOZORDER);
                if (gDetailSong >= 0) {
                    updateDetailPanel(gDetailSong);
                }
                return 0;
            }
            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                info->ptMinTrackSize = {dp(860), dp(560)};
                return 0;
            }
            case WM_COMMAND: {
                const int id = LOWORD(wParam);
                if (id == kIdBrowse) {
                    BROWSEINFOW browse{};
                    browse.hwndOwner = hwnd;
                    browse.lpszTitle = L"选择谱面输出目录";
                    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                    LPITEMIDLIST item = SHBrowseForFolderW(&browse);
                    if (item != nullptr) {
                        wchar_t path[MAX_PATH] = {};
                        if (SHGetPathFromIDListW(item, path)) {
                            gOutDir.assign(path);
                            SetWindowTextW(gOutDirEdit, path);
                        }
                        CoTaskMemFree(item);
                    }
                    return 0;
                }
                if (id == kIdOpenDir) {
                    ShellExecuteW(hwnd, L"open", gOutDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                if (id == kIdCheckAll) {
                    const bool anyUnchecked = [&] {
                        const int rows = ListView_GetItemCount(gList);
                        for (int row = 0; row < rows; ++row) {
                            if (ListView_GetCheckState(gList, row) == 0) {
                                return true;
                            }
                        }
                        return false;
                    }();
                    const int rows = ListView_GetItemCount(gList);
                    SendMessageW(gList, WM_SETREDRAW, FALSE, 0);
                    for (int row = 0; row < rows; ++row) {
                        ListView_SetCheckState(gList, row, anyUnchecked ? TRUE : FALSE);
                    }
                    SendMessageW(gList, WM_SETREDRAW, TRUE, 0);
                    InvalidateRect(gList, nullptr, TRUE);
                    return 0;
                }
                if (id == kIdCancel) {
                    gCancel.store(true);
                    setStatus("正在取消…");
                    return 0;
                }
                if (id == kIdQueue) {
                    if (gRunning.load()) {
                        return 0;
                    }
                    gOutDir = windowTextW(gOutDirEdit);
                    Request request;
                    for (int d = 0; d < 5; ++d) {
                        request.diffs[d] =
                            SendMessageW(gDiffChecks[d], BM_GETCHECK, 0, 0) == BST_CHECKED;
                    }
                    request.jacket = SendMessageW(gJacketCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    request.sidecar = SendMessageW(gSidecarCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    for (std::size_t v = 0; v < gVocalChecks.size(); ++v) {
                        if (SendMessageW(gVocalChecks[v], BM_GETCHECK, 0, 0) == BST_CHECKED) {
                            request.vocalIndexes.push_back(v);
                        }
                    }

                    // Only the detail panel's vocals apply to the detail song;
                    // for every other ticked row every version is taken.
                    bool anyDiff = false;
                    for (int d = 0; d < 5; ++d) {
                        anyDiff = anyDiff || request.diffs[d];
                    }
                    if (!anyDiff && request.vocalIndexes.empty() && !request.jacket && !request.sidecar) {
                        MessageBoxW(hwnd, L"右边至少要勾一样东西（难度 / 演唱版本 / 曲绘 / 元数据）。",
                            L"CppSekai 谱面下载器", MB_OK | MB_ICONINFORMATION);
                        return 0;
                    }

                    const int rows = ListView_GetItemCount(gList);
                    int queued = 0;
                    for (int row = 0; row < rows; ++row) {
                        if (ListView_GetCheckState(gList, row) == 0) {
                            continue;
                        }
                        const int songIndex = gRowSong[static_cast<std::size_t>(row)];
                        const Song& song = gSongs[static_cast<std::size_t>(songIndex)];
                        Request songRequest = request;
                        if (songIndex != gDetailSong || song.vocals.empty()) {
                            songRequest.vocalIndexes.clear();
                            for (std::size_t v = 0; v < song.vocals.size(); ++v) {
                                songRequest.vocalIndexes.push_back(v);
                            }
                        }
                        queueSong(song, songRequest, fs::path(gOutDir), false);
                        ++queued;
                    }
                    if (queued == 0) {
                        MessageBoxW(hwnd, L"左边没有勾选任何歌曲。", L"CppSekai 谱面下载器",
                            MB_OK | MB_ICONINFORMATION);
                        return 0;
                    }
                    gCancel.store(false);
                    gRunning.store(true);
                    std::thread(worker).detach();
                    setStatus("开始下载…");
                    appendLog("=== " + std::to_string(queued) + " 首歌曲，共 "
                        + std::to_string(gJobs.size()) + " 个文件 ===");
                    return 0;
                }
                if (id == kIdSearch && HIWORD(wParam) == EN_CHANGE) {
                    rebuildList(windowText(gSearch));
                    return 0;
                }
                return 0;
            }
            case WM_NOTIFY: {
                auto* header = reinterpret_cast<NMHDR*>(lParam);
                if (header->idFrom == kIdList && header->code == LVN_COLUMNCLICK) {
                    // Click a header to sort by it, click it again to flip the
                    // direction. The list is rebuilt from the new order, so the
                    // ticks (which are kept per song id) survive.
                    const int column = reinterpret_cast<NMLISTVIEW*>(lParam)->iSubItem;
                    if (column == gSortColumn) {
                        gSortAscending = !gSortAscending;
                    } else {
                        gSortColumn = column;
                        gSortAscending = true;
                    }
                    updateHeaderSortMarks();
                    rebuildList(windowText(gSearch));
                    return 0;
                }
                if (header->idFrom == kIdList && header->code == LVN_ITEMCHANGED) {
                    const int row = ListView_GetNextItem(gList, -1, LVNI_SELECTED);
                    const int songIndex =
                        row >= 0 && row < static_cast<int>(gRowSong.size())
                        ? gRowSong[static_cast<std::size_t>(row)]
                        : -1;
                    if (songIndex != gDetailSong) {
                        updateDetailPanel(songIndex);
                    }
                }
                return 0;
            }
            case WM_TIMER: {
                // Progress + log refresh (the worker thread only touches its own
                // state under gJobMutex).
                long long received = 0;
                long long total = 0;
                std::size_t finished = 0;
                {
                    std::lock_guard<std::mutex> lock(gJobMutex);
                    for (const Job& job : gJobs) {
                        if (job.state == JobState::Done || job.state == JobState::Failed
                            || job.state == JobState::Skipped) {
                            ++finished;
                            received += std::max(job.bytes, job.total);
                            total += std::max(job.bytes, job.total);
                            continue;
                        }
                        received += job.bytes;
                        total += job.total;
                    }
                }
                const int permille = total > 0 ? static_cast<int>(received * 1000 / total) : 0;
                SendMessageW(gProgress, PBM_SETPOS, static_cast<WPARAM>(std::max(0, permille)), 0);
                if (gRunning.load() || finished < gJobs.size()) {
                    setStatus("下载中 " + std::to_string(finished) + "/" + std::to_string(gJobs.size())
                        + " 个文件  " + humanBytes(received) + " / " + humanBytes(total));
                } else if (!gJobs.empty()) {
                    std::size_t failed = 0;
                    for (const Job& job : gJobs) {
                        failed += job.state == JobState::Failed ? 1 : 0;
                    }
                    setStatus(failed == 0 ? "全部完成" : ("完成，失败 " + std::to_string(failed) + " 个"));
                }

                std::vector<std::string> lines;
                {
                    std::lock_guard<std::mutex> lock(gLogMutex);
                    for (std::size_t i = gLogShown; i < gLog.size(); ++i) {
                        lines.push_back(gLog[i]);
                    }
                    gLogShown = gLog.size();
                }
                for (const std::string& line : lines) {
                    appendLog(line);
                }
                return 0;
            }
            case WM_DESTROY:
                if (gFont != nullptr && gFont != GetStockObject(DEFAULT_GUI_FONT)) {
                    DeleteObject(gFont); // ours, not a stock object
                    gFont = nullptr;
                }
                KillTimer(hwnd, 1);
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    // Right-hand panel: which difficulty / vocal version / extras to fetch for
    // the song currently selected in the list.
    void updateDetailPanel(int songIndex)
    {
        gDetailSong = songIndex;
        for (HWND check : gVocalChecks) {
            DestroyWindow(check);
        }
        gVocalChecks.clear();
        if (songIndex < 0 || songIndex >= static_cast<int>(gSongs.size())) {
            SetWindowTextW(gDetailTitle, L"（在左边选一首歌）");
            return;
        }
        const Song& song = gSongs[static_cast<std::size_t>(songIndex)];
        const std::wstring title = L"#" + std::to_wstring(song.id) + L"  " + widen(song.title);
        SetWindowTextW(gDetailTitle, title.c_str());

        RECT panel{};
        GetWindowRect(GetDlgItem(GetParent(gList), kIdDetailGroup), &panel);
        MapWindowPoints(HWND_DESKTOP, GetParent(gList), reinterpret_cast<POINT*>(&panel), 2);
        // `panel` is real pixels, the offsets are 96-DPI units.
        const int contentWidth = panel.right - panel.left - dp(28);
        const int baseX = panel.left + dp(14);
        int y = panel.top + dp(40);
        const int rowHeight = dp(24);

        SetWindowPos(gDetailTitle, nullptr, baseX, panel.top + dp(18), contentWidth, dp(18), SWP_NOZORDER);

        for (int d = 0; d < 5; ++d) {
            const int level = song.levels[d];
            const bool available = level > 0;
            std::wstring label = widen(kDiffNames[d]);
            if (available) {
                label += L"  Lv." + std::to_wstring(level);
            } else {
                label += L"  （无）";
            }
            SetWindowTextW(gDiffChecks[d], label.c_str());
            EnableWindow(gDiffChecks[d], available ? TRUE : FALSE);
            SendMessageW(gDiffChecks[d], BM_SETCHECK, available ? BST_CHECKED : BST_UNCHECKED, 0);
            SetWindowPos(gDiffChecks[d], nullptr, baseX, y, contentWidth, rowHeight, SWP_NOZORDER);
            y += rowHeight;
        }
        y += dp(8);
        for (std::size_t v = 0; v < song.vocals.size(); ++v) {
            const VocalVersion& version = song.vocals[v];
            std::wstring label = widen(version.caption.empty() ? version.type : version.caption);
            if (!version.singers.empty()) {
                label += L"  " + widen(version.singers);
            }
            HWND check = CreateWindowExW(0, L"BUTTON", label.c_str(),
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, baseX, y, contentWidth, rowHeight,
                GetParent(gList), reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdVocalBase + v)), nullptr, nullptr);
            SendMessageW(check, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
            SendMessageW(check, BM_SETCHECK, BST_CHECKED, 0);
            gVocalChecks.push_back(check);
            y += rowHeight;
        }
        if (song.vocals.empty()) {
            HWND none = CreateWindowExW(0, L"STATIC", L"（没有演唱版本数据）", WS_CHILD | WS_VISIBLE,
                baseX, y, contentWidth, rowHeight, GetParent(gList), nullptr, nullptr, nullptr);
            SendMessageW(none, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
            y += rowHeight;
        }
        y += dp(8);
        SetWindowPos(gJacketCheck, nullptr, baseX, y, contentWidth, rowHeight, SWP_NOZORDER);
        y += rowHeight;
        SetWindowPos(gSidecarCheck, nullptr, baseX, y, contentWidth, rowHeight, SWP_NOZORDER);
    }

    // --screenshot: grab the window with GDI (no screen capture, no input) so
    // the UI can be checked from a script.
    bool saveWindowPng(HWND hwnd, const std::string& path)
    {
        // Full repaint first: BitBlt off the window DC reads what is on screen, so
        // a child control that has been invalidated but not yet painted (the list
        // header after a sort, for one) would be captured stale - which is exactly
        // what a screenshot check must not do.
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const int width = rect.right;
        const int height = rect.bottom;
        if (width <= 0 || height <= 0) {
            return false;
        }
        HDC windowDc = GetDC(hwnd);
        HDC memDc = CreateCompatibleDC(windowDc);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(memDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(memDc, bitmap);
        // PW_CLIENTONLY matters: the bitmap is client-sized, and without it
        // PrintWindow renders the *whole* window (title bar included) into it, so
        // the top ~30 px came out as the caption and the bottom of the client area
        // was cut off - which also shifted every y coordinate a screenshot check
        // measures against.
        PrintWindow(hwnd, memDc, PW_CLIENTONLY | PW_RENDERFULLCONTENT);

        std::vector<unsigned char> rgba(static_cast<std::size_t>(width) * height * 4);
        const auto* source = static_cast<const unsigned char*>(bits);
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i + 0] = source[i + 2];
            rgba[i + 1] = source[i + 1];
            rgba[i + 2] = source[i + 0];
            rgba[i + 3] = 255;
        }
        SelectObject(memDc, old);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        ReleaseDC(hwnd, windowDc);

        return stbi_write_png(path.c_str(), width, height, 4, rgba.data(), width * 4) != 0;
    }

    int runGui(fs::path outDir, const std::string& screenshotPath, double screenshotTime)
    {
        gOutDir = outDir.native();
        // DPI before anything else: the window size below and every layout number
        // that follows are scaled by it (dp()). WM_CREATE re-asserts the value for
        // the case where the window is created some other way.
        gDpi = gDpiOverride > 0 ? gDpiOverride : systemDpi();
        loadData();
        if (!gDataError.empty()) {
            appendLog(std::string("[data] ") + gDataError);
        }
        if (gDuplicateIds > 0) {
            appendLog("[data] dropped " + std::to_string(gDuplicateIds) + " duplicate song id(s)");
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        windowClass.lpszClassName = L"CppSekaiChartDl";
        // The icon embedded by app.rc (id 1); falls back to the stock one.
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        windowClass.hIcon = reinterpret_cast<HICON>(
            LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
        windowClass.hIconSm = reinterpret_cast<HICON>(
            LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_SHARED));
        if (windowClass.hIcon == nullptr) {
            windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }
        RegisterClassExW(&windowClass);

        HWND hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"CppSekai 谱面下载器",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, dp(1180), dp(760), nullptr, nullptr,
            windowClass.hInstance, nullptr);
        if (hwnd == nullptr) {
            return 1;
        }
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        {
            // One line so a DPI problem can be diagnosed without guesswork (and so
            // the headless check can assert it). `screen` is SM_CXSCREEN: for a
            // DPI-aware process that is the real pixel width of the primary
            // display, while an unaware process is handed a virtualised (smaller)
            // value - so the pair tells you both the scale and whether the
            // manifest took effect.
            RECT client{};
            GetClientRect(hwnd, &client);
            // Which face createUiFont() actually ended up with - the substitution
            // check inside it is silent, so report the winner.
            std::wstring face = L"?";
            if (HDC dc = GetDC(hwnd)) {
                HGDIOBJ previous = SelectObject(dc, gFont);
                wchar_t name[LF_FACESIZE] = {};
                GetTextFaceW(dc, LF_FACESIZE, name);
                SelectObject(dc, previous);
                ReleaseDC(hwnd, dc);
                face = name;
            }
            note("[ui] dpi=" + std::to_string(gDpi) + " client=" + std::to_string(client.right) + "x"
                + std::to_string(client.bottom) + " screen=" + std::to_string(GetSystemMetrics(SM_CXSCREEN))
                + "x" + std::to_string(GetSystemMetrics(SM_CYSCREEN)) + " font=" + toUtf8(face));
        }
        rebuildList("");

        const ULONGLONG start = GetTickCount64();
        MSG message{};
        while (true) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    return 0;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!screenshotPath.empty()
                && static_cast<double>(GetTickCount64() - start) / 1000.0 >= screenshotTime) {
                saveWindowPng(hwnd, screenshotPath);
                break;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 30, QS_ALLINPUT);
        }
        return 0;
    }
} // namespace

int main(int argc, char** argv)
{
    // Built as a GUI subsystem app (no console flash when double-clicked), so a
    // command line run has to borrow the shell's console back for its output.
    if (argc > 1) {
        // Only when nothing is connected already: a shell that piped our output
        // (git bash, cmd) has a valid handle and must keep it.
        const HANDLE standardOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if ((standardOut == nullptr || standardOut == INVALID_HANDLE_VALUE)
            && AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* dummy = nullptr;
            freopen_s(&dummy, "CONOUT$", "w", stdout);
            freopen_s(&dummy, "CONOUT$", "w", stderr);
        }
    }
    fs::path outDir = "..\\charts";
    std::string listFilter;
    std::string downloadIds;
    std::string diffArg = "all";
    std::string vocalArg = "all";
    bool wantList = false;
    std::string screenshotPath;
    double screenshotTime = 1.0;
    bool force = false;
    bool wantJacket = true;
    bool wantSidecar = true;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& out) {
            if (i + 1 < argc) {
                out = argv[++i];
            }
        };
        if (arg == "--list") {
            wantList = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                listFilter = argv[++i];
            }
        } else if (arg == "--download") {
            next(downloadIds);
        } else if (arg == "--diffs") {
            next(diffArg);
        } else if (arg == "--vocals") {
            next(vocalArg);
        } else if (arg == "--out") {
            std::string dir;
            next(dir);
            if (!dir.empty()) {
                outDir = dir;
            }
        } else if (arg == "--screenshot") {
            next(screenshotPath);
        } else if (arg == "--dpi") {
            // Force the layout scale instead of reading the system DPI. Mainly a
            // testing hook: it is the only way to look at a 150% / 200% layout on
            // a 100% machine (see dp()).
            std::string value;
            next(value);
            gDpiOverride = std::atoi(value.c_str());
        } else if (arg == "--screenshot-time") {
            std::string value;
            next(value);
            if (!value.empty()) {
                screenshotTime = std::atof(value.c_str());
            }
        } else if (arg == "--force") {
            force = true;
        } else if (arg == "--no-jacket") {
            wantJacket = false;
        } else if (arg == "--no-sidecar") {
            wantSidecar = false;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
    }

    loadData();
    if (!gDataError.empty()) {
        std::fprintf(stderr, "[data] %s\n", gDataError.c_str());
    }
    note("[data] " + std::to_string(gSongs.size()) + " songs");
    if (gDuplicateIds > 0) {
        note("[data] dropped " + std::to_string(gDuplicateIds) + " duplicate song id(s)");
    }
    InitCommonControls();

    auto difficultyMask = [&](bool mask[5]) {
        if (diffArg == "all") {
            for (int d = 0; d < 5; ++d) {
                mask[d] = true;
            }
            return;
        }
        for (int d = 0; d < 5; ++d) {
            mask[d] = diffArg.find(kDiffNames[d]) != std::string::npos;
        }
    };

    if (wantList) {
        for (const Song& song : gSongs) {
            const std::string line = id4(song.id) + "  " + song.title + "  [" + song.kana + "]  "
                + std::to_string(song.levels[3]) + "/" + std::to_string(song.levels[4]) + "  "
                + std::to_string(song.vocals.size()) + " versions";
            if (listFilter.empty()
                || line.find(listFilter) != std::string::npos
                || std::to_string(song.id) == listFilter) {
                std::printf("%s\n", line.c_str());
            }
        }
        return 0;
    }

    if (!downloadIds.empty()) {
        bool diffs[5] = {false, false, false, false, false};
        difficultyMask(diffs);
        int failed = 0;
        for (size_t start = 0; start <= downloadIds.size();) {
            const size_t comma = downloadIds.find(',', start);
            const std::string token = downloadIds.substr(start, comma == std::string::npos
                    ? std::string::npos
                    : comma - start);
            start = comma == std::string::npos ? downloadIds.size() + 1 : comma + 1;
            const int id = std::atoi(token.c_str());
            const auto found = std::find_if(gSongs.begin(), gSongs.end(),
                [&](const Song& song) { return song.id == id; });
            if (found == gSongs.end()) {
                std::fprintf(stderr, "[skip] %s: unknown song id\n", token.c_str());
                ++failed;
                continue;
            }
            Request request;
            for (int d = 0; d < 5; ++d) {
                request.diffs[d] = diffs[d];
            }
            request.jacket = wantJacket;
            request.sidecar = wantSidecar;
            if (vocalArg == "all" || found->vocals.empty()) {
                for (size_t v = 0; v < found->vocals.size(); ++v) {
                    request.vocalIndexes.push_back(v);
                }
            } else {
                for (size_t v = 0; v < found->vocals.size(); ++v) {
                    if (vocalArg.find(std::to_string(v)) != std::string::npos) {
                        request.vocalIndexes.push_back(v);
                    }
                }
            }
            queueSong(*found, request, outDir, force);
        }
        std::string error;
        return runJobQueue(error);
    }

    return runGui(outDir, screenshotPath, screenshotTime);
}
