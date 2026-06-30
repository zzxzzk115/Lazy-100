#pragma once

#include "lazy100/common/types.hpp"

namespace lazy100
{
    // The console's virtual gamepad: 6 buttons (Left/Right/Up/Down/O/X), player 0 only in v1.
    // Edges are sampled per fixed logic step, not per render frame, so a one-frame tap fires
    // btnp exactly once even when render runs faster than the logic rate. SDL keymap lives in
    // the .cpp; this header stays SDL-free.
    class Input
    {
    public:
        enum Button
        {
            Left = 0,
            Right,
            Up,
            Down,
            O, // Z / C
            X, // X / V
            Count
        };

        void poll();       // refresh held state from the keyboard (once per render frame)
        void begin_step(); // compute pressed edges + auto-repeat (before _update)
        void end_step();   // latch prev = held (after _update)

        bool held(int button, int player = 0) const;
        bool pressed(int button, int player = 0) const;
        u32  held_mask(int player = 0) const;
        u32  pressed_mask(int player = 0) const;

    private:
        u32 held_    = 0;
        u32 prev_    = 0;
        u32 pressed_ = 0;
        int repeat_[Count] = {}; // consecutive held-steps per button, for auto-repeat
    };
} // namespace lazy100
