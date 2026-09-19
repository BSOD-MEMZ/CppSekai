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
#include "path_utf8.hpp"
#include "platform/Audio.hpp"
#include "platform/Party.hpp"
#include "platform/Renderer.hpp"
#include "platform/SystemMedia.hpp"
#include "game/Intro.hpp"
#include "game/Judgement.hpp"
#include "game/Hud.hpp"
#include "game/PartyScreen.hpp"
#include "game/Result.hpp"
#include "game/SongSelect.hpp"
#include "game/StageBackground.hpp"
#include "game/Ui.hpp"
#include "game/TapEffect.hpp"

#include <map>

#define STB_IMAGE_WRITE_IMPLEMENTATION
// Declarations only: platform/Renderer.cpp owns STB_IMAGE_IMPLEMENTATION.
#include "third_party/mmw_preview/vendor/stb_image.h"
#include "third_party/stb_image_write.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Alignment overrides shared with startSession() (set from the command line).
double gFillerSec = -1.0;    // < 0: auto-detect the leading silence
double gUserOffsetSec = 0.0; // manual fine tune, seconds
bool gForceFlickLog = false; // --flick-log: same switch as the 判定 card's checkbox

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
        // Event timestamp (ms) of the last *processed* sample. Deliberately the
        // event's own stamp and not SDL_GetTicks() at processing time: SDL hands
        // a whole frame's events over in one batch, so processing-time stamps
        // made every sample of a batch share one millisecond (see movePointer).
        Uint32 lastMoveTimeMs = 0;
        // Displacement of the samples that arrived inside the same millisecond,
        // waiting to be folded into one measurement instead of being dropped.
        float pendingDx = 0.0f;
        float pendingDy = 0.0f;
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

    // ------------------------------------------------------------------
    // Flick debug log (settings > 判定 > Flick 调试日志).
    //
    // A missed touch flick leaves nothing to inspect afterwards: the gesture
    // is a stream of raw SDL_FINGER* events, the thresholds live in
    // flickDirFrom() and the outcome is one enum. So while the switch is on
    // every raw sample, the measurement the classifier saw, the direction it
    // picked and the judgement that came out are appended to flick_debug.log
    // (flushed per line, so it can be tailed while playing), together with the
    // flick notes the swipe was aiming at when it cleared nothing.
    //
    // Off by default: a dense chart is thousands of lines per song.
    // ------------------------------------------------------------------
    struct FlickDebugLog
    {
        bool enabled = false;
        std::FILE* file = nullptr;

        void setEnabled(bool on)
        {
            if (on == enabled) {
                return;
            }
            enabled = on;
            if (file != nullptr) {
                std::fclose(file);
                file = nullptr;
            }
            if (!enabled) {
                return;
            }
            file = std::fopen("flick_debug.log", "w");
            if (file == nullptr) {
                std::printf("[debug] cannot open flick_debug.log\n");
                return;
            }
            std::printf("[debug] flick log -> %s\n", "flick_debug.log");
            write("=== flick debug log (touch samples in event order, all px / px-per-s) ===");
        }

        // printf-style. Goes to stdout as well (only visible in --screenshot
        // runs, where stdout is redirected into cppsekai.log).
        void write(const char* fmt, ...)
        {
            if (!enabled) {
                return;
            }
            char buffer[1024];
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(buffer, sizeof(buffer), fmt, args);
            va_end(args);
            std::printf("%s\n", buffer);
            if (file != nullptr) {
                std::fprintf(file, "%s\n", buffer);
            }
        }

        void flush()
        {
            if (file != nullptr) {
                std::fflush(file);
            }
        }
    };

    FlickDebugLog gFlickLog;

    const char* flickDirName(game::FlickDir dir)
    {
        switch (dir) {
            case game::FlickUp: return "up";
            case game::FlickLeft: return "left";
            case game::FlickRight: return "right";
            default: return "none";
        }
    }

    const char* judgeName(game::Judge judge)
    {
        switch (judge) {
            case game::Judge::Perfect: return "perfect";
            case game::Judge::Great: return "great";
            case game::Judge::Good: return "good";
            case game::Judge::Bad: return "bad";
            case game::Judge::Miss: return "miss";
            default: return "none";
        }
    }

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
    // Only a *successful* judgement re-arms it (see movePointer).
    constexpr double kFlickRefireSec = 0.06;
    // Below this speed (screen px/s, scaled by the window height) the finger
    // counts as parked, so its lane is remembered as the flick's origin lane.
    constexpr float kFlickRestSpeed = 250.0f;

    // A swipe that travelled this far in one direction *is* a flick, whatever
    // the measured speed says (in 1080p-relative pixels). This is the safety
    // net for everything the speed test can get wrong: a slow-reporting panel,
    // a sample batch that collapsed, or a deliberately gentle thumb swipe. For
    // scale, a tap never travels more than ~10px.
    //
    // The mouse gets a much longer one on purpose: it reaches 900 px/s without
    // trying, so it does not need the help, and a mouse player resting a button
    // while sliding to reposition would otherwise fire flicks by accident.
    constexpr float kFlickBigTravelTouch = 56.0f;
    constexpr float kFlickBigTravelMouse = 150.0f;

    // Flick direction from a swipe measured in screen px/s (up / right
    // positive) plus how far the gesture actually travelled in that direction.
    //
    // Two things matter here and both were wrong for touchscreens:
    //
    //  1. Neither axis may win on speed alone. The dominant *travel* decides,
    //     and a small margin goes to the horizontal axis: swiping a left/right
    //     note diagonally used to be classified "up" (the up test ran first and
    //     only needed up >= side*0.5), so the note never cleared and the whole
    //     gesture was consumed - the single biggest reason a touch flick felt
    //     impossible.
    //  2. A long swipe counts even when the measured speed is low. Touch panels
    //     report unevenly, and a batched/collapsed sample under-reports the
    //     speed by a factor of the batch size.
    //
    // `heightScale` (= window height / 1080) keeps the thresholds in the same
    // "feel" at every resolution.
    game::FlickDir flickDirFrom(float upSpeed, float sideSpeed, float travelUp, float travelSide, bool isTouch,
        float heightScale)
    {
        const float upThreshold = (isTouch ? 380.0f : 900.0f) * heightScale;
        const float sideThreshold = (isTouch ? 450.0f : 900.0f) * heightScale;
        const float minTravel = 12.0f * heightScale;
        const float bigTravel = (isTouch ? kFlickBigTravelTouch : kFlickBigTravelMouse) * heightScale;

        const float upTravel = travelUp;
        const float sideTravel = std::abs(travelSide);
        const bool upOk = travelUp > minTravel && (upSpeed > upThreshold || travelUp > bigTravel);
        const bool sideOk = sideTravel > minTravel && (std::abs(sideSpeed) > sideThreshold || sideTravel > bigTravel);
        if (!upOk && !sideOk) {
            return game::FlickNone;
        }
        // 0.85 rather than 0.5: a few degrees past 45 still counts as a side
        // flick, which is what a hand swiping toward a side arrow does.
        const bool sideWins = !upOk || (sideOk && sideTravel >= upTravel * 0.85f);
        if (sideWins) {
            return travelSide > 0.0f ? game::FlickRight : game::FlickLeft;
        }
        return game::FlickUp;
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
        // `path` is UTF-8 (see path_utf8.hpp); the stream has to be opened with
        // it decoded, not with the ANSI code page.
        std::ifstream file(path_utf8::toPath(path), std::ios::binary);
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
                "                [--screenshot <png>] [--screenshot-time <sec>]\n"
            "                [--title <text>] [--lyricist <text>] [--composer <text>]\n"
            "                [--arranger <text>] [--vocal <text>] [--difficulty <text>]\n"
            "                [--width <px>] [--height <px>] [--window <mode>] [--fps <n>]\n"
            "                [--judge-sheet] [--judge-frame <n>] [--test-hits]\n"
            "                [--show-pause-dialog] [--test-restart] [--restart-at <sec>]\n"
            "                [--result-preview] [--result-at <sec>] [--help]\n"
            "                [--confirm-flash [<sec>]] [--settings] [--settings-tab <n>]\n"
            "                [--profile] [--player <name[:org]>] [--player-rank <n>]\n"
            "                [--player-exp <0..1>]\n\n"

            "No --sus: opens the song select screen (scans --charts, then charts/ next\n"
            "to the exe, then the charts/ of the parent folder).\n"
            "--filler: seconds of silence at the head of the BGM (auto-detected when\n"
            "          omitted). --offset: manual fine tune in seconds.\n"
            "--auto: autoplay preview for this run only (the saved setting is not\n"
            "        changed). --screenshot: headless frame dump, then exit.\n"
            "--window: borderless (default) | windowed | fullscreen. --width/--height:\n"
            "          window size (default 1280x720).\n"
            "--render-size <w>x<h>: fixed render mode - draw at this size and scale it\n"
            "                into the window (letterbox). Same as the 渲染模式 setting.\n"
            "--activate-profile <id>: multi-user check - switch the active profile.\n"
            "--fake-pad <button>: hold an Xbox button for half a second at boot\n"
            "                (A/B/X/Y/LB/RB/START/BACK) so the controller bindings can\n"
            "                be checked without a device.\n"
            "--instance <single|multi>: single = refuse to start twice (bring the\n"
            "                running window forward instead), multi = every extra\n"
            "                copy logs in as a different user. Same as 设置 -> 系统.\n"
            "--party [--party-name <name>]: force 多人游玩 on for this run and join\n"
            "                the shared room (multiple copies on one machine; the\n"
            "                first window is the host and owns the song list, the\n"
            "                host alone plays the BGM). Same as 设置 -> 系统 ->\n"
            "                多人游玩.\n"
            "--no-party: force 多人游玩 off for this run (plain single window),\n"
            "                whatever the profile says.\n"
            "--chartdl-test [<sec>]: debug - press the empty song list's\n"
            "                下载谱面 button from the log (a posted click cannot\n"
            "                reach that screen), so the chartdl launch and the\n"
            "                re-scan on its exit can be checked headlessly.\n"
            "--party-auto [<difficulty 0-6>]: drive a room round without touching\n"
            "                the window: the host presses 确定 on whatever chart the\n"
            "                list is sitting on, a member picks that difficulty in\n"
            "                the phone panel and presses 确定.\n"
            "--ui-scale <n>: zoom the song-select and result screens (1.0 = fit the\n"
            "                window, saved in userdata.json). The play screen is not\n"
            "                affected. Useful for high-DPI displays.\n"
            "--fps: cap the frame rate in addition to vsync (0 = vsync only). Lower\n"
            "       values (e.g. 30/60) reduce GPU/CPU load and power draw.\n"
            "Keyboard: Z S X D C V G B H N J M = 12 lanes\n"
            "          SPACE = pause, F = fullscreen, H = debug panel, ESC = back/quit\n"
            "--test-hits: fire the hit effects for upcoming notes without input (debug).\n"
            "--flick-as-tap: judge flick notes as taps for this run only (debug; the\n"
            "               saved 判定 > Flick 视作 Tap setting is not changed).\n"
            "--judge-frame <n>: freeze the judge text on animation frame n (debug).\n"
            "--result-preview: jump straight to the result screen at boot (debug; uses\n"
            "                  the reference screenshot's numbers for pixel checks).\n"
            "--result-at <sec>: show the result screen once the chart reaches <sec>.\n"
            "--confirm-flash [<sec>]: play the 确定 white burst in the song list (debug;\n"
            "                        no song is loaded).\n"
            "--settings [--settings-tab <0-4>]: open the settings card at boot on the\n"
            "          given page (0 演奏 / 1 画面 / 2 判定 / 3 系统 / 4 账户).\n"
            "--profile: open the player profile card (the level chip's card) at boot.\n"
            "--flick-log: write flick_debug.log (every touch sample + the swipe\n"
            "          measurement + the judgement it produced; same switch as\n"
            "          settings > 判定 > Flick 调试日志). For touch-flick diagnosis.\n"
            "--player <name[:org]> / --player-rank <n> / --player-exp <0..1>: seed the\n"
            "          local account for headless checks (--player-exp = how far into the\n"
            "          current rank, i.e. the level chip's green progress bar); memory\n"
            "          only, userdata.json is not touched.\n"
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

    // Per-kind SE gain, following the upstream overlay player: a tap plays at
    // 0.75, a flick at 0.80, a trace (竹节) at 0.82 and a hold tick at 0.92.
    // pjsk does not hit everything at the same level - the sustain sounds sit
    // lower so a dense chart does not turn into a wall of clicks.
    float seGainForKind(float kind)
    {
        switch (static_cast<int>(kind)) {
            case 2: return 0.80f; // flick
            case 3: return 0.82f; // trace / friction
            case 4: return 0.92f; // hold tick
            default: return 0.75f; // tap / critical tap
        }
    }

    void playHitSe(platform::AudioEngine& audio, const game::JudgementEngine& judgement, float seVolume)
    {
        const auto& stats = judgement.stats();
        if (stats.lastJudge == game::Judge::None) {
            return;
        }
        const bool quieter = stats.lastJudge == game::Judge::Good || stats.lastJudge == game::Judge::Bad;
        audio.playSe(seForKind(stats.lastHitKind, stats.lastJudgeCritical),
            seVolume * seGainForKind(stats.lastHitKind) * (quieter ? 0.5f : 1.0f));
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
        const game::JudgementStats& stats, double previousBest, float chartRating,
        const game::AccountData& account, int expGain, int rankUps)
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
        // Player rank: read *after* addPlayerExp(), so the chip already shows
        // the rank this score earned.
        data.playerRank = account.rank;
        data.playerExp = account.exp;
        data.playerExpNeed = game::expToNextRank(account.rank);
        data.expGain = expGain;
        data.rankUps = rankUps;
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

    // Debug (--dump-events <n>): print the first n packed HitEvents after a
    // chart loads. Declared here because startSession (below) reads it.
    int dumpEvents = 0;
    int selectVocal = -1;         // --select-vocal <n>: preselect a vocal version
    std::string dumpStageBgPath;  // --dump-stage-bg <png>: write the generated plate
    double testVocalSwitchSec = -1.0; // --test-vocal-switch <sec>: flip the version once
    bool testVocalSwitchDone = false;

    // `allowMusic = false` is the 多人游玩 member path: the chart, the cover and
    // the stage plate all load as usual, but the BGM stays out of this window -
    // one machine with two copies of the same track playing a few milliseconds
    // apart sounds like an echo, so the host is the only one that plays it.
    bool startSession(Session& session, const game::ChartEntry& entry, platform::Renderer& renderer,
        platform::AudioEngine& audio, game::JudgementEngine& judgement, float noteSpeed,
        std::string& error, bool allowMusic = true)
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

        // Debug (--dump-events <n>): print the first n packed HitEvents. Each
        // event is 7 floats: time, center, width, kind, flags, endTime,
        // volume. kind 0 tap / 1 critical / 2 flick / 3 trace / 4 hold tick /
        // 5 hold marker (endTime valid). Invaluable when a chart "looks
        // wrong": it separates what the parser produced from what the
        // renderer did with it.
        if (dumpEvents > 0) {
            std::printf("[events] %d total, first %d:\n", count, std::min(dumpEvents, count));
            for (int i = 0; i < count && i < dumpEvents; ++i) {
                const float* e = events + static_cast<size_t>(i) * 7;
                std::printf("  #%-4d t=%.3f center=%.3f w=%.2f kind=%d flags=%d end=%.3f vol=%.2f\n", i,
                    static_cast<double>(e[0]), static_cast<double>(e[1]), static_cast<double>(e[2]),
                    static_cast<int>(std::lround(e[3])), static_cast<int>(std::lround(e[4])),
                    static_cast<double>(e[5]), static_cast<double>(e[6]));
            }
            std::fflush(stdout);
        }
        // Chart level drives the score formula's levelFactor (upstream
        // hard-codes RATING = 26; the official level table is used here).
        int chartLevel = game::musicLevel(entry.musicId, entry.difficulty);
        if (chartLevel <= 0) {
            chartLevel = std::atoi(entry.level.c_str());
        }
        judgement.setChartRating(chartLevel > 0 ? static_cast<float>(chartLevel) : 26.0f);

        if (allowMusic && !entry.bgmPath.empty()) {
            // Logged because the file now depends on the vocal version picked in
            // the song select (charts\se_0374_01.mp3 vs an_0374_02.mp3 ...).
            std::printf("[audio] bgm: %s\n", entry.bgmPath.c_str());
            std::fflush(stdout);
            audio.loadMusic(entry.bgmPath, error);
        } else if (!allowMusic) {
            // 多人游玩 member: no BGM here (the host is playing it), so this
            // window's chart clock comes from the wall clock instead - aimed at
            // the start instant the host published.
            std::printf("[audio] bgm muted (multiplayer member; the host plays it)\n");
            std::fflush(stdout);
        }
        // Official pjsk audio starts with fillerSec seconds of silence; chart
        // tick 0 is right after it. Sidecar > auto-detect > 0. The SUS
        // #WAVEOFFSET (positive = audio plays later) is added on top.
        double startPos = entry.audioStartSec;
        if (allowMusic && startPos <= 0.0 && !entry.bgmPath.empty()) {
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

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Crash diagnostics. This is a Windows-subsystem binary, so a fault leaves
// nothing behind: no console output, only the numbers in the Windows dialog
// ("异常偏移: 00000000001d7812"). This writes that same offset, the state that was
// live, and a backtrace to cppsekai-crash.log next to the exe.
//
// Those offsets are RVAs (address minus module base) and resolve to a function
// with .workbuddy/tools/pe_symbols.py - against a build that still has a symbol
// table, i.e. one made with CPSEKAI_DEBUG_SYMBOLS=1. The symbol table does not
// move code, so the offsets match the release exe.
//
// This runs on a broken stack, so it stays tiny: no C++ strings, no allocation,
// plain stdio. The globals below are the only module-level mutable state in the
// program, written once a frame so the log says what was going on.
// ---------------------------------------------------------------------------
volatile long gCrashState = -1;
volatile long gCrashGlassMode = -1;
volatile long gCrashFrameless = -1;
volatile unsigned long long gCrashFrameCount = 0;

void logCrashAddress(FILE* out, const char* tag, const void* address)
{
    HMODULE module = nullptr;
    unsigned long long rva = 0;
    char modulePath[MAX_PATH] = "?";
    const char* base = modulePath;
    if (address != nullptr && GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(address), &module)) {
        if (GetModuleFileNameA(module, modulePath, MAX_PATH) != 0) {
            for (const char* p = modulePath; *p != '\0'; ++p) {
                if (*p == '\\' || *p == '/') {
                    base = p + 1;
                }
            }
        }
        rva = reinterpret_cast<unsigned long long>(address)
            - reinterpret_cast<unsigned long long>(module);
    }
    std::fprintf(out, "  %-8s %s+0x%llX  (rva 0x%llX)\n", tag, base, rva, rva);
}

LONG WINAPI cppsekaiCrashFilter(EXCEPTION_POINTERS* info)
{
    const unsigned long code = (info != nullptr && info->ExceptionRecord != nullptr)
        ? info->ExceptionRecord->ExceptionCode : 0;
    const void* where = (info != nullptr && info->ExceptionRecord != nullptr)
        ? info->ExceptionRecord->ExceptionAddress : nullptr;
    if (FILE* out = std::fopen("cppsekai-crash.log", "a")) {
        std::fprintf(out, "--- crash: exception 0x%08lX\n", code);
        std::fprintf(out, "  state=%ld glassMode=%ld frameless=%ld frames=%llu\n",
            gCrashState, gCrashGlassMode, gCrashFrameless, gCrashFrameCount);
        logCrashAddress(out, "fault", where);
        void* frames[32] = {};
        unsigned short count = 0;
        using CaptureFn =
            unsigned short(__stdcall*)(unsigned long, void**, unsigned long, unsigned long*);
        if (HMODULE kernel = GetModuleHandleA("kernel32.dll")) {
            if (auto capture = reinterpret_cast<CaptureFn>(
                    reinterpret_cast<void*>(GetProcAddress(kernel, "RtlCaptureStackBackTrace")));
                capture != nullptr) {
                count = capture(0, frames, 32, nullptr);
            }
        }
        for (unsigned short i = 0; i < count; ++i) {
            logCrashAddress(out, "bt", frames[i]);
        }
        std::fprintf(out, "--- end\n");
        std::fclose(out);
    }
    // Terminate right away: the Windows dialog would only repeat what is now in
    // the file.
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    // See above: log a crash to cppsekai-crash.log instead of dying silently.
    SetUnhandledExceptionFilter(cppsekaiCrashFilter);
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
    // Windows tools either hand a GUI-subsystem child no console handles at all
    // (then the block above sent everything to cppsekai.log), or - the usual
    // case for a redirection from a shell / driver script - they hand down a
    // console that msvcrt will happily line-buffer. `> run.txt` in any ordinary
    // shell (cmd, PowerShell, Git Bash) is exactly that: the process owns a
    // console *and* a redirected stdout, and the log only reaches the file once
    // a 4 KB buffer happens to fill. That is why a run that stops logging looks
    // like a hang: the last minutes of output are still sitting in msvcrt's
    // buffer when the process is killed. Unbuffered costs one write per line
    // and makes every [party] / [sync] line land the moment it is printed.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
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
    bool profileShot = false;      // headless check: force the profile card open
    std::string playerSpec;        // --player 昵称[:组织]
    bool playerSpecGiven = false;
    int playerRankGiven = -1;      // --player-rank
    double playerExpGiven = -1.0;  // --player-exp: fraction towards the next rank
    int renderSizeW = 0;           // --render-size <w>x<h>: fixed render mode
    int renderSizeH = 0;
    std::string activateProfileArg; // --activate-profile <id> (headless check)
    std::string fakePadButton;      // --fake-pad <A|B|X|Y|LB|RB|START|BACK>
    int instanceModeOverride = 0;   // --instance single|multi
    bool instanceModeGiven = false;
    bool partyForced = false;       // --party: 多人游玩 for this run only
    bool partyGiven = false;
    std::string partyName;          // --party-name: what the room shows for me
    // --party-auto <difficulty>: play a whole room round without a mouse. The
    // host takes this as "press 确定 on the current chart", every window as
    // "pick this difficulty and ready up" - which is what a headless check of
    // the sync needs (two windows started with it play a round together).
    int partyAutoDiff = -1;
    bool partyAutoGiven = false;
    int selectMusicId = 0;         // --select-id: preselect this song id in the list
    bool testRestart = false; // debug: replay "give up -> pick another song"
    double restartAtSec = 8.0;
    bool resultPreview = false; // debug: boot straight into the result screen
    double resultAtSec = -1.0;  // debug: show the result at this chart time
    // Debug: fire the 确定 white burst on its own, without starting a song -
    // the only way to inspect that transition from a --screenshot run.
    bool confirmFlashShot = false;
    double confirmFlashAtSec = 1.0;
    // Debug: press the empty-list 下载谱面 button without a mouse. A posted
    // click cannot reach this one (the button only exists on a screen that needs
    // an interactive session to be driven by PostMessage), so the launch path
    // gets its own switch.
    double chartDlTestAtSec = -1.0;
    int winWidth = 1366;
    int winHeight = 768;
    float uiScaleArg = 1.0f;      // --ui-scale: song-select / result zoom
    bool uiScaleGiven = false;
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
    // --flick-as-tap: per-run override of the 判定 > "Flick 视作 Tap" setting
    // (same rule as --auto - a headless check must not flip the saved value).
    bool flickAsTapGiven = false;
    bool widthGiven = false;
    bool heightGiven = false;
    game::UserSettings userSettings;
    std::map<std::string, game::ScoreRecord> scores;
    // Local, offline account: nickname / school plus the pjsk player rank. Only
    // ever shown in the level chip, the profile card and 设置 -> 账户.
    game::AccountData account;
    // Vocal version picked in the song select for the song under the cursor
    // (index into game::availableVocals(); -1 = the song has no switcher).
    // Written by drawSongSelect, read when a song is started.
    int selectedVocal = selectVocal;

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
        } else if (arg == "--ui-scale" && i + 1 < utf8Argc) {
            uiScaleArg = static_cast<float>(std::atof(utf8Argv[++i]));
            uiScaleGiven = true;
        } else if (arg == "--width" && i + 1 < utf8Argc) {
            winWidth = std::atoi(utf8Argv[++i]);
            widthGiven = true;
        } else if (arg == "--height" && i + 1 < utf8Argc) {
            winHeight = std::atoi(utf8Argv[++i]);
            heightGiven = true;
        } else if (arg == "--render-size" && i + 1 < utf8Argc) {            // "<w>x<h>": switch to the fixed render mode at that size. The
            // window itself still comes from --width/--height (or the saved
            // settings), which is how a letterbox can be produced headlessly.
            const std::string value = utf8Argv[++i];
            const size_t sep = value.find_first_of("xX*");
            if (sep != std::string::npos) {
                renderSizeW = std::atoi(value.substr(0, sep).c_str());
                renderSizeH = std::atoi(value.substr(sep + 1).c_str());
            }
        } else if (arg == "--activate-profile" && i + 1 < utf8Argc) {
            // Headless check for the multi-user switch: the picker in 设置 ->
            // 账户 does the same thing through activateProfile().
            activateProfileArg = utf8Argv[++i];
        } else if (arg == "--fake-pad" && i + 1 < utf8Argc) {
            // Headless check for the controller bindings (see padDown()).
            fakePadButton = utf8Argv[++i];
        } else if (arg == "--instance" && i + 1 < utf8Argc) {
            // single = one copy only (default), multi = extra copies take
            // another user. Same as the 系统 page setting.
            const std::string mode = utf8Argv[++i];
            instanceModeGiven = true;
            instanceModeOverride = mode == "multi" ? 1 : 0;
        } else if (arg == "--party") {
            partyForced = true;
            partyGiven = true;
        } else if (arg == "--no-party") {
            // 多人游玩 off just for this run, whatever the profile says: the
            // single-window path a regression run has to be able to reach.
            partyForced = false;
            partyGiven = true;
        } else if (arg == "--party-name" && i + 1 < utf8Argc) {
            partyName = utf8Argv[++i];
        } else if (arg == "--party-auto") {
            partyAutoGiven = true;
            partyAutoDiff = (i + 1 < utf8Argc && utf8Argv[i + 1][0] != '-')
                ? std::atoi(utf8Argv[++i])
                : -1;
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
        } else if (arg == "--auto") {
            autoPlay = true;
            autoplayGiven = true;
        } else if (arg == "--flick-as-tap") {
            flickAsTapGiven = true;
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
        } else if (arg == "--test-vocal-switch" && i + 1 < utf8Argc) {
            testVocalSwitchSec = std::atof(utf8Argv[++i]);
        } else if (arg == "--dump-stage-bg" && i + 1 < utf8Argc) {
            dumpStageBgPath = utf8Argv[++i];
        } else if (arg == "--select-vocal" && i + 1 < utf8Argc) {
            selectVocal = std::atoi(utf8Argv[++i]);
        } else if (arg == "--dump-events" && i + 1 < utf8Argc) {
            dumpEvents = std::atoi(utf8Argv[++i]);
        } else if (arg == "--select-id" && i + 1 < utf8Argc) {
            selectMusicId = std::atoi(utf8Argv[++i]);
        } else if (arg == "--settings") {
            // Headless check: open the settings card right away, so a
            // --screenshot run can look at it (a key press can't be sent).
            showSettingsShot = true;
        } else if (arg == "--settings-tab" && i + 1 < utf8Argc) {
            settingsTabShot = std::atoi(utf8Argv[++i]);
        } else if (arg == "--profile") {
            // Headless check: open the account's profile card on the song
            // select, so a --screenshot run can look at it (the card normally
            // appears when the level chip is clicked).
            profileShot = true;
        } else if (arg == "--player") {
            // Headless check: seed the account (昵称:组织) without touching
            // userdata.json - the profile card and the settings tab need
            // something to show in a screenshot run. Applied *after* the file
            // is read, so it overrides what is on disk.
            if (i + 1 < utf8Argc) {
                playerSpec = utf8Argv[++i];
                playerSpecGiven = true;
            }
        } else if (arg == "--player-rank" && i + 1 < utf8Argc) {
            playerRankGiven = std::max(1, std::atoi(utf8Argv[++i]));
        } else if (arg == "--player-exp" && i + 1 < utf8Argc) {
            // Fraction of the way to the next rank (the chip's green fill), for
            // screenshot runs - e.g. --player-exp 0.35.
            playerExpGiven = std::clamp(std::atof(utf8Argv[++i]), 0.0, 1.0);
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
        } else if (arg == "--confirm-flash") {
            // Debug: play the 确定 burst on its own in the song list (no song
            // is loaded, so nothing depends on the timing of a real click).
            confirmFlashShot = true;
            if (i + 1 < utf8Argc && utf8Argv[i + 1][0] != '-') {
                confirmFlashAtSec = std::atof(utf8Argv[++i]);
            }
        } else if (arg == "--chartdl-test") {
            // Debug: press 下载谱面 (the empty-list button) at this second.
            chartDlTestAtSec = 1.5;
            if (i + 1 < utf8Argc && utf8Argv[i + 1][0] != '-') {
                chartDlTestAtSec = std::atof(utf8Argv[++i]);
            }
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
        } else if (arg == "--flick-log") {
            // Debug log without touching the profile (headless / support runs).
            gForceFlickLog = true;
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
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
    // Profiles (multi-user): one data file per user, listed in an index next to
    // them. On the very first run loadProfiles() imports the old single
    // userdata.json as the "default" user, so an existing install keeps its
    // scores, settings and account.
    std::string userDataFile = game::userDataPath(baseDir);
    const std::string userDataDir = game::userDataDir(baseDir);
    std::string activeProfileId;
    std::vector<game::UserProfile> profiles = game::loadProfiles(userDataDir, activeProfileId);
    userDataFile = game::profileDataPath(userDataDir, activeProfileId);
    std::printf("[profile] active='%s' (%zu user(s), dir %s)\n", activeProfileId.c_str(),
        profiles.size(), userDataDir.c_str());
    game::loadUserData(userDataFile, userSettings, scores, account);
    // The 用户 combobox lists the profile *files* (profiles/index.json), the
    // nickname lives inside the profile data - two labels for one user. If the
    // file still carries an older label (renamed before this was tied
    // together, or hand-edited), fix it up on load so the combobox agrees.
    // A second window that picked up one of the *other* profiles does the same
    // for its own (see the multi-open branch below).
    auto syncProfileLabel = [&]() {
        if (account.name.empty()) {
            return;
        }
        for (game::UserProfile& user : profiles) {
            if (user.id == activeProfileId && user.name != account.name) {
                user.name = account.name;
                game::saveProfiles(userDataDir, profiles, activeProfileId);
                std::printf("[profile] label of '%s' updated to '%s'\n", user.id.c_str(),
                    user.name.c_str());
                std::fflush(stdout);
                break;
            }
        }
    };
    syncProfileLabel();

    // ------------------------------------------------------------------
    // Instance policy (UserSettings::instanceMode).
    //
    // 0 (default) - one copy only. A second launch finds the running window,
    //               brings it to the front and exits. Two copies writing the
    //               same profile file is the one thing multi-open can corrupt.
    // 1           - several copies, and every extra one logs in as a different
    //               user, so no two windows share a save file.
    //
    // The guard is a named mutex (Local\ so it is per logon session) held for
    // the rest of the process; the OS releases it on exit, even after a crash.
    // ------------------------------------------------------------------
    HANDLE singleInstanceLock = nullptr;
    HANDLE profileLock = nullptr;
    {
        auto toWideAscii = [](const std::string& text) {
            return std::wstring(text.begin(), text.end());
        };
        auto tryLockProfile = [&](const std::string& id) -> HANDLE {
            const std::wstring name = L"Local\\CppSekai.Profile." + toWideAscii(id);
            HANDLE lock = CreateMutexW(nullptr, FALSE, name.c_str());
            if (lock == nullptr) {
                return nullptr;
            }
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                CloseHandle(lock); // someone else's window owns this profile
                return nullptr;
            }
            return lock;
        };

        singleInstanceLock = CreateMutexW(nullptr, FALSE, L"Local\\CppSekai.SingleInstance");
        const bool alreadyRunning = singleInstanceLock != nullptr
            && GetLastError() == ERROR_ALREADY_EXISTS;
        int instanceMode = instanceModeGiven ? instanceModeOverride : userSettings.instanceMode;
        // 多人游玩 needs several windows by definition, so it forces the
        // multi-open policy (each copy then logs in as its own user).
        if (partyGiven ? partyForced : userSettings.multiplayer) {
            instanceMode = 1;
        }
        // 主实例已经开了多人游玩的房间：这个新实例不管自己的设置是什么，都得按
        // 多开起来 —— 否则它拿着 single 策略去抢默认档案、或者干脆把手交回主窗口，
        // 用户看到的就是"第二个窗口起不来 / 连不上"。房间在就是硬事实，优先级高于
        // 本机的 settings.json。
        {
            const bool roomAlreadyOpen = platform::PartyLink::roomExists();
            if (alreadyRunning && roomAlreadyOpen && instanceMode == 0) {
                instanceMode = 1;
                std::printf("[instance] a 多人游玩 room is already open -> multi-open forced\n");
                std::fflush(stdout);
            }
        }

        if (alreadyRunning && instanceMode == 0) {
            // Hand the running window back to the user. The title is unique in
            // this mode by definition (this branch is what keeps it so).
            struct Finder
            {
                static BOOL CALLBACK proc(HWND hwnd, LPARAM param)
                {
                    auto* found = reinterpret_cast<HWND*>(param);
                    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
                        return TRUE; // owned popups are not the main window
                    }
                    wchar_t title[128] = {};
                    if (GetWindowTextW(hwnd, title, 128) == 0 || std::wcscmp(title, L"CppSekai") != 0) {
                        return TRUE;
                    }
                    *found = hwnd;
                    return FALSE;
                }
            };
            HWND existing = nullptr;
            EnumWindows(&Finder::proc, reinterpret_cast<LPARAM>(&existing));
            if (existing != nullptr) {
                ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
            }
            std::printf("[instance] already running%s, brought it to the front and exiting\n",
                existing != nullptr ? "" : " (window not found)");
            std::fflush(stdout);
            SDL_Quit();
            return 0;
        }

        // Every instance holds a lock on the profile it uses, so the next one
        // can tell which are taken. Without this a second window would happily
        // take the same "default" profile and both would write it.
        profileLock = tryLockProfile(activeProfileId);
        if (profileLock != nullptr) {
            std::printf("[instance] single owner of profile '%s'\n", activeProfileId.c_str());
        } else if (instanceMode == 1) {
            std::string chosen;
            for (const game::UserProfile& user : profiles) {
                if (user.id == activeProfileId) {
                    continue;
                }
                profileLock = tryLockProfile(user.id);
                if (profileLock != nullptr) {
                    chosen = user.id;
                    break;
                }
            }
            if (chosen.empty()) {
                // Every existing user is already playing: make one up. It shows
                // up in 设置 -> 账户 like any other and can be renamed there.
                game::UserProfile user;
                user.name = "用户" + std::to_string(profiles.size() + 1);
                user.id = game::makeProfileId(user.name, profiles);
                profiles.push_back(user);
                game::saveProfiles(userDataDir, profiles, activeProfileId);
                profileLock = tryLockProfile(user.id);
                chosen = user.id;
            }
            if (!chosen.empty() && chosen != activeProfileId) {
                activeProfileId = chosen;
                userDataFile = game::profileDataPath(userDataDir, chosen);
                // Start from the defaults, then read: a key missing from the
                // other profile must not inherit this one's value.
                userSettings = game::UserSettings{};
                scores.clear();
                account = game::AccountData{};
                game::loadUserData(userDataFile, userSettings, scores, account);
                // This window ended up on somebody else's profile: give that
                // profile the same label/nickname treatment as the main path.
                syncProfileLabel();
            }
            std::printf("[instance] multi-open -> profile '%s' (%zu user(s))\n", chosen.c_str(),
                profiles.size());
            std::fflush(stdout);
        }
    }
    // --player / --player-rank override what the file just loaded (headless
    // checks: they must never write a fixture into the player's own save).
    if (playerSpecGiven) {
        const size_t sep = playerSpec.find(':');
        account.name = sep == std::string::npos ? playerSpec : playerSpec.substr(0, sep);
        account.org = sep == std::string::npos ? std::string() : playerSpec.substr(sep + 1);
    }
    if (playerRankGiven > 0) {
        account.rank = playerRankGiven;
        account.exp = 0.0;
    }
    if (playerExpGiven >= 0.0) {
        account.exp = playerExpGiven * game::expToNextRank(account.rank);
    }

    // ------------------------------------------------------------------
    // 多人游玩: join the room every other CppSekai window on this machine is
    // in (see platform/Party.hpp). The window that gets seat 0 is the host: it
    // picks the song, publishes it, and is the only one that plays the BGM.
    // Without the setting this object stays inactive and every code path below
    // behaves exactly as it did before multiplayer existed.
    // ------------------------------------------------------------------
    platform::PartyLink party;
    std::string partyLabel; // also goes into the window title (see below)
    const bool partyWanted = partyGiven ? partyForced : userSettings.multiplayer;
    const bool roomOpen = platform::PartyLink::roomExists();
    if (partyWanted || roomOpen) {
        if (party.init()) {
            std::string label = !partyName.empty() ? partyName : account.name;
            if (label.empty()) {
                label = "玩家 " + std::to_string(party.slot() + 1);
            }
            partyLabel = label;
            party.setName(label);
            std::printf("[party] ready: seat %d as %s ('%s'), %d player(s) so far%s\n", party.slot(),
                party.isHost() ? "host" : "member", label.c_str(), party.playerCount(),
                (!partyWanted && roomOpen) ? " [joined a room this profile has switched off]"
                                           : "");
            // Nothing on this machine has a touchscreen-friendly answer to "the
            // first tap on a background window is swallowed": SDL ignores the
            // click that activates a window by default, so the first tap of a
            // note in the window that is not in front would simply vanish.
            SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
        } else {
            std::printf("[party] shared room unavailable, continuing solo\n");
        }
        std::fflush(stdout);
    } else {
        std::printf("[party] off (设置 -> 系统 -> 多人游玩 没开，本机也没有别人开好的房间)\n");
        std::fflush(stdout);
    }
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
    // --render-size <w>x<h>: forces the fixed render mode and pins the game's
    // drawing size, which is what a headless letterbox check needs (the window
    // can then be a different size via --width/--height).
    if (renderSizeW > 0 && renderSizeH > 0) {
        userSettings.renderScale = 1;
        userSettings.windowWidth = renderSizeW;
        userSettings.windowHeight = renderSizeH;
    }
    // Saved resolution applies unless --width/--height overrode it. resW/resH
    // keep the *chosen* size (the live windowW/H follow resizes + fullscreen),
    // so persisting never records the desktop size of a fullscreen session.
    if (!widthGiven && !heightGiven) {
        winWidth = userSettings.windowWidth;
        winHeight = userSettings.windowHeight;
    }
    int resW = std::max(320, renderSizeW > 0 ? renderSizeW : winWidth);
    int resH = std::max(240, renderSizeH > 0 ? renderSizeH : winHeight);
    if (!fpsGiven) {
        fpsLimit = userSettings.fpsLimit;
    }
    if (!offsetGiven) {
        gUserOffsetSec = userSettings.offsetSec;
    }
    if (uiScaleGiven) {
        userSettings.uiScale = std::clamp(uiScaleArg, 0.5f, 2.0f);
    }
    if (!autoplayGiven) {
        autoPlay = userSettings.autoplay;
    }
    showProgressBar = userSettings.showProgressBar;
    hideTouchFeedback = userSettings.hideTouchFeedback;
    // Flick debug log (settings > 判定 > Flick 调试日志). Truncates the file at
    // boot when it is on, so every run starts with a clean log.
    gFlickLog.setEnabled(userSettings.debugLog || gForceFlickLog);
    std::printf("[settings] flick debug log %s\n",
        gFlickLog.enabled ? "on (flick_debug.log)" : "off");
    const int splashStyle = userSettings.splashStyle; // 0=image 1=classic
    // "Aero glass" background: no background fill at all, the window's own
    // pixels stay transparent where nothing is drawn. Needs the same window
    // setup as the image splash (alpha channel + DWM's extended frame), so the
    // two share the checks below.
    const bool glassBackground = userSettings.bgStyle == 2;

    // Set by the window subclass (WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE) and read by
    // the frame pacing: while the window is being dragged, every frame we serve
    // from inside the modal loop blocks that loop for its whole duration, so
    // vsync - which pads every frame out to a vblank - is what makes a drag feel
    // sticky. Off, the frames are only as long as the rendering itself, which
    // gives the modal loop the time to move the window. Declared here because
    // runFrame captures it by reference.

    int windowW = std::max(320, winWidth);
    int windowH = std::max(240, winHeight);
    // Image splash + fullscreen: the boot window must also not span the whole
    // desktop. Windows promotes a window that covers the monitor to
    // "fullscreen optimized" presentation, which bypasses DWM composition -
    // the transparent splash background turns opaque black. The window is
    // fully transparent apart from the picture (which is drawn centred at its
    // native size), so shrinking it by a few pixels is invisible, and it goes
    // fullscreen at the end of the boot sequence anyway.
    if (windowMode == 2 && (splashStyle == 0 || glassBackground)) {
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
    // Image splash is a free-floating PNG, and the glass background is a
    // see-through whole window: both need DWM per-pixel transparency
    // (DwmExtendFrameIntoClientArea with -1 margins) so the pixels the GL frame
    // leaves with alpha < 255 blend with the desktop instead of a black
    // backdrop. The GL frames decide opacity themselves: clearing alpha=0 shows
    // the desktop (blurred by Aero on Win7), clearing alpha=1 (the normal
    // background) is opaque, so this can stay on all run.
    // SDL2 has no transparent-window flag (that is SDL3), so do it manually.
    // The window framebuffer always gets an alpha channel: DwmExtendFrameInto-
    // ClientArea (below) makes the window blend per pixel, and both the image
    // splash and the "Aero glass" background rely on that. It has to be
    // requested before the context exists, so it cannot follow the setting -
    // without it, turning glass on at runtime would silently do nothing.
    // Non-glass runs ignore the extra channel entirely.
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    // Which of the "整块玻璃" variants is in force (设置 -> 玻璃实现, see AGENTS.md
    // 「窗口外观」). Non-const: the settings card changes it at runtime.
    int glassMode = std::clamp(userSettings.glassMode, 0, 2);
    // What the subclassed window procedure needs to know: 1 = "treat this window
    // as frameless right now" (WM_NCCALCSIZE -> 0, WM_NCHITTEST does the rest).
    // Kept separate from glassMode because fullscreen must stay out of it: with the
    // non-client area gone, a window covering the screen would also cover the
    // taskbar.
    int noFrameMode = 0;
#ifdef _WIN32
    // CPSEKAI_GLASS_TOGGLE=<sec>: flip 玻璃实现 once, that many seconds in, through
    // exactly the call the settings card makes. Diagnosis only (see AGENTS.md) - it
    // exists because "切到自绘无框就崩" has to be reproducible without clicking
    // anything.
    const double glassToggleSec = std::getenv("CPSEKAI_GLASS_TOGGLE") != nullptr
        ? std::atof(std::getenv("CPSEKAI_GLASS_TOGGLE")) : -1.0;
    bool glassToggleDone = false;
#endif
#ifdef _WIN32
    // Windows SDK import libs are not part of the toolchain, so this is a
    // dynamic load. Also called again when the setting is toggled at runtime
    // (glass off = zero margins, which puts the frame back and makes the client
    // area opaque again).
    auto applyWindowTransparency = [](SDL_Window* target, bool enable) {
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        if (!SDL_GetWindowWMInfo(target, &wmi) || wmi.subsystem != SDL_SYSWM_WINDOWS) {
            return;
        }
        struct Margins
        {
            int left;
            int right;
            int top;
            int bottom;
        };
        using DwmExtendFn = long(__stdcall*)(HWND, const Margins*);
        if (HMODULE dwm = LoadLibraryA("dwmapi.dll")) {
            if (auto extend = reinterpret_cast<DwmExtendFn>(
                    reinterpret_cast<void*>(GetProcAddress(dwm, "DwmExtendFrameIntoClientArea"))); extend) {
                const Margins full{-1, -1, -1, -1};
                const Margins none{0, 0, 0, 0};
                const long hr = extend(wmi.info.win.window, enable ? &full : &none);
                std::printf("[window] DwmExtendFrameIntoClientArea(%s) -> 0x%lX\n",
                    enable ? "-1 margins" : "no margins", static_cast<unsigned long>(hr));
            }
            FreeLibrary(dwm);
        }
        std::fflush(stdout);
        // Tried and rejected (2026-09-19): DWMWA_NCRENDERING_POLICY =
        // DWMNCRP_DISABLED as a cheap way to drop the frame. On Windows 7 it makes
        // DWM fall back to the Basic frame (no Aero at all), which is uglier than
        // the frame it was supposed to remove - removed, see AGENTS.md.
    };
#endif
    // 多人游玩: several windows with the same title are indistinguishable in
    // the taskbar (and impossible to address for a script), so the room puts
    // the player's name in it.
    const std::string windowTitle = partyLabel.empty() ? std::string("CppSekai")
                                                       : ("CppSekai - " + partyLabel);
    SDL_Window* window = SDL_CreateWindow(
        windowTitle.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowW, windowH,
        windowFlags);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    // Window / taskbar icon, from icon.png next to the exe (or the repo root in
    // the dev layout). The *file* icon comes from the embedded resource
    // (app.rc); this is the one Windows uses while a window is open, and SDL's
    // default would otherwise be a generic application icon. Missing file is
    // not an error - the embedded one still shows in Explorer.
    {
        int iconW = 0;
        int iconH = 0;
        int iconChannels = 0;
        const std::string iconCandidates[] = {
            baseDir + "icon.png", baseDir + "..\\icon.png", std::string("icon.png")};
        for (const std::string& candidate : iconCandidates) {
            stbi_uc* pixels = stbi_load(candidate.c_str(), &iconW, &iconH, &iconChannels, 4);
            if (pixels == nullptr) {
                continue;
            }
            SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(pixels, iconW, iconH, 32, iconW * 4,
                0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
            if (surface != nullptr) {
                SDL_SetWindowIcon(window, surface);
                SDL_FreeSurface(surface);
                std::printf("[window] icon %s (%dx%d)\n", candidate.c_str(), iconW, iconH);
                std::fflush(stdout);
            }
            stbi_image_free(pixels);
            break;
        }
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
    // something to display even if the load below takes a while.
    if (splashStyle == 0 || glassBackground) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    } else {
        glClearColor(0.03f, 0.03f, 0.05f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    SDL_GL_SwapWindow(window);
#ifdef _WIN32
    // One place that decides what the window looks like right now: the DWM side
    // (transparency + NCR policy) and the frame removal are two halves of the same
    // setting and always move together. Called once here and again whenever 背景
    // 或 玻璃实现 changes in the settings.
    // Asked for from the settings card, done at the top of the next frame: removing
    // or restoring the non-client area fires a cascade of *synchronous* messages
    // (WM_NCCALCSIZE, WM_WINDOWPOSCHANGED, and on Windows 7 a WM_SIZE) and the
    // settings card is drawn from the middle of an ImGui frame - the worst place to
    // restructure the window. Park the request and service it before ImGui starts
    // the next frame.
    bool glassReapplyWanted = false;
    bool glassReapplyEnable = false;
    auto requestGlassWindowMode = [&](bool enable) {
        glassReapplyWanted = true;
        glassReapplyEnable = enable;
    };
    auto applyGlassWindowMode = [&](bool enable) {
        noFrameMode = (enable && glassMode == 2 && windowMode != 2) ? 1 : 0;
        applyWindowTransparency(window, enable);
        // WM_NCCALCSIZE only runs when the window box changes, so a runtime switch
        // has to ask for one - SWP_FRAMECHANGED is exactly that request.
        SDL_SysWMinfo wmi;
        SDL_VERSION(&wmi.version);
        if (SDL_GetWindowWMInfo(window, &wmi) && wmi.subsystem == SDL_SYSWM_WINDOWS) {
            SetWindowPos(wmi.info.win.window, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        gCrashGlassMode = glassMode;
        std::printf("[window] glass mode %d (%s), frameless %d\n", glassMode,
            glassMode == 0 ? "extend frame only" :
            glassMode == 1 ? "DWM NCR off" : "own custom frame", noFrameMode);
        std::fflush(stdout);
    };
    // DWM: the first swap above is what the compositor keeps until the game
    // draws over it, so this goes right after it.
    if (splashStyle == 0 || glassBackground) {
        applyGlassWindowMode(true);
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
    // "Aero glass" background: no background plate, alpha-0 clear, so the
    // window's empty pixels show the desktop instead. The window side of this
    // (alpha channel + DWM extended frame) is set up above.
    renderer.setTransparentBackground(glassBackground);

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
    // Render size vs window size. Everything in the game (lanes, HUD, ImGui
    // layout, input) works in `windowW x windowH`; in the fixed-resolution
    // render mode that is the configured resolution and the picture is scaled
    // into the real window, letterboxed. `winPixelW/H` always hold the real
    // window, which is what SDL events and the GL viewport are in.
    int winPixelW = windowW;
    int winPixelH = windowH;
    auto applyRenderMode = [&]() {
        if (userSettings.renderScale == 1) {
            renderer.setRenderTargetSize(std::max(320, resW), std::max(240, resH));
        } else {
            renderer.setRenderTargetSize(0, 0);
        }
        windowW = renderer.width();
        windowH = renderer.height();
        core_api::resize(windowW, windowH, 1.0f);
        std::printf("[window] window=%dx%d render=%dx%d scaleMode=%s\n", winPixelW, winPixelH,
            windowW, windowH, userSettings.renderScale == 1 ? "fixed" : "window");
        std::fflush(stdout);
    };
    applyRenderMode();
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
    // Music master volume from the settings (BGM slider). Set before the first
    // loadMusic / startPreview so the very first track already obeys it.
    audio.setBgmVolume(userSettings.bgmVolume);
    // UI sound effects (click / select / level_choose / window_open / close).
    // Requests are queued by the widgets and resolved once per frame below; the
    // base gain is scaled by the same SE volume the hit sounds use.
    ui::bindSe(&audio, 0.8f * seVolume);
    bootLog("audio");
    drawSplash(0.92f, "loading audio");

    // ------------------------------------------------------------------
    // Judgement
    // ------------------------------------------------------------------
    game::JudgementEngine judgement;
    // Starting life is a user setting (settings > 判定 > 初始血量) and the engine
    // seeds every session from its own copy on reset(), so it is pushed once here
    // and again whenever the slider moves.
    judgement.setInitialLife(userSettings.initialLife);
    std::printf("[settings] initialLife setting=%.0f engine=%.0f\n", userSettings.initialLife,
        judgement.initialLife());
    std::fflush(stdout);

    // Apply the persisted judgement settings before anything is judged.
    {
        game::JudgementWindows windows;
        windows.perfectMs = userSettings.perfectMs;
        windows.greatMs = userSettings.greatMs;
        windows.goodMs = userSettings.goodMs;
        windows.badMs = userSettings.badMs;
        windows.missAfterMs = userSettings.linkBadMiss ? userSettings.badMs : userSettings.missMs;
        windows.holdTailGraceMs = userSettings.holdTailGraceMs;
        windows.holdStartGraceMs = userSettings.holdStartGraceMs;
        judgement.setWindows(windows);
        judgement.setStrictFlick(userSettings.strictFlick);
        judgement.setFlickAsTap(flickAsTapGiven || userSettings.flickAsTap);
        // One line that says exactly what is in force, so a "the judgement feels
        // wrong" report can be checked against the numbers without opening the
        // dialog. missAfter is what an untouched note waits for.
        std::printf("[settings] windows perfect=%.0f great=%.0f good=%.0f bad=%.0f missAfter=%.0f "
                    "holdTail=%.0f holdStart=%.0f linked=%d\n",
            windows.perfectMs, windows.greatMs, windows.goodMs, windows.badMs, windows.missAfterMs,
            windows.holdTailGraceMs, windows.holdStartGraceMs, userSettings.linkBadMiss ? 1 : 0);
        std::fflush(stdout);
    }

    // Play results (cleared / full combo) + song select UI assets.
    // setSelectAssetDir expects the assets root; SongSelect appends "select\\".
    // `scores` was already loaded from userdata.json near the top of main().
    game::setSelectAssetDir(baseDir + "assets");

    // Player level chip glyph (assets/select/level.png). Missing is harmless:
    // the chip falls back to a drawn note.
    if (const GLuint levelIcon = renderer.loadUiTexture(baseDir + "assets\\select\\level.png", error);
        levelIcon != 0) {
        ui::setLevelIconTexture(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(levelIcon)));
    } else {
        std::printf("[ui] no level icon (%s)\n", error.c_str());
        error.clear();
    }

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
        // Glass mode draws no wash at all in the song select (the wallpaper and
        // the built-in gradient both go away); the floating shapes stay.
        game::setSelectTransparentBackground(userSettings.bgStyle == 2);
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

    // Official vocal versions (music-vocals.json, next to musics.json). The
    // song select grows a version switcher for songs that have more than one
    // version's mp3 sitting next to the chart; without the table there is no
    // switcher, everything else works the same.
    for (const std::string& candidate :
        {baseDir + "music-vocals.json", baseDir + "..\\music-vocals.json",
            std::string("music-vocals.json")}) {
        std::ifstream probe(candidate, std::ios::binary);
        if (probe.good()) {
            probe.close();
            game::loadMusicVocals(candidate);
            break;
        }
    }

    // ------------------------------------------------------------------
    // UI fonts
    // ------------------------------------------------------------------
    game::loadIntroFonts();
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
                // SDL_SetWindowBordered re-applies a caption but NOT the resize
                // frame (WS_THICKFRAME), so the image splash left a window that
                // looked normal and could not be dragged by its edges.
                SDL_SetWindowResizable(window, SDL_TRUE);
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
    std::vector<game::ChartEntry> entries;
    // ------------------------------------------------------------------
    // Chart scan. Every candidate folder is scanned and merged, not just the
    // first one that has something: the downloader writes to <exe>\charts
    // while an older library may still sit in ..\charts, and both have to be
    // visible at once. A chart that exists in two folders is listed once (the
    // earlier candidate wins). `chartsDir` is the folder the downloader's
    // "play this now" hand-off (--select-id) and the empty-state hint use.
    // ------------------------------------------------------------------
    auto chartFileKey = [](const std::string& path) {
        const size_t cut = path.find_last_of("\\/");
        return cut == std::string::npos ? path : path.substr(cut + 1);
    };
    auto scanAllChartDirs = [&]() {
        std::vector<game::ChartEntry> found;
        std::set<std::string> seen;
        for (const std::string& candidate : chartCandidates) {
            std::vector<game::ChartEntry> scanned = game::scanChartFolder(candidate);
            // Remember the first candidate that had anything: this used to be
            // found by scanning every candidate a *second* time further down,
            // which doubled the whole chart scan for nothing (~1.3 s on a
            // 700-chart library).
            if (!scanned.empty() && chartsDir.empty()) {
                chartsDir = candidate;
            }
            for (game::ChartEntry& entry : scanned) {
                const std::string key = chartFileKey(entry.susPath);
                if (key.empty() || !seen.insert(key).second) {
                    continue;
                }
                found.push_back(std::move(entry));
            }
        }
        // Same ordering scanChartFolder() applies inside one folder: title (or
        // the file-name fallback) first, difficulty as the tiebreak.
        std::stable_sort(found.begin(), found.end(),
            [](const game::ChartEntry& a, const game::ChartEntry& b) {
                const std::string left = a.title.empty() ? a.displayName : a.title;
                const std::string right = b.title.empty() ? b.displayName : b.title;
                if (left != right) {
                    return left < right;
                }
                return a.difficulty < b.difficulty;
            });
        return found;
    };

    std::vector<game::ChartEntry>& chartEntries = entries;
    chartEntries = scanAllChartDirs();
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
    if (selectMusicId > 0) {
        // --select-id <id>: park the list on that song (used by screenshots and
        // by the chart downloader's "play this" hand-off).
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].musicId == selectMusicId) {
                selected = static_cast<int>(i);
                                break;
            }
        }
    }
    std::string loadedCoverPath;
    // Jacket whose stage backdrop is currently installed ("" = the default
    // plate). A CPU composite is only worth redoing when the song changes.
    std::string stageBackgroundFor;

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
        // 多人游玩: the host is the one with the BGM, so it is the one Windows
        // should hear about - two windows announcing the same live fight over
        // the media widget and the taskbar bar.
        if (userSettings.reportSmtc && (!party.active() || party.isHost())) {
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
    // to fade in, so the lead-in can never be shorter than that. A 多人游玩 run
    // replaces it with the host's value so every window's card lines up.
    const double baseLeadInSec = std::max(leadIn, static_cast<double>(game::kMinLeadInSec));
    double leadInSec = baseLeadInSec;
    bool paused = false;
    bool pauseDialogOpen = false; // pjsk style pause dialog (重试/放弃/继续演出)
    bool running = true;
    bool fullscreen = false;
    bool showDebug = showSettingsShot; // H / the musicsetting button on the song select
    if (profileShot) {
        game::debugOpenProfileCard(true);
    }
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 perfStart = SDL_GetPerformanceCounter();

    // Clock without BGM (auto mode / missing audio): wall clock. In 多人游玩
    // this is also the follower's raw clock: armed on the instant the host
    // published, then steered onto the host's audio clock (see songClock()).
    auto wallSongTime = [&]() {
        return -leadInSec + static_cast<double>(SDL_GetPerformanceCounter() - perfStart) / static_cast<double>(perfFreq);
    };
    // `at` (a QPC value, 0 = right now) arms the clock on an absolute instant
    // instead of the current frame - that is what makes several windows reach
    // chart time 0 together.
    auto beginSessionClockAt = [&](Uint64 at) {
        perfStart = at != 0 ? at : SDL_GetPerformanceCounter();
        audio.start(leadInSec);
    };
    auto beginSessionClock = [&]() { beginSessionClockAt(0); };

    std::vector<TouchTrack> touches;
    bool keyHeld[12] = {};
    std::array<float, LANE_COUNT> lanePress{};
    std::array<float, LANE_COUNT> laneHover{};
    double lastFrameDeltaSec = 0.0;
    Uint64 lastFrameCounter = SDL_GetPerformanceCounter();
    SDL_Event event;

    // ------------------------------------------------------------------
    // 多人游玩 runtime state (see platform/Party.hpp).
    //
    // The host - the first window - owns the room: it picks the song (as it
    // moves the list cursor the song is published, so every other window's
    // phone panel follows), decides when the live starts, and is the only one
    // with a decoded BGM. Everybody else runs the same chart with the BGM muted
    // and their clock derived from the host's: the same QPC start instant (QPC
    // is machine-wide, so "start at T" means the same for every window), then a
    // slow correction onto the host's published audio clock. The correction is
    // capped at 3% of speed, i.e. invisible, and is what keeps the windows
    // locked together even when the host's audio device takes a few ms to
    // actually start.
    //
    // 确定 is the room's one button: the host locks/publishes the song with it,
    // a member marks itself ready with it, and the live starts the moment every
    // player still in the round has pressed it - hence the three flags below.
    // ------------------------------------------------------------------
    game::ChartEntry mpEntry;   // the chart this window plays in the room
    int mpEntryIndex = -1;      // its index in `entries` (host side)
    int mpMyDifficulty = -1;    // canonical difficulty index picked in the room
    bool mpConfirmed = false;   // this window pressed 确定 for the current song
    bool mpSpectating = false;  // 旁观: out of this round, still in the room
    bool mpPublished = false;   // host: the focused song is already published
    std::string mpPublishedKey; // host: the chart file name it published
    int mpSeenLockEpoch = -1;   // last song lock this window reacted to
    int mpSeenChargeEpoch = -1; // last start this window loaded a chart for
    int mpConfirmedEpoch = -1;  // host: the lock epoch its 确定 belongs to
    bool mpStartPending = false;
    Uint64 mpStartCounter = 0;
    // Host: the charge epoch whose start instant this window has already armed,
    // and when the loading phase opened. The epoch guards the arming block
    // against re-arming every frame (which would push the instant forward for
    // ever); the timestamp is the stall watchdog, so one window that never
    // finishes loading cannot hold the room on the song list for good.
    int mpArmedEpoch = -1;
    Uint64 mpChargeStartCounter = 0;
    bool mpFollowing = false;   // member without BGM: follow the host's clock
    bool mpClockSynced = false;
    double mpClockOffset = 0.0;
    bool mpHostPaused = false;
    double mpFrozenTime = 0.0;
    // Host-side freeze for a chart whose clock is the wall clock (no BGM): the
    // pause has to hold *this* window's time still as well, because this is the
    // value it publishes for the members to follow.
    bool mpHostFreezeValid = false;
    double mpHostFreezeTime = 0.0;
    std::string mpStatus;       // room status line (banner + phone panel)
    double frameSongTime = 0.0; // this frame's chart clock, resolved once
    // --party-auto bookkeeping: the host's 确定 fires 1.8s in (the song list has
    // settled by then), a member picks its difficulty one second after the
    // host's song shows up.
    bool partyAutoConfirmed = false;
    double partyAutoAtSec = 1.8;
    double partyAutoMemberAt = 0.0;
    bool partyAutoContinued = false; // --party-auto: 继续 on the result screen
    bool partyAutoReady = false;
    bool chartDlTestFired = false;   // --chartdl-test: the auto-press ran once

    auto partyUsable = [&]() { return party.active() && party.playerCount() >= 2; };
    // ---- chartdl (the standalone chart downloader) --------------------------
    // The empty song list offers a 下载谱面 button (game::SelectDownload); this
    // is what it does. chartdl.exe sits *next to* the game exe, and like the
    // game it must be started with the exe's own directory as the working
    // directory - it resolves its data files and its default download folder
    // relative to that, not to whatever directory the game was launched from.
    // The child is a totally separate GUI process, so the game keeps drawing
    // behind it; its handle is polled in the frame loop and the chart scan runs
    // again once it exits (that is what the player expects after picking songs
    // in the downloader).
    void* chartDlProcess = nullptr; // HANDLE of the running downloader, 0 = none
    std::string chartDlPath;        // resolved exe path, so the search runs once
    bool chartDlMissing = false;    // reported once, not every frame

    auto chartDlWindow = []() -> HWND {
#ifdef _WIN32
        // The downloader's window title is a fixed string (chartdl.cpp), which
        // is the only handle we have without a process -> window enumeration.
        return FindWindowW(L"#32770", L"CppSekai 谱面下载器");
#else
        return nullptr;
#endif
    };

    auto launchChartDownloader = [&]() -> bool {
#ifdef _WIN32
        if (chartDlProcess != nullptr) {
            // Already open: bring it back to the front instead of starting a
            // second copy (two downloaders would fight over the same files).
            if (HWND window = chartDlWindow()) {
                SetForegroundWindow(window);
            }
            return true;
        }
        if (chartDlPath.empty() && !chartDlMissing) {
            // <exe>\chartdl.exe, with the dev-tree fallback: a packaged build
            // keeps it next to the game, a build/ layout keeps the game in
            // build/ and the downloader right there with it.
            const std::string candidates[] = {
                baseDir + "chartdl.exe",
                baseDir + "build\\chartdl.exe",
                baseDir + "..\\build\\chartdl.exe",
            };
            for (const std::string& candidate : candidates) {
                std::error_code ec;
                if (std::filesystem::is_regular_file(path_utf8::toPath(candidate), ec)) {
                    chartDlPath = candidate;
                    break;
                }
            }
            if (chartDlPath.empty()) {
                chartDlMissing = true;
                std::printf("[chartdl] chartdl.exe not found next to %s\n", baseDir.c_str());
                std::fflush(stdout);
                return false;
            }
        }
        if (chartDlPath.empty()) {
            return false;
        }
        // UTF-8 -> wide throughout: CreateProcessW is the only path that
        // survives a non-ASCII install directory (the ANSI one mangles it).
        const std::wstring wide = path_utf8::widen(chartDlPath);
        const std::wstring cwd = path_utf8::widen(baseDir);
        std::vector<wchar_t> cmd(wide.begin(), wide.end());
        cmd.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        // lpApplicationName = the exe, lpCommandLine = its own path (the argv
        // the child sees), so quoting inside the command line is a non-issue.
        if (CreateProcessW(wide.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(),
                &si, &pi)) {
            CloseHandle(pi.hThread);
            chartDlProcess = pi.hProcess;
            std::printf("[chartdl] launched %s\n", chartDlPath.c_str());
            std::fflush(stdout);
            return true;
        }
        std::printf("[chartdl] CreateProcessW failed (%lu)\n",
            static_cast<unsigned long>(GetLastError()));
        std::fflush(stdout);
        return false;
#else
        return false;
#endif
    };

    // Once per frame; true exactly on the frame the downloader exited.
    auto pollChartDownloader = [&]() -> bool {
#ifdef _WIN32
        if (chartDlProcess == nullptr) {
            return false;
        }
        HANDLE process = static_cast<HANDLE>(chartDlProcess);
        if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) {
            return false;
        }
        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        CloseHandle(process);
        chartDlProcess = nullptr;
        std::printf("[chartdl] exited (%lu); re-scanning charts\n",
            static_cast<unsigned long>(code));
        std::fflush(stdout);
        return true;
#else
        return false;
#endif
    };
    // frame: following the host involves a correction step, and applying that
    // step several times per frame would speed it up.
    auto resolveSongClock = [&]() -> double {
        const double local = wallSongTime();
        if (!mpFollowing) {
            // Host (or a solo window). The audio clock is the good one when
            // there is a track, but it is not the only case: a chart with no BGM
            // (or one still in its lead-in) runs on the wall clock, and the wall
            // clock knows nothing about the pause. Freezing here is what makes
            // 暂停 stop the picture *and* the published clock - without it the
            // host kept publishing an advancing instant while its own window sat
            // still, and every member's chart clock was dragged along with it.
            frameSongTime = audio.hasMusic() ? audio.songTime() : local;
            if (paused && !audio.hasMusic()) {
                if (!mpHostFreezeValid) {
                    mpHostFreezeTime = frameSongTime;
                    mpHostFreezeValid = true;
                }
                frameSongTime = mpHostFreezeTime;
            } else if (!paused) {
                mpHostFreezeValid = false;
            }
        } else {
            double hostTime = 0.0;
            Uint64 hostCounter = 0;
            const bool havePacket = party.readHostClock(hostTime, hostCounter);
            if (mpHostPaused) {
                // The host's clock is standing still; mirror it exactly instead
                // of extrapolating, or this window would run away while paused.
                if (havePacket) {
                    mpFrozenTime = hostTime;
                }
                frameSongTime = mpFrozenTime;
            } else {
                if (havePacket) {
                    const double nowSec = platform::PartyLink::counterToSeconds(
                        platform::PartyLink::nowCounter());
                    const double hostNow = hostTime
                        + (nowSec - platform::PartyLink::counterToSeconds(hostCounter));
                    const double error = hostNow - local;
                    if (std::fabs(error) > 5.0) {
                        // Nothing legitimate is hours away: that is a torn read
                        // or a packet from another run. Ignore it - taking it
                        // would throw this window's chart clock into the future
                        // and mark every remaining note as a miss.
                        std::printf("[party] clock sample rejected (%.3fs off)\n", error);
                        std::fflush(stdout);
                    } else if (!mpClockSynced || local < 0.5) {
                        // First packet, and everything before the chart starts
                        // (the opening card does not care about a 50 ms jump):
                        // follow the host exactly, so the residual is down to
                        // noise by the time the first note is judged. Waiting
                        // out a slow slew here would leave the first notes tens
                        // of milliseconds apart.
                        mpClockOffset = error;
                        mpClockSynced = true;
                    } else if (std::fabs(error - mpClockOffset) > 0.20) {
                        // A pause, a window that was stalled or a host that
                        // restarted its clock: too far to walk, so jump.
                        mpClockOffset = error;
                    } else {
                        // Speed limit. Normally 3% - far below what an eye can
                        // notice - but the first seconds of the live get a much
                        // faster one: the host's audio clock is re-anchored the
                        // moment its music actually starts, which steps it by up
                        // to a frame (25 ms at 30 fps) right around chart time 0,
                        // and that is exactly where the first notes are judged.
                        // Converging at 3% would leave them tens of ms apart for
                        // seconds; at 50% the step is gone in ~50 ms.
                        const double rate = local < 3.0 ? 0.50 : 0.03;
                        const double step = rate * lastFrameDeltaSec;
                        mpClockOffset += std::clamp(error - mpClockOffset, -step, step);
                    }
                }
                frameSongTime = local + mpClockOffset;
            }
        }
        return frameSongTime;
    };

    // The chart clock to judge / draw against. Identical to the host's own
    // audio clock on the host; on a 多人游玩 member it is the synced value from
    // resolveSongClock() (that window has no BGM of its own, so its raw clock
    // is the wall clock).
    auto songClock = [&]() -> double {
        if (mpFollowing) {
            return frameSongTime;
        }
        return audio.hasMusic() ? audio.songTime() : wallSongTime();
    };

    // File name of a chart path: the room identifies a song by it, because the
    // host's "0374_master.sus" is the same file in every window.
    auto chartFileName = [](const std::string& path) {
        const std::size_t sep = path.find_last_of("\\/");
        return sep == std::string::npos ? path : path.substr(sep + 1);
    };
    // Index into `entries` of the locked song's `difficultyIndex` chart
    // (-1 = this window has no such chart).
    auto findPartyEntry = [&](int musicId, const std::string& songKey, int difficultyIndexWanted) {
        const std::string key = chartFileName(songKey);
        int target = musicId;
        if (target <= 0) {
            for (const game::ChartEntry& entry : entries) {
                if (chartFileName(entry.susPath) == key) {
                    target = entry.musicId;
                    break;
                }
            }
        }
        int fallback = -1;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const game::ChartEntry& entry = entries[i];
            const bool same = target > 0 ? entry.musicId == target : chartFileName(entry.susPath) == key;
            if (!same) {
                continue;
            }
            if (game::difficultyIndex(entry.difficulty) == difficultyIndexWanted) {
                return static_cast<int>(i);
            }
            if (fallback < 0) {
                fallback = static_cast<int>(i);
            }
        }
        return fallback;
    };
    // Chart index of whatever the room is locked on, in *this* window's list
    // (-1 = nothing locked, or this window does not have that song). The room
    // identifies a song by file name / music id, never by a path only the host
    // can see.
    auto partyLockedEntry = [&](const platform::PartyState& snap) {
        const bool locked = snap.phase == platform::PartySongLocked
            || snap.phase == platform::PartyCharging || snap.phase == platform::PartyRunning;
        return locked ? findPartyEntry(snap.musicId, snap.songKey, -1) : -1;
    };
    // Chart time 0 sits this far past the shared start instant. It is the song's
    // own lead-in (the opening card + the stage fade-in) and nothing else: the
    // previous extra four seconds of "load grace" were felt as a countdown
    // nobody asked for.
    auto partyLeadInSec = [&](const platform::PartyState& snap) {
        return std::max(static_cast<double>(game::kMinLeadInSec),
            static_cast<double>(snap.leadInMs) / 1000.0);
    };
    // Safety net for the loading phase (see the host's arming block): a window
    // that never reports its chart decoded - a failed load, a hung decode -
    // must not hold the whole room on the song list for ever. Generous on
    // purpose; a healthy load is well under a second.
    constexpr double kPartyLoadTimeoutSec = 6.0;
    // The one way into the pause dialog. In 多人游玩 only the host may pause: it
    // owns the clock everybody else follows, so a member pausing itself would
    // do nothing but desync its own window.
    auto requestPause = [&]() {
        if (pauseDialogOpen) {
            return;
        }
        if (party.active() && !party.isHost()) {
            mpStatus = "多人游玩中：暂停由房主控制";
            std::printf("[party] pause ignored (only the host can pause a shared live)\n");
            std::fflush(stdout);
            return;
        }
        if (party.active()) {
            party.setHostPaused(true);
        }
        paused = true;
        audio.pause();
        pauseDialogOpen = true;
    };
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
        // (userSettings.multiplayer is a launch-time switch: the room is joined
        // at startup, so --party never writes it back into the profile.)
        const game::JudgementWindows& w = judgement.windows();
        userSettings.perfectMs = w.perfectMs;
        userSettings.greatMs = w.greatMs;
        userSettings.goodMs = w.goodMs;
        userSettings.badMs = w.badMs;
        userSettings.missMs = w.missAfterMs;
        userSettings.holdTailGraceMs = w.holdTailGraceMs;
        userSettings.holdStartGraceMs = w.holdStartGraceMs;
        userSettings.strictFlick = judgement.strictFlick();
        userSettings.flickAsTap = judgement.flickAsTap();
        game::saveUserData(userDataFile, userSettings, scores, account);
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

    // -----------------------------------------------------------------------
    // Screen transitions (both drawn on the foreground draw list, see the end of
    // the frame loop).
    // -----------------------------------------------------------------------
    // Confirm: a white burst out of the 确定 button that fills the screen while the
    // next song is being loaded. Loading the chart, its audio and its generated
    // stage plate takes over a second, and a frozen frame is exactly what this
    // hides - the session is only started once the screen is fully white.
    constexpr float kConfirmExpand = 0.28f; // burst grows from the button
    constexpr float kConfirmAttack = 0.09f; // soft onset - no hard "pop" on frame 0
    constexpr float kConfirmHold = 0.16f;   // brightest, session loads here
    constexpr float kConfirmFade = 0.75f;   // the glow lifts off over the intro
    // Peak brightness of the flash. Deliberately below 1: a frame of solid
    // white is a cut, not light (the old 1.0 + flat 50% ray triangles is what
    // read as "hard and untransparent").
    constexpr float kConfirmPeak = 0.88f;
    bool confirmFlashActive = false;
    float confirmFlashTime = 0.0f;
    ImVec2 confirmFlashOrigin{0.0f, 0.0f};
    bool confirmStartPending = false;
    bool confirmFlashShotFired = false;
    game::ChartEntry confirmPendingEntry;
    ImVec2 selectConfirmCenter{0.0f, 0.0f}; // filled by drawSongSelect each frame
    // Song end: as the track runs out the screen goes black, and the result screen
    // fades that black back off. One value drives both halves.
    constexpr float kSongEndFadeSec = 1.2f;
    float songEndBlackout = 0.0f;
    double resultPreviousBest = 0.0;
    // Player rank bookkeeping for the run that is currently ending: what the
    // score granted and how many ranks it rolled over (see the result screen).
    int resultExpGain = 0;
    int resultRankUps = 0;
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
    // Profiles (multi-user): switching replaces settings + scores + account and
    // re-applies the subset that can change live. Window mode / resolution /
    // splash style are read once at boot, so those need a restart - everything
    // else (volumes, offset, note speed, judgement, ui scale, background)
    // takes effect on the spot.
    // ------------------------------------------------------------------
    auto activateProfile = [&](const std::string& id) {
        const auto found = std::find_if(profiles.begin(), profiles.end(),
            [&](const game::UserProfile& user) { return user.id == id; });
        if (found == profiles.end() || id == activeProfileId) {
            return;
        }
        // Persist the outgoing profile before anything else: the in-memory
        // settings/scores belong to it.
        game::saveUserData(userDataFile, userSettings, scores, account);

        userDataFile = game::profileDataPath(userDataDir, id);
        // Start from the defaults, then read: a key missing from the new file
        // must not silently inherit the previous user's value.
        game::UserSettings freshSettings;
        std::map<std::string, game::ScoreRecord> freshScores;
        game::AccountData freshAccount;
        game::loadUserData(userDataFile, freshSettings, freshScores, freshAccount);
        userSettings = freshSettings;
        scores = freshScores;
        account = freshAccount;
        activeProfileId = id;
        game::saveProfiles(userDataDir, profiles, activeProfileId);
        syncProfileLabel(); // the combobox label follows the nickname
        if (party.active()) {
            party.setName(account.name.empty() ? partyLabel : account.name);
        }

        // Re-derive every live mirror from the profile that was just loaded.
        // persistUserData() copies these *into* userSettings before saving, so
        // leaving the old user's values here would write them into the new
        // user's file on the spot. Command-line overrides keep winning, exactly
        // like they do at boot.
        if (!speedGiven) {
            noteSpeed = userSettings.noteSpeed;
        }
        if (!seGiven) {
            seVolume = userSettings.seVolume;
        }
        if (!leadInGiven) {
            leadIn = userSettings.leadInSec;
        }
        if (!offsetGiven) {
            gUserOffsetSec = userSettings.offsetSec;
        }
        if (!windowGiven) {
            windowMode = userSettings.windowMode;
        }
        if (!fpsGiven) {
            fpsLimitLive = userSettings.fpsLimit;
        }
        if (!widthGiven && !heightGiven) {
            // The window itself only follows at the next launch, but in the
            // fixed render mode this is the size the game draws at - that part
            // takes effect right here.
            resW = std::max(320, userSettings.windowWidth);
            resH = std::max(240, userSettings.windowHeight);
        }
        audio.setBgmVolume(userSettings.bgmVolume);
        audio.setUserOffset(userSettings.offsetSec);
        ui::bindSe(&audio, 0.8f * seVolume);
        autoPlay = userSettings.autoplay;
        showProgressBar = userSettings.showProgressBar;
        hideTouchFeedback = userSettings.hideTouchFeedback;
        gFlickLog.setEnabled(userSettings.debugLog || gForceFlickLog); // the new profile's own setting
#ifdef _WIN32
        applyTouchFeedback(window, hideTouchFeedback);
#endif
        judgement.setStrictFlick(userSettings.strictFlick);
        judgement.setFlickAsTap(flickAsTapGiven || userSettings.flickAsTap);
        judgement.setInitialLife(userSettings.initialLife);
        {
            game::JudgementWindows windows;
            windows.perfectMs = userSettings.perfectMs;
            windows.greatMs = userSettings.greatMs;
            windows.goodMs = userSettings.goodMs;
            windows.badMs = userSettings.badMs;
            windows.missAfterMs = userSettings.linkBadMiss ? userSettings.badMs : userSettings.missMs;
            windows.holdTailGraceMs = userSettings.holdTailGraceMs;
            windows.holdStartGraceMs = userSettings.holdStartGraceMs;
            judgement.setWindows(windows);
        }
        core_api::setPreviewConfig(0, 1, 1, 1, 0, 0, noteSpeed, 1.0f, 0.6f, 0.0f, 1.0f, 0.85f);
        refreshSelectBackdrop();
        applyRenderMode();
        systemMedia.setReporting(userSettings.reportSmtc);
        game::applyScores(entries, scores);
        std::printf("[profile] switched to '%s' (%zu score(s))\n", id.c_str(), scores.size());
        std::fflush(stdout);
        persistUserData();
    };

    if (!activateProfileArg.empty()) {
        activateProfile(activateProfileArg);
    }

    // ------------------------------------------------------------------
    // Settings card ("debug panel"), shared by the song select and the
    // play states. Opened with H or the musicsetting button; alive flag
    // keeps it on screen while the close animation plays out.
    // ------------------------------------------------------------------
    // 演奏 / 画面 / 判定 / 系统 / 账户. Hoisted out of the card lambda so the
    // pad's shoulder buttons can switch pages from the input side.
    // Long-note tolerance presets (settings > 判定 > 长条容错): {松手容错, 起按容错}.
    // Index 0 is the classic feel the engine shipped with, 2 the tightest.
    static constexpr float kHoldGracePresets[3][2] = {
        {180.0f, 140.0f}, // 宽容
        {120.0f, 80.0f},  // 较紧
        {60.0f, 40.0f},   // 严格
    };
    const std::vector<std::string> kHoldPresetLabels = {"宽容", "较紧", "严格"};
    // One slot per preset, in list order. In pick-one mode the capsules are
    // drawn in this order (left to right) and a press walks the selection by
    // one - so the signs only say which way each capsule moves the selection.
    const std::vector<float> kPresetChoiceDeltas = {-1.0f, -1.0f, -1.0f};
    constexpr int kSettingsTabCount = 5;
    int settingsTab = settingsTabShot >= 0 ? settingsTabShot : 0;
    // True only on the frame the card opens. The judgement page rebuilds its
    // working copy then (see tab 2), which must happen *before* the sliders
    // are laid out - so last frame's value is kept here rather than inside the
    // card lambda, where it would only become visible one frame too late.
    bool settingsWasOpen = false;
    // 开启多开（实验性）的确认框。选「允许多开」或勾「多人游玩」时先弹一张卡
    // 说明这是实验性功能，确认后才真的写进设置。
    bool multiInstanceAsk = false;
    bool multiInstanceAskFromParty = false; // true = 用户点的是多人游玩
    auto drawSettingsCard = [&]() {
        static bool settingsAlive = false;
        if (showDebug) {
            settingsAlive = true;
        }
        if (!settingsAlive) {
            return;
        }
        const bool settingsJustOpened = !settingsWasOpen;
        settingsWasOpen = true;
        // pjsk style settings panel (tabbed card, pjsk sliders).
        const float s = ui::scale();
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        // 760 tall (was 520 -> 640 -> 700): the 判定 tab now carries seven rows
        // (Perfect/Great/Good/Bad/Miss + the long-note preset stepper and its
        // two sliders) and 画面 is also deep, so the card has to hold both
        // without immediately scrolling. The tab content is still clipped by a
        // child, so a future row can never run under the 关闭 button.
        ImVec2 cardSize = ImVec2(380.0f * s, 780.0f * s);
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

            // Hoisted out of this lambda (see settingsTab): the pad's LB/RB
            // changes it from the input side, where a function-local static is
            // simply unreachable.
            int& tab = settingsTab;
            ui::tabBar("settings-tabs",
                {std::string("演奏"), std::string("画面"), std::string("判定"), std::string("系统"),
                    std::string("账户")},
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
            // Switching tabs slides the new page up into place instead of
            // swapping it in a single frame. Purely a vertical offset, so it also
            // works for the rows drawn by hand (which a fade would not cover).
            static int lastTab = tab;
            static float tabIn = 1.0f;
            if (tab != lastTab) {
                lastTab = tab;
                tabIn = 0.0f;
            }
            tabIn = std::min(1.0f, tabIn + ImGui::GetIO().DeltaTime / 0.18f);
            const float tabEase = 1.0f - (1.0f - tabIn) * (1.0f - tabIn) * (1.0f - tabIn);
            const float tabSlide = (1.0f - tabEase) * 14.0f * s;

            const float contentTop = ImGui::GetCursorScreenPos().y;
            const float contentBottom = cardCenter.y + cardSize.y * 0.5f - 74.0f * s;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            // The tab scrolls when it is taller than the card (画面 has plenty of
            // rows). Not ImGuiWindowFlags_NoScrollbar any more, and the bar is
            // styled for the light card instead of ImGui's dark default.
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(226, 226, 236, 150));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(152, 152, 174, 220));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(126, 126, 152, 240));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, IM_COL32(106, 106, 132, 255));
            ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - cardSize.x * 0.5f, contentTop));
            ImGui::BeginChild("##tabcontent",
                ImVec2(cardSize.x, std::max(40.0f * s, contentBottom - contentTop)),
                ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            // The child draws into its own list, so the card has to be told
            // about it or the whole tab body (sliders, checkboxes, native Text)
            // would sit at full size and opacity while the card animates - see
            // ui::cardSubList.
            ui::cardSubList(ImGui::GetWindowDrawList());
            ImGui::PopStyleVar();
            // Window-local coordinates: an absolute screen y here would pin the
            // first row in place while the rest of the tab scrolls under it.
            ImGui::SetCursorPos(ImVec2(padX, tabSlide));

            if (tab == 0) {
                // 演奏: audio offset + note speed + the two volume sliders.
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
                contentLeft();
                ImGui::Text("BGM 音量");
                float bgmPct = userSettings.bgmVolume * 100.0f;
                contentLeft();
                if (ui::slider("bgmvol", &bgmPct, 0.0f, 100.0f, 5.0f, "%.0f%%", interior)) {
                    userSettings.bgmVolume = std::clamp(bgmPct / 100.0f, 0.0f, 1.0f);
                    // Takes effect immediately: the chart track, the select
                    // preview and the result BGM all read this master.
                    audio.setBgmVolume(userSettings.bgmVolume);
                    persistUserData();
                }
                contentLeft();
                ImGui::Text("音效音量");
                float sePct = seVolume * 100.0f;
                contentLeft();
                if (ui::slider("sevol", &sePct, 0.0f, 100.0f, 5.0f, "%.0f%%", interior)) {
                    seVolume = std::clamp(sePct / 100.0f, 0.0f, 1.0f);
                    // Hit SE read `seVolume` directly; the UI clicks are bound
                    // with their own base gain, so re-bind them here.
                    ui::bindSe(&audio, 0.8f * seVolume);
                    persistUserData();
                }
            } else if (tab == 1) {
                // 画面: resolution + window mode + frame rate.
                // UI zoom for the two screens that are laid out on a virtual
                // canvas. The play screen is not affected on purpose.
                contentLeft();
                ImGui::Text("界面缩放（选曲 / 结算）");
                float uiScalePct = userSettings.uiScale * 100.0f;
                contentLeft();
                if (ui::slider("uiscale", &uiScalePct, 70.0f, 150.0f, 5.0f, "%.0f%%", interior)) {
                    const float next = std::clamp(uiScalePct / 100.0f, 0.7f, 1.5f);
                    if (next != userSettings.uiScale) {
                        userSettings.uiScale = next;
                        persistUserData();
                    }
                }
                contentLeft();
                ImGui::Text("分辨率");
                // Presets from 640x360 up. The low end is for small windows /
                // testing; anything else goes through 自定义… below.
                static constexpr int kResCount = 10;
                static constexpr int kResW[kResCount] = {640, 800, 960, 1024, 1120, 1280, 1366, 1600, 1920, 2560};
                static constexpr int kResH[kResCount] = {360, 450, 540, 576, 630, 720, 768, 900, 1080, 1440};
                // Applies the chosen size: the window when it is not fullscreen
                // (there the desktop size wins until exit), the offscreen render
                // target always - in the fixed render mode that is what actually
                // decides how much the game draws.
                auto applySize = [&]() {
                    resW = std::clamp(resW, 320, 7680);
                    resH = std::clamp(resH, 240, 4320);
                    userSettings.windowWidth = resW;
                    userSettings.windowHeight = resH;
                    if (windowMode != 2) {
                        SDL_SetWindowSize(window, resW, resH);
                        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                    }
                    applyRenderMode();
                    persistUserData();
                };
                int resIdx = [](int w, int h) {
                    for (int i = 0; i < kResCount; ++i) {
                        if (kResW[i] == w && kResH[i] == h) {
                            return i;
                        }
                    }
                    return kResCount; // 自定义…
                }(resW, resH);
                contentLeft();
                ImGui::SetNextItemWidth(interior);
                if (ImGui::Combo("##resolution", &resIdx,
                        "640 x 360\0" "800 x 450\0" "960 x 540\0" "1024 x 576\0" "1120 x 630\0"
                        "1280 x 720\0" "1366 x 768\0" "1600 x 900\0" "1920 x 1080\0" "2560 x 1440\0"
                        "自定义…\0")) {
                    if (resIdx < kResCount) {
                        resW = kResW[resIdx];
                        resH = kResH[resIdx];
                        applySize();
                    }
                }
                if (resIdx >= kResCount) {
                    // Committed on Enter / focus loss, not per keystroke: "1280"
                    // typed digit by digit would otherwise resize the window
                    // four times on the way there.
                    static int customW = resW;
                    static int customH = resH;
                    const float half = interior * 0.48f;
                    contentLeft();
                    ImGui::Text("自定义宽 / 高");
                    contentLeft();
                    ImGui::SetNextItemWidth(half);
                    ImGui::InputInt("##resw", &customW, 16, 160);
                    const bool editedW = ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(half);
                    ImGui::InputInt("##resh", &customH, 16, 160);
                    const bool editedH = ImGui::IsItemDeactivatedAfterEdit();
                    if (editedW || editedH) {
                        resW = customW;
                        resH = customH;
                        applySize();
                    }
                }
                contentLeft();
                ImGui::Text("渲染模式");
                contentLeft();
                {
                    // Not `static`: a profile switch replaces the whole settings
                    // struct and a stale index would show the wrong mode.
                    int renderModeSel = userSettings.renderScale;
                    ImGui::SetNextItemWidth(interior);
                    if (ImGui::Combo("##renderscale", &renderModeSel,
                            "窗口多大就渲染多大\0固定分辨率（等比缩放 + 黑边）\0")) {
                        userSettings.renderScale = std::clamp(renderModeSel, 0, 1);
                        applyRenderMode();
                        persistUserData();
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
                        // Leaving fullscreen and re-applying the frame drops
                        // WS_THICKFRAME (see the boot sequence), so put the
                        // resize frame back explicitly.
                        SDL_SetWindowResizable(window, SDL_TRUE);
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
                ImGui::Text("超过 %d Hz 自动关垂直同步", displayRefreshHz);
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
                ui::checkBox("经典开屏画面", &classicSplashBox, interior);
                if (classicSplashBox != (userSettings.splashStyle != 0)) {
                    userSettings.splashStyle = classicSplashBox ? 1 : 0;
                    persistUserData();
                }
                contentLeft();
                // Song-select background: the built-in gradient, the user's
                // desktop wallpaper (blurred + dimmed), or nothing at all so the
                // window itself is see-through (Aero glass on Win7).
                ImGui::Text("选曲背景");
                contentLeft();
                static int bgMode = userSettings.bgStyle;
                ImGui::SetNextItemWidth(interior);
                if (ImGui::Combo("##bgstyle", &bgMode, "默认渐变\0桌面壁纸\0透明（Aero 玻璃）\0")) {
                    userSettings.bgStyle = bgMode;
                    refreshSelectBackdrop();
                    // The play screen shares the setting, and switching it has
                    // to touch the window too: the frame is what makes the
                    // client area see-through in the first place.
                    renderer.setTransparentBackground(userSettings.bgStyle == 2);
#ifdef _WIN32
                    requestGlassWindowMode(splashStyle == 0 || userSettings.bgStyle == 2);
#endif
                    persistUserData();
                }
                if (userSettings.bgStyle == 2) {
                    // How far to go with the window chrome; see AGENTS.md「窗口外观」.
                    contentLeft();
                    ImGui::Text("玻璃实现");
                    contentLeft();
                    // 0 and 2 only: the middle idea (ask DWM to skip the non-client
                    // area) falls back to the Basic frame on Windows 7, which is
                    // worse than what it removes. See AGENTS.md.
                    static int glassModeUi = userSettings.glassMode == 2 ? 1 : 0;
                    ImGui::SetNextItemWidth(interior);
                    if (ImGui::Combo("##glassmode", &glassModeUi,
                            "extend frame（默认）\0自绘无框（窗口无边框）\0")) {
                        const int chosen = glassModeUi == 1 ? 2 : 0;
                        userSettings.glassMode = chosen;
                        glassMode = chosen; // the helper reads this one
#ifdef _WIN32
                        requestGlassWindowMode(true); // applied between frames, not here
#endif
                        persistUserData();
                    }
                    contentLeft();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::TextWrapped("背景不填充，窗口透到桌面（Win7 Aero 下是毛玻璃）。"
                                       "全屏时会退化成普通深色背景——Windows 的全屏优化会绕过 DWM。"
                                       "「自绘无框」把非客户区算成零，移动与缩放由我们自己接管"
                                       "（窗口顶部 23px 是拖动区）。");
                    ImGui::PopStyleColor();
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
                ui::checkBox("自动演出", &autoPlayBox, interior);
                if (autoPlayBox != autoPlay) {
                    autoPlay = autoPlayBox;
                    userSettings.autoplay = autoPlayBox;
                    persistUserData();
                }
            } else if (tab == 2) {
                // 判定: judgement windows.
                contentLeft();
                ImGui::Text("判定窗口 (ms)");
                // The working copy is rebuilt from the engine whenever the
                // dialog opens, so the sliders always show what is in force
                // (and a linked BAD/MISS always moves as one).
                static float perfect = 40.0f;
                static float great = 90.0f;
                static float good = 140.0f;
                static float bad = 200.0f;
                static float miss = 200.0f;
                static float holdTail = 180.0f;
                static float holdStart = 140.0f;
                static float holdPreset = 0.0f;
                static bool linkBadMiss = true;
                if (settingsJustOpened) {
                    const game::JudgementWindows& cur = judgement.windows();
                    perfect = cur.perfectMs;
                    great = cur.greatMs;
                    good = cur.goodMs;
                    bad = cur.badMs;
                    miss = cur.missAfterMs;
                    holdTail = cur.holdTailGraceMs;
                    holdStart = cur.holdStartGraceMs;
                    linkBadMiss = userSettings.linkBadMiss;
                    holdPreset = -1.0f;
                    for (int i = 0; i < 3; ++i) {
                        if (std::fabs(holdTail - kHoldGracePresets[i][0]) < 1.0f
                            && std::fabs(holdStart - kHoldGracePresets[i][1]) < 1.0f) {
                            holdPreset = static_cast<float>(i);
                        }
                    }
                }
                bool windowsChanged = false;
                contentLeft();
                windowsChanged |= ui::slider("perfect", &perfect, 10.0f, 100.0f, 1.0f, "Perfect %.0f", interior);
                contentLeft();
                windowsChanged |= ui::slider("great", &great, 20.0f, 200.0f, 1.0f, "Great %.0f", interior);
                contentLeft();
                windowsChanged |= ui::slider("goodw", &good, 30.0f, 260.0f, 1.0f, "Good %.0f", interior);
                good = std::max(good, great + 10.0f);
                contentLeft();
                windowsChanged |= ui::slider("badw", &bad, 40.0f, 400.0f, 5.0f, "Bad %.0f", interior);
                bad = std::max(bad, good + 10.0f);
                contentLeft();
                bool linkBox = linkBadMiss;
                ui::checkBox("Bad 与 Miss 同步", &linkBox, interior);
                if (linkBox != linkBadMiss) {
                    linkBadMiss = linkBox;
                    userSettings.linkBadMiss = linkBox;
                    if (linkBox) {
                        miss = bad; // linked: the two are one number
                    }
                    persistUserData();
                }
                if (linkBadMiss) {
                    miss = bad;
                }
                contentLeft();
                windowsChanged |= ui::slider("missw", &miss, 40.0f, 500.0f, 5.0f, "Miss %.0f", interior,
                    /*enabled=*/!linkBadMiss);
                miss = std::max(miss, good + 10.0f);

                contentLeft();
                ImGui::Text("长条容错 (ms)");
                // Pick-one stepper (see ui::stepper): -1 = the current values
                // match no preset, i.e. 松手/起按 were dragged by hand. The pill
                // shows the preset name and each capsule jumps to its own.
                contentLeft();
                if (ui::stepper("holdpreset", &holdPreset, kPresetChoiceDeltas, "%.0f", interior,
                        kHoldPresetLabels)) {
                    const int target = std::clamp(static_cast<int>(std::lround(holdPreset)), 0, 2);
                    holdPreset = static_cast<float>(target);
                    holdTail = kHoldGracePresets[target][0];
                    holdStart = kHoldGracePresets[target][1];
                    windowsChanged = true;
                }
                contentLeft();
                bool tailChanged = ui::slider("holdtail", &holdTail, 20.0f, 300.0f, 10.0f, "松手容错 %.0f", interior);
                contentLeft();
                bool startChanged = ui::slider("holdstart", &holdStart, 20.0f, 300.0f, 10.0f, "起按容错 %.0f", interior);
                if (tailChanged || startChanged) {
                    windowsChanged = true;
                    holdPreset = -1.0f; // dragged off the presets
                }
                static bool strictFlick = judgement.strictFlick();
                ui::checkBox("严格 Flick 方向", &strictFlick, interior);
                judgement.setStrictFlick(strictFlick);
                static bool flickAsTap = judgement.flickAsTap();
                contentLeft();
                ui::checkBox("Flick 视作 Tap", &flickAsTap, interior);
                judgement.setFlickAsTap(flickAsTap);
                // Starting life. The engine seeds every session's stats from its own
                // copy on reset(), so pushing it here (and once at boot) is enough.
                float life = judgement.initialLife();
                contentLeft();
                ImGui::Text("初始血量");
                contentLeft();
                if (ui::slider("initlife", &life, 100.0f, game::kMaxInitialLife, 50.0f, "%.0f", interior)) {
                    const float next = std::clamp(life, 100.0f, game::kMaxInitialLife);
                    if (next != userSettings.initialLife) {
                        userSettings.initialLife = next;
                        judgement.setInitialLife(next);
                        persistUserData();
                    }
                }
                // Keep the ladder monotonic: a GREAT window narrower than
                // PERFECT (or a GOOD one narrower than GREAT) would silently
                // delete a judgement tier. Built upwards from PERFECT so the
                // result can never satisfy lo > hi (which std::clamp hates).
                perfect = std::clamp(perfect, 10.0f, 100.0f);
                great = std::max(great, perfect + 10.0f);
                good = std::max(good, great + 10.0f);
                bad = std::max(bad, good + 10.0f);
                if (linkBadMiss) {
                    miss = bad;
                } else {
                    miss = std::max(miss, good + 10.0f);
                }
                if (windowsChanged) {
                    game::JudgementWindows windows;
                    windows.perfectMs = perfect;
                    windows.greatMs = great;
                    windows.goodMs = good;
                    windows.badMs = bad;
                    windows.missAfterMs = miss;
                    windows.holdTailGraceMs = holdTail;
                    windows.holdStartGraceMs = holdStart;
                    judgement.setWindows(windows);
                    // Mirror into the profile immediately so closing the game
                    // never loses a window tweak.
                    userSettings.perfectMs = perfect;
                    userSettings.greatMs = great;
                    userSettings.goodMs = good;
                    userSettings.badMs = bad;
                    userSettings.missMs = miss;
                    userSettings.holdTailGraceMs = holdTail;
                    userSettings.holdStartGraceMs = holdStart;
                    persistUserData();
                }
            } else if (tab == 3) {
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
                contentLeft();
                ImGui::Text("多开 / 单实例");
                contentLeft();
                {
                    int mode = userSettings.instanceMode;
                    ImGui::SetNextItemWidth(interior);
                    if (ImGui::Combo("##instancemode", &mode,
                            "只允许一个实例（再启动就切回已有窗口）\0"
                            "允许多开，新实例登录另一个用户\0")) {
                        const int picked = std::clamp(mode, 0, 1);
                        // 多开是实验性功能：第一次开启要先确认（已经确认过就直接写）。
                        if (picked == 1 && userSettings.instanceMode == 0
                            && !userSettings.multiInstanceAccepted) {
                            multiInstanceAsk = true;
                            multiInstanceAskFromParty = false;
                        } else {
                            userSettings.instanceMode = picked;
                            // 多人游玩 only means something with several windows, so
                            // switching to single-instance turns it off instead of
                            // leaving a switch that can never do anything.
                            if (userSettings.instanceMode == 0 && userSettings.multiplayer) {
                                userSettings.multiplayer = false;
                                std::printf("[settings] multiplayer off (single instance)\n");
                                std::fflush(stdout);
                            }
                            persistUserData();
                        }
                    }
                }
                contentLeft();
                {
                    // Needs 允许多开: greyed out (and inert) when the instance
                    // policy forbids a second window.
                    const bool multiOpen = userSettings.instanceMode == 1;
                    bool partyBox = multiOpen ? userSettings.multiplayer : false;
                    ui::checkBox("多人游玩（同机多窗口一起打）", &partyBox, interior, multiOpen);
                    // Only a click can change it: with the policy on single
                    // instance the greyed box shows "off" and storing that would
                    // silently wipe the setting just for opening this page.
                    if (multiOpen && partyBox != userSettings.multiplayer) {
                        // 勾上多人游玩同样要先确认多开是实验性功能（已经确认过就直接走）。
                        if (partyBox && !userSettings.multiInstanceAccepted) {
                            multiInstanceAsk = true;
                            multiInstanceAskFromParty = true;
                        } else {
                            userSettings.multiplayer = partyBox;
                            // 多人游玩 needs several windows, so it also switches
                            // the instance policy to multi-open; each copy logs in
                            // as its own user (they keep separate records).
                            if (partyBox) {
                                userSettings.instanceMode = 1;
                            }
                            std::printf("[settings] multiplayer %s\n", partyBox ? "on" : "off");
                            std::fflush(stdout);
                            persistUserData();
                        }
                    }
                }
                contentLeft();
                if (userSettings.instanceMode == 0) {
                    ImGui::TextWrapped("多人游玩需要「允许多开」（多个窗口各登录一个用户）。");
                } else if (userSettings.multiplayer) {
                    ImGui::TextWrapped("多人游玩已开启：重开所有窗口生效。先开的窗口是房主"
                                       "（选曲 + 播放 BGM），其它窗口选完难度准备后一起开始。");
                } else {
                    ImGui::TextWrapped("多人游玩：先开的窗口是房主（负责选曲与播放 BGM），"
                                       "其它窗口选完难度准备后一起开始。");
                }
            } else {
                // 账户: the local profile. Nothing here leaves the machine, and
                // none of it is drawn during play - see game/AccountData.
                // ImGui::InputText wants a mutable char buffer, so the three
                // fields are mirrored once (the account is already loaded from
                // disk by the time this first runs) and written back on change.
                static char nameBuf[64] = {};
                static char orgBuf[96] = {};
                static char noteBuf[160] = {};
                static bool bufReady = false;
                // Which profile the buffers were filled from. Switching user
                // has to re-read them, otherwise the next keystroke would write
                // the previous player's name into the new profile.
                static std::string bufProfile;
                if (!bufReady || bufProfile != activeProfileId) {
                    bufReady = true;
                    bufProfile = activeProfileId;
                    std::snprintf(nameBuf, sizeof(nameBuf), "%s", account.name.c_str());
                    std::snprintf(orgBuf, sizeof(orgBuf), "%s", account.org.c_str());
                    std::snprintf(noteBuf, sizeof(noteBuf), "%s", account.note.c_str());
                }

                // --- 多用户 ---
                contentLeft();
                ImGui::Text("用户");
                contentLeft();
                {
                    int current = 0;
                    for (std::size_t i = 0; i < profiles.size(); ++i) {
                        if (profiles[i].id == activeProfileId) {
                            current = static_cast<int>(i);
                        }
                    }
                    std::vector<const char*> labels;
                    labels.reserve(profiles.size());
                    for (const game::UserProfile& user : profiles) {
                        labels.push_back(user.name.c_str());
                    }
                    int chosen = current;
                    ImGui::SetNextItemWidth(interior);
                    if (!labels.empty()
                        && ImGui::Combo("##profile", &chosen, labels.data(), static_cast<int>(labels.size()))) {
                        if (chosen != current) {
                            activateProfile(profiles[static_cast<std::size_t>(chosen)].id);
                        }
                    }
                }
                contentLeft();
                {
                    static char newName[48] = "新用户";
                    const float buttonW = interior * 0.3f;
                    const float fieldW = interior - buttonW - 8.0f * s;
                    ImGui::SetNextItemWidth(fieldW);
                    ImGui::InputText("##newprofile", newName, sizeof(newName));
                    ImGui::SameLine();
                    if (ui::capsuleButton("新建", ImVec2(buttonW, 40.0f * s), true)) {
                        std::string label(newName);
                        if (label.empty()) {
                            label = "新用户";
                        }
                        // A fresh profile starts from the defaults (so it does
                        // not inherit this user's note speed / offset), which
                        // is also what "save the current one first" protects.
                        game::saveUserData(userDataFile, userSettings, scores, account);
                        game::UserProfile user;
                        user.id = game::makeProfileId(label, profiles);
                        user.name = label;
                        profiles.push_back(user);
                        activeProfileId.clear(); // so activateProfile() does not bail out
                        activateProfile(user.id);
                        // New account: clear the mirrored text buffers by
                        // renaming through the profile id change.
                        std::printf("[profile] created '%s' as %s\n", label.c_str(), user.id.c_str());
                    }
                }
                contentLeft();
                {
                    // Two-step instead of a modal: the first press only arms it,
                    // and the same button becomes the confirmation for ~3s.
                    static int confirmFrames = 0;
                    const float buttonW = interior * 0.36f;
                    if (profiles.size() < 2) {
                        ImGui::BeginDisabled();
                    }
                    if (confirmFrames > 0) {
                        if (ui::capsuleButton("确认删除？", ImVec2(buttonW, 40.0f * s), true)) {
                            confirmFrames = 0;
                            const std::string victim = activeProfileId;
                            std::string victimName;
                            std::size_t target = profiles.size();
                            for (std::size_t i = 0; i < profiles.size(); ++i) {
                                if (profiles[i].id == victim) {
                                    victimName = profiles[i].name;
                                } else if (target == profiles.size()) {
                                    target = i;
                                }
                            }
                            if (target < profiles.size()) {
                                // Take the id first: erasing shifts the indexes.
                                const std::string targetId = profiles[target].id;
                                profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
                                                   [&](const game::UserProfile& user) { return user.id == victim; }),
                                    profiles.end());
                                activeProfileId.clear(); // so activateProfile() does not bail out
                                activateProfile(targetId);
                                // The file is left on disk on purpose: removing a
                                // user from the list must never destroy their
                                // scores. It sits in profiles/ as <id>.json.
                                std::printf("[profile] removed '%s' (%s) from the list; file kept\n",
                                    victimName.c_str(), victim.c_str());
                            }
                        }
                        --confirmFrames;
                    } else if (ui::capsuleButton("删除此用户", ImVec2(buttonW, 40.0f * s), false)) {
                        confirmFrames = 180;
                    }
                    if (profiles.size() < 2) {
                        ImGui::EndDisabled();
                    }
                }

                contentLeft();
                ImGui::Text("昵称");
                contentLeft();
                ImGui::SetNextItemWidth(interior);
                if (ImGui::InputText("##pname", nameBuf, sizeof(nameBuf))) {
                    account.name = nameBuf;
                    // The 用户 combobox above lists the *profile files*
                    // (profiles/index.json), which had their own label - so
                    // renaming yourself left it showing "默认用户" and the two
                    // looked like unrelated things. Keep them the same label;
                    // an empty nickname keeps whatever was there before.
                    for (game::UserProfile& user : profiles) {
                        if (user.id != activeProfileId || account.name.empty()
                            || user.name == account.name) {
                            continue;
                        }
                        user.name = account.name;
                        game::saveProfiles(userDataDir, profiles, activeProfileId);
                        break;
                    }
                    if (party.active()) {
                        // The room shows this name too (the window title only
                        // changes on the next launch).
                        party.setName(account.name.empty() ? partyLabel : account.name);
                    }
                    persistUserData();
                }
                contentLeft();
                ImGui::Text("学校 / 组织");
                contentLeft();
                ImGui::SetNextItemWidth(interior);
                if (ImGui::InputText("##porg", orgBuf, sizeof(orgBuf))) {
                    account.org = orgBuf;
                    persistUserData();
                }
                contentLeft();
                ImGui::Text("个性签名");
                contentLeft();
                ImGui::SetNextItemWidth(interior);
                if (ImGui::InputText("##pnote", noteBuf, sizeof(noteBuf))) {
                    account.note = noteBuf;
                    persistUserData();
                }
                contentLeft();
                ImGui::Text("只存在本机 userdata.json，");
                contentLeft();
                ImGui::Text("点选曲右上角的等级牌可查看。");

                const double need = game::expToNextRank(account.rank);
                char lvLine[64];
                std::snprintf(lvLine, sizeof(lvLine), "%d", account.rank);
                char expLine[64];
                std::snprintf(expLine, sizeof(expLine), "%d / %d",
                    static_cast<int>(account.exp), static_cast<int>(need));
                contentLeft();
                std::vector<std::pair<std::string, std::string>> accRows = {
                    {"等级", lvLine},
                    {"本级经验", expLine},
                };
                ui::infoRows(accRows, interior);
            }
            // End on an item: the checkbox helper leaves the cursor at the row
            // bottom with a bare SetCursorScreenPos, which trips ImGui's
            // "don't extend the parent with SetCursorPos" check when the child
            // is the last window in the frame. The extra height also keeps the
            // last row off the card edge and inside the scroll range.
            ImGui::Dummy(ImVec2(1.0f, 14.0f * s));
            // Diagnostic (CPSEKAI_UI_TRACE=1): how tall this tab really is against
            // the view. `used` past `view` means rows are being drawn below the
            // child and clipped - which is what "the settings do not fit" looks
            // like, and what a scrollbar has to cover.
            if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
                std::printf("[ui] tab %d view=%.0f used=%.0f scrollMax=%.0f\n", tab,
                    ImGui::GetWindowHeight(), ImGui::GetCursorPosY(), ImGui::GetScrollMaxY());
                std::fflush(stdout);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(4);
            ImGui::PopFont();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(5);
            ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - cardSize.x * 0.5f + padX,
                cardCenter.y + cardSize.y * 0.5f - 68.0f * s));
            if (ui::capsuleButton("关闭", ImVec2(132.0f * s, 46.0f * s), false)) {
                showDebug = false;
            }
            ui::endCard();
        } else {
            settingsAlive = false;
        }
    };

    // ------------------------------------------------------------------
    // 开启多开（实验性）的确认框。
    //
    // 多开 / 多人游玩 都是实验性功能：同一个游戏开好几份窗口靠共享内存总线
    // 同步，机器一忙就掉帧、时钟漂了也没人纠。所以第一次开启之前先说清楚，
    // 用户确认过之后（multiInstanceAccepted）就不再拦。
    // 「取消」什么也不改 —— 用户点错的 combo 会自己跳回去，因为 combo 的值
    // 每帧都从 userSettings.instanceMode 重新读。
    // ------------------------------------------------------------------
    auto drawMultiInstanceAskDialog = [&]() {
        if (!multiInstanceAsk) {
            return;
        }
        // eulaDialog is the only card that takes body paragraphs (messageDialog
        // is title + buttons only), so the notice reuses it with no checkbox.
        const int action = ui::eulaDialog(renderer, "##multiask", "开启多开？",
            {
                "多开 / 多人游玩是实验性功能：同一台机器开几个窗口，靠共享内存总线同步"
                "选曲、难度和起奏时刻。",
                "它没有网络校验，机器一忙可能掉帧或时钟漂移；窗口越多越明显。",
                "每个窗口各登录一个用户，各自记成绩。确定开启吗？",
            },
            nullptr, nullptr, {std::string("取消"), std::string("确定开启")}, {false, true});
        if (action == 0) {
            // 取消：什么也不存。combo / 复选框下一帧自己从 userSettings 归位。
            std::printf("[instance] multi-open declined\n");
            std::fflush(stdout);
            multiInstanceAsk = false;
            multiInstanceAskFromParty = false;
        } else if (action == 1) {
            userSettings.multiInstanceAccepted = true;
            userSettings.instanceMode = 1;
            if (multiInstanceAskFromParty) {
                userSettings.multiplayer = true;
            }
            std::printf("[instance] multi-open accepted (fromParty=%d)\n",
                multiInstanceAskFromParty ? 1 : 0);
            std::fflush(stdout);
            persistUserData();
            multiInstanceAsk = false;
            multiInstanceAskFromParty = false;
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
        const double pressSongTime = songClock();
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
        // The pause dialog opens on the same frame, so its window_open sound
        // outranks this click (that file already carries a click).
        ui::se(ui::SeClick);
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

    // Debug log: one line per flick judgement attempt, and when it cleared
    // nothing, the flick notes the swipe was aimed at (nearest first) with the
    // lane / time distance and the direction the note wanted. Inert unless the
    // debug log is on.
    auto logFlickOutcome = [&](const TouchTrack& track, double songTime, game::FlickDir dir,
                               game::Judge result, const char* where) {
        if (!gFlickLog.enabled) {
            return;
        }
        gFlickLog.write(
            "[flick] %s finger=%u touch=%d dir=%s judge=%s lane=%.3f restLane=%.3f songTime=%.3f "
            "peak=(up %.0f side %.0f) travel=(up %.1f side %.1f)",
            where, static_cast<unsigned>(track.fingerId), track.isTouch ? 1 : 0, flickDirName(dir),
            judgeName(result), static_cast<double>(track.lanePos), static_cast<double>(track.restLanePos),
            songTime, static_cast<double>(track.peakUp), static_cast<double>(track.peakSide),
            static_cast<double>(track.travelUp), static_cast<double>(track.travelSide));
        if (result == game::Judge::None) {
            // (`candidates`, not `near`: windef.h defines `near` as an empty
            // macro, so that name silently swallows the declaration.)
            std::vector<game::JudgementEngine::FlickDebugNote> candidates;
            judgement.debugFlickNotesNear(static_cast<float>(songTime), 0.6f, candidates);
            if (candidates.empty()) {
                gFlickLog.write("[flick]   -> no flick note within 0.6s, the swipe had nothing to clear");
            }
            for (const auto& note : candidates) {
                gFlickLog.write(
                    "[flick]   -> candidate t=%.3f dt=%+.3f center=%.3f width=%.2f wantDir=%s state=%u "
                    "laneDelta=%.3f (+halfWidth %.2f)",
                    static_cast<double>(note.timeSec), static_cast<double>(note.timeSec - songTime),
                    static_cast<double>(note.center), static_cast<double>(note.width),
                    flickDirName(static_cast<game::FlickDir>(note.dir)), static_cast<unsigned>(note.state),
                    static_cast<double>(track.lanePos - note.center), static_cast<double>(note.width * 0.5f));
            }
        }
        gFlickLog.flush();
    };

    // Starts a tap at a window position. Returns false when the press is
    // outside the playfield (e.g. on the sky above the horizon), where the
    // inverse perspective would map it to a bogus lane.
    auto beginPointer = [&](SDL_FingerID id, int x, int y, bool isTouch, Uint32 eventMs) {
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
        track.lastMoveTimeMs = eventMs;
        touches.push_back(track);
        lanePress[static_cast<size_t>(track.laneIndex)] = 1.0f;
        const double songTime = songClock();
        const game::Judge result = judgement.tap(track.lanePos, static_cast<float>(songTime), false, 0.8f);
        if (gFlickLog.enabled) {
            gFlickLog.write("[touch] down finger=%u touch=%d screen=(%d,%d) lane=%.3f laneIndex=%d "
                            "eventMs=%u songTime=%.3f tap=%s",
                static_cast<unsigned>(id), isTouch ? 1 : 0, x, y, static_cast<double>(lanePos),
                track.laneIndex, static_cast<unsigned>(eventMs), songTime, judgeName(result));
            gFlickLog.flush();
        }
        if (result != game::Judge::None) {
            playHitSe(audio, judgement, seVolume);
        }
        return true;
    };

    // Tracks pointer movement; a fast swipe is a flick. The gesture is judged
    // in screen pixels per second (see flickDirFrom) so the up / left / right
    // decision does not depend on the perspective scaling of the playfield.
    //
    // `eventMs` is the *event's own* timestamp (event.tfinger.timestamp /
    // event.motion.timestamp), never SDL_GetTicks(): SDL delivers a whole
    // frame's events in one batch, so stamping them at processing time gave
    // every sample of a batch the same millisecond. The old code then dropped
    // those samples (dt == 0 skips the update) while still advancing the
    // position, so the displacement was lost and both speed and travel came out
    // far too small. A mouse never hit this because Windows coalesces
    // WM_MOUSEMOVE (at most one per pump) while WM_TOUCH is not coalesced.
    // Samples that do share a millisecond are now accumulated into one
    // measurement instead of being thrown away.
    auto movePointer = [&](SDL_FingerID id, int x, int y, Uint32 eventMs) {
        const float clipX = (static_cast<float>(x) / static_cast<float>(windowW)) * 2.0f - 1.0f;
        const float clipY = 1.0f - (static_cast<float>(y) / static_cast<float>(windowH)) * 2.0f;
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
            // The position advances with every event - the displacement is
            // banked, not swallowed.
            track.lastScreenX = static_cast<float>(x);
            track.lastScreenY = static_cast<float>(y);
            track.pendingDx += dx;
            track.pendingDy += dy;
            const Uint32 rawMs = eventMs - track.lastMoveTimeMs; // wraps cleanly
            if (rawMs == 0) {
                continue; // same millisecond: the rest of the batch is still coming
            }
            const double rawDt = static_cast<double>(rawMs) / 1000.0;
            // See kFlickMaxSampleSec: a finger parked on a hold emits no motion
            // events, and the gap before the first sample of the flick must not
            // be used as the sample period - that alone made hold-tail flicks
            // measure ~100 px/s instead of ~800 and never register.
            const double dt = std::min(rawDt, kFlickMaxSampleSec);
            const float sampleDx = track.pendingDx;
            const float sampleDy = track.pendingDy;
            track.pendingDx = 0.0f;
            track.pendingDy = 0.0f;
            track.lastMoveTimeMs = eventMs;
            const double now = static_cast<double>(eventMs) / 1000.0;
            if (rawDt > kFlickIdleGapSec) {
                track.velUp = 0.0f;
                track.velSide = 0.0f;
            }
            if (dt > 0.001) {
                // Low-pass the per-sample velocity: touch panels report
                // unevenly spaced position jumps and a single-frame delta
                // often under- or over-shoots a real flick.
                const float upSpeed = -sampleDy / static_cast<float>(dt);
                const float sideSpeed = sampleDx / static_cast<float>(dt);
                track.velUp = track.velUp * 0.35f + upSpeed * 0.65f;
                track.velSide = track.velSide * 0.35f + sideSpeed * 0.65f;
                // Distance travelled in the current direction: the counter
                // starts over whenever the movement reverses, so the jitter of
                // a resting finger never adds up while a deliberate swipe does.
                const float upDelta = -sampleDy;
                track.travelUp = (upDelta >= 0.0f) == (track.travelUp >= 0.0f)
                    ? track.travelUp + upDelta
                    : upDelta;
                track.travelSide = (sampleDx >= 0.0f) == (track.travelSide >= 0.0f)
                    ? track.travelSide + sampleDx
                    : sampleDx;
                if (std::abs(track.velUp) > std::abs(track.peakUp)) {
                    track.peakUp = track.velUp;
                }
                if (std::abs(track.velSide) > std::abs(track.peakSide)) {
                    track.peakSide = track.velSide;
                }
            }
            const game::FlickDir dir = flickDirFrom(track.velUp, track.velSide, track.travelUp,
                track.travelSide, track.isTouch, heightScale);
            if (gFlickLog.enabled) {
                gFlickLog.write(
                    "[touch] move finger=%u screen=(%d,%d) d=(%.1f,%.1f) dt=%.1fms rawDt=%.1fms "
                    "vel=(up %.0f side %.0f) travel=(up %.1f side %.1f) dir=%s lane=%.3f",
                    static_cast<unsigned>(id), x, y, static_cast<double>(sampleDx), static_cast<double>(sampleDy),
                    dt * 1000.0, rawDt * 1000.0, static_cast<double>(track.velUp),
                    static_cast<double>(track.velSide), static_cast<double>(track.travelUp),
                    static_cast<double>(track.travelSide), flickDirName(dir), static_cast<double>(lanePos));
            }
            if (dir != game::FlickNone && now - track.lastFlickFireTimeSec >= kFlickRefireSec) {
                const double songTime = songClock();
                const game::Judge result = flickJudge(track, songTime, dir);
                logFlickOutcome(track, songTime, dir, result, "fire/move");
                if (result != game::Judge::None) {
                    // Consume the gesture: resetting the travelled distance stops
                    // one continuous swipe from firing on every sample, while the
                    // finger stays armed so a later, separate swipe fires again.
                    track.lastFlickFireTimeSec = now;
                    track.travelUp = 0.0f;
                    track.travelSide = 0.0f;
                    playHitSe(audio, judgement, seVolume);
                }
                // A miss keeps the distance: the direction a swipe resolves to
                // can change as it continues (a diagonal that straightens out),
                // and the old code burnt the gesture on the first wrong guess -
                // the player had to lift the finger and swipe again.
            } else if (dir != game::FlickNone && gFlickLog.enabled) {
                // Debug only: the swipe classified fine but the re-fire latch
                // is still hot from a previous fire.
                gFlickLog.write("[touch]   (dir=%s held back: %.0f ms since the last fire)",
                    flickDirName(dir), (now - track.lastFlickFireTimeSec) * 1000.0);
            } else if (std::abs(track.velUp) < kFlickRestSpeed * heightScale
                && std::abs(track.velSide) < kFlickRestSpeed * heightScale) {
                // Slow enough to count as parked: remember this lane. It is the
                // one a flick started from - a finger following a sliding hold
                // keeps it up to date, a swipe that is already under way does
                // not (its velocity is high), so the value cannot drift along
                // with the up-stroke's perspective skew.
                track.restLanePos = lanePos;
            }
            track.lastLanePos = lanePos;
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
            judgement.chartRating(), account, 0, 0);
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

    // -----------------------------------------------------------------------
    // Game controller (Xbox pad). Menus and dialogs only - the play screen
    // deliberately ignores it (a pad cannot play a 12-lane chart). Every press
    // is turned into the key press the keyboard would send, so the existing
    // arrow / Enter / Escape handling drives the song select, the settings card
    // and the pause dialog without a second input path per screen.
    // One press = one short pulse: the key goes down now and is released on the
    // next frame, which is what ImGui reads as a clean press.
    // -----------------------------------------------------------------------
    SDL_GameController* pad = nullptr;
    const Uint32 padWindowId = SDL_GetWindowID(window);
    constexpr Sint16 kPadDeadZone = 12000; // stick dead zone (~37% of 32767)
    std::vector<SDL_Scancode> padReleaseQueue; // pulses to release next frame
    bool padDirHeld[4] = {false, false, false, false};
    Uint32 padDirNext[4] = {0, 0, 0, 0};
    bool padPrevA = false;
    bool padPrevB = false;
    bool padPrevX = false;
    bool padPrevY = false;
    bool padPrevL1 = false;
    bool padPrevR1 = false;
    bool padPrevStart = false;
    bool padPrevBack = false;
    // --fake-pad <A|B|X|Y|LB|RB|START|BACK>[,...]: makes those pad buttons read
    // as pressed, cycling down/up every ~half second, so the controller
    // bindings can be checked headlessly - there is no way to press a real pad
    // in a --screenshot run. Several names at once is what makes a two-step
    // flow checkable (START opens the pause dialog, X picks 重试 in it). It also
    // keeps the pad block alive when SDL found no device, which is exactly the
    // headless case.
    std::vector<std::string> fakePad;
    if (!fakePadButton.empty()) {
        std::string current;
        for (const char ch : fakePadButton + ",") {
            if (ch == ',' || ch == ' ') {
                if (!current.empty()) {
                    fakePad.push_back(current);
                    current.clear();
                }
                continue;
            }
            current.push_back(static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch));
        }
    }
    bool fakePadHeld = !fakePad.empty();
    int fakePadFrames = 0;
    // How long --fake-pad keeps pressing (in 30-frame cycles). Long enough for
    // the boot + the first dialog, short enough that the run still settles.
    constexpr int kFakePadCycles = 6;
    auto padDown = [&](SDL_GameControllerButton button) -> bool {
        if (!fakePad.empty()) {
            const char* name = SDL_GameControllerGetStringForButton(button);
            if (name != nullptr) {
                std::string upper;
                for (const char* p = name; *p != '\0'; ++p) {
                    upper.push_back(static_cast<char>(*p >= 'a' && *p <= 'z' ? *p - 32 : *p));
                }
                for (std::size_t i = 0; i < fakePad.size(); ++i) {
                    const std::string& wanted = fakePad[i];
                    // SDL calls the shoulders "leftshoulder"/"rightshoulder" and
                    // the d-pad "dpup"/..., so the short names people actually
                    // type are mapped onto them here.
                    const bool match = upper == wanted
                        || ((wanted == "LB" || wanted == "L1") && upper == "LEFTSHOULDER")
                        || ((wanted == "RB" || wanted == "R1") && upper == "RIGHTSHOULDER")
                        || (wanted == "UP" && upper == "DPUP")
                        || (wanted == "DOWN" && upper == "DPDOWN")
                        || (wanted == "LEFT" && upper == "DPLEFT")
                        || (wanted == "RIGHT" && upper == "DPRIGHT");
                    if (!match) {
                        continue;
                    }
                    // 30 frames down, 30 up; every further entry is offset by
                    // half a cycle. Two buttons in one list must never fire on
                    // the same frame, or "START opens the pause dialog, X picks
                    // 重试" cannot be checked - START would win the frame and
                    // just resume.
                    //
                    // Stops after kFakePadCycles cycles: an endless press/release
                    // loop would keep re-opening the dialog (and re-picking the
                    // choice) so the run never settles enough to screenshot.
                    if (fakePadFrames >= kFakePadCycles * 30) {
                        return false;
                    }
                    return ((fakePadFrames + static_cast<int>(i) * 15) / 30) % 2 == 0;
                }
            }
        }
        return pad != nullptr && SDL_GameControllerGetButton(pad, button) != 0;
    };
    auto openPad = [&]() {
        if (pad != nullptr) {
            return;
        }
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i) == SDL_FALSE) {
                continue;
            }
            pad = SDL_GameControllerOpen(i);
            if (pad != nullptr) {
                std::printf("[pad] %s connected (menu navigation)\n", SDL_GameControllerName(pad));
                std::fflush(stdout);
                return;
            }
        }
    };
    openPad();

    // -----------------------------------------------------------------------
    // Opening-card skip, shared by the mouse / touch paths and the pad's A.
    // The button only exists while the card is on screen, so `available` is
    // also the guard for "A does nothing else during the lead-in".
    // -----------------------------------------------------------------------
    auto introSkipAvailable = [&]() {
        const double currentSongTime = songClock();
        return currentSongTime + leadInSec < static_cast<double>(game::kHudIntroDurationSec);
    };
    auto skipIntro = [&]() {
        if (audio.hasMusic()) {
            audio.skipLeadIn();
        } else {
            // No BGM: the clock is the wall clock - move its anchor so chart
            // time 0 is now.
            perfStart = SDL_GetPerformanceCounter()
                - static_cast<Uint64>(leadInSec * static_cast<double>(perfFreq));
        }
        std::printf("[intro] lead-in skipped\n");
        std::fflush(stdout);
    };
    // A choice made with the pad on the pause dialog, handed to
    // ui::messageDialog() as a forced click (see the dialog below).
    int pauseDialogChoice = -1;
    // 多人游玩: stop playing because the *room* moved on (the host gave up, or
    // re-armed the round) - this window goes back to the song select with a
    // reason on its panel, without touching anybody else's seat: the caller has
    // already done that part.
    auto leaveLiveForRoom = [&](const std::string& status) {
        audio.stopMusic();
        audio.stopResultBgm();
        audio.setHoldLoop(false, false, 0.0f);
        touches.clear();
        std::fill(std::begin(keyHeld), std::end(keyHeld), false);
        lanePress.fill(0.0f);
        pauseDialogOpen = false;
        paused = false;
        countdownActive = false;
        session.active = false;
        resultScheduled = false;
        resultData = game::ResultData{};
        songEndBlackout = 0.0f;
        lastSeenJudgeTime = -100.0f;
        hudState = game::HudState{};
        mpFollowing = false;
        mpStartPending = false;
        mpHostPaused = false;
        mpStatus = status;
        party.clearPlayingScore();
        systemMedia.setTaskbarProgress(-1.0, false);
        state = AppState::Select;
        std::printf("[party] left the live: %s\n", status.c_str());
        std::fflush(stdout);
    };

    bool dragFramePacing = false; // true while the window subclass is feeding frames
    // ------------------------------------------------------------------
    // One frame of the main loop, wrapped so it can also be served from inside
    // Windows' modal move/size loop (see the window subclass right below).
    //
    // Why this exists: a title-bar drag or a border resize makes DefWindowProc
    // run its *own* message loop, and our pump (SDL_PollEvent -> DispatchMessage)
    // stays parked inside it for as long as the mouse button is held - the frame
    // loop does not run, the picture freezes, while the audio and the chart clock
    // ride on in miniaudio's own thread. Releasing the mouse then jumped the
    // chart forward over a stretch of notes that had never been on screen.
    //
    // The one-shot `for` around the body is load-bearing: the body uses
    // `continue` and `break`, and the compiler reads those as "next round of this
    // loop" / "leave this loop". With a loop that runs exactly once, `continue`
    // leaves the wrapper - which is what it used to do to the main loop - and so
    // does `break`; the three places that end the loop all set `running = false`
    // first and the condition is checked right after the call. That is why the
    // body itself needed no edits at all.
    // ------------------------------------------------------------------
    auto runFrame = [&]() {
    for (bool once = true; once; once = false) {
        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        lastFrameDeltaSec = static_cast<double>(nowCounter - lastFrameCounter) / static_cast<double>(perfFreq);
        lastFrameCounter = nowCounter;
        const float frameDelta = static_cast<float>(lastFrameDeltaSec);
        uiClock += lastFrameDeltaSec;
#ifdef _WIN32
        if (glassReapplyWanted) {
            glassReapplyWanted = false;
            applyGlassWindowMode(glassReapplyEnable);
        }
        // Feeds the crash log (cppsekaiCrashFilter); a handful of stores a frame.
        gCrashState = static_cast<long>(state);
        gCrashFrameless = noFrameMode;
        ++gCrashFrameCount;
        if (glassToggleSec >= 0.0 && !glassToggleDone && uiClock >= glassToggleSec) {
            glassToggleDone = true;
            glassMode = (glassMode == 2) ? 0 : 2;
            std::printf("[glass] probe: switching to mode %d now\n", glassMode);
            std::fflush(stdout);
            requestGlassWindowMode(true); // same path the settings card uses
        }
#endif
        // ------------------------------------------------------------------
        // Frame-rate sampler. Quiet while nothing interesting happens, and it
        // needs no environment variable - a "the window's picture does not
        // refresh while dragging" report has to be answerable from the log alone,
        // because the one thing the user cannot be asked to remember is a flag.
        //
        // Two situations print a line, both of them worth knowing about:
        //
        //  * the window geometry changed during that second (drag / resize /
        //    maximize). `window moved N px` plus the frame count and the longest
        //    frame tell the two possible causes apart:
        //      - loop parked by Windows' modal loop: ~0-1 frames, longest frame ==
        //        the whole drag (the hook is supposed to feed frames from inside).
        //      - loop running, presentation slow: ~10-30 frames, longest ~80-100 ms.
        //  * a single frame took longer than kHitchMs even though nothing moved
        //    (a hitch: asset load, GC, driver stall).
        //
        // 2026-09-19: on both Win10 22H2 and Win11 a real title-bar drag never
        // delivers WM_ENTERSIZEMOVE / WM_MOVING to our window procedure, which is
        // why the geometry is polled here instead.
        // ------------------------------------------------------------------
        {
            constexpr double kHitchMs = 60.0;
            static int sampleFrames = 0;
            static double sampleLongestMs = 0.0;
            static double sampleMovedPx = 0.0;
            static double sampleNextAtSec = 0.0;
            static int lastWinX = 0;
            static int lastWinY = 0;
            static int lastWinW = 0;
            static int lastWinH = 0;
            static bool haveLastGeometry = false;
            int winX = 0;
            int winY = 0;
            int winW = 0;
            int winH = 0;
            SDL_GetWindowPosition(window, &winX, &winY);
            SDL_GetWindowSize(window, &winW, &winH);
            if (haveLastGeometry) {
                sampleMovedPx += std::abs(winX - lastWinX) + std::abs(winY - lastWinY)
                    + std::abs(winW - lastWinW) + std::abs(winH - lastWinH);
            }
            lastWinX = winX;
            lastWinY = winY;
            lastWinW = winW;
            lastWinH = winH;
            haveLastGeometry = true;
            ++sampleFrames;
            const double sampleFrameMs = lastFrameDeltaSec * 1000.0;
            if (sampleFrameMs > sampleLongestMs) {
                sampleLongestMs = sampleFrameMs;
            }
            if (uiClock >= sampleNextAtSec) {
                sampleNextAtSec = uiClock + 1.0;
                if (sampleMovedPx > 0.5) {
                    std::printf("[frame] geometry changed: %d frame(s)/s, longest frame %.0f ms, "
                                "window moved %.0f px (state=%d)\n",
                        sampleFrames, sampleLongestMs, sampleMovedPx, static_cast<int>(state));
                    std::fflush(stdout);
                } else if (sampleLongestMs > kHitchMs) {
                    std::printf("[frame] %d frame(s)/s, longest frame %.0f ms (state=%d)\n", sampleFrames,
                        sampleLongestMs, static_cast<int>(state));
                    std::fflush(stdout);
                }
                sampleFrames = 0;
                sampleLongestMs = 0.0;
                sampleMovedPx = 0.0;
            }
        }
        // Diagnostic (CPSEKAI_MP_TRACE=1): "the frame loop is still turning"
        // stamp for a window that goes quiet in a room. Two reasons it matters:
        // a [party] line that just stops is otherwise indistinguishable from a
        // crashed process, and gBlock->seats[slot].alive covers every *other*
        // member's view of us - the loop exiting while alive still reads 1 is
        // exactly the "连不上" shape (the room keeps waiting for a window that
        // is not there any more).
        if (platform::traceEnabled() && party.active()) {
            static double alivePrintAtSec = -1.0;
            const double qpcSec = platform::PartyLink::counterToSeconds(
                platform::PartyLink::nowCounter());
            if (alivePrintAtSec < 0.0 || qpcSec - alivePrintAtSec >= 1.0) {
                alivePrintAtSec = qpcSec;
                std::printf("[alive] seat %d %s qpc=%.3f frame %.1f ms uiClock=%.3f loop=run\n", party.slot(),
                    party.isHost() ? "host" : "member", qpcSec, lastFrameDeltaSec * 1000.0, uiClock);
                std::fflush(stdout);
            }
        }

        if (beginSessionClockPending) {
            beginSessionClockPending = false;
            beginSessionClock();
        }

        // 多人游玩: heartbeat first (it is the only thing that tells the other
        // windows this one is still alive), then settle this frame's clock. The
        // host publishes its clock with the QPC it was read on; the members
        // steer onto it (see resolveSongClock).
        party.update();
        if (state == AppState::Play && session.active) {
            // The host's window was closed mid-live: its clock stops being
            // published, and following a frozen sample would freeze this window
            // for the rest of the song. Fall back to our own clock instead (the
            // room's host role moves on by itself, see PartyLink::update).
            if (mpFollowing && !party.hostAlive()) {
                mpFollowing = false;
                mpClockSynced = false;
                std::printf("[party] host gone: running on the local clock\n");
                std::fflush(stdout);
            }
            const bool hostHeld = mpFollowing && party.read().paused;
            if (hostHeld != mpHostPaused) {
                // The host owns the chart clock, so a pause there has to stop
                // this window's picture too - otherwise the notes keep sliding
                // past while the music is silent.
                std::printf("[party] host %s\n", hostHeld ? "paused: picture frozen" : "resumed");
                std::fflush(stdout);
            }
            mpHostPaused = hostHeld;
            resolveSongClock();
            if (party.active() && party.isHost()) {
                party.publishHostClock(frameSongTime);
            }
            // Diagnostic (CPSEKAI_MP_TRACE=1): one line per second carrying the
            // shared QPC next to this window's chart time, so two logs can be
            // diffed for how far the clocks actually drifted apart.
            if (platform::traceEnabled()) {
                static double traceAtSec = -1.0;
                const double qpcSec = platform::PartyLink::counterToSeconds(
                    platform::PartyLink::nowCounter());
                if (traceAtSec < 0.0 || qpcSec - traceAtSec >= 1.0) {
                    traceAtSec = qpcSec;
                    // The other seats' live numbers ride along: the scoreboard
                    // is a shared-memory read, so this proves the whole chain
                    // (write -> other process -> read) and not just the clock.
                    std::string peers;
                    for (const platform::PartyPlayer& player : party.players()) {
                        if (player.slot == party.slot()) {
                            continue;
                        }
                        peers += " " + player.name + "=" + std::to_string(
                            static_cast<long long>(player.score)) + "/" + std::to_string(player.combo);
                    }
                    std::printf("[sync] qpc=%.6f t=%.6f offset=%+.6f %s%s%s\n", qpcSec, frameSongTime,
                        mpClockOffset, mpFollowing ? "follow" : "host", mpHostPaused ? " PAUSED" : "",
                        peers.c_str());
                    std::fflush(stdout);
                }
            }
        }

        auto saveScreenshot = [&]() {
        if (screenshotPath.empty()) {
            return;
        }
        // The real framebuffer, not the logical render size: in the fixed
        // render mode the picture is presented letterboxed and reading only
        // `windowW x windowH` would grab a corner of it.
        const int shotW = winPixelW;
        const int shotH = winPixelH;
        std::vector<unsigned char> pixels(static_cast<size_t>(shotW) * static_cast<size_t>(shotH) * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, shotW, shotH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // Flip vertically (GL origin is bottom-left).
        const int rowBytes = shotW * 4;
        std::vector<unsigned char> flipped(pixels.size());
        for (int y = 0; y < shotH; ++y) {
            std::memcpy(flipped.data() + static_cast<size_t>(y) * rowBytes,
                pixels.data() + static_cast<size_t>(shotH - 1 - y) * rowBytes,
                static_cast<size_t>(rowBytes));
        }
        stbi_write_png(screenshotPath.c_str(), shotW, shotH, 4, flipped.data(), rowBytes);
        std::printf("screenshot saved: %s\n", screenshotPath.c_str());
        std::fflush(stdout);
    };

    // Fixed render mode: the window is bigger than the picture, so a pointer
    // position in window pixels has to be mapped into the render size before
    // anything looks at it. Rewriting the event in place means the game's
    // handlers, the ImGui backend and the touch paths all keep working in one
    // coordinate space (`windowW x windowH`) without a single call-site change.
    auto mapPointerEvent = [&](SDL_Event& e) {
        if (!renderer.offscreen()) {
            return;
        }
        const float scale = renderer.outputScale();
        if (scale <= 0.0f) {
            return;
        }
        int ox = 0;
        int oy = 0;
        int ow = 0;
        int oh = 0;
        renderer.outputRect(ox, oy, ow, oh);
        auto cx = [&](float x) { return (x - static_cast<float>(ox)) / scale; };
        auto cy = [&](float y) { return (y - static_cast<float>(oy)) / scale; };
        switch (e.type) {
            case SDL_MOUSEMOTION:
                e.motion.x = static_cast<int>(cx(static_cast<float>(e.motion.x)));
                e.motion.y = static_cast<int>(cy(static_cast<float>(e.motion.y)));
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                e.button.x = static_cast<int>(cx(static_cast<float>(e.button.x)));
                e.button.y = static_cast<int>(cy(static_cast<float>(e.button.y)));
                break;
            default:
                break;
        }
    };

    // Window pixels -> the game's coordinate space, for the places that poll
    // SDL directly instead of reading an event.
    auto toGamePoint = [&](int& x, int& y) {
        if (!renderer.offscreen()) {
            return;
        }
        const float scale = renderer.outputScale();
        if (scale <= 0.0f) {
            return;
        }
        int ox = 0;
        int oy = 0;
        int ow = 0;
        int oh = 0;
        renderer.outputRect(ox, oy, ow, oh);
        x = static_cast<int>((static_cast<float>(x) - static_cast<float>(ox)) / scale);
        y = static_cast<int>((static_cast<float>(y) - static_cast<float>(oy)) / scale);
    };

    bool escapePressed = false;
    bool rescanRequested = false; // F5 in the song list: re-read charts/
        while (SDL_PollEvent(&event) != 0) {
            mapPointerEvent(event);
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    // Hot-plug: opening the first game controller. The joystick
                    // index SDL hands us here is a *device* index, which is what
                    // SDL_GameControllerOpen wants (the instance id comes with
                    // the removal event below).
                    openPad();
                    break;
                case SDL_CONTROLLERDEVICEREMOVED: {
                    if (pad != nullptr) {
                        SDL_Joystick* js = SDL_GameControllerGetJoystick(pad);
                        if (js != nullptr && SDL_JoystickInstanceID(js) == event.cdevice.which) {
                            std::printf("[pad] %s disconnected\n", SDL_GameControllerName(pad));
                            std::fflush(stdout);
                            SDL_GameControllerClose(pad);
                            pad = nullptr;
                            for (int i = 0; i < 4; ++i) {
                                padDirHeld[i] = false;
                            }
                            padPrevA = padPrevB = padPrevY = padPrevStart = padPrevBack = false;
                            openPad(); // another pad may still be around
                        }
                    }
                    break;
                }
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        // The real window changed; the render size only follows
                        // it when the render mode is "window sized".
                        winPixelW = event.window.data1;
                        winPixelH = event.window.data2;
                        renderer.resize(winPixelW, winPixelH);
                        applyRenderMode();
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
                        // 多人游玩: never auto-pause. Several windows sit on one
                        // screen, so every one of them is unfocused almost all
                        // of the time and the live would never get going. This
                        // covers a window that found a room on the machine even
                        // if it never joined it (`roomOpen`): the room is on,
                        // so focus means nothing here.
                        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && state == AppState::Play
                            && !paused && !pauseDialogOpen && screenshotPath.empty()
                            && userSettings.autoPauseOnBlur && !party.active() && !roomOpen) {
                            const double currentSongTime =
                                songClock();
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
                    // A focused ImGui text field (the song-search box) owns the
                    // keyboard: typing "f" used to toggle fullscreen in the
                    // middle of a search ("h" the debug panel). WantTextInput is
                    // the narrow check - WantCaptureKeyboard is always true
                    // here, because the HUD lives in an invisible fullscreen
                    // window.
                    const bool typingText = ImGui::GetIO().WantTextInput;
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        escapePressed = true;
                    } else if (!typingText && event.key.keysym.sym == SDLK_f) {
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
                            SDL_SetWindowResizable(window, SDL_TRUE);
                        }
                    } else if (!typingText && event.key.keysym.sym == SDLK_h) {
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
                        requestPause();
                    } else if (state == AppState::Play && !autoPlay && !paused) {
                        const double songTime = songClock();
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
                        if (game::resultContinueHitTest(windowW, windowH, fx, fy, userSettings.uiScale)) {
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
                            songClock();
                        if (!ImGui::GetIO().WantCaptureMouse
                            && currentSongTime + leadInSec < static_cast<double>(game::kHudIntroDurationSec)
                            && game::introSkipHitTest(windowW, windowH, fx, fy)) {
                            skipIntro();
                            break;
                        }
                    }
                    if (autoPlay) {
                        break;
                    }
                    beginPointer(event.tfinger.fingerId,
                        static_cast<int>(event.tfinger.x * static_cast<float>(windowW)),
                        static_cast<int>(event.tfinger.y * static_cast<float>(windowH)), true,
                        event.tfinger.timestamp);
                    break;
                }
                case SDL_FINGERMOTION: {
                    if (autoPlay || paused || state != AppState::Play) {
                        break;
                    }
                    movePointer(event.tfinger.fingerId,
                        static_cast<int>(event.tfinger.x * static_cast<float>(windowW)),
                        static_cast<int>(event.tfinger.y * static_cast<float>(windowH)),
                        event.tfinger.timestamp);
                    break;
                }
                case SDL_FINGERUP: {
                    // Last chance flick: judge the lift-off from the fastest
                    // swipe speed seen during the gesture - short, fast flicks
                    // on touch panels often end before the mid-move check
                    // fires (and the last move can already be a slow one).
                    if (state == AppState::Play && !autoPlay && !paused) {
                        const float heightScale = static_cast<float>(windowH) / 1080.0f;
                        for (TouchTrack& track : touches) {
                            if (track.fingerId != event.tfinger.fingerId) {
                                continue;
                            }
                            // Fold in whatever was banked for the millisecond in
                            // progress: a short flick can end before that batch
                            // is processed, and those are exactly the pixels the
                            // gesture was made of.
                            const float upDelta = -track.pendingDy;
                            const float sideDelta = track.pendingDx;
                            if (upDelta != 0.0f) {
                                track.travelUp = (upDelta >= 0.0f) == (track.travelUp >= 0.0f)
                                    ? track.travelUp + upDelta
                                    : upDelta;
                            }
                            if (sideDelta != 0.0f) {
                                track.travelSide = (sideDelta >= 0.0f) == (track.travelSide >= 0.0f)
                                    ? track.travelSide + sideDelta
                                    : sideDelta;
                            }
                            track.pendingDx = 0.0f;
                            track.pendingDy = 0.0f;
                            const game::FlickDir dir = flickDirFrom(track.peakUp, track.peakSide, track.travelUp,
                                track.travelSide, track.isTouch, heightScale);
                            if (dir != game::FlickNone) {
                                const double songTime =
                                    songClock();
                                const game::Judge result = flickJudge(track, songTime, dir);
                                logFlickOutcome(track, songTime, dir, result, "fire/up");
                                if (result != game::Judge::None) {
                                    playHitSe(audio, judgement, seVolume);
                                }
                            } else if (gFlickLog.enabled) {
                                // The lift-off safety net also found nothing -
                                // this is the case a "touch flick feels dead"
                                // report turns out to be, so keep the numbers.
                                const double songTime = songClock();
                                logFlickOutcome(track, songTime, game::FlickNone, game::Judge::None, "up/no-dir");
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
                    // Diagnostic (CPSEKAI_UI_TRACE=1): proves a posted click
                    // arrived at all - invaluable when driving the UI headless,
                    // where a swallowed event and a mis-measured hit box look
                    // exactly the same from the outside.
                    if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
                        std::printf("[ui] mouse down at %d,%d (button %d)\n", event.button.x,
                            event.button.y, event.button.button);
                        std::fflush(stdout);
                    }
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
                                   event.button.y, userSettings.uiScale)) {
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
                            songClock();
                        if (currentSongTime + leadInSec < static_cast<double>(game::kHudIntroDurationSec)
                            && game::introSkipHitTest(windowW, windowH, event.button.x, event.button.y)) {
                            skipIntro();
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
                    beginPointer(pointerIdForButton(event.button.button), event.button.x, event.button.y, false,
                        event.button.timestamp);
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
                        movePointer(pointerIdForButton(SDL_BUTTON_LEFT), event.motion.x, event.motion.y,
                            event.motion.timestamp);
                    }
                    if ((event.motion.state & SDL_BUTTON_RMASK) != 0) {
                        movePointer(pointerIdForButton(SDL_BUTTON_RIGHT), event.motion.x, event.motion.y,
                            event.motion.timestamp);
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

        // ------------------------------------------------------------------
        // Game controller: turn the pad into key presses (see the note above
        // the main loop). Runs after the SDL event pump, so a pulse lands in
        // the queue and is consumed by ImGui at the start of the next frame.
        // ------------------------------------------------------------------
        // Release whatever pulsed last frame.
        if (!padReleaseQueue.empty()) {
            for (SDL_Scancode sc : padReleaseQueue) {
                SDL_Event up{};
                up.type = SDL_KEYUP;
                up.key.type = SDL_KEYUP;
                up.key.state = SDL_RELEASED;
                up.key.keysym.scancode = sc;
                up.key.keysym.sym = SDL_GetKeyFromScancode(sc);
                up.key.windowID = padWindowId;
                SDL_PushEvent(&up);
            }
            padReleaseQueue.clear();
        }
        if (pad != nullptr || !fakePad.empty()) {
            SDL_GameControllerUpdate();
            // --fake-pad cycles the button (held ~half a second, then released
            // for half a second) instead of firing once: a single fire would
            // only ever catch whatever screen is up in the first second, and
            // the dialogs this is meant to check open much later.
            // --fake-pad cycles the buttons (held ~half a second, released for
            // half a second) instead of firing once: a single fire would only
            // catch whatever screen is up in the first second, and the dialogs
            // this has to check open much later.
            if (!fakePad.empty() && (++fakePadFrames % 30) == 0) {
                std::printf("[pad] fake cycle %d\n", fakePadFrames / 30);
                std::fflush(stdout);
            }
            auto pulse = [&](SDL_Scancode sc) {
                SDL_Event down{};
                down.type = SDL_KEYDOWN;
                down.key.type = SDL_KEYDOWN;
                down.key.state = SDL_PRESSED;
                down.key.repeat = 0;
                down.key.keysym.scancode = sc;
                down.key.keysym.sym = SDL_GetKeyFromScancode(sc);
                down.key.windowID = padWindowId;
                SDL_PushEvent(&down);
                padReleaseQueue.push_back(sc);
            };
            auto pressed = [&](SDL_GameControllerButton button, bool& prev) {
                const bool down = padDown(button);
                const bool edge = down && !prev;
                prev = down;
                return edge;
            };

            // Directions: D-pad or left stick, with auto-repeat while held.
            {
                const Sint16 lx = pad != nullptr
                    ? SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX)
                    : 0;
                const Sint16 ly = pad != nullptr
                    ? SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY)
                    : 0;
                const bool dir[4] = {
                    padDown(SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -kPadDeadZone,
                    padDown(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > kPadDeadZone,
                    padDown(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -kPadDeadZone,
                    padDown(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > kPadDeadZone,
                };
                const SDL_Scancode dirKey[4] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,
                    SDL_SCANCODE_RIGHT};
                const Uint32 nowMs = SDL_GetTicks();
                for (int i = 0; i < 4; ++i) {
                    if (!dir[i]) {
                        padDirHeld[i] = false;
                        continue;
                    }
                    // First repeat is slow, the ones after it fast - the usual
                    // keyboard feel for a list that can hold hundreds of songs.
                    if (!padDirHeld[i] || nowMs >= padDirNext[i]) {
                        pulse(dirKey[i]);
                        padDirNext[i] = nowMs + (padDirHeld[i] ? 110u : 400u);
                    }
                    padDirHeld[i] = true;
                }
            }

            // Left / right shoulder: the settings card's tab row. Its tabs are
            // custom-drawn (InvisibleButton hit tests), so neither the keyboard
            // nor ImGui's own focus navigation can reach them - the pad is the
            // only way to switch pages without a mouse.
            const bool tabPrev = pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, padPrevL1);
            const bool tabNext = pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, padPrevR1);
            if (showDebug && (tabPrev || tabNext)) {
                settingsTab = (settingsTab + (tabNext ? 1 : kSettingsTabCount - 1)) % kSettingsTabCount;
                if (settingsTab < 0) {
                    settingsTab += kSettingsTabCount;
                }
                std::printf("[pad] settings tab -> %d\n", settingsTab);
                std::fflush(stdout);
            }
            const bool padA = pressed(SDL_CONTROLLER_BUTTON_A, padPrevA);
            const bool padB = pressed(SDL_CONTROLLER_BUTTON_B, padPrevB);
            const bool padX = pressed(SDL_CONTROLLER_BUTTON_X, padPrevX);
            const bool padY = pressed(SDL_CONTROLLER_BUTTON_Y, padPrevY);
            const bool padStart = pressed(SDL_CONTROLLER_BUTTON_START, padPrevStart);
            const bool padBack = pressed(SDL_CONTROLLER_BUTTON_BACK, padPrevBack);

            // A = 确定 / start the song / skip the opening card / continue;
            // START = pause or settings;  X / Y = dialog choices and rescan;
            // B / BACK = dismiss a dialog or leave a screen. B deliberately does
            // nothing in the song list: Escape quits the game there.
            //
            // The edges are read once above: calling pressed() twice for the
            // same button in one frame returns false the second time (prev is
            // already true), which is how "A also confirms the pause dialog"
            // silently stops working.
            if (pauseDialogOpen) {
                // Pause dialog: every button has to be reachable from the pad.
                // The dialog is custom-drawn, so the choice is handed to
                // ui::messageDialog as a forced click (same close animation).
                if (padA || padStart) {
                    pauseDialogChoice = 2; // 继续演出
                } else if (padX) {
                    pauseDialogChoice = 0; // 重试
                } else if (padY) {
                    pauseDialogChoice = 1; // 放弃
                }
                if (pauseDialogChoice >= 0) {
                    std::printf("[pad] pause dialog -> choice %d\n", pauseDialogChoice);
                    std::fflush(stdout);
                }
            } else if (padA) {
                if (state == AppState::Result) {
                    resultContinueRequested = true;
                } else if (state == AppState::Play && !paused && !countdownActive && introSkipAvailable()) {
                    // The opening card's skip button was mouse/touch only; A is
                    // free during the lead-in, so it takes that job.
                    skipIntro();
                } else {
                    pulse(SDL_SCANCODE_RETURN);
                }
            }
            if (padStart) {
                if (state == AppState::Play && !pauseDialogOpen && !countdownActive) {
                    // Same as the HUD pause button / Space: the dialog appears
                    // and its window_open sound plays.
                    requestPause();
                } else if (state == AppState::Result) {
                    resultContinueRequested = true;
                } else if (state != AppState::Play) {
                    pulse(SDL_SCANCODE_H); // settings card
                }
            }
            if (padB) {
                if (showDebug) {
                    pulse(SDL_SCANCODE_H); // close the settings card
                } else if (pauseDialogOpen || state == AppState::Result) {
                    pulse(SDL_SCANCODE_ESCAPE);
                }
            }
            if (!pauseDialogOpen && padY && state == AppState::Select) {
                pulse(SDL_SCANCODE_F5); // re-scan charts/
            }
            // BACK used to send Escape everywhere except the song list, i.e.
            // during a run it abandoned the song and went back - the last thing
            // a stray thumb press should do. In Play it pauses like START;
            // elsewhere it keeps the old meaning.
            if (padBack) {
                if (state == AppState::Play) {
                    if (!pauseDialogOpen && !countdownActive) {
                        requestPause();
                    }
                } else if (state != AppState::Select) {
                    pulse(SDL_SCANCODE_ESCAPE);
                }
            }
        }

        if (state == AppState::Play && paused && !countdownActive) {
            SDL_Delay(16);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        // Fixed render mode: ImGui has to lay out in the *render* size (it draws
        // into the same offscreen buffer as the scene) instead of the window's.
        // FramebufferScale 1.0 also means the atlas is rasterised at that size
        // and then scaled up with everything else, which is exactly what "只渲染
        // 这个分辨率" means. Mouse positions arrive already mapped by
        // mapPointerEvent(); the backend's own fallback path (mouse outside the
        // window) is left alone - ImGui has nothing hovered there anyway.
        if (renderer.offscreen()) {
            ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(windowW), static_cast<float>(windowH));
            ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        }
        ImGui::NewFrame();

        if (state == AppState::Select) {
            // ----------------------------------------------------------
            // Song select
            // ----------------------------------------------------------
            // ----------------------------------------------------------
            // 多人游玩: read the room before drawing. The song select *is* the
            // room screen (see platform/Party.hpp), so what the host locked has
            // to be known by the time the phone panel is laid out below.
            // ----------------------------------------------------------
            platform::PartyState mpSnap = party.read();
            int mpLocked = -1;
            if (party.active()) {
                mpLocked = partyLockedEntry(mpSnap);
                // The host moved to another song: this window's answer belonged
                // to the old one, so it goes back to "nothing picked". The epoch
                // is what makes that happen once per song.
                if (!party.isHost() && mpSnap.phase == platform::PartySongLocked
                    && mpSnap.epoch != mpSeenLockEpoch) {
                    mpSeenLockEpoch = mpSnap.epoch;
                    mpConfirmed = false;
                    mpSpectating = false;
                    mpMyDifficulty = -1;
                    mpEntryIndex = -1;
                    party.setDifficulty(-1);
                    party.setReady(false);
                    std::printf("[party] host picked '%s' (%s) - pick a difficulty in the phone panel\n",
                        mpSnap.songTitle.c_str(), mpSnap.songKey.c_str());
                    std::fflush(stdout);
                }
                // Back in the lobby (the run just ended): arm the next round, so
                // the same button starts the next song instead of replaying.
                if (mpSnap.phase == platform::PartyLobby
                    && (mpConfirmed || mpSpectating || mpPublished)) {
                    mpConfirmed = false;
                    mpSpectating = false;
                    mpPublished = false;
                    mpPublishedKey.clear();
                    mpMyDifficulty = -1;
                    mpEntryIndex = -1;
                    party.setReady(false);
                    party.setDifficulty(-1);
                }
            }
            if (!stageBackgroundFor.empty()) {
                // Back to the default room plate: the song select is not a
                // performance, and the next song generates its own anyway.
                stageBackgroundFor.clear();
                renderer.setSongBackground(nullptr, 0, 0, error);
                error.clear();
            }
            renderer.setLaneGlows({});
            // playfieldVisibility 0: the song select is not a performance, so the
            // stage/playfield plate must not be drawn - only the backdrop. It
            // never showed before because the select screen's own background is
            // opaque; with the "Aero glass" background (bgStyle 2) it does, and a
            // playfield sitting behind the song list looks like a bug.
            renderer.renderFrame(nullptr, 0, 0.85f, 0.0f);

            // Music preview: cut a clip from partway into the BGM and loop it,
            // the way the official select screen never previews from the top.
            // Reloads only when the selection (or the chosen vocal version)
            // changes; ran after drawSongSelect because `selectedVocal` is this
            // frame's pick. Charts that ship only official vocal files have no
            // <id4>.mp3 for the scan to find, so the chip is what decides what
            // the preview plays.
            // Only the host plays the preview: two windows previewing the same
            // clip a few milliseconds apart sound like a broken speaker, and
            // the rule for the live is the same (BGM belongs to the host).
            // 多人游玩: the preview is off the moment a charge is armed. The
            // host stays on this screen for the whole lead-in *with the live's
            // BGM already loaded*, so a preview that kept restarting here mixed
            // the song-select clip into the running track (the "有概率混一起"
            // report: it depended on which frame the charge landed in).
            const bool playPreviewHere = (!party.active() || party.isHost()) && !mpStartPending;
            if (playPreviewHere && selected >= 0 && selected < static_cast<int>(entries.size())) {
                const game::ChartEntry& playing = entries[static_cast<size_t>(selected)];
                std::string previewPath = game::vocalAudioPath(playing, selectedVocal);
                if (previewPath.empty()) {
                    previewPath = playing.bgmPath;
                }
                // Only a *version* switch continues where the preview was: a
                // different song has to start at its own clip start.
                static std::string previewSong;
                const bool sameSong = !previewSong.empty() && previewSong == playing.susPath;
                previewSong = playing.susPath;
                audio.startPreview(previewPath, error, sameSong);
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
            if (testVocalSwitchSec >= 0.0 && !testVocalSwitchDone && selected >= 0
                && uiClock >= testVocalSwitchSec) {
                testVocalSwitchDone = true;
                const int versionCount =
                    static_cast<int>(game::availableVocals(entries[static_cast<size_t>(selected)]).size());
                if (versionCount > 1) {
                    selectedVocal = (std::max(selectedVocal, 0) + 1) % versionCount;
                    std::printf("[t] switched to vocal version %d\n", selectedVocal);
                    std::fflush(stdout);
                }
            }

            // Host: put the song this window is sitting on into the room. The
            // members' phone panels follow it, so this runs on every move of the
            // list cursor - the lock's epoch is what tells them "new song, pick
            // again". Nothing is published while a charge is armed: the round is
            // already loaded on the old song.
            auto publishHostSong = [&]() {
                if (!party.active() || !party.isHost() || mpStartPending) {
                    return;
                }
                if (selected < 0 || selected >= static_cast<int>(entries.size())) {
                    return;
                }
                const platform::PartyState snap = party.read();
                if (snap.phase == platform::PartyCharging || snap.phase == platform::PartyRunning) {
                    return;
                }
                const game::ChartEntry& focused = entries[static_cast<size_t>(selected)];
                const std::string key = chartFileName(focused.susPath);
                if (mpPublished && key == mpPublishedKey) {
                    return; // same song, nothing for the room to hear about
                }
                mpPublished = true;
                mpPublishedKey = key;
                mpConfirmed = false; // a new song needs a fresh 确定
                mpConfirmedEpoch = -1;
                mpSpectating = false;
                const int diff = game::difficultyIndex(focused.difficulty);
                mpMyDifficulty = diff >= 0 ? diff : -1;
                party.setReady(false);
                party.setDifficulty(mpMyDifficulty);
                party.lockSong(focused.musicId, key,
                    focused.title.empty() ? focused.displayName : focused.title, mpMyDifficulty,
                    static_cast<int>(std::lround(baseLeadInSec * 1000.0)));
                mpSeenLockEpoch = party.read().epoch;
            };
            if (party.active() && party.isHost()) {
                publishHostSong();
            }

            // The room line under the phone panel's 确定 button: what this
            // window is waiting for right now.
            if (party.active()) {
                // Alone in the room (one window on this machine): 多人游玩 has
                // nothing to coordinate, so its whole vocabulary - the badge, the
                // "other players pick their difficulty on their phone" line -
                // would be noise describing people who are not there. The room
                // itself stays live (the next window that opens joins it); only
                // the UI goes quiet.
                const bool partySolo = party.playerCount() <= 1;
                if (party.isHost()) {
                    if (selected < 0 || selected >= static_cast<int>(entries.size())) {
                        mpStatus = partySolo ? std::string() : "选一首曲子";
                    } else if (!mpConfirmed) {
                        mpStatus = partySolo ? std::string() : "点确定开始（其他玩家在手机上选难度）";
                    } else if (party.allReady()) {
                        mpStatus = partySolo ? std::string() : "全员确定 · 即将开始";
                    } else {
                        mpStatus = partySolo ? std::string() : "已确定 · 等待其他玩家（再按一次强制开始）";
                    }
                } else if (mpSpectating) {
                    mpStatus = "旁观中 · 点确定加入本曲";
                } else if (mpSnap.phase == platform::PartyLobby) {
                    mpStatus = "等待房主选曲";
                } else if (mpLocked < 0) {
                    // Named out loud, because "房主选了一首这台机器上没有的
                    // 曲子" is otherwise invisible: this window would just sit
                    // there while the others ready up.
                    mpStatus = "本窗口没有" + (mpSnap.songTitle.empty() ? mpSnap.songKey : mpSnap.songTitle)
                        + "，无法参加本曲";
                } else if (mpMyDifficulty < 0) {
                    mpStatus = "在手机上选择难度";
                } else if (!mpConfirmed) {
                    mpStatus = "点确定加入本曲";
                } else {
                    mpStatus = "已确定 · 等待其他玩家";
                }
                // The charge is on: say which half it is in. No numbers - the
                // loading phase is as long as the slowest window's load (under a
                // second), and the armed phase is one fuse long (0.8s), so a
                // counter would be a flicker rather than information.
                if (mpConfirmed && mpSnap.phase == platform::PartyCharging) {
                    mpStatus = mpSnap.startCounter != 0 ? "即将开始" : "谱面加载中…";
                }
            }

            game::SelectPartyInfo selParty;
            if (party.active()) {
                selParty.active = true;
                // ... and the status stays as-is even when alone: the charge
                // lines ("谱面加载中…" / "即将开始") and the error lines
                // ("本窗口没有…" / "谱面加载失败") are about *this* window, not
                // about other players, so they keep showing.
                selParty.host = party.isHost();
                selParty.songLocked = mpSnap.phase == platform::PartySongLocked
                    || mpSnap.phase == platform::PartyCharging
                    || mpSnap.phase == platform::PartyRunning;
                selParty.lockedEntry = mpLocked;
                selParty.myDifficulty = mpMyDifficulty;
                selParty.confirmed = mpConfirmed;
                selParty.spectating = mpSpectating;
                selParty.status = mpStatus;
            }
            game::SelectPartyResult selPartyOut;

            const int prevSortMode = userSettings.sortMode;
            const int prevGroupMode = userSettings.groupMode;
            int action = game::drawSongSelect(renderer, entries, selected, windowW, windowH,
                static_cast<float>(uiClock), userSettings.sortMode, userSettings.groupMode,
                selectedVocal, userSettings.uiScale, &selectConfirmCenter, &account,
                party.active() ? &selParty : nullptr, party.active() ? &selPartyOut : nullptr);
            // Debug (--party-auto): the host's 确定, without a mouse. Waits for
            // the list to settle so the chart it picks is the one the list
            // starts on, not whatever a startup animation left selected; in a
            // room it waits for a second player (unless nobody turns up within
            // a few seconds, which is the "solo in a room" run), and with no
            // room at all it is simply a "press 确定" for a solo regression run.
            if (partyAutoGiven && !partyAutoConfirmed && selected >= 0 && uiClock >= partyAutoAtSec
                && (!party.active() || partyUsable() || uiClock >= partyAutoAtSec + 6.0)) {
                partyAutoConfirmed = true;
                if (party.active()) {
                    selPartyOut.confirm = true;
                } else {
                    action = selected;
                }
                std::printf("[party] auto-confirm chart #%d (debug)\n", selected);
                std::fflush(stdout);
            }
            // Diagnostic (CPSEKAI_UI_TRACE=1): where the 确定 button landed, in
            // window pixels - what an input-driving script has to click.
            if (std::getenv("CPSEKAI_UI_TRACE") != nullptr) {
                std::printf("[ui] confirm button at %.0f,%.0f (window %dx%d)\n", selectConfirmCenter.x,
                    selectConfirmCenter.y, windowW, windowH);
                std::fflush(stdout);
            }
            if (prevSortMode != userSettings.sortMode || prevGroupMode != userSettings.groupMode) {
                persistUserData();
            }
            // Consume the F5 request here so it cannot leak into a later frame.
            // The chartdl hand-off counts as a rescan request too: the moment
            // the downloader process is gone, whatever it fetched has to show
            // up in the list (that is the whole point of the empty-list button).
            const bool chartDlFinished = pollChartDownloader();
            const bool wantRescan =
                rescanRequested || action == game::SelectRescan || chartDlFinished;
            rescanRequested = false;
            if (action >= 0 && action < static_cast<int>(entries.size())) {
                if (party.active()) {
                    // 多人游玩: 确定 never starts a local live - it is the room's
                    // button (see the block right below), so it only confirms
                    // this window. No white burst either: nothing is loaded
                    // here, the load happens when the shared charge is armed
                    // and *that* is what the flash covers.
                    selPartyOut.confirm = true;
                } else {
                    // Point the entry at the chosen vocal version's audio before
                    // it goes in (the entries themselves are const here).
                    game::ChartEntry playEntry = entries[static_cast<size_t>(action)];
                    game::applyVocalVersion(playEntry, game::availableVocals(playEntry), selectedVocal);
                    // Kick off the white burst instead of loading right here: the
                    // load is what stalls, so it runs later, once the screen is
                    // white (see the confirm-flash block below).
                    confirmPendingEntry = playEntry;
                    confirmStartPending = true;
                    confirmFlashActive = true;
                    confirmFlashTime = 0.0f;
                    confirmFlashOrigin = selectConfirmCenter;
                    ui::se(ui::SeStart); // start.mp3, fires with the burst
                }
            } else if (action == game::SelectSettings) {
                showDebug = true;
            } else if (action == game::SelectDownload) {
                // The list is empty and 下载谱面 was pressed: hand off to the
                // standalone downloader. It runs as its own process (a window,
                // not a child dialog) so this frame loop keeps rendering behind
                // it; the handle is polled further down and the chart scan runs
                // again once the downloader exits, which is exactly what the
                // player expects after picking songs in it.
                if (!launchChartDownloader()) {
                    std::printf("[chartdl] could not start the downloader\n");
                    std::fflush(stdout);
                }
            }
            // Debug (--chartdl-test): press the empty-list button from the log,
            // since a posted click cannot reach a screen that needs a real
            // interactive session. Fires at most once, and only when the list is
            // actually empty (which is the only state the button exists in).
            if (chartDlTestAtSec >= 0.0 && uiClock >= chartDlTestAtSec && entries.empty()
                && !chartDlTestFired) {
                chartDlTestFired = true;
                std::printf("[chartdl] auto-pressed 下载谱面 (debug)\n");
                std::fflush(stdout);
                launchChartDownloader();
            } else if (wantRescan) {
                entries = scanAllChartDirs();
                selected = entries.empty() ? -1 : 0;
                game::applyScores(entries, scores);
                loadedCoverPath.clear();
                std::printf("[select] %d chart(s)\n", static_cast<int>(entries.size()));
            }

            // Debug (--confirm-flash): play the 确定 burst on its own, so a
            // --screenshot run can inspect the transition (nothing is loaded).
            if (confirmFlashShot && !confirmFlashShotFired && uiClock >= confirmFlashAtSec) {
                confirmFlashShotFired = true;
                confirmFlashActive = true;
                confirmFlashTime = 0.0f;
                confirmFlashOrigin = selectConfirmCenter;
                ui::se(ui::SeStart);
                std::printf("[ui] confirm flash at %.0f,%.0f (debug)\n", selectConfirmCenter.x,
                    selectConfirmCenter.y);
                std::fflush(stdout);
            }

            // ---- 确定: the room's one button --------------------------------
            // The host locks the song with it, a member marks itself ready with
            // it, and the live starts the moment every player still in the round
            // has pressed it (see the charge block below).
            if (party.active()) {
                const bool host = party.isHost();
                if (selPartyOut.spectate) {
                    // 旁观: out of this round without leaving the room, so an
                    // idle window can never hold the host's start hostage.
                    mpSpectating = !mpSpectating;
                    mpConfirmed = false;
                    party.setReady(false);
                    party.setSeat(mpSpectating ? platform::PartySeatLobby : platform::PartySeatChoosing);
                    std::printf("[party] %s\n", mpSpectating ? "spectating this round" : "back in the round");
                    std::fflush(stdout);
                }
                if (!host && selPartyOut.difficulty >= 0) {
                    mpMyDifficulty = selPartyOut.difficulty;
                    mpSpectating = false;
                    party.setDifficulty(mpMyDifficulty);
                    std::printf("[party] difficulty %s (%d)\n", game::difficultyName(mpMyDifficulty),
                        mpMyDifficulty);
                    std::fflush(stdout);
                }
                if (selPartyOut.confirm) {
                    const bool roundOn = mpSnap.phase == platform::PartyCharging
                        || mpSnap.phase == platform::PartyRunning;
                    if (roundOn) {
                        // A round is already loading or running: 确定 cannot
                        // start a second one (that is what the phase is for).
                        std::printf("[party] confirm ignored (the round is already on)\n");
                        std::fflush(stdout);
                    } else if (host) {
                        if (selected >= 0 && selected < static_cast<int>(entries.size())) {
                            publishHostSong();
                            // The host's own difficulty is whatever chart it is
                            // sitting on; it can change it right up to 确定.
                            const int hostDiff = game::difficultyIndex(entries[static_cast<size_t>(selected)].difficulty);
                            if (hostDiff >= 0) {
                                mpMyDifficulty = hostDiff;
                                party.setDifficulty(hostDiff);
                            }
                            // Already confirmed and the room is still not
                            // ready: this press forces the start, so a member
                            // who never confirms cannot stall the rest.
                            const bool forcing = mpConfirmed && !party.allReady();
                            mpConfirmed = true;
                            mpConfirmedEpoch = party.read().epoch;
                            party.setReady(true);
                            std::printf("[party] host confirmed%s (epoch %d)\n",
                                forcing ? " [force start]" : "", mpConfirmedEpoch);
                            std::fflush(stdout);
                            if (forcing) {
                                // Forcing means "start now, do not wait for the
                                // stragglers" - so it opens the *loading* phase
                                // like a normal start and lets the arming block
                                // decide when the instant is (a window that never
                                // confirmed sits this one out at once, so it
                                // cannot hold the start).
                                party.beginLoading();
                                mpChargeStartCounter = platform::PartyLink::nowCounter();
                                std::printf("[party] forced start -> loading\n");
                                std::fflush(stdout);
                            }
                        }
                    } else if (mpSpectating) {
                        // Joining back in is the same press: it un-spectates and
                        // readies up in one go.
                        mpSpectating = false;
                        if (mpLocked >= 0 && mpMyDifficulty >= 0) {
                            mpConfirmed = true;
                            party.setSeat(platform::PartySeatReady);
                            party.setReady(true);
                            party.setDifficulty(mpMyDifficulty);
                        }
                    } else if (mpSnap.phase == platform::PartyLobby || mpLocked < 0) {
                        mpStatus = "房主还没有定下本窗口能打的曲子";
                        std::printf("[party] confirm ignored (no playable song in this window)\n");
                        std::fflush(stdout);
                    } else if (mpMyDifficulty < 0) {
                        mpStatus = "先在手机上选择难度";
                        std::printf("[party] confirm ignored (no difficulty picked)\n");
                        std::fflush(stdout);
                    } else {
                        mpConfirmed = true;
                        party.setSeat(platform::PartySeatReady);
                        party.setReady(true);
                        party.setDifficulty(mpMyDifficulty);
                        std::printf("[party] confirmed, %s (%d)\n", game::difficultyName(mpMyDifficulty),
                            mpMyDifficulty);
                        std::fflush(stdout);
                    }
                }
            }

            // Debug (--party-auto <n>): drive a member window without a mouse -
            // pick that difficulty as soon as the host's song shows up, then
            // press 确定. See the flag's help text.
            if (partyAutoGiven && partyAutoDiff >= 0 && party.active() && !party.isHost()) {
                if (partyAutoMemberAt <= 0.0) {
                    partyAutoMemberAt = uiClock + 1.0; // let the host's song arrive
                } else if (uiClock >= partyAutoMemberAt) {
                    if (mpSnap.phase == platform::PartyLobby || mpLocked < 0) {
                        partyAutoMemberAt = uiClock + 0.5;
                    } else if (mpMyDifficulty < 0) {
                        if (findPartyEntry(mpSnap.musicId, mpSnap.songKey, partyAutoDiff) >= 0) {
                            mpMyDifficulty = partyAutoDiff;
                            mpSpectating = false;
                            party.setDifficulty(mpMyDifficulty);
                            std::printf("[party] auto difficulty %s (debug)\n",
                                game::difficultyName(mpMyDifficulty));
                            std::fflush(stdout);
                        } else {
                            partyAutoMemberAt = uiClock + 0.5;
                        }
                    } else if (!mpConfirmed) {
                        mpConfirmed = true;
                        party.setSeat(platform::PartySeatReady);
                        party.setReady(true);
                        std::printf("[party] auto confirm (debug)\n");
                        std::fflush(stdout);
                    }
                }
            }

            // ---- the room: charge, load, go --------------------------------
            // No room screen any more: the host publishes the instant once
            // everybody has confirmed, every window loads its own chart, and the
            // shared QPC instant is what makes them reach chart time 0 together.
            if (party.active()) {
                mpSnap = party.read();
                const bool host = party.isHost();

                // Host: everybody in the round is ready -> open the loading
                // phase. No start instant yet: the host arms it once every
                // window reports its chart decoded (see the arming block below),
                // so the wait is exactly as long as the slowest load instead of
                // a fixed countdown.
                if (host && mpConfirmed && mpConfirmedEpoch == mpSnap.epoch
                    && mpSnap.phase == platform::PartySongLocked && party.allReady()) {
                    party.beginLoading();
                    // QPC, not the frame clock: the load itself happens inside a
                    // frame, so frame time would hide the very thing this is
                    // here to measure.
                    mpChargeStartCounter = platform::PartyLink::nowCounter();
                    std::printf("[party] all ready -> loading%s\n",
                        party.playerCount() < 2 ? " [solo in the room]" : "");
                    std::fflush(stdout);
                }

                // Everybody: the charge is on -> load this window's chart.
                if (mpSnap.phase == platform::PartyCharging && mpSnap.epoch != mpSeenChargeEpoch) {
                    mpSeenChargeEpoch = mpSnap.epoch;
                    // startCounter == 0 is the loading phase (the instant is not
                    // armed yet) - not a start this window missed. QPC is a big
                    // positive number, so a bare `now >= 0` would bench every
                    // window the moment the room opened the charge.
                    const bool late = mpSnap.startCounter != 0
                        && platform::PartyLink::nowCounter() >= mpSnap.startCounter;
                    if (!host && (!mpConfirmed || late)) {
                        // The room started without this window - it never
                        // confirmed, or it joined after the instant. Sit the
                        // round out instead of dropping into a live that is
                        // already running (and stop holding the start hostage).
                        mpConfirmed = false;
                        party.setSeat(platform::PartySeatLobby);
                        party.setReady(false);
                        mpStatus = late ? "本曲已经开始" : "房主已开始，你未确定";
                        std::printf("[party] charge skipped (%s)\n",
                            late ? "joined after the start" : "not confirmed");
                        std::fflush(stdout);
                    } else {
                        leadInSec = partyLeadInSec(mpSnap);
                        // The host plays the chart it published (its own
                        // difficulty); a member plays the one it picked, which
                        // the two windows name by canonical index.
                        mpEntryIndex = host ? selected
                                            : findPartyEntry(mpSnap.musicId, mpSnap.songKey, mpMyDifficulty);
                        if (mpEntryIndex < 0 || mpEntryIndex >= static_cast<int>(entries.size())) {
                            mpStatus = "本窗口没有这首曲子";
                            std::printf("[party] charge failed: chart missing\n");
                            std::fflush(stdout);
                        } else {
                            mpEntry = entries[static_cast<size_t>(mpEntryIndex)];
                            if (host) {
                                // The host keeps the vocal version it picked in
                                // the song select; a member has no BGM of its
                                // own, so the scan's default is good enough.
                                game::applyVocalVersion(mpEntry, game::availableVocals(mpEntry),
                                    selectedVocal);
                            } else {
                                game::applyDefaultVocal(mpEntry);
                            }
                            // Only the host decodes the BGM (see startSession).
                            if (!startSession(session, mpEntry, renderer, audio, judgement, noteSpeed, error,
                                    host)) {
                                mpStatus = "谱面加载失败";
                                std::printf("[party] charge failed: %s\n", error.c_str());
                                std::fflush(stdout);
                                error.clear();
                                mpConfirmed = false;
                                party.setReady(false);
                                // The host's own load failed, so there is no
                                // chart to start: hand the room back to the
                                // lobby instead of leaving it charging for an
                                // instant that would never be armed. The others
                                // read that as 房主放弃本曲 and return to the list.
                                if (host) {
                                    party.releaseSong();
                                }
                            } else {
                                // The run length has to travel with the round:
                                // a member has no track of its own, and without
                                // this it would sit on the result screen
                                // seconds away from the host (see the play
                                // state, which re-reads it every frame).
                                announceTrack();
                                if (host) {
                                    party.publishTrackEnd(trackDurationSec);
                                }
                                mpFollowing = !host;
                                mpClockSynced = false;
                                mpHostPaused = false;
                                mpStartPending = true;
                                loadedCoverPath = session.entry.coverPath;
                                touches.clear();
                                std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                                lanePress.fill(0.0f);
                                paused = false;
                                resultScheduled = false;
                                resultData = game::ResultData{};
                                songEndBlackout = 0.0f;
                                // The white burst is the cover for exactly this
                                // load (see confirmFlashActive): arm it here,
                                // with nothing for it to load of its own.
                                confirmStartPending = false;
                                confirmFlashActive = true;
                                confirmFlashTime = 0.0f;
                                confirmFlashOrigin = selectConfirmCenter;
                                ui::se(ui::SeStart);
                                // Still 准备 until the clock is armed (below),
                                // so the room does not claim this seat is
                                // already playing during the lead-in.
                                party.clearPlayingScore();
                                // Report in: this is the flag the host waits for
                                // before it arms the shared instant (allLoaded).
                                // A window that never gets here simply is not
                                // waited for past the watchdog below.
                                party.setSeat(platform::PartySeatLoaded);
                                std::printf("[party] chart loaded (%s, %s), waiting for the shared start\n",
                                    mpEntry.susPath.c_str(), mpEntry.difficulty.c_str());
                                std::fflush(stdout);
                            }
                        }
                    }
                }

                // Host: everybody's chart is in -> fix the instant. This is the
                // whole point of the loading phase: the round starts the moment
                // the slowest window is ready, so there is nothing to count
                // down. `mpArmedEpoch` keeps it to one arming per charge (a
                // second write would push the instant forward for ever), and
                // `mpStartPending` means this window's own chart is decoded.
                //
                // The fuse only has to cover one frame on the other windows
                // (16-33ms at their fps), so 0.8s is generous and still reads as
                // "instant". The watchdog is the safety net for a window that
                // never reports in (a failed load, a hung decode): past
                // kPartyLoadTimeoutSec the room goes without it rather than
                // sitting on the song list for ever.
                if (host && mpStartPending && mpSnap.phase == platform::PartyCharging
                    && mpSnap.startCounter == 0 && mpArmedEpoch != mpSnap.epoch) {
                    const bool allIn = party.allLoaded();
                    const double waited = platform::PartyLink::counterToSeconds(
                                              platform::PartyLink::nowCounter())
                        - platform::PartyLink::counterToSeconds(mpChargeStartCounter);
                    const bool stalled = waited > kPartyLoadTimeoutSec;
                    if (allIn || stalled) {
                        mpArmedEpoch = mpSnap.epoch;
                        constexpr double kPartyStartFuseSec = 0.8;
                        party.armStart(platform::PartyLink::nowCounter()
                            + static_cast<Uint64>(
                                kPartyStartFuseSec * platform::PartyLink::counterFrequency()));
                        // The waited-for figure is the number that matters when
                        // the start feels slow: it is the slowest window's load.
                        // It used to be a flat 5.8s whatever the load took.
                        std::printf("[party] start in %.2fs (%s; the room waited %.2fs for the loads)\n",
                            kPartyStartFuseSec,
                            allIn ? "every chart loaded" : "watchdog: somebody never loaded", waited);
                        std::fflush(stdout);
                    }
                }

                // Everybody: the instant arrived -> go. This is also how a
                // window that joined during the charge falls in on the beat.
                if (mpStartPending && mpSnap.startCounter != 0
                    && platform::PartyLink::nowCounter() >= mpSnap.startCounter) {
                    mpStartPending = false;
                    // The clock is armed on the *shared* counter, so every window
                    // reaches chart time 0 on the same beat (the host's audio clock
                    // is then what the members steer onto, see resolveSongClock).
                    beginSessionClockAt(mpSnap.startCounter);
                    party.setSeat(platform::PartySeatPlaying);
                    state = AppState::Play;
                    std::printf("[party] go (lead-in %.1fs, start counter %llu)\n", leadInSec,
                        static_cast<unsigned long long>(mpSnap.startCounter));
                    std::fflush(stdout);
                }
            }

            // Settings card, opened from the musicsetting button (or H).
            drawSettingsCard();
            // 多开的实验性功能确认框（设置卡片里点出来的）。
            drawMultiInstanceAskDialog();
            // Who else is in the room (hidden while the settings card is up:
            // it draws on the foreground list, above the card). A room with a
            // single seat is just this window, so the badge and its hint - the
            // one thing that would say "waiting for other players" - stay away
            // until somebody actually joins.
            if (party.active() && party.playerCount() > 1 && !showDebug) {
                std::string hint;
                if (party.isHost()) {
                    hint = mpConfirmed ? "已确定 · 再按一次可强制开始" : "选好歌按确定开始";
                } else if (mpSpectating) {
                    hint = "旁观中 · 手机里选难度后按确定";
                } else if (mpConfirmed) {
                    hint = "已确定 · 等待其他玩家";
                } else {
                    hint = "在手机里选难度，然后按确定";
                }
                game::drawPartyBadge(party.players(), party.slot(), windowW, windowH,
                    userSettings.uiScale, hint);
            }
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
            const double songTime = songClock();
            audio.update();
            const float outputTime = static_cast<float>(songTime + leadInSec);

            // 多人游玩: the room can move on while this window is playing. The
            // host's 放弃 / 重试 on its pause dialog hands the room back to the
            // lobby (phase Lobby + a new epoch) - without watching for that, a
            // member would keep playing a live nobody is timing any more and
            // never come back to the list.
            if (party.active() && !party.isHost()) {
                const platform::PartyState snap = party.read();
                if (snap.phase == platform::PartyLobby && mpSeenChargeEpoch >= 0
                    && snap.epoch != mpSeenChargeEpoch) {
                    // ... but the host dropping to the lobby right after a live is
                    // the *result* hand-over, not a give-up: it puts its own seat
                    // on 结算中 first. Both windows switch on the same chart time
                    // but not in the same frame, so without this check the one
                    // that is a frame behind would be yanked to the song select
                    // instead of its own result screen.
                    bool hostFinished = false;
                    for (const platform::PartyPlayer& player : party.players()) {
                        if (player.host && player.seat == platform::PartySeatResult) {
                            hostFinished = true;
                            break;
                        }
                    }
                    if (!hostFinished) {
                        party.setSeat(platform::PartySeatLobby);
                        party.setReady(false);
                        mpConfirmed = false;
                        leaveLiveForRoom("房主已放弃本曲");
                    }
                }
            }

            // 多人游玩 member: the run length comes from the host. This window
            // has no BGM, so its own guess would be the chart's last note -
            // seconds away from the host's track, which is what the result
            // screen switches on. Read every frame: the host publishes it right
            // after it loads, i.e. possibly after this window got here.
            if (party.active() && !party.isHost()) {
                const double sharedEnd = party.readTrackEnd();
                if (sharedEnd > 1.0 && std::fabs(sharedEnd - trackDurationSec) > 0.05) {
                    trackDurationSec = sharedEnd;
                    std::printf("[party] run length from the host: %.2fs\n", sharedEnd);
                    std::fflush(stdout);
                }
            }

            // Report to Windows: SMTC position + taskbar button progress.
            // 多人游玩: only the host owns the live as far as Windows is
            // concerned (see announceTrack).
            const bool smtcHere = !party.active() || party.isHost();
            if (userSettings.reportSmtc && smtcHere) {
                systemMedia.updatePlayback(true, paused, songTime, trackDurationSec);
            }
            if (smtcHere) {
                systemMedia.setTaskbarProgress(
                    trackDurationSec > 1.0 ? songTime / trackDurationSec : -1.0, paused);
            }

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
            }
            // Published in both modes, and every frame: in autoplay the list is
            // empty, which is exactly what the renderer needs to hear - a
            // preview that publishes nothing keeps whatever the *previous* run
            // left in the core, and would draw those holds as dropped.
            {
                const std::vector<float>& missed = judgement.missedHoldKeys();
                static std::size_t lastMissedKeys = 0;
                if (missed.size() != lastMissedKeys) {
                    lastMissedKeys = missed.size();
                    std::printf("[hold] %d long note(s) now marked missed\n",
                        static_cast<int>(missed.size() / 2));
                    std::fflush(stdout);
                }
                core_api::setMissedHolds(missed);
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

            // Fade to black as the track runs out; the result screen inherits this
            // value and lifts it (see the transition overlays at the end of the
            // frame). Starting a little before the end means the hand-over happens
            // on a black screen instead of a hard cut.
            //
            // Measured against the same end the result triggers on (including the
            // --result-at debug override), so the fade always leads into the switch.
            const double effectiveEnd = resultAtSec > 0.0 ? resultAtSec : trackDurationSec - 0.15;
            if (trackDurationSec > 1.0) {
                const float fadeFrom = static_cast<float>(effectiveEnd) - kSongEndFadeSec;
                const float u = std::clamp((static_cast<float>(songTime) - fadeFrom) / kSongEndFadeSec,
                    0.0f, 1.0f);
                songEndBlackout = std::max(songEndBlackout, u);
            }

            // ----------------------------------------------------------
            // Song finished -> result screen. The music is cut here (the
            // chart keeps running past the last note while the track plays
            // out), then the result screen takes over with its own clock.
            // ----------------------------------------------------------
            const bool resultDue = state == AppState::Play && trackDurationSec > 1.0
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
                // Fresh each run: an autoplay preview never banks exp, and the
                // result screen must not show the previous song's gain.
                resultExpGain = 0;
                resultRankUps = 0;
                if (!autoPlay && !session.scoreRecorded && songTime >= trackDurationSec - 0.25) {
                    session.scoreRecorded = true;
                    const bool cleared = st.life > 0.0f;
                    const bool fullCombo = cleared && st.miss == 0;
                    const std::string key = game::scoreKey(session.entry);
                    // Keep the old best for the result screen's 最高得分 /
                    // 新纪录! before the merge below overwrites it.
                    resultPreviousBest = scores[key].bestScore;
                    scores[key] = game::mergeScore(scores[key], cleared, fullCombo, st.score);
                    // Player rank: an official live grants the score-rank
                    // multiplier (we have no live-bonus system, so it is not
                    // multiplied again). Same score-rank call the result screen
                    // makes, so the badge and the exp can never disagree.
                    const game::ScoreRank sr = game::scoreRankAndBar(st.score, judgement.chartRating());
                    resultExpGain = game::scoreRankExp(sr.rank);
                    resultRankUps = game::addPlayerExp(account, resultExpGain);
                    ++account.plays;
                    account.totalScore += st.score;
                    game::applyScores(entries, scores);
                    persistUserData();
                    std::printf("[score] %s %s%s (life=%.0f/%.0f) (%s)\n", key.c_str(),
                        cleared ? "cleared" : "failed", fullCombo ? " (full combo)" : "",
                        static_cast<double>(st.life), static_cast<double>(game::kMaxLife),
                        userDataFile.c_str());
                    std::printf("[rank] %c +%d exp -> rank %d (%d/%d)%s\n", sr.rank, resultExpGain,
                        account.rank, static_cast<int>(account.exp),
                        static_cast<int>(game::expToNextRank(account.rank)),
                        resultRankUps > 0 ? " (rank up)" : "");
                    std::fflush(stdout);
                }
                resultData = buildResultData(session.intro, session.entry, st,
                    resultPreviousBest, judgement.chartRating(), account, resultExpGain, resultRankUps);
                audio.setHoldLoop(false, false, 0.0f);
                audio.stopMusic();
                touches.clear();
                std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                lanePress.fill(0.0f);
                paused = false;
                countdownActive = false;
                systemMedia.setTaskbarProgress(-1.0, false);
                if (party.active()) {
                    // The live is over for this window. The host hands the room
                    // back to the lobby (which is also what re-arms the next
                    // round: see the song-select branch) so the others stop
                    // following a clock that is about to stop; everybody shows
                    // up as 结算中.
                    party.setHostPaused(false);
                    party.setSeat(platform::PartySeatResult);
                    if (party.isHost()) {
                        party.releaseSong();
                    }
                    // Nobody is following anybody outside a live: the follower
                    // is (re)armed when the next charge loads the chart.
                    mpFollowing = false;
                    mpHostPaused = false;
                }
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
                // 0.70: the upstream overlay player's hold-loop level.
                audio.setHoldLoop(holding, holdCritical, seVolume * 0.70f);
            }

            // ----------------------------------------------------------
            // Lane highlight: hover (mouse) + press (keys / touches)
            // ----------------------------------------------------------
            int mouseX = 0;
            int mouseY = 0;
            laneHover.fill(0.0f);
            if (SDL_GetMouseState(&mouseX, &mouseY) != 0 && !autoPlay) {
                // Polled straight from SDL, so it is still in window pixels.
                toGamePoint(mouseX, mouseY);
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
                // Floating "+N" beside the score panel: the delta this
                // judgement paid, keyed to the note's own chart time so the
                // animation freezes with the clock.
                hudState.scoreDelta = stats.lastScoreDelta;
                hudState.scoreDeltaAtSec = stats.scoreDeltaAtSec;
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
            // The digits show the raw pool (5000 stays 5000); the bar keeps the
            // 0..1 ratio. See game/Hud.hpp.
            hudState.lifeValue = stats.life;
            hudState.autoJudge = autoPlay;

            // 多人游玩: hand this window's live numbers to the room, so the other
            // windows can put them on screen (a plain store into the shared
            // page - the reader sees it on its next frame).
            if (party.active()) {
                party.setPlayingScore(hudState.score, hudState.combo, hudState.lifeRatio);
            }

            // Damage vignette state: any life drop *flashes* the corners.
            // Assign, never accumulate - a long bad streak must not build up a
            // permanent black screen (life at 0 is the only thing that keeps it
            // on, and that is handled where it is drawn).
            if (stats.life < lastSeenLife - 0.5f) {
                damageVignette = 1.0f;
            }
            lastSeenLife = stats.life;
            damageVignette = std::max(0.0f, damageVignette - frameDelta / 0.42f);

            // Debug (`--judge-frame N`): freeze the judge text on frame N of
            // its 60fps pop-in so the animation can be checked from a headless
            // screenshot, where no input can be injected.
            if (judgeAnimFrame >= 0) {
                hudState.lastJudge = game::Judge::Perfect;
                hudState.lastJudgeAtSec =
                    static_cast<float>(songTime) - static_cast<float>(judgeAnimFrame) / 60.0f;
            }

            // ----------------------------------------------------------
            // Song stage: this song's jacket projected into the stage screens
            // (game/StageBackground.cpp, ported from the upstream generator).
            // A CPU composite of the whole plate, so it is done once per
            // jacket, not per frame.
            // ----------------------------------------------------------
            if (session.entry.coverPath != stageBackgroundFor) {
                stageBackgroundFor = session.entry.coverPath;
                int plateW = 0;
                int plateH = 0;
                std::vector<std::uint8_t> plate;
                if (!session.entry.coverPath.empty()) {
                    const auto buildStart = std::chrono::steady_clock::now();
                    plate = game::buildStageBackground(baseDir + "assets/mmw/overlay/bggen/v3",
                        session.entry.coverPath, plateW, plateH);
                    const auto buildMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - buildStart)
                                             .count();
                    std::printf("[stage] %s plate %dx%d in %lld ms\n", plate.empty() ? "no" : "built",
                        plateW, plateH, static_cast<long long>(buildMs));
                    std::fflush(stdout);
                }
                if (!dumpStageBgPath.empty() && !plate.empty()) {
                    stbi_write_png(dumpStageBgPath.c_str(), plateW, plateH, 4, plate.data(), plateW * 4);
                    std::printf("[stage] wrote %s\n", dumpStageBgPath.c_str());
                    std::fflush(stdout);
                }
                renderer.setSongBackground(plate.empty() ? nullptr : plate.data(), plateW, plateH, error);
                error.clear();
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

            // ----------------------------------------------------------
            // Damage vignette: a dark corner shadow. A life loss *flashes* it
            // (decays over ~0.42s); life at 0 leaves it on for good.
            //
            // Two rules this has to obey, both learned the hard way:
            //   * it is a *flash*, not an accumulator. `damageVignette` is
            //     assigned 1 on a drop, never `+=`, so a long bad streak cannot
            //     pile up into a black screen.
            //   * it sits UNDER the HUD. Drawn on the background list *before*
            //     drawHud() below, so the score / life bar stay readable while
            //     the corners darken. Afterwards (or on the foreground list) it
            //     covered them.
            //
            // Geometry: one *full-length strip per screen edge*, each fading
            // perpendicular to its own edge. Two strips overlap in every corner,
            // and alpha-blending them there multiplies into the darkest patch -
            // that is what makes it read as a vignette.
            //
            // The old version drew one square per corner instead, which broke on
            // any window wider than it is tall: the squares only reached
            // min(w,h)*0.55 in from each side, so on a 1920x1080 window the top
            // and bottom edges had an ~880px hole right in the middle, with a
            // hard step from 50% black to nothing where each square ended
            // (measured: alpha 127 at x=594, 0 at x=614, along y=8).
            // ----------------------------------------------------------
            const float damageVig = [&]() {
                // Debug (CPSEKAI_VIGNETTE=0.85): freeze the vignette at a fixed
                // level so it can be checked from a headless screenshot - a real
                // life drop needs a player who misses, which --screenshot has no
                // way to produce.
                if (const char* forced = std::getenv("CPSEKAI_VIGNETTE")) {
                    return std::clamp(static_cast<float>(std::atof(forced)), 0.0f, 1.0f);
                }
                const float deadVignette = judgement.lifeRatio() <= 0.0f ? 1.0f : 0.0f;
                return std::max(damageVignette * 0.85f, deadVignette);
            }();
            if (damageVig > 0.004f) {
                ImDrawList* bg = ImGui::GetBackgroundDrawList();
                const float w = static_cast<float>(windowW);
                const float h = static_cast<float>(windowH);
                // Peak alpha of *one* strip. Corners blend two of them, so the
                // corner ends up at 1-(1-a)^2 - about 1.6x the edge value.
                const int a = static_cast<int>(120.0f * std::clamp(damageVig, 0.0f, 1.0f));
                const ImU32 dark = IM_COL32(0, 0, 0, a);
                const ImU32 none = IM_COL32(0, 0, 0, 0);
                // Reach of the fade, off the short side so a wide window and a
                // tall one darken by the same amount.
                const float rc = std::min(w, h) * 0.42f;
                // WARNING: the corner order of AddRectFilledMultiColor is
                // (upper-left, upper-right, **lower-right**, lower-left) - the
                // two bottom corners are NOT in reading order. Each strip below
                // only ever varies across its own axis, so the order only has to
                // be right at the two ends of that axis.
                // Top: dark at y=0, gone at y=rc.
                bg->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(w, rc), dark, dark, none, none);
                // Bottom: gone at y=h-rc, dark at y=h.
                bg->AddRectFilledMultiColor(ImVec2(0.0f, h - rc), ImVec2(w, h), none, none, dark, dark);
                // Left: dark at x=0, gone at x=rc.
                bg->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(rc, h), dark, none, none, dark);
                // Right: gone at x=w-rc, dark at x=w.
                bg->AddRectFilledMultiColor(ImVec2(w - rc, 0.0f), ImVec2(w, h), none, dark, dark, none);
            }

            if (visibility > 0.0f) {
                game::drawHud(renderer, hudState, static_cast<float>(songTime), windowW, windowH,
                    static_cast<float>(leadInSec), dumpJudgeSheet);
            }
            game::drawIntro(renderer, session.intro, outputTime, windowW, windowH);

            // 多人游玩: the other players' score / combo, and - while the host
            // has the shared clock on hold - why nothing is moving.
            if (party.active() && visibility > 0.0f) {
                const bool hostHeld = mpFollowing && mpHostPaused;
                game::drawPartyScores(party.players(), party.slot(), windowW, windowH,
                    hostHeld ? "房主已暂停" : std::string(), hostHeld);
            }

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
            // Pause button zone (right end of the life bar).
            // ----------------------------------------------------------
            // The press was already hit-tested in the event handler; act on it
            // here so the same click never also counts as a lane hit.
            if (pauseClickRequested) {
                pauseClickRequested = false;
                requestPause();
            }

            // Settings card (H key), shared with the song select state.
            drawSettingsCard();
            drawMultiInstanceAskDialog();

            // ----------------------------------------------------------
            // Pause dialog: 重试 / 放弃 / 继续演出.
            // ----------------------------------------------------------
            if (showPauseDialogShot && !pauseDialogOpen && songTime > 0.5) {
                requestPause();
            }
            // Alive flag keeps drawing while the close animation plays out.
            static bool pauseDialogAlive = false;
            if (pauseDialogOpen) {
                pauseDialogAlive = true;
            }
            // Pause dialog: every button has to be reachable from the pad.
            // The dialog is custom-drawn, so the choice is handed to
            // ui::messageDialog as a forced click (same close animation).
            if (pauseDialogChoice >= 0) {
                std::printf("[pause] pad choice %d (state=%d paused=%d dialog=%d)\n", pauseDialogChoice,
                    static_cast<int>(state), paused ? 1 : 0, pauseDialogOpen ? 1 : 0);
                std::fflush(stdout);
            }
            if (pauseDialogAlive) {
                const int action = ui::messageDialog(renderer, "##pauseDialog", "是否继续演出？",
                    {std::string("重试"), std::string("放弃"), std::string("继续演出")},
                    {false, false, true}, pauseDialogChoice);
                pauseDialogChoice = -1;
                if (action == 0) {
                    // Retry: reload the current chart from the top. 多人游玩:
                    // a shared live cannot be restarted by one window - the
                    // clock, the chart and the start instant all belong to the
                    // room - so 重试 means "re-arm the round": the host hands the
                    // room back to the lobby and re-publishes the same song, and
                    // everybody picks up their 确定 again (which is also what
                    // makes a retry reach the other windows, see the play state).
                    pauseDialogOpen = false;
                    paused = false;
                    if (party.active()) {
                        pauseDialogAlive = false;
                        party.setHostPaused(false);
                        party.setSeat(platform::PartySeatLobby);
                        party.setReady(false);
                        mpConfirmed = false;
                        party.releaseSong();
                        leaveLiveForRoom("房主重开本曲");
                    } else if (startSession(session, session.entry, renderer, audio, judgement, noteSpeed, error)) {
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
                    if (party.active()) {
                        // The room follows: the host drops back to the lobby,
                        // which is what tells every other window to stop playing
                        // (see the play state's room watch).
                        party.setHostPaused(false);
                        party.setSeat(platform::PartySeatLobby);
                        party.setReady(false);
                        mpConfirmed = false;
                        party.releaseSong();
                    }
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
                    // 多人游玩: tell the members the shared clock is running
                    // again (they hold their picture on the frozen time until
                    // then, so a host pause stops every window at once).
                    if (party.active()) {
                        party.setHostPaused(false);
                    }
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
            game::drawResult(renderer, resultData, resultElapsed, windowW, windowH,
                userSettings.uiScale);
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
            // Debug (--party-auto): press 继续 a couple of seconds in, so a
            // headless round also covers the walk back to the song select (and
            // the room being re-armed for the next song).
            if (partyAutoGiven && !partyAutoContinued && resultElapsed >= 2.0f) {
                partyAutoContinued = true;
                resultContinueRequested = true;
            }
            // Headless check: dump the settled result screen.
            if (!screenshotPath.empty() && !wantScreenshot && resultElapsed >= 2.6f) {
                wantScreenshot = true;
            }
        }

        // ------------------------------------------------------------------
        // ELUA - the license / disclaimer card.
        //
        // Shown over the song select the first time a profile comes up (and
        // again for a profile that has never ticked the box). It sits *outside*
        // the AppState::Select branch above so the result screen's 继续 cannot
        // reveal it mid-transition, but it is still only drawn while the song
        // select is on screen - that is what "主界面出现时" means, and it keeps
        // the card from landing on top of a running live.
        //
        // Nothing here gates playing: the checkbox is the record, the buttons
        // just dismiss the card. It is deliberately not a 我同意 / 我不同意 pair
        // (this program has no EULA to agree to - the license is the AGPL, and
        // it comes with the source), so the card states facts instead.
        // ------------------------------------------------------------------
        static bool eulaAlive = false;
        // 这次运行里已经关掉过了。以前没有这个标志：eulaAlive 每帧被
        // `state == Select && !eulaAccepted` 重新置真，而「知道了」按未勾选的
        // 复选框写入 eulaAccepted = false —— 于是关掉的下一帧又被打开，
        // 无限弹。「不再提示」的复选框是**下次启动**要不要再看到的记录，
        // 不是「能不能关掉这一张」的开关，两者必须分开。
        static bool eulaDismissedThisRun = false;
        if (state == AppState::Select && !userSettings.eulaAccepted && !eulaDismissedThisRun) {
            eulaAlive = true;
        }
        if (eulaAlive) {
            if (state != AppState::Select) {
                // The player got into a song while the card was up (the host
                // started the round, or the list was confirmed). Drop it
                // without animating: it may not reappear over the live.
                eulaAlive = false;
            } else {
                // Seeded from the profile each time the card is (re)raised, so
                // the box shows what is actually stored.
                static bool eulaNoShow = false;
                static bool eulaSeeded = false;
                if (!eulaSeeded) {
                    eulaSeeded = true;
                    eulaNoShow = userSettings.eulaAccepted;
                }
                const int eulaAction = ui::eulaDialog(renderer, "##eula", "关于本软件",
                    {
                        "CppSekai 是免费、开源、非营利的同人练习工具（AGPL-3.0）。"
                            "它与 SEGA、Colorful Palette 以及《世界计划》官方没有任何关系。",
                        "本程序不含官方游戏的任何代码、音频、曲绘或谱面；谱面与素材需由使用者"
                            "自行下载，仅供本地学习与练习使用。",
                        "请勿将本程序用于任何商业用途，也请勿传播你无权传播的素材。"
                            "一切权利归各自权利人所有。",
                        "程序按现状提供，不附带任何担保；本机数据（成绩、设置）只保存在本地，"
                            "不会上传到任何服务器。",
                    },
                    "以后不再显示", &eulaNoShow, {std::string("知道了")}, {true}, -1);
                // Only act once the close animation is over (-2) or a button was
                // pressed (>= 0). Acting on -3 (still closing) would relatch the
                // flag mid-animation and the card would pop back up.
                if (eulaAction >= 0 || eulaAction == -2) {
                    // 复选框是"下次还看不看"的记录；不管勾没勾，这一次都关掉。
                    if (userSettings.eulaAccepted != eulaNoShow) {
                        userSettings.eulaAccepted = eulaNoShow;
                        persistUserData();
                    }
                    std::printf("[eula] dismissed (noShow=%d)\n", eulaNoShow ? 1 : 0);
                    std::fflush(stdout);
                    eulaAlive = false;
                    eulaDismissedThisRun = true;
                    eulaSeeded = false; // next raise re-reads the profile
                }
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

        // --- Confirm flash: expand -> hold (the session loads here) -> fade -----
        // Runs in every state: it starts in Select and has to keep animating while
        // the intro card is already up.
        if (confirmFlashActive) {
            // Clamped: the loading stall lands inside one frame's delta, and the
            // timeline has to keep its shape across it (the frame the load happens
            // in simply takes a little longer, and the white covers it).
            confirmFlashTime += std::min(frameDelta, 0.05f);
            if (confirmStartPending && confirmFlashTime >= kConfirmExpand) {
                confirmStartPending = false;
                if (startSession(session, confirmPendingEntry, renderer, audio, judgement, noteSpeed,
                        error)) {
                    loadedCoverPath = session.entry.coverPath;
                    announceTrack();
                    touches.clear();
                    std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                    lanePress.fill(0.0f);
                    paused = false;
                    resultScheduled = false;
                    resultData = game::ResultData{};
                    songEndBlackout = 0.0f;
                    state = AppState::Play;
                    beginSessionClock();
                } else {
                    std::fprintf(stderr, "%s\n", error.c_str());
                    error.clear();
                    confirmFlashActive = false; // nothing to reveal: drop the white
                }
            }
            if (!confirmStartPending
                && confirmFlashTime >= kConfirmExpand + kConfirmHold + kConfirmFade) {
                confirmFlashActive = false;
            }
        }

        // --- Transition overlays (foreground: above every screen) ---------------
        {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            const float w = static_cast<float>(windowW);
            const float h = static_cast<float>(windowH);
            // Song end: the result screen takes the black over and lifts it.
            if (state == AppState::Result) {
                songEndBlackout = std::max(0.0f, songEndBlackout - frameDelta / 0.6f);
            }
            if (songEndBlackout > 0.002f) {
                fg->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(w, h),
                    IM_COL32(0, 0, 0, static_cast<int>(std::clamp(songEndBlackout, 0.0f, 1.0f) * 255.0f)));
            }
            // 多人游玩 charge: no countdown here any more. The room starts as
            // soon as every window's chart is decoded (see the host's arming
            // block), so what is left of the wait is the load itself - about a
            // second, covered by the confirm burst. The 5.8s "即将开始 5.8" that
            // used to sit here was a fixed wait dressed up as a countdown.
            if (confirmFlashActive) {
                const float t = confirmFlashTime;
                // Envelope: soft onset -> plateau (the session loads inside it) ->
                // soft tail. The attack is the half of "it looks hard" the old
                // version missed: it jumped to full brightness on frame 0.
                float env = 1.0f;
                if (t < kConfirmAttack) {
                    const float a = t / kConfirmAttack;
                    env = a * a * (3.0f - 2.0f * a);
                }
                // Cover grows with a smoothstep (no hard arrival), and the
                // brightness envelope never reaches pure white.
                float cover = 1.0f;
                if (t < kConfirmExpand) {
                    const float u = t / kConfirmExpand;
                    cover = u * u * (3.0f - 2.0f * u);
                }
                if (t > kConfirmExpand + kConfirmHold) {
                    const float f =
                        std::clamp((t - kConfirmExpand - kConfirmHold) / kConfirmFade, 0.0f, 1.0f);
                    const float inv = 1.0f - f;
                    env *= inv * inv * (3.0f - 2.0f * inv); // smoothstep out: slow, soft tail
                }
                const float alpha = kConfirmPeak * env;
                if (alpha > 0.002f) {
                    const ImVec2 o = confirmFlashOrigin;
                    // Distance the light has to travel to leave no pixel dark.
                    // The radius is *not* that distance: rInner (the fully lit
                    // core) has to pass it, otherwise the far corners sit inside
                    // the falloff and the loading shows through the white. That
                    // was the "did not fill the screen" half of the bug.
                    const float farX = std::max(o.x, w - o.x);
                    const float farY = std::max(o.y, h - o.y);
                    const float want = std::sqrt(farX * farX + farY * farY);
                    const float rOuter = want * (0.85f + 0.90f * cover);
                    const float rInner = rOuter * 0.62f; // -> 1.085 * want when cover = 1
                    // Rays: the "light" read. Each one is a degenerate quad -
                    // two coincident vertices in the button, two at the tip -
                    // so ImGui can blend it from bright to fully transparent
                    // along its length. (The old version was an opaque-ish
                    // triangle at a flat 50% alpha, and that flat, hard-edged
                    // wedge is exactly what looked wrong.) They also step back
                    // as the disc comes up, so they never fight with it.
                    const float rayFade = std::clamp(1.0f - cover * 1.3f, 0.0f, 1.0f);
                    if (rayFade > 0.01f) {
                        const int rays = 12;
                        const int rayA = static_cast<int>(alpha * rayFade * 130.0f);
                        const ImU32 colIn = IM_COL32(255, 255, 255, rayA);
                        const ImU32 colOut = IM_COL32(255, 255, 255, 0);
                        // ImGui 1.92 has no gradient triangle helper, so the
                        // ray is written straight into the vertex buffer: two
                        // coincident centre vertices (bright) + two tip
                        // vertices (transparent) = one soft wedge.
                        const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
                        for (int i = 0; i < rays; ++i) {
                            const float ang =
                                (6.2831853f / static_cast<float>(rays)) * static_cast<float>(i) + 0.21f;
                            const float len = rOuter * (0.45f + 0.55f * cover);
                            const float spread = want * (0.008f + 0.006f * static_cast<float>(i % 3));
                            const ImVec2 dir(std::cos(ang), std::sin(ang));
                            const ImVec2 side(-dir.y, dir.x);
                            const ImVec2 tipA(o.x + dir.x * len + side.x * spread,
                                o.y + dir.y * len + side.y * spread);
                            const ImVec2 tipB(o.x + dir.x * len - side.x * spread,
                                o.y + dir.y * len - side.y * spread);
                            fg->PrimReserve(6, 4);
                            const ImDrawIdx base = static_cast<ImDrawIdx>(fg->VtxBuffer.Size);
                            fg->PrimWriteVtx(o, uv, colIn);
                            fg->PrimWriteVtx(o, uv, colIn);
                            fg->PrimWriteVtx(tipA, uv, colOut);
                            fg->PrimWriteVtx(tipB, uv, colOut);
                            fg->PrimWriteIdx(base);
                            fg->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
                            fg->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
                            fg->PrimWriteIdx(base);
                            fg->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
                            fg->PrimWriteIdx(static_cast<ImDrawIdx>(base + 3));
                        }
                    }
                    // The disc that ends up covering the screen. It is built out
                    // of concentric filled circles, outermost first, and each
                    // ring's alpha is *solved* so the accumulated coverage equals
                    // the wanted profile at that radius (coverage accumulates as
                    // 1 - prod(1 - a_i), which is what makes the stack a smooth
                    // gradient instead of a set of visible steps). 16 rings at a
                    // constant 0.18 alpha - the old version - left the far corner
                    // at 29% and a visible rim at the outer edge; that is exactly
                    // what "hard" and "not full screen" meant.
                    constexpr int kRings = 40;
                    const float band = std::max(rOuter - rInner, 1e-3f);
                    float comp = 0.0f; // coverage already laid down
                    for (int i = 0; i < kRings; ++i) {
                        const float u = static_cast<float>(i) / static_cast<float>(kRings - 1);
                        const float r = rOuter + (rInner - rOuter) * u;
                        if (r < 0.75f) {
                            break;
                        }
                        const float d = std::clamp((rOuter - r) / band, 0.0f, 1.0f);
                        const float target = alpha * d * d * (3.0f - 2.0f * d);
                        if (target <= comp + 1e-4f) {
                            continue;
                        }
                        const float add = (target - comp) / std::max(1.0f - comp, 1e-3f);
                        comp += add * (1.0f - comp);
                        const int a8 = static_cast<int>(std::clamp(add, 0.0f, 1.0f) * 255.0f + 0.5f);
                        if (a8 <= 0) {
                            continue;
                        }
                        const int seg = std::clamp(static_cast<int>(r * 0.35f), 16, 192);
                        fg->AddCircleFilled(o, r, IM_COL32(255, 255, 255, a8), seg);
                    }
                }
            }
        }

        // One sound per frame: the widgets queued their requests while the
        // frame was drawn, resolve them now (a dialog opening swallows the
        // click that opened it).
        ui::flushSe();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        // Presents the offscreen buffer into the window (letterboxed). No-op
        // when the render mode draws straight to the window.
        renderer.presentFrame();

        // Captured after the ImGui pass so the HUD / intro card / song list
        // are part of the frame.
        if (wantScreenshot) {
            wantScreenshot = false;
            {
                const auto& st = judgement.stats();
                std::printf("[stats] perfect=%d great=%d good=%d bad=%d miss=%d combo=%d maxCombo=%d tails=%d breaks=%d score=%.0f life=%.0f (%.1f%%)\n",
                    st.perfect, st.great, st.good, st.bad, st.miss, st.combo, st.maxCombo, st.holdTails, st.holdBreaks,
                    st.score, st.life, 100.0f * judgement.lifeRatio());
            }
            saveScreenshot();
            running = false;
        }

        // Frame pacing: vsync quantizes presentation to refresh intervals, so
        // on a 60 Hz panel any cap above 60 is physically impossible and the
        // limiter's sleep just jitters around the vblank (fps "乱跳"). Past
        // the refresh rate we drop vsync and pace purely with the limiter.
        const bool wantVsync =
            !dragFramePacing && !(fpsLimitLive > 0 && fpsLimitLive > displayRefreshHz);
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
    } // the one-shot wrapper loop: `continue` / `break` in the body land here
    }; // end runFrame

#ifdef _WIN32
    // ------------------------------------------------------------------
    // Serving frames while Windows has our message pump parked.
    //
    // A modal move/size loop (title-bar drag, border resize, maximize) runs inside
    // DefWindowProc and parks our pump: SDL_PollEvent does not return until the
    // mouse comes up, so not a single frame runs - while the audio and the chart
    // clock that rides on it keep going.
    //
    // SDL_SetWindowsMessageHook cannot help here, and that is the whole reason
    // this is a window subclass instead: SDL calls that hook from WIN_PumpEvents
    // (its own message pump), *not* from the window procedure, so during the
    // modal loop it is never called at all. Measured 2026-09-19 on Windows 10
    // 22H2 and Windows 11: the hook saw no WM_ENTERSIZEMOVE and no WM_MOVING while
    // a single frame spanned the whole drag (2264 ms for a 2.1 s drag).
    //
    // What does run in there is the window procedure - the modal loop sends
    // WM_ENTERSIZEMOVE / WM_MOVING / WM_SIZING to the window and posts WM_TIMER -
    // so the window procedure is what gets subclassed here. Everything is
    // forwarded to SDL's procedure untouched; only the drag messages turn into
    // frames.
    //
    // The state lives in a window property (SDL keeps its window data the same
    // way), so there is no module-level mutable state and nothing can outlive the
    // window. CPSEKAI_MSG_LOG=1 dumps every message the subclass sees.
    constexpr UINT_PTR kDragTimerId = 0x4353; // 'CS'
    constexpr UINT kDragFrameMs = 16;         // WM_TIMER period while dragging
    // Frame rate while dragging: every frame served from inside the modal loop
    // blocks it for its whole duration, so a 60 fps animation leaves the loop no
    // time to move the window at all - that is what "拖动很卡" was. ~33 fps with
    // vsync off (below) is the balance: the picture keeps moving and the loop
    // keeps ~3/4 of the wall clock to follow the cursor.
    constexpr Uint64 kDragMinGapMs = 30;      // never serve two frames faster than this
    struct SubclassState
    {
        WNDPROC chain = nullptr;             // SDL's procedure; we forward to it
        decltype(runFrame)* frame = nullptr; // one frame, from main()
        int depth = 0;                       // frames running right now (0 or 1)
        int fromTimer = 0;
        int fromMoving = 0;
        int fromSizing = 0;
        Uint64 lastServedMs = 0;
        bool logMessages = false;
        bool* dragPacing = nullptr; // vsync off while a drag is in progress
        int* noFrame = nullptr;     // glassMode 2: remove the non-client area
        // Frames are only ever served **during a real modal move/size loop**, which
        // is what WM_ENTERSIZEMOVE marks and WM_EXITSIZEMOVE ends. Without this gate
        // any WM_MOVE/WM_SIZE reaching the window would run the whole frame body
        // *from inside itself* - and one of the ways to get a WM_SIZE without a drag
        // is our own SetWindowPos from the settings card, so switching 玻璃实现
        // re-entered the frame in the middle of an ImGui frame. ImGui is not
        // reentrant; that is the crash this counter used to catch (see servedOutside).
        bool dragging = false;
        int servedOutside = 0;
    };
    SubclassState subclass;
    subclass.frame = &runFrame;
    subclass.dragPacing = &dragFramePacing;
    subclass.noFrame = &noFrameMode;
    subclass.logMessages = std::getenv("CPSEKAI_MSG_LOG") != nullptr;
    SDL_SysWMinfo mainWmi;
    SDL_VERSION(&mainWmi.version);
    if (SDL_GetWindowWMInfo(window, &mainWmi) && mainWmi.subsystem == SDL_SYSWM_WINDOWS) {
        HWND gameWindow = mainWmi.info.win.window;
        subclass.chain = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gameWindow, GWLP_WNDPROC));
        SetPropW(gameWindow, L"CppSekaiSubclassState", reinterpret_cast<HANDLE>(&subclass));
        SetWindowLongPtrW(gameWindow, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(static_cast<WNDPROC>(
                [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
                    SubclassState* st =
                        static_cast<SubclassState*>(GetPropW(hwnd, L"CppSekaiSubclassState"));
                    if (st == nullptr) {
                        return DefWindowProcW(hwnd, message, wParam, lParam);
                    }
                    if (st->logMessages) {
                        std::printf("[msg] 0x%04X wparam=%llu lparam=%lld\n", message,
                            static_cast<unsigned long long>(wParam),
                            static_cast<long long>(lParam));
                        std::fflush(stdout);
                    }
                    // ------------------------------------------------------
                    // 玻璃实现 = 「自绘无框」: the recipe from Microsoft's
                    // "Custom Window Frame Using DWM" - hand the whole window to
                    // the client (WM_NCCALCSIZE -> 0) and then put moving and
                    // resizing back by hand (WM_NCHITTEST), because "a side effect
                    // of removing the standard frame is the loss of the default
                    // resizing and moving behavior". Both messages do reach a window
                    // whose non-client area is zero (the doc says so for the hit
                    // test), and they are the only way to keep a frameless window
                    // draggable without giving up the native drag loop, Aero Snap
                    // and the DWM shadow.
                    // ------------------------------------------------------
                    if (st->noFrame != nullptr && *st->noFrame != 0) {
                        if (message == WM_NCCALCSIZE && wParam != 0) {
                            // A zoomed window whose client area is the whole window
                            // would reach over the taskbar, so maximized keeps the
                            // normal frame (and the setting is documented as a
                            // windowed-mode thing anyway).
                            if (IsZoomed(hwnd) || IsIconic(hwnd)) {
                                return CallWindowProcW(st->chain, hwnd, message, wParam, lParam);
                            }
                            return 0; // every pixel of the window is client area
                        }
                        if (message == WM_NCHITTEST) {
                            // lParam is a screen-space point in a packed pair of
                            // 16-bit signed values; the LOWORD/HIWORD macros must
                            // not be used raw (multi-monitor coordinates go
                            // negative), and windowsx.h is not in the toolchain, so
                            // decode them here.
                            const int screenX = static_cast<int>(static_cast<short>(LOWORD(lParam)));
                            const int screenY = static_cast<int>(static_cast<short>(HIWORD(lParam)));
                            RECT wr{};
                            GetWindowRect(hwnd, &wr);
                            const int x = screenX - wr.left;
                            const int y = screenY - wr.top;
                            const int w = wr.right - wr.left;
                            const int h = wr.bottom - wr.top;
                            // Same numbers Windows itself uses for the grab area, so
                            // grabbing an edge feels like it does on a framed window.
                            int edge = GetSystemMetrics(SM_CXSIZEFRAME)
                                + GetSystemMetrics(SM_CXPADDEDBORDER);
                            if (edge < 4) {
                                edge = 4;
                            }
                            const bool left = x < edge;
                            const bool right = x >= w - edge;
                            const bool top = y < edge;
                            const bool bottom = y >= h - edge;
                            if (top && left) return HTTOPLEFT;
                            if (top && right) return HTTOPRIGHT;
                            if (bottom && left) return HTBOTTOMLEFT;
                            if (bottom && right) return HTBOTTOMRIGHT;
                            if (left) return HTLEFT;
                            if (right) return HTRIGHT;
                            if (top) return HTTOP;
                            if (bottom) return HTBOTTOM;
                            // No caption exists any more, so the drag area is ours to
                            // define: the top strip, as tall as a real caption. Below
                            // it the window stays a normal client area.
                            const int caption = std::max(22, static_cast<int>(GetSystemMetrics(SM_CYCAPTION)));
                            if (y < caption) return HTCAPTION;
                            return HTCLIENT;
                        }
                    }
                    if (message == WM_ENTERSIZEMOVE) {
                        st->dragging = true;
                        *st->dragPacing = true;
                        st->fromTimer = 0;
                        st->fromMoving = 0;
                        st->fromSizing = 0;
                        st->lastServedMs = 0;
                        SetTimer(hwnd, kDragTimerId, kDragFrameMs, nullptr);
                        std::printf("[window] WM_ENTERSIZEMOVE: pump parked, feeding frames from "
                                    "WM_MOVING/WM_SIZING + a %u ms WM_TIMER\n",
                            kDragFrameMs);
                        std::fflush(stdout);
                    } else if (message == WM_EXITSIZEMOVE) {
                        st->dragging = false;
                        *st->dragPacing = false;
                        KillTimer(hwnd, kDragTimerId);
                        std::printf("[window] WM_EXITSIZEMOVE: %d frame(s) served during the drag "
                                    "(moving=%d sizing=%d timer=%d)\n",
                            st->fromMoving + st->fromSizing + st->fromTimer, st->fromMoving,
                            st->fromSizing, st->fromTimer);
                        std::fflush(stdout);
                    } else if (st->depth == 0) {
                        // WM_MOVING/WM_SIZING are only ever *sent* by the system's
                        // modal move/size loop; WM_MOVE/WM_SIZE are not (SetWindowPos
                        // and SDL_SetWindowSize produce those), so they must never
                        // ask for a frame - doing that used to nest a whole frame
                        // inside a frame. They are counted instead, as evidence that
                        // the machine does deliver them here (Windows 7 does, Windows
                        // 11 apparently does not).
                        int* counter = nullptr;
                        if (message == WM_MOVING) {
                            counter = &st->fromMoving;
                        } else if (message == WM_SIZING) {
                            counter = &st->fromSizing;
                        } else if (message == WM_MOVE || message == WM_SIZE || message == WM_NCCALCSIZE
                            || message == WM_WINDOWPOSCHANGED) {
                            if (!st->dragging && st->servedOutside < 8) {
                                ++st->servedOutside;
                                std::printf("[window] %s outside a drag (frameless=%d) - not served\n",
                                    message == WM_MOVE ? "WM_MOVE" :
                                    message == WM_SIZE ? "WM_SIZE" :
                                    message == WM_NCCALCSIZE ? "WM_NCCALCSIZE" : "WM_WINDOWPOSCHANGED",
                                    *st->noFrame);
                                std::fflush(stdout);
                            }
                            return CallWindowProcW(st->chain, hwnd, message, wParam, lParam);
                        } else if (message == WM_TIMER && wParam == kDragTimerId) {
                            // The modal loop can be entered and left without our
                            // seeing WM_EXITSIZEMOVE (the release lands in the
                            // middle of a frame we served, and SDL's pump eats it),
                            // which used to leave the timer running and vsync off
                            // forever. The button state is the ground truth, so
                            // check it here and clean up if the drag is over.
                            if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
                                KillTimer(hwnd, kDragTimerId);
                                st->dragging = false;
                                *st->dragPacing = false;
                                std::printf("[window] drag over (button released): %d frame(s) served "
                                            "(moving=%d sizing=%d timer=%d)\n",
                                    st->fromMoving + st->fromSizing + st->fromTimer, st->fromMoving,
                                    st->fromSizing, st->fromTimer);
                                std::fflush(stdout);
                                return CallWindowProcW(st->chain, hwnd, message, wParam, lParam);
                            }
                            counter = &st->fromTimer;
                        }
                        if (counter != nullptr && !st->dragging) {
                            return CallWindowProcW(st->chain, hwnd, message, wParam, lParam);
                        }
                        if (counter != nullptr) {
                            // The frame served here pumps messages itself (the next WM_TIMER can
                            // land in the middle of it), so only one nested frame is ever allowed;
                            // the time floor keeps a burst of mouse messages from asking for a
                            // frame each and turning the drag into 16 ms steps.
                            const Uint64 nowMs = SDL_GetTicks64();
                            if (st->lastServedMs == 0 || nowMs - st->lastServedMs >= kDragMinGapMs) {
                                st->lastServedMs = nowMs;
                                ++*counter;
                                ++st->depth;
                                (*st->frame)();
                                --st->depth;
                                const int total = st->fromMoving + st->fromSizing + st->fromTimer;
                                if (total % 30 == 0) {
                                    std::printf(
                                        "[window] drag frames: %d (moving=%d sizing=%d timer=%d)\n",
                                        total, st->fromMoving, st->fromSizing, st->fromTimer);
                                    std::fflush(stdout);
                                }
                            }
                        }
                    }
                    return CallWindowProcW(st->chain, hwnd, message, wParam, lParam);
                })));
        std::printf("[window] window procedure subclassed for drag frames\n");
        std::fflush(stdout);
        if (noFrameMode != 0) {
            // The window was created long before this subclass existed, so the
            // WM_NCCALCSIZE of its creation went to SDL's procedure (with a normal
            // frame as the answer). Nothing recomputes the frame on its own, so ask
            // for one now that we are in the chain - without this the setting looks
            // like it did nothing at all.
            RECT before{};
            GetClientRect(gameWindow, &before);
            SetWindowPos(gameWindow, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            RECT after{};
            GetClientRect(gameWindow, &after);
            std::printf("[window] frameless: client %ldx%ld -> %ldx%ld\n",
                before.right, before.bottom, after.right, after.bottom);
            std::fflush(stdout);
        }
    }
#endif

    while (running) {
        runFrame();
    }

    // Headless checks (--screenshot) must not touch the player's data file.
    if (screenshotPath.empty()) {
        persistUserData();
    }

    // Leave the 多人游玩 room before anything else: the other windows see the
    // seat go and stop waiting for this one (a crash would leave them to time
    // it out, see platform/Party.cpp).
    party.shutdown();

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
