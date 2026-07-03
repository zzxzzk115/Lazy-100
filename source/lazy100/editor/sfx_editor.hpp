#pragma once

#include "lazy100/editor/editor.hpp"

namespace lazy100
{
    struct SfxPattern;

    // Edit one SfxPattern of the cart's sound bank, tracker-style: two views like the classic
    // fantasy-console layout - BARS (32 pitch columns, drag to draw melodies) and TRACK (step
    // rows showing note / waveform / volume / effect). Speed and the loop region sit in the
    // toolbar; waveform / volume / effect pickers along the bottom set the pen and the selected
    // step. Edits go into Console::sounds(), so they persist in the cart's __sfx__ on save.
    class SfxEditor : public Editor
    {
    public:
        const char* name() const override { return "SFX"; }
        icon::Id    icon() const override { return icon::TabSfx; }
        void        update(Console& con) override;
        void        draw(Console& con, Framebuffer& fb) override;
        void        draw_tools(Console& con, Framebuffer& fb) override;

    private:
        void draw_bars(Console& con, Framebuffer& fb, SfxPattern& pat);
        void draw_tracker(Console& con, Framebuffer& fb, SfxPattern& pat);

        int  current_  = 0;    // sfx pattern index 0..63
        int  cur_wave_ = 0;    // pen waveform (for newly-activated steps)
        int  cur_vol_  = 5;    // pen volume
        int  cur_fx_   = 0;    // pen effect
        int  sel_step_ = 0;    // cursor row 0..31
        int  top_      = 0;    // first visible step (scroll, tracker view)
        bool bars_     = true; // true: pitch-bars view; false: tracker rows
        int  aud_step_  = -1;  // last auditioned (step, pitch) during a bars drag, so the
        int  aud_pitch_ = -1;  // preview blip fires per change, not per frame
    };
} // namespace lazy100
