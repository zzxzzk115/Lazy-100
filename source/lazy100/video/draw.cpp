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
            fb.fpset(x0, y0, c);
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
        fb.frectfill(x0, y0, x1, y0, c); // top
        fb.frectfill(x0, y1, x1, y1, c); // bottom
        fb.frectfill(x0, y0, x0, y1, c); // left
        fb.frectfill(x1, y0, x1, y1, c); // right
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
            fb.fpset(cx + x, cy + y, c);
            fb.fpset(cx + y, cy + x, c);
            fb.fpset(cx - y, cy + x, c);
            fb.fpset(cx - x, cy + y, c);
            fb.fpset(cx - x, cy - y, c);
            fb.fpset(cx - y, cy - x, c);
            fb.fpset(cx + y, cy - x, c);
            fb.fpset(cx + x, cy - y, c);
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
            fb.frectfill(cx - x, cy + y, cx + x, cy + y, c);
            fb.frectfill(cx - x, cy - y, cx + x, cy - y, c);
            fb.frectfill(cx - y, cy + x, cx + y, cy + x, c);
            fb.frectfill(cx - y, cy - x, cx + y, cy - x, c);
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
        // Midpoint ellipse in the inclusive (x0,y0)-(x1,y1) bounding box, matching the classic
        // console's "fat" rasterization: a square box reproduces circfill's shape exactly.
        // Twin centers extend it to even spans - two adjacent center columns/rows share the
        // arcs, so the ellipse still fills the whole box symmetrically. `arc` receives each
        // arc extent; `flats` the center band once.
        template <typename ArcFn, typename FlatFn>
        void ellipse_arcs(int x0, int y0, int x1, int y1, ArcFn&& arc, FlatFn&& flats)
        {
            const int xr = (x1 - x0) / 2, yr = (y1 - y0) / 2;
            const int xcl = x0 + xr, xcr = x1 - xr; // twin centers (equal when the span is odd)
            const int yct = y0 + yr, ycb = y1 - yr;

            flats(xcl, xcr, yct, ycb);

            const i64 asq = static_cast<i64>(xr) * xr;
            const i64 bsq = static_cast<i64>(yr) * yr;

            // Top/bottom arcs (stepping x, shrinking y).
            i64 wx = 0, wy = yr, xa = 0, ya = asq * 2 * yr;
            i64 thresh = asq / 4 - asq * yr;
            for (;;)
            {
                thresh += xa + bsq;
                if (thresh >= 0)
                {
                    ya -= asq * 2;
                    thresh -= ya;
                    --wy;
                }
                xa += bsq * 2;
                ++wx;
                if (xa >= ya)
                    break;
                arc(static_cast<int>(xcl - wx), static_cast<int>(xcr + wx),
                    static_cast<int>(yct - wy), static_cast<int>(ycb + wy));
            }
            // Left/right arcs (stepping y, shrinking x).
            wx = xr;
            wy = 0;
            xa = bsq * 2 * xr;
            ya = 0;
            thresh = bsq / 4 - bsq * xr;
            for (;;)
            {
                thresh += ya + asq;
                if (thresh >= 0)
                {
                    xa -= bsq * 2;
                    thresh -= xa;
                    --wx;
                }
                ya += asq * 2;
                ++wy;
                if (ya > xa || (ya == 0 && xa == 0))
                    break;
                arc(static_cast<int>(xcl - wx), static_cast<int>(xcr + wx),
                    static_cast<int>(yct - wy), static_cast<int>(ycb + wy));
            }
        }
    } // namespace

    void oval(Framebuffer& fb, int x0, int y0, int x1, int y1, u8 c)
    {
        if (x0 > x1)
            std::swap(x0, x1);
        if (y0 > y1)
            std::swap(y0, y1);
        if (x1 - x0 < 2 || y1 - y0 < 2) // degenerate: a bar
        {
            fb.frectfill(x0, y0, x1, y1, c);
            return;
        }
        ellipse_arcs(
            x0, y0, x1, y1,
            [&](int xl, int xr, int yt, int yb)
            {
                fb.fpset(xl, yt, c);
                fb.fpset(xr, yt, c);
                fb.fpset(xl, yb, c);
                fb.fpset(xr, yb, c);
            },
            [&](int xcl, int xcr, int yct, int ycb)
            {
                fb.frectfill(xcl, y0, xcr, y0, c); // top cap
                fb.frectfill(xcl, y1, xcr, y1, c); // bottom cap
                fb.frectfill(x0, yct, x0, ycb, c); // left cap
                fb.frectfill(x1, yct, x1, ycb, c); // right cap
            });
    }

    void ovalfill(Framebuffer& fb, int x0, int y0, int x1, int y1, u8 c)
    {
        if (x0 > x1)
            std::swap(x0, x1);
        if (y0 > y1)
            std::swap(y0, y1);
        if (x1 - x0 < 2 || y1 - y0 < 2) // degenerate: a bar
        {
            fb.frectfill(x0, y0, x1, y1, c);
            return;
        }
        int yctS = 0, ycbS = 0;
        ellipse_arcs(
            x0, y0, x1, y1,
            [&](int xl, int xr, int yt, int yb)
            {
                fb.frectfill(xl, yt, xr, yt, c);
                fb.frectfill(xl, yb, xr, yb, c);
            },
            [&](int xcl, int xcr, int yct, int ycb)
            {
                yctS = yct;
                ycbS = ycb;
                // Cardinal caps: the extreme rows always carry at least the center pixels
                // (a very flat ellipse's top row is narrower than any arc reaches).
                fb.frectfill(xcl, y0, xcr, y0, c);
                fb.frectfill(xcl, y1, xcr, y1, c);
            });
        // Middle band: full-width rows between the top and bottom arc sets.
        fb.frectfill(x0, yctS, x1, ycbS, c);
    }
} // namespace lazy100::draw
