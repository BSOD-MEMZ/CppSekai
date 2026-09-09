// Offline HUD texture shrinker: copies assets/mmw/overlay/*.png into
// assets/mmw/overlay_opt/, downscaling the oversized originals (e.g. the
// 1000x1333 life digits, drawn at ~50px) so boot-time PNG decode is fast.
// alpha-weighted (premultiplied) box filter, integer factor, so antialiased
// edges do not darken.
//
// Usage: shrink_hud <srcDir> <dstDir> <maxDim> [skip1,skip2,...]
//   Files whose max(w,h) <= maxDim are copied unchanged; names in the skip
//   list are always copied unchanged. Files outside srcDir (the loadHud
//   "../x.png" entries) are not touched.
//
// Build:  zig c++ -O2 -Ithird_party .workbuddy/tools/shrink_hud.cpp -o build/shrink_hud.exe

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static bool hasExt(const std::string& name, const char* ext)
{
    return name.size() >= 5 && _stricmp(name.c_str() + name.size() - 4, ext) == 0;
}

static void mkdirs(const fs::path& dir)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::printf("usage: shrink_hud <srcDir> <dstDir> <maxDim> [skip1,skip2,...]\n");
        return 1;
    }
    const fs::path srcDir = argv[1];
    const fs::path dstDir = argv[2];
    const int maxDim = std::atoi(argv[3]);
    std::vector<std::string> skip;
    if (argc >= 5) {
        const std::string list = argv[4];
        size_t start = 0;
        while (start <= list.size()) {
            const size_t comma = list.find(',', start);
            skip.push_back(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
    }
    const auto isSkipped = [&](const std::string& name) {
        for (const std::string& s : skip) {
            if (!s.empty() && name == s) {
                return true;
            }
        }
        return false;
    };

    int shrunk = 0;
    int copied = 0;
    double srcMpx = 0.0;
    double dstMpx = 0.0;
    for (const auto& entry : fs::recursive_directory_iterator(srcDir)) {
        if (!entry.is_regular_file() || !hasExt(entry.path().filename().string(), ".png")) {
            continue;
        }
        const fs::path rel = fs::relative(entry.path(), srcDir);
        const fs::path out = dstDir / rel;
        mkdirs(out.parent_path());

        int w = 0;
        int h = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(entry.path().string().c_str(), &w, &h, &channels, 4);
        if (pixels == nullptr) {
            std::printf("FAILED to read: %s\n", entry.path().string().c_str());
            return 1;
        }
        srcMpx += static_cast<double>(w) * h / 1e6;

        int factor = 1;
        const int m = std::max(w, h);
        if (!isSkipped(rel.generic_string()) && m > maxDim) {
            factor = (m + maxDim - 1) / maxDim; // ceil, so result <= maxDim
        }
        const int ow = w / factor;
        const int oh = h / factor;
        dstMpx += static_cast<double>(ow) * oh / 1e6;

        std::vector<stbi_uc> outPixels(static_cast<size_t>(ow) * oh * 4);
        for (int y = 0; y < oh; ++y) {
            for (int x = 0; x < ow; ++x) {
                double r = 0.0;
                double g = 0.0;
                double b = 0.0;
                double a = 0.0;
                for (int sy = 0; sy < factor; ++sy) {
                        const stbi_uc* row = pixels + (static_cast<size_t>(y * factor + sy) * w + x * factor) * 4;
                        for (int sx = 0; sx < factor; ++sx) {
                            // Accumulate premultiplied by the RAW alpha so the
                            // division below un-premultiplies exactly (using
                            // alpha/255 here shrank RGB by ~255x -> black HUD).
                            r += row[sx * 4 + 0] * row[sx * 4 + 3];
                            g += row[sx * 4 + 1] * row[sx * 4 + 3];
                            b += row[sx * 4 + 2] * row[sx * 4 + 3];
                            a += row[sx * 4 + 3];
                        }
                }
                stbi_uc* dst = outPixels.data() + (static_cast<size_t>(y) * ow + x) * 4;
                const double block = static_cast<double>(factor * factor);
                const double alphaAvg = a / block;
                dst[3] = static_cast<stbi_uc>(alphaAvg + 0.5);
                if (a > 0.0) {
                    dst[0] = static_cast<stbi_uc>(r / a + 0.5);
                    dst[1] = static_cast<stbi_uc>(g / a + 0.5);
                    dst[2] = static_cast<stbi_uc>(b / a + 0.5);
                }
            }
        }
        stbi_image_free(pixels);

        if (!stbi_write_png(out.string().c_str(), ow, oh, 4, outPixels.data(), ow * 4)) {
            std::printf("FAILED to write: %s\n", out.string().c_str());
            return 1;
        }
        if (factor > 1) {
            ++shrunk;
            std::printf("shrunk x%d  %5dx%-5d -> %4dx%-4d  %s\n", factor, w, h, ow, oh,
                rel.generic_string().c_str());
        } else {
            ++copied;
        }
    }
    std::printf("done: %d shrunk, %d copied, %.1f Mpx -> %.1f Mpx (%.0f%%)\n", shrunk, copied,
        srcMpx, dstMpx, srcMpx > 0.0 ? 100.0 * dstMpx / srcMpx : 0.0);
    return 0;
}
