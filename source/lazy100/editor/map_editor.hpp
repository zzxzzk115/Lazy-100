#pragma once

#include "lazy100/editor/editor.hpp"

namespace lazy100
{
    // Paint the 128x64 tile map: a scrolling viewport of 16px tiles on top (left mouse paints
    // the selected sprite, right mouse erases; arrow keys pan), a sprite picker at the bottom,
    // and a magnified preview of the selected tile. Edits go into Console::map(), so they
    // persist in the cart's __map__ on save.
    class MapEditor : public Editor
    {
    public:
        const char* name() const override { return "MAP"; }
        void        update(Console& con) override;
        void        draw(Console& con, Framebuffer& fb) override;

    private:
        int tile_  = 1; // sprite index to paint (0 = erase / empty)
        int cam_x_ = 0; // top-left visible tile column
        int cam_y_ = 0; // top-left visible tile row
    };
} // namespace lazy100
