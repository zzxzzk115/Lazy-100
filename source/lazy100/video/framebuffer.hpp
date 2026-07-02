#pragma once

#include "lazy100/common/types.hpp"
#include "lazy100/console/config.hpp"

#include <array>

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

        const u8* pixels() const { return px_.data(); }

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
    };
} // namespace lazy100
