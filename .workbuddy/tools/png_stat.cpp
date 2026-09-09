// Debug helper: prints the mean RGB / brightness of a PNG (used to check how
// dark the pause backdrop actually is).
// Build: zig c++ -O2 -Ithird_party -Ithird_party/mmw_preview/vendor
//        .workbuddy/tools/png_stat.cpp -o build/png_stat.exe
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: png_stat <file.png>\n");
        return 1;
    }
    int w = 0;
    int h = 0;
    stbi_uc* px = stbi_load(argv[1], &w, &h, nullptr, 4);
    if (px == nullptr) {
        std::printf("cannot read %s\n", argv[1]);
        return 1;
    }
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    const size_t n = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < n; ++i) {
        r += px[i * 4 + 0];
        g += px[i * 4 + 1];
        b += px[i * 4 + 2];
    }
    std::printf("%s %dx%d mean R=%.1f G=%.1f B=%.1f brightness=%.1f\n", argv[1], w, h,
        r / n, g / n, b / n, (r + g + b) / (3.0 * n));
    // 4x4 grid of cell brightness (row 0 = top) to locate dark/light areas.
    for (int cy = 0; cy < 4; ++cy) {
        for (int cx = 0; cx < 4; ++cx) {
            double cr = 0, cg = 0, cb = 0;
            size_t cn = 0;
            for (int y = cy * h / 4; y < (cy + 1) * h / 4; ++y) {
                for (int x = cx * w / 4; x < (cx + 1) * w / 4; ++x) {
                    const size_t i = (static_cast<size_t>(y) * w + x) * 4;
                    cr += px[i + 0];
                    cg += px[i + 1];
                    cb += px[i + 2];
                    ++cn;
                }
            }
            std::printf("  cell[%d][%d] %.1f\n", cy, cx, (cr + cg + cb) / (3.0 * cn));
        }
    }
    stbi_image_free(px);
    return 0;
}
