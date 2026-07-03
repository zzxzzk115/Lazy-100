#pragma once

#include "lazy100/common/types.hpp"
#include "lazy100/console/config.hpp"

#include <array>
#include <utility>

namespace lazy100
{
    // The console's 320x240 indexed framebuffer: one palette index per pixel. All CPU-side
    // drawing writes here; gpu/present uploads it as an R8 texture each frame. M1 ships the
    // primitives needed to prove the pipeline (cls/pset/rectfill); lines/circles/text arrive
    // with video/draw in M2.
    class Framebuffer
    {
    public:
        void cls(u8 color = 0); // full clear; ignores the clip region
        void pset(int x, int y, u8 color);
        u8   pget(int x, int y) const;
        void rectfill(int x0, int y0, int x1, int y1, u8 color);

        // Clipping region (clip()): pset - and so every primitive/blit built on it -
        // and rectfill only touch pixels inside it. clip_reset restores the full screen.
        void clip(int x, int y, int w, int h);
        void clip_reset();

        // fillp() dither pattern, applied by the shape primitives (fpset/frectfill and the
        // draw:: shapes) but never by sprite blits or engine chrome. `bits` is a 4x4 pattern
        // (bit 15 = top-left pixel of the tile); a set bit paints the secondary color, or
        // nothing when transparent. In nibble mode (p8 carts) the two colors ride in the low
        // and high nibble of the draw color instead of `second`.
        void fillp_set(u16 bits, bool transparent, bool nibble, u8 second)
        {
            fillp_bits_   = bits;
            fillp_transp_ = transparent;
            fillp_nibble_ = nibble;
            fillp_second_ = second;
        }
        void fillp_reset() { fillp_set(0, true, false, 0); }
        // Secondary palette (pal mode 2): per-color dither pair (low nibble = clear bits,
        // high nibble = set bits) used when a plain color (< 16) draws under a nibble-mode
        // pattern. Persists independently of fillp(); pal() with no args resets it.
        void fillp_secondary(int c, u8 pair) { fillp_sec_[c & 15] = pair; }
        void fillp_secondary_reset()
        {
            for (int i = 0; i < 16; ++i)
                fillp_sec_[i] = static_cast<u8>(i); // high nibble 0: set bits fall to black
        }
        u32  fillp_save() const // pack the state for save/restore around engine chrome
        {
            return fillp_bits_ | (fillp_transp_ ? 0x10000u : 0) | (fillp_nibble_ ? 0x20000u : 0) |
                   (static_cast<u32>(fillp_second_) << 18);
        }
        void fillp_restore(u32 v)
        {
            fillp_set(static_cast<u16>(v & 0xffff), (v & 0x10000u) != 0, (v & 0x20000u) != 0,
                      static_cast<u8>((v >> 18) & 0xff));
        }

        // Pattern-aware pixel/rect: identical to pset/rectfill while no pattern is set.
        void fpset(int x, int y, u8 color)
        {
            if (fillp_bits_ == 0)
            {
                pset(x, y, color);
                return;
            }
            const bool second = (fillp_bits_ >> (15 - ((y & 3) * 4 + (x & 3)))) & 1;
            u8         pair   = color; // 0xXY form carries its own dither pair
            if (fillp_nibble_ && color < 16)
                pair = fillp_sec_[color]; // plain color: the secondary palette supplies it
            if (!second)
                pset(x, y, fillp_nibble_ ? (pair & 0xf) : color);
            else if (!fillp_transp_)
                pset(x, y, fillp_nibble_ ? ((pair >> 4) & 0xf) : fillp_second_);
        }
        void frectfill(int x0, int y0, int x1, int y1, u8 color)
        {
            if (fillp_bits_ == 0)
            {
                rectfill(x0, y0, x1, y1, color);
                return;
            }
            if (x1 < x0)
                std::swap(x0, x1);
            if (y1 < y0)
                std::swap(y0, y1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    fpset(x, y, color);
        }

        const u8* pixels() const { return px_.data(); }
        u8*       pixels_mut() { return px_.data(); } // raw, clip-agnostic (present-time palette bake)

        static constexpr u32 width() { return kScreenW; }
        static constexpr u32 height() { return kScreenH; }
        static constexpr u32 pixel_count() { return kScreenW * kScreenH; }

    private:
        static bool in_bounds(int x, int y)
        {
            return x >= 0 && y >= 0 && x < static_cast<int>(kScreenW) && y < static_cast<int>(kScreenH);
        }
        bool in_clip(int x, int y) const
        {
            return x >= clip_x0_ && y >= clip_y0_ && x <= clip_x1_ && y <= clip_y1_;
        }

        std::array<u8, kScreenW * kScreenH> px_ {};
        int clip_x0_ = 0, clip_y0_ = 0;
        int clip_x1_ = static_cast<int>(kScreenW) - 1, clip_y1_ = static_cast<int>(kScreenH) - 1;

        u16  fillp_bits_   = 0;    // 0 = no pattern (fast path)
        u8   fillp_sec_[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        bool fillp_transp_ = true; // set bits: skip (true) or paint the secondary color
        bool fillp_nibble_ = false; // p8 mode: colors packed as low/high nibbles of the draw color
        u8   fillp_second_ = 0;
    };
} // namespace lazy100
