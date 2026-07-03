#include "lazy100/editor/map_editor.hpp"

#include "lazy100/console/console.hpp"
#include "lazy100/editor/ui.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/icons.hpp"
#include "lazy100/video/sprites.hpp"
#include "lazy100/world/map.hpp"

#include <algorithm>
#include <cstdio>

namespace lazy100
{
    namespace
    {
        constexpr int kTile   = SpriteSheet::kSpriteSize;    // 16
        constexpr int kPerRow = SpriteSheet::kSpritesPerRow; // 16

        // Left: a bordered map viewport. Right column: a tools/preview panel over a sprite picker.
        constexpr int kViewX = 6, kViewY = 32, kCols = 12, kRows = 11; // 192x176

        constexpr int kPrevZoom = 3;                              // 16x16 -> 48x48 tile preview
        constexpr int kPickX = 226, kPickY = 129, kPickSize = 80; // sheet sampled to 80x80
        constexpr int kPickCell = kPickSize / kPerRow;            // 5
    } // namespace

    void MapEditor::update(Console& con)
    {
        Map&            map = con.map();
        const Mouse&    m   = con.mouse();
        const Keyboard& kb  = con.keyboard();
        const int       mx = m.x(), my = m.y();

        if (kb.repeat(Keyboard::Left))
            --cam_x_;
        if (kb.repeat(Keyboard::Right))
            ++cam_x_;
        if (kb.repeat(Keyboard::Up))
            --cam_y_;
        if (kb.repeat(Keyboard::Down))
            ++cam_y_;
        cam_x_ = std::clamp(cam_x_, 0, Map::kW - kCols);
        cam_y_ = std::clamp(cam_y_, 0, Map::kH - kRows);

        if (ui::hit(m, kViewX, kViewY, kCols * kTile, kRows * kTile))
        {
            const int tx = cam_x_ + (mx - kViewX) / kTile;
            const int ty = cam_y_ + (my - kViewY) / kTile;
            if (m.down(Mouse::Left))
                map.set(tx, ty, static_cast<u8>(tile_));
            if (m.down(Mouse::Right))
                map.set(tx, ty, Map::kEmpty);
        }
        if (m.pressed(Mouse::Left) && ui::hit(m, kPickX, kPickY, kPickSize, kPickSize))
            tile_ = ((my - kPickY) / kPickCell) * kPerRow + (mx - kPickX) / kPickCell;
    }

