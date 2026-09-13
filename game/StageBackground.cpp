#include "StageBackground.hpp"

#include "../third_party/mmw_preview/vendor/stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace game
{
namespace
{
    struct Image
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels; // RGBA8, row major
    };

    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Where the jacket goes, in the room plate's pixel space. Copied verbatim
    // from the upstream generator (it measures them off the plate). Only the
    // "normal" screens are used: the mirror quads target the mirrored playfield
    // layout, which we do not have, and the whole plate is visible at once here
    // (upstream tiles it into a square and scrolls).
    constexpr Point kSideLeft[4] = {{566, 161}, {1183, 134}, {633, 731}, {1226, 682}};
    constexpr Point kSideRight[4] = {{966, 104}, {1413, 72}, {954, 525}, {1390, 524}};
    constexpr Point kCenterNormal[4] = {{824, 227}, {1224, 227}, {833, 608}, {1216, 608}};

    Image loadImage(const std::string& path)
    {
        Image image;
        int channels = 0;
        stbi_uc* data = stbi_load(path.c_str(), &image.width, &image.height, &channels, 4);
        if (data == nullptr || image.width <= 0 || image.height <= 0) {
            if (data != nullptr) {
                stbi_image_free(data);
            }
            return image;
        }
        image.pixels.assign(data, data + static_cast<std::size_t>(image.width) * image.height * 4);
        stbi_image_free(data);
        return image;
    }

    Image makeImage(int width, int height)
    {
        Image image;
        image.width = std::max(1, width);
        image.height = std::max(1, height);
        image.pixels.assign(static_cast<std::size_t>(image.width) * image.height * 4, 0);
        return image;
    }

    Image resizeNearest(const Image& source, int targetWidth, int targetHeight)
    {
        Image target = makeImage(targetWidth, targetHeight);
        if (source.width <= 0 || source.height <= 0) {
            return target;
        }
        for (int y = 0; y < target.height; ++y) {
            const int sourceY = std::min(source.height - 1, y * source.height / target.height);
            for (int x = 0; x < target.width; ++x) {
                const int sourceX = std::min(source.width - 1, x * source.width / target.width);
                const std::size_t src = (static_cast<std::size_t>(sourceY) * source.width + sourceX) * 4;
                const std::size_t dst = (static_cast<std::size_t>(y) * target.width + x) * 4;
                for (int c = 0; c < 4; ++c) {
                    target.pixels[dst + c] = source.pixels[src + c];
                }
            }
        }
        return target;
    }

    // Source-over with straight alpha, same arithmetic as the upstream
    // generator's overlayImage().
    void overlayImage(Image& base, const Image& top, int offsetX, int offsetY)
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
                const std::size_t src = (static_cast<std::size_t>(y) * top.width + x) * 4;
                const std::size_t dst = (static_cast<std::size_t>(targetY) * base.width + targetX) * 4;
                const int srcA = top.pixels[src + 3];
                if (srcA == 0) {
                    continue;
                }
                const int dstA = base.pixels[dst + 3];
                if (dstA == 0 || srcA == 255) {
                    for (int c = 0; c < 4; ++c) {
                        base.pixels[dst + c] = top.pixels[src + c];
                    }
                    continue;
                }
                const float srcAF = static_cast<float>(srcA) / 255.0f;
                const float dstAF = static_cast<float>(dstA) / 255.0f;
                const float outA = srcAF + dstAF * (1.0f - srcAF);
                for (int c = 0; c < 3; ++c) {
                    const float srcC = static_cast<float>(top.pixels[src + c]) / 255.0f;
                    const float dstC = static_cast<float>(base.pixels[dst + c]) / 255.0f;
                    const float outC = (srcC * srcAF + dstC * dstAF * (1.0f - srcAF)) / outA;
                    base.pixels[dst + c] = static_cast<std::uint8_t>(std::lround(outC * 255.0f));
                }
                base.pixels[dst + 3] = static_cast<std::uint8_t>(std::lround(outA * 255.0f));
            }
        }
    }

    // Multiplies the image's alpha by the mask's (min of the two).
    Image applyAlphaMask(const Image& image, const Image& mask)
    {
        Image masked = image;
        const std::size_t length = std::min(masked.pixels.size(), mask.pixels.size());
        for (std::size_t index = 3; index < length; index += 4) {
            masked.pixels[index] = std::min(masked.pixels[index], mask.pixels[index]);
        }
        return masked;
    }

    bool solveLinear8(std::array<std::array<double, 8>, 8> matrix, std::array<double, 8> values,
        std::array<double, 8>& out)
    {
        constexpr int size = 8;
        for (int pivot = 0; pivot < size; ++pivot) {
            int maxRow = pivot;
            double maxValue = std::fabs(matrix[pivot][pivot]);
            for (int row = pivot + 1; row < size; ++row) {
                const double value = std::fabs(matrix[row][pivot]);
                if (value > maxValue) {
                    maxValue = value;
                    maxRow = row;
                }
            }
            if (maxValue < 1e-8) {
                return false;
            }
            if (maxRow != pivot) {
                std::swap(matrix[pivot], matrix[maxRow]);
                std::swap(values[pivot], values[maxRow]);
            }
            const double pivotValue = matrix[pivot][pivot];
            for (int col = pivot; col < size; ++col) {
                matrix[pivot][col] /= pivotValue;
            }
            values[pivot] /= pivotValue;
            for (int row = 0; row < size; ++row) {
                if (row == pivot) {
                    continue;
                }
                const double factor = matrix[row][pivot];
                if (std::fabs(factor) < 1e-8) {
                    continue;
                }
                for (int col = pivot; col < size; ++col) {
                    matrix[row][col] -= factor * matrix[pivot][col];
                }
                values[row] -= factor * values[pivot];
            }
        }
        out = values;
        return true;
    }

    // Homography mapping the four source points onto the four target points,
    // 3x3 row major.
    bool buildHomography(const std::array<Point, 4>& source, const std::array<Point, 4>& target,
        std::array<double, 9>& out)
    {
        std::array<std::array<double, 8>, 8> matrix{};
        std::array<double, 8> values{};
        for (int index = 0; index < 4; ++index) {
            const double sx = source[index].x;
            const double sy = source[index].y;
            const double tx = target[index].x;
            const double ty = target[index].y;
            matrix[index * 2] = {sx, sy, 1.0, 0.0, 0.0, 0.0, -sx * tx, -sy * tx};
            values[index * 2] = tx;
            matrix[index * 2 + 1] = {0.0, 0.0, 0.0, sx, sy, 1.0, -sx * ty, -sy * ty};
            values[index * 2 + 1] = ty;
        }
        std::array<double, 8> solved{};
        if (!solveLinear8(matrix, values, solved)) {
            return false;
        }
        out = {solved[0], solved[1], solved[2], solved[3], solved[4], solved[5], solved[6], solved[7], 1.0};
        return true;
    }

    bool invert3x3(const std::array<double, 9>& m, std::array<double, 9>& out)
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
        const double determinant = a * A + b * B + c * C;
        if (std::fabs(determinant) < 1e-10) {
            return false;
        }
        const double r = 1.0 / determinant;
        out = {A * r, D * r, G * r, B * r, E * r, H * r, C * r, F * r, I * r};
        return true;
    }

    bool projectPoint(const std::array<double, 9>& m, double x, double y, double& outX, double& outY)
    {
        const double denominator = m[6] * x + m[7] * y + m[8];
        if (std::fabs(denominator) < 1e-8) {
            return false;
        }
        outX = (m[0] * x + m[1] * y + m[2]) / denominator;
        outY = (m[3] * x + m[4] * y + m[5]) / denominator;
        return true;
    }

    // Projects the jacket into the quad and returns a plate-sized layer with it
    // at the quad's place.
    Image morph(const Image& source, const std::array<Point, 4>& quad, int targetWidth, int targetHeight)
    {
        float minX = quad[0].x;
        float minY = quad[0].y;
        float maxX = quad[0].x;
        float maxY = quad[0].y;
        for (const Point& point : quad) {
            minX = std::min(minX, point.x);
            minY = std::min(minY, point.y);
            maxX = std::max(maxX, point.x);
            maxY = std::max(maxY, point.y);
        }
        const int width = std::max(1, static_cast<int>(maxX - minX));
        const int height = std::max(1, static_cast<int>(maxY - minY));
        const Image scaled = resizeNearest(source, width, height);

        std::array<Point, 4> local{};
        for (int i = 0; i < 4; ++i) {
            local[i] = {quad[i].x - minX, quad[i].y - minY};
        }
        const std::array<Point, 4> corners{{{0.0f, 0.0f},
            {static_cast<float>(width), 0.0f},
            {0.0f, static_cast<float>(height)},
            {static_cast<float>(width), static_cast<float>(height)}}};

        std::array<double, 9> projection{};
        std::array<double, 9> inverse{};
        Image target = makeImage(targetWidth, targetHeight);
        if (!buildHomography(corners, local, projection) || !invert3x3(projection, inverse)) {
            return target;
        }

        Image projected = makeImage(width, height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                double sampleX = 0.0;
                double sampleY = 0.0;
                if (!projectPoint(inverse, x, y, sampleX, sampleY)) {
                    continue;
                }
                const int sx = static_cast<int>(std::lround(sampleX));
                const int sy = static_cast<int>(std::lround(sampleY));
                if (sx < 0 || sx >= width || sy < 0 || sy >= height) {
                    continue;
                }
                const std::size_t src = (static_cast<std::size_t>(sy) * width + sx) * 4;
                const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 4;
                for (int c = 0; c < 4; ++c) {
                    projected.pixels[dst + c] = scaled.pixels[src + c];
                }
            }
        }
        overlayImage(target, projected, static_cast<int>(minX), static_cast<int>(minY));
        return target;
    }
} // namespace

