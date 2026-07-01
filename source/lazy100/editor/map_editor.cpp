#include "lazy100/editor/map_editor.hpp"

#include "lazy100/console/console.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/sprites.hpp"
#include "lazy100/world/map.hpp"

#include <algorithm>
#include <cstdio>

namespace lazy100
{
    namespace
    {
        constexpr int kTile = SpriteSheet::kSpriteSize; // 16
        constexpr int kPerRow = SpriteSheet::kSpritesPerRow; // 16

        // Scrolling map viewport (native 16px tiles).
        constexpr int kViewX = 0, kViewY = 18, kCols = 20, kRows = 7; // 320x112

        constexpr int kStatusY = kViewY + kRows * kTile + 2; // 132

        // Sprite picker: the whole 256x256 sheet sampled down, plus a magnified preview.
        constexpr int kPickX = 4, kPickY = 142, kPickSize = 96;   // 96x96
        constexpr int kPickCell = kPickSize / kPerRow;            // 6 px per sprite
        constexpr int kPrevX = 120, kPrevY = 146, kPrevZoom = 5;  // 16x16 -> 80x80

        bool inside(int mx, int my, int x, int y, int w, int h)
        {
            return mx >= x && my >= y && mx < x + w && my < y + h;
        }
    } // namespace

    void MapEditor::update(Console& con)
    {
        Map&            map = con.map();
        const Mouse&    m   = con.mouse();
        const Keyboard& kb  = con.keyboard();
        const int       mx = m.x(), my = m.y();

        // Pan with the arrow keys (auto-repeat while held), clamped to the map bounds.
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

        // Paint into the viewport: left stamps the selected tile, right erases.
        if (inside(mx, my, kViewX, kViewY, kCols * kTile, kRows * kTile))
        {
            const int tx = cam_x_ + (mx - kViewX) / kTile;
            const int ty = cam_y_ + (my - kViewY) / kTile;
            if (m.down(Mouse::Left))
                map.set(tx, ty, static_cast<u8>(tile_));
            if (m.down(Mouse::Right))
                map.set(tx, ty, 0);
        }
        // Picker: choose which sprite to paint.
        if (m.pressed(Mouse::Left) && inside(mx, my, kPickX, kPickY, kPickSize, kPickSize))
        {
            const int col = (mx - kPickX) / kPickCell;
            const int row = (my - kPickY) / kPickCell;
            tile_         = row * kPerRow + col;
        }
    }

    void MapEditor::draw(Console& con, Framebuffer& fb)
    {
        const Map&         map   = con.map();
        const SpriteSheet& sheet = con.sheet();
        u8*                dpal  = con.draw_pal();
        bool*              trans = con.transparent();

        // Viewport: dark backdrop, then each non-empty tile as its sprite.
        fb.rectfill(kViewX, kViewY, kViewX + kCols * kTile - 1, kViewY + kRows * kTile - 1, 1);
        for (int row = 0; row < kRows; ++row)
            for (int col = 0; col < kCols; ++col)
            {
                const u8 n = map.get(cam_x_ + col, cam_y_ + row);
                if (n != 0)
                    sheet.spr(fb, n, kViewX + col * kTile, kViewY + row * kTile, 1, 1, false, false, dpal, trans);
            }
        draw::rect(fb, kViewX, kViewY - 1, kViewX + kCols * kTile - 1, kViewY + kRows * kTile, 5);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "cam %d,%d  tile %d", cam_x_, cam_y_, tile_);
        font::print(fb, buf, kViewX + 2, kStatusY, 7);

        // Picker: full sheet sampled to kPickSize, current tile boxed.
        for (int y = 0; y < kPickSize; ++y)
            for (int x = 0; x < kPickSize; ++x)
                fb.pset(kPickX + x, kPickY + y,
                        sheet.get(x * SpriteSheet::kSize / kPickSize, y * SpriteSheet::kSize / kPickSize));
        const int bx = kPickX + (tile_ % kPerRow) * kPickCell;
        const int by = kPickY + (tile_ / kPerRow) * kPickCell;
        draw::rect(fb, bx - 1, by - 1, bx + kPickCell, by + kPickCell, 7);
        draw::rect(fb, kPickX - 1, kPickY - 1, kPickX + kPickSize, kPickY + kPickSize, 6);

        // Magnified preview of the tile being painted.
        const int sx = (tile_ % kPerRow) * kTile, sy = (tile_ / kPerRow) * kTile;
        for (int py = 0; py < kTile; ++py)
            for (int px = 0; px < kTile; ++px)
            {
                const u8  c  = sheet.get(sx + px, sy + py);
                const int x0 = kPrevX + px * kPrevZoom;
                const int y0 = kPrevY + py * kPrevZoom;
                fb.rectfill(x0, y0, x0 + kPrevZoom - 1, y0 + kPrevZoom - 1, c);
            }
        draw::rect(fb, kPrevX - 1, kPrevY - 1, kPrevX + kTile * kPrevZoom, kPrevY + kTile * kPrevZoom, 6);
        font::print(fb, "arrows: pan", kPrevX + kTile * kPrevZoom + 8, kPrevY + 4, 6);
        font::print(fb, "L: paint", kPrevX + kTile * kPrevZoom + 8, kPrevY + 16, 6);
        font::print(fb, "R: erase", kPrevX + kTile * kPrevZoom + 8, kPrevY + 28, 6);
    }
} // namespace lazy100
