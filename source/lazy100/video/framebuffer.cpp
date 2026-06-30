#include "lazy100/video/framebuffer.hpp"

#include <algorithm>

namespace lazy100
{
    void Framebuffer::cls(u8 color) { px_.fill(color); }

    void Framebuffer::pset(int x, int y, u8 color)
    {
        if (in_bounds(x, y))
            px_[static_cast<u32>(y) * kScreenW + static_cast<u32>(x)] = color;
    }

    u8 Framebuffer::pget(int x, int y) const
    {
        return in_bounds(x, y) ? px_[static_cast<u32>(y) * kScreenW + static_cast<u32>(x)] : 0;
    }

    void Framebuffer::rectfill(int x0, int y0, int x1, int y1, u8 color)
    {
        if (x0 > x1)
            std::swap(x0, x1);
        if (y0 > y1)
            std::swap(y0, y1);
        x0 = std::max(x0, 0);
        y0 = std::max(y0, 0);
        x1 = std::min(x1, static_cast<int>(kScreenW) - 1);
        y1 = std::min(y1, static_cast<int>(kScreenH) - 1);
        for (int y = y0; y <= y1; ++y)
        {
            u8* row = px_.data() + static_cast<u32>(y) * kScreenW;
            for (int x = x0; x <= x1; ++x)
                row[x] = color;
        }
    }
} // namespace lazy100
