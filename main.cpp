// CppSekai - Project SEKAI style SUS chart player (Windows native)
// Derived from sekai-mmw-preview-web (AGPL-3.0), which is based on
// MikuMikuWorld (MIT). See README.md for details.
// We provide our own console main(); tell SDL not to redefine it as SDL_main.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#include <GL/gl.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "core_api.hpp"
#include "platform/Audio.hpp"
#include "platform/Renderer.hpp"
#include "platform/SystemMedia.hpp"
#include "game/Intro.hpp"
#include "game/Judgement.hpp"
#include "game/Hud.hpp"
#include "game/Result.hpp"
#include "game/SongSelect.hpp"
#include "game/Ui.hpp"
#include "game/TapEffect.hpp"

#include <map>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Alignment overrides shared with startSession() (set from the command line).
double gFillerSec = -1.0;    // < 0: auto-detect the leading silence
double gUserOffsetSec = 0.0; // manual fine tune, seconds

namespace
{
    // SDL2's MinGW import library references three screensaver entry points
    // that nothing exports; provide stubs to satisfy the linker.
    extern "C" {
        void* ScreenSaverProc(void* hwnd, unsigned msg, unsigned long long w, long long l) { (void)hwnd; (void)msg; (void)w; (void)l; return nullptr; }
        int ScreenSaverConfigureDialog(void* hwnd, unsigned msg, unsigned long long w, long long l) { (void)hwnd; (void)msg; (void)w; (void)l; return 0; }
        long long RegisterDialogClasses(void* hInst) { (void)hInst; return 1; }
    }

    struct TouchTrack
    {
        SDL_FingerID fingerId = 0;
        // A real touch or a mouse button. Never infer this from the id's sign:
        // the flick thresholds are different for the two (a mouse swipe is
        // much faster than a finger), so a wrong guess makes touchscreen
        // flicks near-impossible / mouse flicks hair-triggered.
        bool isTouch = true;
        float lanePos = 0.0f;
        // Where the finger last rested (updated while it is not swiping). A
        // hold that slides sideways ends on a DIFFERENT lane than the press
        // started on, and a flick is covered by the lane it sits on - so
        // judging the tail flick at the press lane found nothing and the
        // player had to lift the finger and swipe again (a fresh touch reads
        // its lane from the current position). See flickJudge().
        float restLanePos = 0.0f;
        int laneIndex = 0;
        float lastLanePos = 0.0f;
        float lastScreenX = 0.0f;
        float lastScreenY = 0.0f;
        double lastMoveTimeSec = 0.0;
        // When this touch last fired a flick. A finger may fire more than
        // once: the old one-shot latch burned it for the rest of the contact,
        // so a swipe that came a little early (or any accidental swipe during
        // a long hold) consumed the gesture and the real flick at the hold
        // tail was then ignored - the tail only reacted to lifting the finger
        // and swiping again.
        double lastFlickFireTimeSec = -1.0;
        // Swipe velocity in *screen pixels per second*, low-passed over the
        // per-event samples (touch panels report unevenly spaced jumps, and a
        // single-frame delta easily misses a fast flick). Up / right positive.
        float velUp = 0.0f;
        float velSide = 0.0f;
        // Largest smoothed speed seen during the gesture, and the total
        // distance travelled - a flick is judged from these at lift-off too,
        // because a short flick often ends before the next move event.
        float peakUp = 0.0f;
        float peakSide = 0.0f;
        float travelUp = 0.0f;
        float travelSide = 0.0f;
    };

    // Sampling of a swipe. A touch panel reports NOTHING while the finger
    // holds still, so the first motion event of a hold-tail flick arrives
    // after a gap of hundreds of milliseconds. Dividing the displacement by
    // that gap measured a fraction of the real speed and the flick never
    // fired - which is exactly why the tail flick only registered after
    // *lifting* the finger and swiping with a fresh touch (a new touch starts
    // its own clock at SDL_FINGERDOWN). Clamp the sample period to a
    // plausible flick sample instead.
    constexpr double kFlickMaxSampleSec = 0.03;
    // A gap longer than this means the finger was parked: restart the velocity
    // filter so a stale speed is never blended into the new movement.
    constexpr double kFlickIdleGapSec = 0.05;
    // Shortest time between two flick fires from the same touch. One swipe may
    // clear consecutive flick notes, but it must not fire on every event.
    constexpr double kFlickRefireSec = 0.06;
    // Below this speed (screen px/s, scaled by the window height) the finger
    // counts as parked, so its lane is remembered as the flick's origin lane.
    constexpr float kFlickRestSpeed = 250.0f;

    // Flick direction from a swipe measured in screen px/s (up / right
    // positive) plus how far the gesture actually travelled in that direction.
    //
    // Measuring both axes in pixels matters: the old check compared world-Y
    // against *lane* units, and at 16:9 one lane unit is ~6x coarser than one
    // world unit, so an up flick effectively had to be 3.6x more vertical than
    // horizontal before it counted - which is why swiping on a touchscreen
    // almost never registered. `heightScale` (= window height / 1080) keeps the
    // threshold in the same "feel" at every resolution.
    game::FlickDir flickDirFrom(float upSpeed, float sideSpeed, float travelUp, float travelSide, bool isTouch,
        float heightScale)
    {
        const float upThreshold = (isTouch ? 500.0f : 900.0f) * heightScale;
        const float sideThreshold = (isTouch ? 600.0f : 900.0f) * heightScale;
        // How vertical an up flick has to be. A finger swipe is rarely
        // straight, so touch gets a generous cone (~63 degrees off vertical);
        // the travel check keeps tap jitter out.
        const float upBias = isTouch ? 0.5f : 0.8f;
        const float minTravel = 14.0f * heightScale;
        if (upSpeed > upThreshold && travelUp > minTravel
            && upSpeed >= std::abs(sideSpeed) * upBias) {
            return game::FlickUp;
        }
        if (sideSpeed > sideThreshold && travelSide > minTravel) {
            return game::FlickRight;
        }
        if (sideSpeed < -sideThreshold && travelSide < -minTravel) {
            return game::FlickLeft;
        }
        return game::FlickNone;
    }

    // Keyboard: 12 keys -> 12 lanes. Lane i (0..11) is the core's note lane i,
    // whose center sits at (i - 6) + 0.5 = i - 5.5 in lane coordinates.
    const SDL_Keycode kLaneKeys[12] = {
        SDLK_z, SDLK_s, SDLK_x, SDLK_d, SDLK_c, SDLK_v,
        SDLK_g, SDLK_b, SDLK_h, SDLK_n, SDLK_j, SDLK_m,
    };

    float keyLanePos(int lane)
    {
        return static_cast<float>(lane) - 5.5f;
    }

    int laneIndexFromPos(float lanePos)
    {
        return std::clamp(static_cast<int>(std::lround(lanePos + 5.5f)), 0, 11);
    }

    // The playfield is a fake perspective: a lane coordinate x at height y is
    // drawn at world (x * y, y). y = 1 is the judge line, y -> 0 the horizon.
    constexpr float JUDGE_LINE_Y = 1.0f;
    constexpr int LANE_COUNT = 12;

    std::string readFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

#ifdef _WIN32
    std::string wideToUtf8(const wchar_t* wide)
    {
        if (wide == nullptr || wide[0] == L'\0') {
            return {};
        }
        const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 1) {
            return {};
        }
        std::string out(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
        return out;
    }

    // Path of the picture Windows is showing as the desktop wallpaper, empty
    // when nothing usable is found. Sources in order of reliability:
    //   1. SPI_GETDESKWALLPAPER - the documented way, works for a static
    //      wallpaper (the usual case);
    //   2. the HKCU value behind it - survives the SPI call being blocked;
    //   3. %APPDATA%\Microsoft\Windows\Themes\TranscodedWallpaper - what
    //      Windows uses for slideshows / Spotlight (a JPEG with no extension;
    //      stb_image sniffs the content, so the missing extension is fine).
    std::string windowsWallpaperPath()
    {
        wchar_t buffer[MAX_PATH * 4] = {};
        if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, static_cast<UINT>(std::size(buffer)), buffer, 0)
            && GetFileAttributesW(buffer) != INVALID_FILE_ATTRIBUTES) {
            return wideToUtf8(buffer);
        }
        DWORD size = sizeof(buffer);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallPaper", RRF_RT_REG_SZ, nullptr,
                buffer, &size) == ERROR_SUCCESS
            && GetFileAttributesW(buffer) != INVALID_FILE_ATTRIBUTES) {
            return wideToUtf8(buffer);
        }
        const char* appData = std::getenv("APPDATA");
        if (appData != nullptr) {
            const std::string candidate =
                std::string(appData) + "\\Microsoft\\Windows\\Themes\\TranscodedWallpaper";
            if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                return candidate;
            }
        }
        return {};
    }

    // Hides (or restores) the Windows touch visual feedback - the ripple /
    // circle the system draws around a touch contact - for OUR window only
    // (SetWindowFeedbackSetting, Win8+). Because it is a per-window setting,
    // closing the game restores the system behaviour automatically; there is
    // no global state to save or undo.
    void applyTouchFeedback(SDL_Window* window, bool hide)
    {
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) {
            return;
        }
        using SetWindowFeedbackSettingFn = BOOL(WINAPI*)(HWND, UINT, DWORD, UINT32, const void*);
        const auto fn = reinterpret_cast<SetWindowFeedbackSettingFn>(reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowFeedbackSetting")));
        if (fn == nullptr) {
            return; // pre-Win8 or stripped user32: feature simply stays on
        }
        const BOOL value = hide ? FALSE : TRUE;
        // 1 = FEEDBACK_TOUCH_CONTACTVISUALIZATION (the ripple),
        // 7 = FEEDBACK_TOUCH_TAP.
        fn(info.info.win.window, 1, 0, sizeof(BOOL), &value);
        fn(info.info.win.window, 7, 0, sizeof(BOOL), &value);
    }
#endif

    void printUsage()
    {        std::printf(
            "CppSekai - Project SEKAI style SUS chart player\n"
            "Usage: cppsekai [--sus <file.sus>] [--bgm <audio>] [--charts <dir>]\n"
            "                [--offset <sec>] [--filler <sec>] [--auto] [--speed <1-12>]\n"
            "                [--se-volume <0-1>] [--lead-in <sec>] [--cover <image>]\n"
            "                [--screenshot <png>] [--screenshot-time <sec>] [--pjsk-font]\n"
            "                [--title <text>] [--lyricist <text>] [--composer <text>]\n"
            "                [--arranger <text>] [--vocal <text>] [--difficulty <text>]\n"
            "                [--width <px>] [--height <px>] [--window <mode>] [--fps <n>]\n"
            "                [--judge-sheet] [--judge-frame <n>] [--test-hits]\n"
            "                [--show-pause-dialog] [--test-restart] [--restart-at <sec>]\n"
            "                [--result-preview] [--result-at <sec>] [--help]\n\n"

            "No --sus: opens the song select screen (scans --charts, then charts/ next\n"
            "to the exe, then the charts/ of the parent folder).\n"
            "--filler: seconds of silence at the head of the BGM (auto-detected when\n"
            "          omitted). --offset: manual fine tune in seconds.\n"
            "--auto: autoplay preview for this run only (the saved setting is not\n"
            "        changed). --screenshot: headless frame dump, then exit.\n"
            "--pjsk-font: use the bundled pjsk fonts instead of the system UI font.\n"
            "--window: borderless (default) | windowed | fullscreen. --width/--height:\n"
            "          window size (default 1280x720).\n"
            "--fps: cap the frame rate in addition to vsync (0 = vsync only). Lower\n"
            "       values (e.g. 30/60) reduce GPU/CPU load and power draw.\n"
            "Keyboard: Z S X D C V G B H N J M = 12 lanes\n"
            "          SPACE = pause, F = fullscreen, H = debug panel, ESC = back/quit\n"
            "--test-hits: fire the hit effects for upcoming notes without input (debug).\n"
            "--judge-frame <n>: freeze the judge text on animation frame n (debug).\n"
            "--result-preview: jump straight to the result screen at boot (debug; uses\n"
            "                  the reference screenshot's numbers for pixel checks).\n"
            "--result-at <sec>: show the result screen once the chart reaches <sec>.\n"
            "Mouse   : left/right button = tap a lane (hold = long note),\n"
            "          drag up/left/right = flick (direction must match the\n"
            "          note arrow; see the strict-Flick setting). Right button\n"
            "          gives a second pointer.\n"
            "          Clicks on the HUD / panels never count as a hit.\n"
            "Touch   : multi-touch lanes, swipe up for flicks\n"
            "Song list: wheel / drag to scroll, tap a row to pick it (the row in\n"
            "          the middle is the selection), double click / tap to play,\n"
            "          arrows + Enter, F5 = rescan charts/.\n"
            "Logs    : to the console that launched the exe, otherwise cppsekai.log\n"
            "          next to the working directory (--screenshot always logs to\n"
            "          cppsekai.log). See CLI.md for the full manual.\n");
    }

    // Trigger the SE matching a hit event kind + critical flag.
    platform::AudioEngine::SeKind seForKind(float kind, bool critical)
    {
        using platform::AudioEngine;
        const int k = static_cast<int>(kind);
        if (critical) {
            switch (k) {
                case 2: return AudioEngine::SeFlickCritical;
                case 3: return AudioEngine::SeTraceCritical;
                case 4: return AudioEngine::SeTickCritical;
                default: return AudioEngine::SeCriticalTap;
            }
        }
        switch (k) {
            case 2: return AudioEngine::SeFlick;
            case 3: return AudioEngine::SeTrace;
            case 4: return AudioEngine::SeTick;
            default: return AudioEngine::SePerfect;
        }
    }

    void playHitSe(platform::AudioEngine& audio, const game::JudgementEngine& judgement, float seVolume)
    {
        const auto& stats = judgement.stats();
        if (stats.lastJudge == game::Judge::None) {
            return;
        }
        const bool quieter = stats.lastJudge == game::Judge::Good || stats.lastJudge == game::Judge::Bad;
        audio.playSe(seForKind(stats.lastHitKind, stats.lastJudgeCritical), seVolume * (quieter ? 0.5f : 1.0f));
    }

    enum class AppState
    {
        Select,
        Play,
        Result,
    };

    // Snapshot handed to the result screen when a chart ends. Filled from the
    // judgement stats + the record that was stored for this chart.
    game::ResultData buildResultData(const game::IntroInfo& intro, const game::ChartEntry& entry,
        const game::JudgementStats& stats, double previousBest, float chartRating)
    {
        game::ResultData data;
        data.title = !intro.title.empty() ? intro.title : entry.title;
        if (data.title.empty()) {
            data.title = entry.displayName;
        }
        data.difficulty = !entry.difficulty.empty() ? entry.difficulty : intro.difficulty;
        data.level = entry.level;
        if (data.level.empty() && entry.musicId > 0) {
            const int level = game::musicLevel(entry.musicId, data.difficulty);
            if (level > 0) {
                data.level = std::to_string(level);
            }
        }
        data.score = stats.score;
        data.highScore = previousBest;
        data.newRecord = stats.score > previousBest + 0.5;
        data.perfect = stats.perfect;
        data.great = stats.great;
        data.good = stats.good;
        data.bad = stats.bad;
        data.miss = stats.miss;
        data.maxCombo = stats.maxCombo;
        data.chartRating = chartRating;
        return data;
    }

    // Opening card metadata. SUS has no lyricist/composer fields, so they can
    // be supplied on the command line (same flags as the upstream preview
    // player). Shared by every session started in this run.
    game::IntroMetadata gCardMetadata;

    // One loaded chart: everything the play loop needs to draw + judge it.
    struct Session
    {
        bool active = false;
        game::ChartEntry entry;
        game::IntroInfo intro;
        double waveOffsetSec = 0.0;
        Uint64 startCounter = 0;
        bool scoreRecorded = false; // set once the song is played to the end
    };

    // CppSekai: how many of the judgement engine's hit event indices have
    // already been published to the chart core (markNoteHit). Reset per session.
    std::size_t s_hitPublishCursor = 0;

    bool startSession(Session& session, const game::ChartEntry& entry, platform::Renderer& renderer,
        platform::AudioEngine& audio, game::JudgementEngine& judgement, float noteSpeed, std::string& error)
    {
        audio.stopMusic();
        audio.stopResultBgm();
        judgement.reset();
        s_hitPublishCursor = 0;
        core_api::clearHitNotes();

        const std::string susText = readFile(entry.susPath);
        if (susText.empty()) {
            error = "cannot read chart: " + entry.susPath;
            return false;
        }

        const double waveOffset = core_api::readWaveOffset(susText);
        if (!core_api::loadSusTextPrecise(susText.c_str(), waveOffset * 1000.0)) {
            error = std::string("SUS parse failed: ") + core_api::getLastError();
            return false;
        }
        core_api::setPreviewConfig(0, 1, 1, 1, 0, 0, noteSpeed, 1.0f, 0.6f, 0.0f, 1.0f, 0.85f);

        const float* events = core_api::getHitEventBuffer();
        const int count = core_api::getHitEventCount();
        judgement.load(events, count);
        // Chart level drives the score formula's levelFactor (upstream
        // hard-codes RATING = 26; the official level table is used here).
        int chartLevel = game::musicLevel(entry.musicId, entry.difficulty);
        if (chartLevel <= 0) {
            chartLevel = std::atoi(entry.level.c_str());
        }
        judgement.setChartRating(chartLevel > 0 ? static_cast<float>(chartLevel) : 26.0f);

        if (!entry.bgmPath.empty()) {
            audio.loadMusic(entry.bgmPath, error);
        }
        // Official pjsk audio starts with fillerSec seconds of silence; chart
        // tick 0 is right after it. Sidecar > auto-detect > 0. The SUS
        // #WAVEOFFSET (positive = audio plays later) is added on top.
        double startPos = entry.audioStartSec;
        if (startPos <= 0.0 && !entry.bgmPath.empty()) {
            startPos = platform::AudioEngine::detectLeadingSilence(entry.bgmPath);
            if (startPos > 0.0) {
                std::printf("[audio] detected %.2fs of leading silence\n", startPos);
            }
        }
        if (gFillerSec >= 0.0) {
            startPos = gFillerSec;
        }
        audio.setMusicStartPos(startPos + waveOffset);
        audio.setUserOffset(gUserOffsetSec);
        std::printf("[audio] music starts at %.2fs (chart 0), user offset %+.3fs\n",
            audio.musicStartPos(), audio.userOffset());

        renderer.loadCover(entry.coverPath, error);
        game::IntroMetadata metadata = gCardMetadata;
        metadata.susPath = entry.susPath;
        if (metadata.title.empty()) {
            metadata.title = core_api::getMetadataTitle() != nullptr ? core_api::getMetadataTitle() : "";
        }
        if (metadata.composer.empty()) {
            metadata.composer = entry.composer;
        }
        if (metadata.composer.empty()) {
            metadata.composer = core_api::getMetadataArtist() != nullptr ? core_api::getMetadataArtist() : "";
        }
        if (metadata.arranger.empty()) {
            metadata.arranger = entry.arranger;
        }
        if (metadata.arranger.empty()) {
            metadata.arranger = core_api::getMetadataDesigner() != nullptr ? core_api::getMetadataDesigner() : "";
        }
        if (metadata.lyricist.empty()) {
            metadata.lyricist = entry.lyricist;
        }
        if (metadata.vocal.empty()) {
            metadata.vocal = entry.vocal;
        }
        if (metadata.difficulty.empty()) {
            metadata.difficulty = entry.difficulty;
        }
        session.intro = game::buildIntroInfo(metadata, renderer.cover() != nullptr);
        // unipjsk charts ship without #TITLE; fall back to the file name.
        if (session.intro.title.empty() || session.intro.title == "Unknown Title") {
            session.intro.title = entry.title.empty() ? entry.displayName : entry.title;
        }
        session.entry = entry;
        session.waveOffsetSec = waveOffset;
        session.active = true;
        session.scoreRecorded = false;

        for (int i = 0; i < count && i < 8; ++i) {
            const int off = i * 7;
            std::printf("hitEvent[%d] t=%.3f center=%.2f width=%.2f kind=%.0f end=%.3f\n",
                i, events[off + 0], events[off + 1], events[off + 2], events[off + 3], events[off + 5]);
        }
        std::fflush(stdout);
        return true;
    }
} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    // The binary is linked as a Windows-subsystem app, so double-clicking it
    // does not spawn a console window. A terminal that launched us (cmd /
    // PowerShell) may have handed down its standard handles; when it did not,
    // grab that console so the log lines (and --help) still show up there. With
    // neither - a plain double click - everything goes to cppsekai.log so the
    // run leaves something readable behind.
    {
        const HANDLE outHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        const bool haveStdout = outHandle != nullptr && outHandle != INVALID_HANDLE_VALUE;
        const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
        if (!haveStdout) {
            if (attached) {
                std::freopen("CONOUT$", "w", stdout);
                std::freopen("CONOUT$", "w", stderr);
            } else if (std::freopen("cppsekai.log", "w", stdout) != nullptr) {
                std::freopen("cppsekai.log", "w", stderr);
                setvbuf(stdout, nullptr, _IONBF, 0);
                setvbuf(stderr, nullptr, _IONBF, 0);
            }
        }
    }
