// CppSekai - outline-only text rasteriser.
//
// A stb_truetype translation unit of its own: imstb_truetype.h already has its
// implementation compiled into third_party/imgui/imgui_draw.cpp, so this file
// takes the STBTT_STATIC flavour - every symbol is local, and the two copies
// cannot collide at link time.
#include "platform/FontOutline.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>

namespace platform
{
namespace
{
    bool readFile(const std::string& path, std::vector<unsigned char>& out)
    {
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) {
            return false;
        }
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (size <= 0) {
            std::fclose(file);
            return false;
        }
        out.resize(static_cast<std::size_t>(size));
        const std::size_t got = std::fread(out.data(), 1, out.size(), file);
        std::fclose(file);
        return got == out.size();
    }
} // namespace

std::vector<unsigned char> buildTextOutline(const OutlineRequest& request, int& outW, int& outH)
{
    outW = 0;
    outH = 0;
    if (request.text.empty() || request.pixelHeight <= 1.0f || request.advance <= 0.0f) {
        return {};
    }
    std::vector<unsigned char> file;
    if (!readFile(request.ttfPath, file)) {
        std::fprintf(stderr, "fontoutline: cannot read '%s'\n", request.ttfPath.c_str());
        return {};
    }
    const int faceOffset = stbtt_GetFontOffsetForIndex(file.data(), 0);
    stbtt_fontinfo font{};
    if (faceOffset < 0 || stbtt_InitFont(&font, file.data(), faceOffset) == 0) {
        std::fprintf(stderr, "fontoutline: '%s' is not a usable TTF/TTC\n", request.ttfPath.c_str());
        return {};
    }
    const float scale = stbtt_ScaleForPixelHeight(&font, request.pixelHeight);

    // Pass 1: rasterise every glyph and remember where it wants to sit, with
    // the pen starting at originX and the baseline at y = 0. The run is
    // measured in that temporary space and shifted down as a whole afterwards,
    // so the ink ends up centred on the y the caller asked for.
    struct Piece
    {
        int x = 0; // bitmap origin relative to the pen, in canvas pixels
        int y = 0; // ... and to the baseline (y grows downwards, so usually < 0)
        int w = 0;
        int h = 0;
        std::vector<unsigned char> ink;
    };
    std::vector<Piece> pieces;
    pieces.reserve(request.text.size());
    int inkMinX = INT_MAX;
    int inkMaxX = INT_MIN;
    int inkMinY = INT_MAX;
    int inkMaxY = INT_MIN;
    float pen = request.originX;
    for (const char character : request.text) {
        int w = 0;
        int h = 0;
        int xo = 0;
        int yo = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&font, 0.0f, scale,
            static_cast<unsigned char>(character), &w, &h, &xo, &yo);
        if (bitmap == nullptr) {
            pen += request.advance; // a glyph this face cannot draw: keep the pitch
            continue;
        }
        Piece piece;
        piece.x = static_cast<int>(std::lround(pen)) + xo;
        piece.y = yo;
        piece.w = w;
        piece.h = h;
        piece.ink.assign(bitmap, bitmap + static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        stbtt_FreeBitmap(bitmap, nullptr);
        inkMinX = std::min(inkMinX, piece.x);
        inkMaxX = std::max(inkMaxX, piece.x + w);
        inkMinY = std::min(inkMinY, piece.y);
        inkMaxY = std::max(inkMaxY, piece.y + h);
        pieces.push_back(std::move(piece));
        pen += request.advance;
    }
    if (pieces.empty() || inkMinX == INT_MAX) {
        return {};
    }

    const int baselineY = static_cast<int>(
        std::lround(request.centerY - (inkMinY + inkMaxY) * 0.5f));
    const int stroke = std::max(1, static_cast<int>(std::lround(request.strokePx)));
    outW = std::max(1, inkMaxX + stroke);
    outH = std::max(1, baselineY + inkMaxY + stroke);

    std::vector<unsigned char> coverage(static_cast<std::size_t>(outW) * outH, 0);
    for (const Piece& piece : pieces) {
        const int ox = piece.x;
        const int oy = baselineY + piece.y;
        for (int y = 0; y < piece.h; ++y) {
            const int dy = oy + y;
            if (dy < 0 || dy >= outH) {
                continue;
            }
            for (int x = 0; x < piece.w; ++x) {
                const int dx = ox + x;
                if (dx < 0 || dx >= outW) {
                    continue;
                }
                coverage[static_cast<std::size_t>(dy) * outW + dx]
                    = piece.ink[static_cast<std::size_t>(y) * piece.w + x];
            }
        }
    }

    // Pass 2: circular dilation minus the original coverage = the ring. The
    // difference is taken per pixel, which keeps antialiased edges
    // antialiased instead of turning them into a hard 1-bit outline. Simple
    // per-pixel dilation is ~17M taps for a six-letter run at 322 px; this
    // runs once per process, so clarity wins over a separable formulation.
    std::vector<unsigned char> rgba(static_cast<std::size_t>(outW) * outH * 4, 0);
    const int radiusSquared = stroke * stroke;
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * outW + x;
            const unsigned char self = coverage[index];
            unsigned char ring = 0;
            if (self < 255) {
                for (int dy = -stroke; dy <= stroke && ring < 255; ++dy) {
                    const int sy = y + dy;
                    if (sy < 0 || sy >= outH) {
                        continue;
                    }
                    for (int dx = -stroke; dx <= stroke; ++dx) {
                        if (dx * dx + dy * dy > radiusSquared) {
                            continue;
                        }
                        const int sx = x + dx;
                        if (sx < 0 || sx >= outW) {
                            continue;
                        }
                        const unsigned char sample
                            = coverage[static_cast<std::size_t>(sy) * outW + sx];
                        if (sample > ring) {
                            ring = sample;
                        }
                    }
                }
            }
            const int alpha = static_cast<int>(ring) - static_cast<int>(self);
            if (alpha > 0) {
                rgba[index * 4 + 0] = 255;
                rgba[index * 4 + 1] = 255;
                rgba[index * 4 + 2] = 255;
                rgba[index * 4 + 3] = static_cast<unsigned char>(alpha);
            }
        }
    }
    return rgba;
}

} // namespace platform
