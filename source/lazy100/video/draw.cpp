#include "lazy100/video/draw.hpp"

#include "lazy100/video/framebuffer.hpp"

#include <cstdlib>

namespace lazy100::draw
{
    void line(Framebuffer& fb, int x0, int y0, int x1, int y1, u8 c)
    {
        // Integer Bresenham.
        int dx  = std::abs(x1 - x0);
        int dy  = -std::abs(y1 - y0);
        int sx  = x0 < x1 ? 1 : -1;
        int sy  = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;)
        {
            fb.pset(x0, y0, c);
            if (x0 == x1 && y0 == y1)
                break;
            const int e2 = 2 * err;
            if (e2 >= dy)
            {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx)
            {
                err += dx;
                y0 += sy;
            }
        }
    }

    void rect(Framebuffer& fb, int x0, int y0, int x1, int y1, u8 c)
    {
        fb.rectfill(x0, y0, x1, y0, c); // top
        fb.rectfill(x0, y1, x1, y1, c); // bottom
        fb.rectfill(x0, y0, x0, y1, c); // left
        fb.rectfill(x1, y0, x1, y1, c); // right
    }

    void circ(Framebuffer& fb, int cx, int cy, int r, u8 c)
    {
        if (r < 0)
            return;
        // Midpoint circle, 8-way symmetry.
        int x   = r;
        int y   = 0;
        int err = 1 - r;
        while (x >= y)
        {
            fb.pset(cx + x, cy + y, c);
            fb.pset(cx + y, cy + x, c);
            fb.pset(cx - y, cy + x, c);
            fb.pset(cx - x, cy + y, c);
            fb.pset(cx - x, cy - y, c);
            fb.pset(cx - y, cy - x, c);
            fb.pset(cx + y, cy - x, c);
            fb.pset(cx + x, cy - y, c);
            ++y;
            if (err < 0)
                err += 2 * y + 1;
            else
            {
                --x;
                err += 2 * (y - x) + 1;
            }
        }
    }

    void circfill(Framebuffer& fb, int cx, int cy, int r, u8 c)
    {
        if (r < 0)
            return;
        int x   = r;
        int y   = 0;
        int err = 1 - r;
        while (x >= y)
        {
            // Horizontal spans for each symmetric pair of scanlines.
            fb.rectfill(cx - x, cy + y, cx + x, cy + y, c);
            fb.rectfill(cx - x, cy - y, cx + x, cy - y, c);
            fb.rectfill(cx - y, cy + x, cx + y, cy + x, c);
            fb.rectfill(cx - y, cy - x, cx + y, cy - x, c);
            ++y;
            if (err < 0)
                err += 2 * y + 1;
            else
            {
                --x;
                err += 2 * (y - x) + 1;
            }
        }
    }
} // namespace lazy100::draw
