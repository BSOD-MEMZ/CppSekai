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
#include "game/Judgement.hpp"
#include "game/Hud.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
        float lastWorldY = 0.0f;
        double lastMoveTimeSec = 0.0;
        bool flicked = false;
    };

    // Keyboard: 12 keys -> 12 half-lanes. Lane i (0..11) covers world x
    // [i-5, i-4]; the input position is the lane middle.
    const SDL_Keycode kLaneKeys[12] = {
        SDLK_z, SDLK_s, SDLK_x, SDLK_d, SDLK_c, SDLK_v,
        SDLK_g, SDLK_b, SDLK_h, SDLK_n, SDLK_j, SDLK_m,
    };

    float keyLanePos(int lane)
    {
        return static_cast<float>(lane) - 4.5f;
    }

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
            "Usage: cppsekai --sus <file.sus> [--bgm <audio>] [--offset <sec>] [--auto]\n"
            "                [--speed <1-12>] [--se-volume <0-1>]\n\n"
            "Keyboard: Z S X D C V G B H N J M = 12 half-lanes\n"
            "          SPACE = pause, F = fullscreen, ESC = quit\n"
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
} // namespace

int main(int argc, char** argv)
{
    std::string susPath;
    std::string bgmPath;
    double offsetSec = 0.0;
    bool offsetGiven = false;
    bool autoPlay = false;
    float noteSpeed = 8.0f;
    float seVolume = 0.8f;
    std::string screenshotPath; // if set: dump a frame and exit (headless check)
    double screenshotTimeSec = 4.0;
    double leadIn = 3.5; // intro display time before the music starts
    bool dumpJudgeSheet = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sus" && i + 1 < argc) {
            susPath = argv[++i];
        } else if (arg == "--bgm" && i + 1 < argc) {
            bgmPath = argv[++i];
        } else if (arg == "--offset" && i + 1 < argc) {
            offsetSec = std::atof(argv[++i]);
            offsetGiven = true;
        } else if (arg == "--auto") {
            autoPlay = true;
        } else if (arg == "--speed" && i + 1 < argc) {
            noteSpeed = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--se-volume" && i + 1 < argc) {
            seVolume = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (arg == "--screenshot-time" && i + 1 < argc) {
            screenshotTimeSec = std::atof(argv[++i]);
        } else if (arg == "--lead-in" && i + 1 < argc) {
            leadIn = std::atof(argv[++i]);
        } else if (arg == "--judge-sheet") {
            dumpJudgeSheet = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
    }

    if (susPath.empty()) {
        printUsage();
        return 1;
    }

    // GUI-subsystem binaries have no console; when running headless checks
    // route stdout/stderr into a log file next to the screenshot.
    std::FILE* logFile = nullptr;
    if (!screenshotPath.empty()) {
        logFile = std::fopen("cppsekai.log", "w");
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

    int windowW = 1280;
    int windowH = 720;
    SDL_Window* window = SDL_CreateWindow(
        "CppSekai",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowW, windowH,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
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
    const std::string seDir = baseDir + "assets\\se";

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

    // ------------------------------------------------------------------
    // Chart core
    // ------------------------------------------------------------------
    core_api::init();
    core_api::resize(windowW, windowH, 1.0f);
    const std::string susText = readFile(susPath);
    if (!offsetGiven) {
        offsetSec = core_api::readWaveOffset(susText);
    }
    if (!core_api::loadSusTextPrecise(susText.c_str(), offsetSec * 1000.0)) {
        std::fprintf(stderr, "SUS parse failed: %s\n", core_api::getLastError());
        return 1;
    }
    core_api::setPreviewConfig(0, 1, 1, 1, 0, 0, noteSpeed, 1.0f, 0.6f, 0.0f, 1.0f, 0.85f);

    // ------------------------------------------------------------------
    // Audio
    // ------------------------------------------------------------------
    platform::AudioEngine audio;
    if (!audio.init(error)) {
        std::fprintf(stderr, "audio init failed: %s\n", error.c_str());
        return 1;
    }
    if (!bgmPath.empty() && !audio.loadMusic(bgmPath, error)) {
        std::fprintf(stderr, "warning: %s\n", error.c_str());
        error.clear();
    }
    if (!audio.loadSe(seDir, error)) {
        std::fprintf(stderr, "warning: SE load failed: %s\n", error.c_str());
        error.clear();
    }
    audio.setMusicDelay(offsetSec);

    // ------------------------------------------------------------------
    // Judgement
    // ------------------------------------------------------------------
    game::JudgementEngine judgement;
    {
        const float* events = core_api::getHitEventBuffer();
        const int count = core_api::getHitEventCount();
        judgement.load(events, count);

        // Debug: dump the first events so time-base problems are visible
        // in cppsekai.log.
        for (int i = 0; i < count && i < 8; ++i) {
            const int off = i * 7;
            std::printf("hitEvent[%d] t=%.3f center=%.2f width=%.2f kind=%.0f end=%.3f\n",
                i, events[off + 0], events[off + 1], events[off + 2], events[off + 3], events[off + 5]);
        }
        std::fflush(stdout);
    }

    // ------------------------------------------------------------------
    // ImGui
    // ------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // ------------------------------------------------------------------
    // Main loop
    // ------------------------------------------------------------------
    const double leadInSec = leadIn;
    bool paused = false;
    bool running = true;
    bool fullscreen = false;
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 perfStart = SDL_GetPerformanceCounter();

    // Clock without BGM (auto mode / missing audio): wall clock.
    auto wallSongTime = [&]() {
        return -leadInSec + static_cast<double>(SDL_GetPerformanceCounter() - perfStart) / static_cast<double>(perfFreq);
    };

    audio.start(leadInSec);

    std::vector<TouchTrack> touches;
    bool keyHeld[12] = {};
    double lastFrameDeltaSec = 0.0;
    Uint64 lastFrameCounter = SDL_GetPerformanceCounter();
    SDL_Event event;

    while (running) {
        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        lastFrameDeltaSec = static_cast<double>(nowCounter - lastFrameCounter) / static_cast<double>(perfFreq);
        lastFrameCounter = nowCounter;
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
                        running = false;
                    } else if (event.key.keysym.sym == SDLK_SPACE && !autoPlay) {
                        paused = !paused;
                        if (paused) {
                            audio.pause();
                        } else {
                            audio.resume();
                        }
                    } else if (event.key.keysym.sym == SDLK_f) {
                        fullscreen = !fullscreen;
                        SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    } else if (!autoPlay && !paused) {
                        const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
                        for (int lane = 0; lane < 12; ++lane) {
                            if (event.key.keysym.sym == kLaneKeys[lane]) {
                                keyHeld[lane] = true;
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
                    if (autoPlay || paused) {
                        break;
                    }
                    TouchTrack track;
                    track.fingerId = event.tfinger.fingerId;
                    const float clipX = (event.tfinger.x * 2.0f) - 1.0f;
                    const float clipY = 1.0f - (event.tfinger.y * 2.0f);
                    track.lanePos = renderer.clipToWorldX(clipX);
                    track.lastWorldY = renderer.clipToWorldY(clipY);
                    track.lastMoveTimeSec = SDL_GetTicks() / 1000.0;
                    touches.push_back(track);
                    const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
                    const game::Judge result = judgement.tap(track.lanePos, static_cast<float>(songTime), false, 0.8f);
                    if (result != game::Judge::None) {
                        playHitSe(audio, judgement, seVolume);
                    }
                    break;
                }
                case SDL_FINGERMOTION: {
                    if (autoPlay || paused) {
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

        if (paused) {
            SDL_Delay(16);
        }

        // ------------------------------------------------------------------
        // Clock + hold lane tracking
        // ------------------------------------------------------------------
        const double songTime = audio.hasMusic() ? audio.songTime() : wallSongTime();
        audio.update();

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

        // Update the HUD state; the judge text only triggers on real hits.
        const auto& stats = judgement.stats();
        static float lastSeenJudgeTime = -100.0f;
        static game::HudState hudState;
        static std::vector<game::HitFx> hitEffects;
        hudState.score = stats.score;
        hudState.combo = stats.combo;
        if (stats.lastJudgeTimeSec != lastSeenJudgeTime) {
            lastSeenJudgeTime = stats.lastJudgeTimeSec;
            hudState.lastJudge = stats.lastJudge;
            hudState.lastJudgeAtSec = stats.lastJudgeTimeSec;
            if (stats.lastJudge != game::Judge::Miss && stats.lastJudge != game::Judge::None) {
                hitEffects.push_back(game::HitFx{stats.lastHitCenter, 0.0f});
            }
        }
        const float frameDelta = static_cast<float>(lastFrameDeltaSec);
        for (auto& fx : hitEffects) {
            fx.age += frameDelta;
        }
        hitEffects.erase(std::remove_if(hitEffects.begin(), hitEffects.end(),
                             [](const game::HitFx& fx) { return fx.age > 0.35f; }),
            hitEffects.end());
        hudState.lifeRatio = 1.0f; // TODO: wire to real life calculation

        // ------------------------------------------------------------------
        // Render
        // ------------------------------------------------------------------
        core_api::render(static_cast<float>(songTime));
        const float* quads = core_api::getQuadBuffer();
        const int quadCount = core_api::getQuadCount();
        renderer.renderFrame(quads, quadCount, 0.85f);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // pjsk overlay HUD (drawn into the foreground draw list).
        game::drawHud(renderer, hudState, static_cast<float>(songTime), windowW, windowH, hitEffects,
            static_cast<float>(leadInSec), dumpJudgeSheet);

        {
            const auto& stats = judgement.stats();
            ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Once);
            ImGui::SetNextWindowSize(ImVec2(300, 260), ImGuiCond_Once);
            ImGui::Begin("CppSekai", nullptr, ImGuiWindowFlags_NoCollapse);
            ImGui::Text("time: %.2fs", songTime);
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
            static float offsetMs = 0.0f;
            if (ImGui::SliderFloat("audio offset ms", &offsetMs, -500.0f, 500.0f, "%.0f")) {
                audio.setMusicDelay(static_cast<double>(offsetMs) / 1000.0);
            }
            float speed = noteSpeed;
            if (ImGui::SliderFloat("speed", &speed, 1.0f, 12.0f, "%.1f")) {
                noteSpeed = speed;
                core_api::setPreviewConfig(0, 1, 1, 1, 0, 0, noteSpeed, 1.0f, 0.6f, 0.0f, 1.0f, 0.85f);
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
            ImGui::Separator();
            ImGui::Text("%s", core_api::getMetadataTitle());
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (!screenshotPath.empty() && songTime >= screenshotTimeSec) {
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
            running = false;
        }

        SDL_GL_SwapWindow(window);
    }

    audio.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
