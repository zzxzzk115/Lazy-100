#pragma once

#include "lazy100/common/types.hpp"

namespace lazy100
{
    class Framebuffer;

    // Higher-level drawing primitives built on the framebuffer's pixel ops. cls/pset/pget/
    // rectfill live on Framebuffer itself (the irreducible store); these compose them.
    namespace draw
    {
        void line(Framebuffer& fb, int x0, int y0, int x1, int y1, u8 c);
        void rect(Framebuffer& fb, int x0, int y0, int x1, int y1, u8 c);     // outline
        void circ(Framebuffer& fb, int cx, int cy, int r, u8 c);             // outline
        void circfill(Framebuffer& fb, int cx, int cy, int r, u8 c);         // filled
    } // namespace draw
} // namespace lazy100
