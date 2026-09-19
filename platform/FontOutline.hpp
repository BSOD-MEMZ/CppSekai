// CppSekai - outline-only text rasteriser (see FontOutline.cpp).
#pragma once

#include <string>
#include <vector>

namespace platform
{

// Everything buildTextOutline() needs. The bitmap it returns covers the
// virtual rectangle (0,0)..(outW,outH), so the caller draws it at (0,0) and
// has no offset math to do.
struct OutlineRequest
{
    std::string ttfPath;      // a .ttf or the face-0 .ttc the body font came from
    std::string text;
    float pixelHeight = 322.0f; // em size, in canvas pixels
    float originX = 0.0f;       // where the pen starts, in canvas pixels
    float advance = 166.0f;     // per-glyph pitch (canvas pixels)
    float centerY = 0.0f;       // canvas y the text's ink is centred on
    float strokePx = 3.0f;      // outline width, measured outwards from the ink
};

// Returns an RGBA8 bitmap of `dilate(silhouette, strokePx) - silhouette`:
// white, with the coverage in all four channels. Empty on failure.
//
// Why this exists at all: ImGui's draw list can only stamp *filled* glyphs, so
// the usual outline trick is "stamp the silhouette around a circle in the
// stroke colour, then punch the body out with the backdrop colour". That works
// over a flat backdrop and falls apart over a textured one - the punched body
// shows up as a flat block, which is exactly what the result screen's RESULT
// watermark did once the wash was removed. A real outline leaves the body
// alone and lets whatever is behind it show through.
//
// The stroke joins smoothly (the dilation is circular, not square), and the
// difference against the original coverage keeps antialiased edges antialiased.
std::vector<unsigned char> buildTextOutline(const OutlineRequest& request, int& outW, int& outH);

} // namespace platform
