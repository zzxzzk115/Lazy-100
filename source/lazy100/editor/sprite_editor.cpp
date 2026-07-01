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
        constexpr int kSprPx = SpriteSheet::kSpriteSize;     // 16
        constexpr int kPerRow = SpriteSheet::kSpritesPerRow; // 16

        // Left: the current 16x16 sprite magnified. Right column: a 16x16 palette grid on top of
        // a scaled-down navigator of the whole 256x256 sheet.
        constexpr int kCanvasX = 4, kCanvasY = 18, kZoom = 11; // 16x16 -> 176x176

        constexpr int kSw = 6, kPalX = 186, kPalY = 18; // 256 colors, 16x16 grid -> 96x96

        constexpr int kNavX = 186, kNavY = 122, kNavSize = 96; // 256x256 sheet sampled to 96x96
        constexpr int kNavCell = kNavSize / kPerRow;           // 6 px per sprite in the navigator

        constexpr int kInfoY = 122 + kNavSize + 4;

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
        const int    sx = (sprite_ % kPerRow) * kSprPx, sy = (sprite_ / kPerRow) * kSprPx;

        // Canvas: left draws, right eyedrops.
        if (inside(mx, my, kCanvasX, kCanvasY, kSprPx * kZoom, kSprPx * kZoom))
        {
            const int px = (mx - kCanvasX) / kZoom;
            const int py = (my - kCanvasY) / kZoom;
            if (m.down(Mouse::Left))
                sheet.set(sx + px, sy + py, static_cast<u8>(color_));
            if (m.down(Mouse::Right))
                color_ = sheet.get(sx + px, sy + py);
        }
        // Palette select (256 colors, 16x16 grid).
        if (m.pressed(Mouse::Left) && inside(mx, my, kPalX, kPalY, kPerRow * kSw, kPerRow * kSw))
            color_ = ((my - kPalY) / kSw) * kPerRow + (mx - kPalX) / kSw;
        // Navigator: pick which sprite.
        if (m.pressed(Mouse::Left) && inside(mx, my, kNavX, kNavY, kNavSize, kNavSize))
        {
            const int col = (mx - kNavX) / kNavCell;
            const int row = (my - kNavY) / kNavCell;
            sprite_       = row * kPerRow + col;
        }
    }

    void SpriteEditor::draw(Console& con, Framebuffer& fb)
    {
        const SpriteSheet& sheet = con.sheet();
        const int          sx = (sprite_ % kPerRow) * kSprPx, sy = (sprite_ / kPerRow) * kSprPx;

        // Magnified canvas of the current sprite.
        for (int py = 0; py < kSprPx; ++py)
            for (int px = 0; px < kSprPx; ++px)
            {
                const u8  c  = sheet.get(sx + px, sy + py);
                const int x0 = kCanvasX + px * kZoom;
                const int y0 = kCanvasY + py * kZoom;
                fb.rectfill(x0, y0, x0 + kZoom - 1, y0 + kZoom - 1, c);
            }
        draw::rect(fb, kCanvasX - 1, kCanvasY - 1, kCanvasX + kSprPx * kZoom, kCanvasY + kSprPx * kZoom, 6);

        // Palette grid (256 swatches, 16x16).
        for (int i = 0; i < 256; ++i)
        {
            const int x0 = kPalX + (i % kPerRow) * kSw;
            const int y0 = kPalY + (i / kPerRow) * kSw;
            fb.rectfill(x0, y0, x0 + kSw - 1, y0 + kSw - 1, static_cast<u8>(i));
        }
        {
            const int x0 = kPalX + (color_ % kPerRow) * kSw;
            const int y0 = kPalY + (color_ / kPerRow) * kSw;
            draw::rect(fb, x0 - 1, y0 - 1, x0 + kSw, y0 + kSw, 7); // selected color
        }

        // Navigator: the whole 256x256 sheet sampled down to kNavSize, current sprite boxed.
        for (int y = 0; y < kNavSize; ++y)
            for (int x = 0; x < kNavSize; ++x)
                fb.pset(kNavX + x, kNavY + y, sheet.get(x * SpriteSheet::kSize / kNavSize, y * SpriteSheet::kSize / kNavSize));
        const int bx = kNavX + (sprite_ % kPerRow) * kNavCell;
        const int by = kNavY + (sprite_ / kPerRow) * kNavCell;
        draw::rect(fb, bx - 1, by - 1, bx + kNavCell, by + kNavCell, 7);

        char buf[48];
        std::snprintf(buf, sizeof(buf), "spr %d  col %d", sprite_, color_);
        font::print(fb, buf, kCanvasX, kInfoY, 7);
    }
} // namespace lazy100