std::vector<std::uint8_t> buildStageBackground(const std::string& bggenDir, const std::string& jacketPath,
    int& outWidth, int& outHeight)
{
    outWidth = 0;
    outHeight = 0;
    if (bggenDir.empty() || jacketPath.empty()) {
        return {};
    }
    const std::string dir = bggenDir.back() == '/' || bggenDir.back() == '\\' ? bggenDir : bggenDir + "/";
    Image base = loadImage(dir + "base.png");
    Image bottom = loadImage(dir + "bottom.png");
    Image centerCover = loadImage(dir + "center_cover.png");
    Image centerMask = loadImage(dir + "center_mask.png");
    Image sideCover = loadImage(dir + "side_cover.png");
    Image sideMask = loadImage(dir + "side_mask.png");
    Image windows = loadImage(dir + "windows.png");
    Image cover = loadImage(jacketPath);
    if (base.pixels.empty() || bottom.pixels.empty() || centerCover.pixels.empty()
        || centerMask.pixels.empty() || sideCover.pixels.empty() || sideMask.pixels.empty()
        || windows.pixels.empty() || cover.pixels.empty()) {
        return {};
    }

    // Side screens: both side quads into one layer, then the glass on top.
    Image sideJackets = makeImage(base.width, base.height);
    overlayImage(sideJackets, morph(cover, {{kSideLeft[0], kSideLeft[1], kSideLeft[2], kSideLeft[3]}},
                                base.width, base.height),
        0, 0);
    overlayImage(sideJackets, morph(cover, {{kSideRight[0], kSideRight[1], kSideRight[2], kSideRight[3]}},
                                base.width, base.height),
        0, 0);
    overlayImage(sideJackets, sideCover, 0, 0);

    // Centre screen.
    Image center = makeImage(base.width, base.height);
    overlayImage(center, morph(cover, {{kCenterNormal[0], kCenterNormal[1], kCenterNormal[2], kCenterNormal[3]}},
                             base.width, base.height),
        0, 0);
    overlayImage(center, centerCover, 0, 0);

    const Image maskedSide = applyAlphaMask(sideJackets, sideMask);
    const Image maskedCenter = applyAlphaMask(center, centerMask);

    // Compose the plate in the upstream order.
    overlayImage(base, maskedSide, 0, 0);
    overlayImage(base, sideCover, 0, 0);
    overlayImage(base, windows, 0, 0);
    overlayImage(base, maskedCenter, 0, 0);
    overlayImage(base, bottom, 0, 0);

    outWidth = base.width;
    outHeight = base.height;
    return std::move(base.pixels);
}
} // namespace game
