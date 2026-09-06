#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif
#endif

#include "generated_resources.h"
#include "../vendor/nlohmann/json.hpp"
#include "../mmw_port/ApplicationConfiguration.h"
#include "../mmw_port/EffectView.h"
#include "../mmw_port/ResourceManager.h"
#include "../mmw_port/ScoreContext.h"
#include "../mmw_port/Rendering/Renderer.h"

namespace mmw_preview
{
    namespace mmw = MikuMikuWorld;
    constexpr int TICKS_PER_BEAT = 480;
    constexpr int MIN_LANE = 0;
    constexpr int MAX_LANE = 11;
    constexpr int MAX_FLICK_SPRITES = 6;
    constexpr int NOTE_SIDE_WIDTH = 91;
    constexpr int NOTE_SIDE_PAD = 10;
    constexpr int HOLD_XCUTOFF = 36;
    constexpr int GUIDE_XCUTOFF = 3;
    constexpr int GUIDE_Y_TOP_CUTOFF = -41;
    constexpr int GUIDE_Y_BOTTOM_CUTOFF = -12;
    constexpr double NUM_PI = 3.14159265358979323846;
    constexpr uint8_t HUD_FLAG_CRITICAL = 1u << 0;
    constexpr uint8_t HUD_FLAG_HALF_BEAT = 1u << 1;
    constexpr uint8_t HUD_FLAG_SHOW_JUDGE = 1u << 2;

    constexpr float STAGE_LANE_TOP = 47.0f;
    constexpr float STAGE_LANE_HEIGHT = 850.0f;
    constexpr float STAGE_LANE_WIDTH = 1420.0f;
    constexpr float STAGE_NUM_LANES = 12.0f;
    constexpr float STAGE_TEX_WIDTH = 2048.0f;
    constexpr float STAGE_TEX_HEIGHT = 1176.0f;
    constexpr float STAGE_NOTE_HEIGHT = 75.0f;
    constexpr float STAGE_TARGET_WIDTH = 1920.0f;
    constexpr float STAGE_TARGET_HEIGHT = 1080.0f;
    constexpr float STAGE_ASPECT_RATIO = STAGE_TARGET_WIDTH / STAGE_TARGET_HEIGHT;
    constexpr float STAGE_ZOOM = 927.0f / 800.0f;
    constexpr float STAGE_WIDTH_RATIO = STAGE_ZOOM * STAGE_LANE_WIDTH / (STAGE_TEX_HEIGHT * STAGE_ASPECT_RATIO) / STAGE_NUM_LANES;
    constexpr float STAGE_HEIGHT_RATIO = STAGE_ZOOM * STAGE_LANE_HEIGHT / STAGE_TEX_HEIGHT;
    constexpr float SCALED_ASPECT_RATIO = (STAGE_TARGET_WIDTH * STAGE_WIDTH_RATIO) / (STAGE_TARGET_HEIGHT * STAGE_HEIGHT_RATIO);
    constexpr float EFFECTS_TARGET_ASPECT = 16.0f / 9.0f;

    enum class TextureId : int
    {
        Notes = 0,
        LongNoteLine = 1,
        TouchLine = 2,
    };

    enum class NoteType
    {
        Tap,
        Hold,
        HoldMid,
        HoldEnd,
    };

    enum class FlickType
    {
        None,
        Default,
        Left,
        Right,
        FlickTypeCount,
    };

    enum class HoldStepType
    {
        Normal,
        Hidden,
        Skip,
    };

    enum class HoldNoteType
    {
        Normal,
        Hidden,
        Guide,
    };

    enum class EaseType
    {
        Linear,
        EaseIn,
        EaseOut,
    };

    enum class SpriteLayer : uint8_t
    {
        FLICK_ARROW,
        DIAMOND,
        BASE_NOTE,
        TICK_NOTE,
        HOLD_PATH,
        GUIDE_PATH,
        UNDER_NOTE_EFFECT,
    };

    enum SpriteTransformIndex : size_t
    {
        TransformNoteLeft = 0,
        TransformNoteMiddle = 1,
        TransformNoteRight = 2,
        TransformTraceDiamond = 3,
        TransformFlickArrowLeft1 = 4,
        TransformFlickArrowLeft2 = 5,
        TransformFlickArrowLeft3 = 6,
        TransformFlickArrowLeft4 = 7,
        TransformFlickArrowLeft5 = 8,
        TransformFlickArrowLeft6 = 9,
        TransformFlickArrowUp1 = 10,
        TransformFlickArrowUp2 = 11,
        TransformFlickArrowUp3 = 12,
        TransformFlickArrowUp4 = 13,
        TransformFlickArrowUp5 = 14,
        TransformFlickArrowUp6 = 15,
        TransformSimultaneousLine = 16,
        TransformHoldTick = 17,
    };

    enum NoteSpriteIndex : int
    {
        SPR_NOTE_CRITICAL,
        SPR_NOTE_FLICK,
        SPR_NOTE_LONG,
        SPR_NOTE_TAP,
        SPR_NOTE_FRICTION,
        SPR_NOTE_FRICTION_CRITICAL,
        SPR_NOTE_FRICTION_FLICK,
        SPR_NOTE_LONG_AMONG,
        SPR_NOTE_LONG_AMONG_CRITICAL,
        SPR_NOTE_FRICTION_AMONG,
        SPR_NOTE_FRICTION_AMONG_CRITICAL,
        SPR_NOTE_FRICTION_AMONG_FLICK,
        SPR_FLICK_ARROW_01,
        SPR_FLICK_ARROW_01_DIAGONAL,
        SPR_FLICK_ARROW_02,
        SPR_FLICK_ARROW_02_DIAGONAL,
        SPR_FLICK_ARROW_03,
        SPR_FLICK_ARROW_03_DIAGONAL,
        SPR_FLICK_ARROW_04,
        SPR_FLICK_ARROW_04_DIAGONAL,
        SPR_FLICK_ARROW_05,
        SPR_FLICK_ARROW_05_DIAGONAL,
        SPR_FLICK_ARROW_06,
        SPR_FLICK_ARROW_06_DIAGONAL,
        SPR_FLICK_ARROW_CRITICAL_01,
        SPR_FLICK_ARROW_CRITICAL_01_DIAGONAL,
        SPR_FLICK_ARROW_CRITICAL_02,
        SPR_FLICK_ARROW_CRITICAL_02_DIAGONAL,
        SPR_FLICK_ARROW_CRITICAL_03,
        SPR_FLICK_ARROW_CRITICAL_03_DIAGONAL,
        SPR_FLICK_ARROW_CRITICAL_04,
        SPR_FLICK_ARROW_CRITICAL_04_DIAGONAL,
        SPR_FLICK_ARROW_CRITICAL_05,
        SPR_FLICK_ARROW_CRITICAL_05_DIAGONAL,
        SPR_FLICK_ARROW_CRITICAL_06,
        SPR_FLICK_ARROW_CRITICAL_06_DIAGONAL,
        SPR_SIMULTANEOUS_CONNECTION,
    };

    struct Range
    {
        double min{};
        double max{};
    };

    struct Vec2
    {
        float x{};
        float y{};
    };

    using QuadPoints = std::array<Vec2, 4>;
    using QuadUvs = std::array<Vec2, 4>;
    using QuadReciprocalW = std::array<float, 4>;

    struct Tempo
    {
        int tick{};
        float bpm{160.0f};
    };

    struct HiSpeedChange
    {
        int tick{};
        float speed{1.0f};
    };

    struct SEVolumeChange
    {
        int tick{};
        float volume{1.0f};
    };

    struct Note
    {
        NoteType type{NoteType::Tap};
        int ID{};
        int parentID{-1};
        int tick{};
        int lane{};
        int width{3};
        float speedRatio{1.0f};
        bool critical{false};
        bool friction{false};
        FlickType flick{FlickType::None};

        [[nodiscard]] bool isFlick() const
        {
            return flick != FlickType::None && type != NoteType::Hold && type != NoteType::HoldMid;
        }
    };

    struct HoldStep
    {
        int ID{};
        HoldStepType type{HoldStepType::Normal};
        EaseType ease{EaseType::Linear};
    };

    struct HoldNote
    {
        HoldStep start{};
        std::vector<HoldStep> steps;
        int end{};
        HoldNoteType startType{HoldNoteType::Normal};
        HoldNoteType endType{HoldNoteType::Normal};

        [[nodiscard]] bool isGuide() const
        {
            return startType == HoldNoteType::Guide || endType == HoldNoteType::Guide;
        }
    };

    struct ScoreMetadata
    {
        std::string title;
        std::string artist;
        std::string author;
        float musicOffset{};
    };

    struct Score
    {
        ScoreMetadata metadata;
        std::map<int, Note> notes;
        std::map<int, HoldNote> holdNotes;
        std::vector<Tempo> tempoChanges;
        std::vector<HiSpeedChange> hiSpeedChanges;
        std::vector<SEVolumeChange> seVolumeChanges;
    };

    struct SUSNote
    {
        int tick{};
        int lane{};
        int width{};
        int type{};
        float speedRatio{1.0f};
    };

    struct BPM
    {
        int tick{};
        float bpm{};
    };

    struct Bar
    {
        int measure{};
        int ticksPerMeasure{};
        int ticks{};
    };

    struct BarLength
    {
        int bar{};
        float length{};
    };

    struct HiSpeed
    {
        int tick{};
        float speed{};
    };

    struct SEVolume
    {
        int tick{};
        float volume{};
    };

    struct SUSMetadata
    {
        std::map<std::string, std::string> data;
        float waveOffset{};
    };

    using SUSNoteStream = std::vector<std::vector<SUSNote>>;

    struct SUS
    {
        SUSMetadata metadata;
        std::vector<SUSNote> taps;
        std::vector<SUSNote> directionals;
        SUSNoteStream slides;
        SUSNoteStream guides;
        std::vector<BPM> bpms;
        std::vector<BarLength> barlengths;
        std::vector<HiSpeed> hiSpeeds;
        std::vector<SEVolume> seVolumes;
    };

    struct DrawingNote
    {
        int refID{};
        Range visualTime{};
    };

    struct DrawingLine
    {
        Range xPos{};
        Range visualTime{};
        float visibleDuration{};
    };

    struct DrawingHoldTick
    {
        int refID{};
        float center{};
        Range visualTime{};
        float visibleDuration{};
    };

    struct DrawingHoldSegment
    {
        int endID{};
        EaseType ease{EaseType::Linear};
        bool isGuide{};
        bool critical{};
        ptrdiff_t tailStepIndex{};
        double headTime{};
        double tailTime{};
        float headLeft{};
        float headRight{};
        float tailLeft{};
        float tailRight{};
        float startTime{};
        float endTime{};
        double activeTime{};
        float visibleDuration{};
    };

    struct DrawData
    {
        float noteSpeed{10.5f};
        int maxTicks{1};
        std::vector<DrawingNote> drawingNotes;
        std::vector<DrawingLine> drawingLines;
        std::vector<DrawingHoldTick> drawingHoldTicks;
        std::vector<DrawingHoldSegment> drawingHoldSegments;

        void clear()
        {
            drawingNotes.clear();
            drawingLines.clear();
            drawingHoldTicks.clear();
            drawingHoldSegments.clear();
            maxTicks = 1;
        }
    };

    struct PreviewRuntimeConfig
    {
        bool mirror{false};
        bool flickAnimation{true};
        bool holdAnimation{true};
        bool simultaneousLine{true};
        int effectProfile{};
        int noteSkin{};
        float noteSpeed{10.5f};
        float holdAlpha{1.0f};
        float guideAlpha{0.8f};
        float stageCover{};
        float stageOpacity{1.0f};
        float backgroundBrightness{1.0f};
    };

    struct RenderQuad
    {
        QuadPoints positions{};
        QuadUvs uvs{};
        float r{1.0f};
        float g{1.0f};
        float b{1.0f};
        float a{1.0f};
        QuadReciprocalW reciprocalW{1.0f, 1.0f, 1.0f, 1.0f};
        int texture{};
        int zIndex{};
    };

    struct HitEvent
    {
        float timeSec{};
        float center{};
        float width{};
        float kind{};
        float flags{};
        float endTimeSec{};
        float volume{1.0f};
    };

    enum class HudEventKind : int
    {
        Tap = 0,
        CriticalTap = 1,
        Flick = 2,
        Trace = 3,
        Tick = 4,
        HoldHalfBeat = 5,
    };

    struct HudEvent
    {
        float timeSec{};
        float weight{};
        float kind{};
        float flags{};
    };

