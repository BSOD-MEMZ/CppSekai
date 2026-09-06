// CppSekai - implementation of the core_api wrapper (see core_api.hpp).
// The chart core (mmw_preview.cpp) exposes a C API; this file re-exports it
// as C++ and adds the #WAVEOFFSET text scan.
#include "core_api.hpp"

extern "C"
{
    int init(int);
    void dispose(void);
    void resize(int, int, float);
    int loadSusText(const char*, int);
    int loadSusTextPrecise(const char*, double);
    const char* getLastError(void);
    void setPreviewConfig(int, int, int, int, int, int, float, float, float, float, float, float);
    int render(float);
    const float* getQuadBufferPointer(void);
    int getQuadCount(void);
    const float* getHitEventBufferPointer(void);
    int getHitEventCount(void);
    const char* getMetadataTitle(void);
    const char* getMetadataArtist(void);
    const char* getMetadataDesigner(void);
}

namespace core_api
{

void init()
{
    ::init(0);
}

void dispose()
{
    ::dispose();
}

void resize(int width, int height, float dpr)
{
    ::resize(width, height, dpr);
}

bool loadSusTextPrecise(const char* susText, double normalizedOffsetMs)
{
    return ::loadSusTextPrecise(susText, normalizedOffsetMs) != 0;
}

bool loadSusText(const std::string& susText, int normalizedOffsetMs)
{
    return ::loadSusText(susText.c_str(), normalizedOffsetMs) != 0;
}

const char* getLastError()
{
    return ::getLastError();
}

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
    float backgroundBrightness)
{
    ::setPreviewConfig(mirror, flickAnimation, holdAnimation, simultaneousLine, effectProfile, noteSkin,
        noteSpeed, holdAlpha, guideAlpha, stageCover, stageOpacity, backgroundBrightness);
}

int render(float chartTimeSec)
{
    return ::render(chartTimeSec);
}

const float* getQuadBuffer()
{
    return ::getQuadBufferPointer();
}

int getQuadCount()
{
    return ::getQuadCount();
}

const float* getHitEventBuffer()
{
    return ::getHitEventBufferPointer();
}

int getHitEventCount()
{
    return ::getHitEventCount();
}

const char* getMetadataTitle()
{
    return ::getMetadataTitle();
}

const char* getMetadataArtist()
{
    return ::getMetadataArtist();
}

const char* getMetadataDesigner()
{
    return ::getMetadataDesigner();
}

double readWaveOffset(const std::string& susText)
{
    // SUS header lines look like "#WAVEOFFSET -1.5"; value is in seconds.
    double result = 0.0;
    std::size_t pos = 0;
    while ((pos = susText.find("WAVEOFFSET", pos)) != std::string::npos) {
        const std::size_t lineStart = susText.rfind('#', pos);
        if (lineStart != std::string::npos && susText.find('\n', lineStart) > pos) {
            const std::size_t valueStart = susText.find_first_not_of(" \t", pos + 10);
            if (valueStart != std::string::npos) {
                result = std::atof(susText.c_str() + valueStart);
            }
            break;
        }
        ++pos;
    }
    return result;
}

} // namespace core_api
