// CppSekai chart downloader (chartdl) - a separate, standalone tool.
//
// Downloads charts / BGM / jackets for Project SEKAI songs from unipjsk into the
// game's charts\ folder, including every vocal version of a song, and writes the
// metadata sidecar (<id4>.json) the game reads.
//
// It shares nothing with the game except the vendored libraries (SDL2 + ImGui +
// nlohmann::json) and deliberately keeps the *old* CppSekai look: ImGui's
// default dark theme, default font, stock widgets - no pjsk skin.
//
//   chartdl.exe                       # the GUI
//   chartdl.exe --list [filter]       # print the song table and exit
//   chartdl.exe --download 374,75 --diffs all --vocals all --out ..\charts
//   chartdl.exe --screenshot shot.png [--screenshot-time 1.5]
//
// HTTP comes from winhttp.dll loaded at runtime (the toolchain has no import
// library for it, same trick platform/SystemMedia.cpp uses for combase.dll).
#define SDL_MAIN_HANDLED
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <SDL.h>

#include <GL/gl.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
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
        a.dll = reinterpret_cast<void*>(SDL_LoadObject("winhttp.dll"));
        if (a.dll == nullptr) {
            return a;
        }
        auto sym = [&](const char* name) { return SDL_LoadFunction(a.dll, name); };
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
        const fs::path partPath = path.string() + ".part";
        std::ofstream out(partPath, std::ios::binary);
        if (!out) {
            std::lock_guard<std::mutex> lock(gJobMutex);
            gJobs[index].state = JobState::Failed;
            gJobs[index].note = "cannot write " + partPath.string();
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
            logLine("[ok]   " + path.filename().string());
        } else {
            std::error_code removeEc;
            fs::remove(partPath, removeEc);
            logLine("[fail] " + path.filename().string() + "  " + error);
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

int main(int argc, char** argv)
{
    // The MinGW/subsystem-free build keeps stdout when launched from a shell;
    // a double-click gets no console, which is fine for the GUI mode.
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

    // -----------------------------------------------------------------------
    // GUI
    // -----------------------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("CppSekai chart downloader", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1180, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // The old look: stock ImGui dark theme, stock metrics - but the stock font
    // has no kana / kanji, so one system CJK face is merged in (the game does
    // the same, and Yu Gothic UI must be skipped there: CFF outlines, which
    // stb_truetype cannot rasterise).
    {
        const char* candidates[] = {"C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\msyh.ttf",
            "C:\\Windows\\Fonts\\meiryo.ttc", "C:\\Windows\\Fonts\\msgothic.ttc"};
        ImFontConfig config;
        config.OversampleH = 1;
        config.OversampleV = 1;
        for (const char* path : candidates) {
            if (fs::exists(path)) {
                ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 16.0f, &config,
                    ImGui::GetIO().Fonts->GetGlyphRangesJapanese());
                note(std::string("[font] ") + path + (font != nullptr ? " ok" : " FAILED"));
                break;
            }
        }
    }
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    bool running = true;
    char search[64] = "";
    char outDirBuf[512];
    std::snprintf(outDirBuf, sizeof(outDirBuf), "%s", outDir.string().c_str());
    int selectedRow = -1;
    std::vector<char> checked(gSongs.size(), 0);
    Request request;
    if (!gSongs.empty() && !gSongs[0].vocals.empty()) {
        request.vocalIndexes.push_back(0);
    }
    bool forceRedownload = false;
    Uint64 startTicks = SDL_GetTicks64();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        if (!screenshotPath.empty()
            && static_cast<double>(SDL_GetTicks64() - startTicks) / 1000.0 >= screenshotTime) {
            running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("CppSekai chart downloader", nullptr, ImGuiWindowFlags_NoCollapse);

        ImGui::Text("source: assets.unipjsk.com  |  %d songs loaded", static_cast<int>(gSongs.size()));
        if (!gDataError.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(%s)", gDataError.c_str());
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##outdir", outDirBuf, sizeof(outDirBuf));
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##search", "search: id / title / reading", search, sizeof(search));
        ImGui::SameLine();
        if (ImGui::Button("queue checked")) {
            int queued = 0;
            for (size_t i = 0; i < gSongs.size(); ++i) {
                if (checked[i] != 0) {
                    queueSong(gSongs[i], request, outDirBuf, forceRedownload);
                    ++queued;
                }
            }
            if (queued > 0) {
                startWorker();
            } else {
                logLine("[warn] nothing checked");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("cancel")) {
            gCancel.store(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("clear list")) {
            std::lock_guard<std::mutex> lock(gJobMutex);
            gJobs.clear();
        }
        ImGui::Checkbox("re-download existing files", &forceRedownload);
        ImGui::SameLine();
        ImGui::Checkbox("jacket", &request.jacket);
        ImGui::SameLine();
        ImGui::Checkbox("sidecar metadata", &request.sidecar);
        ImGui::SameLine();
        ImGui::Text("| difficulties:");
        for (int d = 0; d < 5; ++d) {
            ImGui::SameLine();
            ImGui::Checkbox(kDiffNames[d], &request.diffs[d]);
        }
        ImGui::Separator();

        // Left: song table. Right: details of the selected song.
        ImGui::BeginChild("##songs", ImVec2(ImGui::GetContentRegionAvail().x * 0.56f, -170.0f), true);
        if (ImGui::BeginTable("songs", 6,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("title", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("reading", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("EXP", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn("MST", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableHeadersRow();
            const std::string needle = search;
            int shown = 0;
            for (size_t i = 0; i < gSongs.size(); ++i) {
                const Song& song = gSongs[i];
                if (!needle.empty()) {
                    const std::string hay = song.title + song.kana + std::to_string(song.id);
                    if (hay.find(needle) == std::string::npos) {
                        continue;
                    }
                }
                ++shown;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                bool rowChecked = checked[i] != 0;
                if (ImGui::Checkbox("##check", &rowChecked)) {
                    checked[i] = rowChecked ? 1 : 0;
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%04d", song.id);
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Selectable(song.title.c_str(), selectedRow == static_cast<int>(i),
                        ImGuiSelectableFlags_SpanAllColumns)) {
                    selectedRow = static_cast<int>(i);
                    request.vocalIndexes.clear();
                    if (!song.vocals.empty()) {
                        request.vocalIndexes.push_back(0);
                    }
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(song.kana.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d", song.levels[3]);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%d", song.levels[4]);
            }
            ImGui::EndTable();
            if (!needle.empty()) {
                ImGui::Text("%d match(es)", shown);
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##details", ImVec2(0.0f, -170.0f), true);
        if (selectedRow >= 0 && selectedRow < static_cast<int>(gSongs.size())) {
            const Song& song = gSongs[selectedRow];
            ImGui::Text("%s", song.title.c_str());
            ImGui::TextWrapped("%s", song.kana.c_str());
            ImGui::Text("id %04d   lyricist %s", song.id, song.lyricist.c_str());
            ImGui::Text("composer %s   arranger %s", song.composer.c_str(), song.arranger.c_str());
            ImGui::Text("levels  E %d  N %d  H %d  EXP %d  MST %d", song.levels[0], song.levels[1],
                song.levels[2], song.levels[3], song.levels[4]);
            const fs::path dir(outDirBuf);
            ImGui::Separator();
            ImGui::Text("charts:");
            for (int d = 0; d < 5; ++d) {
                ImGui::SameLine();
                const bool have = fileExists(chartPath(dir, song.id, kDiffNames[d]));
                ImGui::TextColored(have ? ImVec4(0.4f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                    "%s", kDiffNames[d]);
            }
            ImGui::Separator();
            ImGui::Text("vocal versions:");
            if (song.vocals.empty()) {
                ImGui::TextDisabled("(no official vocal table entry)");
            }
            for (size_t v = 0; v < song.vocals.size(); ++v) {
                const VocalVersion& version = song.vocals[v];
                const bool have = fileExists(audioPath(dir, version, song.id));
                bool picked = std::find(request.vocalIndexes.begin(), request.vocalIndexes.end(), v)
                    != request.vocalIndexes.end();
                ImGui::PushID(static_cast<int>(v) + 500);
                if (ImGui::Checkbox("##vocal", &picked)) {
                    if (picked) {
                        request.vocalIndexes.push_back(v);
                    } else {
                        request.vocalIndexes.erase(std::remove(request.vocalIndexes.begin(),
                                                        request.vocalIndexes.end(), v),
                            request.vocalIndexes.end());
                    }
                }
                ImGui::PopID();
                ImGui::SameLine();
                ImGui::Text("%-26s %-22s %s", version.caption.c_str(), version.singers.c_str(),
                    have ? "[have]" : "");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n%s", version.asset.c_str(), audioUrl(version, song.id).c_str());
                }
            }
            ImGui::Separator();
            const bool haveJacket = fileExists(jacketPath(dir, song));
            ImGui::Text("jacket: %s", haveJacket ? "[have]" : "[missing]");
            if (ImGui::Button("queue this song")) {
                queueSong(song, request, dir, forceRedownload);
                startWorker();
            }
            ImGui::SameLine();
            if (ImGui::Button("open folder")) {
                std::string cmd = "explorer \"" + fs::absolute(dir).string() + "\"";
                std::system(cmd.c_str());
            }
        } else {
            ImGui::TextDisabled("select a song on the left");
        }
        ImGui::EndChild();

        // Bottom: queue progress + log.
        ImGui::BeginChild("##queue", ImVec2(0.0f, 0.0f), true);
        long long doneBytes = 0;
        long long totalBytes = 0;
        size_t finished = 0;
        size_t totalJobs = 0;
        std::string activeName = "(idle)";
        {
            std::lock_guard<std::mutex> lock(gJobMutex);
            totalJobs = gJobs.size();
            for (const Job& job : gJobs) {
                finished += job.state != JobState::Waiting && job.state != JobState::Active ? 1 : 0;
                if (job.state == JobState::Active) {
                    activeName = job.path.filename().string() + "  " + humanBytes(job.bytes)
                        + (job.total > 0 ? " / " + humanBytes(job.total) : "");
                    totalBytes += job.total;
                }
                doneBytes += job.bytes;
            }
        }
        const float progress = totalJobs == 0 ? 0.0f
                                              : static_cast<float>(finished) / static_cast<float>(totalJobs);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f),
            (std::to_string(finished) + " / " + std::to_string(totalJobs)).c_str());
        ImGui::Text("%s   %s", gRunning.load() ? "downloading" : "idle", activeName.c_str());
        ImGui::SameLine();
        ImGui::Text("   %s total so far", humanBytes(doneBytes).c_str());
        ImGui::Separator();
        ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(gLogMutex);
            for (const std::string& line : gLog) {
                ImGui::TextUnformatted(line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (!screenshotPath.empty()
            && static_cast<double>(SDL_GetTicks64() - startTicks) / 1000.0 >= screenshotTime) {
            std::vector<unsigned char> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            // stb_image_write expects rows top-down; GL gives them bottom-up.
            std::vector<unsigned char> flipped(pixels.size());
            const size_t rowBytes = static_cast<size_t>(w) * 4;
            for (int y = 0; y < h; ++y) {
                std::memcpy(flipped.data() + static_cast<size_t>(y) * rowBytes,
                    pixels.data() + static_cast<size_t>(h - 1 - y) * rowBytes, rowBytes);
            }
            stbi_write_png(screenshotPath.c_str(), w, h, 4, flipped.data(), static_cast<int>(rowBytes));
            std::printf("screenshot saved: %s\n", screenshotPath.c_str());
        }
        SDL_GL_SwapWindow(window);
    }

    if (gRunning.load()) {
        gCancel.store(true);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
