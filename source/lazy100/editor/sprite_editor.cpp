#include "lazy100/editor/sprite_editor.hpp"

#include "lazy100/console/console.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/sprites.hpp"

#include <cstdio>

namespace lazy100
{
    namespace
    {
        constexpr int kCanvasX = 6, kCanvasY = 22, kZoom = 13; // 8x8 sprite magnified
        constexpr int kSw = 8, kPalX = 6, kPalY = 142;         // palette swatch strip
        constexpr int kInfoY = 162;
        constexpr int kNavX = 186, kNavY = 22; // full-sheet navigator (128x128 @ 1x)

        bool inside(int mx, int my, int x, int y, int w, int h)
        {
            return mx >= x && my >= y && mx < x + w && my < y + h;
        }
    } // namespace

    void SpriteEditor::update(Console& con)
    {
        SpriteSheet& sheet = con.sheet();
        const Mouse& m     = con.mouse();
        const int    mx = m.x(), my = m.y();
        const int    sx = (sprite_ % 16) * 8, sy = (sprite_ / 16) * 8;

        // Canvas: left draws, right eyedrops.
        if (inside(mx, my, kCanvasX, kCanvasY, 8 * kZoom, 8 * kZoom))
        {
            const int px = (mx - kCanvasX) / kZoom;
            const int py = (my - kCanvasY) / kZoom;
            if (m.down(Mouse::Left))
                sheet.set(sx + px, sy + py, static_cast<u8>(color_));
            if (m.down(Mouse::Right))
                color_ = sheet.get(sx + px, sy + py);
        }
        // Palette select.
        if (m.pressed(Mouse::Left) && inside(mx, my, kPalX, kPalY, 16 * kSw, 2 * kSw))
            color_ = ((my - kPalY) / kSw) * 16 + (mx - kPalX) / kSw;
        // Navigator: pick which sprite.
        if (m.pressed(Mouse::Left) && inside(mx, my, kNavX, kNavY, 128, 128))
            sprite_ = ((my - kNavY) / 8) * 16 + (mx - kNavX) / 8;
    }

    void SpriteEditor::draw(Console& con, Framebuffer& fb)
    {
        const SpriteSheet& sheet = con.sheet();
        const int          sx = (sprite_ % 16) * 8, sy = (sprite_ / 16) * 8;

        // Magnified canvas of the current sprite.
        for (int py = 0; py < 8; ++py)
            for (int px = 0; px < 8; ++px)
            {
                const u8  c  = sheet.get(sx + px, sy + py);
                const int x0 = kCanvasX + px * kZoom;
                const int y0 = kCanvasY + py * kZoom;
                fb.rectfill(x0, y0, x0 + kZoom - 1, y0 + kZoom - 1, c);
            }
        draw::rect(fb, kCanvasX - 1, kCanvasY - 1, kCanvasX + 8 * kZoom, kCanvasY + 8 * kZoom, 6);

        // Palette strip (32 swatches, 2 rows).
        for (int i = 0; i < 32; ++i)
        {
            const int x0 = kPalX + (i % 16) * kSw;
            const int y0 = kPalY + (i / 16) * kSw;
            fb.rectfill(x0, y0, x0 + kSw - 1, y0 + kSw - 1, static_cast<u8>(i));
        }
        {
            const int x0 = kPalX + (color_ % 16) * kSw;
            const int y0 = kPalY + (color_ / 16) * kSw;
            draw::rect(fb, x0 - 1, y0 - 1, x0 + kSw, y0 + kSw, 7); // selected color
        }

        // Navigator: the whole sheet at 1x with the current sprite boxed.
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
                fb.pset(kNavX + x, kNavY + y, sheet.get(x, y));
        draw::rect(fb, kNavX + sx - 1, kNavY + sy - 1, kNavX + sx + 8, kNavY + sy + 8, 7);

        char buf[48];
        std::snprintf(buf, sizeof(buf), "spr %d  col %d", sprite_, color_);
        font::print(fb, buf, kPalX, kInfoY, 7);
    }
} // namespace lazy100
