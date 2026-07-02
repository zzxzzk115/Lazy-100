#pragma once

#include "lazy100/common/types.hpp"

namespace lazy100
{
    class Framebuffer;

    // Runtime glyph rendering: a pixel TTF (Fusion Pixel, pan-CJK) is rasterized on demand at
    // its native size with stb_truetype and cached, so print() handles any UTF-8 text -
    // half-width Latin and full-width 中日韩 alike.
    namespace font
    {
        bool init(); // load the built-in TTF from the embedded VFS; false if missing/invalid
        void shutdown();

        // Draw UTF-8 `text` top-left at (x,y) in color c; handles '\n'. Returns the x just
        // past the last glyph (print()'s return value).
        int print(Framebuffer& fb, const char* text, int x, int y, u8 c);

        // Advance width of `text` in pixels (no drawing) — for carets, columns, popups.
        int text_width(const char* text);

        int line_height();
    } // namespace font
} // namespace lazy100
