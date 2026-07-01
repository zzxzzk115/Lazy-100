#pragma once

#include "lazy100/common/types.hpp"

namespace lazy100
{
    class Framebuffer;

    // Software pixel-art mouse cursors drawn into the framebuffer (the OS cursor is hidden). Each
    // shape is a two-color bitmap (black outline + white fill) with its own hotspot, so it reads
    // on any background. The console picks a shape by context (shell/paint/text/...).
    namespace cursor
    {
        enum Type
        {
            Arrow = 0, // default pointer (shell, buttons, lists)
            Pencil,    // painting (map viewport)
            Cross,     // precise pixel target (sprite canvas)
            Ibeam,     // text insertion (code editor)
            Count
        };

        // Draw cursor `t` so its hotspot lands on framebuffer pixel (x, y).
        void draw(Framebuffer& fb, Type t, int x, int y);
    } // namespace cursor
} // namespace lazy100
