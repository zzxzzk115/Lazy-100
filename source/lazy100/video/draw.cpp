#include "lazy100/video/draw.hpp"

#include "lazy100/video/framebuffer.hpp"

#include <algorithm>
#include <cmath>
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

    namespace
    {
        // Ellipse inscribed in the (x0,y0)-(x1,y1) bounding box, rasterized per scanline from
        // the analytic half-width; `spans` receives one horizontal span per row. Works with
        // half-pixel centers so even-sized boxes stay symmetric.
        template <typename SpanFn>
        void ellipse_rows(int x0, int y0, int x1, int y1, SpanFn&& span)
        {
            if (x0 > x1)
                std::swap(x0, x1);
            if (y0 > y1)
                std::swap(y0, y1);
            const double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
            const double a = (x1 - x0) * 0.5, b = (y1 - y0) * 0.5;
            if (b <= 0.0) // degenerate: a 1px-tall box is a horizontal line
            {
                span(y0, x0, x1);
                return;
            }
            for (int y = y0; y <= y1; ++y)
            {
                const double dy = (y - cy) / b;
                const double t  = 1.0 - dy * dy;
                if (t < 0.0)
                    continue;
                const int half = static_cast<int>(a * std::sqrt(t) + 0.5);
                span(y, static_cast<int>(cx - half), static_cast<int>(cx + half + 0.5));
            }
        }
    } // namespace

    void oval(Framebuffer& fb, int x0, int y0, int x1, int y1, u8 c)
    {
        // Outline: per-row endpoints, plus a filled edge row wherever the width jumps by more
        // than a pixel between adjacent rows (keeps the flat top/bottom closed).
        int prevL = 0, prevR = -1;
        bool first = true;
        ellipse_rows(x0, y0, x1, y1, [&](int y, int l, int r) {
            if (first || l > prevR || r < prevL) // first/last visible rows: draw the full span
                fb.rectfill(l, y, r, y, c);
            else
            {
                fb.rectfill(l, y, std::min(prevL - 1, r), y, c); // left edge step
                fb.rectfill(std::max(prevR + 1, l), y, r, y, c); // right edge step
                fb.pset(l, y, c);
                fb.pset(r, y, c);
            }
            prevL = l;
            prevR = r;
            first = false;
        });
        // close the last row (bottom cap) the same way the first was
        if (!first)
            fb.rectfill(prevL, std::max(y0, y1), prevR, std::max(y0, y1), c);
    }

    void ovalfill(Framebuffer& fb, int x0, int y0, int x1, int y1, u8 c)
    {
        ellipse_rows(x0, y0, x1, y1, [&](int y, int l, int r) { fb.rectfill(l, y, r, y, c); });
    }
} // namespace lazy100::draw
