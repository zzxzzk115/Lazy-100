#pragma once

#include "lazy100/common/types.hpp"
#include "lazy100/console/config.hpp"

#include <algorithm>

namespace lazy100::layout
{
    struct Rect
    {
        i32 x, y;
        u32 w, h;
    };

    // Integer-scaled, centered letterbox rect for the 320x240 screen inside a w*h window.
    // Shared by gpu/present (where to draw) and input/mouse (window px -> framebuffer px).
    inline Rect letterbox(u32 winW, u32 winH)
    {
        const u32 scale = std::max(1u, std::min(winW / kScreenW, winH / kScreenH));
        const u32 w     = kScreenW * scale;
        const u32 h     = kScreenH * scale;
        return {static_cast<i32>((winW - w) / 2), static_cast<i32>((winH - h) / 2), w, h};
    }
} // namespace lazy100::layout
