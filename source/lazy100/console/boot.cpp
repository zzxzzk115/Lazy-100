#include "lazy100/console/boot.hpp"

#include "lazy100/console/config.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

#include <algorithm>
#include <cstring>

namespace lazy100
{
    namespace
    {
        // Draw `text` centered horizontally at row y; returns nothing.
        void center(Framebuffer& fb, const char* text, int y, u8 c)
        {
            font::print(fb, text, (static_cast<int>(kScreenW) - font::text_width(text)) / 2, y, c);
        }
    } // namespace

    SfxPattern boot::jingle()
    {
        // A bright major arpeggio climbing two octaves to a held high C — the "power-on" chime.
        SfxPattern p;
        p.speed   = 8; // ~66 ms per step
        auto note = [&](int i, int pitch, int vol)
        { p.notes[i] = SfxNote {static_cast<u8>(pitch), static_cast<u8>(Wave::Pulse), static_cast<u8>(vol), 0}; };
        note(0, 24, 4);  // C4
        note(2, 28, 4);  // E4
        note(4, 31, 5);  // G4
        note(6, 36, 5);  // C5
        note(8, 40, 5);  // E5
        note(10, 43, 6); // G5
        note(12, 48, 6); // C6
        note(13, 48, 6); // (held sparkle)
        note(14, 48, 5);
        return p; // remaining steps default to vol 0 (silent)
    }

    void boot::draw(Framebuffer& fb, double t)
    {
        fb.cls(0);
        const int    W          = static_cast<int>(kScreenW);
        const int    H          = static_cast<int>(kScreenH);
        const int    lh         = font::line_height();
        const u8     rainbow[6] = {8, 9, 10, 11, 12, 14}; // red orange yellow green blue pink

        // Power-on warm-up: rainbow scanlines fill downward (0..0.5s), then a black gap opens
        // from the middle outward (0.5..1.0s), wiping the color away to reveal the logo.
        if (t < 1.05)
        {
            const int fill = std::min(H, static_cast<int>(t / 0.5 * H));
            for (int y = 0; y < fill; ++y)
                fb.rectfill(0, y, W - 1, y, rainbow[((y / 6) + static_cast<int>(t * 30)) % 6]);
            if (t > 0.5)
            {
                const int open = std::min(H / 2, static_cast<int>((t - 0.5) / 0.5 * (H / 2)));
                fb.rectfill(0, H / 2 - open, W - 1, H / 2 + open, 0);
            }
        }

        // Logo: "LAZY-100" typed out char-by-char, bold, drop-shadowed, per-letter rainbow.
        const double la = t - 0.7;
        if (la > 0.0)
        {
            const char* title = "LAZY-100";
            const int   len   = static_cast<int>(std::strlen(title));
            const int   shown = std::min(len, 1 + static_cast<int>(la / 0.07));
            int         tw    = 0;
            for (int i = 0; i < shown; ++i)
            {
                const char c[2] = {title[i], 0};
                tw += font::text_width(c);
            }
            int       x = (W - tw) / 2;
            const int y = H / 2 - lh;
            for (int i = 0; i < shown; ++i)
            {
                const char c[2]  = {title[i], 0};
                const u8   col   = rainbow[(i + static_cast<int>(t * 6)) % 6];
                font::print(fb, c, x + 1, y + 1, 1);   // shadow
                font::print(fb, c, x, y, col);         // face
                font::print(fb, c, x + 1, y, col);     // double-strike for a bold weight
                x += font::text_width(c);
            }
        }

        if (t > 1.1)
            center(fb, "fantasy console", H / 2 + 4, 6);
        if (t > 1.0)
            center(fb, "copyright Lazy_V 2017-present", H - lh - 3, 5);
        if (t > 1.9 && (static_cast<int>(t * 2) % 2) == 0)
            center(fb, "press any key", H - lh * 2 - 6, 6);
    }
} // namespace lazy100
