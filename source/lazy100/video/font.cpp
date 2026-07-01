#include "lazy100/video/font.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/vfs/vfs.hpp"
#include "lazy100/video/framebuffer.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace lazy100::font
{
    namespace
    {
        constexpr int kPixel = 10; // Fusion Pixel native size

        struct Glyph
        {
            int             w = 0, h = 0; // bitmap dimensions
            int             xoff = 0;     // x offset from the pen
            int             yoff = 0;     // y offset from the baseline (negative = above)
            int             advance = 0;  // pen advance in px
            std::vector<u8> bits;         // w*h, 1 = lit (already thresholded)
        };

        std::vector<unsigned char>     g_ttf;
        stbtt_fontinfo                 g_info {};
        bool                           g_ready  = false;
        float                          g_scale  = 0.0f;
        int                            g_lineH  = kPixel + 2;
        int                            g_ascentPx = kPixel;
        std::unordered_map<int, Glyph> g_cache;

        const Glyph& glyph_for(int cp)
        {
            if (auto it = g_cache.find(cp); it != g_cache.end())
                return it->second;

            Glyph gl;
            int   adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&g_info, cp, &adv, &lsb);
            gl.advance = static_cast<int>(std::lround(adv * g_scale));

            int            w = 0, h = 0, xo = 0, yo = 0;
            unsigned char* bmp = stbtt_GetCodepointBitmap(&g_info, g_scale, g_scale, cp, &w, &h, &xo, &yo);
            gl.w = w;
            gl.h = h;
            gl.xoff = xo;
            gl.yoff = yo;
            gl.bits.assign(static_cast<size_t>(w) * h, 0);
            for (int i = 0; i < w * h; ++i)
                gl.bits[i] = bmp && bmp[i] > 127 ? 1 : 0; // grid-aligned pixel font -> clean threshold
            if (bmp)
                stbtt_FreeBitmap(bmp, nullptr);

            return g_cache.emplace(cp, std::move(gl)).first->second;
        }

        // Decode one UTF-8 codepoint, advancing p past it.
        int next_cp(const char*& p)
        {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c < 0x80)
            {
                ++p;
                return c;
            }
            int cp = 0, n = 0;
            if ((c & 0xE0) == 0xC0)
            {
                cp = c & 0x1F;
                n  = 1;
            }
            else if ((c & 0xF0) == 0xE0)
            {
                cp = c & 0x0F;
                n  = 2;
            }
            else if ((c & 0xF8) == 0xF0)
            {
                cp = c & 0x07;
                n  = 3;
            }
            else
            {
                ++p;
                return 0xFFFD;
            }
            ++p;
            for (int i = 0; i < n; ++i, ++p)
            {
                if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80)
                    return 0xFFFD;
                cp = (cp << 6) | (static_cast<unsigned char>(*p) & 0x3F);
            }
            return cp;
        }
    } // namespace

    bool init()
    {
        // Built-in font, linked into the binary and read from the in-memory VFS.
        constexpr const char* kPath = "fonts/fusion-pixel-10px-monospaced-zh_hans.ttf";
        auto                  bytes = vfs::read_builtin(kPath);
        if (!bytes || bytes->empty())
        {
            LZ_ERROR("font: built-in asset '%s' not available", kPath);
            return false;
        }
        g_ttf.resize(bytes->size());
        std::memcpy(g_ttf.data(), bytes->data(), bytes->size());
        if (!stbtt_InitFont(&g_info, g_ttf.data(), stbtt_GetFontOffsetForIndex(g_ttf.data(), 0)))
        {
            LZ_ERROR("font: failed to parse built-in TTF");
            return false;
        }

        // The font is designed on a px grid: map 1 em -> kPixel so glyph cells land on
        // integer pixels (matches GDI+ "kPixel px" rendering of the same TTF).
        g_scale = stbtt_ScaleForMappingEmToPixels(&g_info, static_cast<float>(kPixel));
        int ascent = 0, descent = 0, linegap = 0;
        stbtt_GetFontVMetrics(&g_info, &ascent, &descent, &linegap);
        g_ascentPx = static_cast<int>(std::lround(ascent * g_scale));
        g_lineH    = static_cast<int>(std::lround((ascent - descent + linegap) * g_scale));
        if (g_lineH < kPixel)
            g_lineH = kPixel + 2;
        g_ready = true;
        LZ_INFO("font: loaded built-in TTF (%zu bytes), lineH=%d", g_ttf.size(), g_lineH);
        return true;
    }

    void shutdown()
    {
        g_cache.clear();
        g_ttf.clear();
        g_ready = false;
    }

    int line_height() { return g_lineH; }

    int text_width(const char* text)
    {
        if (!g_ready)
            return 0;
        int w = 0;
        for (const char* p = text; *p;)
        {
            if (*p == '\n')
            {
                ++p;
                continue;
            }
            const int cp = next_cp(p);
            w += glyph_for(cp).advance;
        }
        return w;
    }

    int print(Framebuffer& fb, const char* text, int x, int y, u8 c)
    {
        if (!g_ready)
            return x;
        int cx       = x;
        int baseline = y + g_ascentPx;
        for (const char* p = text; *p;)
        {
            if (*p == '\n')
            {
                ++p;
                cx       = x;
                y       += g_lineH;
                baseline = y + g_ascentPx;
                continue;
            }
            const int    cp = next_cp(p);
            const Glyph& gl = glyph_for(cp);
            for (int j = 0; j < gl.h; ++j)
            {
                const u8* row = gl.bits.data() + static_cast<size_t>(j) * gl.w;
                for (int i = 0; i < gl.w; ++i)
                    if (row[i])
                        fb.pset(cx + gl.xoff + i, baseline + gl.yoff + j, c);
            }
            cx += gl.advance;
        }
        return cx;
    }
} // namespace lazy100::font
