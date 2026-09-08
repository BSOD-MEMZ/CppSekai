// CppSekai - thin C++ wrapper over the chart core's extern "C" API
// (mmw_preview.cpp). The core is compiled unmodified from
// sekai-mmw-preview-web (AGPL-3.0).
#pragma once

#include <string>

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
