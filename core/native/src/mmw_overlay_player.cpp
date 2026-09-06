#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#ifndef __EMSCRIPTEN__
#include <iostream>
#endif
#include <limits>
#include <map>
#include <sstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/imgui_internal.h"
#include "../vendor/imgui/imgui_impl_opengl3.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/mmw_preview/vendor/stb_image.h"
#include "generated_resources.h"

extern "C"
{
    int init(int);
    void resize(int width, int height, float dpr);
    int loadSusText(const char* susText, int normalizedOffsetMs);
    int loadSusTextPrecise(const char* susText, double normalizedOffsetMs);
    int loadCustomScoreJsonTextPrecise(const char* jsonText, double normalizedOffsetMs);
    void setPreviewConfig(
        int mirror,
        int flickAnimation,
        int holdAnimation,
        int simultaneousLine,
        int effectProfile,
        int noteSkin,
        float noteSpeed,
        float holdAlpha,
        float guideAlpha,
        float stageCover,
        float stageOpacity,
        float backgroundBrightness);
    int render(float chartTimeSec);
    const float* getQuadBufferPointer();
    int getQuadCount();
    double getChartEndTimeSec();
    const float* getHitEventBufferPointer();
    int getHitEventCount();
    const float* getHudEventBufferPointer();
    int getHudEventCount();
    const char* getMetadataTitle();
    const char* getMetadataArtist();
    const char* getMetadataDesigner();
    const char* getLastError();
    void dispose();
}

namespace
{
    constexpr int FLOATS_PER_VERTEX = 9;
    constexpr double NOTE_AUDIO_DELAY_SEC = 0.05;
    constexpr int FLOATS_PER_QUAD = 25;

    constexpr float STAGE_LANE_TOP = 47.0f;
    constexpr float STAGE_LANE_HEIGHT = 850.0f;
    constexpr float STAGE_LANE_WIDTH = 1420.0f;
    constexpr float STAGE_NUM_LANES = 12.0f;
    constexpr float STAGE_TEX_WIDTH = 2048.0f;
    constexpr float STAGE_TEX_HEIGHT = 1176.0f;
    constexpr float STAGE_TARGET_WIDTH = 1920.0f;
    constexpr float STAGE_TARGET_HEIGHT = 1080.0f;
    constexpr float STAGE_ASPECT_RATIO = STAGE_TARGET_WIDTH / STAGE_TARGET_HEIGHT;
    constexpr float STAGE_ZOOM = 927.0f / 800.0f;
    constexpr float STAGE_WIDTH_RATIO = STAGE_ZOOM * STAGE_LANE_WIDTH / (STAGE_TEX_HEIGHT * STAGE_ASPECT_RATIO) / STAGE_NUM_LANES;
    constexpr float STAGE_HEIGHT_RATIO = STAGE_ZOOM * STAGE_LANE_HEIGHT / STAGE_TEX_HEIGHT;
    constexpr float STAGE_TOP_RATIO = 0.5f + STAGE_ZOOM * STAGE_LANE_TOP / STAGE_TEX_HEIGHT;

    constexpr float WORLD_STAGE_WIDTH = (STAGE_TEX_WIDTH / STAGE_LANE_WIDTH) * STAGE_NUM_LANES;
    constexpr float WORLD_STAGE_LEFT = -WORLD_STAGE_WIDTH / 2.0f;
    constexpr float WORLD_STAGE_TOP = STAGE_LANE_TOP / STAGE_LANE_HEIGHT;
    constexpr float WORLD_STAGE_HEIGHT = STAGE_TEX_HEIGHT / STAGE_LANE_HEIGHT;

    constexpr float BACKGROUND_SIZE = 2462.25f;
    constexpr float WORLD_BACKGROUND_WIDTH = BACKGROUND_SIZE / (STAGE_TARGET_WIDTH * STAGE_WIDTH_RATIO);
    constexpr float WORLD_BACKGROUND_HEIGHT = BACKGROUND_SIZE / (STAGE_TARGET_HEIGHT * STAGE_HEIGHT_RATIO);
    constexpr float WORLD_BACKGROUND_LEFT = -WORLD_BACKGROUND_WIDTH / 2.0f;
    constexpr float WORLD_BACKGROUND_TOP =
        0.5f / STAGE_HEIGHT_RATIO + STAGE_LANE_TOP / STAGE_LANE_HEIGHT - WORLD_BACKGROUND_HEIGHT / 2.0f;

    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Rect
    {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
    };

    struct Color
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct Texture
    {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };

    enum class TextureKey
    {
        Background,
        Stage,
        Notes,
        LongNoteLine,
        TouchLine,
        Effect,
    };

    enum class BlendMode
    {
        Normal,
        Additive,
    };

    enum class ResourceKind : int
    {
        Asset = 0,
        Font = 1,
        Sound = 2,
    };

    enum class TransportStateNative : int
    {
        Idle = 0,
        Loading = 1,
        Ready = 2,
        Playing = 3,
        Paused = 4,
        Error = 5,
    };

    struct BinaryBlob
    {
        std::vector<std::uint8_t> bytes;
    };

    struct SessionMetadata
    {
        std::string title;
        std::string lyricist;
        std::string composer;
        std::string arranger;
        std::string vocal;
        std::string difficulty;
    };

    struct PlayerSnapshot
    {
        double currentTimeSec = 0.0;
        double durationSec = 0.0;
        double chartEndSec = 0.0;
        double sourceOffsetSec = 0.0;
        double effectiveLeadInSec = 9.0;
        double audioStartDelaySec = 0.0;
        double apStartSec = 0.0;
        TransportStateNative transportState = TransportStateNative::Idle;
        bool requiresGesture = false;
        bool hasAudio = false;
    };

    struct PlayerRuntimeState
    {
        std::unordered_map<std::string, BinaryBlob> assets;
        std::unordered_map<std::string, BinaryBlob> fonts;
        std::unordered_map<std::string, BinaryBlob> sounds;
        std::string lastError;
        std::string canvasSelector = "#preview-canvas";
        std::unique_ptr<class GlRenderer> renderer;
        std::unordered_map<std::string, Texture> hudTextures;
        std::unique_ptr<struct IntroFontSet> introFonts;
        std::unique_ptr<struct IntroCardState> introCard;
        std::unique_ptr<struct HudTimelineNative> hudTimeline;
        SessionMetadata sessionMetadata;
        std::string coverAssetKey;
        bool initialized = false;
        bool sessionLoaded = false;
        bool imguiInitialized = false;
        bool audioUnlocked = false;
        float effectOpacity = 1.0f;
        float stageOpacity = 1.0f;
        float stageCover = 0.0f;
        int noteSkin = 0;
        float chartPlayableEndSec = 0.0f;
        float previousChartTimeSec = -1000.0f;
        float scorePlusTriggerSec = -1000.0f;
        int scorePlusValue = 0;
        int lastScoreEventIndex = -1;
        int nextHitEventIndex = 0;
        double sourceOffsetSec = 0.0;
        double effectiveLeadInSec = 9.0;
        double audioStartDelaySec = 0.0;
        double durationSec = 0.0;
        double chartEndSec = 0.0;
        double apStartSec = 0.0;
        double playbackRate = 1.0;
    };

    PlayerRuntimeState gPlayer;

    EM_JS(void, jsAudioEnsureEngine, (), {
        if (Module.__mmwAudio) {
            return;
        }
        class WasmAudioTransport {
            constructor() {
                this.audioContext = null;
                this.audioBuffer = null;
                this.soundBuffers = new Map();
                this.source = null;
                this.gainNode = null;
                this.bgmVolume = 1;
                this.soundVolume = 1;
                this.oneShots = new Set();
                this.extendableSources = new Map();
                this.state = 0;
                this.playbackRate = 1;
                this.baseTimeSec = 0;
                this.startedAtAudioTime = 0;
                this.startedAtWallTime = 0;
                this.durationSec = 0;
                this.audioStartOffsetSec = 0;
                this.pendingGestureStart = false;
                this.lastError = "";
            }

            ensureAudioContext() {
                if (!this.audioContext) {
                    const Ctx = globalThis.AudioContext || globalThis.webkitAudioContext;
                    if (!Ctx) {
                        throw new Error('AudioContext unavailable');
                    }
                    this.audioContext = new Ctx();
                }
                return this.audioContext;
            }

            clampTime(timeSec) {
                return this.durationSec > 0 ? Math.min(Math.max(timeSec, 0), this.durationSec) : Math.max(timeSec, 0);
            }

            getCurrentTimeSec() {
                if (this.state !== 3) {
                    return this.baseTimeSec;
                }

                let current = this.baseTimeSec;
                if (this.audioBuffer && this.audioContext) {
                    current += (this.audioContext.currentTime - this.startedAtAudioTime) * this.playbackRate;
                } else {
                    current += ((performance.now() - this.startedAtWallTime) / 1000) * this.playbackRate;
                }

                if (current >= this.durationSec && this.durationSec > 0) {
                    this.baseTimeSec = this.durationSec;
                    this.stopSource();
                    this.state = 4;
                    return this.baseTimeSec;
                }

                return current;
            }

            stopSource() {
                if (!this.source) {
                    return;
                }
                this.source.onended = null;
                try {
                    this.source.stop();
                } catch {}
                this.source.disconnect();
                this.source = null;
            }

            clearOneShots() {
                for (const node of this.oneShots) {
                    try {
                        node.stop();
                    } catch {}
                    try {
                        node.disconnect();
                    } catch {}
                }
                this.oneShots.clear();
                for (const [_, entry] of this.extendableSources) {
                    try {
                        entry.source.onended = null;
                        entry.source.stop();
                    } catch {}
                    try {
                        entry.source.disconnect();
                        entry.gainNode.disconnect();
                    } catch {}
                }
                this.extendableSources.clear();
            }

            startPlayback() {
                this.baseTimeSec = this.clampTime(this.baseTimeSec);
                this.startedAtWallTime = performance.now();
                this.startedAtAudioTime = this.audioContext?.currentTime ?? 0;
                this.stopSource();
                if (this.audioBuffer && this.audioContext) {
                    const audioTimeSec = this.baseTimeSec - this.audioStartOffsetSec;
                    const sourceOffsetSec = Math.max(audioTimeSec, 0);
                    const startDelaySec = audioTimeSec < 0 ? (-audioTimeSec) / this.playbackRate : 0;
                    if (sourceOffsetSec < this.audioBuffer.duration) {
                        const source = this.audioContext.createBufferSource();
                        source.buffer = this.audioBuffer;
                        source.playbackRate.value = this.playbackRate;
                        if (!this.gainNode) {
                            this.gainNode = this.audioContext.createGain();
                        }
                        this.gainNode.gain.value = this.bgmVolume;
                        source.connect(this.gainNode);
                        this.gainNode.connect(this.audioContext.destination);
                        source.onended = () => {
                            if (this.source === source && this.state === 3) {
                                const currentTimeSec = Math.min(this.durationSec, this.getCurrentTimeSec());
                                this.source = null;
                                this.baseTimeSec = currentTimeSec;
                                if (currentTimeSec < this.durationSec) {
                                    this.startedAtWallTime = performance.now();
                                    this.startedAtAudioTime = this.audioContext?.currentTime ?? 0;
                                    return;
                                }
                                this.state = 4;
                            }
                        };
                        source.start(this.audioContext.currentTime + startDelaySec, sourceOffsetSec);
                        this.source = source;
                        this.startedAtAudioTime = this.audioContext.currentTime;
                    }
                }
                this.state = 3;
            }

            setDuration(durationSec) {
                this.durationSec = Math.max(Number(durationSec) || 0, 0);
                this.baseTimeSec = Math.min(this.baseTimeSec, this.durationSec);
            }

            setAudioStartOffset(offsetSec) {
                this.audioStartOffsetSec = Math.max(Number(offsetSec) || 0, 0);
                if (this.audioBuffer) {
                    this.durationSec = Math.max(this.durationSec, this.audioStartOffsetSec + this.audioBuffer.duration);
                }
                if (this.state === 3) {
                    this.baseTimeSec = this.getCurrentTimeSec();
                    this.stopSource();
                    this.startPlayback();
                    return;
                }
                this.baseTimeSec = this.clampTime(this.baseTimeSec);
            }

            async loadBgm(bytes) {
                if (!bytes || !bytes.byteLength) {
                    this.audioBuffer = null;
                    if (this.state !== 3) {
                        this.state = 2;
                    }
                    return;
                }
                const context = this.ensureAudioContext();
                this.audioBuffer = await context.decodeAudioData(bytes.slice(0));
                this.durationSec = Math.max(this.durationSec, this.audioStartOffsetSec + this.audioBuffer.duration);
                if (this.state !== 3) {
                    this.state = 2;
                }
            }

            async loadSound(name, bytes) {
                if (!name || !bytes || !bytes.byteLength) {
                    return;
                }
                const context = this.ensureAudioContext();
                const buffer = await context.decodeAudioData(bytes.slice(0));
                this.soundBuffers.set(name, buffer);
            }

            async unlock() {
                const context = this.ensureAudioContext();
                await context.resume();
                if (this.pendingGestureStart) {
                    this.pendingGestureStart = false;
                    this.startPlayback();
                }
                return true;
            }

            async play() {
                if (this.state === 3) {
                    return true;
                }
                if (this.audioBuffer) {
                    const context = this.ensureAudioContext();
                    if (context.state !== 'running') {
                        try {
                            await context.resume();
                        } catch {
                            this.pendingGestureStart = true;
                            return false;
                        }
                    }
                }
                this.startPlayback();
                return true;
            }

            pause() {
                if (this.state !== 3) {
                    return;
                }
                this.baseTimeSec = this.getCurrentTimeSec();
                this.stopSource();
                this.state = 4;
            }

            seek(timeSec) {
                const clamped = this.clampTime(timeSec);
                const wasPlaying = this.state === 3;
                this.baseTimeSec = clamped;
                if (wasPlaying) {
                    this.stopSource();
                    this.startPlayback();
                }
            }

            setPlaybackRate(rate) {
                const nextRate = Math.max(0.05, Number(rate) || 1);
                this.playbackRate = nextRate;
                if (this.state === 3) {
                    this.baseTimeSec = this.getCurrentTimeSec();
                    this.stopSource();
                    this.startPlayback();
                }
            }

            setAudioVolumes(bgmVolume, soundVolume) {
                this.bgmVolume = Math.min(1, Math.max(0, Number(bgmVolume)));
                if (!Number.isFinite(this.bgmVolume)) {
                    this.bgmVolume = 1;
                }
                this.soundVolume = Math.min(1, Math.max(0, Number(soundVolume)));
                if (!Number.isFinite(this.soundVolume)) {
                    this.soundVolume = 1;
                }
                if (this.gainNode) {
                    this.gainNode.gain.value = this.bgmVolume;
                }
                for (const entry of this.extendableSources.values()) {
                    entry.gainNode.gain.value = entry.baseGain * this.soundVolume;
                }
            }

            trigger(name, gain, delaySec) {
                if (!name) {
                    return;
                }
                const context = this.audioContext;
                const buffer = this.soundBuffers.get(name);
                if (!context || !buffer) {
                    return;
                }
                const source = context.createBufferSource();
                const gainNode = context.createGain();
                source.buffer = buffer;
                gainNode.gain.value = Math.max(0, Number(gain) || 0) * this.soundVolume;
                source.connect(gainNode);
                gainNode.connect(context.destination);
                source.onended = () => {
                    this.oneShots.delete(source);
                    try {
                        source.disconnect();
                        gainNode.disconnect();
                    } catch {}
                };
                source.start(context.currentTime + Math.max(0, Number(delaySec) || 0));
                this.oneShots.add(source);
            }

            triggerExtendable(name, gain, currentOutputTimeSec, endOutputTimeSec, delaySec) {
                if (!name) {
                    return;
                }
                const context = this.audioContext;
                const buffer = this.soundBuffers.get(name);
                if (!context || !buffer) {
                    return;
                }
                const currentOutput = Math.max(0, Number(currentOutputTimeSec) || 0);
                const nextEndTimeSec = Math.max(0, Number(endOutputTimeSec) || 0);
                if (nextEndTimeSec - currentOutput <= 0.0001) {
                    return;
                }
                const existing = this.extendableSources.get(name);
                if (existing) {
                    if (currentOutput > existing.endTimeSec + 0.0001) {
                        this.stopExtendable(name);
                    } else {
                        if (nextEndTimeSec <= existing.endTimeSec + 0.0001) {
                            return;
                        }
                        existing.endTimeSec = nextEndTimeSec;
                        return;
                    }
                }

                const source = context.createBufferSource();
                const gainNode = context.createGain();
                const baseGain = Math.max(0, Number(gain) || 0);
                source.buffer = buffer;
                source.loop = true;
                if (buffer.length > 6000) {
                    const guardFrames = 3000;
                    source.loopStart = guardFrames / buffer.sampleRate;
                    source.loopEnd = (buffer.length - guardFrames) / buffer.sampleRate;
                }
                source.playbackRate.value = this.playbackRate;
                gainNode.gain.value = baseGain * this.soundVolume;
                source.connect(gainNode);
                gainNode.connect(context.destination);
                source.onended = () => {
                    const current = this.extendableSources.get(name);
                    if (current && current.source === source) {
                        this.extendableSources.delete(name);
                    }
                    try {
                        source.disconnect();
                        gainNode.disconnect();
                    } catch {}
                };
                source.start(context.currentTime + Math.max(0, Number(delaySec) || 0));
                this.extendableSources.set(name, {
                    source,
                    gainNode,
                    baseGain,
                    endTimeSec: nextEndTimeSec,
                });
            }

            stopExtendable(name) {
                const entry = this.extendableSources.get(name);
                if (!entry) {
                    return;
                }
                this.extendableSources.delete(name);
                try {
                    entry.source.onended = null;
                    entry.source.stop();
                } catch {}
                try {
                    entry.source.disconnect();
                    entry.gainNode.disconnect();
                } catch {}
            }

            cleanupExtendables(currentOutputTimeSec) {
                const currentOutput = Math.max(0, Number(currentOutputTimeSec) || 0);
                for (const [name, entry] of this.extendableSources) {
                    if (currentOutput <= entry.endTimeSec + 0.0001) {
                        continue;
                    }
                    this.stopExtendable(name);
                }
            }

            getStateCode() {
                return this.state;
            }
        }
        Module.__mmwAudio = new WasmAudioTransport();
    });

    EM_ASYNC_JS(int, jsAudioLoadBgm, (const std::uint8_t* dataPtr, int length), {
        jsAudioEnsureEngine();
        try {
            const bytes = HEAPU8.slice(dataPtr, dataPtr + length).buffer;
            await Module.__mmwAudio.loadBgm(bytes);
            return 1;
        } catch (error) {
            Module.__mmwAudio.lastError = String(error && error.message ? error.message : error);
            return 0;
        }
    });

