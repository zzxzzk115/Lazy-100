#pragma once

#include "lazy100/editor/editor.hpp"

namespace lazy100
{
    // Edit one SfxPattern of the cart's sound bank as a classic vertical tracker: 32 step rows,
    // each showing note name / waveform / volume. Arrow keys move the cursor and tune the note
    // (Left/Right = semitone, PageUp/Down = octave); digits set volume; the waveform/volume
    // pickers and pattern/speed/play controls are on-screen. Edits go into Console::sounds(), so
    // they persist in the cart's __sfx__ on save.
    class SfxEditor : public Editor
    {
    public:
        const char* name() const override { return "SFX"; }
        icon::Id    icon() const override { return icon::TabSfx; }
        void        update(Console& con) override;
        void        draw(Console& con, Framebuffer& fb) override;

    private:
        int current_  = 0; // sfx pattern index 0..63
        int cur_wave_ = 0; // pen waveform (for newly-activated steps)
        int cur_vol_  = 5; // pen volume
        int sel_step_ = 0; // cursor row 0..31
        int top_      = 0; // first visible step (scroll)
    };
} // namespace lazy100
