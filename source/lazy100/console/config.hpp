#pragma once

#include <cstdint>

namespace lazy100
{
    // The console's fixed virtual screen. 320x240 (not 128x128) so text stays readable.
    inline constexpr std::uint32_t kScreenW = 320;
    inline constexpr std::uint32_t kScreenH = 240;

    // Palette entries. Indices stored in the framebuffer are 0..kPaletteSize-1.
    inline constexpr std::uint32_t kPaletteSize = 32;

    // Logic update rate (carts that define _update60 run at 60). Render follows vsync.
    inline constexpr std::uint32_t kTargetFps = 30;

    // Initial integer upscale: a 320x240 console opens as a 960x720 window.
    inline constexpr std::uint32_t kDefaultScale = 3;
} // namespace lazy100