    struct RuntimeState
    {
        PreviewRuntimeConfig config{};
        Score score{};
        DrawData drawData{};
        bool loaded{};
        int width{};
        int height{};
        float dpr{1.0f};
        std::string lastError;
        std::vector<RenderQuad> renderQuads;
        std::vector<float> packedQuads;
        std::vector<HitEvent> hitEvents;
        std::vector<float> packedHitEvents;
        std::vector<HudEvent> hudEvents;
        std::vector<float> packedHudEvents;
        mmw::ScoreContext effectContext{};
        mmw::Effect::EffectView effectView{};
        mmw::Camera effectCamera{};
        mmw::Renderer effectRenderer{};
        std::vector<mmw::EffectOutputQuad> effectQuads;
        float lastEffectTimeSec{-1000.0f};
    };

    RuntimeState gRuntime{};
    int gNextID = 1;

    [[nodiscard]] std::string trim(std::string_view input)
    {
        const auto begin = input.find_first_not_of(" \t\r\n");
        if (begin == std::string_view::npos) {
            return {};
        }
        const auto end = input.find_last_not_of(" \t\r\n");
        return std::string(input.substr(begin, end - begin + 1));
    }

    [[nodiscard]] bool startsWith(std::string_view value, std::string_view prefix)
    {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    [[nodiscard]] bool endsWith(std::string_view value, std::string_view suffix)
    {
        return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
    }

    [[nodiscard]] bool isDigitString(std::string_view value)
    {
        if (value.empty()) {
            return false;
        }
        return std::all_of(value.begin(), value.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
    }

    [[nodiscard]] std::vector<std::string> split(std::string_view input, char delimiter)
    {
        std::vector<std::string> result;
        std::string part;
        std::stringstream stream{std::string(input)};
        while (std::getline(stream, part, delimiter)) {
            result.push_back(part);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> splitWhitespace(std::string_view input)
    {
        std::vector<std::string> result;
        std::stringstream stream{std::string(input)};
        std::string part;
        while (stream >> part) {
            result.push_back(part);
        }
        return result;
    }

    [[nodiscard]] float lerp(float start, float end, float ratio)
    {
        return start + ratio * (end - start);
    }

    [[nodiscard]] double lerpD(double start, double end, double ratio)
    {
        return start + ratio * (end - start);
    }

    [[nodiscard]] float unlerp(float start, float end, float value)
    {
        return (value - start) / (end - start);
    }

    [[nodiscard]] double unlerpD(double start, double end, double value)
    {
        return (value - start) / (end - start);
    }

    [[nodiscard]] float easeIn(float start, float end, float ratio)
    {
        return lerp(start, end, ratio * ratio);
    }

    [[nodiscard]] float easeOut(float start, float end, float ratio)
    {
        return lerp(start, end, 1.0f - (1.0f - ratio) * (1.0f - ratio));
    }

    [[nodiscard]] float cubicEaseIn(float t)
    {
        return t * t * t;
    }

    [[nodiscard]] auto getEaseFunction(EaseType ease)
    {
        switch (ease) {
            case EaseType::EaseIn:
                return easeIn;
            case EaseType::EaseOut:
                return easeOut;
            case EaseType::Linear:
            default:
                return lerp;
        }
    }

    [[nodiscard]] float ticksToSec(int ticks, int beatTicks, float bpm)
    {
        return ticks * (60.0f / bpm / static_cast<float>(beatTicks));
    }

    [[nodiscard]] int secsToTicks(float seconds, int beatTicks, float bpm)
    {
        return static_cast<int>(seconds / (60.0f / bpm / static_cast<float>(beatTicks)));
    }

    [[nodiscard]] float accumulateDuration(int tick, int beatTicks, const std::vector<Tempo>& tempos)
    {
        if (tempos.empty()) {
            return 0.0f;
        }

        float total = 0.0f;
        int accTicks = 0;
        int lastTempo = 0;

        for (int i = 0; i < static_cast<int>(tempos.size()) - 1; ++i) {
            lastTempo = i;
            const int ticks = tempos[i + 1].tick - tempos[i].tick;
            if (accTicks + ticks >= tick) {
                break;
            }
            accTicks += ticks;
            total += ticksToSec(ticks, beatTicks, tempos[i].bpm);
            lastTempo = i + 1;
        }

        total += ticksToSec(tick - tempos[lastTempo].tick, beatTicks, tempos[lastTempo].bpm);
        return total;
    }

    [[nodiscard]] double accumulateScaledDuration(int tick, int beatTicks, const std::vector<Tempo>& tempos, const std::vector<HiSpeedChange>& hiSpeeds)
    {
        if (tempos.empty()) {
            return 0.0;
        }
        if (tick <= 0) {
            const float currentBpm = tempos.front().bpm;
            return ticksToSec(tick, beatTicks, currentBpm);
        }

        int previousTempo = 0;
        int previousSpeed = -1;
        int accTicks = 0;
        double total = 0.0;

        while (accTicks < tick) {
            const int nextTempoTick = previousTempo + 1 < static_cast<int>(tempos.size()) ? tempos[previousTempo + 1].tick : std::numeric_limits<int>::max();
            const int nextSpeedTick = previousSpeed + 1 < static_cast<int>(hiSpeeds.size()) ? hiSpeeds[previousSpeed + 1].tick : std::numeric_limits<int>::max();
            const int nextTick = std::min({nextTempoTick, nextSpeedTick, tick});
            const float currentBpm = tempos[previousTempo].bpm;
            const float currentSpeed = previousSpeed >= 0 ? hiSpeeds[previousSpeed].speed : 1.0f;

            total += ticksToSec(nextTick - accTicks, beatTicks, currentBpm) * currentSpeed;

            if (nextTick == nextTempoTick) {
                ++previousTempo;
            }
            if (nextTick == nextSpeedTick) {
                ++previousSpeed;
            }
            accTicks = nextTick;
        }

        return total;
    }

    [[nodiscard]] int accumulateTicks(float seconds, int beatTicks, const std::vector<Tempo>& tempos)
    {
        if (tempos.empty()) {
            return 0;
        }

        int total = 0;
        float accSeconds = 0.0f;
        int lastTempo = 0;

        for (int i = 0; i < static_cast<int>(tempos.size()) - 1; ++i) {
            lastTempo = i;
            const float segmentSeconds = ticksToSec(tempos[i + 1].tick - tempos[i].tick, beatTicks, tempos[i].bpm);
            if (accSeconds + segmentSeconds >= seconds) {
                break;
            }
            total += secsToTicks(segmentSeconds, beatTicks, tempos[i].bpm);
            accSeconds += segmentSeconds;
            lastTempo = i + 1;
        }

        total += secsToTicks(seconds - accSeconds, beatTicks, tempos[lastTempo].bpm);
        return total;
    }

    [[nodiscard]] float laneToLeft(float lane)
    {
        return lane - 6.0f;
    }

    [[nodiscard]] float getNoteCenter(const Note& note)
    {
        return laneToLeft(static_cast<float>(note.lane)) + static_cast<float>(note.width) / 2.0f;
    }

    [[nodiscard]] float getNoteDuration(float noteSpeed)
    {
        return static_cast<float>(lerpD(0.35, 4.0, std::pow(unlerpD(12.0, 1.0, noteSpeed), 1.31)));
    }

    [[nodiscard]] float sanitizeSpeedRatio(float speedRatio)
    {
        return std::isfinite(speedRatio) && speedRatio > 0.0f ? speedRatio : 1.0f;
    }

    [[nodiscard]] float sanitizeSEVolume(float volume)
    {
        return std::isfinite(volume) && volume >= 0.0f ? volume : 1.0f;
    }

    [[nodiscard]] double approach(double startTime, double endTime, double currentTime)
    {
        return std::pow(1.06, 45.0 * lerpD(-1.0, 0.0, unlerpD(startTime, endTime, currentTime)));
    }

    [[nodiscard]] float getNoteHeight()
    {
        return STAGE_NOTE_HEIGHT / STAGE_LANE_HEIGHT / 2.0f;
    }

    [[nodiscard]] int getZIndex(SpriteLayer layer, float xOffset, float yOffset)
    {
        const auto clampFloat = [](float value, float minValue, float maxValue) {
            return value < minValue ? minValue : (value <= maxValue ? value : maxValue);
        };

        constexpr int32_t mask24 = 0xFFFFFF;
        constexpr int32_t mask4 = 0x0F;
        const int32_t y = static_cast<int32_t>(clampFloat(1.0f - yOffset, 0.0f, 1.0f) * static_cast<float>(mask24) + 0.5f);
        const int32_t x = static_cast<int32_t>(clampFloat(xOffset / 12.0f + 0.5f, 0.0f, 1.0f) * 12.0f + 0.5f);

        return std::numeric_limits<int32_t>::max()
            - ((static_cast<int32_t>(layer) & mask4) << 28)
            - ((y & mask24) << 4)
            - ((x & mask4) << 0);
    }

    [[nodiscard]] float getEffectiveSpeedRatio(const Note& note, const Score& score)
    {
        float speedRatio = note.speedRatio;
        if (note.type == NoteType::HoldMid || note.type == NoteType::HoldEnd) {
            auto parent = score.notes.find(note.parentID);
            if (parent != score.notes.end()) {
                speedRatio = parent->second.speedRatio;
            }
        }
        return sanitizeSpeedRatio(speedRatio);
    }

    [[nodiscard]] float getNoteVisibleDuration(const Note& note, const Score& score, float noteSpeed)
    {
        return getNoteDuration(noteSpeed) / getEffectiveSpeedRatio(note, score);
    }

    [[nodiscard]] float getSEVolumeAtTick(int tick, const Score& score)
    {
        for (auto it = score.seVolumeChanges.rbegin(); it != score.seVolumeChanges.rend(); ++it) {
            if (it->tick <= tick) {
                return sanitizeSEVolume(it->volume);
            }
        }
        return 1.0f;
    }

    [[nodiscard]] Range getNoteVisualTime(const Note& note, const Score& score, float noteSpeed)
    {
        const double targetTime = accumulateScaledDuration(note.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges);
        return {targetTime - getNoteVisibleDuration(note, score, noteSpeed), targetTime};
    }

    [[nodiscard]] QuadPoints quadvPos(float left, float right, float top, float bottom)
    {
        return {{
            {right, bottom},
            {right, top},
            {left, top},
            {left, bottom},
        }};
    }

    [[nodiscard]] QuadPoints perspectiveQuadvPos(float left, float right, float top, float bottom)
    {
        return {{
            {right * top, top},
            {right * bottom, bottom},
            {left * bottom, bottom},
            {left * top, top},
        }};
    }

    [[nodiscard]] QuadPoints perspectiveQuadvPos(float leftStart, float leftStop, float rightStart, float rightStop, float top, float bottom)
    {
        return {{
            {rightStart * top, top},
            {rightStop * bottom, bottom},
            {leftStop * bottom, bottom},
            {leftStart * top, top},
        }};
    }

    [[nodiscard]] QuadUvs makeUvRect(float x1, float x2, float y1, float y2)
    {
        return {{
            {x2, y1},
            {x2, y2},
            {x1, y2},
            {x1, y1},
        }};
    }

    [[nodiscard]] std::array<float, 4> mulRowVec(const std::array<float, 64>& transform, size_t offset, const std::array<float, 4>& vector)
    {
        std::array<float, 4> output{};
        for (size_t column = 0; column < 4; ++column) {
            for (size_t row = 0; row < 4; ++row) {
                output[column] += vector[row] * transform[offset + row * 4 + column];
            }
        }
        return output;
    }

    [[nodiscard]] QuadPoints applyTransform(size_t index, const QuadPoints& input)
    {
        const auto& transform = kSpriteTransforms[index];
        const std::array<float, 4> xs{input[0].x, input[1].x, input[2].x, input[3].x};
        const std::array<float, 4> ys{input[0].y, input[1].y, input[2].y, input[3].y};
        const auto txx = mulRowVec(transform, 0, xs);
        const auto txy = mulRowVec(transform, 16, xs);
        const auto tyy = mulRowVec(transform, 48, ys);
        const auto tyx = mulRowVec(transform, 32, ys);

        return {{
            {txx[0] + txy[0], tyx[0] + tyy[0]},
            {txx[1] + txy[1], tyx[1] + tyy[1]},
            {txx[2] + txy[2], tyx[2] + tyy[2]},
            {txx[3] + txy[3], tyx[3] + tyy[3]},
        }};
    }

    [[nodiscard]] QuadPoints scaleQuad(const QuadPoints& input, float scale)
    {
        QuadPoints output = input;
        for (auto& point : output) {
            point.x *= scale;
            point.y *= scale;
        }
        return output;
    }

    [[nodiscard]] QuadPoints translateThenScaleQuad(const QuadPoints& input, float tx, float ty, float scale)
    {
        QuadPoints output = input;
        for (auto& point : output) {
            point.x = (point.x + tx) * scale;
            point.y = (point.y + ty) * scale;
        }
        return output;
    }

    [[nodiscard]] bool isArrayIndexInBounds(ptrdiff_t index, size_t size)
    {
        return index >= 0 && static_cast<size_t>(index) < size;
    }

    void pushQuad(
        const QuadPoints& positions,
        const QuadUvs& uvs,
        TextureId texture,
        float r,
        float g,
        float b,
        float a,
        int zIndex,
        const QuadReciprocalW& reciprocalW = {1.0f, 1.0f, 1.0f, 1.0f})
    {
        gRuntime.renderQuads.push_back(RenderQuad{positions, uvs, r, g, b, a, reciprocalW, static_cast<int>(texture), zIndex});
    }

    mmw::NoteType toMmwNoteType(NoteType type)
    {
        switch (type) {
            case NoteType::Hold:
                return mmw::NoteType::Hold;
            case NoteType::HoldMid:
                return mmw::NoteType::HoldMid;
            case NoteType::HoldEnd:
                return mmw::NoteType::HoldEnd;
            case NoteType::Tap:
            default:
                return mmw::NoteType::Tap;
        }
    }

    mmw::FlickType toMmwFlickType(FlickType type)
    {
        switch (type) {
            case FlickType::Left:
                return mmw::FlickType::Left;
            case FlickType::Right:
                return mmw::FlickType::Right;
            case FlickType::Default:
                return mmw::FlickType::Default;
            case FlickType::None:
            default:
                return mmw::FlickType::None;
        }
    }

    mmw::HoldStepType toMmwHoldStepType(HoldStepType type)
    {
        switch (type) {
            case HoldStepType::Hidden:
                return mmw::HoldStepType::Hidden;
            case HoldStepType::Skip:
                return mmw::HoldStepType::Skip;
            case HoldStepType::Normal:
            default:
                return mmw::HoldStepType::Normal;
        }
    }

    mmw::HoldNoteType toMmwHoldNoteType(HoldNoteType type)
    {
        switch (type) {
            case HoldNoteType::Guide:
                return mmw::HoldNoteType::Guide;
            case HoldNoteType::Hidden:
                return mmw::HoldNoteType::Hidden;
            case HoldNoteType::Normal:
            default:
                return mmw::HoldNoteType::Normal;
        }
    }

    mmw::EaseType toMmwEaseType(EaseType type)
    {
        switch (type) {
            case EaseType::EaseIn:
                return mmw::EaseType::EaseIn;
            case EaseType::EaseOut:
                return mmw::EaseType::EaseOut;
            case EaseType::Linear:
            default:
                return mmw::EaseType::Linear;
        }
    }

    void rebuildEffectScore()
    {
        mmw::Score converted;
        converted.metadata.musicOffset = gRuntime.score.metadata.musicOffset;
        converted.tempoChanges.clear();
        converted.hiSpeedChanges.clear();
        converted.notes.clear();
        converted.holdNotes.clear();

        for (const auto& tempo : gRuntime.score.tempoChanges) {
            converted.tempoChanges.push_back({tempo.tick, tempo.bpm});
        }
        if (converted.tempoChanges.empty()) {
            converted.tempoChanges.push_back({0, 120.0f});
        }

        for (const auto& hiSpeed : gRuntime.score.hiSpeedChanges) {
            converted.hiSpeedChanges.push_back({hiSpeed.tick, hiSpeed.speed});
        }

        for (const auto& [id, note] : gRuntime.score.notes) {
            mmw::Note convertedNote(toMmwNoteType(note.type));
            convertedNote.ID = id;
            convertedNote.parentID = note.parentID;
            convertedNote.tick = note.tick;
            convertedNote.lane = note.lane;
            convertedNote.width = note.width;
            convertedNote.critical = note.critical;
            convertedNote.friction = note.friction;
            convertedNote.flick = toMmwFlickType(note.flick);
            converted.notes[id] = convertedNote;
        }

        for (const auto& [id, hold] : gRuntime.score.holdNotes) {
            mmw::HoldNote convertedHold;
            convertedHold.start = {hold.start.ID, toMmwHoldStepType(hold.start.type), toMmwEaseType(hold.start.ease)};
            convertedHold.end = hold.end;
            convertedHold.startType = toMmwHoldNoteType(hold.startType);
            convertedHold.endType = toMmwHoldNoteType(hold.endType);
            for (const auto& step : hold.steps) {
                convertedHold.steps.push_back({step.ID, toMmwHoldStepType(step.type), toMmwEaseType(step.ease)});
            }
            converted.holdNotes[id] = convertedHold;
        }

        gRuntime.effectContext.score = converted;
        gRuntime.effectContext.currentTick = 0;
        gRuntime.effectView.reset();
        gRuntime.lastEffectTimeSec = -1000.0f;
    }

    void initializeEffects()
    {
        mmw::ResourceManager::loadEmbeddedEffects(gRuntime.config.effectProfile);
        gRuntime.effectView = {};
        gRuntime.effectView.init();
        gRuntime.effectCamera.setFov(50.0f);
        gRuntime.effectCamera.setRotation(-90.0f, 27.1f);
        gRuntime.effectCamera.setPosition(DirectX::XMVectorSet(0.0f, 5.32f, -5.86f, 0.0f));
        gRuntime.effectCamera.positionCamNormal();
        gRuntime.effectRenderer = {};
        gRuntime.effectQuads.clear();
    }

    void appendEffectQuads(bool underNotes)
    {
        const float aspectRatio = gRuntime.height > 0 ? static_cast<float>(gRuntime.width) / static_cast<float>(gRuntime.height) : (16.0f / 9.0f);
        auto projection = gRuntime.effectCamera.getProjectionMatrix(aspectRatio, 0.3f, 1000.0f);
        const float projectionScale = std::min(aspectRatio / EFFECTS_TARGET_ASPECT, 1.0f);
        projection = DirectX::XMMatrixScaling(projectionScale, projectionScale, 1.0f) * projection;
        gRuntime.effectRenderer.setEffectMatrices(gRuntime.effectCamera.getViewMatrix(), projection);
        gRuntime.effectRenderer.setOutput(&gRuntime.effectQuads);
        gRuntime.effectQuads.clear();

        const float currentTime = static_cast<float>(gRuntime.effectContext.getTimeAtCurrentTick());
        if (underNotes) {
            gRuntime.effectView.drawUnderNoteEffects(&gRuntime.effectRenderer, currentTime);
        } else {
            gRuntime.effectView.drawEffects(&gRuntime.effectRenderer, currentTime);
        }

        for (const auto& quad : gRuntime.effectQuads) {
            QuadPoints positions{};
            QuadUvs uvs{};
            QuadReciprocalW reciprocalW{};
            for (size_t i = 0; i < 4; ++i) {
                positions[i] = {quad.positions[i * 2 + 0], quad.positions[i * 2 + 1]};
                uvs[i] = {quad.uvs[i * 2 + 0], quad.uvs[i * 2 + 1]};
                reciprocalW[i] = quad.reciprocalW[i];
            }
            const int zIndex = quad.zIndex <= 5 ? (-1000000 + quad.zIndex) : (std::numeric_limits<int>::max() - 4096 + quad.zIndex);
            gRuntime.renderQuads.push_back(RenderQuad{
                positions,
                uvs,
                quad.color.r,
                quad.color.g,
                quad.color.b,
                quad.color.a,
                reciprocalW,
                quad.textureId,
                zIndex,
            });
        }
    }

    void pushSpriteQuad(const QuadPoints& positions, TextureId texture, const SpriteRect& sprite, float r, float g, float b, float a, int zIndex)
    {
        pushQuad(positions, makeUvRect(sprite.x1, sprite.x2, sprite.y1, sprite.y2), texture, r, g, b, a, zIndex);
    }

    void calculateHitEvents()
    {
        gRuntime.hitEvents.clear();
        gRuntime.packedHitEvents.clear();

        if (gRuntime.score.tempoChanges.empty()) {
            return;
        }

        std::unordered_map<int, HoldStepType> holdStepTypesById;
        holdStepTypesById.reserve(gRuntime.score.notes.size());
        for (const auto& [holdId, hold] : gRuntime.score.holdNotes) {
            (void)holdId;
            for (const auto& step : hold.steps) {
                holdStepTypesById.emplace(step.ID, step.type);
            }
        }

        for (const auto& [id, note] : gRuntime.score.notes) {
            (void)id;
            float kind = 0.0f;
            bool playEvent = true;

            if (note.type == NoteType::Hold) {
                const HoldNote& hold = gRuntime.score.holdNotes.at(note.ID);
                playEvent = hold.startType == HoldNoteType::Normal;
            } else if (note.type == NoteType::HoldEnd) {
                const HoldNote& hold = gRuntime.score.holdNotes.at(note.parentID);
                playEvent = hold.endType == HoldNoteType::Normal;
            }

            if (playEvent && note.type == NoteType::HoldMid) {
                auto stepTypeIt = holdStepTypesById.find(note.ID);
                if (stepTypeIt != holdStepTypesById.end() && stepTypeIt->second == HoldStepType::Hidden) {
                    playEvent = false;
                } else {
                    kind = 4.0f;
                }
            } else if (note.isFlick()) {
                kind = 2.0f;
            } else if (note.friction) {
                kind = 3.0f;
            } else if (note.critical && note.type == NoteType::Tap) {
                kind = 1.0f;
            } else {
                kind = 0.0f;
            }

            if (!playEvent) {
                continue;
            }

            float flags = note.critical ? 1.0f : 0.0f;
            float endTimeSec = -1.0f;
            const float volume = getSEVolumeAtTick(note.tick, gRuntime.score);
            gRuntime.hitEvents.push_back(HitEvent{
                accumulateDuration(note.tick, TICKS_PER_BEAT, gRuntime.score.tempoChanges),
                getNoteCenter(note),
                static_cast<float>(note.width),
                kind,
                flags,
                endTimeSec,
                volume,
            });

            if (note.type == NoteType::Hold) {
                const HoldNote& hold = gRuntime.score.holdNotes.at(note.ID);
                if (!hold.isGuide() && hold.startType == HoldNoteType::Normal) {
                    const Note& endNote = gRuntime.score.notes.at(hold.end);
                    gRuntime.hitEvents.push_back(HitEvent{
                        accumulateDuration(note.tick, TICKS_PER_BEAT, gRuntime.score.tempoChanges),
                        getNoteCenter(note),
                        static_cast<float>(note.width),
                        5.0f,
                        flags,
                        accumulateDuration(endNote.tick, TICKS_PER_BEAT, gRuntime.score.tempoChanges),
                        volume,
                    });
                }
            }
        }

        std::stable_sort(gRuntime.hitEvents.begin(), gRuntime.hitEvents.end(), [](const HitEvent& lhs, const HitEvent& rhs) {
            if (lhs.timeSec == rhs.timeSec) {
                return lhs.center < rhs.center;
            }
            return lhs.timeSec < rhs.timeSec;
        });

        gRuntime.packedHitEvents.reserve(gRuntime.hitEvents.size() * 7);
        for (const auto& event : gRuntime.hitEvents) {
            gRuntime.packedHitEvents.push_back(event.timeSec);
            gRuntime.packedHitEvents.push_back(event.center);
            gRuntime.packedHitEvents.push_back(event.width);
            gRuntime.packedHitEvents.push_back(event.kind);
            gRuntime.packedHitEvents.push_back(event.flags);
            gRuntime.packedHitEvents.push_back(event.endTimeSec);
            gRuntime.packedHitEvents.push_back(event.volume);
        }
    }

    [[nodiscard]] float getHudWeight(HudEventKind kind, bool critical)
    {
        switch (kind) {
            case HudEventKind::Flick:
                return critical ? 3.0f : 1.0f;
            case HudEventKind::Trace:
                return critical ? 0.2f : 0.1f;
            case HudEventKind::Tick:
            case HudEventKind::HoldHalfBeat:
                return critical ? 0.2f : 0.1f;
            case HudEventKind::CriticalTap:
                return 2.0f;
            case HudEventKind::Tap:
            default:
                return critical ? 2.0f : 1.0f;
        }
    }

    [[nodiscard]] std::string comboDedupKey(const Note& note)
    {
        return std::to_string(static_cast<int>(note.type)) + "|" +
            std::to_string(note.tick) + "|" +
            std::to_string(note.lane) + "|" +
            std::to_string(note.width) + "|" +
            std::to_string(note.critical ? 1 : 0) + "|" +
            std::to_string(note.friction ? 1 : 0) + "|" +
            std::to_string(static_cast<int>(note.flick));
    }

    [[nodiscard]] std::string holdHalfBeatDedupKey(int holdId, const HoldNote& hold, const Score& score, int tick)
    {
        const Note& holdStart = score.notes.at(holdId);
        const Note& holdEnd = score.notes.at(hold.end);

        std::string key =
            std::to_string(tick) + "|" +
            std::to_string(holdStart.tick) + "|" +
            std::to_string(holdStart.lane) + "|" +
            std::to_string(holdStart.width) + "|" +
            std::to_string(holdEnd.tick) + "|" +
            std::to_string(holdEnd.lane) + "|" +
            std::to_string(holdEnd.width) + "|" +
            std::to_string(holdStart.critical ? 1 : 0) + "|" +
            std::to_string(holdStart.friction ? 1 : 0);

        for (const auto& step : hold.steps) {
            const Note& stepNote = score.notes.at(step.ID);
            key += "|" +
                std::to_string(stepNote.tick) + ":" +
                std::to_string(stepNote.lane) + ":" +
                std::to_string(stepNote.width) + ":" +
                std::to_string(static_cast<int>(step.type));
        }

        return key;
    }

    void calculateHudEvents()
    {
        gRuntime.hudEvents.clear();
        gRuntime.packedHudEvents.clear();

        if (gRuntime.score.tempoChanges.empty()) {
            return;
        }

        std::unordered_map<int, HoldStepType> holdStepTypesById;
        holdStepTypesById.reserve(gRuntime.score.notes.size());
        for (const auto& [holdId, hold] : gRuntime.score.holdNotes) {
            (void)holdId;
            for (const auto& step : hold.steps) {
                holdStepTypesById.emplace(step.ID, step.type);
            }
        }

        auto pushEvent = [](float timeSec, HudEventKind kind, bool critical, bool halfBeat, bool showJudge) {
            uint8_t flags = 0;
            if (critical) {
                flags = static_cast<uint8_t>(flags | HUD_FLAG_CRITICAL);
            }
            if (halfBeat) {
                flags = static_cast<uint8_t>(flags | HUD_FLAG_HALF_BEAT);
            }
            if (showJudge) {
                flags = static_cast<uint8_t>(flags | HUD_FLAG_SHOW_JUDGE);
            }

            gRuntime.hudEvents.push_back(HudEvent{
                timeSec,
                getHudWeight(kind, critical),
                static_cast<float>(static_cast<int>(kind)),
                static_cast<float>(flags),
            });
        };

        std::unordered_set<std::string> seenComboKeys;
        seenComboKeys.reserve(gRuntime.score.notes.size() * 2);

        for (const auto& [id, note] : gRuntime.score.notes) {
            (void)id;

            const HoldNote* hold = nullptr;
            if (note.type == NoteType::Hold) {
                auto holdIt = gRuntime.score.holdNotes.find(note.ID);
                if (holdIt == gRuntime.score.holdNotes.end()) {
                    continue;
                }
                hold = &holdIt->second;
            } else if (note.type == NoteType::HoldMid || note.type == NoteType::HoldEnd) {
                auto holdIt = gRuntime.score.holdNotes.find(note.parentID);
                if (holdIt == gRuntime.score.holdNotes.end()) {
                    continue;
                }
                hold = &holdIt->second;
            }

            if (hold != nullptr && hold->isGuide()) {
                continue;
            }

            if (note.type == NoteType::Hold && hold != nullptr && hold->startType != HoldNoteType::Normal) {
                continue;
            }
            if (note.type == NoteType::HoldEnd && hold != nullptr && hold->endType != HoldNoteType::Normal) {
                continue;
            }
            if (note.type == NoteType::HoldMid) {
                auto stepTypeIt = holdStepTypesById.find(note.ID);
                if (stepTypeIt != holdStepTypesById.end() && stepTypeIt->second == HoldStepType::Hidden) {
                    continue;
                }
            }

            HudEventKind kind = HudEventKind::Tap;
            if (note.type == NoteType::HoldMid) {
                kind = HudEventKind::Tick;
            } else if (note.isFlick()) {
                kind = HudEventKind::Flick;
            } else if (note.friction) {
                kind = HudEventKind::Trace;
            } else if (note.critical) {
                kind = HudEventKind::CriticalTap;
            }

            if (!seenComboKeys.insert(comboDedupKey(note)).second) {
                continue;
            }

            pushEvent(
                accumulateDuration(note.tick, TICKS_PER_BEAT, gRuntime.score.tempoChanges),
                kind,
                note.critical,
                false,
                true);
        }

        constexpr int halfBeat = TICKS_PER_BEAT / 2;
        for (const auto& [holdId, hold] : gRuntime.score.holdNotes) {
            if (hold.isGuide()) {
                continue;
            }

            const Note& holdStart = gRuntime.score.notes.at(holdId);
            const Note& holdEnd = gRuntime.score.notes.at(hold.end);
            int startTick = holdStart.tick;
            int endTick = holdEnd.tick;
            int eigthTick = startTick;

            eigthTick += halfBeat;
            if (eigthTick % halfBeat) {
                eigthTick -= (eigthTick % halfBeat);
            }

            if (eigthTick == startTick || eigthTick == endTick) {
                continue;
            }

            if (endTick % halfBeat) {
                endTick += halfBeat - (endTick % halfBeat);
            }

            for (int tick = eigthTick; tick < endTick; tick += halfBeat) {
                if (!seenComboKeys.insert(holdHalfBeatDedupKey(holdId, hold, gRuntime.score, tick)).second) {
                    continue;
                }
                pushEvent(
                    accumulateDuration(tick, TICKS_PER_BEAT, gRuntime.score.tempoChanges),
                    HudEventKind::HoldHalfBeat,
                    holdStart.critical,
                    true,
                    true);
            }
        }

        std::stable_sort(gRuntime.hudEvents.begin(), gRuntime.hudEvents.end(), [](const HudEvent& lhs, const HudEvent& rhs) {
            if (lhs.timeSec == rhs.timeSec) {
                return lhs.kind < rhs.kind;
            }
            return lhs.timeSec < rhs.timeSec;
        });

        gRuntime.packedHudEvents.reserve(gRuntime.hudEvents.size() * 4);
        for (const auto& event : gRuntime.hudEvents) {
            gRuntime.packedHudEvents.push_back(event.timeSec);
            gRuntime.packedHudEvents.push_back(event.weight);
            gRuntime.packedHudEvents.push_back(event.kind);
            gRuntime.packedHudEvents.push_back(event.flags);
        }
    }

    [[nodiscard]] std::string noteKey(const SUSNote& note)
    {
        return std::to_string(note.tick) + "-" + std::to_string(note.lane);
    }

    void sortHoldSteps(const Score& score, HoldNote& hold)
    {
        std::stable_sort(hold.steps.begin(), hold.steps.end(), [&score](const HoldStep& lhs, const HoldStep& rhs) {
            const auto& left = score.notes.at(lhs.ID);
            const auto& right = score.notes.at(rhs.ID);
            return left.tick == right.tick ? left.lane < right.lane : left.tick < right.tick;
        });
    }

#include "custom_score_json.h"

    int getFlickArrowSpriteIndex(const Note& note)
    {
        const int startIndex = note.critical ? SPR_FLICK_ARROW_CRITICAL_01 : SPR_FLICK_ARROW_01;
        return startIndex + ((std::min(note.width, 6) - 1) * 2) + (note.flick != FlickType::Default ? 1 : 0);
    }

    int getNoteSpriteIndex(const Note& note)
    {
        if (note.friction) {
            if (note.critical) {
                return SPR_NOTE_FRICTION_CRITICAL;
            }
            return note.flick != FlickType::None ? SPR_NOTE_FRICTION_FLICK : SPR_NOTE_FRICTION;
        }

        if (note.type == NoteType::HoldMid) {
            return note.critical ? SPR_NOTE_LONG_AMONG_CRITICAL : SPR_NOTE_LONG_AMONG;
        }

        if (note.critical) {
            return SPR_NOTE_CRITICAL;
        }
        if (note.isFlick()) {
            return SPR_NOTE_FLICK;
        }
        if (note.type == NoteType::Hold || note.type == NoteType::HoldEnd) {
            return SPR_NOTE_LONG;
        }
        return SPR_NOTE_TAP;
    }

    int getFrictionSpriteIndex(const Note& note)
    {
        if (note.critical) {
            return SPR_NOTE_FRICTION_AMONG_CRITICAL;
        }
        return note.flick != FlickType::None ? SPR_NOTE_FRICTION_AMONG_FLICK : SPR_NOTE_FRICTION_AMONG;
    }

    class SusDataLine
    {
    public:
        explicit SusDataLine(int measureOffset, const std::string& line)
            : measureOffset_(measureOffset)
        {
            const size_t separatorIndex = line.find_first_of(':');
            header = trim(line.substr(1, separatorIndex - 1));
            data = trim(line.substr(separatorIndex + 1));

            const std::string headerMeasure = header.substr(0, 3);
            if (isDigitString(headerMeasure)) {
                measure_ = std::atoi(headerMeasure.c_str());
            }
        }

        [[nodiscard]] int getEffectiveMeasure() const
        {
            return measureOffset_ + measure_;
        }

        std::string header;
        std::string data;

    private:
        int measureOffset_{};
        int measure_{};
    };

    class SusParser
    {
    public:
        [[nodiscard]] SUS parseText(const std::string& text)
        {
            ticksPerBeat_ = 480;
            measureOffset_ = 0;
            waveOffset_ = 0.0f;
            title_.clear();
            artist_.clear();
            designer_.clear();
            bpmDefinitions_.clear();
            bars_.clear();

            SUS sus{};
            std::vector<SusDataLine> noteLines;
            std::vector<SusDataLine> bpmLines;
            std::vector<SusDataLine> hiSpeedLines;
            std::vector<SusDataLine> seVolumeLines;

            std::stringstream stream(text);
            std::string rawLine;
            while (std::getline(stream, rawLine)) {
                const std::string line = trim(rawLine);
                if (!startsWith(line, "#")) {
                    continue;
                }

                if (isCommand(line)) {
                    processCommand(line);
                } else {
                    SusDataLine susLine(measureOffset_, line);
                    const std::string& header = susLine.header;
                    if (header.size() != 5 && header.size() != 6) {
                        continue;
                    }

                    if (endsWith(header, "02") && isDigitString(header)) {
                        sus.barlengths.push_back({susLine.getEffectiveMeasure(), std::strtof(susLine.data.c_str(), nullptr)});
                    } else if (startsWith(header, "BPM")) {
                        bpmDefinitions_[header.substr(3)] = std::strtof(susLine.data.c_str(), nullptr);
                    } else if (endsWith(header, "08")) {
                        bpmLines.push_back(susLine);
                    } else if (startsWith(header, "TIL")) {
                        hiSpeedLines.push_back(susLine);
                    } else if (startsWith(header, "VOLUME")) {
                        seVolumeLines.push_back(susLine);
                    } else {
                        noteLines.push_back(susLine);
                    }
                }
            }

            if (sus.barlengths.empty()) {
                sus.barlengths.push_back({0, 4.0f});
            }

            bars_ = getBars(sus.barlengths);
            sus.bpms = getBpms(bpmLines);
            sus.hiSpeeds = getHiSpeeds(hiSpeedLines);
            sus.seVolumes = getSEVolumes(seVolumeLines);

            std::map<int, std::vector<SUSNote>> slideStreams;
            std::map<int, std::vector<SUSNote>> guideStreams;
            for (const auto& line : noteLines) {
                const std::string& header = line.header;
                if (header.size() == 5 && header[3] == '1') {
                    const auto append = getNotes(line);
                    sus.taps.insert(sus.taps.end(), append.begin(), append.end());
                } else if (header.size() == 5 && header[3] == '5') {
                    const auto append = getNotes(line);
                    sus.directionals.insert(sus.directionals.end(), append.begin(), append.end());
                } else if (header.size() == 6 && header[3] == '3') {
                    const int channel = static_cast<int>(std::strtoul(header.substr(5, 1).c_str(), nullptr, 36));
                    const auto append = getNotes(line);
                    auto& streamRef = slideStreams[channel];
                    streamRef.insert(streamRef.end(), append.begin(), append.end());
                } else if (header.size() == 6 && header[3] == '9') {
                    const int channel = static_cast<int>(std::strtoul(header.substr(5, 1).c_str(), nullptr, 36));
                    const auto append = getNotes(line);
                    auto& streamRef = guideStreams[channel];
                    streamRef.insert(streamRef.end(), append.begin(), append.end());
                }
            }

            for (const auto& [_, stream] : slideStreams) {
                const auto notes = getNoteStream(stream);
                sus.slides.insert(sus.slides.end(), notes.begin(), notes.end());
            }
            for (const auto& [_, stream] : guideStreams) {
                const auto notes = getNoteStream(stream);
                sus.guides.insert(sus.guides.end(), notes.begin(), notes.end());
            }

            sus.metadata.data["title"] = title_;
            sus.metadata.data["artist"] = artist_;
            sus.metadata.data["designer"] = designer_;
            sus.metadata.waveOffset = waveOffset_;
            return sus;
        }

    private:
        [[nodiscard]] bool isCommand(const std::string& line) const
        {
            if (line.size() < 2) {
                return false;
            }
            if (std::isdigit(static_cast<unsigned char>(line[1])) != 0) {
                return false;
            }
            if (line.find('"') != std::string::npos) {
                const auto parts = splitWhitespace(line);
                if (parts.size() < 2) {
                    return false;
                }
                if (parts[0].find(':') != std::string::npos) {
                    return false;
                }
                const size_t firstQuote = line.find('"');
                const size_t lastQuote = line.find_last_of('"');
                return firstQuote != lastQuote && lastQuote != std::string::npos;
            }
            return line.find(':') == std::string::npos;
        }

        [[nodiscard]] int getTicks(int measure, int index, int total) const
        {
            int barIndex = 0;
            int accBarTicks = 0;
            for (size_t i = 0; i < bars_.size(); ++i) {
                if (bars_[i].measure > measure) {
                    break;
                }
                barIndex = static_cast<int>(i);
                accBarTicks += bars_[i].ticks;
            }

            return accBarTicks
                + ((measure - bars_[barIndex].measure) * bars_[barIndex].ticksPerMeasure)
                + ((index * bars_[barIndex].ticksPerMeasure) / total);
        }

        [[nodiscard]] SUSNoteStream getNoteStream(const std::vector<SUSNote>& stream) const
        {
            std::vector<SUSNote> sorted = stream;
            std::stable_sort(sorted.begin(), sorted.end(), [](const SUSNote& left, const SUSNote& right) {
                return left.tick < right.tick;
            });

            SUSNoteStream result;
            std::vector<SUSNote> current;
            bool newSlide = true;
            for (const auto& note : sorted) {
                if (newSlide) {
                    current.clear();
                    newSlide = false;
                }
                current.push_back(note);
                if (note.type == 2) {
                    result.push_back(current);
                    newSlide = true;
                }
            }
            return result;
        }

        [[nodiscard]] std::vector<SUSNote> getNotes(const SusDataLine& line) const
        {
            std::vector<SUSNote> notes;
            const auto cells = getNoteCells(line.data);
            for (size_t i = 0; i < cells.size(); ++i) {
                const std::string& data = cells[i].first;
                if (data.size() < 2 || (data[0] == '0' && data[1] == '0')) {
                    continue;
                }
                notes.push_back(SUSNote{
                    getTicks(line.getEffectiveMeasure(), static_cast<int>(i * 2), static_cast<int>(cells.size() * 2)),
                    static_cast<int>(std::strtoul(line.header.substr(4, 1).c_str(), nullptr, 36)),
                    static_cast<int>(std::strtoul(data.substr(1, 1).c_str(), nullptr, 36)),
                    static_cast<int>(std::strtoul(data.substr(0, 1).c_str(), nullptr, 36)),
                    sanitizeSpeedRatio(cells[i].second),
                });
            }
            return notes;
        }

        [[nodiscard]] std::vector<std::pair<std::string, float>> getNoteCells(const std::string& data) const
        {
            std::vector<std::pair<std::string, float>> cells;
            if (data.find(',') == std::string::npos) {
                for (size_t i = 0; i + 1 < data.size(); i += 2) {
                    cells.push_back({data.substr(i, 2), 1.0f});
                }
                return cells;
            }

            size_t i = 0;
            while (i < data.size()) {
                while (i < data.size() && (std::isspace(static_cast<unsigned char>(data[i])) != 0 || data[i] == ',')) {
                    ++i;
                }
                if (i + 1 >= data.size()) {
                    break;
                }

                const std::string noteData = data.substr(i, 2);
                i += 2;

                float speedRatio = 1.0f;
                if (i < data.size() && data[i] == ',') {
                    ++i;
                    const size_t speedStart = i;
                    while (i < data.size() && std::isspace(static_cast<unsigned char>(data[i])) == 0 && data[i] != ',') {
                        ++i;
                    }
                    if (i > speedStart) {
                        speedRatio = std::strtof(data.substr(speedStart, i - speedStart).c_str(), nullptr);
                    }
                }

                cells.push_back({noteData, sanitizeSpeedRatio(speedRatio)});
            }
            return cells;
        }

        [[nodiscard]] std::vector<BPM> getBpms(const std::vector<SusDataLine>& lines) const
        {
            std::vector<BPM> bpms;
            for (const auto& line : lines) {
                for (size_t i = 0; i + 1 < line.data.size(); i += 2) {
                    if (line.data[i] == '0' && line.data[i + 1] == '0') {
                        continue;
                    }

                    const int tick = getTicks(line.getEffectiveMeasure(), static_cast<int>(i), static_cast<int>(line.data.size()));
                    float bpm = 120.0f;
                    const std::string key = line.data.substr(i, 2);
                    auto it = bpmDefinitions_.find(key);
                    if (it != bpmDefinitions_.end()) {
                        bpm = it->second;
                    }
                    bpms.push_back({tick, bpm});
                }
            }

            std::sort(bpms.begin(), bpms.end(), [](const BPM& left, const BPM& right) {
                return left.tick < right.tick;
            });
            return bpms;
        }

        [[nodiscard]] std::vector<Bar> getBars(const std::vector<BarLength>& lengths) const
        {
            std::vector<Bar> bars;
            bars.reserve(lengths.size());
            bars.push_back({lengths[0].bar, static_cast<int>(lengths[0].length * ticksPerBeat_), 0});
            for (size_t i = 1; i < lengths.size(); ++i) {
                const int measure = lengths[i].bar;
                const int ticksPerMeasure = static_cast<int>(lengths[i].length * ticksPerBeat_);
                const int ticks = static_cast<int>((measure - lengths[i - 1].bar) * lengths[i - 1].length * ticksPerBeat_);
                bars.push_back({measure, ticksPerMeasure, ticks});
            }

            std::sort(bars.begin(), bars.end(), [](const Bar& left, const Bar& right) {
                return left.measure < right.measure;
            });
            return bars;
        }

        [[nodiscard]] std::vector<HiSpeed> getHiSpeeds(const std::vector<SusDataLine>& lines) const
        {
            std::vector<HiSpeed> hiSpeeds;
            for (const auto& line : lines) {
                std::string lineData = line.data;
                const size_t firstQuote = lineData.find('"');
                const size_t lastQuote = lineData.find_last_of('"');
                if (firstQuote == std::string::npos || lastQuote == std::string::npos || lastQuote <= firstQuote) {
                    continue;
                }
                lineData = lineData.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                if (lineData.empty()) {
                    continue;
                }

                for (const auto& change : split(lineData, ',')) {
                    size_t i1 = 0;
                    size_t i2 = change.find('\'', i1);
                    const int measure = std::atoi(change.substr(i1, i2 - i1).c_str());

                    i1 = i2 + 1;
                    i2 = change.find(':', i1);
                    const int tick = std::atoi(change.substr(i1, i2 - i1).c_str());

                    i1 = i2 + 1;
                    const float speed = std::strtof(change.substr(i1).c_str(), nullptr);

                    hiSpeeds.push_back({getTicks(measure, 0, 1) + tick, speed});
                }
            }
            std::sort(hiSpeeds.begin(), hiSpeeds.end(), [](const HiSpeed& left, const HiSpeed& right) {
                return left.tick < right.tick;
            });
            return hiSpeeds;
        }

        [[nodiscard]] std::vector<SEVolume> getSEVolumes(const std::vector<SusDataLine>& lines) const
        {
            std::vector<SEVolume> seVolumes;
            for (const auto& line : lines) {
                std::string lineData = line.data;
                const size_t firstQuote = lineData.find('"');
                const size_t lastQuote = lineData.find_last_of('"');
                if (firstQuote == std::string::npos || lastQuote == std::string::npos || lastQuote <= firstQuote) {
                    continue;
                }
                lineData = lineData.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                if (lineData.empty()) {
                    continue;
                }

                for (const auto& change : split(lineData, ',')) {
                    size_t i1 = 0;
                    size_t i2 = change.find('\'', i1);
                    if (i2 == std::string::npos) {
                        continue;
                    }
                    const int measure = std::atoi(change.substr(i1, i2 - i1).c_str());

                    i1 = i2 + 1;
                    i2 = change.find(':', i1);
                    if (i2 == std::string::npos) {
                        continue;
                    }
                    const int tick = std::atoi(change.substr(i1, i2 - i1).c_str());

                    i1 = i2 + 1;
                    const float volume = sanitizeSEVolume(std::strtof(change.substr(i1).c_str(), nullptr));
                    seVolumes.push_back({getTicks(measure, 0, 1) + tick, volume});
                }
            }
            std::sort(seVolumes.begin(), seVolumes.end(), [](const SEVolume& left, const SEVolume& right) {
                return left.tick < right.tick;
            });
            return seVolumes;
        }

        void processCommand(const std::string& line)
        {
            const size_t keyPos = line.find(' ');
            if (keyPos == std::string::npos) {
                return;
            }

            std::string key = line.substr(1, keyPos - 1);
            std::string value = line.substr(keyPos + 1);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

            if (startsWith(value, "\"") && endsWith(value, "\"")) {
                value = value.substr(1, value.size() - 2);
            }

            if (key == "TITLE") {
                title_ = value;
            } else if (key == "ARTIST") {
                artist_ = value;
            } else if (key == "DESIGNER") {
                designer_ = value;
            } else if (key == "WAVEOFFSET") {
                waveOffset_ = std::strtof(value.c_str(), nullptr);
            } else if (key == "MEASUREBS") {
                measureOffset_ = std::atoi(value.c_str());
            } else if (key == "REQUEST") {
                const auto requestArgs = splitWhitespace(value);
                if (requestArgs.size() == 2 && requestArgs[0] == "ticks_per_beat") {
                    ticksPerBeat_ = std::atoi(requestArgs[1].c_str());
                }
            }
        }

        int ticksPerBeat_{480};
        int measureOffset_{};
        float waveOffset_{};
        std::string title_;
        std::string artist_;
        std::string designer_;
        std::map<std::string, float> bpmDefinitions_;
        std::vector<Bar> bars_;
    };

    Score susToScore(const SUS& sus, float normalizedOffsetMs)
    {
        gNextID = 1;

        const auto getMeta = [&](const char* key) -> std::string {
            auto it = sus.metadata.data.find(key);
            return it == sus.metadata.data.end() ? std::string{} : it->second;
        };

        Score score{};
        score.metadata.title = getMeta("title");
        score.metadata.artist = getMeta("artist");
        score.metadata.author = getMeta("designer");
        score.metadata.musicOffset = normalizedOffsetMs;

        std::unordered_map<std::string, FlickType> flicks;
        std::unordered_set<std::string> criticals;
        std::unordered_set<std::string> stepIgnore;
        std::unordered_set<std::string> easeIns;
        std::unordered_set<std::string> easeOuts;
        std::unordered_set<std::string> slideKeys;
        std::unordered_set<std::string> frictions;
        std::unordered_set<std::string> hiddenHolds;
        std::unordered_map<std::string, float> noteSpeedRatios;

        auto rememberNoteSpeed = [&](const SUSNote& note) {
            const float speedRatio = sanitizeSpeedRatio(note.speedRatio);
            if (std::abs(speedRatio - 1.0f) > 0.0001f) {
                noteSpeedRatios.insert_or_assign(noteKey(note), speedRatio);
            }
        };

        for (const auto& tap : sus.taps) {
            rememberNoteSpeed(tap);
        }
        for (const auto& dir : sus.directionals) {
            rememberNoteSpeed(dir);
        }

        for (const auto& slide : sus.slides) {
            for (const auto& note : slide) {
                rememberNoteSpeed(note);
                if (note.type == 1 || note.type == 2 || note.type == 3 || note.type == 5) {
                    slideKeys.insert(noteKey(note));
                }
            }
        }

        for (const auto& guide : sus.guides) {
            for (const auto& note : guide) {
                rememberNoteSpeed(note);
            }
        }

        auto getNoteSpeed = [&](const SUSNote& note) {
            const auto it = noteSpeedRatios.find(noteKey(note));
            return it == noteSpeedRatios.end() ? 1.0f : it->second;
        };

        for (const auto& dir : sus.directionals) {
            const std::string key = noteKey(dir);
            switch (dir.type) {
                case 1:
                    flicks.insert_or_assign(key, FlickType::Default);
                    break;
                case 3:
                    flicks.insert_or_assign(key, FlickType::Left);
                    break;
                case 4:
                    flicks.insert_or_assign(key, FlickType::Right);
                    break;
                case 2:
                    easeIns.insert(key);
                    break;
                case 5:
                case 6:
                    easeOuts.insert(key);
                    break;
                default:
                    break;
            }
        }

        for (const auto& tap : sus.taps) {
            const std::string key = noteKey(tap);
            switch (tap.type) {
                case 2:
                    criticals.insert(key);
                    break;
                case 3:
                    stepIgnore.insert(key);
                    break;
                case 5:
                    frictions.insert(key);
                    break;
                case 6:
                    criticals.insert(key);
                    frictions.insert(key);
                    break;
                case 7:
                    hiddenHolds.insert(key);
                    break;
                case 8:
                    hiddenHolds.insert(key);
                    criticals.insert(key);
                    break;
                default:
                    break;
            }
        }

        for (const auto& tap : sus.taps) {
            if (tap.type == 7 || tap.type == 8) {
                continue;
            }
            if (tap.lane - 2 < MIN_LANE || tap.lane - 2 > MAX_LANE) {
                continue;
            }
            const std::string key = noteKey(tap);
            if (slideKeys.contains(key)) {
                continue;
            }

            Note note{};
            note.type = NoteType::Tap;
            note.tick = tap.tick;
            note.lane = tap.lane - 2;
            note.width = tap.width;
            note.speedRatio = getNoteSpeed(tap);
            note.critical = criticals.contains(key);
            note.friction = frictions.contains(key);
            auto flickIt = flicks.find(key);
            note.flick = flickIt == flicks.end() ? FlickType::None : flickIt->second;
            note.ID = gNextID++;
            score.notes[note.ID] = note;
        }

        const auto fillSlides = [&](const SUSNoteStream& slides, bool isGuide) {
            for (const auto& slide : slides) {
                if (slide.size() < 2) {
                    continue;
                }

                auto start = std::find_if(slide.begin(), slide.end(), [](const SUSNote& note) {
                    return note.type == 1 || note.type == 2;
                });
                if (start == slide.end()) {
                    continue;
                }

                const std::string criticalKey = noteKey(slide[0]);
                const bool critical = criticals.contains(criticalKey);

                HoldNote hold{};
                const int startID = gNextID++;
                hold.steps.reserve(slide.size() - 2);

                for (const auto& susNote : slide) {
                    const std::string key = noteKey(susNote);
                    EaseType ease = EaseType::Linear;
                    if (easeIns.contains(key)) {
                        ease = EaseType::EaseIn;
                    } else if (easeOuts.contains(key)) {
                        ease = EaseType::EaseOut;
                    }

                    switch (susNote.type) {
                        case 1: {
                            Note note{};
                            note.type = NoteType::Hold;
                            note.tick = susNote.tick;
                            note.lane = susNote.lane - 2;
                            note.width = susNote.width;
                            note.speedRatio = getNoteSpeed(susNote);
                            note.critical = critical;
                            note.ID = startID;
                            if (isGuide) {
                                hold.startType = HoldNoteType::Guide;
                            } else {
                                note.friction = frictions.contains(key);
                                hold.startType = hiddenHolds.contains(key) ? HoldNoteType::Hidden : HoldNoteType::Normal;
                            }
                            score.notes[note.ID] = note;
                            hold.start = HoldStep{note.ID, HoldStepType::Normal, ease};
                            break;
                        }
                        case 2: {
                            Note note{};
                            note.type = NoteType::HoldEnd;
                            note.tick = susNote.tick;
                            note.lane = susNote.lane - 2;
                            note.width = susNote.width;
                            note.speedRatio = getNoteSpeed(susNote);
                            note.critical = critical ? true : criticals.contains(key);
                            note.ID = gNextID++;
                            note.parentID = startID;
                            if (isGuide) {
                                hold.endType = HoldNoteType::Guide;
                            } else {
                                auto flickIt = flicks.find(key);
                                note.flick = flickIt == flicks.end() ? FlickType::None : flickIt->second;
                                note.friction = frictions.contains(key);
                                hold.endType = hiddenHolds.contains(key) ? HoldNoteType::Hidden : HoldNoteType::Normal;
                            }
                            score.notes[note.ID] = note;
                            hold.end = note.ID;
                            break;
                        }
                        case 3:
                        case 5: {
                            Note note{};
                            note.type = NoteType::HoldMid;
                            note.tick = susNote.tick;
                            note.lane = susNote.lane - 2;
                            note.width = susNote.width;
                            note.speedRatio = getNoteSpeed(susNote);
                            note.critical = critical;
                            note.ID = gNextID++;
                            note.parentID = startID;
                            HoldStepType type = susNote.type == 3 ? HoldStepType::Normal : HoldStepType::Hidden;
                            if (stepIgnore.contains(key)) {
                                type = HoldStepType::Skip;
                            }
                            score.notes[note.ID] = note;
                            hold.steps.push_back(HoldStep{note.ID, type, ease});
                            break;
                        }
                        default:
                            break;
                    }
                }

                if (hold.start.ID == 0 || hold.end == 0) {
                    throw std::runtime_error("Invalid hold note");
                }
                sortHoldSteps(score, hold);
                score.holdNotes[startID] = hold;
            }
        };

        fillSlides(sus.slides, false);
        fillSlides(sus.guides, true);

        score.tempoChanges.reserve(sus.bpms.size());
        for (const auto& bpm : sus.bpms) {
            score.tempoChanges.push_back({bpm.tick, bpm.bpm});
        }
        if (score.tempoChanges.empty()) {
            score.tempoChanges.push_back({0, 120.0f});
        }

        score.hiSpeedChanges.reserve(sus.hiSpeeds.size());
        for (const auto& hiSpeed : sus.hiSpeeds) {
            score.hiSpeedChanges.push_back({hiSpeed.tick, hiSpeed.speed});
        }
        std::sort(score.hiSpeedChanges.begin(), score.hiSpeedChanges.end(), [](const HiSpeedChange& lhs, const HiSpeedChange& rhs) {
            return lhs.tick < rhs.tick;
        });

        score.seVolumeChanges.reserve(sus.seVolumes.size());
        for (const auto& seVolume : sus.seVolumes) {
            score.seVolumeChanges.push_back({seVolume.tick, sanitizeSEVolume(seVolume.volume)});
        }
        std::sort(score.seVolumeChanges.begin(), score.seVolumeChanges.end(), [](const SEVolumeChange& lhs, const SEVolumeChange& rhs) {
            return lhs.tick < rhs.tick;
        });

        return score;
    }

    void addHoldNote(DrawData& drawData, const HoldNote& holdNote, const Score& score)
    {
        const Note& startNote = score.notes.at(holdNote.start.ID);
        const Note& endNote = score.notes.at(holdNote.end);
        const float noteDuration = getNoteVisibleDuration(startNote, score, drawData.noteSpeed);
        float activeTime = accumulateDuration(startNote.tick, TICKS_PER_BEAT, score.tempoChanges);
        float startTime = activeTime;
        struct HoldStepDraw
        {
            int ID{};
            int tick{};
            double time{};
            float left{};
            float right{};
            EaseType ease{EaseType::Linear};
        };

        HoldStepDraw head{
            startNote.ID,
            startNote.tick,
            accumulateScaledDuration(startNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges),
            laneToLeft(static_cast<float>(startNote.lane)),
            laneToLeft(static_cast<float>(startNote.lane)) + startNote.width,
            holdNote.start.ease,
        };

        for (ptrdiff_t headIndex = -1, tailIndex = 0, stepCount = static_cast<ptrdiff_t>(holdNote.steps.size()); headIndex < stepCount; ++tailIndex) {
            if (tailIndex < stepCount && holdNote.steps[tailIndex].type == HoldStepType::Skip) {
                continue;
            }

            HoldStep tailStep = tailIndex == stepCount ? HoldStep{holdNote.end, HoldStepType::Hidden, EaseType::Linear} : holdNote.steps[tailIndex];
            const Note& tailNote = score.notes.at(tailStep.ID);
            auto easeFunction = getEaseFunction(head.ease);
            HoldStepDraw tail{
                tailNote.ID,
                tailNote.tick,
                accumulateScaledDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges),
                laneToLeft(static_cast<float>(tailNote.lane)),
                laneToLeft(static_cast<float>(tailNote.lane)) + tailNote.width,
                tailStep.ease,
            };
            const float endTime = accumulateDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges);

            drawData.drawingHoldSegments.push_back(DrawingHoldSegment{
                holdNote.end,
                head.ease,
                holdNote.isGuide(),
                score.notes.at(head.ID).critical,
                tailIndex,
                head.time,
                tail.time,
                head.left,
                head.right,
                tail.left,
                tail.right,
                startTime,
                endTime,
                activeTime,
                noteDuration,
            });
            startTime = endTime;

            while ((headIndex + 1) < tailIndex) {
                const HoldStep& skipStep = holdNote.steps[headIndex + 1];
                if (skipStep.type != HoldStepType::Skip) {
                    break;
                }
                const Note& skipNote = score.notes.at(skipStep.ID);
                if (skipNote.tick > tail.tick) {
                    break;
                }
                const double tickTime = accumulateScaledDuration(skipNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges);
                const double tickProgress = unlerpD(head.time, tail.time, tickTime);
                const float skipLeft = easeFunction(head.left, tail.left, static_cast<float>(tickProgress));
                const float skipRight = easeFunction(head.right, tail.right, static_cast<float>(tickProgress));
                drawData.drawingHoldTicks.push_back(DrawingHoldTick{
                    skipStep.ID,
                    skipLeft + (skipRight - skipLeft) / 2.0f,
                    {tickTime - noteDuration, tickTime},
                    noteDuration,
                });
                ++headIndex;
            }

            if (tailStep.type != HoldStepType::Hidden) {
                const double tickTime = accumulateScaledDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges);
                drawData.drawingHoldTicks.push_back(DrawingHoldTick{
                    tailNote.ID,
                    getNoteCenter(tailNote),
                    {tickTime - noteDuration, tickTime},
                    noteDuration,
                });
            }

            head = tail;
            ++headIndex;
        }
    }

    void calculateDrawData(DrawData& drawData, const Score& score)
    {
        drawData.clear();
        drawData.noteSpeed = gRuntime.config.noteSpeed;

        std::map<int, Range> simultaneousBuilder;
        std::map<int, float> simultaneousDurations;
        std::map<int, float> simultaneousSpeedRatios;
        std::unordered_set<int> speedMismatchedSimultaneousTicks;
        for (auto it = score.notes.rbegin(); it != score.notes.rend(); ++it) {
            const Note& note = it->second;
            drawData.maxTicks = std::max(drawData.maxTicks, note.tick);
            if (note.type == NoteType::HoldMid) {
                continue;
            }
            if (note.type == NoteType::Hold && score.holdNotes.at(note.ID).startType != HoldNoteType::Normal) {
                continue;
            }
            if (note.type == NoteType::HoldEnd && score.holdNotes.at(note.parentID).endType != HoldNoteType::Normal) {
                continue;
            }

            drawData.drawingNotes.push_back({note.ID, getNoteVisualTime(note, score, drawData.noteSpeed)});

            const float center = getNoteCenter(note);
            const float speedRatio = getEffectiveSpeedRatio(note, score);
            const float visibleDuration = getNoteVisibleDuration(note, score, drawData.noteSpeed);
            auto [rangeIt, inserted] = simultaneousBuilder.try_emplace(note.tick, Range{center, center});
            auto [durationIt, durationInserted] = simultaneousDurations.try_emplace(note.tick, visibleDuration);
            if (!durationInserted) {
                durationIt->second = std::max(durationIt->second, visibleDuration);
            }
            auto [speedIt, speedInserted] = simultaneousSpeedRatios.try_emplace(note.tick, speedRatio);
            if (!speedInserted && std::abs(speedIt->second - speedRatio) > 0.0001f) {
                speedMismatchedSimultaneousTicks.insert(note.tick);
            }
            if (!inserted) {
                rangeIt->second.min = std::min(rangeIt->second.min, static_cast<double>(center));
                rangeIt->second.max = std::max(rangeIt->second.max, static_cast<double>(center));
            }
        }

        for (const auto& [tick, range] : simultaneousBuilder) {
            if (range.min == range.max) {
                continue;
            }
            if (speedMismatchedSimultaneousTicks.contains(tick)) {
                continue;
            }
            const double targetTime = accumulateScaledDuration(tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges);
            const float visibleDuration = simultaneousDurations.at(tick);
            drawData.drawingLines.push_back({range, {targetTime - visibleDuration, targetTime}, visibleDuration});
        }

        for (auto it = score.holdNotes.rbegin(); it != score.holdNotes.rend(); ++it) {
            addHoldNote(drawData, it->second, score);
        }
    }

    void drawNoteBase(const Note& note, float noteLeft, float noteRight, float y, float zScalar = 1.0f)
    {
        const auto& sprite = kNoteSprites[getNoteSpriteIndex(note)];
        const float noteHeight = getNoteHeight();
        const float noteTop = 1.0f - noteHeight;
        const float noteBottom = 1.0f + noteHeight;
        if (gRuntime.config.mirror) {
            std::swap(noteLeft *= -1.0f, noteRight *= -1.0f);
        }

        const int zIndex = getZIndex(!note.friction ? SpriteLayer::BASE_NOTE : SpriteLayer::TICK_NOTE, noteLeft + (noteRight - noteLeft) / 2.0f, y * zScalar);

        auto middle = scaleQuad(applyTransform(TransformNoteMiddle, perspectiveQuadvPos(noteLeft + 0.25f, noteRight - 0.3f, noteTop, noteBottom)), y);
        pushQuad(middle, makeUvRect(sprite.x1 + NOTE_SIDE_WIDTH, sprite.x2 - NOTE_SIDE_WIDTH, sprite.y1, sprite.y2), TextureId::Notes, 1.0f, 1.0f, 1.0f, 1.0f, zIndex);

        auto left = scaleQuad(applyTransform(TransformNoteLeft, perspectiveQuadvPos(noteLeft, noteLeft + 0.25f, noteTop, noteBottom)), y);
        pushQuad(left, makeUvRect(sprite.x1 + NOTE_SIDE_PAD, sprite.x1 + NOTE_SIDE_WIDTH, sprite.y1, sprite.y2), TextureId::Notes, 1.0f, 1.0f, 1.0f, 1.0f, zIndex);

        auto right = scaleQuad(applyTransform(TransformNoteRight, perspectiveQuadvPos(noteRight - 0.3f, noteRight, noteTop, noteBottom)), y);
        pushQuad(right, makeUvRect(sprite.x2 - NOTE_SIDE_WIDTH, sprite.x2 - NOTE_SIDE_PAD, sprite.y1, sprite.y2), TextureId::Notes, 1.0f, 1.0f, 1.0f, 1.0f, zIndex);
    }

    void drawTraceDiamond(const Note& note, float noteLeft, float noteRight, float y)
    {
        const auto& sprite = kNoteSprites[getFrictionSpriteIndex(note)];
        const float w = getNoteHeight() / SCALED_ASPECT_RATIO;
        const float noteTop = 1.0f + getNoteHeight();
        const float noteBottom = 1.0f - getNoteHeight();
        if (gRuntime.config.mirror) {
            std::swap(noteLeft *= -1.0f, noteRight *= -1.0f);
        }
        const float center = noteLeft + (noteRight - noteLeft) / 2.0f;
        const int zIndex = getZIndex(SpriteLayer::DIAMOND, center, y);
        const auto quad = scaleQuad(applyTransform(TransformTraceDiamond, quadvPos(center - w, center + w, noteTop, noteBottom)), y);
        pushSpriteQuad(quad, TextureId::Notes, sprite, 1.0f, 1.0f, 1.0f, 1.0f, zIndex);
    }

    void drawFlickArrow(const Note& note, float y, double time)
    {
        const auto& sprite = kNoteSprites[getFlickArrowSpriteIndex(note)];
        const size_t transformIndex = static_cast<size_t>(std::clamp(note.width, 1, MAX_FLICK_SPRITES) - 1)
            + static_cast<size_t>((note.flick == FlickType::Left || note.flick == FlickType::Right) ? TransformFlickArrowLeft1 : TransformFlickArrowUp1);

        const int mirror = gRuntime.config.mirror ? -1 : 1;
        const int direction = mirror * (note.flick == FlickType::Left ? -1 : (note.flick == FlickType::Right ? 1 : 0));
        const float center = getNoteCenter(note) * mirror;
        const float w = std::clamp(note.width, 0, MAX_FLICK_SPRITES) * (note.flick == FlickType::Right ? -1.0f : 1.0f) * mirror / 4.0f;

        const auto baseQuad = applyTransform(transformIndex, quadvPos(center - w, center + w, 1.0f, 1.0f - 2.0f * std::abs(w) * SCALED_ASPECT_RATIO));
        const int zIndex = getZIndex(SpriteLayer::FLICK_ARROW, center, y);

        if (gRuntime.config.flickAnimation) {
            const double t = std::fmod(time, 0.5) / 0.5;
            const auto animated = translateThenScaleQuad(baseQuad, static_cast<float>(direction * t), static_cast<float>(-2.0 * SCALED_ASPECT_RATIO * t), y);
            pushSpriteQuad(animated, TextureId::Notes, sprite, 1.0f, 1.0f, 1.0f, 1.0f - cubicEaseIn(static_cast<float>(t)), zIndex);
        } else {
            pushSpriteQuad(scaleQuad(baseQuad, y), TextureId::Notes, sprite, 1.0f, 1.0f, 1.0f, 1.0f, zIndex);
        }
    }

    void drawLines(double currentScaledTime)
    {
        if (!gRuntime.config.simultaneousLine) {
            return;
        }

        const float noteTop = 1.0f + getNoteHeight();
        const float noteBottom = 1.0f - getNoteHeight();
        const auto& sprite = kNoteSprites[SPR_SIMULTANEOUS_CONNECTION];

        for (const auto& line : gRuntime.drawData.drawingLines) {
            if (currentScaledTime < line.visualTime.min || currentScaledTime > line.visualTime.max) {
                continue;
            }
            float left = static_cast<float>(line.xPos.min);
            float right = static_cast<float>(line.xPos.max);
            if (gRuntime.config.mirror) {
                std::swap(left *= -1.0f, right *= -1.0f);
            }
            const float y = static_cast<float>(approach(line.visualTime.min, line.visualTime.max, currentScaledTime));
            const auto quad = scaleQuad(applyTransform(TransformSimultaneousLine, perspectiveQuadvPos(left, right, noteTop, noteBottom)), y);
            pushSpriteQuad(quad, TextureId::Notes, sprite, 1.0f, 1.0f, 1.0f, 1.0f, getZIndex(SpriteLayer::UNDER_NOTE_EFFECT, 0.0f, y));
        }
    }

    void drawHoldTicks(double currentScaledTime)
    {
        const float notesHeight = getNoteHeight() * 1.3f;
        const float w = notesHeight / SCALED_ASPECT_RATIO;
        const float noteTop = 1.0f + notesHeight;
        const float noteBottom = 1.0f - notesHeight;

        for (const auto& tick : gRuntime.drawData.drawingHoldTicks) {
            if (currentScaledTime < tick.visualTime.min || currentScaledTime > tick.visualTime.max) {
                continue;
            }
            const auto& note = gRuntime.score.notes.at(tick.refID);
            const auto& sprite = kNoteSprites[getNoteSpriteIndex(note)];
            const float y = static_cast<float>(approach(tick.visualTime.min, tick.visualTime.max, currentScaledTime));
            const float center = tick.center * (gRuntime.config.mirror ? -1.0f : 1.0f);
            const auto quad = scaleQuad(applyTransform(TransformHoldTick, quadvPos(center - w, center + w, noteTop, noteBottom)), y);
            pushSpriteQuad(quad, TextureId::Notes, sprite, 1.0f, 1.0f, 1.0f, 1.0f, getZIndex(SpriteLayer::DIAMOND, center, y));
        }
    }

    void drawNotes(double currentTime, double currentScaledTime)
    {
        for (const auto& drawing : gRuntime.drawData.drawingNotes) {
            if (currentScaledTime < drawing.visualTime.min || currentScaledTime > drawing.visualTime.max) {
                continue;
            }

            const auto& note = gRuntime.score.notes.at(drawing.refID);
            const float y = static_cast<float>(approach(drawing.visualTime.min, drawing.visualTime.max, currentScaledTime));
            const float left = laneToLeft(static_cast<float>(note.lane));
            const float right = left + note.width;
            drawNoteBase(note, left, right, y);
            if (note.friction) {
                drawTraceDiamond(note, left, right, y);
            }
            if (note.isFlick()) {
                drawFlickArrow(note, y, currentTime);
            }
        }
    }

    void drawHoldCurves(double currentTime, double currentScaledTime)
    {
        const float totalTime = std::max(accumulateDuration(gRuntime.drawData.maxTicks, TICKS_PER_BEAT, gRuntime.score.tempoChanges), 0.0001f);
        const float mirror = gRuntime.config.mirror ? -1.0f : 1.0f;

        for (const auto& segment : gRuntime.drawData.drawingHoldSegments) {
            const double visibleScaledTime = currentScaledTime + segment.visibleDuration;
            if ((std::min(segment.headTime, segment.tailTime) > visibleScaledTime && segment.startTime > currentTime) || currentTime >= segment.endTime) {
                continue;
            }

            const Note& holdEnd = gRuntime.score.notes.at(segment.endID);
            const Note& holdStart = gRuntime.score.notes.at(holdEnd.parentID);
            const float holdStartCenter = getNoteCenter(holdStart) * mirror;
            const bool holdActivated = currentTime >= segment.activeTime;
            const bool segmentActivated = currentTime >= segment.startTime;

            const bool critical = segment.critical;
            const TextureId texture = segment.isGuide ? TextureId::TouchLine : TextureId::LongNoteLine;
            const auto& atlas = segment.isGuide ? kTouchLineSprites : kLongNoteSprites;
            const int spriteIndex = critical ? 3 : 1;
            const auto& sprite = atlas[spriteIndex];

            const double segmentHeadScaled = std::min(segment.headTime, segment.tailTime);
            const double segmentTailScaled = std::max(segment.headTime, segment.tailTime);
            const double segmentStartScaled = std::max(segmentHeadScaled, currentScaledTime);
            const double segmentEndScaled = std::min(segmentTailScaled, visibleScaledTime);
            double segmentStartProgress{};
            double segmentEndProgress{};
            double holdStartProgress{};
            double holdEndProgress{};

            if (!segmentActivated) {
                segmentStartProgress = 0.0;
                segmentEndProgress = unlerpD(segmentHeadScaled, segmentTailScaled, segmentEndScaled);
            } else {
                segmentStartProgress = unlerpD(segment.startTime, segment.endTime, currentTime);
                segmentEndProgress = lerpD(segmentStartProgress, 1.0, unlerpD(currentScaledTime, segmentTailScaled, segmentEndScaled));
            }

            int steps = (segment.ease == EaseType::Linear ? 10 : 15)
                + static_cast<int>(std::log(std::max((segmentEndScaled - segmentStartScaled) / segment.visibleDuration, 4.5399e-5)) + 0.5);
            steps = std::max(steps, 1);
            const auto ease = getEaseFunction(segment.ease);
            float startLeft = segment.headLeft;
            float startRight = segment.headRight;
            float endLeft = segment.tailLeft;
            float endRight = segment.tailRight;

            if (segmentActivated && gRuntime.score.holdNotes.at(holdStart.ID).startType == HoldNoteType::Normal) {
                const float l = ease(startLeft, endLeft, static_cast<float>(segmentStartProgress));
                const float r = ease(startRight, endRight, static_cast<float>(segmentStartProgress));
                drawNoteBase(holdStart, l, r, 1.0f, static_cast<float>(segment.activeTime / totalTime));
                if (holdStart.friction) {
                    drawTraceDiamond(holdStart, l, r, 1.0f);
                }
            }

            if (gRuntime.config.mirror) {
                std::swap(startLeft *= -1.0f, startRight *= -1.0f);
                std::swap(endLeft *= -1.0f, endRight *= -1.0f);
            }

            if (segment.isGuide) {
                const HoldNote& hold = gRuntime.score.holdNotes.at(holdStart.ID);
                const double totalJoints = 1.0 + hold.steps.size();
                const double headProgress = segment.tailStepIndex / totalJoints;
                const double tailProgress = (segment.tailStepIndex + 1) / totalJoints;

                if (!segmentActivated) {
                    holdStartProgress = headProgress;
                    holdEndProgress = lerpD(headProgress, tailProgress, unlerpD(segmentHeadScaled, segmentTailScaled, segmentEndScaled));
                } else {
                    holdStartProgress = lerpD(headProgress, tailProgress, unlerp(segment.startTime, segment.endTime, static_cast<float>(currentTime)));
                    holdEndProgress = lerpD(holdStartProgress, tailProgress, unlerpD(currentScaledTime, segment.tailTime, segmentEndScaled));
                }
            }

            double fromPercentage = 0.0;
            double stepStartScaled = segmentStartScaled;
            double stepTop = approach(stepStartScaled - segment.visibleDuration, stepStartScaled, currentScaledTime);
            double stepStartProgress = segmentStartProgress;
            const float alpha = segment.isGuide ? gRuntime.config.guideAlpha : gRuntime.config.holdAlpha;
            const int zIndex = getZIndex(segment.isGuide ? SpriteLayer::GUIDE_PATH : SpriteLayer::HOLD_PATH, holdStartCenter, static_cast<float>(segment.activeTime / totalTime));
            for (int i = 0; i < steps; ++i) {
                const double toPercentage = static_cast<double>(i + 1) / steps;
                const double stepEndScaled = lerpD(segmentStartScaled, segmentEndScaled, toPercentage);
                const double stepBottom = approach(stepEndScaled - segment.visibleDuration, stepEndScaled, currentScaledTime);
                const double stepEndProgress = lerpD(segmentStartProgress, segmentEndProgress, toPercentage);

                const float stepStartLeft = ease(startLeft, endLeft, static_cast<float>(stepStartProgress));
                const float stepEndLeft = ease(startLeft, endLeft, static_cast<float>(stepEndProgress));
                const float stepStartRight = ease(startRight, endRight, static_cast<float>(stepStartProgress));
                const float stepEndRight = ease(startRight, endRight, static_cast<float>(stepEndProgress));

                const auto positions = perspectiveQuadvPos(stepStartLeft, stepEndLeft, stepStartRight, stepEndRight, static_cast<float>(stepTop), static_cast<float>(stepBottom));

                float x1 = sprite.x1;
                float x2 = sprite.x2;
                float y1 = sprite.y1;
                float y2 = sprite.y2;
                if (segment.isGuide) {
                    x1 += GUIDE_XCUTOFF;
                    x2 -= GUIDE_XCUTOFF;
                    y1 = lerp(sprite.y2 - GUIDE_Y_BOTTOM_CUTOFF, sprite.y1 + GUIDE_Y_TOP_CUTOFF, static_cast<float>(lerpD(holdStartProgress, holdEndProgress, fromPercentage)));
                    y2 = lerp(sprite.y2 - GUIDE_Y_BOTTOM_CUTOFF, sprite.y1 + GUIDE_Y_TOP_CUTOFF, static_cast<float>(lerpD(holdStartProgress, holdEndProgress, toPercentage)));
                } else {
                    x1 += HOLD_XCUTOFF;
                    x2 -= HOLD_XCUTOFF;
                }

                const auto uvs = makeUvRect(x1, x2, y1, y2);
                if (gRuntime.config.holdAnimation && holdActivated && isArrayIndexInBounds(spriteIndex - 1, atlas.size())) {
                    const auto& activeSprite = atlas[spriteIndex - 1];
                    const float normalAlpha = static_cast<float>((std::cos((currentTime - segment.activeTime) * NUM_PI * 2.0) + 2.0) / 3.0);
                    pushQuad(positions, uvs, texture, 1.0f, 1.0f, 1.0f, alpha * normalAlpha, zIndex);
                    pushQuad(positions, makeUvRect(x1, x2, y1 + (activeSprite.y1 - sprite.y1), y2 + (activeSprite.y1 - sprite.y1)), texture, 1.0f, 1.0f, 1.0f, alpha * (1.0f - normalAlpha), zIndex);
                } else {
                    pushQuad(positions, uvs, texture, 1.0f, 1.0f, 1.0f, alpha, zIndex);
                }

                fromPercentage = toPercentage;
                stepStartScaled = stepEndScaled;
                stepTop = stepBottom;
                stepStartProgress = stepEndProgress;
            }
        }
    }

    void packQuads()
    {
        std::stable_sort(gRuntime.renderQuads.begin(), gRuntime.renderQuads.end(), [](const RenderQuad& lhs, const RenderQuad& rhs) {
            return lhs.zIndex < rhs.zIndex;
        });

        gRuntime.packedQuads.clear();
        gRuntime.packedQuads.reserve(gRuntime.renderQuads.size() * 25);
        for (const auto& quad : gRuntime.renderQuads) {
            for (size_t i = 0; i < quad.positions.size(); ++i) {
                const auto& position = quad.positions[i];
                gRuntime.packedQuads.push_back(position.x);
                gRuntime.packedQuads.push_back(position.y);
                gRuntime.packedQuads.push_back(quad.reciprocalW[i]);
            }
            for (const auto& uv : quad.uvs) {
                gRuntime.packedQuads.push_back(uv.x);
                gRuntime.packedQuads.push_back(uv.y);
            }
            gRuntime.packedQuads.push_back(quad.r);
            gRuntime.packedQuads.push_back(quad.g);
            gRuntime.packedQuads.push_back(quad.b);
            gRuntime.packedQuads.push_back(quad.a);
            gRuntime.packedQuads.push_back(static_cast<float>(quad.texture));
        }
    }

    void finishLoadedScore()
    {
        calculateDrawData(gRuntime.drawData, gRuntime.score);
        calculateHitEvents();
        calculateHudEvents();
        initializeEffects();
        rebuildEffectScore();
        gRuntime.lastError.clear();
        gRuntime.loaded = true;
    }

    void clearLoadedScoreAfterError(const std::exception& exception)
    {
        gRuntime.lastError = exception.what();
        gRuntime.loaded = false;
        gRuntime.score = {};
        gRuntime.drawData.clear();
        gRuntime.renderQuads.clear();
        gRuntime.packedQuads.clear();
        gRuntime.hitEvents.clear();
        gRuntime.packedHitEvents.clear();
        gRuntime.hudEvents.clear();
        gRuntime.packedHudEvents.clear();
    }
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int loadSusTextPrecise(const char* susText, double normalizedOffsetMs);
    EMSCRIPTEN_KEEPALIVE int loadCustomScoreJsonTextPrecise(const char* jsonText, double normalizedOffsetMs);

    EMSCRIPTEN_KEEPALIVE int init(int)
    {
        mmw_preview::gRuntime = {};
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE void resize(int width, int height, float dpr)
    {
        mmw_preview::gRuntime.width = width;
        mmw_preview::gRuntime.height = height;
        mmw_preview::gRuntime.dpr = dpr;
    }

    EMSCRIPTEN_KEEPALIVE int loadSusText(const char* susText, int normalizedOffsetMs)
    {
        return loadSusTextPrecise(susText, static_cast<double>(normalizedOffsetMs));
    }

    EMSCRIPTEN_KEEPALIVE int loadSusTextPrecise(const char* susText, double normalizedOffsetMs)
    {
        using namespace mmw_preview;

        try {
            if (susText == nullptr) {
                throw std::runtime_error("Missing SUS text");
            }
            const std::string text(susText);
            const SUS sus = SusParser().parseText(text);
            gRuntime.score = susToScore(sus, static_cast<float>(normalizedOffsetMs));
            finishLoadedScore();
            return 1;
        } catch (const std::exception& exception) {
            clearLoadedScoreAfterError(exception);
            return 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE int loadCustomScoreJsonTextPrecise(const char* jsonText, double normalizedOffsetMs)
    {
        using namespace mmw_preview;

        try {
            if (jsonText == nullptr) {
                throw std::runtime_error("Missing custom score JSON text");
            }
            gRuntime.score = custom_score_json::parse(std::string(jsonText), static_cast<float>(normalizedOffsetMs));
            finishLoadedScore();
            return 1;
        } catch (const std::exception& exception) {
            clearLoadedScoreAfterError(exception);
            return 0;
        }
    }

    EMSCRIPTEN_KEEPALIVE void setPreviewConfig(
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
        float backgroundBrightness)
    {
        using namespace mmw_preview;

        const bool noteSpeedChanged = std::abs(gRuntime.config.noteSpeed - noteSpeed) > 0.0001f;
        const int resolvedEffectProfile = effectProfile == 1 ? 1 : 0;
        const int resolvedNoteSkin = noteSkin == 1 ? 1 : 0;
        const bool effectProfileChanged = gRuntime.config.effectProfile != resolvedEffectProfile;
        gRuntime.config.mirror = mirror != 0;
        gRuntime.config.flickAnimation = flickAnimation != 0;
        gRuntime.config.holdAnimation = holdAnimation != 0;
        gRuntime.config.simultaneousLine = simultaneousLine != 0;
        gRuntime.config.effectProfile = resolvedEffectProfile;
        gRuntime.config.noteSkin = resolvedNoteSkin;
        gRuntime.config.noteSpeed = noteSpeed;
        gRuntime.config.holdAlpha = holdAlpha;
        gRuntime.config.guideAlpha = guideAlpha;
        gRuntime.config.stageCover = std::clamp(stageCover, 0.0f, 1.0f);
        gRuntime.config.stageOpacity = stageOpacity;
        gRuntime.config.backgroundBrightness = backgroundBrightness;
        mmw::config.pvMirrorScore = gRuntime.config.mirror;

        if (effectProfileChanged && gRuntime.loaded) {
            initializeEffects();
            rebuildEffectScore();
        }
        if (noteSpeedChanged && gRuntime.loaded) {
            calculateDrawData(gRuntime.drawData, gRuntime.score);
        }
    }

    EMSCRIPTEN_KEEPALIVE int render(float chartTimeSec)
    {
        using namespace mmw_preview;

        if (!gRuntime.loaded) {
            gRuntime.renderQuads.clear();
            gRuntime.packedQuads.clear();
            return 0;
        }

        const int currentTick = accumulateTicks(chartTimeSec, TICKS_PER_BEAT, gRuntime.score.tempoChanges);
        const double currentTime = accumulateDuration(currentTick, TICKS_PER_BEAT, gRuntime.score.tempoChanges);
        const double currentScaledTime = accumulateScaledDuration(currentTick, TICKS_PER_BEAT, gRuntime.score.tempoChanges, gRuntime.score.hiSpeedChanges);

        if (chartTimeSec + 0.05f < gRuntime.lastEffectTimeSec) {
            gRuntime.effectView.reset();
        }
        gRuntime.lastEffectTimeSec = chartTimeSec;
        gRuntime.effectContext.currentTick = currentTick;
        gRuntime.effectView.update(gRuntime.effectContext);
        gRuntime.effectView.updateEffects(gRuntime.effectContext, gRuntime.effectCamera, static_cast<float>(currentTime));

        gRuntime.renderQuads.clear();
        drawLines(currentScaledTime);
        drawHoldCurves(currentTime, currentScaledTime);
        appendEffectQuads(true);
        drawHoldTicks(currentScaledTime);
        drawNotes(currentTime, currentScaledTime);
        appendEffectQuads(false);
        packQuads();
        return static_cast<int>(gRuntime.renderQuads.size());
    }

    EMSCRIPTEN_KEEPALIVE const char* getLastError()
    {
        return mmw_preview::gRuntime.lastError.c_str();
    }

    EMSCRIPTEN_KEEPALIVE const float* getQuadBufferPointer()
    {
        return mmw_preview::gRuntime.packedQuads.empty() ? nullptr : mmw_preview::gRuntime.packedQuads.data();
    }

    EMSCRIPTEN_KEEPALIVE int getQuadCount()
    {
        return static_cast<int>(mmw_preview::gRuntime.renderQuads.size());
    }

    EMSCRIPTEN_KEEPALIVE double getChartEndTimeSec()
    {
        using namespace mmw_preview;
        if (!gRuntime.loaded) {
            return 0.0;
        }
        return accumulateDuration(gRuntime.drawData.maxTicks, TICKS_PER_BEAT, gRuntime.score.tempoChanges);
    }

    EMSCRIPTEN_KEEPALIVE const float* getHitEventBufferPointer()
    {
        return mmw_preview::gRuntime.packedHitEvents.empty() ? nullptr : mmw_preview::gRuntime.packedHitEvents.data();
    }

    EMSCRIPTEN_KEEPALIVE int getHitEventCount()
    {
        return static_cast<int>(mmw_preview::gRuntime.hitEvents.size());
    }

    EMSCRIPTEN_KEEPALIVE const char* getMetadataTitle()
    {
        return mmw_preview::gRuntime.score.metadata.title.c_str();
    }

    EMSCRIPTEN_KEEPALIVE const char* getMetadataArtist()
    {
        return mmw_preview::gRuntime.score.metadata.artist.c_str();
    }

    EMSCRIPTEN_KEEPALIVE const char* getMetadataDesigner()
    {
        return mmw_preview::gRuntime.score.metadata.author.c_str();
    }

    EMSCRIPTEN_KEEPALIVE const float* getHudEventBufferPointer()
    {
        return mmw_preview::gRuntime.packedHudEvents.empty() ? nullptr : mmw_preview::gRuntime.packedHudEvents.data();
    }

    EMSCRIPTEN_KEEPALIVE int getHudEventCount()
    {
        return static_cast<int>(mmw_preview::gRuntime.hudEvents.size());
    }

    EMSCRIPTEN_KEEPALIVE void dispose()
    {
        mmw_preview::gRuntime = {};
    }
}
