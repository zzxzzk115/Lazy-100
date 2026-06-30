#pragma once

#include "lazy100/common/types.hpp"

#include <array>

namespace lazy100
{
    class Framebuffer;

    // The 128x128 indexed sprite sheet: 16x16 sprites of 8x8 px. Drawing honors a per-index
    // transparency mask (skip those texels) and a draw-palette remap (recolor on blit), both
    // owned by the Console and passed in.
    class SpriteSheet
    {
    public:
        static constexpr int kSize         = 128;
        static constexpr int kSpritesPerRow = 16;
        static constexpr int kSpriteCount   = 256;

        u8   get(int x, int y) const;
        void set(int x, int y, u8 index);

        u8   flags(int n) const;
        void set_flags(int n, u8 f);
        bool flag_bit(int n, int bit) const;
        void set_flag_bit(int n, int bit, bool on);

        // Draw sprite n (a w*h block of 8px sprites) at (x,y), with optional flips.
        void spr(Framebuffer& fb, int n, int x, int y, int w, int h, bool fx, bool fy, const u8* draw_pal,
                 const bool* transp) const;

        // Copy a source pixel rect to a (possibly scaled) dest rect, with optional flips.
        void sspr(Framebuffer& fb, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool fx, bool fy,
                  const u8* draw_pal, const bool* transp) const;

    private:
        static bool in_bounds(int x, int y) { return x >= 0 && y >= 0 && x < kSize && y < kSize; }

        std::array<u8, kSize * kSize>   px_ {};
        std::array<u8, kSpriteCount>    flags_ {};
    };
} // namespace lazy100
