#include "lazy100/video/font.hpp"

#include "lazy100/video/font_data.h" // kFontHeight/kFontSpace/kFontGap/kFontWidth/kFontBits
#include "lazy100/video/framebuffer.hpp"

namespace lazy100::font
{
    int line_height() { return kFontHeight + 1; }

    int print(Framebuffer& fb, const char* text, int x, int y, u8 c)
    {
        int cx = x;
        for (const char* p = text; *p; ++p)
        {
            const char ch = *p;
            if (ch == '\n')
            {
                cx = x;
                y += kFontHeight + 1;
                continue;
            }
            if (ch < 32 || ch > 126)
            {
                cx += kFontSpace;
                continue;
            }
            const int idx = ch - 32;
            const int w   = kFontWidth[idx];
            if (w == 0) // space and other ink-less glyphs
            {
                cx += kFontSpace;
                continue;
            }
            const unsigned char* glyph = kFontBits[idx];
            for (int row = 0; row < kFontHeight; ++row)
            {
                const unsigned bits = glyph[row];
                for (int col = 0; col < w; ++col)
                    if (bits & (1u << col)) // bit col = column col (LSB = leftmost)
                        fb.pset(cx + col, y + row, c);
            }
            cx += w + kFontGap;
        }
        return cx;
    }
} // namespace lazy100::font