    void MapEditor::draw(Console& con, Framebuffer& fb)
    {
        const Map&         map   = con.map();
        const SpriteSheet& sheet = con.sheet();
        u8*                dpal  = con.draw_pal();
        bool*              trans = con.transparent();
        const Mouse&       m     = con.mouse();

        ui::clear(fb, EditorHost::kTabH);

        // ---- map viewport ----
        ui::titled_panel(fb, 2, 18, 210, 200, icon::TabMap);
        fb.rectfill(kViewX, kViewY, kViewX + kCols * kTile - 1, kViewY + kRows * kTile - 1, ui::kPanelLo);
        for (int row = 0; row < kRows; ++row)
            for (int col = 0; col < kCols; ++col)
            {
                const u8 n = map.get(cam_x_ + col, cam_y_ + row);
                if (n == Map::kEmpty)
                    continue;
                const int cx = kViewX + col * kTile, cy = kViewY + row * kTile;
                // Tint placed cells so painting is visible even when the sprite art is still blank.
                fb.rectfill(cx, cy, cx + kTile - 1, cy + kTile - 1, 1);
                sheet.spr(fb, n, cx, cy, 1, 1, false, false, dpal, trans);
            }
        // Tile grid overlay (top-bar GRID toggle).
        if (grid_)
        {
            for (int c = 0; c <= kCols; ++c)
                draw::line(fb, kViewX + c * kTile, kViewY, kViewX + c * kTile, kViewY + kRows * kTile, 5);
            for (int r = 0; r <= kRows; ++r)
                draw::line(fb, kViewX, kViewY + r * kTile, kViewX + kCols * kTile, kViewY + r * kTile, 5);
        }
        draw::rect(fb, kViewX - 1, kViewY - 1, kViewX + kCols * kTile, kViewY + kRows * kTile, ui::kBorder);

        // ---- tools / tile preview ----
        ui::titled_panel(fb, 214, 18, 104, 94, icon::Pencil);
        if (ui::icon_button(fb, m, 220, 32, 14, 14, icon::Prev))
            tile_ = (tile_ + 255) % 256;
        if (ui::icon_button(fb, m, 236, 32, 14, 14, icon::Next))
            tile_ = (tile_ + 1) % 256;
        char tn[12];
        std::snprintf(tn, sizeof(tn), "%03d", tile_);
        font::print(fb, tn, 256, 35, ui::kText);

        const int sx = (tile_ % kPerRow) * kTile, sy = (tile_ / kPerRow) * kTile;
        const int pvx = 214 + (104 - kTile * kPrevZoom) / 2, pvy = 56; // centered 48x48 preview
        fb.rectfill(pvx, pvy, pvx + kTile * kPrevZoom - 1, pvy + kTile * kPrevZoom - 1, ui::kPanelLo);
        for (int py = 0; py < kTile; ++py)
            for (int px = 0; px < kTile; ++px)
            {
                const u8 c = sheet.get(sx + px, sy + py);
                if (c != 0)
                    fb.rectfill(pvx + px * kPrevZoom, pvy + py * kPrevZoom, pvx + px * kPrevZoom + kPrevZoom - 1,
                                pvy + py * kPrevZoom + kPrevZoom - 1, c);
            }
        draw::rect(fb, pvx - 1, pvy - 1, pvx + kTile * kPrevZoom, pvy + kTile * kPrevZoom, ui::kBorder);

        // ---- sprite picker ----
        ui::titled_panel(fb, 214, 116, 104, 102, icon::TabSprite);
        for (int y = 0; y < kPickSize; ++y)
            for (int x = 0; x < kPickSize; ++x)
                fb.pset(kPickX + x, kPickY + y,
                        sheet.get(x * SpriteSheet::kSize / kPickSize, y * SpriteSheet::kSize / kPickSize));
        const int bx = kPickX + (tile_ % kPerRow) * kPickCell;
        const int by = kPickY + (tile_ / kPerRow) * kPickCell;
        draw::rect(fb, bx - 1, by - 1, bx + kPickCell, by + kPickCell, ui::kBorderHi);
        draw::rect(fb, kPickX - 1, kPickY - 1, kPickX + kPickSize, kPickY + kPickSize, ui::kBorder);

        // ---- bottom status bar: hints + camera ----
        char cam[16];
        std::snprintf(cam, sizeof(cam), "cam %d,%d", cam_x_, cam_y_);
        font::print(fb, "L paint   R erase   arrows: pan", 6, 226, ui::kDim);
        font::print(fb, cam, 214, 226, ui::kDim);

        ui::help_button(fb, con, m, 302, 20, 2,
                        "MAP\n"
                        "L: paint tile   R: erase\n"
                        "arrows: pan the view\n"
                        "< > : prev / next tile\n"
                        "click the sheet to pick a tile");
    }

    void MapEditor::draw_tools(Console& con, Framebuffer& fb)
    {
        // Top-bar GRID toggle, lit while the overlay is on.
        const Mouse& m = con.mouse();
        const int    w = font::text_width("GRID") + 8;
        fb.rectfill(4, 1, 4 + w, EditorHost::kTabH - 2, grid_ ? ui::kBtnActive : ui::kBtn);
        font::print(fb, "GRID", 8, 3, grid_ ? ui::kText : ui::kDim);
        if (ui::hit(m, 4, 1, w + 1, EditorHost::kTabH - 2) && m.pressed(Mouse::Left))
            grid_ = !grid_;
    }

    cursor::Type MapEditor::cursor(Console& con) const
    {
        return ui::hit(con.mouse(), kViewX, kViewY, kCols * kTile, kRows * kTile) ? cursor::Pencil : cursor::Arrow;
    }
} // namespace lazy100