    EM_ASYNC_JS(int, jsAudioLoadSound, (const char* keyPtr, const std::uint8_t* dataPtr, int length), {
        jsAudioEnsureEngine();
        try {
            const key = UTF8ToString(keyPtr);
            const bytes = HEAPU8.slice(dataPtr, dataPtr + length).buffer;
            await Module.__mmwAudio.loadSound(key, bytes);
            return 1;
        } catch (error) {
            Module.__mmwAudio.lastError = String(error && error.message ? error.message : error);
            return 0;
        }
    });

    EM_ASYNC_JS(int, jsAudioUnlock, (), {
        jsAudioEnsureEngine();
        try {
            await Module.__mmwAudio.unlock();
            return 1;
        } catch (error) {
            Module.__mmwAudio.lastError = String(error && error.message ? error.message : error);
            return 0;
        }
    });

    EM_ASYNC_JS(int, jsAudioPlay, (), {
        jsAudioEnsureEngine();
        try {
            return (await Module.__mmwAudio.play()) ? 1 : 0;
        } catch (error) {
            Module.__mmwAudio.lastError = String(error && error.message ? error.message : error);
            return 0;
        }
    });

    EM_JS(void, jsAudioPause, (), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.pause();
    });

    EM_JS(void, jsAudioSeek, (double timeSec), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.seek(timeSec);
    });

    EM_JS(void, jsAudioSetPlaybackRate, (double rate), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.setPlaybackRate(rate);
    });

    EM_JS(void, jsAudioSetVolumes, (double bgmVolume, double soundVolume), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.setAudioVolumes(bgmVolume, soundVolume);
    });

    EM_JS(void, jsAudioSetDuration, (double durationSec), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.setDuration(durationSec);
    });

    EM_JS(void, jsAudioSetStartOffset, (double offsetSec), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.setAudioStartOffset(offsetSec);
    });

    EM_JS(double, jsAudioGetCurrentTime, (), {
        jsAudioEnsureEngine();
        return Module.__mmwAudio.getCurrentTimeSec();
    });

    EM_JS(int, jsAudioGetStateCode, (), {
        jsAudioEnsureEngine();
        return Module.__mmwAudio.getStateCode();
    });

    EM_JS(int, jsAudioRequiresGesture, (), {
        jsAudioEnsureEngine();
        return Module.__mmwAudio.pendingGestureStart ? 1 : 0;
    });

    EM_JS(int, jsAudioHasAudio, (), {
        jsAudioEnsureEngine();
        return Module.__mmwAudio.audioBuffer ? 1 : 0;
    });

    EM_JS(void, jsAudioTriggerOneShot, (const char* keyPtr, double gain, double delaySec), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.trigger(UTF8ToString(keyPtr), gain, delaySec);
    });

    EM_JS(void, jsAudioTriggerExtendable, (const char* keyPtr, double gain, double currentOutputTimeSec, double endTimeSec, double delaySec), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.triggerExtendable(UTF8ToString(keyPtr), gain, currentOutputTimeSec, endTimeSec, delaySec);
    });

    EM_JS(void, jsAudioCleanupExtendables, (double currentOutputTimeSec), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.cleanupExtendables(currentOutputTimeSec);
    });

    EM_JS(void, jsAudioClearOneShots, (), {
        jsAudioEnsureEngine();
        Module.__mmwAudio.clearOneShots();
    });

    EM_JS(const char*, jsAudioGetLastError, (), {
        jsAudioEnsureEngine();
        const text = Module.__mmwAudio.lastError || "";
        const length = lengthBytesUTF8(text) + 1;
        if (Module.__mmwAudio.lastErrorPtr) {
            _free(Module.__mmwAudio.lastErrorPtr);
            Module.__mmwAudio.lastErrorPtr = 0;
        }
        Module.__mmwAudio.lastErrorPtr = _malloc(length);
        stringToUTF8(text, Module.__mmwAudio.lastErrorPtr, length);
        return Module.__mmwAudio.lastErrorPtr;
    });

    struct Args
    {
        std::string susPath;
        std::string outPath;
        std::string assetsDir = "assets/mmw";
        std::string coverPath;
        std::string bgmPath;
        std::string keyAudioPath;
        std::string title;
        std::string lyricist;
        std::string composer;
        std::string arranger;
        std::string vocal;
        std::string difficulty;
        int fps = 60;
        int width = 1920;
        int height = 1080;
        float dpr = 1.0f;
        int normalizedOffsetMs = -9000;
        double maxSeconds = 0.0;
        float effectOpacity = 1.0f;
        bool staticScene = true;
        std::string codec = "h264_videotoolbox";
        int crf = 18;
        std::string preset = "medium";
        std::string hitEventsOutPath;
        bool gui = false;
        bool helpRequested = false;
    };

    [[nodiscard]] std::string shellQuote(const std::string& value)
    {
        std::string out = "'";
        for (const char ch : value) {
            if (ch == '\'') {
                out += "'\\''";
            } else {
                out.push_back(ch);
            }
        }
        out.push_back('\'');
        return out;
    }

    void printUsage()
    {
#ifdef __EMSCRIPTEN__
        return;
#else
        std::cout
            << "Usage: mmw-native-gl-render --sus <path> --out <path> [options]\n"
            << "Options:\n"
            << "  --assets-dir <dir>              (default: assets/mmw)\n"
            << "  --cover <path>                  (optional, build background from jacket)\n"
            << "  --bgm <path>                    (optional, mux bgm into output mp4)\n"
            << "  --key-audio <path>              (optional, mux key/note audio into output mp4)\n"
            << "  --title <text>                  (optional, intro title)\n"
            << "  --lyricist <text>               (optional, intro lyricist)\n"
            << "  --composer <text>               (optional, intro composer)\n"
            << "  --arranger <text>               (optional, intro arranger)\n"
            << "  --vocal <text>                  (optional, intro vocal)\n"
            << "  --difficulty <text>             (optional, intro difficulty)\n"
            << "  --fps <n>                       (default: 60)\n"
            << "  --width <n>                     (default: 1920)\n"
            << "  --height <n>                    (default: 1080)\n"
            << "  --dpr <n>                       (default: 1)\n"
            << "  --normalized-offset-ms <n>      (default: -9000)\n"
            << "  --max-seconds <n>               (default: 0 = full chart)\n"
            << "  --effect-opacity <n>            (default: 1)\n"
            << "  --codec <name>                  (default: h264_videotoolbox)\n"
            << "  --crf <n>                       (default: 18, libx264 only)\n"
            << "  --preset <name>                 (default: medium, libx264 only)\n"
            << "  --hit-events-out <path>         (optional JSON output)\n"
            << "  --gui                           (run realtime GUI preview instead of video export)\n"
            << "  --no-static-scene               (disable background/stage)\n";
#endif
    }

    bool parseArgs(int argc, char** argv, Args& args)
    {
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            auto next = [&]() -> const char* {
                if (i + 1 >= argc) {
                    return nullptr;
                }
                ++i;
                return argv[i];
            };

            if (key == "--sus") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.susPath = value;
            } else if (key == "--out") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.outPath = value;
            } else if (key == "--assets-dir") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.assetsDir = value;
            } else if (key == "--cover") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.coverPath = value;
            } else if (key == "--bgm") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.bgmPath = value;
            } else if (key == "--key-audio") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.keyAudioPath = value;
            } else if (key == "--title") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.title = value;
            } else if (key == "--lyricist") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.lyricist = value;
            } else if (key == "--composer") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.composer = value;
            } else if (key == "--arranger") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.arranger = value;
            } else if (key == "--vocal") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.vocal = value;
            } else if (key == "--difficulty") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.difficulty = value;
            } else if (key == "--fps") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.fps = std::stoi(value);
            } else if (key == "--width") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.width = std::stoi(value);
            } else if (key == "--height") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.height = std::stoi(value);
            } else if (key == "--dpr") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.dpr = std::stof(value);
            } else if (key == "--normalized-offset-ms") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.normalizedOffsetMs = std::stoi(value);
            } else if (key == "--max-seconds") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.maxSeconds = std::stod(value);
            } else if (key == "--effect-opacity") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.effectOpacity = std::stof(value);
            } else if (key == "--codec") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.codec = value;
            } else if (key == "--crf") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.crf = std::stoi(value);
            } else if (key == "--preset") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.preset = value;
            } else if (key == "--hit-events-out") {
                const char* value = next();
                if (!value) {
                    return false;
                }
                args.hitEventsOutPath = value;
            } else if (key == "--gui") {
                args.gui = true;
            } else if (key == "--no-static-scene") {
                args.staticScene = false;
            } else if (key == "--help" || key == "-h") {
                args.helpRequested = true;
                printUsage();
                return false;
            } else {
#ifndef __EMSCRIPTEN__
                std::cerr << "Unknown arg: " << key << "\n";
#endif
                return false;
            }
        }

        const bool hasOutput = args.gui || !args.outPath.empty();
        return !args.susPath.empty() && hasOutput && args.width > 0 && args.height > 0 && args.fps > 0 && args.dpr > 0.0f;
    }

    std::string readFile(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + path);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    GLuint createShader(GLenum type, const char* source)
    {
        std::string normalizedSource = source != nullptr ? std::string(source) : std::string();
        if (!normalizedSource.empty()) {
            const std::string versionToken = "#version";
            const std::size_t versionPos = normalizedSource.find(versionToken);
            if (versionPos != std::string::npos) {
                bool onlyIgnorablePrefix = true;
                for (std::size_t i = 0; i < versionPos; ++i) {
                    const unsigned char ch = static_cast<unsigned char>(normalizedSource[i]);
                    const bool isBomPrefix =
                        ch == 0xEF || ch == 0xBB || ch == 0xBF;
                    if (!std::isspace(ch) && !isBomPrefix) {
                        onlyIgnorablePrefix = false;
                        break;
                    }
                }
                if (onlyIgnorablePrefix && versionPos > 0) {
                    normalizedSource.erase(0, versionPos);
                }
            }
        }

        const char* shaderSource = normalizedSource.empty() ? source : normalizedSource.c_str();
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &shaderSource, nullptr);
        glCompileShader(shader);

        GLint status = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
            char log[2048]{};
            glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), nullptr, log);
            glDeleteShader(shader);
            throw std::runtime_error(std::string("Shader compile failed: ") + log);
        }

        return shader;
    }

    GLuint createProgram(const char* vertexSource, const char* fragmentSource)
    {
        const GLuint vertexShader = createShader(GL_VERTEX_SHADER, vertexSource);
        const GLuint fragmentShader = createShader(GL_FRAGMENT_SHADER, fragmentSource);

        const GLuint program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        GLint status = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &status);
        if (status != GL_TRUE) {
            char log[2048]{};
            glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(log)), nullptr, log);
            glDeleteProgram(program);
            throw std::runtime_error(std::string("Program link failed: ") + log);
        }

        return program;
    }

    [[nodiscard]] const BinaryBlob& requireBinaryBlob(
        const std::unordered_map<std::string, BinaryBlob>& store,
        const std::string& key)
    {
        const auto it = store.find(key);
        if (it == store.end() || it->second.bytes.empty()) {
            throw std::runtime_error("Missing binary resource: " + key);
        }
        return it->second;
    }

    Texture loadTextureFromMemory(const std::uint8_t* bytes, int length)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(bytes, length, &width, &height, &channels, 4);
        if (!pixels || width <= 0 || height <= 0) {
            throw std::runtime_error("Failed to load texture from memory");
        }

        GLuint textureId = 0;
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        stbi_image_free(pixels);

        Texture texture;
        texture.id = textureId;
        texture.width = width;
        texture.height = height;
        return texture;
    }

    Texture loadTexture(const std::filesystem::path& path)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            throw std::runtime_error("Failed to load texture: " + path.string());
        }

        GLuint textureId = 0;
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        stbi_image_free(pixels);

        Texture texture;
        texture.id = textureId;
        texture.width = width;
        texture.height = height;
        return texture;
    }

    Texture loadTexture(const std::unordered_map<std::string, BinaryBlob>& store, const std::string& key)
    {
        const BinaryBlob& blob = requireBinaryBlob(store, key);
        return loadTextureFromMemory(blob.bytes.data(), static_cast<int>(blob.bytes.size()));
    }

    Texture createTextureFromPixels(const std::uint8_t* pixels, int width, int height)
    {
        if (!pixels || width <= 0 || height <= 0) {
            throw std::runtime_error("Invalid pixel buffer for texture creation");
        }

        GLuint textureId = 0;
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        Texture texture;
        texture.id = textureId;
        texture.width = width;
        texture.height = height;
        return texture;
    }

    struct Point
    {
        int x = 0;
        int y = 0;
    };

    struct RgbaImage
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> data;
    };

    [[nodiscard]] RgbaImage blankImage(int width, int height)
    {
        return RgbaImage{
            std::max(1, width),
            std::max(1, height),
            std::vector<std::uint8_t>(static_cast<size_t>(std::max(1, width)) * static_cast<size_t>(std::max(1, height)) * 4u, 0u),
        };
    }

    [[nodiscard]] RgbaImage cloneImage(const RgbaImage& source)
    {
        return RgbaImage{source.width, source.height, source.data};
    }

    [[nodiscard]] RgbaImage loadImage(const std::filesystem::path& path)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (!pixels || width <= 0 || height <= 0) {
            throw std::runtime_error("Failed to load image: " + path.string());
        }
        RgbaImage image;
        image.width = width;
        image.height = height;
        image.data.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        std::memcpy(image.data.data(), pixels, image.data.size());
        stbi_image_free(pixels);
        return image;
    }

    [[nodiscard]] RgbaImage loadImageFromMemory(const std::uint8_t* bytes, int length, const std::string& key)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(bytes, length, &width, &height, &channels, 4);
        if (!pixels || width <= 0 || height <= 0) {
            throw std::runtime_error("Failed to load image from memory: " + key);
        }
        RgbaImage image;
        image.width = width;
        image.height = height;
        image.data.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        std::memcpy(image.data.data(), pixels, image.data.size());
        stbi_image_free(pixels);
        return image;
    }

    [[nodiscard]] RgbaImage loadImage(const std::unordered_map<std::string, BinaryBlob>& store, const std::string& key)
    {
        const BinaryBlob& blob = requireBinaryBlob(store, key);
        return loadImageFromMemory(blob.bytes.data(), static_cast<int>(blob.bytes.size()), key);
    }

    [[nodiscard]] RgbaImage resizeNearest(const RgbaImage& source, int targetWidth, int targetHeight)
    {
        RgbaImage target = blankImage(targetWidth, targetHeight);
        for (int y = 0; y < target.height; ++y) {
            const int sourceY = std::min(source.height - 1, (y * source.height) / target.height);
            for (int x = 0; x < target.width; ++x) {
                const int sourceX = std::min(source.width - 1, (x * source.width) / target.width);
                const size_t srcOffset = (static_cast<size_t>(sourceY) * static_cast<size_t>(source.width) + static_cast<size_t>(sourceX)) * 4u;
                const size_t dstOffset = (static_cast<size_t>(y) * static_cast<size_t>(target.width) + static_cast<size_t>(x)) * 4u;
                target.data[dstOffset + 0] = source.data[srcOffset + 0];
                target.data[dstOffset + 1] = source.data[srcOffset + 1];
                target.data[dstOffset + 2] = source.data[srcOffset + 2];
                target.data[dstOffset + 3] = source.data[srcOffset + 3];
            }
        }
        return target;
    }

    void overlayImage(RgbaImage& base, const RgbaImage& top, int offsetX, int offsetY)
    {
        for (int y = 0; y < top.height; ++y) {
            const int targetY = y + offsetY;
            if (targetY < 0 || targetY >= base.height) {
                continue;
            }
            for (int x = 0; x < top.width; ++x) {
                const int targetX = x + offsetX;
                if (targetX < 0 || targetX >= base.width) {
                    continue;
                }
                const size_t srcIndex = (static_cast<size_t>(y) * static_cast<size_t>(top.width) + static_cast<size_t>(x)) * 4u;
                const size_t dstIndex = (static_cast<size_t>(targetY) * static_cast<size_t>(base.width) + static_cast<size_t>(targetX)) * 4u;
                const int srcA = top.data[srcIndex + 3];
                if (srcA <= 0) {
                    continue;
                }
                const int dstA = base.data[dstIndex + 3];
                if (dstA <= 0 || srcA >= 255) {
                    base.data[dstIndex + 0] = top.data[srcIndex + 0];
                    base.data[dstIndex + 1] = top.data[srcIndex + 1];
                    base.data[dstIndex + 2] = top.data[srcIndex + 2];
                    base.data[dstIndex + 3] = static_cast<std::uint8_t>(srcA);
                    continue;
                }

                const float srcAF = static_cast<float>(srcA) / 255.0f;
                const float dstAF = static_cast<float>(dstA) / 255.0f;
                const float outA = srcAF + dstAF * (1.0f - srcAF);
                if (outA <= 1e-8f) {
                    continue;
                }

                const float srcR = static_cast<float>(top.data[srcIndex + 0]) / 255.0f;
                const float srcG = static_cast<float>(top.data[srcIndex + 1]) / 255.0f;
                const float srcB = static_cast<float>(top.data[srcIndex + 2]) / 255.0f;
                const float dstR = static_cast<float>(base.data[dstIndex + 0]) / 255.0f;
                const float dstG = static_cast<float>(base.data[dstIndex + 1]) / 255.0f;
                const float dstB = static_cast<float>(base.data[dstIndex + 2]) / 255.0f;

                const float outR = (srcR * srcAF + dstR * dstAF * (1.0f - srcAF)) / outA;
                const float outG = (srcG * srcAF + dstG * dstAF * (1.0f - srcAF)) / outA;
                const float outB = (srcB * srcAF + dstB * dstAF * (1.0f - srcAF)) / outA;

                base.data[dstIndex + 0] = static_cast<std::uint8_t>(std::clamp(std::lround(outR * 255.0f), 0l, 255l));
                base.data[dstIndex + 1] = static_cast<std::uint8_t>(std::clamp(std::lround(outG * 255.0f), 0l, 255l));
                base.data[dstIndex + 2] = static_cast<std::uint8_t>(std::clamp(std::lround(outB * 255.0f), 0l, 255l));
                base.data[dstIndex + 3] = static_cast<std::uint8_t>(std::clamp(std::lround(outA * 255.0f), 0l, 255l));
            }
        }
    }

    [[nodiscard]] RgbaImage applyAlphaMask(const RgbaImage& image, const RgbaImage& mask)
    {
        RgbaImage out = cloneImage(image);
        const size_t length = std::min(out.data.size(), mask.data.size());
        for (size_t i = 3; i < length; i += 4) {
            out.data[i] = std::min(out.data[i], mask.data[i]);
        }
        return out;
    }

    [[nodiscard]] std::array<double, 8> solveLinear8(std::array<std::array<double, 8>, 8> matrix, std::array<double, 8> values, bool& ok)
    {
        ok = true;
        for (int pivot = 0; pivot < 8; ++pivot) {
            int maxRow = pivot;
            double maxValue = std::fabs(matrix[pivot][pivot]);
            for (int row = pivot + 1; row < 8; ++row) {
                const double value = std::fabs(matrix[row][pivot]);
                if (value > maxValue) {
                    maxValue = value;
                    maxRow = row;
                }
            }
            if (maxValue < 1e-8) {
                ok = false;
                return {};
            }
            if (maxRow != pivot) {
                std::swap(matrix[pivot], matrix[maxRow]);
                std::swap(values[pivot], values[maxRow]);
            }
            const double pivotValue = matrix[pivot][pivot];
            for (int col = pivot; col < 8; ++col) {
                matrix[pivot][col] /= pivotValue;
            }
            values[pivot] /= pivotValue;
            for (int row = 0; row < 8; ++row) {
                if (row == pivot) {
                    continue;
                }
                const double factor = matrix[row][pivot];
                if (std::fabs(factor) < 1e-8) {
                    continue;
                }
                for (int col = pivot; col < 8; ++col) {
                    matrix[row][col] -= factor * matrix[pivot][col];
                }
                values[row] -= factor * values[pivot];
            }
        }
        return values;
    }

    [[nodiscard]] std::array<double, 9> buildHomography(
        const std::array<Point, 4>& source,
        const std::array<Point, 4>& target,
        bool& ok)
    {
        std::array<std::array<double, 8>, 8> matrix{};
        std::array<double, 8> values{};
        for (int i = 0; i < 4; ++i) {
            const double sx = static_cast<double>(source[i].x);
            const double sy = static_cast<double>(source[i].y);
            const double tx = static_cast<double>(target[i].x);
            const double ty = static_cast<double>(target[i].y);
            matrix[i * 2 + 0] = {sx, sy, 1.0, 0.0, 0.0, 0.0, -sx * tx, -sy * tx};
            matrix[i * 2 + 1] = {0.0, 0.0, 0.0, sx, sy, 1.0, -sx * ty, -sy * ty};
            values[i * 2 + 0] = tx;
            values[i * 2 + 1] = ty;
        }

        bool solvedOk = false;
        const auto solved = solveLinear8(matrix, values, solvedOk);
        if (!solvedOk) {
            ok = false;
            return {};
        }
        ok = true;
        return {solved[0], solved[1], solved[2], solved[3], solved[4], solved[5], solved[6], solved[7], 1.0};
    }

    [[nodiscard]] std::array<double, 9> invert3x3(const std::array<double, 9>& m, bool& ok)
    {
        const double a = m[0], b = m[1], c = m[2];
        const double d = m[3], e = m[4], f = m[5];
        const double g = m[6], h = m[7], i = m[8];
        const double A = e * i - f * h;
        const double B = -(d * i - f * g);
        const double C = d * h - e * g;
        const double D = -(b * i - c * h);
        const double E = a * i - c * g;
        const double F = -(a * h - b * g);
        const double G = b * f - c * e;
        const double H = -(a * f - c * d);
        const double I = a * e - b * d;
        const double det = a * A + b * B + c * C;
        if (std::fabs(det) < 1e-10) {
            ok = false;
            return {};
        }
        const double r = 1.0 / det;
        ok = true;
        return {A * r, D * r, G * r, B * r, E * r, H * r, C * r, F * r, I * r};
    }

    [[nodiscard]] bool projectPoint(const std::array<double, 9>& m, double x, double y, double& outX, double& outY)
    {
        const double denominator = m[6] * x + m[7] * y + m[8];
        if (std::fabs(denominator) < 1e-8) {
            return false;
        }
        outX = (m[0] * x + m[1] * y + m[2]) / denominator;
        outY = (m[3] * x + m[4] * y + m[5]) / denominator;
        return true;
    }

    [[nodiscard]] RgbaImage morph(const RgbaImage& source, const std::array<Point, 4>& quad, int targetWidth, int targetHeight)
    {
        const int minX = std::min({quad[0].x, quad[1].x, quad[2].x, quad[3].x});
        const int minY = std::min({quad[0].y, quad[1].y, quad[2].y, quad[3].y});
        const int maxX = std::max({quad[0].x, quad[1].x, quad[2].x, quad[3].x});
        const int maxY = std::max({quad[0].y, quad[1].y, quad[2].y, quad[3].y});
        const int width = std::max(1, maxX - minX);
        const int height = std::max(1, maxY - minY);
        const RgbaImage minImage = resizeNearest(source, width, height);

        const std::array<Point, 4> localQuad{
            Point{quad[0].x - minX, quad[0].y - minY},
            Point{quad[1].x - minX, quad[1].y - minY},
            Point{quad[2].x - minX, quad[2].y - minY},
            Point{quad[3].x - minX, quad[3].y - minY},
        };
        const std::array<Point, 4> sourcePoints{
            Point{0, 0},
            Point{width, 0},
            Point{0, height},
            Point{width, height},
        };

        bool ok = false;
        const auto projection = buildHomography(sourcePoints, localQuad, ok);
        if (!ok) {
            return blankImage(targetWidth, targetHeight);
        }
        const auto inverse = invert3x3(projection, ok);
        if (!ok) {
            return blankImage(targetWidth, targetHeight);
        }

        RgbaImage projected = blankImage(width, height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                double sx = 0.0;
                double sy = 0.0;
                if (!projectPoint(inverse, static_cast<double>(x), static_cast<double>(y), sx, sy)) {
                    continue;
                }
                const int sampleX = static_cast<int>(std::lround(sx));
                const int sampleY = static_cast<int>(std::lround(sy));
                if (sampleX < 0 || sampleX >= width || sampleY < 0 || sampleY >= height) {
                    continue;
                }
                const size_t srcOffset = (static_cast<size_t>(sampleY) * static_cast<size_t>(width) + static_cast<size_t>(sampleX)) * 4u;
                const size_t dstOffset = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
                projected.data[dstOffset + 0] = minImage.data[srcOffset + 0];
                projected.data[dstOffset + 1] = minImage.data[srcOffset + 1];
                projected.data[dstOffset + 2] = minImage.data[srcOffset + 2];
                projected.data[dstOffset + 3] = minImage.data[srcOffset + 3];
            }
        }

        RgbaImage target = blankImage(targetWidth, targetHeight);
        overlayImage(target, projected, minX, minY);
        return target;
    }

    [[nodiscard]] RgbaImage renderToSquareBackground(const RgbaImage& rendered, int size)
    {
        const int outSize = std::max(1, size);
        RgbaImage out = blankImage(outSize, outSize);
        const int centerX = (outSize - rendered.width) / 2;
        const int centerY = (outSize - rendered.height) / 2;
        auto wrap = [](int value, int mod) -> int {
            const int m = std::max(1, mod);
            int r = value % m;
            if (r < 0) {
                r += m;
            }
            return r;
        };

        for (int y = 0; y < outSize; ++y) {
            for (int x = 0; x < outSize; ++x) {
                const int sx = wrap(x - centerX, rendered.width);
                const int sy = wrap(y - centerY, rendered.height);
                const size_t dstOffset = (static_cast<size_t>(y) * static_cast<size_t>(outSize) + static_cast<size_t>(x)) * 4u;
                const size_t srcOffset = (static_cast<size_t>(sy) * static_cast<size_t>(rendered.width) + static_cast<size_t>(sx)) * 4u;
                out.data[dstOffset + 0] = rendered.data[srcOffset + 0];
                out.data[dstOffset + 1] = rendered.data[srcOffset + 1];
                out.data[dstOffset + 2] = rendered.data[srcOffset + 2];
                out.data[dstOffset + 3] = rendered.data[srcOffset + 3];
            }
        }

        overlayImage(out, rendered, centerX, centerY);
        return out;
    }

    [[nodiscard]] RgbaImage composeOverlayBackgroundV3(
        const RgbaImage& cover,
        const std::unordered_map<std::string, BinaryBlob>& assetStore)
    {
        const auto base = loadImage(assetStore, "overlay/bggen/v3/base.png");
        const auto bottom = loadImage(assetStore, "overlay/bggen/v3/bottom.png");
        const auto centerCover = loadImage(assetStore, "overlay/bggen/v3/center_cover.png");
        const auto centerMask = loadImage(assetStore, "overlay/bggen/v3/center_mask.png");
        const auto sideCover = loadImage(assetStore, "overlay/bggen/v3/side_cover.png");
        const auto sideMask = loadImage(assetStore, "overlay/bggen/v3/side_mask.png");
        const auto windows = loadImage(assetStore, "overlay/bggen/v3/windows.png");

        const std::array<Point, 4> MORPH_LEFT_NORMAL{{{566, 161}, {1183, 134}, {633, 731}, {1226, 682}}};
        const std::array<Point, 4> MORPH_RIGHT_NORMAL{{{966, 104}, {1413, 72}, {954, 525}, {1390, 524}}};
        const std::array<Point, 4> MORPH_LEFT_MIRROR{{{633, 1071}, {1256, 1045}, {598, 572}, {1197, 569}}};
        const std::array<Point, 4> MORPH_RIGHT_MIRROR{{{954, 1122}, {1393, 1167}, {942, 702}, {1366, 717}}};
        const std::array<Point, 4> MORPH_CENTER_NORMAL{{{824, 227}, {1224, 227}, {833, 608}, {1216, 608}}};
        const std::array<Point, 4> MORPH_CENTER_MIRROR{{{830, 1017}, {1214, 1017}, {833, 676}, {1216, 676}}};

        RgbaImage sideJackets = blankImage(base.width, base.height);
        overlayImage(sideJackets, morph(cover, MORPH_LEFT_NORMAL, base.width, base.height), 0, 0);
        overlayImage(sideJackets, morph(cover, MORPH_RIGHT_NORMAL, base.width, base.height), 0, 0);
        overlayImage(sideJackets, morph(cover, MORPH_LEFT_MIRROR, base.width, base.height), 0, 0);
        overlayImage(sideJackets, morph(cover, MORPH_RIGHT_MIRROR, base.width, base.height), 0, 0);
        overlayImage(sideJackets, sideCover, 0, 0);

        RgbaImage center = blankImage(base.width, base.height);
        overlayImage(center, morph(cover, MORPH_CENTER_NORMAL, base.width, base.height), 0, 0);
        overlayImage(center, morph(cover, MORPH_CENTER_MIRROR, base.width, base.height), 0, 0);
        overlayImage(center, centerCover, 0, 0);

        const RgbaImage maskedSide = applyAlphaMask(sideJackets, sideMask);
        const RgbaImage maskedCenter = applyAlphaMask(center, centerMask);

        RgbaImage rendered = cloneImage(base);
        overlayImage(rendered, maskedSide, 0, 0);
        overlayImage(rendered, sideCover, 0, 0);
        overlayImage(rendered, windows, 0, 0);
        overlayImage(rendered, maskedCenter, 0, 0);
        overlayImage(rendered, bottom, 0, 0);

        return renderToSquareBackground(rendered, base.width);
    }

    class WorldToClip
    {
      public:
        WorldToClip(int width, int height)
        {
            const float sourceAspect = static_cast<float>(width) / static_cast<float>(height);
            const float targetAspect = STAGE_TARGET_WIDTH / STAGE_TARGET_HEIGHT;
            if (targetAspect < sourceAspect) {
                mFillWidth = sourceAspect * STAGE_TARGET_HEIGHT;
                mFillHeight = STAGE_TARGET_HEIGHT;
            } else {
                mFillWidth = STAGE_TARGET_WIDTH;
                mFillHeight = STAGE_TARGET_WIDTH / sourceAspect;
            }
        }

        Vec2 convert(float worldX, float worldY) const
        {
            const float scaledWidth = STAGE_TARGET_WIDTH * STAGE_WIDTH_RATIO;
            const float scaledHeight = STAGE_TARGET_HEIGHT * STAGE_HEIGHT_RATIO;
            const float screenTop = STAGE_TARGET_HEIGHT * STAGE_TOP_RATIO;
            const float x = (2.0f * worldX * scaledWidth) / mFillWidth;
            const float y = (-2.0f * (worldY * scaledHeight - screenTop)) / mFillHeight;
            return Vec2{x, y};
        }

      private:
        float mFillWidth = STAGE_TARGET_WIDTH;
        float mFillHeight = STAGE_TARGET_HEIGHT;
    };

    class GlRenderer
    {
      public:
        GlRenderer(const std::string& canvasSelector, int width, int height, float dpr, float effectOpacity)
            : mCssWidth(std::max(1, width)),
              mCssHeight(std::max(1, height)),
              mDpr(std::max(0.1f, dpr)),
              mEffectOpacity(effectOpacity),
              mCanvasSelector(canvasSelector),
              mWorldToClip(width, height)
        {
            initContext();
            resize(width, height, dpr);
            initGlObjects();
        }

        ~GlRenderer()
        {
            cleanup();
        }

        void resize(int width, int height, float dpr)
        {
            mCssWidth = std::max(1, width);
            mCssHeight = std::max(1, height);
            mDpr = std::max(0.1f, dpr);
            mPixelWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(mCssWidth) * mDpr)));
            mPixelHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(mCssHeight) * mDpr)));
            mWorldToClip = WorldToClip(mCssWidth, mCssHeight);
            emscripten_set_canvas_element_size(mCanvasSelector.c_str(), mPixelWidth, mPixelHeight);
        }

        void setEffectOpacity(float effectOpacity)
        {
            mEffectOpacity = std::clamp(effectOpacity, 0.0f, 1.0f);
        }

        void setBackgroundBrightness(float backgroundBrightness)
        {
            mBackgroundBrightness = std::clamp(backgroundBrightness, 0.0f, 1.0f);
        }

        void setStageOpacity(float stageOpacity)
        {
            mStageOpacity = std::clamp(stageOpacity, 0.0f, 1.0f);
        }

        void setStageCover(float stageCover)
        {
            mStageCover = std::clamp(stageCover, 0.0f, 1.0f);
        }

        void loadNoteSkinTextures(const std::unordered_map<std::string, BinaryBlob>& assetStore, int noteSkin)
        {
            const int resolvedNoteSkin = noteSkin == 1 ? 1 : 0;
            if (mActiveNoteSkin == resolvedNoteSkin && mNotes.id != 0 && mLongNoteLine.id != 0 && mTouchLine.id != 0) {
                return;
            }

            destroyTexture(mNotes);
            destroyTexture(mLongNoteLine);
            destroyTexture(mTouchLine);
            mNotes = loadTexture(assetStore, resolvedNoteSkin == 1 ? "notes_02.png" : "notes_01.png");
            mLongNoteLine = loadTexture(assetStore, resolvedNoteSkin == 1 ? "longNoteLine_02.png" : "longNoteLine_01.png");
            mTouchLine = loadTexture(assetStore, resolvedNoteSkin == 1 ? "touchLine_eff_02.png" : "touchLine_eff_01.png");
            mActiveNoteSkin = resolvedNoteSkin;
        }

        void loadAllTextures(
            const std::unordered_map<std::string, BinaryBlob>& assetStore,
            const BinaryBlob* coverBlob,
            int noteSkin)
        {
            destroyTexture(mBackground);
            destroyTexture(mStage);
            destroyTexture(mEffect);
            mActiveNoteSkin = -1;

            if (coverBlob && !coverBlob->bytes.empty()) {
                const auto cover = loadImageFromMemory(
                    coverBlob->bytes.data(),
                    static_cast<int>(coverBlob->bytes.size()),
                    "__session/cover");
                const auto composed = composeOverlayBackgroundV3(cover, assetStore);
                mBackground = createTextureFromPixels(composed.data.data(), composed.width, composed.height);
            } else {
                mBackground = loadTexture(assetStore, "background_overlay.png");
            }
            mStage = loadTexture(assetStore, "stage.png");
            loadNoteSkinTextures(assetStore, noteSkin);
            mEffect = loadTexture(assetStore, "effect.png");

            buildStaticVertices();
        }

        void renderFrame(const float* packedQuads, int quadCount, bool drawStaticScene, float playfieldVisibility = 1.0f)
        {
            const auto viewport = previewViewportRect();
            glViewport(0, 0, viewport.fullWidth, viewport.fullHeight);
            glClearColor(0.03f, 0.03f, 0.05f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glViewport(viewport.x, viewport.y, viewport.width, viewport.height);

            const float visibility = std::max(0.0f, std::min(1.0f, playfieldVisibility));
            if (drawStaticScene) {
                std::vector<float> backgroundVertices = mStaticBackgroundVertices;
                const float brightness = mBackgroundBrightness;
                for (size_t i = 4; i < backgroundVertices.size(); i += FLOATS_PER_VERTEX) {
                    backgroundVertices[i + 0] *= brightness;
                    backgroundVertices[i + 1] *= brightness;
                    backgroundVertices[i + 2] *= brightness;
                }
                drawVertices(mBackground, backgroundVertices, false, BlendMode::Normal);
                if (visibility > 0.001f && mStageOpacity > 0.001f) {
                    std::vector<float> stageVertices = mStaticStageVertices;
                    const float stageAlpha = visibility * mStageOpacity;
                    for (size_t i = 7; i < stageVertices.size(); i += FLOATS_PER_VERTEX) {
                        stageVertices[i] *= stageAlpha;
                    }
                    drawVertices(mStage, stageVertices, false, BlendMode::Normal);
                }
            }

            mRuntimeVisibility = visibility;
            applyRuntimeStageCoverScissor(viewport);
            drawRuntime(packedQuads, quadCount);
            clearRuntimeStageCoverScissor();
            mRuntimeVisibility = 1.0f;
            drawStageCover(visibility);
        }

        [[nodiscard]] std::pair<int, int> framebufferSize() const
        {
            const auto viewport = previewViewportRect();
            return {viewport.width, viewport.height};
        }

        [[nodiscard]] std::array<int, 4> previewRect() const
        {
            const auto viewport = previewViewportRect();
            return {viewport.x, viewport.y, viewport.width, viewport.height};
        }

        [[nodiscard]] std::array<int, 4> previewRectWindow() const
        {
            return {0, 0, mPixelWidth, mPixelHeight};
        }

        void present()
        {
            glFlush();
        }

      private:
        int mCssWidth = 0;
        int mCssHeight = 0;
        int mPixelWidth = 0;
        int mPixelHeight = 0;
        float mDpr = 1.0f;
        float mEffectOpacity = 1.0f;
        float mBackgroundBrightness = 1.0f;
        float mStageOpacity = 1.0f;
        float mStageCover = 0.0f;
        std::string mCanvasSelector;
        WorldToClip mWorldToClip;
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE mContext = 0;
        GLuint mProgram = 0;
        GLuint mEffectProgram = 0;
        GLuint mVao = 0;
        GLuint mVbo = 0;

        Texture mBackground;
        Texture mStage;
        Texture mNotes;
        Texture mLongNoteLine;
        Texture mTouchLine;
        Texture mEffect;
        int mActiveNoteSkin = -1;

        std::vector<float> mStaticBackgroundVertices;
        std::vector<float> mStaticStageVertices;
        float mRuntimeVisibility = 1.0f;

        struct ViewportRect
        {
            int x = 0;
            int y = 0;
            int width = 1;
            int height = 1;
            int fullWidth = 1;
            int fullHeight = 1;
        };

        [[nodiscard]] ViewportRect previewViewportRect() const
        {
            ViewportRect rect;
            rect.x = 0;
            rect.y = 0;
            rect.width = std::max(1, mPixelWidth);
            rect.height = std::max(1, mPixelHeight);
            rect.fullWidth = rect.width;
            rect.fullHeight = rect.height;
            return rect;
        }

        void initContext()
        {
            EmscriptenWebGLContextAttributes attributes;
            emscripten_webgl_init_context_attributes(&attributes);
            attributes.alpha = false;
            attributes.depth = false;
            attributes.stencil = false;
            attributes.antialias = true;
            attributes.premultipliedAlpha = false;
            attributes.enableExtensionsByDefault = true;
            attributes.majorVersion = 2;
            attributes.minorVersion = 0;

            mContext = emscripten_webgl_create_context(mCanvasSelector.c_str(), &attributes);
            if (mContext <= 0) {
                throw std::runtime_error("emscripten_webgl_create_context failed");
            }
            if (emscripten_webgl_make_context_current(mContext) != EMSCRIPTEN_RESULT_SUCCESS) {
                throw std::runtime_error("emscripten_webgl_make_context_current failed");
            }
        }

        void initGlObjects()
        {
            static constexpr const char* kVertexShader = R"GLSL(#version 300 es
precision mediump float;
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUv;
layout (location = 2) in vec4 aColor;
layout (location = 3) in float aReciprocalW;
out vec2 vUv;
out vec4 vColor;
void main() {
    vUv = aUv;
    vColor = aColor;
    float w = aReciprocalW != 0.0 ? (1.0 / aReciprocalW) : 1.0;
gl_Position = vec4(aPos * w, 0.0, w);
}
)GLSL";

            static constexpr const char* kFragmentShader = R"GLSL(#version 300 es
precision mediump float;
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 outColor;
void main() {
outColor = texture(uTexture, vUv) * vColor;
}
)GLSL";

            static constexpr const char* kEffectFragmentShader = R"GLSL(#version 300 es
