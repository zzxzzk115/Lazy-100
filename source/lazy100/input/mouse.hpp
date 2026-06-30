#pragma once

#include "lazy100/common/types.hpp"

namespace lazy100
{
    class Window;

    // Mouse mapped into the 320x240 framebuffer (via the shared letterbox transform), with
    // per-frame button edges. For the sprite/map editors.
    class Mouse
    {
    public:
        enum Button
        {
            Left = 0,
            Right,
            Middle,
            Count
        };

        void update(const Window& window); // call once per render frame

        int  x() const { return x_; } // framebuffer coords (may fall outside 0..319 / 0..239)
        int  y() const { return y_; }
        bool in_bounds() const;

        bool down(Button b) const;
        bool pressed(Button b) const;
        bool released(Button b) const;
        int  wheel() const { return wheel_; }

    private:
        int  x_ = 0, y_ = 0;
        bool down_[Count] = {};
        bool prev_[Count] = {};
        int  wheel_       = 0;
    };
} // namespace lazy100
