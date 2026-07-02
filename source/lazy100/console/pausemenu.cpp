#include "lazy100/console/pausemenu.hpp"

#include "lazy100/console/config.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

#include <algorithm>

namespace lazy100
{
    int PauseMenu::update(Keyboard& kb)
    {
        if (!open_ || items_.empty())
            return -1;
        const int n = static_cast<int>(items_.size());
        if (kb.repeat(Keyboard::Up))
            sel_ = (sel_ + n - 1) % n;
        if (kb.repeat(Keyboard::Down))
            sel_ = (sel_ + 1) % n;
        if (kb.pressed(Keyboard::Right) || kb.pressed(Keyboard::Return))
        {
            open_ = false;
            return sel_;
        }
        return -1;
    }

    void PauseMenu::draw(Framebuffer& fb) const
    {
        if (!open_ || items_.empty())
            return;
        const int W    = static_cast<int>(kScreenW);
        const int H    = static_cast<int>(kScreenH);
        const int lh   = font::line_height();
        const int rowH = lh + 3;
        const int n    = static_cast<int>(items_.size());

        int textW = 0;
        for (const std::string& item : items_)
            textW = std::max(textW, font::text_width(item.c_str()));
        const int boxW = textW + 34;
        const int boxH = n * rowH + 12;
        const int x0   = (W - boxW) / 2;
        const int y0   = (H - boxH) / 2;

        fb.rectfill(x0 + 3, y0 + 3, x0 + boxW + 3, y0 + boxH + 3, 0); // drop shadow
        fb.rectfill(x0, y0, x0 + boxW, y0 + boxH, 1);
        draw::rect(fb, x0, y0, x0 + boxW, y0 + boxH, 7);
        draw::rect(fb, x0 + 2, y0 + 2, x0 + boxW - 2, y0 + boxH - 2, 13);

        for (int i = 0; i < n; ++i)
        {
            const int y = y0 + 7 + i * rowH;
            if (i == sel_)
            {
                fb.rectfill(x0 + 4, y - 1, x0 + boxW - 4, y + lh - 1, 2);
                font::print(fb, ">", x0 + 8, y, 10);
            }
            font::print(fb, items_[i].c_str(), x0 + 18, y, i == sel_ ? 7 : 6);
        }
    }
} // namespace lazy100
