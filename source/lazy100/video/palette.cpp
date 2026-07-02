#include "lazy100/video/palette.hpp"

namespace lazy100
{
    namespace
    {
        // The first 32 entries are curated for pixel art: a classic 16-color set followed by its
        // 16 "secret" extended colors. Readable and battle-tested.
        constexpr Color32 kCurated[32] = {
            {0, 0, 0, 255},       {29, 43, 83, 255},    {126, 37, 83, 255},   {0, 135, 81, 255},
            {171, 82, 54, 255},   {95, 87, 79, 255},    {194, 195, 199, 255}, {255, 241, 232, 255},
            {255, 0, 77, 255},    {255, 163, 0, 255},   {255, 236, 39, 255},  {0, 228, 54, 255},
            {41, 173, 255, 255},  {131, 118, 156, 255}, {255, 119, 168, 255}, {255, 204, 170, 255},
            {41, 24, 20, 255},    {17, 29, 53, 255},    {66, 33, 54, 255},    {18, 83, 89, 255},
            {116, 47, 41, 255},   {73, 51, 59, 255},    {162, 136, 121, 255}, {243, 239, 125, 255},
            {190, 18, 80, 255},   {255, 108, 36, 255},  {168, 231, 46, 255},  {0, 181, 67, 255},
            {6, 90, 181, 255},    {117, 70, 101, 255},  {255, 110, 89, 255},  {255, 157, 129, 255},
        };

        // The 256-color default: 32 curated entries, then a 6x6x6 RGB color cube (216 entries,
        // indices 32..247), then an 8-step grayscale ramp (indices 248..255).
        Color32 default_color(u32 index)
        {
            if (index < 32)
                return kCurated[index];
            if (index < 248)
            {
                const u32           c     = index - 32; // 0..215
                constexpr u8        lvl[6] = {0, 51, 102, 153, 204, 255};
                return {lvl[(c / 36) % 6], lvl[(c / 6) % 6], lvl[c % 6], 255};
            }
            const u8 g = static_cast<u8>((index - 248) * 255 / 7); // 8-step gray
            return {g, g, g, 255};
        }
    } // namespace

    void Palette::reset()
    {
        for (u32 i = 0; i < kPaletteSize; ++i)
            colors_[i] = default_color(i);
        repack();
    }

    Color32 Palette::default_at(u32 index) const { return default_color(index < kPaletteSize ? index : 0); }

    void Palette::set(u32 index, Color32 c)
    {
        if (index >= kPaletteSize)
            return;
        colors_[index] = c;
        repack();
    }

    void Palette::repack()
    {
        for (u32 i = 0; i < kPaletteSize; ++i)
        {
            const Color32 c = colors_[i];
            packed_[i]      = static_cast<u32>(c.r) | (static_cast<u32>(c.g) << 8) |
                         (static_cast<u32>(c.b) << 16) | (static_cast<u32>(c.a) << 24);
        }
        dirty_ = true;
    }
} // namespace lazy100
