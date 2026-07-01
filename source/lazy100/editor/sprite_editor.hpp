#pragma once

#include "lazy100/editor/editor.hpp"

namespace lazy100
{
    // Paint 8x8 sprites into the console's sprite sheet: a magnified canvas (left mouse draws,
    // right mouse picks color), a 32-color palette strip, and a navigator showing the whole
    // 128x128 sheet. Edits go straight into Console::sheet(), so they persist in the cart's
    // __gfx__ on save.
    class SpriteEditor : public Editor
    {
    public:
        const char* name() const override { return "SPRITE"; }
        void        update(Console& con) override;
        void        draw(Console& con, Framebuffer& fb) override;

    private:
        int color_  = 7; // current draw color
        int sprite_ = 0; // selected sprite index 0..255
    };
} // namespace lazy100
