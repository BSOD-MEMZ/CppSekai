#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace game
{
// Builds the song's own stage backdrop ("bggen v3", ported from the upstream
// preview's src/lib/overlayBackgroundGen.ts): the jacket is projected into the
// side screens and the centre screens of the room plate, plus their dim
// reflections in the screens below the stage, masked by the screen glass, and
// the room + windows + bottom overlays are composited on top - so every song
// performs on a stage made of its own jacket, exactly like the game.
//
// `bggenDir` is the folder holding base.png / bottom.png / center_cover.png /
// center_mask.png / side_cover.png / side_mask.png / windows.png (all the same
// size). `jacketPath` is any image stb_image can read. Returns RGBA8 pixels
// (outWidth x outHeight) or an empty vector when the assets or the jacket are
// missing. The plate is square (plate width x plate width, upstream
// renderToSquareBackground): the world background quad is square, so a
// 2048x1168 plate uploaded as-is would be stretched vertically.
std::vector<std::uint8_t> buildStageBackground(const std::string& bggenDir,
    const std::string& jacketPath, int& outWidth, int& outHeight);
} // namespace game
