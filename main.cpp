// CppSekai - Project SEKAI style SUS chart player (Windows native)
// Derived from sekai-mmw-preview-web (AGPL-3.0), which is based on
// MikuMikuWorld (MIT). See README.md for details.
// We provide our own console main(); tell SDL not to redefine it as SDL_main.
#define SDL_MAIN_HANDLED
#include <SDL.h>
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
#include "game/SongSelect.hpp"
#include "game/Ui.hpp"

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
        float lanePos = 0.0f;
        int laneIndex = 0;
        float lastWorldY = 0.0f;
        double lastMoveTimeSec = 0.0;
        bool flicked = false;
    };

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

    void printUsage()
    {
        std::printf(
            "CppSekai - Project SEKAI style SUS chart player\n"
            "Usage: cppsekai [--sus <file.sus>] [--bgm <audio>] [--charts <dir>]\n"
            "                [--offset <sec>] [--filler <sec>] [--auto] [--speed <1-12>]\n"
            "                [--se-volume <0-1>] [--lead-in <sec>] [--cover <image>]\n"
            "                [--screenshot <png>] [--pjsk-font]\n"
            "                [--title <text>] [--lyricist <text>] [--composer <text>]\n"
            "                [--arranger <text>] [--vocal <text>] [--difficulty <text>]\n"
            "                [--width <px>] [--height <px>] [--window <mode>] [--fps <n>]\n\n"
            "No --sus: opens the song select screen (scans --charts, then charts/ next\n"
            "to the exe, then the charts/ of the parent folder).\n"
            "--filler: seconds of silence at the head of the BGM (auto-detected when\n"
            "          omitted). --offset: manual fine tune in seconds.\n"
            "--pjsk-font: use the bundled pjsk fonts instead of the system UI font.\n"
            "--window: borderless (default) | windowed | fullscreen. --width/--height:\n"
            "          window size (default 1280x720).\n"
            "--fps: cap the frame rate in addition to vsync (0 = vsync only). Lower\n"
            "       values (e.g. 30/60) reduce GPU/CPU load and power draw.\n"
            "Keyboard: Z S X D C V G B H N J M = 12 lanes\n"
            "          SPACE = pause, F = fullscreen, H = debug panel, ESC = back/quit\n"
            "Touch   : multi-touch lanes, swipe up for flicks\n");
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
        audio.playSe(seForKind(stats.lastHitKind, stats.lastJudgeCritical), seVolume * (stats.lastJudge == game::Judge::Good ? 0.5f : 1.0f));
    }

    enum class AppState
    {
        Select,
        Play,
    };

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
    };

    bool startSession(Session& session, const game::ChartEntry& entry, platform::Renderer& renderer,
        platform::AudioEngine& audio, game::JudgementEngine& judgement, float noteSpeed, std::string& error)
    {
        audio.stopMusic();
        judgement.reset();

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
    double leadIn = 6.0; // intro card (4s) + playfield fade-in, then the music
    bool dumpJudgeSheet = false;
    bool showPauseDialogShot = false; // headless check: force the pause dialog open
    int winWidth = 1280;
    int winHeight = 720;
    int windowMode = 0; // 0=borderless 1=windowed 2=fullscreen(desktop)
    int fpsLimit = 0;   // extra frame cap on top of vsync; 0 = vsync only

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
        } else if (arg == "--height" && i + 1 < utf8Argc) {
            winHeight = std::atoi(utf8Argv[++i]);
        } else if (arg == "--window" && i + 1 < utf8Argc) {
            const std::string mode = utf8Argv[++i];
            if (mode == "windowed") {
                windowMode = 1;
            } else if (mode == "fullscreen") {
                windowMode = 2;
            } else {
                windowMode = 0; // borderless
            }
        } else if (arg == "--fps" && i + 1 < utf8Argc) {
            fpsLimit = std::atoi(utf8Argv[++i]);
        } else if (arg == "--pjsk-font") {
            useSystemFont = false;
        } else if (arg == "--auto") {
            autoPlay = true;
        } else if (arg == "--speed" && i + 1 < utf8Argc) {
            noteSpeed = static_cast<float>(std::atof(utf8Argv[++i]));
        } else if (arg == "--se-volume" && i + 1 < utf8Argc) {
            seVolume = static_cast<float>(std::atof(utf8Argv[++i]));
        } else if (arg == "--screenshot" && i + 1 < utf8Argc) {
            screenshotPath = utf8Argv[++i];
        } else if (arg == "--screenshot-time" && i + 1 < utf8Argc) {
            screenshotTimeSec = std::atof(utf8Argv[++i]);
        } else if (arg == "--lead-in" && i + 1 < utf8Argc) {
            leadIn = std::atof(utf8Argv[++i]);
        } else if (arg == "--judge-sheet") {
            dumpJudgeSheet = true;
        } else if (arg == "--show-pause-dialog") {
            showPauseDialogShot = true;
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
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int windowW = std::max(320, winWidth);
    int windowH = std::max(240, winHeight);
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (windowMode != 1) {
        windowFlags |= SDL_WINDOW_BORDERLESS; // borderless windowed + fullscreen(desktop) both hide the frame
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
    if (windowMode == 2) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    std::printf("[window] %dx%d mode=%s\n", windowW, windowH,
        windowMode == 0 ? "borderless" : windowMode == 1 ? "windowed" : "fullscreen");
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
        std::fprintf(stderr, "OpenGL 3.3 core unavailable: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);

    std::string error;
    platform::Renderer renderer;

    // Resolve bundled assets relative to the executable, not the CWD.
    std::string baseDir;
    {
        char* basePath = SDL_GetBasePath();
        if (basePath != nullptr) {
            baseDir = basePath;
            SDL_free(basePath);
        }
    }
    const std::string assetDir = baseDir + "assets\\mmw";
    const std::string overlayDir = baseDir + "assets\\mmw\\overlay";
    const std::string fontDir = baseDir + "assets\\mmw\\font";
    const std::string seDir = baseDir + "assets\\se";
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
    if (!renderer.loadAssets(assetDir, error)) {
        std::fprintf(stderr, "asset load failed: %s\n", error.c_str());
        return 1;
    }
    // Autoplay keeps the core's own note-hit effect timeline; player mode
    // uses judgement-driven effects instead.
    renderer.setDrawCoreEffects(autoPlay);

    if (!renderer.loadHud(overlayDir, error)) {
        std::fprintf(stderr, "warning: HUD load failed: %s\n", error.c_str());
        error.clear();
    }
    // Dialog close X (dark cross on transparent, assets/mmw/ui/close.png).
    if (const platform::Renderer::HudSprite* closeSprite = renderer.hud("ui_close"); closeSprite != nullptr && closeSprite->id != 0) {
        ui::setCloseTexture(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(closeSprite->id)));
    }

    // ------------------------------------------------------------------
    // Chart core
    // ------------------------------------------------------------------
    core_api::init();
    core_api::resize(windowW, windowH, 1.0f);

    // ------------------------------------------------------------------
    // Audio
    // ------------------------------------------------------------------
    platform::AudioEngine audio;
    if (!audio.init(error)) {
        std::fprintf(stderr, "audio init failed: %s\n", error.c_str());
        return 1;
    }
    if (!audio.loadSe(seDir, error)) {
        std::fprintf(stderr, "warning: SE load failed: %s\n", error.c_str());
        error.clear();
    }

    // ------------------------------------------------------------------
    // Judgement
    // ------------------------------------------------------------------
    game::JudgementEngine judgement;

    // ------------------------------------------------------------------
    // ImGui
    // ------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    game::loadIntroFonts(fontDir, useSystemFont);

    // ------------------------------------------------------------------
    // Windows system media integration (SMTC + taskbar progress)
    // ------------------------------------------------------------------
    platform::SystemMedia systemMedia;
    systemMedia.init(window);

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
    std::printf("[select] %d chart(s) under %s\n", static_cast<int>(entries.size()), chartsDir.c_str());
    if (entries.empty()) {
        std::printf("[select] no .sus found. Looked in:\n");
        for (const std::string& candidate : chartCandidates) {
            std::printf("[select]   %s\n", candidate.c_str());
        }
    }
    int selected = entries.empty() ? -1 : 0;
    std::string loadedCoverPath;

    AppState state = susPath.empty() ? AppState::Select : AppState::Play;
    Session session;
    bool beginSessionClockPending = false;

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
        systemMedia.setTrack(session.intro.title, session.entry.artist, trackDurationSec);
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
    bool showDebug = true;
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

    // HUD / hit feedback state (shared with the input handlers below).
    game::HudState hudState;
    std::vector<game::HitFx> hitEffects;
    float lastSeenJudgeTime = -100.0f;
    bool wantScreenshot = false;

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
                    }
                    break;
                case SDL_KEYDOWN: {
                    if (event.key.repeat != 0) {
                        break;
                    }
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        escapePressed = true;
                    } else if (event.key.keysym.sym == SDLK_f) {
                        fullscreen = !fullscreen;
                        SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        if (!fullscreen) {
                            // Restore whatever chrome the current window mode uses.
                            SDL_SetWindowBordered(window, windowMode == 1 ? SDL_TRUE : SDL_FALSE);
                        }
                    } else if (event.key.keysym.sym == SDLK_h) {
                        showDebug = !showDebug;
                    } else if (state == AppState::Play && event.key.keysym.sym == SDLK_SPACE && !autoPlay) {
                        // Space opens the pause dialog (same as the HUD button).
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
                                // Feedback even when nothing is there to hit.
                                hitEffects.push_back(game::HitFx{keyLanePos(lane), 0.0f, 0.4f});
                                const game::Judge result = judgement.tap(keyLanePos(lane), static_cast<float>(songTime), false, 0.5f);
                                if (result != game::Judge::None) {
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
                    if (autoPlay || paused || state != AppState::Play) {
                        break;
                    }
                    TouchTrack track;
                    track.fingerId = event.tfinger.fingerId;
                    const float clipX = (event.tfinger.x * 2.0f) - 1.0f;
                    const float clipY = 1.0f - (event.tfinger.y * 2.0f);
                    const float worldY = renderer.clipToWorldY(clipY);
                    // Undo the fake perspective: screen x = laneX * worldY.
                    const float lanePos = std::abs(worldY) > 0.08f
                        ? renderer.clipToWorldX(clipX) / worldY
                        : renderer.clipToWorldX(clipX);
                    track.lanePos = lanePos;
                    track.laneIndex = laneIndexFromPos(lanePos);
                    track.lastWorldY = worldY;
                    track.lastMoveTimeSec = SDL_GetTicks() / 1000.0;
                    touches.push_back(track);
                    lanePress[static_cast<size_t>(track.laneIndex)] = 1.0f;
                    hitEffects.push_back(game::HitFx{lanePos, 0.0f, 0.4f});
                    const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
                    const game::Judge result = judgement.tap(track.lanePos, static_cast<float>(songTime), false, 0.8f);
                    if (result != game::Judge::None) {
                        playHitSe(audio, judgement, seVolume);
                    }
                    break;
                }
                case SDL_FINGERMOTION: {
                    if (autoPlay || paused || state != AppState::Play) {
                        break;
                    }
                    const float clipY = 1.0f - (event.tfinger.y * 2.0f);
                    const double now = SDL_GetTicks() / 1000.0;
                    const float worldY = renderer.clipToWorldY(clipY);
                    for (auto& track : touches) {
                        if (track.fingerId != event.tfinger.fingerId) {
                            continue;
                        }
                        const float delta = worldY - track.lastWorldY;
                        const double dt = now - track.lastMoveTimeSec;
                        // Upward swipe in screen space = world y increasing.
                        if (dt > 0.001 && delta / dt > 1.0 && !track.flicked) {
                            track.flicked = true;
                            const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
                            const game::Judge result = judgement.flick(track.lanePos, static_cast<float>(songTime), 0.8f);
                            if (result != game::Judge::None) {
                                playHitSe(audio, judgement, seVolume);
                            }
                        }
                        track.lastWorldY = worldY;
                        track.lastMoveTimeSec = now;
                    }
                    break;
                }
                case SDL_FINGERUP: {
                    touches.erase(std::remove_if(touches.begin(), touches.end(),
                                      [&](const TouchTrack& track) {
                                          return track.fingerId == event.tfinger.fingerId;
                                      }),
                        touches.end());
                    break;
                }
                default:
                    break;
            }
        }

        if (escapePressed) {
            if (pauseDialogOpen) {
                pauseDialogOpen = false;
                paused = false;
                audio.resume();
            } else if (state == AppState::Play && susPath.empty()) {
                // back to the song list
                audio.stopMusic();
                audio.setHoldLoop(false, false, 0.0f);
                touches.clear();
                std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                lanePress.fill(0.0f);
                paused = false;
                session.active = false;
                state = AppState::Select;
                systemMedia.setTaskbarProgress(-1.0, false);
            } else {
                running = false;
            }
        }

        if (state == AppState::Play && paused) {
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

            if (selected >= 0 && selected < static_cast<int>(entries.size())) {
                const std::string& cover = entries[static_cast<size_t>(selected)].coverPath;
                if (cover != loadedCoverPath) {
                    loadedCoverPath = cover;
                    renderer.loadCover(cover, error);
                }
            }

            const int action = game::drawSongSelect(renderer, entries, selected, windowW, windowH,
                static_cast<float>(uiClock));
            if (action >= 0 && action < static_cast<int>(entries.size())) {
                if (startSession(session, entries[static_cast<size_t>(action)], renderer, audio, judgement, noteSpeed, error)) {
                    loadedCoverPath = session.entry.coverPath;
                    announceTrack();
                    touches.clear();
                    std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                    lanePress.fill(0.0f);
                    paused = false;
                    state = AppState::Play;
                    beginSessionClock();
                } else {
                    std::fprintf(stderr, "%s\n", error.c_str());
                    error.clear();
                }
            } else if (action == game::SelectRescan) {
                entries = game::scanChartFolder(chartsDir);
                selected = entries.empty() ? -1 : 0;
                loadedCoverPath.clear();
                std::printf("[select] %d chart(s)\n", static_cast<int>(entries.size()));
            }

            // Headless check: dump the song list shortly after startup.
            if (!screenshotPath.empty() && uiClock > 1.2) {
                wantScreenshot = true;
            }
        } else {
            // ----------------------------------------------------------
            // Playing
            // ----------------------------------------------------------
            const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
            audio.update();
            const float outputTime = static_cast<float>(songTime + leadInSec);

            // Report to Windows: SMTC position + taskbar button progress.
            systemMedia.updatePlayback(true, paused, songTime, trackDurationSec);
            systemMedia.setTaskbarProgress(
                trackDurationSec > 1.0 ? songTime / trackDurationSec : -1.0, paused);

            std::vector<float> holdLanes;
            for (int lane = 0; lane < 12; ++lane) {
                if (keyHeld[lane]) {
                    holdLanes.push_back(keyLanePos(lane));
                }
            }
            for (const auto& track : touches) {
                holdLanes.push_back(track.lanePos);
            }
            judgement.setHoldLanes(holdLanes);
            judgement.update(static_cast<float>(songTime));

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
            if (stats.lastJudgeTimeSec != lastSeenJudgeTime) {
                lastSeenJudgeTime = stats.lastJudgeTimeSec;
                hudState.lastJudge = stats.lastJudge;
                hudState.lastJudgeAtSec = stats.lastJudgeTimeSec;
                if (stats.lastJudge != game::Judge::Miss && stats.lastJudge != game::Judge::None) {
                    hitEffects.push_back(game::HitFx{stats.lastHitCenter, 0.0f, 1.0f});
                }
            }
            for (auto& fx : hitEffects) {
                fx.age += frameDelta;
            }
            hitEffects.erase(std::remove_if(hitEffects.begin(), hitEffects.end(),
                                 [](const game::HitFx& fx) { return fx.age > 0.35f; }),
                hitEffects.end());
            hudState.lifeRatio = 1.0f; // TODO: wire to real life calculation

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
                game::drawHud(renderer, hudState, static_cast<float>(songTime), windowW, windowH, hitEffects,
                    static_cast<float>(leadInSec), dumpJudgeSheet);
            }
            game::drawIntro(renderer, session.intro, outputTime, windowW, windowH);

            // ----------------------------------------------------------
            // Pause button zone (right end of the life bar).
            // ----------------------------------------------------------
            if (!autoPlay && !pauseDialogOpen && visibility > 0.0f && ImGui::IsMouseClicked(0)) {
                const ImGuiIO& io = ImGui::GetIO();
                const float hudScale = std::min(static_cast<float>(windowW) / 1920.0f,
                    static_cast<float>(windowH) / 1080.0f);
                const float hudOffX = (static_cast<float>(windowW) - 1920.0f * hudScale) * 0.5f;
                const float hudOffY = (static_cast<float>(windowH) - 1080.0f * hudScale) * 0.5f;
                const float vx = (io.MousePos.x - hudOffX) / hudScale;
                const float vy = (io.MousePos.y - hudOffY) / hudScale;
                const game::HudRect pauseRect = game::lifePauseRect();
                if (vx >= pauseRect.x && vx <= pauseRect.x + pauseRect.w && vy >= pauseRect.y
                    && vy <= pauseRect.y + pauseRect.h) {
                    paused = true;
                    audio.pause();
                    pauseDialogOpen = true;
                }
            }

            if (showDebug) {
                // pjsk style settings panel (rounded card, capsule buttons).
                const float s = ui::scale();
                const ImVec2 display = ImGui::GetIO().DisplaySize;
                const ImVec2 cardSize = ImVec2(470.0f * s, 600.0f * s);
                const ImVec2 cardCenter = ImVec2(20.0f * s + cardSize.x * 0.5f, 20.0f * s + cardSize.y * 0.5f);
                bool closeClicked = false;
                ui::beginCard("##settings", cardCenter, cardSize, true, false, &closeClicked);
                if (closeClicked) {
                    showDebug = false;
                }
                ImGui::SetCursorScreenPos(
                    ImVec2(cardCenter.x - cardSize.x * 0.5f + 40.0f * s, cardCenter.y - cardSize.y * 0.5f + 40.0f * s));
                ui::cardTitle("设置", cardSize.x - 80.0f * s);

                ImGui::PushStyleColor(ImGuiCol_Text, ui::kBodyText);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(224, 224, 235, 255));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(214, 214, 228, 255));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(205, 205, 222, 255));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, ui::kPrimary);
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ui::kPrimaryPress);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f * s);
                ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 14.0f * s);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * s, 8.0f * s));
                ImGui::PushFont(game::bodyFont(), 23.0f * s);
                ImGui::PushItemWidth(210.0f * s);

                ImGui::Text("time: %.2fs / %.2fs", songTime, trackDurationSec);
                ImGui::Text("music start: %.2fs  offset: %+.0fms", audio.musicStartPos(), audio.userOffset() * 1000.0);
                ImGui::Text("combo: %d (max %d)", stats.combo, stats.maxCombo);
                ImGui::Text("score: %.0f", stats.score);
                ImGui::Text("P %d  G %d  Good %d  Miss %d", stats.perfect, stats.great, stats.good, stats.miss);
                const char* judgeName = "none";
                switch (stats.lastJudge) {
                    case game::Judge::Perfect: judgeName = "PERFECT"; break;
                    case game::Judge::Great: judgeName = "GREAT"; break;
                    case game::Judge::Good: judgeName = "GOOD"; break;
                    case game::Judge::Miss: judgeName = "MISS"; break;
                    default: break;
                }
                ImGui::Text("judge: %s", judgeName);
                static float offsetMs = static_cast<float>(gUserOffsetSec * 1000.0);
                if (ImGui::SliderFloat("audio offset ms", &offsetMs, -2000.0f, 2000.0f, "%.0f")) {
                    audio.setUserOffset(static_cast<double>(offsetMs) / 1000.0);
                }
                // Window mode: borderless hides the frame, windowed shows the
                // title bar, fullscreen grabs the whole desktop.
                static int winMode = windowMode;
                if (ImGui::Combo("window mode", &winMode, "borderless\0windowed\0fullscreen\0")) {
                    windowMode = winMode;
                    if (winMode == 2) {
                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    } else {
                        SDL_SetWindowFullscreen(window, 0);
                        SDL_SetWindowBordered(window, winMode == 1 ? SDL_TRUE : SDL_FALSE);
                    }
                }
                ImGui::SliderInt("fps limit", &fpsLimitLive, 0, 240,
                    fpsLimitLive == 0 ? "vsync only" : "%d");
                ImGui::Text("fps: %.1f", 1.0 / std::max(1e-6, lastFrameDeltaSec));
                float speed = noteSpeed;
                if (ImGui::SliderFloat("speed", &speed, 1.0f, 12.0f, "%.1f")) {
                    noteSpeed = speed;
                    core_api::setPreviewConfig(0, 1, 1, 1, 0, 0, noteSpeed, 1.0f, 0.6f, 0.0f, 1.0f, 0.85f);
                }
                // pjsk style stepper: -1 / -0.1 / -0.01 / value / +0.01 / +0.1 / +1.
                {
                    float stepped = noteSpeed;
                    if (ui::stepper("speed-stepper", &stepped, {-1.0f, -0.1f, -0.01f, 0.01f, 0.1f, 1.0f},
                            "%.2f", cardSize.x - 80.0f * s)) {
                        noteSpeed = std::clamp(stepped, 1.0f, 12.0f);
                        core_api::setPreviewConfig(0, 1, 1, 1, 0, 0, noteSpeed, 1.0f, 0.6f, 0.0f, 1.0f, 0.85f);
                    }
                }
                static float perfect = 40.0f;
                static float great = 90.0f;
                static float good = 140.0f;
                bool windowsChanged = false;
                windowsChanged |= ImGui::SliderFloat("perfect ms", &perfect, 10.0f, 100.0f, "%.0f");
                windowsChanged |= ImGui::SliderFloat("great ms", &great, 20.0f, 160.0f, "%.0f");
                windowsChanged |= ImGui::SliderFloat("good ms", &good, 30.0f, 220.0f, "%.0f");
                if (windowsChanged) {
                    game::JudgementWindows windows;
                    windows.perfectMs = perfect;
                    windows.greatMs = std::max(great, perfect + 10.0f);
                    windows.goodMs = std::max(good, great + 10.0f);
                    windows.missAfterMs = good + 60.0f;
                    judgement.setWindows(windows);
                }
                ImGui::PopFont();
                ImGui::PopStyleVar(3);
                ImGui::PopStyleColor(6);
                ImGui::SetCursorScreenPos(ImVec2(cardCenter.x - cardSize.x * 0.5f + 40.0f * s,
                    cardCenter.y + cardSize.y * 0.5f - 100.0f * s));
                if (ui::capsuleButton("关闭", ImVec2(180.0f * s, 64.0f * s), false)) {
                    showDebug = false;
                }
                ui::endCard();
            }

            // ----------------------------------------------------------
            // Pause dialog: 重试 / 放弃 / 继续演出.
            // ----------------------------------------------------------
            if (showPauseDialogShot && !pauseDialogOpen && songTime > 0.5) {
                paused = true;
                audio.pause();
                pauseDialogOpen = true;
            }
            if (pauseDialogOpen) {
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
                    audio.stopMusic();
                    audio.setHoldLoop(false, false, 0.0f);
                    touches.clear();
                    std::fill(std::begin(keyHeld), std::end(keyHeld), false);
                    lanePress.fill(0.0f);
                    paused = false;
                    session.active = false;
                    state = AppState::Select;
                    systemMedia.setTaskbarProgress(-1.0, false);
                } else if (action == 2 || action == -2) {
                    // Continue (the X closes as "continue", nothing is lost).
                    pauseDialogOpen = false;
                    paused = false;
                    audio.resume();
                }
            }

            if (!screenshotPath.empty() && songTime >= screenshotTimeSec) {
                wantScreenshot = true;
            }
        }

        // Paused frames (pause dialog) skip the playing branch above; let the
        // headless screenshot still fire using the wall clock.
        if (!screenshotPath.empty() && !wantScreenshot && state == AppState::Play && paused
            && (wallSongTime() - leadInSec) >= screenshotTimeSec) {
            wantScreenshot = true;
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Captured after the ImGui pass so the HUD / intro card / song list
        // are part of the frame.
        if (wantScreenshot) {
            wantScreenshot = false;
            saveScreenshot();
            running = false;
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
