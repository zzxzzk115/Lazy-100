#include "lazy100/video/cursor.hpp"

#include "lazy100/video/framebuffer.hpp"

namespace lazy100::cursor
{
    namespace
    {
        constexpr u8 kW = 7; // white fill
        constexpr u8 kB = 0; // black outline

        struct Shape
        {
            const char* const* rows;
            int                w, h, hx, hy;
        };

        // '.' transparent, 'W' white fill, 'B' black outline. Hotspot = (hx, hy).
        const char* kArrow[] = {
            "B........", "BB.......", "BWB......", "BWWB.....", "BWWWB....", "BWWWWB...", "BWWWWWB..",
            "BWWWWWWB.", "BWWWWBBBB", "BWBWB....", "BB.BWB...", "...BWB...", "....BB...",
        };
        const char* kPencil[] = {
            ".....BB.", "....BWWB", "...BWWWB", "..BWWWB.", ".BWWWB..", "BWWWB...", "BWBB....", "BB......",
        };
        const char* kCross[] = {
            "..BBB..", "..BWB..", "BBBWBBB", "BWW.WWB", "BBBWBBB", "..BWB..", "..BBB..",
        };
        const char* kIbeam[] = {
            "BWWWB", "BBWBB", ".BWB.", ".BWB.", ".BWB.", ".BWB.", ".BWB.", "BBWBB", "BWWWB",
        };

        const Shape kShapes[Count] = {
            {kArrow, 9, 13, 0, 0},
            {kPencil, 8, 8, 0, 7},
            {kCross, 7, 7, 3, 3},
            {kIbeam, 5, 9, 2, 4},
        };
    } // namespace

    void draw(Framebuffer& fb, Type t, int x, int y)
    {
        if (t < 0 || t >= Count)
            return;
        const Shape& s  = kShapes[t];
        const int    ox = x - s.hx, oy = y - s.hy;
        for (int j = 0; j < s.h; ++j)
            for (int i = 0; i < s.w; ++i)
            {
                const char c = s.rows[j][i];
                if (c == 'W')
                    fb.pset(ox + i, oy + j, kW);
                else if (c == 'B')
                    fb.pset(ox + i, oy + j, kB);
            }
    }
} // namespace lazy100::cursor
