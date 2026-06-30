#pragma once

#include "lazy100/common/types.hpp"

namespace lazy100
{
    class Framebuffer;

    namespace font
    {
        // Draw `text` with the built-in proportional bitmap font (Quaver), top-left at (x,y),
        // in color c. Handles '\n'. Returns the x just past the last glyph (PICO-8 print's
        // return value). Glyph metrics live in the generated font_data.h.
        int print(Framebuffer& fb, const char* text, int x, int y, u8 c);

        int line_height(); // pixels to advance y per text line
    } // namespace font
} // namespace lazy100
