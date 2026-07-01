#pragma once

#include "lazy100/editor/editor.hpp"

namespace lazy100
{
    // Edit one SfxPattern of the cart's sound bank: a 32-step piano-roll grid (left-click sets a
    // step's pitch with the current wave/volume, right-click clears it), waveform and volume
    // pickers, a speed control, pattern selection, and a play button that previews via the audio
    // engine. Edits go into Console::sounds(), so they persist in the cart's __sfx__ on save.
    class SfxEditor : public Editor
    {
    public:
        const char* name() const override { return "SFX"; }
        void        update(Console& con) override;
        void        draw(Console& con, Framebuffer& fb) override;

    private:
        int current_  = 0; // sfx pattern index 0..63
        int cur_wave_ = 0; // waveform to paint
        int cur_vol_  = 5; // volume to paint (1..7)
    };
} // namespace lazy100
