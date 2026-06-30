#include "lazy100/input/mouse.hpp"

#include "lazy100/common/layout.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/console/window.hpp"

namespace lazy100
{
    void Mouse::update(const Window& window)
    {
        const RawInput& raw = window.raw_input();

        u32 ww = 0, wh = 0;
        window.drawable_size(ww, wh);
        if (ww > 0 && wh > 0)
        {
            const layout::Rect lb = layout::letterbox(ww, wh);
            x_ = lb.w > 0 ? (raw.mouse_x - lb.x) * static_cast<int>(kScreenW) / static_cast<int>(lb.w) : 0;
            y_ = lb.h > 0 ? (raw.mouse_y - lb.y) * static_cast<int>(kScreenH) / static_cast<int>(lb.h) : 0;
        }

        for (int b = 0; b < Count; ++b)
        {
            prev_[b] = down_[b];
            down_[b] = (raw.mouse_buttons & (1u << b)) != 0;
        }
        wheel_ = raw.wheel;
    }

    bool Mouse::in_bounds() const
    {
        return x_ >= 0 && y_ >= 0 && x_ < static_cast<int>(kScreenW) && y_ < static_cast<int>(kScreenH);
    }

    bool Mouse::down(Button b) const { return b >= 0 && b < Count && down_[b]; }
    bool Mouse::pressed(Button b) const { return b >= 0 && b < Count && down_[b] && !prev_[b]; }
    bool Mouse::released(Button b) const { return b >= 0 && b < Count && !down_[b] && prev_[b]; }
} // namespace lazy100