precision mediump float;
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 outColor;
void main() {
    vec4 texColor = texture(uTexture, vUv) * vColor;
    float alpha = texColor.a;
    outColor = vec4(texColor.rgb * texColor.aaa, alpha);
}
)GLSL";

            mProgram = createProgram(kVertexShader, kFragmentShader);
            mEffectProgram = createProgram(kVertexShader, kEffectFragmentShader);

            glUseProgram(mProgram);
            glUniform1i(glGetUniformLocation(mProgram, "uTexture"), 0);
            glUseProgram(mEffectProgram);
            glUniform1i(glGetUniformLocation(mEffectProgram, "uTexture"), 0);

            glGenVertexArrays(1, &mVao);
            glGenBuffers(1, &mVbo);
            glBindVertexArray(mVao);
            glBindBuffer(GL_ARRAY_BUFFER, mVbo);
            glBufferData(GL_ARRAY_BUFFER, 1024, nullptr, GL_DYNAMIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * static_cast<GLsizei>(sizeof(float)), reinterpret_cast<void*>(0));

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * static_cast<GLsizei>(sizeof(float)), reinterpret_cast<void*>(2 * sizeof(float)));

            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * static_cast<GLsizei>(sizeof(float)), reinterpret_cast<void*>(4 * sizeof(float)));

            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, FLOATS_PER_VERTEX * static_cast<GLsizei>(sizeof(float)), reinterpret_cast<void*>(8 * sizeof(float)));

            glBindVertexArray(0);

            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }

        void buildStaticVertices()
        {
            mStaticBackgroundVertices.clear();
            mStaticStageVertices.clear();

            buildWorldQuad(
                mStaticBackgroundVertices,
                mBackground,
                {
                    Vec2{WORLD_BACKGROUND_LEFT + WORLD_BACKGROUND_WIDTH, WORLD_BACKGROUND_TOP},
                    Vec2{WORLD_BACKGROUND_LEFT + WORLD_BACKGROUND_WIDTH, WORLD_BACKGROUND_TOP + WORLD_BACKGROUND_HEIGHT},
                    Vec2{WORLD_BACKGROUND_LEFT, WORLD_BACKGROUND_TOP + WORLD_BACKGROUND_HEIGHT},
                    Vec2{WORLD_BACKGROUND_LEFT, WORLD_BACKGROUND_TOP},
                },
                Rect{0.0f, 0.0f, static_cast<float>(mBackground.width), static_cast<float>(mBackground.height)},
                Color{1.0f, 1.0f, 1.0f, 1.0f});

            buildWorldQuad(
                mStaticStageVertices,
                mStage,
                {
                    Vec2{WORLD_STAGE_LEFT + WORLD_STAGE_WIDTH, WORLD_STAGE_TOP},
                    Vec2{WORLD_STAGE_LEFT + WORLD_STAGE_WIDTH, WORLD_STAGE_TOP + WORLD_STAGE_HEIGHT},
                    Vec2{WORLD_STAGE_LEFT, WORLD_STAGE_TOP + WORLD_STAGE_HEIGHT},
                    Vec2{WORLD_STAGE_LEFT, WORLD_STAGE_TOP},
                },
                Rect{0.0f, 0.0f, 2048.0f, 1176.0f},
                Color{1.0f, 1.0f, 1.0f, 1.0f});
        }

        void buildWorldQuad(
            std::vector<float>& out,
            const Texture& texture,
            const std::array<Vec2, 4>& points,
            const Rect& sprite,
            const Color& color)
        {
            const std::array<Vec2, 4> clip{
                mWorldToClip.convert(points[0].x, points[0].y),
                mWorldToClip.convert(points[1].x, points[1].y),
                mWorldToClip.convert(points[2].x, points[2].y),
                mWorldToClip.convert(points[3].x, points[3].y),
            };

            const std::array<Vec2, 4> uv{
                Vec2{sprite.x2 / static_cast<float>(texture.width), sprite.y1 / static_cast<float>(texture.height)},
                Vec2{sprite.x2 / static_cast<float>(texture.width), sprite.y2 / static_cast<float>(texture.height)},
                Vec2{sprite.x1 / static_cast<float>(texture.width), sprite.y2 / static_cast<float>(texture.height)},
                Vec2{sprite.x1 / static_cast<float>(texture.width), sprite.y1 / static_cast<float>(texture.height)},
            };

            pushVertex(out, clip[0], uv[0], color, 1.0f);
            pushVertex(out, clip[1], uv[1], color, 1.0f);
            pushVertex(out, clip[2], uv[2], color, 1.0f);
            pushVertex(out, clip[0], uv[0], color, 1.0f);
            pushVertex(out, clip[2], uv[2], color, 1.0f);
            pushVertex(out, clip[3], uv[3], color, 1.0f);
        }

        void drawStageCover(float visibility)
        {
            if (mStage.id == 0 || mStageCover <= 0.001f || mStageOpacity <= 0.001f || visibility <= 0.001f) {
                return;
            }

            const float coverBottom = WORLD_STAGE_TOP + mStageCover * (1.0f - WORLD_STAGE_TOP);
            const float spriteHeight = mStageCover * (STAGE_LANE_HEIGHT - STAGE_LANE_TOP);
            std::vector<float> coverVertices;
            coverVertices.reserve(6 * FLOATS_PER_VERTEX);
            buildWorldQuad(
                coverVertices,
                mStage,
                {
                    Vec2{WORLD_STAGE_LEFT + WORLD_STAGE_WIDTH, WORLD_STAGE_TOP},
                    Vec2{WORLD_STAGE_LEFT + WORLD_STAGE_WIDTH, coverBottom},
                    Vec2{WORLD_STAGE_LEFT, coverBottom},
                    Vec2{WORLD_STAGE_LEFT, WORLD_STAGE_TOP},
                },
                Rect{0.0f, 0.0f, STAGE_TEX_WIDTH, spriteHeight},
                Color{0.0f, 0.0f, 0.0f, mStageOpacity * visibility});
            drawVertices(mStage, coverVertices, false, BlendMode::Normal);
            drawStageCoverDecoration(visibility);
        }

        void drawStageCoverDecoration(float visibility)
        {
            if (mNotes.id == 0 || mStageCover <= 0.001f || visibility <= 0.001f) {
                return;
            }

            constexpr size_t transformIndex = 16;
            constexpr size_t spriteIndex = 36;
            constexpr float noteHeight = 75.0f / STAGE_LANE_HEIGHT / 2.0f;
            const auto& sprite = mmw_preview::kNoteSprites[spriteIndex];
            const float margin = 0.12f * (1.0f - mStageCover);
            const float scale = WORLD_STAGE_TOP + mStageCover * (1.0f - WORLD_STAGE_TOP);
            const auto quad = scaleQuad(
                applySpriteTransform(
                    transformIndex,
                    perspectiveQuad(-6.0f - margin, 6.0f + margin, 1.0f + noteHeight, 1.0f - noteHeight)),
                scale);

            std::vector<float> lineVertices;
            lineVertices.reserve(6 * FLOATS_PER_VERTEX);
            buildWorldQuad(
                lineVertices,
                mNotes,
                quad,
                Rect{sprite.x1, sprite.y1, sprite.x2, sprite.y2},
                Color{1.0f, 1.0f, 1.0f, visibility});
            drawVertices(mNotes, lineVertices, false, BlendMode::Normal);
        }

        [[nodiscard]] std::array<Vec2, 4> perspectiveQuad(float left, float right, float top, float bottom) const
        {
            return {{
                Vec2{right * top, top},
                Vec2{right * bottom, bottom},
                Vec2{left * bottom, bottom},
                Vec2{left * top, top},
            }};
        }

        [[nodiscard]] std::array<float, 4> mulRowVec(
            const std::array<float, 64>& transform,
            size_t offset,
            const std::array<float, 4>& vector) const
        {
            std::array<float, 4> output{};
            for (size_t column = 0; column < 4; ++column) {
                for (size_t row = 0; row < 4; ++row) {
                    output[column] += vector[row] * transform[offset + row * 4 + column];
                }
            }
            return output;
        }

        [[nodiscard]] std::array<Vec2, 4> applySpriteTransform(size_t index, const std::array<Vec2, 4>& input) const
        {
            const auto& transform = mmw_preview::kSpriteTransforms[index];
            const std::array<float, 4> xs{input[0].x, input[1].x, input[2].x, input[3].x};
            const std::array<float, 4> ys{input[0].y, input[1].y, input[2].y, input[3].y};
            const auto txx = mulRowVec(transform, 0, xs);
            const auto txy = mulRowVec(transform, 16, xs);
            const auto tyx = mulRowVec(transform, 32, ys);
            const auto tyy = mulRowVec(transform, 48, ys);

            return {{
                Vec2{txx[0] + txy[0], tyx[0] + tyy[0]},
                Vec2{txx[1] + txy[1], tyx[1] + tyy[1]},
                Vec2{txx[2] + txy[2], tyx[2] + tyy[2]},
                Vec2{txx[3] + txy[3], tyx[3] + tyy[3]},
            }};
        }

        [[nodiscard]] std::array<Vec2, 4> scaleQuad(const std::array<Vec2, 4>& input, float scale) const
        {
            std::array<Vec2, 4> output = input;
            for (auto& point : output) {
                point.x *= scale;
                point.y *= scale;
            }
            return output;
        }

        void applyRuntimeStageCoverScissor(const ViewportRect& viewport) const
        {
            if (mStageCover <= 0.001f || viewport.width <= 0 || viewport.height <= 0) {
                glDisable(GL_SCISSOR_TEST);
                return;
            }

            const float coverBottom = WORLD_STAGE_TOP + mStageCover * (1.0f - WORLD_STAGE_TOP);
            const float clipY = mWorldToClip.convert(0.0f, coverBottom).y;
            const float normalizedTop = std::clamp(clipY * 0.5f + 0.5f, 0.0f, 1.0f);
            const int scissorHeight = std::clamp(
                static_cast<int>(std::lround(normalizedTop * static_cast<float>(viewport.height))),
                0,
                viewport.height);

            glEnable(GL_SCISSOR_TEST);
            glScissor(viewport.x, viewport.y, viewport.width, scissorHeight);
        }

        void clearRuntimeStageCoverScissor() const
        {
            glDisable(GL_SCISSOR_TEST);
        }

        void pushVertex(std::vector<float>& out, const Vec2& pos, const Vec2& uv, const Color& color, float reciprocalW)
        {
            out.push_back(pos.x);
            out.push_back(pos.y);
            out.push_back(uv.x);
            out.push_back(uv.y);
            out.push_back(color.r);
            out.push_back(color.g);
            out.push_back(color.b);
            out.push_back(color.a);
            out.push_back(reciprocalW);
        }

        const Texture& textureForRuntimeId(int textureId) const
        {
            if (textureId == 0) {
                return mNotes;
            }
            if (textureId == 1) {
                return mLongNoteLine;
            }
            if (textureId == 2) {
                return mTouchLine;
            }
            return mEffect;
        }

        void appendRuntimeQuad(std::vector<float>& out, int rawTextureId, const float* packed, int offset)
        {
            const Texture& texture = textureForRuntimeId(rawTextureId);
            const bool isEffect = rawTextureId >= 3;
            const float alphaMultiplier = isEffect ? mEffectOpacity : 1.0f;

            std::array<Vec2, 4> clip{};
            std::array<float, 4> reciprocalW{};
            std::array<Vec2, 4> uv{};

            for (int i = 0; i < 4; ++i) {
                const float px = packed[offset + i * 3 + 0];
                const float py = packed[offset + i * 3 + 1];
                const float rw = packed[offset + i * 3 + 2];

                reciprocalW[i] = isEffect ? rw : 1.0f;
                clip[i] = isEffect ? Vec2{px, py} : mWorldToClip.convert(px, py);
                uv[i] = Vec2{
                    packed[offset + 12 + i * 2 + 0] / static_cast<float>(texture.width),
                    packed[offset + 12 + i * 2 + 1] / static_cast<float>(texture.height),
                };
            }

            const Color color{
                packed[offset + 20],
                packed[offset + 21],
                packed[offset + 22],
                packed[offset + 23] * alphaMultiplier * mRuntimeVisibility,
            };

            pushVertex(out, clip[0], uv[0], color, reciprocalW[0]);
            pushVertex(out, clip[1], uv[1], color, reciprocalW[1]);
            pushVertex(out, clip[2], uv[2], color, reciprocalW[2]);
            pushVertex(out, clip[0], uv[0], color, reciprocalW[0]);
            pushVertex(out, clip[2], uv[2], color, reciprocalW[2]);
            pushVertex(out, clip[3], uv[3], color, reciprocalW[3]);
        }

        void drawRuntime(const float* packedQuads, int quadCount)
        {
            if (!packedQuads || quadCount <= 0 || mRuntimeVisibility <= 0.001f) {
                return;
            }

            int currentTextureId = -1;
            BlendMode currentBlend = BlendMode::Normal;
            std::vector<float> vertices;
            vertices.reserve(6 * FLOATS_PER_VERTEX * 64);

            auto flush = [&]() {
                if (vertices.empty() || currentTextureId < 0) {
                    return;
                }
                const Texture& texture = textureForRuntimeId(currentTextureId);
                drawVertices(texture, vertices, currentTextureId >= 3, currentBlend);
                vertices.clear();
            };

            for (int index = 0; index < quadCount; ++index) {
                const int offset = index * FLOATS_PER_QUAD;
                const int rawTextureId = static_cast<int>(std::lround(packedQuads[offset + 24]));
                const int runtimeTextureBucket = rawTextureId <= 2 ? rawTextureId : 3;
                const BlendMode blend = rawTextureId == 4 ? BlendMode::Additive : BlendMode::Normal;

                if (currentTextureId != -1 && (runtimeTextureBucket != currentTextureId || blend != currentBlend)) {
                    flush();
                }

                currentTextureId = runtimeTextureBucket;
                currentBlend = blend;
                appendRuntimeQuad(vertices, rawTextureId, packedQuads, offset);
            }

            flush();
        }

        void drawVertices(const Texture& texture, const std::vector<float>& vertices, bool effectPass, BlendMode blendMode)
        {
            if (vertices.empty()) {
                return;
            }

            if (effectPass) {
                glUseProgram(mEffectProgram);
                if (blendMode == BlendMode::Additive) {
                    glBlendFunc(GL_ONE, GL_ONE);
                } else {
                    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                }
            } else {
                glUseProgram(mProgram);
                if (blendMode == BlendMode::Additive) {
                    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
                } else {
                    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                }
            }

            glBindVertexArray(mVao);
            glBindBuffer(GL_ARRAY_BUFFER, mVbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture.id);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / FLOATS_PER_VERTEX));
            glBindVertexArray(0);
        }

        void destroyTexture(Texture& texture)
        {
            if (texture.id != 0) {
                glDeleteTextures(1, &texture.id);
                texture.id = 0;
            }
        }

        void cleanup()
        {
            if (mProgram != 0) {
                glDeleteProgram(mProgram);
                mProgram = 0;
            }
            if (mEffectProgram != 0) {
                glDeleteProgram(mEffectProgram);
                mEffectProgram = 0;
            }
            if (mVbo != 0) {
                glDeleteBuffers(1, &mVbo);
                mVbo = 0;
            }
            if (mVao != 0) {
                glDeleteVertexArrays(1, &mVao);
                mVao = 0;
            }

            destroyTexture(mBackground);
            destroyTexture(mStage);
            destroyTexture(mNotes);
            destroyTexture(mLongNoteLine);
            destroyTexture(mTouchLine);
            destroyTexture(mEffect);
            if (mContext > 0) {
                emscripten_webgl_destroy_context(mContext);
                mContext = 0;
            }
        }
    };

    std::string jsonEscape(const std::string& input)
    {
        std::string output;
        output.reserve(input.size() + 8);
        for (const char c : input) {
            switch (c) {
                case '\"':
                    output += "\\\"";
                    break;
                case '\\':
                    output += "\\\\";
                    break;
                case '\b':
                    output += "\\b";
                    break;
                case '\f':
                    output += "\\f";
                    break;
                case '\n':
                    output += "\\n";
                    break;
                case '\r':
                    output += "\\r";
                    break;
                case '\t':
                    output += "\\t";
                    break;
                default:
                    output.push_back(c);
                    break;
            }
        }
        return output;
    }

    std::string hitKindToString(int kindValue)
    {
        if (kindValue == 1) {
            return "criticalTap";
        }
        if (kindValue == 2) {
            return "flick";
        }
        if (kindValue == 3) {
            return "trace";
        }
        if (kindValue == 4) {
            return "tick";
        }
        if (kindValue == 5) {
            return "holdLoop";
        }
        return "tap";
    }

    void writeHitEventsJson(const std::string& filePath)
    {
        const int count = getHitEventCount();
        const float* packed = getHitEventBufferPointer();
        const int stride = 7;

        std::ofstream output(filePath, std::ios::binary);
        if (!output.is_open()) {
            throw std::runtime_error("Failed to write hit events file: " + filePath);
        }

        output << "{\n  \"events\": [\n";
        for (int index = 0; index < count; ++index) {
            const int offset = index * stride;
            const int kindValue = static_cast<int>(std::lround(packed[offset + 3]));
            const int flags = static_cast<int>(std::lround(packed[offset + 4]));
            const bool critical = (flags & 1) != 0;
            const float endTimeSec = packed[offset + 5];
            const float volume = packed[offset + 6];

            output << "    {\n";
            output << "      \"timeSec\": " << packed[offset + 0] << ",\n";
            output << "      \"center\": " << packed[offset + 1] << ",\n";
            output << "      \"width\": " << packed[offset + 2] << ",\n";
            output << "      \"kind\": \"" << jsonEscape(hitKindToString(kindValue)) << "\",\n";
            output << "      \"critical\": " << (critical ? "true" : "false") << ",\n";
            output << "      \"volume\": " << volume;
            if (endTimeSec >= 0.0f) {
                output << ",\n      \"endTimeSec\": " << endTimeSec << "\n";
            } else {
                output << "\n";
            }
            output << "    }";
            if (index + 1 < count) {
                output << ",";
            }
            output << "\n";
        }
        output << "  ]\n}\n";
    }

    constexpr int HUD_FLAG_SHOW_JUDGE = 1 << 2;
    constexpr float JUDGE_VISIBLE_WINDOW_SEC = 0.24f;
    constexpr float SCORE_DELTA_VISIBLE_WINDOW_SEC = 0.5f;
    constexpr float COMBO_BASE_SCALE = 0.85f;
    constexpr float COMBO_DIGIT_STEP = 92.0f;
    constexpr float COMBO_GROUP_SCALE = 1.25f;
    constexpr float COMBO_GROUP_OFFSET_Y = -12.0f;
    constexpr float COMBO_GROUP_CENTER_X = 1634.0f;
    constexpr float COMBO_GROUP_CENTER_Y = 478.0f;
    constexpr float SCORE_BAR_FULL_WIDTH = 354.0f;
    constexpr float SCORE_ROOT_SCALE = 1.5f;
    constexpr float CHART_END_PADDING_SEC = 5.0f;
    constexpr float TEAM_POWER = 250000.0f;
    constexpr float RATING = 26.0f;
    constexpr float HUD_INTRO_DURATION_SEC = 4.0f;
    constexpr float INTRO_CLEAN_BG_DURATION_SEC = 0.0f;
    constexpr float INTRO_PLAYFIELD_FADE_IN_SEC = 1.8f;
    constexpr float AUTO_BADGE_BLINK_START_SEC = 1.6f;
    constexpr float AUTO_BADGE_BLINK_PERIOD_SEC = 1.25f;
    constexpr float AUTO_BADGE_BLINK_MOD_SEC = 1.2f;
    constexpr float INTRO_ENTER_FADE_SEC = 0.52f;
    constexpr float INTRO_EXIT_FADE_SEC = 0.56f;
    constexpr float INTRO_BG_ALPHA = 0.82f;
    constexpr float INTRO_GRAD_ALPHA = 0.10f;
    constexpr float INTRO_GRAD_START_SEC = 1.0f;
    constexpr float INTRO_GRAD_DURATION_SEC = 2.0f;
    constexpr float INTRO_GRAD_DRAW_WIDTH = 2001.0f;
    constexpr float INTRO_GRAD_DRAW_HEIGHT = 1125.0f;
    constexpr float INTRO_GRAD_START_Y = 1500.0f;
    constexpr float INTRO_GRAD_END_Y = 0.0f;
    constexpr float INTRO_COVER_LEFT_PX = 148.0f;
    constexpr float INTRO_COVER_BOTTOM_PX = 104.0f;
    constexpr float INTRO_COVER_SIZE_PX = 350.0f;
    constexpr float INTRO_TEXT_LEFT_WITH_COVER_PX = 540.0f;
    constexpr float INTRO_TEXT_LEFT_NO_COVER_PX = 186.0f;
    constexpr float INTRO_TEXT_BOTTOM_PX = 110.0f;
    constexpr float INTRO_TEXT_BLOCK_SHIFT_Y_PX = 26.0f;
    constexpr float INTRO_DIFF_LABEL_Y_OFFSET_PX = 9.0f;
    constexpr float INTRO_TITLE_DRAW_SIZE_PX = 38.0f;
    constexpr float INTRO_TITLE_LETTER_SPACING_PX = 5.0f;
    constexpr float INTRO_DIFF_DRAW_SIZE_PX = 28.0f;

    struct IntroCardState
    {
        bool hasContent = false;
        bool hasCover = false;
        std::string title;
        std::string description1;
        std::string description2;
        std::string difficulty;
    };

    struct IntroFontSet
    {
        ImFont* body = nullptr;
        ImFont* title = nullptr;
        ImFont* difficulty = nullptr;
    };

    struct HudSnapshot
    {
        int score = 0;
        int combo = 0;
        char rank = 'd';
        float scoreBarRatio = 0.0f;
        float lifeRatio = 1.0f;
        float scoreDelta = 0.0f;
        int eventIndex = -1;
        bool showPerfect = false;
    };

    struct HudTimelineNative
    {
        std::vector<float> times;
        std::vector<float> deltas;
        std::vector<int> scores;
        std::vector<int> combos;
        std::vector<char> ranks;
        std::vector<float> scoreBars;
        std::vector<float> lastJudgeTimes;
        std::vector<float> judgeTimes;
        std::vector<float> comboTimes;
    };

    struct UiTransform
    {
        float scale = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
    };

    [[nodiscard]] int upperBound(const std::vector<float>& values, float target)
    {
        int low = 0;
        int high = static_cast<int>(values.size());
        while (low < high) {
            const int mid = (low + high) / 2;
            if (values[mid] <= target + 0.0001f) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        return low;
    }

    [[nodiscard]] float clamp01(float value)
    {
        return std::max(0.0f, std::min(1.0f, value));
    }

    [[nodiscard]] float lerp(float value, float start, float end, float startPos, float endPos)
    {
        if (end <= start) {
            return endPos;
        }
        return ((value - start) / (end - start)) * (endPos - startPos) + startPos;
    }

    [[nodiscard]] std::pair<char, float> scoreRankAndBar(float score)
    {
        const float rankBorder = 1200000.0f + (RATING - 5.0f) * 4100.0f;
        const float rankS = 1040000.0f + (RATING - 5.0f) * 5200.0f;
        const float rankA = 840000.0f + (RATING - 5.0f) * 4200.0f;
        const float rankB = 400000.0f + (RATING - 5.0f) * 2000.0f;
        const float rankC = 20000.0f + (RATING - 5.0f) * 100.0f;

        constexpr float rankBorderPos = 1650.0f / 1650.0f;
        constexpr float rankSPos = 1478.0f / 1650.0f;
        constexpr float rankAPos = 1234.0f / 1650.0f;
        constexpr float rankBPos = 990.0f / 1650.0f;
        constexpr float rankCPos = 746.0f / 1650.0f;

        if (score >= rankBorder) {
            return {'s', rankBorderPos};
        }
        if (score >= rankS) {
            return {'s', clamp01(lerp(score, rankS, rankBorder, rankSPos, rankBorderPos))};
        }
        if (score >= rankA) {
            return {'a', clamp01(lerp(score, rankA, rankS, rankAPos, rankSPos))};
        }
        if (score >= rankB) {
            return {'b', clamp01(lerp(score, rankB, rankA, rankBPos, rankAPos))};
        }
        if (score >= rankC) {
            return {'c', clamp01(lerp(score, rankC, rankB, rankCPos, rankBPos))};
        }
        return {'d', clamp01((score / std::max(rankC, 1.0f)) * rankCPos)};
    }

    [[nodiscard]] HudTimelineNative buildHudTimeline()
    {
        HudTimelineNative timeline;
        const float* packed = getHudEventBufferPointer();
        const int count = getHudEventCount();
        if (!packed || count <= 0) {
            return timeline;
        }

        timeline.times.resize(count);
        timeline.deltas.resize(count);
        timeline.scores.resize(count);
        timeline.combos.resize(count);
        timeline.ranks.resize(count);
        timeline.scoreBars.resize(count);
        timeline.lastJudgeTimes.resize(count);
        timeline.comboTimes.reserve(count);
        timeline.judgeTimes.reserve(count);

        float weightedCount = 0.0f;
        for (int i = 0; i < count; ++i) {
            const float weight = std::max(0.0f, packed[i * 4 + 1]);
            weightedCount += weight;
        }
        weightedCount = std::max(weightedCount, 1.0f);

        const float levelFactor = (RATING - 5.0f) * 0.005f + 1.0f;
        int combo = 0;
        float comboFactor = 1.0f;
        float score = 0.0f;
        float lastJudgeTime = -100000.0f;

        for (int i = 0; i < count; ++i) {
            const int offset = i * 4;
            const float timeSec = packed[offset + 0];
            const float weight = std::max(0.0f, packed[offset + 1]);
            const int flags = static_cast<int>(std::lround(packed[offset + 3]));
            const bool showJudge = (flags & HUD_FLAG_SHOW_JUDGE) != 0;

            combo += 1;
            if (combo % 100 == 1 && combo > 1) {
                comboFactor = std::min(comboFactor + 0.01f, 1.1f);
            }

            const float delta = (TEAM_POWER / weightedCount) * 4.0f * weight * levelFactor * comboFactor;
            score += delta;

            if (showJudge) {
                lastJudgeTime = timeSec;
                timeline.judgeTimes.push_back(timeSec);
            }

            timeline.comboTimes.push_back(timeSec);
            timeline.times[i] = timeSec;
            timeline.deltas[i] = delta;
            timeline.scores[i] = std::max(0, static_cast<int>(std::lround(score)));
            timeline.combos[i] = combo;
            const auto [rank, bar] = scoreRankAndBar(score);
            timeline.ranks[i] = rank;
            timeline.scoreBars[i] = bar;
            timeline.lastJudgeTimes[i] = lastJudgeTime;
        }

        return timeline;
    }

    [[nodiscard]] HudSnapshot snapshotHud(const HudTimelineNative& timeline, float timeSec)
    {
        HudSnapshot snapshot;
        if (timeline.times.empty()) {
            return snapshot;
        }

        const int index = upperBound(timeline.times, timeSec) - 1;
        if (index < 0) {
            return snapshot;
        }

        snapshot.score = timeline.scores[index];
        snapshot.combo = timeline.combos[index];
        snapshot.rank = timeline.ranks[index];
        snapshot.scoreBarRatio = timeline.scoreBars[index];
        snapshot.eventIndex = index;

        const bool scoreDeltaVisible =
            timeSec >= timeline.times[index] - 0.0001f && timeSec <= timeline.times[index] + SCORE_DELTA_VISIBLE_WINDOW_SEC;
        snapshot.scoreDelta = scoreDeltaVisible ? std::max(0.0f, timeline.deltas[index]) : 0.0f;

        const float judgeTime = timeline.lastJudgeTimes[index];
        snapshot.showPerfect =
            std::isfinite(judgeTime) && timeSec >= judgeTime - 0.0001f && timeSec <= judgeTime + JUDGE_VISIBLE_WINDOW_SEC;
        return snapshot;
    }

    [[nodiscard]] UiTransform buildUiTransform(int width, int height)
    {
        UiTransform tx;
        tx.scale = std::min(static_cast<float>(width) / 1920.0f, static_cast<float>(height) / 1080.0f);
        tx.offsetX = (static_cast<float>(width) - 1920.0f * tx.scale) * 0.5f;
        tx.offsetY = (static_cast<float>(height) - 1080.0f * tx.scale) * 0.5f;
        return tx;
    }

    [[nodiscard]] std::string scoreDigitsText(int score)
    {
        std::string text = std::to_string(std::max(0, score));
        while (text.size() < 8) {
            text.insert(text.begin(), 'n');
        }
        return text;
    }

    [[nodiscard]] Texture loadHudTexture(
        const std::unordered_map<std::string, BinaryBlob>& assetStore,
        const std::string& relativePath)
    {
        return loadTexture(assetStore, relativePath);
    }

    [[nodiscard]] size_t utf8CodepointLength(unsigned char leadByte)
    {
        if ((leadByte & 0x80u) == 0) {
            return 1;
        }
        if ((leadByte & 0xE0u) == 0xC0u) {
            return 2;
        }
        if ((leadByte & 0xF0u) == 0xE0u) {
            return 3;
        }
        if ((leadByte & 0xF8u) == 0xF0u) {
            return 4;
        }
        return 1;
    }

    void drawSpacedUtf8Text(
        ImDrawList* drawList,
        ImFont* font,
        float fontSize,
        ImVec2 position,
        ImU32 color,
        const std::string& text,
        float extraSpacing,
        float maxWidth)
    {
        if (!drawList || !font || text.empty()) {
            return;
        }

        const char* cursor = text.c_str();
        const char* end = cursor + text.size();
        float x = position.x;
        const float maxX = maxWidth > 0.0f ? position.x + maxWidth : FLT_MAX;

        while (cursor < end) {
            const size_t glyphLength = std::min(
                utf8CodepointLength(static_cast<unsigned char>(*cursor)),
                static_cast<size_t>(end - cursor));
            const std::string glyph(cursor, glyphLength);
            const float glyphWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, glyph.c_str()).x;
            if (x + glyphWidth > maxX) {
                break;
            }
            drawList->AddText(font, fontSize, ImVec2(x, position.y), color, glyph.c_str());
            x += glyphWidth + extraSpacing;
            cursor += glyphLength;
        }
    }

    [[nodiscard]] const Texture& requireTexture(
        const std::unordered_map<std::string, Texture>& textures,
        const std::string& key)
    {
        const auto it = textures.find(key);
        if (it == textures.end()) {
            throw std::runtime_error("Missing HUD texture key: " + key);
        }
        return it->second;
    }

    [[nodiscard]] const Texture* findTexture(
        const std::unordered_map<std::string, Texture>& textures,
        const std::string& key)
    {
        const auto it = textures.find(key);
        return it == textures.end() ? nullptr : &it->second;
    }

    [[nodiscard]] ImTextureID textureId(const Texture& texture)
    {
        return static_cast<ImTextureID>(static_cast<std::uintptr_t>(texture.id));
    }

    void drawHudImage(ImDrawList* drawList, const Texture& texture, float x, float y, float width, float height, float alpha = 1.0f)
    {
        if (!drawList || texture.id == 0 || width <= 0.1f || height <= 0.1f) {
            return;
        }
        drawList->AddImage(
            textureId(texture),
            ImVec2(x, y),
            ImVec2(x + width, y + height),
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            IM_COL32(255, 255, 255, static_cast<int>(std::lround(clamp01(alpha) * 255.0f))));
    }

    void drawHudImageClipX(
        ImDrawList* drawList,
        const Texture& texture,
        float x,
        float y,
        float width,
        float height,
        float ratio,
        float alpha = 1.0f)
    {
        if (!drawList || texture.id == 0 || width <= 0.1f || height <= 0.1f) {
            return;
        }
        const float clipped = clamp01(ratio);
        if (clipped <= 0.0f) {
            return;
        }
        const float drawWidth = width * clipped;
        drawList->AddImage(
            textureId(texture),
            ImVec2(x, y),
            ImVec2(x + drawWidth, y + height),
            ImVec2(0.0f, 0.0f),
            ImVec2(clipped, 1.0f),
            IM_COL32(255, 255, 255, static_cast<int>(std::lround(clamp01(alpha) * 255.0f))));
    }

    [[nodiscard]] std::string trimText(std::string value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.pop_back();
        }
        return value;
    }

    [[nodiscard]] IntroCardState buildIntroCardState(
        const SessionMetadata& metadata,
        const std::string& metadataTitle,
        const std::string& metadataArtist,
        bool hasCover)
    {
        auto toUpper = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            return value;
        };
        auto normalizeDifficulty = [&](std::string value) -> std::string {
            value = toUpper(trimText(value));
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; }), value.end());
            if (value == "0") {
                return "EASY";
            }
            if (value == "1") {
                return "NORMAL";
            }
            if (value == "2") {
                return "HARD";
            }
            if (value == "3") {
                return "EXPERT";
            }
            if (value == "4") {
                return "MASTER";
            }
            if (value == "5") {
                return "APPEND";
            }
            if (value == "6") {
                return "ETERNAL";
            }
            return value;
        };
        auto inferDifficultyFromSusPath = [&](std::string susPath) -> std::string {
            susPath = toUpper(susPath);
            const std::array<std::string, 7> ordered{
                "ETERNAL", "APPEND", "MASTER", "EXPERT", "HARD", "NORMAL", "EASY"
            };
            for (const auto& candidate : ordered) {
                if (susPath.find(candidate) != std::string::npos) {
                    return candidate;
                }
            }
            return "";
        };

        IntroCardState intro;
        intro.hasCover = hasCover;
        intro.title = trimText(metadata.title);
        if (intro.title.empty()) {
            intro.title = trimText(metadataTitle);
        }
        if (intro.title.empty()) {
            intro.title = "Unknown Title";
        }

        const std::string lyricist = trimText(metadata.lyricist).empty() ? std::string("-") : trimText(metadata.lyricist);
        std::string composer = trimText(metadata.composer);
        if (composer.empty()) {
            composer = trimText(metadataArtist);
        }
        if (composer.empty()) {
            composer = "-";
        }
        const std::string arranger = trimText(metadata.arranger).empty() ? std::string("-") : trimText(metadata.arranger);
        const std::string vocal = trimText(metadata.vocal).empty() ? std::string("-") : trimText(metadata.vocal);

        intro.description1 = "作詞：" + lyricist + "　作曲：" + composer + "　編曲：" + arranger;
        intro.description2 = "Vo. " + vocal;
        intro.difficulty = normalizeDifficulty(trimText(metadata.difficulty));
        if (intro.difficulty.empty()) {
            intro.difficulty = "";
        }

        intro.hasContent = intro.hasCover || !intro.title.empty() || !intro.description1.empty() || !intro.description2.empty() || !intro.difficulty.empty();
        return intro;
    }

    [[nodiscard]] float easeOutQuad(float value)
    {
        const float clamped = clamp01(value);
        return 1.0f - (1.0f - clamped) * (1.0f - clamped);
    }

    [[nodiscard]] float openingPlayfieldVisibility(float outputTimeSec, bool hasIntroContent)
    {
        if (!hasIntroContent || outputTimeSec < 0.0f) {
            return 1.0f;
        }
        const float revealStartSec = HUD_INTRO_DURATION_SEC + INTRO_CLEAN_BG_DURATION_SEC;
        if (outputTimeSec < revealStartSec) {
            return 0.0f;
        }
        return clamp01((outputTimeSec - revealStartSec) / INTRO_PLAYFIELD_FADE_IN_SEC);
    }

    [[nodiscard]] float introCardAlpha(float outputTimeSec, bool hasIntroContent)
    {
        if (!hasIntroContent || outputTimeSec < 0.0f) {
            return 0.0f;
        }
        const float fadeOutStartSec = std::max(0.0f, HUD_INTRO_DURATION_SEC - INTRO_EXIT_FADE_SEC);

        if (outputTimeSec < fadeOutStartSec) {
            return 1.0f;
        }
        if (outputTimeSec < HUD_INTRO_DURATION_SEC) {
            return clamp01(1.0f - (outputTimeSec - fadeOutStartSec) / std::max(0.001f, INTRO_EXIT_FADE_SEC));
        }
        return 0.0f;
    }

    [[nodiscard]] IntroFontSet loadIntroFonts(const std::unordered_map<std::string, BinaryBlob>& fontStore)
    {
        auto findFirstExisting = [&](const std::vector<std::string>& candidates) -> const BinaryBlob* {
            for (const auto& key : candidates) {
                const auto it = fontStore.find(key);
                if (it != fontStore.end() && !it->second.bytes.empty()) {
                    return &it->second;
                }
            }
            return nullptr;
        };

        IntroFontSet fonts;
        ImGuiIO& io = ImGui::GetIO();
        const BinaryBlob* dbFont = findFirstExisting({
            "font/FOT-RodinNTLGPro-DB.ttf",
        });
        const BinaryBlob* boldFont = findFirstExisting({
            "font/FOT-RodinNTLG Pro EB.otf",
            "font/FOT-RodinNTLGPro-EB.ttf",
            "font/NotoSansCJKSC-Black.ttf",
        });

        if (dbFont) {
            ImFontConfig bodyCfg;
            bodyCfg.OversampleH = 2;
            bodyCfg.OversampleV = 2;
            bodyCfg.RasterizerMultiply = 1.0f;
            void* bodyBytes = std::malloc(dbFont->bytes.size());
            if (!bodyBytes) {
                throw std::bad_alloc();
            }
            std::memcpy(bodyBytes, dbFont->bytes.data(), dbFont->bytes.size());
            fonts.body = io.Fonts->AddFontFromMemoryTTF(
                bodyBytes,
                static_cast<int>(dbFont->bytes.size()),
                42.0f,
                &bodyCfg,
                io.Fonts->GetGlyphRangesJapanese());
        }

        if (boldFont) {
            ImFontConfig titleCfg;
            titleCfg.OversampleH = 2;
            titleCfg.OversampleV = 2;
            titleCfg.RasterizerMultiply = 1.0f;
            void* titleBytes = std::malloc(boldFont->bytes.size());
            if (!titleBytes) {
                throw std::bad_alloc();
            }
            std::memcpy(titleBytes, boldFont->bytes.data(), boldFont->bytes.size());
            fonts.title = io.Fonts->AddFontFromMemoryTTF(
                titleBytes,
                static_cast<int>(boldFont->bytes.size()),
                38.0f,
                &titleCfg,
                io.Fonts->GetGlyphRangesJapanese());

            ImFontConfig diffCfg;
            diffCfg.OversampleH = 2;
            diffCfg.OversampleV = 2;
            diffCfg.RasterizerMultiply = 1.0f;
            void* diffBytes = std::malloc(boldFont->bytes.size());
            if (!diffBytes) {
                throw std::bad_alloc();
            }
            std::memcpy(diffBytes, boldFont->bytes.data(), boldFont->bytes.size());
            fonts.difficulty = io.Fonts->AddFontFromMemoryTTF(
                diffBytes,
                static_cast<int>(boldFont->bytes.size()),
                20.0f,
                &diffCfg,
                io.Fonts->GetGlyphRangesJapanese());
        }
        if (!fonts.body) {
            fonts.body = io.Fonts->AddFontDefault();
        }
        if (!fonts.title) {
            fonts.title = fonts.body;
        }
        if (!fonts.difficulty) {
            fonts.difficulty = fonts.title;
        }
        return fonts;
    }

    void drawOpeningIntroOverlay(
        GlRenderer& renderer,
        const std::unordered_map<std::string, Texture>& hudTextures,
        const IntroCardState& intro,
        const IntroFontSet& fonts,
        float outputTimeSec)
    {
        const float cardAlpha = introCardAlpha(outputTimeSec, intro.hasContent);
        const float maskAlpha =
            (intro.hasContent && outputTimeSec >= 0.0f && outputTimeSec < HUD_INTRO_DURATION_SEC)
                ? INTRO_BG_ALPHA
                : 0.0f;
        if (cardAlpha <= 0.001f && maskAlpha <= 0.001f) {
            return;
        }

        ImDrawList* overlay = ImGui::GetForegroundDrawList();
        const auto previewRectWindow = renderer.previewRectWindow();
        const UiTransform tx = buildUiTransform(previewRectWindow[2], previewRectWindow[3]);
        auto px = [&](float x) { return static_cast<float>(previewRectWindow[0]) + tx.offsetX + x * tx.scale; };
        auto py = [&](float y) { return static_cast<float>(previewRectWindow[1]) + tx.offsetY + y * tx.scale; };
        auto ps = [&](float v) { return v * tx.scale; };

        if (maskAlpha > 0.001f) {
            overlay->AddRectFilled(
                ImVec2(px(0.0f), py(0.0f)),
                ImVec2(px(1920.0f), py(1080.0f)),
                IM_COL32(104, 104, 156, static_cast<int>(std::lround(clamp01(maskAlpha) * 255.0f))));
        }

        const Texture* gradTexture = findTexture(hudTextures, "intro_grad");
        if (maskAlpha > 0.001f && gradTexture && gradTexture->id != 0) {
            const float introTimeSec = std::min(
                HUD_INTRO_DURATION_SEC + INTRO_GRAD_DURATION_SEC * 0.5f,
                std::max(0.0f, outputTimeSec + INTRO_GRAD_DURATION_SEC * 0.5f));
            for (int waveIndex = 0; waveIndex < 2; ++waveIndex) {
                const float waveStartSec = INTRO_GRAD_START_SEC + static_cast<float>(waveIndex) * INTRO_GRAD_DURATION_SEC;
                const float normalized = (introTimeSec - waveStartSec) / INTRO_GRAD_DURATION_SEC;
                if (normalized <= 0.0f || normalized >= 1.0f) {
                    continue;
                }
                const float eased = easeOutQuad(normalized);
                const float offsetY = INTRO_GRAD_START_Y + (INTRO_GRAD_END_Y - INTRO_GRAD_START_Y) * eased;
                const float drawX = (1920.0f - INTRO_GRAD_DRAW_WIDTH) * 0.5f;
                const float drawY = (1080.0f - INTRO_GRAD_DRAW_HEIGHT) * 0.5f + offsetY;
                drawHudImage(
                    overlay,
                    *gradTexture,
                    px(drawX),
                    py(drawY),
                    ps(INTRO_GRAD_DRAW_WIDTH),
                    ps(INTRO_GRAD_DRAW_HEIGHT),
                    INTRO_GRAD_ALPHA);
            }
        }

        if (cardAlpha > 0.001f && intro.hasCover) {
            const Texture* coverTexture = findTexture(hudTextures, "intro_cover");
            if (coverTexture && coverTexture->id != 0) {
                const float coverTop = 1080.0f - INTRO_COVER_BOTTOM_PX - INTRO_COVER_SIZE_PX;
                if (!intro.difficulty.empty()) {
                    const std::string upperDifficulty = [&]() {
                        std::string text = intro.difficulty;
                        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                            return static_cast<char>(std::toupper(c));
                        });
                        return text;
                    }();
                    const int diffAlpha = static_cast<int>(std::lround(clamp01(cardAlpha) * 255.0f));
                    auto difficultyColor = [&](const std::string& upper) {
                        if (upper == "EASY") {
                            return IM_COL32(75, 207, 138, diffAlpha);
                        }
                        if (upper == "NORMAL") {
                            return IM_COL32(90, 140, 255, diffAlpha);
                        }
                        if (upper == "HARD") {
                            return IM_COL32(242, 150, 77, diffAlpha);
                        }
                        if (upper == "EXPERT") {
                            return IM_COL32(239, 90, 102, diffAlpha);
                        }
                        if (upper == "MASTER") {
                            return IM_COL32(181, 91, 255, diffAlpha);
                        }
                        if (upper == "APPEND") {
                            return IM_COL32(179, 162, 255, diffAlpha);
                        }
                        if (upper == "ETERNAL") {
                            return IM_COL32(241, 192, 79, diffAlpha);
                        }
                        return IM_COL32(169, 56, 255, diffAlpha);
                    };
                    const float diffX = INTRO_COVER_LEFT_PX - 40.0f;
                    const float diffTop = coverTop + 36.0f;
                    const float diffSize = INTRO_COVER_SIZE_PX;
                    if (upperDifficulty == "APPEND") {
                        constexpr int appendStartR = 0xAD;
                        constexpr int appendStartG = 0x9F;
                        constexpr int appendStartB = 0xF6;
                        constexpr int appendEndR = 0xEF;
                        constexpr int appendEndG = 0x8D;
                        constexpr int appendEndB = 0xDA;
                        constexpr int appendMidR = (appendStartR + appendEndR) / 2;
                        constexpr int appendMidG = (appendStartG + appendEndG) / 2;
                        constexpr int appendMidB = (appendStartB + appendEndB) / 2;
                        overlay->AddRectFilledMultiColor(
                            ImVec2(px(diffX), py(diffTop)),
                            ImVec2(px(diffX + diffSize), py(diffTop + diffSize)),
                            IM_COL32(appendStartR, appendStartG, appendStartB, diffAlpha),
                            IM_COL32(appendMidR, appendMidG, appendMidB, diffAlpha),
                            IM_COL32(appendEndR, appendEndG, appendEndB, diffAlpha),
                            IM_COL32(appendMidR, appendMidG, appendMidB, diffAlpha));
                    } else {
                        overlay->AddRectFilled(
                            ImVec2(px(diffX), py(diffTop)),
                            ImVec2(px(diffX + diffSize), py(diffTop + diffSize)),
                            difficultyColor(upperDifficulty));
                    }
                    overlay->AddText(
                        fonts.difficulty,
                        ps(INTRO_DIFF_DRAW_SIZE_PX),
                        ImVec2(px(diffX + 10.0f), py(diffTop + diffSize - 42.0f + INTRO_DIFF_LABEL_Y_OFFSET_PX)),
                        IM_COL32(247, 250, 255, static_cast<int>(std::lround(cardAlpha * 255.0f))),
                        intro.difficulty.c_str());
                }
                drawHudImage(
                    overlay,
                    *coverTexture,
                    px(INTRO_COVER_LEFT_PX),
                    py(coverTop),
                    ps(INTRO_COVER_SIZE_PX),
                    ps(INTRO_COVER_SIZE_PX),
                    cardAlpha);
            }
        }

        const float textLeft = intro.hasCover ? INTRO_TEXT_LEFT_WITH_COVER_PX : INTRO_TEXT_LEFT_NO_COVER_PX;
        const float textBottom = INTRO_TEXT_BOTTOM_PX;
        const float blockTop = 1080.0f - textBottom - 180.0f + INTRO_TEXT_BLOCK_SHIFT_Y_PX;
        const float textMaxWidth = std::max(320.0f, 1920.0f - textLeft - 120.0f);
        const ImU32 titleColor = IM_COL32(246, 251, 255, static_cast<int>(std::lround(cardAlpha * 255.0f)));
        const ImU32 metaColor = IM_COL32(255, 255, 255, static_cast<int>(std::lround(cardAlpha * 255.0f)));
        if (cardAlpha > 0.001f) {
            drawSpacedUtf8Text(
                overlay,
                fonts.title,
                ps(INTRO_TITLE_DRAW_SIZE_PX),
                ImVec2(px(textLeft), py(blockTop)),
                titleColor,
                intro.title,
                ps(INTRO_TITLE_LETTER_SPACING_PX),
                ps(textMaxWidth));
            overlay->AddText(
                fonts.body,
                ps(26.0f),
                ImVec2(px(textLeft), py(blockTop + 88.0f)),
                metaColor,
                intro.description1.c_str(),
                nullptr,
                ps(textMaxWidth));
            overlay->AddText(
                fonts.body,
                ps(26.0f),
                ImVec2(px(textLeft), py(blockTop + 136.0f)),
                metaColor,
                intro.description2.c_str(),
                nullptr,
                ps(textMaxWidth));
        }
    }

    [[nodiscard]] std::unordered_map<std::string, Texture> loadHudTextures(
        const std::unordered_map<std::string, BinaryBlob>& assetStore,
        const BinaryBlob* coverBlob)
    {
        std::unordered_map<std::string, Texture> hudTextures;
        auto addHudTexture = [&](const std::string& key, const std::string& path) {
            hudTextures.emplace(key, loadHudTexture(assetStore, path));
        };

        addHudTexture("score_bg", "overlay/score/bg.png");
        addHudTexture("score_fg", "overlay/score/fg.png");
        addHudTexture("score_bar", "overlay/score/bar.png");
        addHudTexture("life_bg", "overlay/life/v3/bg.png");
        addHudTexture("life_fill", "overlay/life/v3/normal.png");
        addHudTexture("combo_tag", "overlay/combo/pt.png");
        addHudTexture("combo_tag_glow", "overlay/combo/pe.png");
        addHudTexture("judge_perfect", "overlay/judge/v3/1.png");
        addHudTexture("auto_badge", "overlay/autolive.png");
        for (const char rank : {'d', 'c', 'b', 'a', 's'}) {
            addHudTexture(std::string("rank_char_") + rank, std::string("overlay/score/rank/chr/") + rank + ".png");
            addHudTexture(std::string("rank_txt_") + rank, std::string("overlay/score/rank/txt/en/") + rank + ".png");
        }
        const std::string scoreDigitChars = "0123456789n+";
        for (const char ch : scoreDigitChars) {
            const std::string fileStem = ch == '+' ? "plus" : std::string(1, ch);
            const std::string shadowStem = ch == '+' ? "splus" : std::string("s") + ch;
            addHudTexture(std::string("score_digit_") + ch, std::string("overlay/score/digit/") + fileStem + ".png");
            addHudTexture(std::string("score_digit_s_") + ch, std::string("overlay/score/digit/") + shadowStem + ".png");
        }
        const std::string comboDigitChars = "0123456789";
        for (const char ch : comboDigitChars) {
            addHudTexture(std::string("combo_digit_n_") + ch, std::string("overlay/combo/p") + ch + ".png");
            addHudTexture(std::string("combo_digit_b_") + ch, std::string("overlay/combo/b") + ch + ".png");
            addHudTexture(std::string("life_digit_") + ch, std::string("overlay/life/v3/digit/") + ch + ".png");
            addHudTexture(std::string("life_digit_s_") + ch, std::string("overlay/life/v3/digit/s") + ch + ".png");
        }
        addHudTexture("intro_grad", "overlay/start_grad.png");
        if (coverBlob && !coverBlob->bytes.empty()) {
            hudTextures.emplace(
                "intro_cover",
                loadTextureFromMemory(coverBlob->bytes.data(), static_cast<int>(coverBlob->bytes.size())));
        }
        return hudTextures;
    }

    void destroyHudTextures(std::unordered_map<std::string, Texture>& hudTextures)
    {
        for (auto& [_, texture] : hudTextures) {
            if (texture.id != 0) {
                glDeleteTextures(1, &texture.id);
                texture.id = 0;
            }
        }
    }

    void drawHudOverlayFrame(
        GlRenderer& renderer,
        const std::unordered_map<std::string, Texture>& hudTextures,
        const HudTimelineNative& hudTimeline,
        float chartTimeSec,
        float outputTimeSec,
        float& scorePlusTriggerSec,
        int& scorePlusValue,
        int& lastScoreEventIndex,
        float hudAlpha = 1.0f)
    {
        const float overlayAlpha = clamp01(hudAlpha);
        if (overlayAlpha <= 0.001f) {
            return;
        }
        const HudSnapshot hud = snapshotHud(hudTimeline, chartTimeSec);
        if (hud.eventIndex >= 0 && hud.eventIndex > lastScoreEventIndex) {
            scorePlusValue = static_cast<int>(std::lround(std::max(0.0f, hudTimeline.deltas[hud.eventIndex])));
            scorePlusTriggerSec = hudTimeline.times[hud.eventIndex];
            lastScoreEventIndex = hud.eventIndex;
        }

        const auto previewRectWindow = renderer.previewRectWindow();
        const UiTransform tx = buildUiTransform(previewRectWindow[2], previewRectWindow[3]);
        auto px = [&](float x) { return static_cast<float>(previewRectWindow[0]) + tx.offsetX + x * tx.scale; };
        auto py = [&](float y) { return static_cast<float>(previewRectWindow[1]) + tx.offsetY + y * tx.scale; };
        auto ps = [&](float v) { return v * tx.scale; };

        ImDrawList* overlay = ImGui::GetForegroundDrawList();
        auto drawImage = [&](const Texture& texture, float x, float y, float width, float height, float alpha = 1.0f) {
            drawHudImage(overlay, texture, x, y, width, height, alpha * overlayAlpha);
        };
        auto drawImageClipX = [&](const Texture& texture, float x, float y, float width, float height, float ratio, float alpha = 1.0f) {
            drawHudImageClipX(overlay, texture, x, y, width, height, ratio, alpha * overlayAlpha);
        };

        auto scoreX = [&](float v) { return 36.0f + v * SCORE_ROOT_SCALE; };
        auto scoreY = [&](float v) { return -3.0f + v * SCORE_ROOT_SCALE; };
        auto scoreS = [&](float v) { return v * SCORE_ROOT_SCALE; };

        drawImage(requireTexture(hudTextures, "score_bg"), px(scoreX(0.0f)), py(scoreY(0.0f)), ps(scoreS(444)), ps(scoreS(96)));
        drawImageClipX(
            requireTexture(hudTextures, "score_bar"),
            px(scoreX(79.0f)),
            py(scoreY(37.0f)),
            ps(scoreS(SCORE_BAR_FULL_WIDTH)),
            ps(scoreS(16.0f)),
            hud.scoreBarRatio);
        drawImage(requireTexture(hudTextures, "score_fg"), px(scoreX(0.0f)), py(scoreY(0.0f)), ps(scoreS(444)), ps(scoreS(96)));
        drawImage(
            requireTexture(hudTextures, std::string("rank_char_") + hud.rank),
            px(scoreX(10.0f)),
            py(scoreY(13.0f)),
            ps(scoreS(49.0f)),
            ps(scoreS(58.0f)));
        drawImage(
            requireTexture(hudTextures, std::string("rank_txt_") + hud.rank),
            px(scoreX(6.0f)),
            py(scoreY(77.0f)),
            ps(scoreS(60.0f)),
            ps(scoreS(8.0f)));

        const std::string scoreText = scoreDigitsText(hud.score);
        for (size_t i = 0; i < scoreText.size(); ++i) {
            const char ch = scoreText[i];
            const float slotX = scoreX(82.0f + static_cast<float>(i) * 22.0f);
            const float slotY = scoreY(60.0f);
            const Texture& shadow = requireTexture(hudTextures, std::string("score_digit_s_") + ch);
            const Texture& main = requireTexture(hudTextures, std::string("score_digit_") + ch);
            const float shadowH = ps(scoreS(36.0f));
            const float mainH = ps(scoreS(29.0f));
            const float shadowW = shadowH * (static_cast<float>(shadow.width) / static_cast<float>(shadow.height));
            const float mainW = mainH * (static_cast<float>(main.width) / static_cast<float>(main.height));
            const float centerX = px(slotX + scoreS(11.0f));
            drawImage(shadow, centerX - shadowW * 0.5f, py(slotY - scoreS(4.0f)), shadowW, shadowH);
            drawImage(main, centerX - mainW * 0.5f, py(slotY), mainW, mainH);
        }

        const float plusElapsed = chartTimeSec - scorePlusTriggerSec;
        if (scorePlusValue > 0 && plusElapsed >= 0.0f && plusElapsed <= SCORE_DELTA_VISIBLE_WINDOW_SEC) {
            const float progress = clamp01(plusElapsed / SCORE_DELTA_VISIBLE_WINDOW_SEC);
            const float entry = clamp01(progress / 0.42f);
            const float eased = 1.0f - std::pow(0.9f, entry * 12.0f);
            const float fadeStart = 0.88f;
            float alpha = std::min(1.0f, 1.3f * eased);
            if (progress > fadeStart) {
                alpha *= std::max(0.0f, 1.0f - (progress - fadeStart) / (1.0f - fadeStart));
            }
            const float offsetX = -32.0f * (1.0f - eased);
            const float offsetY = -2.0f * eased;
            const std::string plusText = "+" + std::to_string(scorePlusValue);
            float cursor = scoreX(290.0f);
            const float plusCenterY = scoreY(74.0f + 11.0f) + offsetY * SCORE_ROOT_SCALE;
            for (const char ch : plusText) {
                const bool isSign = ch == '+';
                const float slotW = scoreS(isSign ? 7.0f : 14.0f);
                const std::string key(1, ch);
                const Texture& shadow = requireTexture(hudTextures, "score_digit_s_" + key);
                const Texture& main = requireTexture(hudTextures, "score_digit_" + key);
                const float centerX = px(cursor + offsetX * SCORE_ROOT_SCALE + slotW * 0.5f);
                if (isSign) {
                    const float shadowH = ps(scoreS(10.0f));
                    const float mainH = ps(scoreS(8.0f));
                    const float shadowW = shadowH * (static_cast<float>(shadow.width) / static_cast<float>(shadow.height));
                    const float mainW = mainH * (static_cast<float>(main.width) / static_cast<float>(main.height));
                    drawImage(shadow, centerX - shadowW * 0.5f, py(plusCenterY) - shadowH * 0.5f, shadowW, shadowH, alpha);
                    drawImage(main, centerX - mainW * 0.5f, py(plusCenterY) - mainH * 0.5f, mainW, mainH, alpha);
                } else {
                    const float shadowH = ps(scoreS(22.0f));
                    const float mainH = ps(scoreS(18.0f));
                    const float shadowW = shadowH * (static_cast<float>(shadow.width) / static_cast<float>(shadow.height));
                    const float mainW = mainH * (static_cast<float>(main.width) / static_cast<float>(main.height));
                    drawImage(shadow, centerX - shadowW * 0.5f, py(plusCenterY) - shadowH * 0.5f, shadowW, shadowH, alpha);
                    drawImage(main, centerX - mainW * 0.5f, py(plusCenterY) - mainH * 0.5f, mainW, mainH, alpha);
                }
                cursor += slotW + (isSign ? scoreS(4.0f) : 0.0f);
            }
        }

        drawImage(requireTexture(hudTextures, "life_bg"), px(1442), py(11), ps(444), ps(104));
        drawImageClipX(
            requireTexture(hudTextures, "life_fill"),
            px(1442),
            py(11),
            ps(444),
            ps(104),
            hud.lifeRatio);
        const int lifeValue = std::max(0, static_cast<int>(std::lround(1000.0f * clamp01(hud.lifeRatio))));
        const std::string lifeText = std::to_string(lifeValue);
        for (size_t i = 0; i < lifeText.size(); ++i) {
            const char ch = lifeText[lifeText.size() - 1 - i];
            const float slotX = 1442.0f + 319.0f - static_cast<float>(i) * 22.0f;
            const float slotY = 11.0f + 10.0f;
            const Texture& shadow = requireTexture(hudTextures, std::string("life_digit_s_") + ch);
            const Texture& main = requireTexture(hudTextures, std::string("life_digit_") + ch);
            const float shadowH = ps(37.0f);
            const float mainH = ps(34.0f);
            const float shadowW = shadowH * (static_cast<float>(shadow.width) / static_cast<float>(shadow.height));
            const float mainW = mainH * (static_cast<float>(main.width) / static_cast<float>(main.height));
            const float centerX = px(slotX + 13.0f);
            drawImage(shadow, centerX - shadowW * 0.5f, py(slotY - 2.0f), shadowW, shadowH);
            drawImage(main, centerX - mainW * 0.5f, py(slotY), mainW, mainH);
        }

        if (hud.combo > 0) {
            constexpr float AP_PULSE_ANGULAR = 3.14159265359f * (4.0f / 3.0f);
            auto comboGroupX = [&](float x) {
                return COMBO_GROUP_CENTER_X + (x - COMBO_GROUP_CENTER_X) * COMBO_GROUP_SCALE;
            };
            auto comboGroupY = [&](float y) {
                return COMBO_GROUP_CENTER_Y + (y - COMBO_GROUP_CENTER_Y) * COMBO_GROUP_SCALE + COMBO_GROUP_OFFSET_Y;
            };
            auto comboGroupS = [&](float v) { return v * COMBO_GROUP_SCALE; };

            const float apAlpha = clamp01((std::sin(chartTimeSec * AP_PULSE_ANGULAR) + 1.0f) * 0.5f);
            const float comboTagGlowWidth = 197.0f * 0.67f;
            const float comboTagGlowHeight = 79.0f * 0.67f;
            drawImage(
                requireTexture(hudTextures, "combo_tag_glow"),
                px(comboGroupX(1634.0f - comboTagGlowWidth * 0.5f)),
                py(comboGroupY((478.0f - 70.0f) - comboTagGlowHeight * 0.5f)),
                ps(comboGroupS(comboTagGlowWidth)),
                ps(comboGroupS(comboTagGlowHeight)),
                apAlpha);
            const float comboTagWidth = 127.0f;
            const float comboTagHeight = 42.0f;
            drawImage(
                requireTexture(hudTextures, "combo_tag"),
                px(comboGroupX(1634.0f - comboTagWidth * 0.5f)),
                py(comboGroupY(478.0f - 67.0f - comboTagHeight * 0.5f)),
                ps(comboGroupS(comboTagWidth)),
                ps(comboGroupS(comboTagHeight)));
            const int latestComboIndex = upperBound(hudTimeline.comboTimes, chartTimeSec) - 1;
            float comboScale = COMBO_BASE_SCALE;
            float progress = 1000.0f;
            if (latestComboIndex >= 0) {
                progress = (chartTimeSec - hudTimeline.comboTimes[latestComboIndex]) * 60.0f;
                const float shiftScale = std::min(1.0f, std::max(0.5f, (progress / 8.0f) * 0.5f + 0.5f));
                comboScale = COMBO_BASE_SCALE * shiftScale;
            }
            const float burstAlpha = progress < 14.0f ? std::max(0.0f, 1.0f - progress / 14.0f) : 0.0f;

            const std::string comboText = std::to_string(hud.combo);
            const float mid = static_cast<float>(comboText.size()) / 2.0f;
            constexpr float comboCenterYOffset = 18.0f;
            for (size_t i = 0; i < comboText.size(); ++i) {
                const char ch = comboText[i];
                const float left = (static_cast<float>(i) - mid + 0.5f) * COMBO_DIGIT_STEP * comboScale;
                const float centerX = comboGroupX(1634.0f + left);
                const Texture& glow = requireTexture(hudTextures, std::string("combo_digit_b_") + ch);
                const Texture& main = requireTexture(hudTextures, std::string("combo_digit_n_") + ch);
                const float mainH = ps(comboGroupS(134.0f * comboScale));
                const float glowH = ps(comboGroupS(150.0f * comboScale));
                const float mainW = mainH * (static_cast<float>(main.width) / static_cast<float>(main.height));
                const float glowW = glowH * (static_cast<float>(glow.width) / static_cast<float>(glow.height));
                const float centerY = comboGroupY(478.0f + comboCenterYOffset * comboScale);
                const float digitGlowAlpha = std::min(1.0f, 0.18f + apAlpha * 0.82f);
                drawImage(glow, px(centerX) - glowW * 0.5f, py(centerY) - glowH * 0.5f, glowW, glowH, digitGlowAlpha);
                drawImage(main, px(centerX) - mainW * 0.5f, py(centerY) - mainH * 0.5f, mainW, mainH);

                if (burstAlpha > 0.0f) {
                    // Hit burst pass: intentionally larger than base glow.
                    const float burstScaleMul = 1.28f + 0.22f * burstAlpha;
                    const float burstGlowH = glowH * burstScaleMul;
                    const float burstGlowW = glowW * burstScaleMul;
                    const float burstGlowAlpha = std::min(1.0f, (0.35f + apAlpha * 0.65f) * burstAlpha);
                    drawImage(
                        glow,
                        px(centerX) - burstGlowW * 0.5f,
                        py(centerY) - burstGlowH * 0.5f,
                        burstGlowW,
                        burstGlowH,
                        burstGlowAlpha);
                }
            }
        }

        if (hud.showPerfect) {
            const int latestJudgeIndex = upperBound(hudTimeline.judgeTimes, chartTimeSec) - 1;
            if (latestJudgeIndex >= 0) {
                const float progressFrames = (chartTimeSec - hudTimeline.judgeTimes[latestJudgeIndex]) * 60.0f;
                if (progressFrames >= 0.0f && progressFrames < 20.0f) {
                    float alpha = 1.0f;
                    float rawScale = 2.0f / 3.0f;
                    if (progressFrames < 2.0f) {
                        alpha = 0.0f;
                    } else if (progressFrames < 5.0f) {
                        rawScale = (2.0f / 3.0f) - std::pow(-1.45f + progressFrames / 4.0f, 4.0f) * (2.0f / 3.0f);
                    }
                    const float scale = std::max(0.01f, rawScale * 1.5f);
                    const float w = ps(310.0f * scale);
                    const float h = ps(81.0f * scale);
                    drawImage(requireTexture(hudTextures, "judge_perfect"), px(960.0f) - w * 0.5f, py(667.5f) - h * 0.5f, w, h, alpha);
                }
            }
        }

        const float hudLocalTimeSec = outputTimeSec - (HUD_INTRO_DURATION_SEC + INTRO_CLEAN_BG_DURATION_SEC);
        if (hudLocalTimeSec >= AUTO_BADGE_BLINK_START_SEC) {
            const float blinkPhase = std::fmod(
                (hudLocalTimeSec - AUTO_BADGE_BLINK_START_SEC) / AUTO_BADGE_BLINK_PERIOD_SEC,
                AUTO_BADGE_BLINK_MOD_SEC);
            const float autoAlpha = std::max(0.0f, std::sin(blinkPhase * 3.14159265359f));
            const Texture& autoBadge = requireTexture(hudTextures, "auto_badge");
            drawImage(
                autoBadge,
                px(1566.0f),
                py(988.0f),
                ps(330.0f),
                ps(74.0f),
                autoAlpha);
        }
    }

    [[nodiscard]] const char* resolveKeySoundKey(int kindValue, bool critical)
    {
        switch (kindValue) {
            case 1:
                return "criticalTap";
            case 2:
                return critical ? "flickCritical" : "flick";
            case 3:
                return critical ? "traceCritical" : "trace";
            case 4:
                return critical ? "tickCritical" : "tick";
            case 5:
                return critical ? "holdLoopCritical" : "holdLoop";
            case 0:
            default:
                return "perfect";
        }
    }

    [[nodiscard]] double keySoundGain(const char* key)
    {
        const std::string_view name = key ? std::string_view(key) : std::string_view();
        if (name == "perfect" || name == "criticalTap") {
            return 0.75;
        }
        if (name == "flick") {
            return 0.75;
        }
        if (name == "flickCritical") {
            return 0.8;
        }
        if (name == "trace") {
            return 0.8;
        }
        if (name == "traceCritical") {
            return 0.82;
        }
        if (name == "tick") {
            return 0.9;
        }
        if (name == "tickCritical") {
            return 0.92;
        }
        if (name == "holdLoop" || name == "holdLoopCritical") {
            return 0.7;
        }
        return 0.75;
    }

    [[nodiscard]] TransportStateNative transportStateFromCode(int code)
    {
        switch (code) {
            case 1:
                return TransportStateNative::Loading;
            case 2:
                return TransportStateNative::Ready;
            case 3:
                return TransportStateNative::Playing;
            case 4:
                return TransportStateNative::Paused;
            case 5:
                return TransportStateNative::Error;
            case 0:
            default:
                return TransportStateNative::Idle;
        }
    }

    void destroyOverlayState()
    {
        destroyHudTextures(gPlayer.hudTextures);
        gPlayer.hudTextures.clear();
        gPlayer.introFonts.reset();
        gPlayer.introCard.reset();
        gPlayer.hudTimeline.reset();
        if (gPlayer.imguiInitialized) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext();
            gPlayer.imguiInitialized = false;
        }
    }

    void ensureImgui()
    {
        if (gPlayer.imguiInitialized) {
            return;
        }
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
#ifdef __EMSCRIPTEN__
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
#endif
        ImGui::StyleColorsDark();
        ImGui_ImplOpenGL3_Init("#version 300 es");
        gPlayer.imguiInitialized = true;
    }

    constexpr float AUDIO_LOOK_AHEAD_SEC = 0.05f;

    void rebuildOverlayResources(const BinaryBlob* coverBlob)
    {
        destroyOverlayState();
        ensureImgui();
        gPlayer.introFonts = std::make_unique<IntroFontSet>(loadIntroFonts(gPlayer.fonts));
        gPlayer.hudTextures = loadHudTextures(gPlayer.assets, coverBlob);
    }

    [[nodiscard]] int lowerBoundHitEvent(float chartTimeSec)
    {
        const int count = getHitEventCount();
        const float* packed = getHitEventBufferPointer();
        const int stride = 7;
        int low = 0;
        int high = count;
        while (low < high) {
            const int mid = (low + high) / 2;
            const float eventTime = packed[mid * stride + 0];
            if (eventTime < chartTimeSec - 0.0001f) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        return low;
    }

    void emitHitSounds(float fromChartSec, float toChartSec);

    void resumeActiveHoldLoops(float chartTimeSec)
    {
        const int count = getHitEventCount();
        const float* packed = getHitEventBufferPointer();
        const int stride = 7;
        for (int index = 0; index < count; ++index) {
            const int offset = index * stride;
            const float kind = packed[offset + 3];
            const float endTimeSec = packed[offset + 5];
            if (static_cast<int>(std::lround(kind)) != 5 || endTimeSec < 0.0f) {
                continue;
            }
            const float eventTimeSec = packed[offset + 0];
            if (eventTimeSec < chartTimeSec - 0.0001f && endTimeSec > chartTimeSec + 0.0001f) {
                const bool critical = (static_cast<int>(std::lround(packed[offset + 4])) & 1) != 0;
                const char* key = resolveKeySoundKey(5, critical);
                const double gain = keySoundGain(key) * std::max(0.0f, packed[offset + 6]);
                jsAudioTriggerExtendable(
                    key,
                    gain,
                    gPlayer.effectiveLeadInSec + chartTimeSec,
                    gPlayer.effectiveLeadInSec + endTimeSec,
                    NOTE_AUDIO_DELAY_SEC);
            }
        }
    }

    void resetHitCursor(float chartTimeSec, bool clearSounds, bool resumeHoldLoops)
    {
        gPlayer.nextHitEventIndex = lowerBoundHitEvent(chartTimeSec);
        if (clearSounds) {
            jsAudioClearOneShots();
        }
        if (resumeHoldLoops) {
            resumeActiveHoldLoops(chartTimeSec);
            const float audioLookAheadSec = AUDIO_LOOK_AHEAD_SEC * static_cast<float>(gPlayer.playbackRate);
            emitHitSounds(chartTimeSec - audioLookAheadSec, chartTimeSec);
        }
    }

    void emitHitSounds(float fromChartSec, float toChartSec)
    {
        const int count = getHitEventCount();
        const float* packed = getHitEventBufferPointer();
        const int stride = 7;
        const float audioLookAheadSec = AUDIO_LOOK_AHEAD_SEC * static_cast<float>(gPlayer.playbackRate);
        const float fromTriggerSec = fromChartSec + audioLookAheadSec;
        const float toTriggerSec = toChartSec + audioLookAheadSec;
        while (gPlayer.nextHitEventIndex < count) {
            const int offset = gPlayer.nextHitEventIndex * stride;
            const float eventTimeSec = packed[offset + 0];
            if (eventTimeSec > toTriggerSec + 0.0001f) {
                break;
            }
            if (eventTimeSec >= fromTriggerSec - 0.0001f) {
                const int kindValue = static_cast<int>(std::lround(packed[offset + 3]));
                const bool critical = (static_cast<int>(std::lround(packed[offset + 4])) & 1) != 0;
                const char* key = resolveKeySoundKey(kindValue, critical);
                const double gain = keySoundGain(key) * std::max(0.0f, packed[offset + 6]);
                if (kindValue == 5) {
                    jsAudioTriggerExtendable(
                        key,
                        gain,
                        gPlayer.effectiveLeadInSec + toChartSec,
                        gPlayer.effectiveLeadInSec + packed[offset + 5],
                        NOTE_AUDIO_DELAY_SEC);
                } else {
                    jsAudioTriggerOneShot(key, gain, NOTE_AUDIO_DELAY_SEC);
                }
            }
            gPlayer.nextHitEventIndex += 1;
        }
    }

    PlayerSnapshot buildSnapshot()
    {
        PlayerSnapshot snapshot;
        snapshot.currentTimeSec = jsAudioGetCurrentTime();
        snapshot.durationSec = std::max(gPlayer.durationSec, static_cast<double>(jsAudioHasAudio() ? jsAudioGetCurrentTime() : 0.0));
        snapshot.chartEndSec = gPlayer.chartEndSec;
        snapshot.sourceOffsetSec = gPlayer.sourceOffsetSec;
        snapshot.effectiveLeadInSec = gPlayer.effectiveLeadInSec;
        snapshot.audioStartDelaySec = gPlayer.audioStartDelaySec;
        snapshot.apStartSec = gPlayer.apStartSec;
        snapshot.transportState = transportStateFromCode(jsAudioGetStateCode());
        snapshot.requiresGesture = jsAudioRequiresGesture() != 0;
        snapshot.hasAudio = jsAudioHasAudio() != 0;
        return snapshot;
    }

    void renderPlayerFrameInternal()
    {
        if (!gPlayer.initialized || !gPlayer.sessionLoaded || !gPlayer.renderer || !gPlayer.hudTimeline || !gPlayer.introCard || !gPlayer.introFonts) {
            return;
        }

        const PlayerSnapshot snapshot = buildSnapshot();
        const float outputTimeSec = static_cast<float>(snapshot.currentTimeSec);
        const float chartTimeSec = outputTimeSec - static_cast<float>(gPlayer.effectiveLeadInSec);
        const float playfieldVisibility = openingPlayfieldVisibility(outputTimeSec, gPlayer.introCard->hasContent);
        const bool gameplaySuppressed = playfieldVisibility <= 0.001f;

        jsAudioCleanupExtendables(outputTimeSec);

        if (gameplaySuppressed) {
            resetHitCursor(chartTimeSec, true, false);
        } else if (
            snapshot.transportState != TransportStateNative::Playing ||
            chartTimeSec < gPlayer.previousChartTimeSec ||
            chartTimeSec - gPlayer.previousChartTimeSec > 0.25f) {
            resetHitCursor(chartTimeSec, snapshot.transportState != TransportStateNative::Playing, snapshot.transportState == TransportStateNative::Playing);
        } else {
            emitHitSounds(gPlayer.previousChartTimeSec, chartTimeSec);
        }

        render(chartTimeSec);
        const float* packed = gameplaySuppressed ? nullptr : getQuadBufferPointer();
        const int quadCount = gameplaySuppressed ? 0 : getQuadCount();
        gPlayer.renderer->renderFrame(packed, quadCount, true, playfieldVisibility);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(
            static_cast<float>(gPlayer.renderer->previewRectWindow()[2]),
            static_cast<float>(gPlayer.renderer->previewRectWindow()[3]));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        drawHudOverlayFrame(
            *gPlayer.renderer,
            gPlayer.hudTextures,
            *gPlayer.hudTimeline,
            chartTimeSec,
            outputTimeSec,
            gPlayer.scorePlusTriggerSec,
            gPlayer.scorePlusValue,
            gPlayer.lastScoreEventIndex,
            playfieldVisibility);
        drawOpeningIntroOverlay(
            *gPlayer.renderer,
            gPlayer.hudTextures,
            *gPlayer.introCard,
            *gPlayer.introFonts,
            outputTimeSec);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        gPlayer.renderer->present();
        gPlayer.previousChartTimeSec = chartTimeSec;
    }
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int initPlayer(const char* canvasSelector, int width, int height, float dpr)
    {
        try {
            init(0);
            gPlayer.lastError.clear();
            gPlayer.canvasSelector = (canvasSelector && *canvasSelector) ? canvasSelector : "#preview-canvas";
            gPlayer.renderer = std::make_unique<GlRenderer>(gPlayer.canvasSelector, width, height, dpr, gPlayer.effectOpacity);
            resize(width, height, dpr);
            setPreviewConfig(0, 1, 1, 1, 0, 0, 10.5f, 0.74f, 0.5f, 0.0f, 1.0f, 1.0f);
            jsAudioEnsureEngine();
            gPlayer.initialized = true;
            gPlayer.sessionLoaded = false;
            return 1;
        } catch (const std::exception& exception) {
            gPlayer.lastError = exception.what();
            return 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE void resizePlayer(int width, int height, float dpr)
    {
        if (!gPlayer.renderer) {
            return;
        }
        resize(width, height, dpr);
        gPlayer.renderer->resize(width, height, dpr);
    }

    EMSCRIPTEN_KEEPALIVE int preloadAssetData(const char* key, const std::uint8_t* data, int length)
    {
        if (!key || !data || length <= 0) {
            return 0;
        }
        gPlayer.assets[std::string(key)] = BinaryBlob{std::vector<std::uint8_t>(data, data + length)};
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE int preloadFontData(const char* key, const std::uint8_t* data, int length)
    {
        if (!key || !data || length <= 0) {
            return 0;
        }
        gPlayer.fonts[std::string(key)] = BinaryBlob{std::vector<std::uint8_t>(data, data + length)};
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE int preloadSoundData(const char* key, const std::uint8_t* data, int length)
    {
        if (!key || !data || length <= 0) {
            return 0;
        }
        try {
            gPlayer.sounds[std::string(key)] = BinaryBlob{std::vector<std::uint8_t>(data, data + length)};
            return 1;
        } catch (const std::exception& exception) {
            gPlayer.lastError = exception.what();
            return 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE int loadSession(
        const char* susText,
        double sourceOffsetMs,
        double effectiveLeadInMs,
        const std::uint8_t* bgmData,
        int bgmLength,
        const std::uint8_t* coverData,
        int coverLength,
        const char* title,
        const char* lyricist,
        const char* composer,
        const char* arranger,
        const char* vocal,
        const char* difficulty)
    {
        try {
            if (!gPlayer.initialized || !gPlayer.renderer) {
                throw std::runtime_error("Player has not been initialized.");
            }
            if (!susText) {
                throw std::runtime_error("Missing SUS text.");
            }

            gPlayer.lastError.clear();
            gPlayer.sessionMetadata = {
                title ? title : "",
                lyricist ? lyricist : "",
                composer ? composer : "",
                arranger ? arranger : "",
                vocal ? vocal : "",
                difficulty ? difficulty : "",
            };

            gPlayer.sourceOffsetSec = sourceOffsetMs / 1000.0;
            gPlayer.effectiveLeadInSec = std::max(gPlayer.sourceOffsetSec, effectiveLeadInMs / 1000.0);
            gPlayer.audioStartDelaySec = std::max(0.0, gPlayer.effectiveLeadInSec - gPlayer.sourceOffsetSec);

            if (loadSusTextPrecise(susText, -gPlayer.effectiveLeadInSec * 1000.0) != 1) {
                throw std::runtime_error(std::string("loadSusTextPrecise failed: ") + getLastError());
            }

            BinaryBlob coverBlob;
            const BinaryBlob* coverBlobPtr = nullptr;
            if (coverData && coverLength > 0) {
                coverBlob.bytes.assign(coverData, coverData + coverLength);
                coverBlobPtr = &coverBlob;
            }

            gPlayer.renderer->setStageOpacity(gPlayer.stageOpacity);
            gPlayer.renderer->setStageCover(gPlayer.stageCover);
            gPlayer.renderer->loadAllTextures(gPlayer.assets, coverBlobPtr, gPlayer.noteSkin);
            rebuildOverlayResources(coverBlobPtr);

            const std::string metadataTitle = getMetadataTitle() ? std::string(getMetadataTitle()) : std::string();
            const std::string metadataArtist = getMetadataArtist() ? std::string(getMetadataArtist()) : std::string();
            gPlayer.hudTimeline = std::make_unique<HudTimelineNative>(buildHudTimeline());
            gPlayer.introCard = std::make_unique<IntroCardState>(
                buildIntroCardState(gPlayer.sessionMetadata, metadataTitle, metadataArtist, coverBlobPtr != nullptr));

            gPlayer.chartEndSec = getChartEndTimeSec();
            gPlayer.chartPlayableEndSec = static_cast<float>(gPlayer.chartEndSec);
            gPlayer.durationSec = std::max(1.0, gPlayer.chartEndSec + gPlayer.effectiveLeadInSec + 1.0);
            gPlayer.apStartSec = gPlayer.effectiveLeadInSec + gPlayer.chartEndSec + 1.0;
            gPlayer.scorePlusTriggerSec = -1000.0f;
            gPlayer.scorePlusValue = 0;
            gPlayer.lastScoreEventIndex = -1;
            gPlayer.previousChartTimeSec = -1000.0f;
            gPlayer.nextHitEventIndex = 0;
            gPlayer.playbackRate = 1.0;

            jsAudioSetDuration(gPlayer.durationSec);
            jsAudioSetStartOffset(gPlayer.audioStartDelaySec);
            for (const auto& [soundKey, soundBlob] : gPlayer.sounds) {
                if (soundBlob.bytes.empty()) {
                    continue;
                }
                if (jsAudioLoadSound(soundKey.c_str(), soundBlob.bytes.data(), static_cast<int>(soundBlob.bytes.size())) == 0) {
                    const char* audioError = jsAudioGetLastError();
                    if (audioError && *audioError) {
                        gPlayer.lastError = audioError;
                        break;
                    }
                }
            }
            if (jsAudioLoadBgm(bgmData, bgmLength) == 0) {
                const char* audioError = jsAudioGetLastError();
                if (audioError && *audioError) {
                    gPlayer.lastError = audioError;
                }
            }
            jsAudioSeek(0.0);
            jsAudioPause();
            resetHitCursor(static_cast<float>(-gPlayer.effectiveLeadInSec), true, false);
            gPlayer.sessionLoaded = true;
            return 1;
        } catch (const std::exception& exception) {
            gPlayer.lastError = exception.what();
            gPlayer.sessionLoaded = false;
            return 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE int loadCustomScoreJsonSession(
        const char* jsonText,
        double sourceOffsetMs,
        double effectiveLeadInMs,
        const std::uint8_t* bgmData,
        int bgmLength,
        const std::uint8_t* coverData,
        int coverLength,
        const char* title,
        const char* lyricist,
        const char* composer,
        const char* arranger,
        const char* vocal,
        const char* difficulty)
    {
        try {
            if (!gPlayer.initialized || !gPlayer.renderer) {
                throw std::runtime_error("Player has not been initialized.");
            }
            if (!jsonText) {
                throw std::runtime_error("Missing custom score JSON text.");
            }

            gPlayer.lastError.clear();
            gPlayer.sessionMetadata = {
                title ? title : "",
                lyricist ? lyricist : "",
                composer ? composer : "",
                arranger ? arranger : "",
                vocal ? vocal : "",
                difficulty ? difficulty : "",
            };

            gPlayer.sourceOffsetSec = sourceOffsetMs / 1000.0;
            gPlayer.effectiveLeadInSec = std::max(gPlayer.sourceOffsetSec, effectiveLeadInMs / 1000.0);
            gPlayer.audioStartDelaySec = std::max(0.0, gPlayer.effectiveLeadInSec - gPlayer.sourceOffsetSec);

            if (loadCustomScoreJsonTextPrecise(jsonText, -gPlayer.effectiveLeadInSec * 1000.0) != 1) {
                throw std::runtime_error(std::string("loadCustomScoreJsonTextPrecise failed: ") + getLastError());
            }

            BinaryBlob coverBlob;
            const BinaryBlob* coverBlobPtr = nullptr;
            if (coverData && coverLength > 0) {
                coverBlob.bytes.assign(coverData, coverData + coverLength);
                coverBlobPtr = &coverBlob;
            }

            gPlayer.renderer->setStageOpacity(gPlayer.stageOpacity);
            gPlayer.renderer->setStageCover(gPlayer.stageCover);
            gPlayer.renderer->loadAllTextures(gPlayer.assets, coverBlobPtr, gPlayer.noteSkin);
            rebuildOverlayResources(coverBlobPtr);

            const std::string metadataTitle = getMetadataTitle() ? std::string(getMetadataTitle()) : std::string();
            const std::string metadataArtist = getMetadataArtist() ? std::string(getMetadataArtist()) : std::string();
            gPlayer.hudTimeline = std::make_unique<HudTimelineNative>(buildHudTimeline());
            gPlayer.introCard = std::make_unique<IntroCardState>(
                buildIntroCardState(gPlayer.sessionMetadata, metadataTitle, metadataArtist, coverBlobPtr != nullptr));

            gPlayer.chartEndSec = getChartEndTimeSec();
            gPlayer.chartPlayableEndSec = static_cast<float>(gPlayer.chartEndSec);
            gPlayer.durationSec = std::max(1.0, gPlayer.chartEndSec + gPlayer.effectiveLeadInSec + 1.0);
            gPlayer.apStartSec = gPlayer.effectiveLeadInSec + gPlayer.chartEndSec + 1.0;
            gPlayer.scorePlusTriggerSec = -1000.0f;
            gPlayer.scorePlusValue = 0;
            gPlayer.lastScoreEventIndex = -1;
            gPlayer.previousChartTimeSec = -1000.0f;
            gPlayer.nextHitEventIndex = 0;
            gPlayer.playbackRate = 1.0;

            jsAudioSetDuration(gPlayer.durationSec);
            jsAudioSetStartOffset(gPlayer.audioStartDelaySec);
            for (const auto& [soundKey, soundBlob] : gPlayer.sounds) {
                if (soundBlob.bytes.empty()) {
                    continue;
                }
                if (jsAudioLoadSound(soundKey.c_str(), soundBlob.bytes.data(), static_cast<int>(soundBlob.bytes.size())) == 0) {
                    const char* audioError = jsAudioGetLastError();
                    if (audioError && *audioError) {
                        gPlayer.lastError = audioError;
                        break;
                    }
                }
            }
            if (jsAudioLoadBgm(bgmData, bgmLength) == 0) {
                const char* audioError = jsAudioGetLastError();
                if (audioError && *audioError) {
                    gPlayer.lastError = audioError;
                }
            }
            jsAudioSeek(0.0);
            jsAudioPause();
            resetHitCursor(static_cast<float>(-gPlayer.effectiveLeadInSec), true, false);
            gPlayer.sessionLoaded = true;
            return 1;
        } catch (const std::exception& exception) {
            gPlayer.lastError = exception.what();
            gPlayer.sessionLoaded = false;
            return 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE int playPlayer()
    {
        return jsAudioPlay();
    }

    EMSCRIPTEN_KEEPALIVE int unlockPlayerAudio()
    {
        return jsAudioUnlock();
    }

    EMSCRIPTEN_KEEPALIVE void pausePlayer()
    {
        jsAudioPause();
    }

    EMSCRIPTEN_KEEPALIVE void seekPlayer(double outputTimeSec)
    {
        jsAudioSeek(outputTimeSec);
        resetHitCursor(static_cast<float>(outputTimeSec - gPlayer.effectiveLeadInSec), true, false);
        gPlayer.previousChartTimeSec = static_cast<float>(outputTimeSec - gPlayer.effectiveLeadInSec);
    }

    EMSCRIPTEN_KEEPALIVE void setPlayerPlaybackRate(double playbackRate)
    {
        gPlayer.playbackRate = std::max(0.05, playbackRate);
        jsAudioSetPlaybackRate(playbackRate);
    }

    EMSCRIPTEN_KEEPALIVE void setPlayerAudioVolumes(double bgmVolume, double soundVolume)
    {
        jsAudioSetVolumes(bgmVolume, soundVolume);
    }

    EMSCRIPTEN_KEEPALIVE void setPlayerPreviewConfig(
        int mirror,
        int flickAnimation,
        int holdAnimation,
        int simultaneousLine,
        int effectProfile,
        int noteSkin,
        float noteSpeed,
        float holdAlpha,
        float guideAlpha,
        float stageCover,
        float stageOpacity,
        float backgroundBrightness,
        float effectOpacity)
    {
        gPlayer.effectOpacity = std::clamp(effectOpacity, 0.0f, 1.0f);
        gPlayer.stageOpacity = std::clamp(stageOpacity, 0.0f, 1.0f);
        gPlayer.stageCover = std::clamp(stageCover, 0.0f, 1.0f);
        gPlayer.noteSkin = noteSkin == 1 ? 1 : 0;
        if (gPlayer.renderer) {
            gPlayer.renderer->setEffectOpacity(gPlayer.effectOpacity);
            gPlayer.renderer->setStageOpacity(gPlayer.stageOpacity);
            gPlayer.renderer->setStageCover(gPlayer.stageCover);
            gPlayer.renderer->setBackgroundBrightness(std::clamp(backgroundBrightness, 0.0f, 1.0f));
            if (gPlayer.sessionLoaded) {
                gPlayer.renderer->loadNoteSkinTextures(gPlayer.assets, gPlayer.noteSkin);
            }
        }
        setPreviewConfig(
            mirror,
            flickAnimation,
            holdAnimation,
            simultaneousLine,
            effectProfile,
            gPlayer.noteSkin,
            noteSpeed,
            holdAlpha,
            guideAlpha,
            gPlayer.stageCover,
            stageOpacity,
            backgroundBrightness);
    }

    EMSCRIPTEN_KEEPALIVE void renderPlayerFrame()
    {
        renderPlayerFrameInternal();
    }

    EMSCRIPTEN_KEEPALIVE double getPlayerCurrentTimeSec()
    {
        return buildSnapshot().currentTimeSec;
    }

    EMSCRIPTEN_KEEPALIVE double getPlayerDurationSec()
    {
        return gPlayer.durationSec;
    }

    EMSCRIPTEN_KEEPALIVE double getPlayerChartEndSec()
    {
        return gPlayer.chartEndSec;
    }

    EMSCRIPTEN_KEEPALIVE double getPlayerSourceOffsetSec()
    {
        return gPlayer.sourceOffsetSec;
    }

    EMSCRIPTEN_KEEPALIVE double getPlayerEffectiveLeadInSec()
    {
        return gPlayer.effectiveLeadInSec;
    }

    EMSCRIPTEN_KEEPALIVE double getPlayerAudioStartDelaySec()
    {
        return gPlayer.audioStartDelaySec;
    }

    EMSCRIPTEN_KEEPALIVE double getPlayerApStartSec()
    {
        return gPlayer.apStartSec;
    }

    EMSCRIPTEN_KEEPALIVE int getPlayerTransportState()
    {
        return jsAudioGetStateCode();
    }

    EMSCRIPTEN_KEEPALIVE int getPlayerRequiresGesture()
    {
        return jsAudioRequiresGesture();
    }

    EMSCRIPTEN_KEEPALIVE int getPlayerHasAudio()
    {
        return jsAudioHasAudio();
    }

    EMSCRIPTEN_KEEPALIVE const char* getPlayerWarningText()
    {
        if (!gPlayer.lastError.empty()) {
            return gPlayer.lastError.c_str();
        }
        return jsAudioGetLastError();
    }

    EMSCRIPTEN_KEEPALIVE void disposePlayer()
    {
        jsAudioPause();
        jsAudioClearOneShots();
        destroyOverlayState();
        gPlayer.renderer.reset();
        gPlayer.sessionLoaded = false;
        gPlayer.initialized = false;
        dispose();
    }
}
