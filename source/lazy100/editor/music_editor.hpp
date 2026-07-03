#pragma once

#include "lazy100/editor/editor.hpp"

namespace lazy100
{
    // Arrange the cart's music, tracker-style: each MusicPattern assigns an sfx to up to 4
    // channels that play together, shown as 4 side-by-side note columns (the assigned sfx's 32
    // steps). Notes are edited in place - click a cell, then arrows tune / digits set volume,
    // exactly like the sfx editor - so a song can be written without leaving this pane.
    // Pattern chaining uses the loop-start / loop-end / stop flags. Edits go into
    // Console::sounds() -> the cart's __music__ and __sfx__.
    class MusicEditor : public Editor
    {
    public:
        const char* name() const override { return "MUSIC"; }
        icon::Id    icon() const override { return icon::TabMusic; }
        void        update(Console& con) override;
        void        draw(Console& con, Framebuffer& fb) override;

    private:
        int  current_ = 0;     // music pattern index 0..63 (edit/play cursor)
        bool paused_  = false; // transport is paused (Play button shows a resume arrow)
        int  sel_ch_  = 0;     // selected channel column 0..3
        int  sel_row_ = 0;     // selected step row 0..31
        int  top_     = 0;     // first visible step row (scroll)
    };
} // namespace lazy100
