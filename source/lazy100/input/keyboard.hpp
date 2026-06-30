#pragma once

#include "lazy100/common/types.hpp"

#include <string>

namespace lazy100
{
    class Window;

    // Full-keyboard input for the shell and editors (distinct from the 6-button game Input):
    // per-frame edges + auto-repeat for navigation/edit keys, plus the UTF-8 text typed this
    // frame and the modifier state. SDL scancodes are mapped in the .cpp so this header stays
    // SDL-free.
    class Keyboard
    {
    public:
        enum Key
        {
            Escape = 0,
            Return,
            Backspace,
            Delete,
            Tab,
            Left,
            Right,
            Up,
            Down,
            Home,
            End,
            PageUp,
            PageDown,
            Count
        };

        void update(const Window& window); // call once per render frame

        bool held(Key k) const;
        bool pressed(Key k) const; // clean single-press edge (no auto-repeat) — for toggles
        bool repeat(Key k) const;  // edge + auto-repeat while held — for nav/edit keys

        const std::string& text() const { return text_; } // characters typed this frame

        bool ctrl() const { return ctrl_; }
        bool shift() const { return shift_; }
        bool alt() const { return alt_; }

    private:
        bool        held_[Count]   = {};
        bool        prev_[Count]   = {};
        int         repeat_[Count] = {}; // consecutive held frames, for auto-repeat
        std::string text_;
        bool        ctrl_  = false;
        bool        shift_ = false;
        bool        alt_   = false;
    };
} // namespace lazy100
