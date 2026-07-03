#include "lazy100/editor/sprite_editor.hpp"

#include "lazy100/console/console.hpp"
#include "lazy100/editor/ui.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/icons.hpp"
#include "lazy100/video/palette.hpp"
#include "lazy100/video/sprites.hpp"

#include <cstdio>

namespace lazy100
{
    namespace
    {
        constexpr int kSprPx  = SpriteSheet::kSpriteSize;    // 16
        constexpr int kPerRow = SpriteSheet::kSpritesPerRow; // 16

        constexpr int kCanvasX = 5, kCanvasY = 39, kZoom = 11; // 16x16 -> 176x176

        constexpr int kPalX = 188, kPalY = 54; // swatch grid origin (below the sub tabs)

        constexpr int kNavX = 220, kNavY = 172, kNavSize = 64; // 256x256 sheet sampled below the header
        constexpr int kNavCell = kNavSize / kPerRow;           // 5

        // Palette sub tabs: curated console-style subsets of the 256-color table, plus the
        // full grid. Each subset lists reference RGB values that get mapped (once) to their
        // nearest default-palette index, so picking a swatch still yields a plain color index.
        struct RGB
        {
            u8 r, g, b;
        };

        constexpr RGB kGB[8] = {// classic DMG green ramp, then the Pocket grayscale ramp
                                {15, 56, 15},  {48, 98, 48},    {139, 172, 15},  {155, 188, 15},
                                {0, 0, 0},     {85, 85, 85},    {170, 170, 170}, {255, 255, 255}};

        // FC/NES 2C02 master palette (canonical 64 entries; row 0 darks .. row 3 pales).
        constexpr RGB kFC[64] = {
            {84, 84, 84},    {0, 30, 116},    {8, 16, 144},    {48, 0, 136},    {68, 0, 100},
            {92, 0, 48},     {84, 4, 0},      {60, 24, 0},     {32, 42, 0},     {8, 58, 0},
            {0, 64, 0},      {0, 60, 0},      {0, 50, 60},     {0, 0, 0},       {0, 0, 0},
            {0, 0, 0},       {152, 150, 152}, {8, 76, 196},    {48, 50, 236},   {92, 30, 228},
            {136, 20, 176},  {160, 20, 100},  {152, 34, 32},   {120, 60, 0},    {84, 90, 0},
            {40, 114, 0},    {8, 124, 0},     {0, 118, 40},    {0, 102, 120},   {0, 0, 0},
            {0, 0, 0},       {0, 0, 0},       {236, 238, 236}, {76, 154, 236},  {120, 124, 236},
            {176, 98, 236},  {228, 84, 236},  {236, 88, 180},  {236, 106, 100}, {212, 136, 32},
            {160, 170, 0},   {116, 196, 0},   {76, 208, 32},   {56, 204, 108},  {56, 180, 204},
            {60, 60, 60},    {0, 0, 0},       {0, 0, 0},       {236, 238, 236}, {168, 204, 236},
            {188, 188, 236}, {212, 178, 236}, {236, 174, 236}, {236, 174, 212}, {236, 180, 176},
            {228, 196, 144}, {204, 210, 120}, {180, 222, 120}, {168, 226, 144}, {152, 226, 180},
            {160, 214, 228}, {160, 162, 160}, {0, 0, 0},       {0, 0, 0}};

        enum PalTab
        {
            TabAll = 0,
            TabP8,
            TabGB,
            TabFC,
            TabCount
        };
        const char* kTabName[TabCount] = {"ALL", "P8", "GB", "FC"};

        // Nearest default-palette index for an RGB reference (computed once per subset).
        u8 nearest_index(const Palette& pal, RGB c)
        {
            int  best  = 0;
            long bestD = -1;
            for (int i = 0; i < static_cast<int>(kPaletteSize); ++i)
            {
                const Color32 p  = pal.default_at(static_cast<u32>(i));
                const long    dr = p.r - c.r, dg = p.g - c.g, db = p.b - c.b;
                const long    d  = dr * dr + dg * dg + db * db;
                if (bestD < 0 || d < bestD)
                {
                    bestD = d;
                    best  = i;
                }
            }
            return static_cast<u8>(best);
        }

