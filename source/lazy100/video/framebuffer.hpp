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
        void cls(u8 color = 0);
        void pset(int x, int y, u8 color);
        u8   pget(int x, int y) const;
        void rectfill(int x0, int y0, int x1, int y1, u8 color);

        const u8* pixels() const { return px_.data(); }

        static constexpr u32 width() { return kScreenW; }
        static constexpr u32 height() { return kScreenH; }
        static constexpr u32 pixel_count() { return kScreenW * kScreenH; }

    private:
        static bool in_bounds(int x, int y)
        {
            return x >= 0 && y >= 0 && x < static_cast<int>(kScreenW) && y < static_cast<int>(kScreenH);
        }

        std::array<u8, kScreenW * kScreenH> px_ {};
    };
} // namespace lazy100
