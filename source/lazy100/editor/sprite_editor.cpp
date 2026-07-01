#include "lazy100/editor/sprite_editor.hpp"

#include "lazy100/console/console.hpp"
#include "lazy100/editor/ui.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/icons.hpp"
#include "lazy100/video/sprites.hpp"

#include <cstdio>

namespace lazy100
{
    namespace
    {
        constexpr int kSprPx  = SpriteSheet::kSpriteSize;    // 16
        constexpr int kPerRow = SpriteSheet::kSpritesPerRow; // 16

        constexpr int kCanvasX = 5, kCanvasY = 39, kZoom = 11; // 16x16 -> 176x176

        constexpr int kSw = 6, kPalX = 192, kPalY = 42; // 256-color 16x16 grid -> 96x96

        constexpr int kNavX = 210, kNavY = 159, kNavSize = 80; // 256x256 sheet sampled below the header
        constexpr int kNavCell = kNavSize / kPerRow;           // 5
    } // namespace

    void SpriteEditor::update(Console& con)
    {
        SpriteSheet& sheet = con.sheet();
        const Mouse& m     = con.mouse();
        const int    mx = m.x(), my = m.y();
        const int    sx = (sprite_ % kPerRow) * kSprPx, sy = (sprite_ / kPerRow) * kSprPx;

        if (ui::hit(m, kCanvasX, kCanvasY, kSprPx * kZoom, kSprPx * kZoom))
        {
            const int px = (mx - kCanvasX) / kZoom;
            const int py = (my - kCanvasY) / kZoom;
            if (m.down(Mouse::Left))
                sheet.set(sx + px, sy + py, static_cast<u8>(color_));
            if (m.down(Mouse::Right))
                color_ = sheet.get(sx + px, sy + py);
        }
        if (m.pressed(Mouse::Left) && ui::hit(m, kPalX, kPalY, kPerRow * kSw, kPerRow * kSw))
            color_ = ((my - kPalY) / kSw) * kPerRow + (mx - kPalX) / kSw;
        if (m.pressed(Mouse::Left) && ui::hit(m, kNavX, kNavY, kNavSize, kNavSize))
            sprite_ = ((my - kNavY) / kNavCell) * kPerRow + (mx - kNavX) / kNavCell;
    }

    void SpriteEditor::draw(Console& con, Framebuffer& fb)
    {
        const SpriteSheet& sheet = con.sheet();
        const Mouse&       m     = con.mouse();
        const int          sx = (sprite_ % kPerRow) * kSprPx, sy = (sprite_ / kPerRow) * kSprPx;

        ui::clear(fb, EditorHost::kTabH);

        // ---- toolbar ----
        ui::panel(fb, 2, 18, 316, 16);
        if (ui::icon_button(fb, m, 6, 19, 14, 14, icon::Prev))
            sprite_ = (sprite_ + 255) % 256;
        if (ui::icon_button(fb, m, 22, 19, 14, 14, icon::Next))
            sprite_ = (sprite_ + 1) % 256;
        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "SPR %03d   COL %d", sprite_, color_);
        font::print(fb, hdr, 44, 20, ui::kText);

        // ---- canvas ----
        ui::panel(fb, 2, 36, 182, 182);
        for (int py = 0; py < kSprPx; ++py)
            for (int px = 0; px < kSprPx; ++px)
            {
                const u8  c  = sheet.get(sx + px, sy + py);
                const int x0 = kCanvasX + px * kZoom;
                const int y0 = kCanvasY + py * kZoom;
                fb.rectfill(x0, y0, x0 + kZoom - 1, y0 + kZoom - 1, c);
            }
        // Faint pixel grid every 4 cells for orientation.
        for (int g = 0; g <= kSprPx; g += 4)
        {
            draw::line(fb, kCanvasX + g * kZoom, kCanvasY, kCanvasX + g * kZoom, kCanvasY + kSprPx * kZoom, ui::kBorder);
            draw::line(fb, kCanvasX, kCanvasY + g * kZoom, kCanvasX + kSprPx * kZoom, kCanvasY + g * kZoom, ui::kBorder);
        }

        // ---- palette ----
        ui::panel(fb, 186, 36, 132, 108);
        for (int i = 0; i < 256; ++i)
        {
            const int x0 = kPalX + (i % kPerRow) * kSw;
            const int y0 = kPalY + (i / kPerRow) * kSw;
            fb.rectfill(x0, y0, x0 + kSw - 1, y0 + kSw - 1, static_cast<u8>(i));
        }
        {
            const int x0 = kPalX + (color_ % kPerRow) * kSw;
            const int y0 = kPalY + (color_ / kPerRow) * kSw;
            draw::rect(fb, x0 - 1, y0 - 1, x0 + kSw, y0 + kSw, ui::kText);
        }

        // ---- navigator ----
        ui::titled_panel(fb, 186, 146, 132, 94, icon::TabSprite);
        for (int y = 0; y < kNavSize; ++y)
            for (int x = 0; x < kNavSize; ++x)
                fb.pset(kNavX + x, kNavY + y,
                        sheet.get(x * SpriteSheet::kSize / kNavSize, y * SpriteSheet::kSize / kNavSize));
        const int bx = kNavX + (sprite_ % kPerRow) * kNavCell;
        const int by = kNavY + (sprite_ / kPerRow) * kNavCell;
        draw::rect(fb, bx - 1, by - 1, bx + kNavCell, by + kNavCell, ui::kBorderHi);
        draw::rect(fb, kNavX - 1, kNavY - 1, kNavX + kNavSize, kNavY + kNavSize, ui::kBorder);

        ui::help_button(fb, con, m, 300, 19, 1,
                        "SPRITE\n"
                        "L: draw   R: pick color\n"
                        "< > : prev / next sprite\n"
                        "click palette to pick color\n"
                        "click navigator to pick sprite");
    }

    cursor::Type SpriteEditor::cursor(Console& con) const
    {
        return ui::hit(con.mouse(), kCanvasX, kCanvasY, kSprPx * kZoom, kSprPx * kZoom) ? cursor::Cross
                                                                                        : cursor::Arrow;
    }
} // namespace lazy100
