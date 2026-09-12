// CppSekai - thin C++ wrapper over the chart core's extern "C" API
// (mmw_preview.cpp). The core is compiled unmodified from
// sekai-mmw-preview-web (AGPL-3.0).
#pragma once

#include <string>
#include <vector>

namespace core_api
{

// Initializes / disposes the chart core global state.
void init();
void dispose();

// Viewport size in CSS pixels (world->clip mapping inside the renderer uses
// its own copy; this drives the core's draw-data generation).
void resize(int width, int height, float dpr);

// Parses and loads a SUS chart. normalizedOffsetMs is informational
// (metadata only); the actual audio offset is applied by the audio engine.
bool loadSusTextPrecise(const char* susText, double normalizedOffsetMs);
bool loadSusText(const std::string& susText, int normalizedOffsetMs);
const char* getLastError();

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

// Generates draw data for the given chart time; returns quad count.
int render(float chartTimeSec);
const float* getQuadBuffer();
int getQuadCount();

// Preview mode fires note-hit effects from the chart timeline (autoplay).
// Player mode turns that off and calls triggerNoteEffect() per judged hit.
void setEffectAutoplay(bool enabled);

// CppSekai addition: long notes the player let go of early are drawn
// translucent until the lane is held again (pjsk behaviour). Republish the
// whole list every frame; an empty list clears it. Keys are flat
// (lane center, hold start time seconds) pairs - the same values the kind 5
// HitEvent reports for a normal hold.
void setDimmedHolds(const std::vector<float>& keys);

// CppSekai addition: the player hit the note behind this HitEvent index
// (same stream as getHitEventBuffer()). The core stops drawing that note
// immediately; notes never reported as hit keep falling past the judgement
// line until off screen (pjsk behaviour). clearHitNotes() on new chart/retry.
void markNoteHit(int hitEventIndex);
void clearHitNotes();

// CppSekai addition: holds whose start note was never hit, same flat
// (lane center, hold start time seconds) pair layout as setDimmedHolds().
// Their bodies keep scrolling past the line instead of parking on it.
void setMissedHolds(const std::vector<float>& keys);

// Plays the core's note-hit effect (the game's own particle system) for the
// note at this lane position: center / width are lane coordinates as reported
// by the HitEvent stream, noteTimeSec is that event's time, kind is the
// HitEvent kind, flickDir is 0 none / 1 up / 2 left / 3 right.
void triggerNoteEffect(float center, float width, float noteTimeSec, int kind, bool critical, int flickDir,
    bool friction);

// Packed HitEvents (7 floats per event) for the judgement engine.
const float* getHitEventBuffer();
int getHitEventCount();

const char* getMetadataTitle();
const char* getMetadataArtist();
const char* getMetadataDesigner();

// Total length of the loaded chart in seconds (0 when nothing is loaded).
double getChartEndTimeSec();

// Extracts #WAVEOFFSET (seconds) from raw SUS text. Returns 0 if absent.
double readWaveOffset(const std::string& susText);

} // namespace core_api
