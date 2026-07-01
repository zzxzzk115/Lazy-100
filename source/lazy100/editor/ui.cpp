#include "lazy100/editor/ui.hpp"

#include "lazy100/console/config.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/icons.hpp"

#include <string>
#include <vector>

namespace lazy100::ui
{
    void clear(Framebuffer& fb, int tab_h)
    {
        fb.rectfill(0, tab_h, static_cast<int>(kScreenW) - 1, static_cast<int>(kScreenH) - 1, kBg);
    }

    void panel(Framebuffer& fb, int x, int y, int w, int h, u8 fill, u8 border)
    {
        fb.rectfill(x, y, x + w - 1, y + h - 1, fill);
        draw::rect(fb, x, y, x + w - 1, y + h - 1, border);
    }

    int titled_panel(Framebuffer& fb, int x, int y, int w, int h, icon::Id ic)
    {
        constexpr int kHdrH = 10;
        panel(fb, x, y, w, h, kPanel, kBorder);
        fb.rectfill(x + 1, y + 1, x + w - 2, y + kHdrH, kHeader);
        icon::draw(fb, ic, x + 2, y + 2, kText);
        draw::line(fb, x, y + kHdrH + 1, x + w - 1, y + kHdrH + 1, kBorder);
        return y + kHdrH + 2; // content start
    }

    bool icon_button(Framebuffer& fb, const Mouse& m, int x, int y, int w, int h, icon::Id ic, bool active)
    {
        const bool over = hit(m, x, y, w, h);
        const u8   bg   = active ? kBtnActive : (over ? kBtnHover : kBtn);
        fb.rectfill(x, y, x + w - 1, y + h - 1, bg);
        draw::rect(fb, x, y, x + w - 1, y + h - 1, active ? kBorderHi : kBorder);
        const u8 ink = active ? kBg : kText; // dark icon on the bright active fill
        icon::draw(fb, ic, x + (w - icon::kSize) / 2, y + (h - icon::kSize) / 2, ink);
        return over && m.pressed(Mouse::Left);
    }

    void divider(Framebuffer& fb, int x, int y, int w, u8 c) { draw::line(fb, x, y, x + w - 1, y, c); }
    void vdivider(Framebuffer& fb, int x, int y, int h, u8 c) { draw::line(fb, x, y, x, y + h - 1, c); }

    bool hit(const Mouse& m, int x, int y, int w, int h)
    {
        return m.x() >= x && m.y() >= y && m.x() < x + w && m.y() < y + h;
    }

    void tooltip(Framebuffer& fb, int ax, int ay, const char* text)
    {
        // Split into lines and measure.
        std::vector<std::string> lines;
        std::string              cur;
        for (const char* p = text; *p; ++p)
        {
            if (*p == '\n')
            {
                lines.push_back(cur);
                cur.clear();
            }
            else
                cur += *p;
        }
        lines.push_back(cur);

        const int lh = font::line_height();
        int       wmax = 0;
        for (const auto& s : lines)
            wmax = std::max(wmax, font::text_width(s.c_str()));

        const int W  = static_cast<int>(kScreenW), H = static_cast<int>(kScreenH);
        const int pw = wmax + 8;
        const int ph = static_cast<int>(lines.size()) * lh + 4;
        int       px = ax + 14; // to the lower-right of the anchor by default
        int       py = ay + 14;
        if (px + pw > W)
            px = W - pw - 1;
        if (py + ph > H)
            py = ay - ph - 2; // flip above when near the bottom
        if (px < 0)
            px = 0;
        if (py < 0)
            py = 0;

        panel(fb, px, py, pw, ph, kPanel, kBorderHi);
        for (size_t i = 0; i < lines.size(); ++i)
            font::print(fb, lines[i].c_str(), px + 4, py + 2 + static_cast<int>(i) * lh, kText);
    }

    void help_button(Framebuffer& fb, Console& con, const Mouse& m, int x, int y, int id, const char* text)
    {
        constexpr int kSz = 12;
        icon_button(fb, m, x, y, kSz, kSz, icon::Help);
        if (con.tooltip_active(id, hit(m, x, y, kSz, kSz)))
            tooltip(fb, x, y, text);
    }
} // namespace lazy100::ui