#endif
    // MinGW's argv is ANSI-codepage; the UI is UTF-8. Re-fetch the command
    // line as UTF-16 and convert, so --title with Japanese text survives.
    int utf8Argc = argc;
    char** utf8Argv = argv;
#ifdef _WIN32
    {
        int wArgc = 0;
        LPWSTR* wArgv = CommandLineToArgvW(GetCommandLineW(), &wArgc);
        if (wArgv != nullptr && wArgc > 0) {
            auto* converted = new char*[static_cast<size_t>(wArgc)];
            for (int i = 0; i < wArgc; ++i) {
                const int bytes = WideCharToMultiByte(CP_UTF8, 0, wArgv[i], -1, nullptr, 0, nullptr, nullptr);
                converted[i] = new char[static_cast<size_t>(bytes)];
                WideCharToMultiByte(CP_UTF8, 0, wArgv[i], -1, converted[i], bytes, nullptr, nullptr);
            }
            LocalFree(wArgv);
            utf8Argc = wArgc;
            utf8Argv = converted;
        }
    }
#endif
    std::string susPath;
    std::string bgmPath;
    std::string chartsDir;
    std::string coverPath;
    bool offsetGiven = false;
    bool useSystemFont = true;
    bool autoPlay = false;
    float noteSpeed = 8.0f;
    float seVolume = 0.8f;
    std::string screenshotPath; // if set: dump a frame and exit (headless check)
    double screenshotTimeSec = 4.0;
    bool screenshotTimeGiven = false; // --screenshot-time was passed explicitly
    double leadIn = 6.0; // intro card (4s) + playfield fade-in, then the music
    bool dumpJudgeSheet = false;
    bool testHits = false; // debug: fire hit effects without player input
    int judgeAnimFrame = -1; // debug: freeze the judge text on this animation frame
    bool showPauseDialogShot = false; // headless check: force the pause dialog open
    bool showSettingsShot = false; // headless check: force the settings card open
    int settingsTabShot = -1;      // headless check: which settings tab to show
    bool testRestart = false; // debug: replay "give up -> pick another song"
    double restartAtSec = 8.0;
    bool resultPreview = false; // debug: boot straight into the result screen
    double resultAtSec = -1.0;  // debug: show the result at this chart time
    int winWidth = 1366;
    int winHeight = 768;
    int windowMode = 1; // 0=borderless 1=windowed 2=fullscreen(desktop)
    int fpsLimit = 60;  // extra frame cap on top of vsync; 0 = vsync only
    bool showProgressBar = true; // subtle top-edge playback bar (settings toggle)
    bool hideTouchFeedback = true; // hide the system touch ripple over our window

    // Settings that also live in userdata.json (loaded below). Flags present on
    // the command line win over the saved values; these record which were given.
    bool speedGiven = false;
    bool seGiven = false;
    bool leadInGiven = false;
    bool windowGiven = false;
    bool fpsGiven = false;
    bool autoplayGiven = false;
    bool widthGiven = false;
    bool heightGiven = false;
    game::UserSettings userSettings;
    std::map<std::string, game::ScoreRecord> scores;

    for (int i = 1; i < utf8Argc; ++i) {
        const std::string arg = utf8Argv[i];
        if (arg == "--sus" && i + 1 < utf8Argc) {
            susPath = utf8Argv[++i];
        } else if (arg == "--bgm" && i + 1 < utf8Argc) {
            bgmPath = utf8Argv[++i];
        } else if (arg == "--charts" && i + 1 < utf8Argc) {
            chartsDir = utf8Argv[++i];
        } else if (arg == "--cover" && i + 1 < utf8Argc) {
            coverPath = utf8Argv[++i];
        } else if (arg == "--offset" && i + 1 < utf8Argc) {
            gUserOffsetSec = std::atof(utf8Argv[++i]);
            offsetGiven = true;
        } else if (arg == "--filler" && i + 1 < utf8Argc) {
            gFillerSec = std::atof(utf8Argv[++i]);
        } else if (arg == "--width" && i + 1 < utf8Argc) {
            winWidth = std::atoi(utf8Argv[++i]);
            widthGiven = true;
        } else if (arg == "--height" && i + 1 < utf8Argc) {
            winHeight = std::atoi(utf8Argv[++i]);
            heightGiven = true;
        } else if (arg == "--window" && i + 1 < utf8Argc) {
            const std::string mode = utf8Argv[++i];
            windowGiven = true;
            if (mode == "windowed") {
                windowMode = 1;
            } else if (mode == "fullscreen") {
                windowMode = 2;
            } else {
                windowMode = 0; // borderless
            }
        } else if (arg == "--fps" && i + 1 < utf8Argc) {
            fpsLimit = std::atoi(utf8Argv[++i]);
            fpsGiven = true;
        } else if (arg == "--pjsk-font") {
            useSystemFont = false;
        } else if (arg == "--auto") {
            autoPlay = true;
            autoplayGiven = true;
        } else if (arg == "--speed" && i + 1 < utf8Argc) {
            noteSpeed = static_cast<float>(std::atof(utf8Argv[++i]));
            speedGiven = true;
        } else if (arg == "--se-volume" && i + 1 < utf8Argc) {
            seVolume = static_cast<float>(std::atof(utf8Argv[++i]));
            seGiven = true;
        } else if (arg == "--screenshot" && i + 1 < utf8Argc) {
            screenshotPath = utf8Argv[++i];
        } else if (arg == "--screenshot-time" && i + 1 < utf8Argc) {
            screenshotTimeSec = std::atof(utf8Argv[++i]);
            screenshotTimeGiven = true;
        } else if (arg == "--lead-in" && i + 1 < utf8Argc) {
            leadIn = std::atof(utf8Argv[++i]);
            leadInGiven = true;
        } else if (arg == "--judge-sheet") {
            dumpJudgeSheet = true;
        } else if (arg == "--test-hits") {
            testHits = true;
        } else if (arg == "--judge-frame" && i + 1 < utf8Argc) {
            judgeAnimFrame = std::atoi(utf8Argv[++i]);
        } else if (arg == "--show-pause-dialog") {
            showPauseDialogShot = true;
        } else if (arg == "--settings") {
            // Headless check: open the settings card right away, so a
            // --screenshot run can look at it (a key press can't be sent).
            showSettingsShot = true;
        } else if (arg == "--settings-tab" && i + 1 < utf8Argc) {
            settingsTabShot = std::atoi(utf8Argv[++i]);
        } else if (arg == "--test-restart") {
            // Debug: at --restart-at seconds, give the running song up and
            // start the next chart (the sequence that used to hang on the
            // second audio load).
            testRestart = true;
        } else if (arg == "--restart-at" && i + 1 < utf8Argc) {
            restartAtSec = std::atof(utf8Argv[++i]);
        } else if (arg == "--result-preview") {
            // Debug: show the result screen right away. Without --result-at
            // the sample numbers of the reference screenshot are used, which
            // is what makes a pixel comparison against it possible.
            resultPreview = true;
        } else if (arg == "--result-at" && i + 1 < utf8Argc) {
            resultAtSec = std::atof(utf8Argv[++i]);
        } else if (arg == "--title" && i + 1 < utf8Argc) {
            gCardMetadata.title = utf8Argv[++i];
        } else if (arg == "--lyricist" && i + 1 < utf8Argc) {
            gCardMetadata.lyricist = utf8Argv[++i];
        } else if (arg == "--composer" && i + 1 < utf8Argc) {
            gCardMetadata.composer = utf8Argv[++i];
        } else if (arg == "--arranger" && i + 1 < utf8Argc) {
            gCardMetadata.arranger = utf8Argv[++i];
        } else if (arg == "--vocal" && i + 1 < utf8Argc) {
            gCardMetadata.vocal = utf8Argv[++i];
        } else if (arg == "--difficulty" && i + 1 < utf8Argc) {
            gCardMetadata.difficulty = utf8Argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
    }

    // GUI-subsystem binaries have no console; when running headless checks
    // route stdout/stderr into a log file next to the screenshot.
    if (!screenshotPath.empty()) {
        std::FILE* logFile = std::fopen("cppsekai.log", "w");
        if (logFile != nullptr) {
            std::fclose(logFile);
            std::freopen("cppsekai.log", "w", stdout);
            std::freopen("cppsekai.log", "w", stderr);
            setvbuf(stdout, nullptr, _IONBF, 0);
            setvbuf(stderr, nullptr, _IONBF, 0);
        }
    }

    // ------------------------------------------------------------------
    // SDL + OpenGL
    // ------------------------------------------------------------------
    // Boot timing: nothing is drawn (so the window stays black) until the
    // first frame, and the stages below are the ones that cost time.
    const Uint64 bootCounter = SDL_GetPerformanceCounter();
    const double bootFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    auto bootMs = [&]() {
        return static_cast<double>(SDL_GetPerformanceCounter() - bootCounter) * 1000.0 / bootFreq;
    };
    auto bootLog = [&](const char* stage) {
        std::printf("[boot] %-22s %7.1f ms\n", stage, bootMs());
    };

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    bootLog("sdl init");
    // Touch must synthesize mouse events or ImGui (which only reads mouse)
    // ignores taps entirely: every UI element (song list, settings, dialogs)
    // becomes unclickable on a touchscreen. The raw SDL_FINGER* path still
    // fires for the playfield; the synthetic mouse events carry
    // SDL_TOUCH_MOUSEID and are filtered out of the mouse handlers below.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    // Native IME UI (the candidate list) is OFF by default in SDL2, and the
    // ImGui SDL2 backend only enables it in its own Init - which runs after
    // SDL_CreateWindow, where the hint no longer reaches the main window.
    // Set it here, before the window exists, so the search box's IME panel
    // actually shows up (SDL_HINT_IME_SHOW_UI docs: "0" = not displayed).
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    // Let the IME send SDL_TEXTEDITING events (default) so ImGui can render
    // the in-progress composition string inside the input box.
    SDL_SetHint(SDL_HINT_IME_INTERNAL_EDITING, "0");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Resolve the executable directory and load the persisted player data
    // (settings + play results) up front, so a saved window mode applies to the
    // window we are about to create. Command-line flags win over saved values.
    std::string baseDir;
    {
        char* basePath = SDL_GetBasePath();
        if (basePath != nullptr) {
            baseDir = basePath;
            SDL_free(basePath);
        }
    }
    const std::string userDataFile = game::userDataPath(baseDir);
    game::loadUserData(userDataFile, userSettings, scores);
    if (!speedGiven) {
        noteSpeed = userSettings.noteSpeed;
    }
    if (!seGiven) {
        seVolume = userSettings.seVolume;
    }
    if (!leadInGiven) {
        leadIn = userSettings.leadInSec;
    }
    if (!windowGiven) {
        windowMode = userSettings.windowMode;
    }
    // Saved resolution applies unless --width/--height overrode it. resW/resH
    // keep the *chosen* size (the live windowW/H follow resizes + fullscreen),
    // so persisting never records the desktop size of a fullscreen session.
    if (!widthGiven && !heightGiven) {
        winWidth = userSettings.windowWidth;
        winHeight = userSettings.windowHeight;
    }
    int resW = std::max(320, winWidth);
    int resH = std::max(240, winHeight);
    if (!fpsGiven) {
        fpsLimit = userSettings.fpsLimit;
    }
    if (!offsetGiven) {
        gUserOffsetSec = userSettings.offsetSec;
    }
    if (!autoplayGiven) {
        autoPlay = userSettings.autoplay;
    }
    showProgressBar = userSettings.showProgressBar;
    hideTouchFeedback = userSettings.hideTouchFeedback;
    const int splashStyle = userSettings.splashStyle; // 0=image 1=classic

    int windowW = std::max(320, winWidth);
    int windowH = std::max(240, winHeight);
    // Image splash + fullscreen: the boot window must also not span the whole
    // desktop. Windows promotes a window that covers the monitor to
    // "fullscreen optimized" presentation, which bypasses DWM composition -
    // the transparent splash background turns opaque black. The window is
    // fully transparent apart from the picture (which is drawn centred at its
    // native size), so shrinking it by a few pixels is invisible, and it goes
    // fullscreen at the end of the boot sequence anyway.
    if (windowMode == 2 && splashStyle == 0) {
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w > 0 && usable.h > 0) {
            windowW = std::min(windowW, std::max(320, usable.w - 16));
            windowH = std::min(windowH, std::max(240, usable.h - 16));
        }
    }
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    // windowed keeps the frame; borderless/fullscreen hide it anyway. The
    // image splash additionally forces frameless: a black title bar over the
    // static picture looks broken - the frame comes back after loading.
    if (windowMode != 1 || splashStyle == 0) {
        windowFlags |= SDL_WINDOW_BORDERLESS;
    }
    // Image splash is a free-floating PNG: enable DWM per-pixel transparency
    // (DwmExtendFrameIntoClientArea with -1 margins) so the picture's alpha
    // shows the desktop instead of a black backdrop. The GL frames decide
    // opacity themselves: clearing alpha=0 shows the desktop, clearing alpha=1
    // (the game's normal background) is opaque, so this can stay on all run.
    // SDL2 has no transparent-window flag (that is SDL3), so do it manually.
    if (splashStyle == 0) {
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8); // the framebuffer needs an alpha channel
    }
    SDL_Window* window = SDL_CreateWindow(
        "CppSekai",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowW, windowH,
        windowFlags);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    // Fullscreen is entered right away for the classic splash - its dark
    // backdrop wants the whole screen from the first frame. The image splash
    // defers it: a window that covers the entire desktop loses the DWM
    // per-pixel transparency (Windows fullscreen optimizations take over), so
    // the alpha=0 clear that makes the splash background transparent turned
    // into an opaque black one and the boot picture appeared on a black
    // screen. The image splash therefore always runs in a plain borderless
    // window of the chosen size and the configured mode is applied once
    // loading is finished (see the end of the boot sequence below).
    if (windowMode == 2 && splashStyle != 0) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    std::printf("[window] %dx%d mode=%s\n", windowW, windowH,
        windowMode == 0 ? "borderless" : windowMode == 1 ? "windowed" : "fullscreen");