        // Swatch layout per tab: index list + grid shape + swatch size.
        struct TabLayout
        {
            const u8* idx;   // nullptr = identity 0..255
            int       count;
            int       cols;
            int       sw, sh; // swatch cell size (wide cells fill the panel width)
        };

        TabLayout tab_layout(const Palette& pal, int tab)
        {
            static u8   gb[8];
            static u8   fc[64];
            static u8   p8[32];
            static bool baked = false;
            if (!baked)
            {
                for (int i = 0; i < 8; ++i)
                    gb[i] = nearest_index(pal, kGB[i]);
                for (int i = 0; i < 64; ++i)
                    fc[i] = nearest_index(pal, kFC[i]);
                for (int i = 0; i < 32; ++i)
                    p8[i] = static_cast<u8>(i); // curated block: 16 classic + 16 extended
                baked = true;
            }
            switch (tab)
            {
                case TabP8: return {p8, 32, 8, 16, 14};
                case TabGB: return {gb, 8, 4, 32, 24}; // row 1 DMG greens, row 2 Pocket grays
                case TabFC: return {fc, 64, 16, 8, 14};
                default: return {nullptr, 256, 16, 8, 6};
            }
        }
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
        const TabLayout lay = tab_layout(con.palette(), pal_tab_);
        if (m.pressed(Mouse::Left) && ui::hit(m, kPalX, kPalY, lay.cols * lay.sw,
                                              ((lay.count + lay.cols - 1) / lay.cols) * lay.sh))
        {
            const int cell = ((my - kPalY) / lay.sh) * lay.cols + (mx - kPalX) / lay.sw;
            if (cell >= 0 && cell < lay.count)
                color_ = lay.idx ? lay.idx[cell] : cell;
        }
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

        // ---- palette: sub tabs (full grid / console-style subsets), then swatches ----
        ui::panel(fb, 186, 36, 132, 120);
        {
            int tx = 190;
            for (int t = 0; t < TabCount; ++t)
            {
                const bool on = (t == pal_tab_);
                const int  w  = font::text_width(kTabName[t]) + 8;
                fb.rectfill(tx, 39, tx + w, 51, on ? ui::kBtnActive : ui::kBtn);
                draw::rect(fb, tx, 39, tx + w, 51, on ? ui::kBorderHi : ui::kBorder);
                font::print(fb, kTabName[t], tx + 4, 41, on ? ui::kText : ui::kDim);
                if (ui::hit(m, tx, 39, w + 1, 13) && m.pressed(Mouse::Left))
                    pal_tab_ = t;
                tx += w + 4;
            }
        }
        const TabLayout lay = tab_layout(con.palette(), pal_tab_);
        for (int i = 0; i < lay.count; ++i)
        {
            const u8  ci = lay.idx ? lay.idx[i] : static_cast<u8>(i);
            const int x0 = kPalX + (i % lay.cols) * lay.sw;
            const int y0 = kPalY + (i / lay.cols) * lay.sh;
            fb.rectfill(x0, y0, x0 + lay.sw - 1, y0 + lay.sh - 1, ci);
            if (ci == color_)
                draw::rect(fb, x0, y0, x0 + lay.sw - 1, y0 + lay.sh - 1, ui::kText);
        }

        // ---- navigator ----
        ui::titled_panel(fb, 186, 158, 132, 82, icon::TabSprite);
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
                        "palette tabs: ALL grid or the\n"
                        "P8 / GB / FC console sets\n"
                        "click navigator to pick sprite");
    }

    cursor::Type SpriteEditor::cursor(Console& con) const
    {
        return ui::hit(con.mouse(), kCanvasX, kCanvasY, kSprPx * kZoom, kSprPx * kZoom) ? cursor::Cross
                                                                                        : cursor::Arrow;
    }
} // namespace lazy100