#ifdef _WIN32
    applyTouchFeedback(window, userSettings.hideTouchFeedback);
#endif
    bootLog("window created");
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
        std::fprintf(stderr, "OpenGL 3.3 core unavailable: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);
    bootLog("gl context");
    // Paint the very first frame before any asset is loaded. Without it
    // Windows shows an unpainted window - and after a swap the compositor has
    // something to display even if the load below takes a while. The image
    // splash clears to fully transparent (see SDL_WINDOW_TRANSPARENT above).
    if (splashStyle == 0) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    } else {
        glClearColor(0.03f, 0.03f, 0.05f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(window);
#ifdef _WIN32
    // DWM: negative margins extend the glass over the whole client area, which
    // makes every pixel whose alpha < 255 blend with whatever is behind the
    // window. Loaded dynamically - the toolchain has no Windows SDK libs.
    if (splashStyle == 0) {
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        if (SDL_GetWindowWMInfo(window, &wmi) && wmi.subsystem == SDL_SYSWM_WINDOWS) {
            struct Margins { int left, right, top, bottom; };
            using DwmExtendFn = long(__stdcall*)(HWND, const Margins*);
            if (HMODULE dwm = LoadLibraryA("dwmapi.dll")) {
                if (auto extend = reinterpret_cast<DwmExtendFn>(
                        reinterpret_cast<void*>(GetProcAddress(dwm, "DwmExtendFrameIntoClientArea"))); extend) {
                    const Margins full{-1, -1, -1, -1};
                    extend(wmi.info.win.window, &full);
                }
                FreeLibrary(dwm);
            }
        }
    }
#endif

    std::string error;
    platform::Renderer renderer;

    // ImGui is created before the asset loads so the splash screen can draw a
    // frame between each loading stage. Init itself costs ~1 ms and nothing
    // else touches ImGui until the UI fonts are loaded below.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    bootLog("imgui");
    // Splash frames are few and each one only costs a GPU submission - but
    // with vsync on every SDL_GL_SwapWindow would still block for a full
    // vblank, so run them unsynced and restore vsync once loading is done.
    SDL_GL_SetSwapInterval(0);

    // Static-image splash (splashStyle 0): load assets\splashscreen.png now,
    // while the GL context is live. Failure just falls back to the classic
    // splash. Size is queried from the GL texture to keep the aspect ratio.
    GLuint splashImg = 0;
    int splashImgW = 0, splashImgH = 0;
    if (splashStyle == 0) {
        splashImg = renderer.loadUiTexture(baseDir + "assets\\splashscreen.png", error);
        if (splashImg != 0) {
            glBindTexture(GL_TEXTURE_2D, splashImg);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &splashImgW);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &splashImgH);
            glBindTexture(GL_TEXTURE_2D, 0);
        } else {
            std::fprintf(stderr, "warning: splashscreen.png missing, using classic splash (%s)\n",
                error.c_str());
            error.clear();
        }
    }

    // Draws one splash frame between loading stages. Classic style (1): dark
    // background, title, the stage label and a thin progress bar. Image style
    // (0): just splashscreen.png centered on the dark background. Both use
    // ImGui's default font (ASCII only - the CJK UI atlas is built later by
    // loadIntroFonts()).
    bool splashShown = false;
    auto drawSplash = [&](float progress, const char* label) {
        splashShown = true;
        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        if (splashImg != 0 && splashImgW > 0 && splashImgH > 0) {
            // Free-floating PNG: drawn at its native size, centered. No
            // background fill at all - the window is DWM-transparent, so the
            // picture's alpha shows the desktop directly.
            const ImVec2 p0(0.5f * (io.DisplaySize.x - splashImgW),
                0.5f * (io.DisplaySize.y - splashImgH));
            dl->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(splashImg)),
                p0, ImVec2(p0.x + splashImgW, p0.y + splashImgH));
        } else {
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, IM_COL32(11, 12, 17, 255));
        const ImVec2 center(0.5f * io.DisplaySize.x, 0.44f * io.DisplaySize.y);
        ImFont* font = ImGui::GetFont();
        const char* title = "CppSekai";
        const float titleSize = 34.0f;
        const ImVec2 ts = font->CalcTextSizeA(titleSize, 4096.0f, 0.0f, title);
        dl->AddText(font, titleSize, ImVec2(center.x - 0.5f * ts.x, center.y - 0.5f * ts.y),
            IM_COL32(235, 238, 245, 255), title);
        const float labelSize = 14.0f;
        const ImVec2 ls = font->CalcTextSizeA(labelSize, 4096.0f, 0.0f, label);
        dl->AddText(font, labelSize, ImVec2(center.x - 0.5f * ls.x, center.y + 0.5f * ts.y + 18.0f),
            IM_COL32(150, 155, 170, 255), label);
        const float barW = std::min(420.0f, 0.5f * io.DisplaySize.x);
        const float barH = 6.0f;
        const ImVec2 b0(center.x - 0.5f * barW, center.y + 0.5f * ts.y + 46.0f);
        const ImVec2 b1(b0.x + barW, b0.y + barH);
        dl->AddRectFilled(b0, b1, IM_COL32(255, 255, 255, 26), 3.0f);
        const float fill = barW * progress;
        if (fill >= 1.0f) {
            dl->AddRectFilled(b0, ImVec2(b0.x + fill, b1.y), IM_COL32(64, 224, 188, 255), 3.0f);
        }
        } // classic splash branch
        ImGui::Render();
        int drawableW = 0, drawableH = 0;
        SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
        glViewport(0, 0, drawableW, drawableH);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    };
    drawSplash(0.05f, "initializing");

    // Resolve bundled assets relative to the executable, not the CWD.
    const std::string assetDir = baseDir + "assets\\mmw";
    const std::string overlayDir = baseDir + "assets\\mmw\\overlay";
    const std::string fontDir = baseDir + "assets\\mmw\\font";
    const std::string seDir = baseDir + "assets\\se";
    // The result screen loops the game's own result track (see game/Result.cpp).
    const std::string resultBgmPath = baseDir + "assets\\ost\\BGM_LIVE_RESULT_2.mp3";
    const std::string fxDir = baseDir + "assets\\fx";
    // Where the charts live: next to the exe when packaged, otherwise the
    // project's charts/ one level up (the usual build/ layout).
    std::vector<std::string> chartCandidates;
    if (!chartsDir.empty()) {
        chartCandidates.push_back(chartsDir);
    } else {
        chartCandidates.push_back(baseDir + "charts");
        chartCandidates.push_back(baseDir + "..\\charts");
        chartCandidates.push_back(std::string("charts"));
    }

    if (!renderer.init(windowW, windowH, error)) {
        std::fprintf(stderr, "renderer init failed: %s\n", error.c_str());
        return 1;
    }
    bootLog("renderer init");
    // Load just the stage first and show it: from here on the window shows
    // the pjsk background instead of a black rectangle while the rest of the
    // textures and the CJK font atlas are being built.
    if (renderer.loadSplash(assetDir, error)) {
        // The splash covers the whole load; the stage only becomes visible
        // when the main loop starts drawing.
        drawSplash(0.30f, "loading stage");
        bootLog("stage textures");
    } else {
        std::fprintf(stderr, "warning: splash load failed: %s\n", error.c_str());
        error.clear();
    }
    if (!renderer.loadAssets(assetDir, error)) {
        std::fprintf(stderr, "asset load failed: %s\n", error.c_str());
        return 1;
    }
    bootLog("assets");
    drawSplash(0.60f, "loading textures");
    // Both modes draw the core's own note-hit effects (assets/mmw/effect.png
    // driven by the embedded pjsk effect definitions). Autoplay lets the chart
    // timeline fire them; player mode turns that off and fires them from
    // game/Judgement, so a burst only shows for notes that were actually hit.
    renderer.setDrawCoreEffects(true);

    if (!renderer.loadHud(overlayDir, error)) {
        std::fprintf(stderr, "warning: HUD load failed: %s\n", error.c_str());
        error.clear();
    }
    bootLog("hud textures");
    drawSplash(0.75f, "loading hud");
    // Dialog close X (dark cross on transparent, assets/mmw/ui/close.png).
    if (const platform::Renderer::HudSprite* closeSprite = renderer.hud("ui_close"); closeSprite != nullptr && closeSprite->id != 0) {
        ui::setCloseTexture(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(closeSprite->id)));
    }

    // PJSK style tap feedback (expanding ring + triangles) on every screen
    // except the play state. A missing asset only disables the effect.
    game::TapEffect tapEffect;

    // Tap feedback textures (assets/fx/tap_*.png). Failure is not fatal.
    if (!tapEffect.load(renderer, fxDir, error)) {
        std::fprintf(stderr, "warning: tap effect disabled (%s)\n", error.c_str());
        error.clear();
    }
    bootLog("tap effect");

    // Ring behind the resume-countdown numbers (same tap_ring.png). Failure
    // is not fatal - the numbers draw without the halo.
    ImTextureID countdownRing = 0;
    if (const GLuint ringId = renderer.loadUiTexture(fxDir + "\\tap_ring.png", error); ringId != 0) {
        countdownRing = reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(ringId));
    }
    error.clear();

    // ------------------------------------------------------------------
    // Chart core
    // ------------------------------------------------------------------
    core_api::init();
    core_api::resize(windowW, windowH, 1.0f);
    // Player mode drives the core's hit effects from the judgement engine
    // instead of letting the chart timeline fire them (autoplay).
    core_api::setEffectAutoplay(autoPlay);
    bootLog("chart core");
    drawSplash(0.85f, "preparing");

    // ------------------------------------------------------------------
    // Audio
    // ------------------------------------------------------------------
    platform::AudioEngine audio;
    if (!audio.init(error)) {
        std::fprintf(stderr, "audio init failed: %s\n", error.c_str());
        return 1;
    }
    bootLog("audio device");
    if (!audio.loadSe(seDir, error)) {
        std::fprintf(stderr, "warning: SE load failed: %s\n", error.c_str());
        error.clear();
    }
    bootLog("audio");
    drawSplash(0.92f, "loading audio");

    // ------------------------------------------------------------------
    // Judgement
    // ------------------------------------------------------------------
    game::JudgementEngine judgement;

    // Apply the persisted judgement settings before anything is judged.
    {
        game::JudgementWindows windows;
        windows.perfectMs = userSettings.perfectMs;
        windows.greatMs = userSettings.greatMs;
        windows.goodMs = userSettings.goodMs;
        windows.missAfterMs = userSettings.goodMs + 60.0f;
        windows.badMs = windows.missAfterMs; // BAD closes exactly where the auto-miss starts
        judgement.setWindows(windows);
        judgement.setStrictFlick(userSettings.strictFlick);
    }

    // Play results (cleared / full combo) + song select UI assets.
    // setSelectAssetDir expects the assets root; SongSelect appends "select\\".
    // `scores` was already loaded from userdata.json near the top of main().
    game::setSelectAssetDir(baseDir + "assets");

    // ------------------------------------------------------------------
    // Song-select backdrop: the user's desktop wallpaper, blurred, when the
    // setting asks for it. The GL context is live here and re-loading only
    // happens when the setting changes, so the boot stays fast for everyone
    // who keeps the built-in gradient.
    // ------------------------------------------------------------------
    GLuint backdropTex = 0;
    int backdropW = 0;
    int backdropH = 0;
    std::string backdropKey;
    bool backdropReported = false;
    auto refreshSelectBackdrop = [&]() {
        if (userSettings.bgStyle != 1) {
            game::setSelectBackdrop(0, 0, 0, 0.0f);
            return;
        }
#ifdef _WIN32
        const std::string path = windowsWallpaperPath();
#else
        const std::string path;
#endif
        if (path.empty()) {
            if (!backdropReported) {
                backdropReported = true;
                std::printf("[bg] no desktop wallpaper found, keeping the built-in background\n");
            }
            game::setSelectBackdrop(0, 0, 0, 0.0f);
            return;
        }
        const std::string key = path + "|" + std::to_string(userSettings.bgBlur);
        if (key != backdropKey) {
            std::string bgError;
            const GLuint tex = renderer.loadBackdropTexture(path, userSettings.bgBlur, backdropW,
                backdropH, bgError);
            if (tex != 0) {
                if (backdropTex != 0) {
                    glDeleteTextures(1, &backdropTex);
                }
                backdropTex = tex;
                backdropKey = key;
                std::printf("[bg] wallpaper '%s' -> %dx%d texture (blur %.2f)\n", path.c_str(),
                    backdropW, backdropH, static_cast<double>(userSettings.bgBlur));
                std::fflush(stdout);
            } else {
                std::printf("[bg] %s\n", bgError.c_str());
                std::fflush(stdout);
                game::setSelectBackdrop(0, 0, 0, 0.0f);
                return;
            }
        }
        game::setSelectBackdrop(backdropTex, backdropW, backdropH, userSettings.bgDim);
    };
    refreshSelectBackdrop();

    // Official per-difficulty levels (see game::loadMusicLevels). unipjsk
    // scores ship with an empty "#PLAYLEVEL", so without this the song select
    // can only show "-" on every difficulty pad.
    for (const std::string& candidate :
        {baseDir + "music-levels.json", baseDir + "..\\music-levels.json",
            std::string("music-levels.json")}) {
        std::ifstream probe(candidate, std::ios::binary);
        if (probe.good()) {
            probe.close();
            game::loadMusicLevels(candidate);
            break;
        }
    }

    // Official readings + titles (musics.json): "sort by name" and the aiueo
    // grouping use the reading, and the title fills in charts whose #TITLE is
    // empty (unipjsk writes none, so the list would read "0018 master").
    // Optional - a missing file just means title-based sorting.
    for (const std::string& candidate :
        {baseDir + "musics.json", baseDir + "..\\musics.json", std::string("musics.json")}) {
        std::ifstream probe(candidate, std::ios::binary);
        if (probe.good()) {
            probe.close();
            game::loadMusicMaster(candidate);
            break;
        }
    }

    // ------------------------------------------------------------------
    // UI fonts
    // ------------------------------------------------------------------
    game::loadIntroFonts(fontDir, useSystemFont);
    bootLog("fonts");
    // The splash frames rendered with the default font atlas, so the GL
    // backend still owns that old font texture. Drop it - the next NewFrame
    // rebuilds it from the real UI fonts (without this, glyph UVs sample the
    // stale texture and every label garbles).
    if (splashShown) {
        ImGui_ImplOpenGL3_DestroyDeviceObjects();
        SDL_GL_SetSwapInterval(1); // splash frames ran vsync-free; back to vsync
        drawSplash(1.0f, "ready");
        // The image splash forced the window frameless (see windowFlags).
        // Loading is done, so hand the window over to the configured mode:
        // restore the frame for windowed, or finally go fullscreen (deferred
        // on purpose - see SDL_CreateWindow above: a window covering the whole
        // desktop cannot stay DWM-transparent, so the boot splash must never
        // be fullscreen).
        if (splashStyle == 0) {
            if (windowMode == 1) {
                SDL_SetWindowBordered(window, SDL_TRUE);
            } else if (windowMode == 2) {
                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
        }
    }

    // ------------------------------------------------------------------
    // Windows system media integration (SMTC + taskbar progress)
    // ------------------------------------------------------------------
    platform::SystemMedia systemMedia;
    systemMedia.init(window);
    if (!userSettings.reportSmtc) {
        // Turned off in the settings: never become the active media session,
        // so Windows keeps showing whatever it showed before.
        systemMedia.setReporting(false);
    }

    // ------------------------------------------------------------------
    // Song list / session
    // ------------------------------------------------------------------
    // Pick the first candidate folder that actually contains a chart.
    std::vector<game::ChartEntry> entries;
    for (const std::string& candidate : chartCandidates) {
        entries = game::scanChartFolder(candidate);
        if (!entries.empty()) {
            chartsDir = candidate;
            break;
        }
    }
    if (chartsDir.empty()) {
        chartsDir = chartCandidates.front();
    }
    bootLog("chart scan");
    std::printf("[select] %d chart(s) under %s\n", static_cast<int>(entries.size()), chartsDir.c_str());
    if (entries.empty()) {
        std::printf("[select] no .sus found. Looked in:\n");
        for (const std::string& candidate : chartCandidates) {
            std::printf("[select]   %s\n", candidate.c_str());
        }
    }
    game::applyScores(entries, scores);
    int selected = entries.empty() ? -1 : 0;
    std::string loadedCoverPath;

    AppState state = susPath.empty() ? AppState::Select : AppState::Play;
    Session session;
    bool beginSessionClockPending = false;
    bool restartDone = false; // --test-restart bookkeeping
    int restartIndex = 0;

    // Reports the current song to Windows (SMTC) and sizes the taskbar bar.
    double trackDurationSec = 0.0;
    auto announceTrack = [&]() {
        if (!session.active) {
            return;
        }
        const double musicLen = audio.musicDurationSec();
        double duration = 0.0;
        if (musicLen > 0.0) {
            duration = musicLen - audio.musicStartPos() - audio.userOffset();
        }
        if (duration <= 1.0) {
            duration = core_api::getChartEndTimeSec();
        }
        trackDurationSec = std::max(0.0, duration);
        if (userSettings.reportSmtc) {
            systemMedia.setTrack(session.intro.title, session.entry.artist, trackDurationSec,
                session.entry.coverPath);
        }
    };
    if (state == AppState::Play) {
        game::ChartEntry entry;
        entry.susPath = susPath;
        entry.bgmPath = bgmPath;
        entry.coverPath = coverPath;
        game::resolveSidecars(entry); // picks up 0075.mp3 / jacket next to the chart
        if (!startSession(session, entry, renderer, audio, judgement, noteSpeed, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        if (offsetGiven) {
            audio.setUserOffset(gUserOffsetSec);
        }
        announceTrack();
        beginSessionClockPending = true;
    }
    bootLog("ready (first frame up)");

    // ------------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------------
    // The opening card runs for a fixed 4s and the stage needs another 1.8s
    // to fade in, so the lead-in can never be shorter than that.
    const double leadInSec = std::max(leadIn, static_cast<double>(game::kMinLeadInSec));
    bool paused = false;
    bool pauseDialogOpen = false; // pjsk style pause dialog (重试/放弃/继续演出)
    bool running = true;
    bool fullscreen = false;
    bool showDebug = showSettingsShot; // H / the musicsetting button on the song select
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 perfStart = SDL_GetPerformanceCounter();

    // Clock without BGM (auto mode / missing audio): wall clock.
    auto wallSongTime = [&]() {
        return -leadInSec + static_cast<double>(SDL_GetPerformanceCounter() - perfStart) / static_cast<double>(perfFreq);
    };
    auto beginSessionClock = [&]() {
        perfStart = SDL_GetPerformanceCounter();
        audio.start(leadInSec);
    };

    std::vector<TouchTrack> touches;
    bool keyHeld[12] = {};
    std::array<float, LANE_COUNT> lanePress{};
    std::array<float, LANE_COUNT> laneHover{};
    double lastFrameDeltaSec = 0.0;
    Uint64 lastFrameCounter = SDL_GetPerformanceCounter();
    SDL_Event event;
    double uiClock = 0.0;
    int fpsLimitLive = fpsLimit; // adjustable from the debug panel
    const double perfFreqD = static_cast<double>(perfFreq);
    // Refresh rate of the monitor the window is on: with vsync on, a frame
    // cap above this is physically impossible (presentation quantizes to
    // refresh intervals) - the fps slider disables vsync instead.
    int displayRefreshHz = 60;
    {
        SDL_DisplayMode dm;
        if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &dm) == 0 && dm.refresh_rate > 0) {
            displayRefreshHz = dm.refresh_rate;
        }
    }
    bool vsyncActive = true;

    // Snapshot everything userdata.json stores and write it out. Called right
    // after a result is recorded and once on exit, so settings and play results
    // survive a restart. Scores are keyed by chart file name, so copying this
    // one file next to a re-downloaded charts/ restores the records.
    auto persistUserData = [&]() {
        userSettings.noteSpeed = noteSpeed;
        userSettings.seVolume = seVolume;
        userSettings.leadInSec = leadIn;
        userSettings.windowMode = windowMode;
        userSettings.windowWidth = resW;
        userSettings.windowHeight = resH;
        userSettings.fpsLimit = fpsLimitLive;
        userSettings.offsetSec = gUserOffsetSec;
        // The saved value is the *UI* preference: a command-line --auto only
        // applies to this run, otherwise previewing a chart from a shell would
        // silently leave autoplay on for the next normal launch.
        if (!autoplayGiven) {
            userSettings.autoplay = autoPlay;
        }
        userSettings.showProgressBar = showProgressBar;
        userSettings.hideTouchFeedback = hideTouchFeedback;
        const game::JudgementWindows& w = judgement.windows();
        userSettings.perfectMs = w.perfectMs;
        userSettings.greatMs = w.greatMs;
        userSettings.goodMs = w.goodMs;
        userSettings.strictFlick = judgement.strictFlick();
        game::saveUserData(userDataFile, userSettings, scores);
    };

    // HUD / hit feedback state (shared with the input handlers below).
    game::HudState hudState;
    float lastSeenJudgeTime = -100.0f;
    bool wantScreenshot = false;

    // Result screen state: the snapshot it draws, when it appeared (drives the
    // entrance animation) and the previous best score of the chart being
    // played (captured *before* the new record is written).
    game::ResultData resultData;
    double resultShownAt = 0.0;
    bool resultScheduled = false;
    double resultPreviousBest = 0.0;
    // Set by the mouse / touch handlers when 继续 was pressed (see
    // resultContinueHitTest): the result screen is not an ImGui window, so it
    // is hit-tested in the SDL event path like the HUD pause button.
    bool resultContinueRequested = false;
    // Set by the mouse handler when the HUD pause button was clicked, so the
    // same click is not also treated as a lane hit.
    bool pauseClickRequested = false;

    // ------------------------------------------------------------------
    // Resume countdown: continuing from the pause dialog shows 3-2-1 with an
    // expanding ring. The clock stays paused until the countdown finishes, so
    // the field is frozen and no input gets through (paused stays true).
    // ------------------------------------------------------------------
    bool countdownActive = false;
    double countdownStartClock = 0.0;
    int countdownNumberShown = -1;

    // Damage vignette: brief dark inner shadow around the screen edges on
    // life loss (BAD / MISS / broken hold), a constant dark state at 0 life -
    // same feedback the original game gives.
    float lastSeenLife = game::kMaxLife;
    float damageVignette = 0.0f;
    auto beginResumeCountdown = [&]() {
        pauseDialogOpen = false; // dialog closes (animation) behind the numbers
        countdownActive = true;
        countdownStartClock = uiClock;
        countdownNumberShown = -1;
    };

    // ------------------------------------------------------------------
    // Settings card ("debug panel"), shared by the song select and the
    // play states. Opened with H or the musicsetting button; alive flag
    // keeps it on screen while the close animation plays out.
    // ------------------------------------------------------------------
    auto drawSettingsCard = [&]() {
        static bool settingsAlive = false;
        if (showDebug) {
            settingsAlive = true;
        }
        if (!settingsAlive) {
            return;
        }
        // pjsk style settings panel (tabbed card, pjsk sliders).
        const float s = ui::scale();
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        // 640 tall (was 520): the 画面 tab grew a resolution combo and the
        // progress-bar checkbox and no longer fit the shorter card.
        ImVec2 cardSize = ImVec2(360.0f * s, 640.0f * s);
        ImVec2 cardCenter = ImVec2(18.0f * s + cardSize.x * 0.5f, 18.0f * s + cardSize.y * 0.5f);
        const float interior = cardSize.x - 56.0f * s;
        const float padX = 28.0f * s;
        bool closeClicked = false;
        if (ui::beginCard("##settings", &cardCenter, &cardSize, true, false, &closeClicked, showDebug)) {
            if (closeClicked) {
                showDebug = false;
            }
            ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - cardSize.x * 0.5f + padX,
                cardCenter.y - cardSize.y * 0.5f + 16.0f * s));
            ui::cardTitle("设置", interior);

            static int tab = settingsTabShot >= 0 ? settingsTabShot : 0;
            ui::tabBar("settings-tabs",
                {std::string("演奏"), std::string("画面"), std::string("判定"), std::string("系统")},
                &tab, interior);

            // ImGui::Text starts each line at the window's left edge
            // (padding is 0); pin content lines to the card interior.
            const auto contentLeft = [&]() {
                ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - cardSize.x * 0.5f + padX,
                    ImGui::GetCursorScreenPos().y));
            };

            ImGui::PushStyleColor(ImGuiCol_Text, ui::kBodyText);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(250, 250, 253, 255));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(224, 224, 235, 255));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(214, 214, 228, 255));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(205, 205, 222, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f * s);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * s, 6.0f * s));
            ImGui::PushFont(game::bodyFont(), 23.0f * s);
            ImGui::PushItemWidth(interior);

            // Everything below the tab bar lives in a clipping child: the 画面
            // tab is taller than the card, and a row that runs past the bottom
            // ends up underneath the 关闭 button (submitted later, so it eats
            // the clicks). Scrolling here keeps every row reachable.
            const float contentTop = ImGui::GetCursorScreenPos().y;
            const float contentBottom = cardCenter.y + cardSize.y * 0.5f - 74.0f * s;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - cardSize.x * 0.5f, contentTop));
            ImGui::BeginChild("##tabcontent",
                ImVec2(cardSize.x, std::max(40.0f * s, contentBottom - contentTop)),
                ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
            ImGui::PopStyleVar();
            ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - cardSize.x * 0.5f + padX, contentTop));

            if (tab == 0) {
                // 演奏: audio offset + note speed.
                contentLeft();
                ImGui::Text("音频偏移");
                static float offsetMs = static_cast<float>(gUserOffsetSec * 1000.0);
                contentLeft();
                if (ui::slider("offset", &offsetMs, -2000.0f, 2000.0f, 5.0f, "%+.0f ms", interior)) {
                    audio.setUserOffset(static_cast<double>(offsetMs) / 1000.0);
                }
                contentLeft();
                ImGui::Text("音符速度");
                float speed = noteSpeed;
                contentLeft();
                if (ui::slider("speed", &speed, 1.0f, 12.0f, 0.1f, "%.1f", interior)) {
                    noteSpeed = speed;
                    core_api::setPreviewConfig(0, 1, 1, 1, 0, 0, noteSpeed, 1.0f, 0.6f, 0.0f, 1.0f, 0.85f);
                }
            } else if (tab == 1) {
                // 画面: resolution + window mode + frame rate.
                contentLeft();
                ImGui::Text("分辨率");
                // Preset sizes; the live window resizes immediately when not
                // in fullscreen (there the desktop size wins until exit).
                static constexpr int kResW[5] = {1280, 1366, 1600, 1920, 2560};
                static constexpr int kResH[5] = {720, 768, 900, 1080, 1440};
                static int resIdx = [](int w, int h) {
                    for (int i = 0; i < 5; ++i) {
                        if (kResW[i] == w && kResH[i] == h) {
                            return i;
                        }
                    }
                    return 1;
                }(resW, resH);
                contentLeft();
                ImGui::SetNextItemWidth(interior);
                if (ImGui::Combo("##resolution", &resIdx,
                        "1280 x 720\0" "1366 x 768\0" "1600 x 900\0" "1920 x 1080\0" "2560 x 1440\0")) {
                    resW = kResW[resIdx];
                    resH = kResH[resIdx];
                    if (windowMode != 2) {
                        SDL_SetWindowSize(window, resW, resH);
                        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                    }
                }
                contentLeft();
                ImGui::Text("窗口模式");
                static int winMode = windowMode;
                contentLeft();
                ImGui::SetNextItemWidth(interior);
                if (ImGui::Combo("##window-mode", &winMode, "borderless\0windowed\0fullscreen\0")) {
                    windowMode = winMode;
                    if (winMode == 2) {
                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    } else {
                        SDL_SetWindowFullscreen(window, 0);
                        SDL_SetWindowBordered(window, winMode == 1 ? SDL_TRUE : SDL_FALSE);
                    }
                }
                contentLeft();
                ImGui::Text("帧率上限 (0 = 仅垂直同步)");
                static float fpsLimitF = static_cast<float>(fpsLimit);
                contentLeft();
                if (ui::slider("fps", &fpsLimitF, 0.0f, 240.0f, 5.0f, "%.0f fps", interior)) {
                    fpsLimitLive = static_cast<int>(fpsLimitF);
                }
                contentLeft();
                ImGui::Text("实测: %.1f fps", 1.0 / std::max(1e-6, lastFrameDeltaSec));
                contentLeft();
                ImGui::Text("显示器刷新率 %d Hz；超过刷新率会自动关垂直同步", displayRefreshHz);
                contentLeft();
                bool showProgressBox = showProgressBar;
                ui::checkBox("显示播放进度条", &showProgressBox, interior);
                if (showProgressBox != showProgressBar) {
                    showProgressBar = showProgressBox;
                    persistUserData();
                }
                contentLeft();
                // Splash style: static image (0) vs classic progress bar (1).
                // Only read at startup, so a change takes effect next launch.
                bool classicSplashBox = userSettings.splashStyle != 0;
                ui::checkBox("经典开屏 (标题+进度条)", &classicSplashBox, interior);
                if (classicSplashBox != (userSettings.splashStyle != 0)) {
                    userSettings.splashStyle = classicSplashBox ? 1 : 0;
                    persistUserData();
                }
                contentLeft();
                // Song-select background: the built-in gradient or the user's
                // desktop wallpaper (blurred + dimmed).
                ImGui::Text("选曲背景");
                contentLeft();
                static int bgMode = userSettings.bgStyle;
                ImGui::SetNextItemWidth(interior);
                if (ImGui::Combo("##bgstyle", &bgMode, "默认渐变\0桌面壁纸\0")) {
                    userSettings.bgStyle = bgMode;
                    refreshSelectBackdrop();
                    persistUserData();
                }
                if (userSettings.bgStyle == 1) {
                    // Re-blurring the picture is a CPU pass over a decoded
                    // wallpaper, so it runs when the slider is let go, not on
                    // every frame of the drag.
                    static bool blurPending = false;
                    contentLeft();
                    float bgBlurV = userSettings.bgBlur;
                    if (ui::slider("背景模糊", &bgBlurV, 0.0f, 1.0f, 0.05f, "%.2f", interior)) {
                        userSettings.bgBlur = bgBlurV;
                        blurPending = true;
                    }
                    if (blurPending && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        blurPending = false;
                        refreshSelectBackdrop();
                        persistUserData();
                    }
                    contentLeft();
                    float bgDimV = userSettings.bgDim;
                    if (ui::slider("背景变暗", &bgDimV, 0.0f, 1.0f, 0.02f, "%.2f", interior)) {
                        userSettings.bgDim = bgDimV;
                        // Cheap: only the overlay drawn on top changes.
                        game::setSelectBackdrop(backdropTex, backdropW, backdropH, bgDimV);
                        persistUserData();
                    }
                }
                contentLeft();
                bool hideTouchBox = hideTouchFeedback;
                ui::checkBox("隐藏系统触摸特效", &hideTouchBox, interior);
                if (hideTouchBox != hideTouchFeedback) {
                    hideTouchFeedback = hideTouchBox;
#ifdef _WIN32
                    applyTouchFeedback(window, hideTouchFeedback);
#endif
                    persistUserData();
                }
                contentLeft();
                // checkBox returns the *new* value, so gate on a real change -
                // gating on the return value made the box impossible to untick.
                bool autoPlayBox = autoPlay;
                ui::checkBox("AUTOPLAY 谱面预览", &autoPlayBox, interior);
                if (autoPlayBox != autoPlay) {
                    autoPlay = autoPlayBox;
                    userSettings.autoplay = autoPlayBox;
                    persistUserData();
                }
            } else if (tab == 2) {
                // 判定: judgement windows.
                contentLeft();
                ImGui::Text("判定窗口 (ms)");
                static float perfect = judgement.windows().perfectMs;
                static float great = judgement.windows().greatMs;
                static float good = judgement.windows().goodMs;
                bool windowsChanged = false;
                contentLeft();
                windowsChanged |= ui::slider("perfect", &perfect, 10.0f, 100.0f, 1.0f, "Perfect %.0f", interior);
                contentLeft();
                windowsChanged |= ui::slider("great", &great, 20.0f, 160.0f, 1.0f, "Great %.0f", interior);
                contentLeft();
                windowsChanged |= ui::slider("goodw", &good, 30.0f, 220.0f, 1.0f, "Good %.0f", interior);
                static bool strictFlick = judgement.strictFlick();
                ui::checkBox("严格 Flick 方向", &strictFlick, interior);
                judgement.setStrictFlick(strictFlick);
                if (windowsChanged) {
                    game::JudgementWindows windows;
                    windows.perfectMs = perfect;
                    windows.greatMs = std::max(great, perfect + 10.0f);
                    windows.goodMs = std::max(good, great + 10.0f);
                    windows.missAfterMs = good + 60.0f;
                    // Keep BAD closing exactly where the auto-miss window starts,
                    // otherwise the extra tier silently disappears.
                    windows.badMs = windows.missAfterMs;
                    judgement.setWindows(windows);
                }
            } else {
                // 系统: how the game behaves towards Windows and the desktop.
                contentLeft();
                bool autoPauseBox = userSettings.autoPauseOnBlur;
                ui::checkBox("失焦时自动暂停", &autoPauseBox, interior);
                if (autoPauseBox != userSettings.autoPauseOnBlur) {
                    userSettings.autoPauseOnBlur = autoPauseBox;
                    persistUserData();
                }
                contentLeft();
                bool smtcBox = userSettings.reportSmtc;
                ui::checkBox("启用 SMTC 汇报", &smtcBox, interior);
                if (smtcBox != userSettings.reportSmtc) {
                    userSettings.reportSmtc = smtcBox;
                    // Off: stop the media session right away instead of leaving
                    // the flyout with a stale song. On: re-announce, otherwise
                    // nothing shows up until the next song starts.
                    systemMedia.setReporting(smtcBox);
                    if (smtcBox) {
                        announceTrack();
                    }
                    persistUserData();
                }
            }
            // End on an item: the checkbox helper leaves the cursor at the row
            // bottom with a bare SetCursorScreenPos, which trips ImGui's
            // "don't extend the parent with SetCursorPos" check when the child
            // is the last window in the frame.
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
            ImGui::EndChild();
            ImGui::PopFont();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(5);
            ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - cardSize.x * 0.5f + padX,
                cardCenter.y + cardSize.y * 0.5f - 68.0f * s));
            if (ui::capsuleButton("关闭", ImVec2(150.0f * s, 54.0f * s), false)) {
                showDebug = false;
            }
            ui::endCard();
        } else {
            settingsAlive = false;
        }
    };

    // ------------------------------------------------------------------
    // Pointer input: touch fingers and mouse buttons share one code path.
    // Mouse pointers get negative ids so they never collide with SDL fingers.
    // ------------------------------------------------------------------
    auto pointerIdForButton = [](Uint8 button) -> SDL_FingerID {
        return button == SDL_BUTTON_RIGHT ? -2 : -1;
    };

    // Window pixel -> 1920x1080 virtual HUD space (letterboxed, like the HUD).
    auto hudPoint = [&](int x, int y, float& outVx, float& outVy) {
        const float hudScale = std::min(static_cast<float>(windowW) / 1920.0f,
            static_cast<float>(windowH) / 1080.0f);
        const float hudOffX = (static_cast<float>(windowW) - 1920.0f * hudScale) * 0.5f;
        const float hudOffY = (static_cast<float>(windowH) - 1080.0f * hudScale) * 0.5f;
        outVx = (static_cast<float>(x) - hudOffX) / hudScale;
        outVy = (static_cast<float>(y) - hudOffY) / hudScale;
    };

    auto isPauseButton = [&](int x, int y) {
        float vx = 0.0f;
        float vy = 0.0f;
        hudPoint(x, y, vx, vy);
        const game::HudRect rect = game::lifePauseRect();
        return vx >= rect.x && vx <= rect.x + rect.w && vy >= rect.y && vy <= rect.y + rect.h;
    };

    // Press inside the HUD pause button zone. Returns true when the press was
    // consumed (so it must never also count as a lane hit).
    //
    // Deliberately shared by the mouse path, the touch-synthesized mouse path
    // and the finger path: the earlier copy-pasted versions were what made the
    // button behave differently per input device. Note this runs *before* the
    // "ignore input while paused / playing a preview" guards - the button is
    // not lane input and has to work wherever the HUD is on screen (including
    // autoplay previews, whose HUD is drawn the same way).
    auto hudPausePress = [&](int x, int y) -> bool {
        if (state != AppState::Play || !isPauseButton(x, y)) {
            return false;
        }
        if (paused || pauseDialogOpen || countdownActive) {
            std::printf("[pause] press ignored (paused=%d dialog=%d countdown=%d)\n",
                paused ? 1 : 0, pauseDialogOpen ? 1 : 0, countdownActive ? 1 : 0);
            std::fflush(stdout);
            return true;
        }
        const double pressSongTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
        const float visibility = game::openingPlayfieldVisibility(
            static_cast<float>(pressSongTime + leadInSec), session.intro.hasContent);
        if (visibility <= 0.0f) {
            // The HUD is not drawn yet (opening card): the press lands on
            // empty screen, so there is nothing to react to. Say so, otherwise
            // a "dead" button is invisible in the log.
            std::printf("[pause] press ignored (HUD hidden behind the opening card)\n");
            std::fflush(stdout);
            return true;
        }
        pauseClickRequested = true;
        return true;
    };

    // Judges a flick gesture, trying the lane the press started on first and
    // the lane the finger was resting on second. The second try is what makes
    // the tail flick of a sideways-sliding hold work: its tail note sits on
    // the lane the hold ENDED on (the core emits a marker at the start lane
    // carrying the end time, and a separate tap/flick event at the end lane),
    // so a swipe at the press lane covers nothing. Before this, the only way
    // to clear such a tail was to release the hold and swipe again, because
    // the new touch read its lane from the current finger position.
    auto flickJudge = [&](const TouchTrack& track, double songTime, game::FlickDir dir) -> game::Judge {
        const float t = static_cast<float>(songTime);
        const game::Judge result = judgement.flick(track.lanePos, t, dir, 0.8f);
        if (result != game::Judge::None || std::fabs(track.restLanePos - track.lanePos) < 0.01f) {
            return result;
        }
        return judgement.flick(track.restLanePos, t, dir, 0.8f);
    };

    // Starts a tap at a window position. Returns false when the press is
    // outside the playfield (e.g. on the sky above the horizon), where the
    // inverse perspective would map it to a bogus lane.
    auto beginPointer = [&](SDL_FingerID id, int x, int y, bool isTouch) {
        const float clipX = (static_cast<float>(x) / static_cast<float>(windowW)) * 2.0f - 1.0f;
        const float clipY = 1.0f - (static_cast<float>(y) / static_cast<float>(windowH)) * 2.0f;
        const float worldY = renderer.clipToWorldY(clipY);
        if (worldY < 0.06f || worldY > 1.6f) {
            return false;
        }
        // Undo the fake perspective: screen x = laneX * worldY.
        const float lanePos = std::abs(worldY) > 0.08f
            ? renderer.clipToWorldX(clipX) / worldY
            : renderer.clipToWorldX(clipX);
        if (lanePos < -6.5f || lanePos > 6.5f) {
            return false;
        }

        TouchTrack track;
        track.fingerId = id;
        track.isTouch = isTouch;
        track.lanePos = lanePos;
        track.restLanePos = lanePos;
        track.laneIndex = laneIndexFromPos(lanePos);
        track.lastLanePos = lanePos;
        track.lastScreenX = static_cast<float>(x);
        track.lastScreenY = static_cast<float>(y);
        track.lastMoveTimeSec = SDL_GetTicks() / 1000.0;
        touches.push_back(track);
        lanePress[static_cast<size_t>(track.laneIndex)] = 1.0f;
        const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
        const game::Judge result = judgement.tap(track.lanePos, static_cast<float>(songTime), false, 0.8f);
        if (result != game::Judge::None) {
            playHitSe(audio, judgement, seVolume);
        }
        return true;
    };

    // Tracks pointer movement; a fast swipe is a flick. The gesture is judged
    // in screen pixels per second (see flickDirFrom) so the up / left / right
    // decision does not depend on the perspective scaling of the playfield.
    auto movePointer = [&](SDL_FingerID id, int x, int y) {
        const float clipX = (static_cast<float>(x) / static_cast<float>(windowW)) * 2.0f - 1.0f;
        const float clipY = 1.0f - (static_cast<float>(y) / static_cast<float>(windowH)) * 2.0f;
        const double now = SDL_GetTicks() / 1000.0;
        const float worldY = renderer.clipToWorldY(clipY);
        const float heightScale = static_cast<float>(windowH) / 1080.0f;
        for (auto& track : touches) {
            if (track.fingerId != id) {
                continue;
            }
            const float lanePos = std::abs(worldY) > 0.08f
                ? renderer.clipToWorldX(clipX) / worldY
                : renderer.clipToWorldX(clipX);
            const float dx = static_cast<float>(x) - track.lastScreenX;
            const float dy = static_cast<float>(y) - track.lastScreenY; // screen: down is positive
            const double rawDt = now - track.lastMoveTimeSec;
            // See kFlickMaxSampleSec: a finger parked on a hold emits no motion
            // events, and the gap before the first sample of the flick must not
            // be used as the sample period - that alone made hold-tail flicks
            // measure ~100 px/s instead of ~800 and never register.
            const double dt = std::min(rawDt, kFlickMaxSampleSec);
            if (rawDt > kFlickIdleGapSec) {
                track.velUp = 0.0f;
                track.velSide = 0.0f;
            }
            if (dt > 0.001) {
                // Low-pass the per-event velocity: touch panels report
                // unevenly spaced position jumps and a single-frame delta
                // often under- or over-shoots a real flick.
                const float upSpeed = -dy / static_cast<float>(dt);
                const float sideSpeed = dx / static_cast<float>(dt);
                track.velUp = track.velUp * 0.35f + upSpeed * 0.65f;
                track.velSide = track.velSide * 0.35f + sideSpeed * 0.65f;
                // Distance travelled in the current direction: the counter
                // starts over whenever the movement reverses, so the jitter of
                // a resting finger never adds up while a deliberate swipe does.
                const float upDelta = -dy;
                track.travelUp = (upDelta >= 0.0f) == (track.travelUp >= 0.0f)
                    ? track.travelUp + upDelta
                    : upDelta;
                track.travelSide = (dx >= 0.0f) == (track.travelSide >= 0.0f)
                    ? track.travelSide + dx
                    : dx;
                if (std::abs(track.velUp) > std::abs(track.peakUp)) {
                    track.peakUp = track.velUp;
                }
                if (std::abs(track.velSide) > std::abs(track.peakSide)) {
                    track.peakSide = track.velSide;
                }
            }
            const game::FlickDir dir = flickDirFrom(track.velUp, track.velSide, track.travelUp,
                track.travelSide, track.isTouch, heightScale);
            if (dir != game::FlickNone && now - track.lastFlickFireTimeSec >= kFlickRefireSec) {
                track.lastFlickFireTimeSec = now;
                // Consume the gesture: resetting the travelled distance stops
                // one continuous swipe from firing on every motion event, while
                // the finger stays armed so a later, separate swipe fires again.
                track.travelUp = 0.0f;
                track.travelSide = 0.0f;
                const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
                const game::Judge result = flickJudge(track, songTime, dir);
                if (result != game::Judge::None) {
                    playHitSe(audio, judgement, seVolume);
                }
            } else if (std::abs(track.velUp) < kFlickRestSpeed * heightScale
                && std::abs(track.velSide) < kFlickRestSpeed * heightScale) {
                // Slow enough to count as parked: remember this lane. It is the
                // one a flick started from - a finger following a sliding hold
                // keeps it up to date, a swipe that is already under way does
                // not (its velocity is high), so the value cannot drift along
                // with the up-stroke's perspective skew.
                track.restLanePos = lanePos;
            }
            track.lastScreenX = static_cast<float>(x);
            track.lastScreenY = static_cast<float>(y);
            track.lastLanePos = lanePos;
            track.lastMoveTimeSec = now;
        }
    };

    auto endPointer = [&](SDL_FingerID id) {
        touches.erase(std::remove_if(touches.begin(), touches.end(),
                          [&](const TouchTrack& track) { return track.fingerId == id; }),
            touches.end());
    };

    // Debug (--result-preview): open the result screen before the first frame
    // with the numbers from the reference screenshot, so `--screenshot` can be
    // diffed against it. The song metadata still comes from the loaded chart.
    if (resultPreview) {
        resultData = buildResultData(session.intro, session.entry, judgement.stats(), 0.0,
            judgement.chartRating());
        if (resultData.title.empty()) {
            resultData.title = "1000年生きてる";
        }
        if (resultData.difficulty.empty()) {
            resultData.difficulty = "EXPERT";
        }
        if (resultData.level.empty()) {
            resultData.level = "23";
        }
        resultData.score = 940021.0;
        resultData.highScore = 0.0;
        resultData.newRecord = true;
        resultData.perfect = 634;
        resultData.great = 32;
        resultData.good = 4;
        resultData.bad = 2;
        resultData.miss = 1;
        resultData.maxCombo = 361;
        resultData.chartRating = 23.0f;
        beginSessionClockPending = false;
        audio.stopMusic();
        state = AppState::Result;
        resultShownAt = uiClock;
        std::printf("[result] preview mode (reference numbers)\n");
        std::fflush(stdout);
        std::fflush(stdout);
    }

    while (running) {
        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        lastFrameDeltaSec = static_cast<double>(nowCounter - lastFrameCounter) / static_cast<double>(perfFreq);
        lastFrameCounter = nowCounter;
        const float frameDelta = static_cast<float>(lastFrameDeltaSec);
        uiClock += lastFrameDeltaSec;

        if (beginSessionClockPending) {
            beginSessionClockPending = false;
            beginSessionClock();
        }

        auto saveScreenshot = [&]() {
        if (screenshotPath.empty()) {
            return;
        }
        std::vector<unsigned char> pixels(static_cast<size_t>(windowW) * static_cast<size_t>(windowH) * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, windowW, windowH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // Flip vertically (GL origin is bottom-left).
        const int rowBytes = windowW * 4;
        std::vector<unsigned char> flipped(pixels.size());
        for (int y = 0; y < windowH; ++y) {
            std::memcpy(flipped.data() + static_cast<size_t>(y) * rowBytes,
                pixels.data() + static_cast<size_t>(windowH - 1 - y) * rowBytes,
                static_cast<size_t>(rowBytes));
        }
        stbi_write_png(screenshotPath.c_str(), windowW, windowH, 4, flipped.data(), rowBytes);
        std::printf("screenshot saved: %s\n", screenshotPath.c_str());
        std::fflush(stdout);
    };

    bool escapePressed = false;
    bool rescanRequested = false; // F5 in the song list: re-read charts/
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        windowW = event.window.data1;
                        windowH = event.window.data2;
                        renderer.resize(windowW, windowH);
                        core_api::resize(windowW, windowH, 1.0f);
                    } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST
                        || event.window.event == SDL_WINDOWEVENT_LEAVE) {
                        // A mouse button released outside the window would
                        // otherwise stay "held" forever (fingers get their
                        // own up event, the mouse may not).
                        endPointer(pointerIdForButton(SDL_BUTTON_LEFT));
                        endPointer(pointerIdForButton(SDL_BUTTON_RIGHT));
                        // Auto-pause when the window loses keyboard focus while
                        // playing (LEAVE only fires for mouse leave, so gate on
                        // the focus event itself). During the opening lead-in
                        // (songTime < 0) a pause dialog could never be resumed
                        // properly - just go back to the song list with the
                        // sound off instead.
                        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && state == AppState::Play
                            && !paused && !pauseDialogOpen && screenshotPath.empty()
                            && userSettings.autoPauseOnBlur) {
                            const double currentSongTime =
                                audio.hasMusic() ? audio.songTime() : wallSongTime();
                            if (currentSongTime < 0.0) {
                                audio.stopMusic();
                                audio.setHoldLoop(false, false, 0.0f);
                                touches.clear();
                                std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                                lanePress.fill(0.0f);
                                paused = false;
                                session.active = false;
                                lastSeenJudgeTime = -100.0f;
                                hudState = game::HudState{};
                                state = AppState::Select;
                                systemMedia.setTaskbarProgress(-1.0, false);
                            } else {
                                paused = true;
                                pauseDialogOpen = true;
                                // Same as ESC: freeze the music clock, otherwise
                                // the song keeps running behind the dialog.
                                audio.pause();
                            }
                        }
                    }
                    break;
                case SDL_KEYDOWN: {
                    if (event.key.repeat != 0) {
                        break;
                    }
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        escapePressed = true;
                    } else if (event.key.keysym.sym == SDLK_f) {
                        // Ask the window instead of trusting a separate flag:
                        // the mode also changes from the settings card and, for
                        // the image splash, at the end of the boot sequence -
                        // which used to leave the first F press doing nothing
                        // when the game started in fullscreen.
                        fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
                        SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        if (!fullscreen) {
                            // Restore whatever chrome the current window mode uses.
                            SDL_SetWindowBordered(window, windowMode == 1 ? SDL_TRUE : SDL_FALSE);
                        }
                    } else if (event.key.keysym.sym == SDLK_h) {
                        showDebug = !showDebug;
                    } else if (event.key.keysym.sym == SDLK_F5 && state == AppState::Select) {
                        // The chart folder is scanned once at startup; this lets
                        // the player drop a new .sus in and pick it up without
                        // restarting (the empty-state hint promises F5).
                        rescanRequested = true;
                    } else if (state == AppState::Play && event.key.keysym.sym == SDLK_SPACE
                        && !countdownActive) {
                        // Space opens the pause dialog (same as the HUD button,
                        // previews included).
                        if (!pauseDialogOpen) {
                            paused = true;
                            audio.pause();
                            pauseDialogOpen = true;
                        }
                    } else if (state == AppState::Play && !autoPlay && !paused) {
                        const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
                        for (int lane = 0; lane < 12; ++lane) {
                            if (event.key.keysym.sym == kLaneKeys[lane]) {
                                keyHeld[lane] = true;
                                lanePress[static_cast<size_t>(lane)] = 1.0f;
                                const game::Judge result =
                                    judgement.tap(keyLanePos(lane), static_cast<float>(songTime), false, 0.5f);
                                game::Judge flickResult = game::Judge::None;
                                if (result == game::Judge::None) {
                                    // Keyboard has no swipe direction; keys can
                                    // still clear up/default flicks (left/right
                                    // flicks need a real swipe).
                                    flickResult = judgement.flick(
                                        keyLanePos(lane), static_cast<float>(songTime), game::FlickUp, 0.5f);
                                }
                                if (result != game::Judge::None || flickResult != game::Judge::None) {
                                    playHitSe(audio, judgement, seVolume);
                                }
                            }
                        }
                    }
                    break;
                }
                case SDL_KEYUP:
                    if (!autoPlay) {
                        for (int lane = 0; lane < 12; ++lane) {
                            if (event.key.keysym.sym == kLaneKeys[lane]) {
                                keyHeld[lane] = false;
                            }
                        }
                    }
                    break;
                case SDL_FINGERDOWN: {
                    if (state == AppState::Result) {
                        // Touch contacts only produce SDL_FINGER* events (their
                        // synthetic mouse events carry SDL_TOUCH_MOUSEID and are
                        // filtered out in the mouse path), so the button has to
                        // be tested here as well.
                        if (tapEffect.loaded()) {
                            tapEffect.spawn(event.tfinger.x * static_cast<float>(windowW),
                                event.tfinger.y * static_cast<float>(windowH));
                        }
                        const int fx = static_cast<int>(event.tfinger.x * static_cast<float>(windowW));
                        const int fy = static_cast<int>(event.tfinger.y * static_cast<float>(windowH));
                        if (game::resultContinueHitTest(windowW, windowH, fx, fy)) {
                            resultContinueRequested = true;
                        }
                        break;
                    }
                    // Tap feedback on the non-play screens (select, settings).
                    if (state != AppState::Play && tapEffect.loaded()) {
                        tapEffect.spawn(
                            event.tfinger.x * static_cast<float>(windowW),
                            event.tfinger.y * static_cast<float>(windowH));
                        break;
                    }
                    // HUD pause button (not lane input): handled before the
                    // paused / autoplay guards, exactly like the mouse path.
                    if (hudPausePress(static_cast<int>(event.tfinger.x * static_cast<float>(windowW)),
                            static_cast<int>(event.tfinger.y * static_cast<float>(windowH)))) {
                        break;
                    }
                    if (paused || state != AppState::Play) {
                        break;
                    }
                    // Opening-card skip button: the mouse path hit-tests it, but
                    // a touch contact only produces SDL_FINGER* events (its
                    // synthetic mouse events carry SDL_TOUCH_MOUSEID and are
                    // filtered out), so on a touchscreen the button was dead.
                    // Same conditions as the mouse path - it works during an
                    // autoplay preview too.
                    {
                        const int fx = static_cast<int>(event.tfinger.x * static_cast<float>(windowW));
                        const int fy = static_cast<int>(event.tfinger.y * static_cast<float>(windowH));
                        const double currentSongTime =
                            audio.hasMusic() ? audio.songTime() : wallSongTime();
                        if (!ImGui::GetIO().WantCaptureMouse
                            && currentSongTime + leadInSec < static_cast<double>(game::kHudIntroDurationSec)
                            && game::introSkipHitTest(windowW, windowH, fx, fy)) {
                            if (audio.hasMusic()) {
                                audio.skipLeadIn();
                            } else {
                                perfStart = SDL_GetPerformanceCounter()
                                    - static_cast<Uint64>(leadInSec * static_cast<double>(perfFreq));
                            }
                            std::printf("[intro] lead-in skipped\n");
                            std::fflush(stdout);
                            break;
                        }
                    }
                    if (autoPlay) {
                        break;
                    }
                    beginPointer(event.tfinger.fingerId,
                        static_cast<int>(event.tfinger.x * static_cast<float>(windowW)),
                        static_cast<int>(event.tfinger.y * static_cast<float>(windowH)), true);
                    break;
                }
                case SDL_FINGERMOTION: {
                    if (autoPlay || paused || state != AppState::Play) {
                        break;
                    }
                    movePointer(event.tfinger.fingerId,
                        static_cast<int>(event.tfinger.x * static_cast<float>(windowW)),
                        static_cast<int>(event.tfinger.y * static_cast<float>(windowH)));
                    break;
                }
                case SDL_FINGERUP: {
                    // Last chance flick: judge the lift-off from the fastest
                    // swipe speed seen during the gesture - short, fast flicks
                    // on touch panels often end before the mid-move check
                    // fires (and the last move can already be a slow one).
                    if (state == AppState::Play && !autoPlay && !paused) {
                        const float heightScale = static_cast<float>(windowH) / 1080.0f;
                        for (const TouchTrack& track : touches) {
                            if (track.fingerId != event.tfinger.fingerId) {
                                continue;
                            }
                            const game::FlickDir dir = flickDirFrom(track.peakUp, track.peakSide, track.travelUp,
                                track.travelSide, track.isTouch, heightScale);
                            if (dir != game::FlickNone) {
                                const double songTime =
                                    audio.hasMusic() ? audio.songTime() : wallSongTime();
                                const game::Judge result = flickJudge(track, songTime, dir);
                                if (result != game::Judge::None) {
                                    playHitSe(audio, judgement, seVolume);
                                }
                            }
                        }
                    }
                    endPointer(event.tfinger.fingerId);
                    break;
                }
                // ------------------------------------------------------
                // Mouse: same as a finger, but the press is ignored when
                // ImGui owns the pointer (settings card, pause dialog) or
                // when it lands on the HUD pause button.
                // ------------------------------------------------------
                case SDL_MOUSEBUTTONDOWN: {
                    // Synthetic event from a touch (hint above): the finger
                    // path already handled it, do not double-process. Some
                    // touch stacks only ever deliver this synthetic mouse
                    // event though, so the HUD buttons are still honoured here
                    // (setting the same request twice is harmless).
                    if (event.button.which == SDL_TOUCH_MOUSEID) {
                        hudPausePress(event.button.x, event.button.y);
                        break;
                    }
                    if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) {
                        break;
                    }
                    if (state == AppState::Result) {
                        // 继续 button: hit-tested here (not by ImGui) so touch
                        // and mouse share one path. No lane input afterwards.
                        if (event.button.button == SDL_BUTTON_LEFT && !ImGui::GetIO().WantCaptureMouse
                            && game::resultContinueHitTest(windowW, windowH, event.button.x,
                                   event.button.y)) {
                            resultContinueRequested = true;
                        }
                        break;
                    }
                    // Tap feedback everywhere except the play state: there the
                    // press is a note hit and the judgement input must stay
                    // untouched. Note there is deliberately NO
                    // WantCaptureMouse check here - the song select screen is
                    // one fullscreen ImGui window, so that flag is always set
                    // and would swallow every click. The effect is purely
                    // cosmetic, so it fires on buttons and sliders too (pjsk
                    // does the same).
                    if (state != AppState::Play && tapEffect.loaded()) {
                        tapEffect.spawn(static_cast<float>(event.button.x),
                            static_cast<float>(event.button.y));
                    }
                    // Opening-card skip button (bottom-right): jump the
                    // lead-in straight to chart time 0 and start the music
                    // now. Works in autoplay previews too.
                    if (state == AppState::Play && !paused && !ImGui::GetIO().WantCaptureMouse) {
                        const double currentSongTime =
                            audio.hasMusic() ? audio.songTime() : wallSongTime();
                        if (currentSongTime + leadInSec < static_cast<double>(game::kHudIntroDurationSec)
                            && game::introSkipHitTest(windowW, windowH, event.button.x, event.button.y)) {
                            if (audio.hasMusic()) {
                                audio.skipLeadIn();
                            } else {
                                // No BGM: the clock is the wall clock - move
                                // its anchor so chart time 0 is now.
                                perfStart = SDL_GetPerformanceCounter()
                                    - static_cast<Uint64>(leadInSec * static_cast<double>(perfFreq));
                            }
                            std::printf("[intro] lead-in skipped\n");
                            std::fflush(stdout);
                            break;
                        }
                    }
                    // HUD pause button (see hudPausePress): checked before the
                    // "no input while paused / autoplaying" guards so the
                    // button behaves the same in a preview as in a real run.
                    if (hudPausePress(event.button.x, event.button.y)) {
                        break;
                    }
                    if (autoPlay || paused || state != AppState::Play) {
                        break;
                    }
                    if (ImGui::GetIO().WantCaptureMouse) {
                        break;
                    }
                    beginPointer(pointerIdForButton(event.button.button), event.button.x, event.button.y, false);
                    break;
                }
                case SDL_MOUSEMOTION: {
                    if (event.motion.which == SDL_TOUCH_MOUSEID) {
                        break; // touch drag is handled by the finger path
                    }
                    if (autoPlay || paused || state != AppState::Play) {
                        break;
                    }
                    if ((event.motion.state & SDL_BUTTON_LMASK) != 0) {
                        movePointer(pointerIdForButton(SDL_BUTTON_LEFT), event.motion.x, event.motion.y);
                    }
                    if ((event.motion.state & SDL_BUTTON_RMASK) != 0) {
                        movePointer(pointerIdForButton(SDL_BUTTON_RIGHT), event.motion.x, event.motion.y);
                    }
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    if (event.button.which == SDL_TOUCH_MOUSEID) {
                        break; // touch release is handled by the finger path
                    }
                    if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) {
                        break;
                    }
                    endPointer(pointerIdForButton(event.button.button));
                    break;
                }
                default:
                    break;
            }
        }

        if (escapePressed) {
            countdownActive = false; // leaving / continuing cancels any countdown
            if (pauseDialogOpen) {
                beginResumeCountdown();
            } else if (state == AppState::Result || (state == AppState::Play && susPath.empty())) {
                // back to the song list
                audio.stopMusic();
                audio.stopResultBgm();
                audio.setHoldLoop(false, false, 0.0f);
                touches.clear();
                std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                lanePress.fill(0.0f);
                paused = false;
                session.active = false;
                resultScheduled = false;
                resultData = game::ResultData{};
                state = AppState::Select;
                systemMedia.setTaskbarProgress(-1.0, false);
            } else {
                running = false;
            }
        }

        if (state == AppState::Play && paused && !countdownActive) {
            SDL_Delay(16);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (state == AppState::Select) {
            // ----------------------------------------------------------
            // Song select
            // ----------------------------------------------------------
            renderer.setLaneGlows({});
            renderer.renderFrame(nullptr, 0, 0.85f);

            // Music preview: cut a clip from partway into the BGM and loop
            // it, the way the official select screen never previews from the
            // top. Reloads only when the selection changes.
            if (selected >= 0 && selected < static_cast<int>(entries.size())) {
                audio.startPreview(entries[static_cast<size_t>(selected)].bgmPath, error);
                error.clear(); // a missing BGM just means silence
            } else {
                audio.stopPreview();
            }
            audio.updatePreview();

            if (selected >= 0 && selected < static_cast<int>(entries.size())) {
                const std::string& cover = entries[static_cast<size_t>(selected)].coverPath;
                if (cover != loadedCoverPath) {
                    loadedCoverPath = cover;
                    renderer.loadCover(cover, error);
                }
            }

            // The list's sort / grouping is part of the settings, so write it
            // back when the player changes it on the select screen.
            const int prevSortMode = userSettings.sortMode;
            const int prevGroupMode = userSettings.groupMode;
            const int action = game::drawSongSelect(renderer, entries, selected, windowW, windowH,
                static_cast<float>(uiClock), userSettings.sortMode, userSettings.groupMode);
            if (prevSortMode != userSettings.sortMode || prevGroupMode != userSettings.groupMode) {
                persistUserData();
            }
            // Consume the F5 request here so it cannot leak into a later frame.
            const bool wantRescan = rescanRequested || action == game::SelectRescan;
            rescanRequested = false;
            if (action >= 0 && action < static_cast<int>(entries.size())) {
                if (startSession(session, entries[static_cast<size_t>(action)], renderer, audio, judgement, noteSpeed, error)) {
                    loadedCoverPath = session.entry.coverPath;
                    announceTrack();
                    touches.clear();
                    std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                    lanePress.fill(0.0f);
                    paused = false;
                    resultScheduled = false;
                    resultData = game::ResultData{};
                    state = AppState::Play;
                    beginSessionClock();
                } else {
                    std::fprintf(stderr, "%s\n", error.c_str());
                    error.clear();
                }
            } else if (action == game::SelectSettings) {
                showDebug = true;
            } else if (wantRescan) {
                entries = game::scanChartFolder(chartsDir);
                selected = entries.empty() ? -1 : 0;
                game::applyScores(entries, scores);
                loadedCoverPath.clear();
                std::printf("[select] %d chart(s)\n", static_cast<int>(entries.size()));
            }

            // Settings card, opened from the musicsetting button (or H).
            drawSettingsCard();

            // Headless check: dump the song list shortly after startup. An
            // explicit --screenshot-time overrides the default 1.2s here as
            // well, which is how a scroll / animation is let to settle first.
            if (!screenshotPath.empty()) {
                const double shotAt = screenshotTimeGiven ? std::max(0.5, screenshotTimeSec) : 1.2;
                if (uiClock > shotAt) {
                    wantScreenshot = true;
                }
            }
        } else if (state == AppState::Play) {
            // ----------------------------------------------------------
            // Playing
            // ----------------------------------------------------------
            const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
            audio.update();
            const float outputTime = static_cast<float>(songTime + leadInSec);

            // Report to Windows: SMTC position + taskbar button progress.
            if (userSettings.reportSmtc) {
                systemMedia.updatePlayback(true, paused, songTime, trackDurationSec);
            }
            systemMedia.setTaskbarProgress(
                trackDurationSec > 1.0 ? songTime / trackDurationSec : -1.0, paused);

            // Debug (`--test-restart`): replay "give up -> pick another song"
            // in the middle of a run, i.e. a second loadMusic() on a live
            // engine. This is the sequence that used to freeze the process.
            if (testRestart && !restartDone && !entries.empty() && songTime >= restartAtSec) {
                restartDone = true;
                std::printf("[restart] give up at %.2fs, loading another chart\n", songTime);
                std::fflush(stdout);
                audio.stopMusic();
                audio.setHoldLoop(false, false, 0.0f);
                touches.clear();
                std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                lanePress.fill(0.0f);
                judgement.reset();
                const int next = (restartIndex + 1) % static_cast<int>(entries.size());
                if (startSession(session, entries[static_cast<size_t>(next)], renderer, audio, judgement,
                        noteSpeed, error)) {
                restartIndex = next;
                announceTrack();
                beginSessionClock();
                lastSeenJudgeTime = -100.0f;
                hudState = game::HudState{};
                    std::printf("[restart] second session started ok (music=%s)\n",
                        entries[static_cast<size_t>(next)].bgmPath.c_str());
                    std::fflush(stdout);
                } else {
                    std::fprintf(stderr, "[restart] failed: %s\n", error.c_str());
                    error.clear();
                }
            }

            std::vector<float> holdLanes;
            for (int lane = 0; lane < 12; ++lane) {
                if (keyHeld[lane]) {
                    holdLanes.push_back(keyLanePos(lane));
                }
            }
            for (const auto& track : touches) {
                holdLanes.push_back(track.lanePos);
            }
            // Debug (`--test-hits`): hold every hold note's lane for its whole
            // duration so the hold tail / break logic can be exercised
            // headlessly (no input can be injected into the preview window).
            static std::vector<std::pair<float, double>> simHolds; // lane, end time
            if (testHits && !autoPlay) {
                simHolds.erase(std::remove_if(simHolds.begin(), simHolds.end(),
                                   [&](const std::pair<float, double>& hold) { return hold.second < songTime; }),
                    simHolds.end());
                const float* events = core_api::getHitEventBuffer();
                const int eventCount = core_api::getHitEventCount();
                for (int i = 0; i < eventCount; ++i) {
                    const float* ev = events + i * 7;
                    if (ev[0] > songTime + 0.05f) {
                        break;
                    }
                    if (static_cast<int>(std::lround(ev[3])) != 5 || ev[5] < songTime) {
                        continue;
                    }
                    bool known = false;
                    for (const auto& hold : simHolds) {
                        if (std::fabs(hold.first - ev[1]) < 0.01f && std::fabs(hold.second - ev[5]) < 0.01) {
                            known = true;
                            break;
                        }
                    }
                    if (!known) {
                        simHolds.emplace_back(ev[1], static_cast<double>(ev[5]));
                    }
                }
                for (const auto& hold : simHolds) {
                    holdLanes.push_back(hold.first);
                }
            }
            judgement.setHoldLanes(holdLanes);
            // Autoplay is runtime-switchable (settings card), so keep the
            // judgement engine and the core's effect timeline in sync here.
            judgement.setAutoPlay(autoPlay);
            core_api::setEffectAutoplay(autoPlay);
            judgement.update(static_cast<float>(songTime));

            // Long notes the player let go of too early keep scrolling but are
            // drawn washed out by the core until the lane is held again (pjsk).
            // Cheap: at most a handful of holds, republished every frame.
            std::vector<float> dimmedHoldKeys;
            judgement.appendDimmedHoldKeys(dimmedHoldKeys);
            core_api::setDimmedHolds(dimmedHoldKeys);

            // CppSekai: notes the player hit vanish from the field at the hit
            // itself, while missed notes keep falling past the judgement line
            // until they are off screen (pjsk). Publish the hit event indices
            // the judgement engine resolved as hits since last frame, and the
            // holds whose start was never hit so their bodies scroll past
            // instead of parking. Nothing is published in autoplay - there the
            // core keeps the upstream "vanish at the line" preview look.
            if (!autoPlay) {
                const std::vector<int>& hitIndices = judgement.hitEventIndices();
                while (s_hitPublishCursor < hitIndices.size()) {
                    core_api::markNoteHit(hitIndices[s_hitPublishCursor]);
                    ++s_hitPublishCursor;
                }
                core_api::setMissedHolds(judgement.missedHoldKeys());
            }

            // Debug (`--test-hits`): tap every upcoming note through the normal
            // judgement path, so the whole hit-effect chain can be checked in a
            // headless screenshot (where no input can be injected).
            if (testHits && !autoPlay) {
                static int testCursor = 0;
                static double testPrevTime = -100.0;
                if (songTime + 0.3 < testPrevTime) {
                    testCursor = 0; // seeked back / restarted
                }
                testPrevTime = songTime;
                const float* events = core_api::getHitEventBuffer();
                const int eventCount = core_api::getHitEventCount();
                while (testCursor < eventCount && events[testCursor * 7] <= songTime + 0.02) {
                    const float* event = events + testCursor * 7;
                    const int kind = static_cast<int>(std::lround(event[3]));
                    const int flickDir = (static_cast<int>(event[4]) >> 1) & 3;
                    if (kind == 0 || kind == 1) {
                        judgement.tap(event[1], static_cast<float>(songTime), false, 0.8f);
                    } else if (kind == 2) {
                        judgement.flick(event[1], static_cast<float>(songTime),
                            flickDir == 0 ? game::FlickUp : static_cast<game::FlickDir>(flickDir), 0.8f);
                    }
                    ++testCursor;
                }
            }

            // ----------------------------------------------------------
            // Song finished -> result screen. The music is cut here (the
            // chart keeps running past the last note while the track plays
            // out), then the result screen takes over with its own clock.
            // ----------------------------------------------------------
            const bool resultDue = trackDurationSec > 1.0
                && songTime >= (resultAtSec > 0.0 ? resultAtSec : trackDurationSec - 0.15);
            if (resultDue && !resultScheduled) {
                resultScheduled = true;
                const auto& st = judgement.stats();
                // Record CLEAR / FULL COMBO right here, not a moment earlier:
                // this is the first point where the run is really over, so the
                // life bar has seen the last of the chart's auto-misses (their
                // window runs past the final note), and it is the exact
                // snapshot the result screen below draws.
                //
                // CLEAR = got through the song alive. Ending with the life bar
                // at 0 is a failed live and must not leave a clear mark - this
                // is why every song played to the end used to come back
                // cleared. A full combo needs the run to count in the first
                // place: hold breaks drain life without a MISS, so miss == 0
                // alone can still end at 0 life.
                if (!autoPlay && !session.scoreRecorded && songTime >= trackDurationSec - 0.25) {
                    session.scoreRecorded = true;
                    const bool cleared = st.life > 0.0f;
                    const bool fullCombo = cleared && st.miss == 0;
                    const std::string key = game::scoreKey(session.entry);
                    // Keep the old best for the result screen's 最高得分 /
                    // 新纪录! before the merge below overwrites it.
                    resultPreviousBest = scores[key].bestScore;
                    scores[key] = game::mergeScore(scores[key], cleared, fullCombo, st.score);
                    game::applyScores(entries, scores);
                    persistUserData();
                    std::printf("[score] %s %s%s (life=%.0f/%.0f) (%s)\n", key.c_str(),
                        cleared ? "cleared" : "failed", fullCombo ? " (full combo)" : "",
                        static_cast<double>(st.life), static_cast<double>(game::kMaxLife),
                        userDataFile.c_str());
                    std::fflush(stdout);
                }
                resultData = buildResultData(session.intro, session.entry, st,
                    resultPreviousBest, judgement.chartRating());
                audio.setHoldLoop(false, false, 0.0f);
                audio.stopMusic();
                touches.clear();
                std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                lanePress.fill(0.0f);
                paused = false;
                countdownActive = false;
                systemMedia.setTaskbarProgress(-1.0, false);
                state = AppState::Result;
                resultShownAt = uiClock;
                std::printf("[result] shown (score=%.0f best=%.0f newRecord=%d)\n",
                    resultData.score, resultData.highScore, resultData.newRecord ? 1 : 0);
                std::fflush(stdout);
            }

            // Hold loop SE: loop while a hold is being tracked (anyActiveHold
            // goes false as soon as the hold ends or the lane is released).
            {
                bool holdCritical = false;
                const bool holding = !paused && judgement.anyActiveHold(&holdCritical);
                audio.setHoldLoop(holding, holdCritical, seVolume * 0.9f);
            }

            // ----------------------------------------------------------
            // Lane highlight: hover (mouse) + press (keys / touches)
            // ----------------------------------------------------------
            int mouseX = 0;
            int mouseY = 0;
            laneHover.fill(0.0f);
            if (SDL_GetMouseState(&mouseX, &mouseY) != 0 && !autoPlay) {
                const float clipX = (static_cast<float>(mouseX) / static_cast<float>(windowW)) * 2.0f - 1.0f;
                const float clipY = 1.0f - (static_cast<float>(mouseY) / static_cast<float>(windowH)) * 2.0f;
                const float worldY = renderer.clipToWorldY(clipY);
                if (worldY > 0.12f && worldY < 1.35f) {
                    const float lanePos = renderer.clipToWorldX(clipX) / worldY;
                    if (lanePos > -6.2f && lanePos < 6.2f) {
                        laneHover[static_cast<size_t>(laneIndexFromPos(lanePos))] = 0.42f;
                    }
                }
            }
            // Touch hover: every active pointer (finger or mouse pointer)
            // lights the lane it is currently over, so dragging a finger
            // around the field lights the lanes up as it moves. SDL's
            // synthesized mouse position only follows the primary finger,
            // which is why the tracks are the source of truth here.
            for (const TouchTrack& track : touches) {
                const size_t idx = static_cast<size_t>(laneIndexFromPos(track.lastLanePos));
                laneHover[idx] = std::max(laneHover[idx], 0.42f);
            }

            std::vector<platform::Renderer::LaneGlow> glows;
            for (int lane = 0; lane < LANE_COUNT; ++lane) {
                lanePress[static_cast<size_t>(lane)] = std::max(0.0f, lanePress[static_cast<size_t>(lane)] - frameDelta * 3.2f);
                const bool held = lane < 12 && keyHeld[lane];
                if (held) {
                    lanePress[static_cast<size_t>(lane)] = std::max(lanePress[static_cast<size_t>(lane)], 0.85f);
                }
                float intensity = lanePress[static_cast<size_t>(lane)];
                for (const auto& track : touches) {
                    if (track.laneIndex == lane) {
                        intensity = std::max(intensity, 0.95f);
                    }
                }
                intensity = std::max(intensity, laneHover[static_cast<size_t>(lane)]);
                if (intensity > 0.01f) {
                    glows.push_back(platform::Renderer::LaneGlow{keyLanePos(lane), 0.5f, intensity});
                }
            }
            renderer.setLaneGlows(glows);

            // ----------------------------------------------------------
            // HUD state; the judge text only triggers on real hits.
            // ----------------------------------------------------------
            const auto& stats = judgement.stats();
            hudState.score = stats.score;
            hudState.combo = stats.combo;
            const game::ScoreRank rank = game::scoreRankAndBar(stats.score, judgement.chartRating());
            hudState.rank = rank.rank;
            hudState.scoreBarRatio = rank.bar;
            if (stats.lastJudgeTimeSec != lastSeenJudgeTime) {
                lastSeenJudgeTime = stats.lastJudgeTimeSec;
                hudState.lastJudge = stats.lastJudge;
                hudState.lastJudgeAtSec = stats.lastJudgeTimeSec;
                if (autoPlay) {
                    // Autoplay: burst effects come from the core's own
                    // timeline, but nothing else plays the hit SE (the input
                    // paths never fire). Ticks stay silent - the hold loop SE
                    // covers the sustain.
                    if (static_cast<int>(std::lround(stats.lastHitKind)) <= 3) {
                        playHitSe(audio, judgement, seVolume);
                    }
                } else if (stats.lastJudge != game::Judge::Miss && stats.lastJudge != game::Judge::None) {
                    // Original hit effect: the chart core's own particle system,
                    // played for the note that was just judged (same sprites and
                    // timings the autoplay preview uses).
                    core_api::triggerNoteEffect(stats.lastHitCenter, stats.lastHitWidth, stats.lastHitTimeSec,
                        static_cast<int>(std::lround(stats.lastHitKind)), stats.lastJudgeCritical,
                        static_cast<int>(stats.lastHitFlickDir), stats.lastHitFriction);
                }
            }
            hudState.lifeRatio = judgement.lifeRatio();
            hudState.autoJudge = autoPlay;

            // Damage vignette state: any life drop flashes the edges, life
            // stuck at 0 keeps them dark.
            if (stats.life < lastSeenLife - 0.5f) {
                damageVignette = 1.0f;
            }
            lastSeenLife = stats.life;
            damageVignette = std::max(0.0f, damageVignette - frameDelta / 0.45f);

            // Debug (`--judge-frame N`): freeze the judge text on frame N of
            // its 60fps pop-in so the animation can be checked from a headless
            // screenshot, where no input can be injected.
            if (judgeAnimFrame >= 0) {
                hudState.lastJudge = game::Judge::Perfect;
                hudState.lastJudgeAtSec =
                    static_cast<float>(songTime) - static_cast<float>(judgeAnimFrame) / 60.0f;
            }

            // ----------------------------------------------------------
            // Render
            // ----------------------------------------------------------
            core_api::render(static_cast<float>(songTime));
            const float* quads = core_api::getQuadBuffer();
            const int quadCount = core_api::getQuadCount();
            // Upstream openingPlayfieldVisibility(): the stage and the notes
            // stay hidden behind the card, then fade in over 1.8s.
            const float visibility =
                game::openingPlayfieldVisibility(outputTime, session.intro.hasContent);
            renderer.renderFrame(quads, quadCount, 0.85f, visibility);

            if (visibility > 0.0f) {
                game::drawHud(renderer, hudState, static_cast<float>(songTime), windowW, windowH,
                    static_cast<float>(leadInSec), dumpJudgeSheet);
            }
            game::drawIntro(renderer, session.intro, outputTime, windowW, windowH);

            // Subtle playback progress bar along the very top edge of the
            // window (semi-transparent, can be turned off in the settings).
            if (showProgressBar) {
                const float progress = trackDurationSec > 1.0
                    ? std::clamp(static_cast<float>(songTime / trackDurationSec), 0.0f, 1.0f)
                    : 0.0f;
                ImDrawList* fg = ImGui::GetForegroundDrawList();
                const float barH = 3.0f;
                fg->AddRectFilled(ImVec2(0.0f, 0.0f),
                    ImVec2(static_cast<float>(windowW), barH), IM_COL32(255, 255, 255, 24));
                if (progress > 0.0f) {
                    fg->AddRectFilled(ImVec2(0.0f, 0.0f),
                        ImVec2(static_cast<float>(windowW) * progress, barH),
                        IM_COL32(255, 255, 255, 84));
                }
            }

            // ----------------------------------------------------------
            // Damage vignette: dark inner shadow around the screen edges.
            // A life loss flashes it (decays over ~0.45s); life at 0 keeps
            // it permanently on (original-game feedback). The shadow has to
            // hug all four window edges, corners included:
            //   * the four edge bands are inset so they never overlap,
            //   * the four corner squares carry a two-colour gradient (dark on
            //     the two outer edges, 0 at the inner corner) so the darkening
            //     is continuous around the perimeter without stacking.
            // Insetting the bands WITHOUT the corner squares (what this used
            // to do) left the top/bottom edges undarkened in the corners, which
            // read as "the shadow is not around the window".
            // ----------------------------------------------------------
            {
                const float deadVignette = judgement.lifeRatio() <= 0.0f ? 0.8f : 0.0f;
                const float vig = std::clamp(damageVignette * 0.55f + deadVignette, 0.0f, 1.0f);
                if (vig > 0.004f) {
                    ImDrawList* fg = ImGui::GetForegroundDrawList();
                    const float w = static_cast<float>(windowW);
                    const float h = static_cast<float>(windowH);
                    const int a = static_cast<int>(90.0f * vig);
                    const int z = 0;
                    const ImU32 dark = IM_COL32(0, 0, 0, a);
                    const ImU32 none = IM_COL32(0, 0, 0, z);
                    const float bandV = h * 0.16f; // top / bottom band height
                    const float bandH = w * 0.12f; // left / right band width
                    // Top / bottom, between the corner squares.
                    fg->AddRectFilledMultiColor(ImVec2(bandH, 0.0f), ImVec2(w - bandH, bandV),
                        dark, dark, none, none);
                    fg->AddRectFilledMultiColor(ImVec2(bandH, h - bandV), ImVec2(w - bandH, h),
                        none, none, dark, dark);
                    // Left / right, between the corner squares.
                    fg->AddRectFilledMultiColor(ImVec2(0.0f, bandV), ImVec2(bandH, h - bandV),
                        dark, none, none, dark);
                    fg->AddRectFilledMultiColor(ImVec2(w - bandH, bandV), ImVec2(w, h - bandV),
                        none, dark, dark, none);
                    // Corners: dark along the two outer edges, fading to the
                    // window's inside (bilinear between the four colours).
                    fg->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(bandH, bandV),
                        dark, dark, none, dark); // top-left
                    fg->AddRectFilledMultiColor(ImVec2(w - bandH, 0.0f), ImVec2(w, bandV),
                        dark, dark, dark, none); // top-right
                    fg->AddRectFilledMultiColor(ImVec2(0.0f, h - bandV), ImVec2(bandH, h),
                        dark, none, dark, dark); // bottom-left
                    fg->AddRectFilledMultiColor(ImVec2(w - bandH, h - bandV), ImVec2(w, h),
                        none, dark, dark, dark); // bottom-right
                }
            }

            // ----------------------------------------------------------
            // Pause button zone (right end of the life bar).
            // ----------------------------------------------------------
            // The press was already hit-tested in the event handler; act on it
            // here so the same click never also counts as a lane hit.
            if (pauseClickRequested) {
                pauseClickRequested = false;
                if (!pauseDialogOpen) {
                    paused = true;
                    audio.pause();
                    pauseDialogOpen = true;
                }
            }

            // Settings card (H key), shared with the song select state.
            drawSettingsCard();

            // ----------------------------------------------------------
            // Pause dialog: 重试 / 放弃 / 继续演出.
            // ----------------------------------------------------------
            if (showPauseDialogShot && !pauseDialogOpen && songTime > 0.5) {
                paused = true;
                audio.pause();
                pauseDialogOpen = true;
            }
            // Alive flag keeps drawing while the close animation plays out.
            static bool pauseDialogAlive = false;
            if (pauseDialogOpen) {
                pauseDialogAlive = true;
            }
            if (pauseDialogAlive) {
                const int action = ui::messageDialog(renderer, "##pauseDialog", "是否继续演出？",
                    {std::string("重试"), std::string("放弃"), std::string("继续演出")},
                    {false, false, true});
                if (action == 0) {
                    // Retry: reload the current chart from the top.
                    pauseDialogOpen = false;
                    paused = false;
                    if (startSession(session, session.entry, renderer, audio, judgement, noteSpeed, error)) {
                        announceTrack();
                        beginSessionClock();
                    } else {
                        std::fprintf(stderr, "%s\n", error.c_str());
                        error.clear();
                    }
                } else if (action == 1) {
                    // Give up: back to the song list.
                    pauseDialogOpen = false;
                    // Without this the dialog's alive flag stays set and the
                    // next session redraws a half-closed dialog.
                    pauseDialogAlive = false;
                    audio.stopMusic();
                    audio.setHoldLoop(false, false, 0.0f);
                    touches.clear();
                    std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                    lanePress.fill(0.0f);
                    paused = false;
                    session.active = false;
                    lastSeenJudgeTime = -100.0f;
                    hudState = game::HudState{};
                    state = AppState::Select;
                    systemMedia.setTaskbarProgress(-1.0, false);
                } else if (action == 2) {
                    // Continue: 3-2-1 countdown, then the music resumes.
                    beginResumeCountdown();
                } else if (action == -2) {
                    // Close animation finished. If the dialog was still open
                    // the user dismissed it via the X (= continue).
                    if (pauseDialogOpen) {
                        beginResumeCountdown();
                    }
                    pauseDialogAlive = false;
                }
            }

            // ----------------------------------------------------------
            // Resume countdown: big white 3-2-1 with an expanding ring,
            // one beep per number (assets/se/count_down.mp3). The music
            // stays paused and resumes when the countdown ends.
            // ----------------------------------------------------------
            if (countdownActive) {
                const double elapsed = uiClock - countdownStartClock;
                if (elapsed >= 3.0) {
                    countdownActive = false;
                    paused = false;
                    audio.resume();
                } else {
                    const int number = 3 - static_cast<int>(elapsed);
                    if (number != countdownNumberShown) {
                        countdownNumberShown = number;
                        audio.playCountdownSe(seVolume);
                    }
                    const float frac = static_cast<float>(elapsed - std::floor(elapsed));
                    auto easeOutCubic = [](float t) {
                        t = std::clamp(t, 0.0f, 1.0f);
                        const float inv = 1.0f - t;
                        return 1.0f - inv * inv * inv;
                    };
                    ImDrawList* fg = ImGui::GetForegroundDrawList();
                    const float cs = ui::scale();
                    const ImVec2 c(static_cast<float>(windowW) * 0.5f, static_cast<float>(windowH) * 0.5f);
                    // Ring: expands and fades within each one-second tick.
                    if (countdownRing != 0) {
                        const float diameter = (170.0f + 330.0f * easeOutCubic(frac)) * cs;
                        const int alpha = static_cast<int>((1.0f - frac) * 170.0f);
                        fg->AddImage(countdownRing,
                            ImVec2(c.x - diameter * 0.5f, c.y - diameter * 0.5f),
                            ImVec2(c.x + diameter * 0.5f, c.y + diameter * 0.5f),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
                    }
                    // The number pops in at the start of its tick.
                    const float fontSize = 150.0f * cs * (0.85f + 0.15f * easeOutCubic(frac * 3.0f));
                    const char digits[2] = {static_cast<char>('0' + number), '\0'};
                    const ImVec2 ts = game::bodyFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, digits);
                    const ImVec2 pos(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
                    fg->AddText(game::bodyFont(), fontSize, ImVec2(pos.x + 3.0f * cs, pos.y + 3.0f * cs),
                        IM_COL32(20, 22, 34, 200), digits);
                    fg->AddText(game::bodyFont(), fontSize, pos, IM_COL32(255, 255, 255, 255), digits);
                }
            }

            if (!screenshotPath.empty() && songTime >= screenshotTimeSec) {
                wantScreenshot = true;
            }
        } else if (state == AppState::Result) {
            // ----------------------------------------------------------
            // Result screen. The stage keeps rendering behind it (the panel is
            // translucent), so the same renderFrame() call as the play state
            // is used with the playfield frozen.
            // ----------------------------------------------------------
            renderer.setLaneGlows({});
            renderer.renderFrame(nullptr, 0, 0.85f);

            const float resultElapsed = static_cast<float>(uiClock - resultShownAt);
            // Looping result track; a no-op while it is already playing, and
            // silence when the file is not shipped.
            if (!audio.resultBgmActive()) {
                audio.startResultBgm(resultBgmPath, 0.85f, error);
                error.clear();
            }
            game::drawResult(renderer, resultData, resultElapsed, windowW, windowH);
            if (resultContinueRequested) {
                audio.stopResultBgm();
                std::printf("[result] continue -> song select\n");
                std::fflush(stdout);
                resultContinueRequested = false;
                resultScheduled = false;
                resultData = game::ResultData{};
                lastSeenJudgeTime = -100.0f;
                hudState = game::HudState{};
                state = AppState::Select;
                std::printf("[result] continue -> song select\n");
                std::fflush(stdout);
            }
            // Headless check: dump the settled result screen.
            if (!screenshotPath.empty() && !wantScreenshot && resultElapsed >= 2.6f) {
                wantScreenshot = true;
            }
        }

        // Paused frames (pause dialog) skip the playing branch above; let the
        // headless screenshot still fire using the wall clock.
        if (!screenshotPath.empty() && !wantScreenshot && state == AppState::Play && paused
            && (wallSongTime() - leadInSec) >= screenshotTimeSec) {
            wantScreenshot = true;
        }

        // PJSK style tap feedback, always on top of whatever is on screen.
        tapEffect.update(frameDelta);
        tapEffect.draw();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Captured after the ImGui pass so the HUD / intro card / song list
        // are part of the frame.
        if (wantScreenshot) {
            wantScreenshot = false;
            {
                const auto& st = judgement.stats();
                std::printf("[stats] perfect=%d great=%d good=%d bad=%d miss=%d combo=%d maxCombo=%d tails=%d breaks=%d score=%.0f life=%.0f (%.1f%%)\n",
                    st.perfect, st.great, st.good, st.bad, st.miss, st.combo, st.maxCombo, st.holdTails, st.holdBreaks,
                    st.score, st.life, 100.0f * st.life / game::kMaxLife);
            }
            saveScreenshot();
            running = false;
        }

        // Frame pacing: vsync quantizes presentation to refresh intervals, so
        // on a 60 Hz panel any cap above 60 is physically impossible and the
        // limiter's sleep just jitters around the vblank (fps "乱跳"). Past
        // the refresh rate we drop vsync and pace purely with the limiter.
        const bool wantVsync = !(fpsLimitLive > 0 && fpsLimitLive > displayRefreshHz);
        if (wantVsync != vsyncActive) {
            SDL_GL_SetSwapInterval(wantVsync ? 1 : 0);
            vsyncActive = wantVsync;
        }
        SDL_GL_SwapWindow(window);

        // Optional frame cap on top of vsync: sleep in coarse chunks, then
        // busy-wait the last millisecond for accuracy. Keeps the GPU idle
        // between frames (less power/heat) when the monitor is 120 Hz+.
        if (fpsLimitLive > 0) {
            const double minFrameSec = 1.0 / static_cast<double>(fpsLimitLive);
            const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - lastFrameCounter) / perfFreqD;
            double remaining = minFrameSec - elapsed;
            while (remaining > 0.002) {
                SDL_Delay(static_cast<Uint32>(remaining * 1000.0) - 1);
                remaining = minFrameSec
                    - static_cast<double>(SDL_GetPerformanceCounter() - lastFrameCounter) / perfFreqD;
            }
            while (remaining > 0.0) {
                remaining = minFrameSec
                    - static_cast<double>(SDL_GetPerformanceCounter() - lastFrameCounter) / perfFreqD;
            }
        }
    }

    // Headless checks (--screenshot) must not touch the player's data file.
    if (screenshotPath.empty()) {
        persistUserData();
    }

    audio.shutdown();
    systemMedia.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
