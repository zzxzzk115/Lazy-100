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
            Num7, // for the Ctrl+7 "capture cart label" shortcut
            A,    // Ctrl+A/C/V/X: the code editor's select-all / clipboard shortcuts.
            C,    // Appended AFTER the original keys so the web inject_keys() bit layout
            V,    // (Escape=0 .. Down=8) stays stable.
            X,
            Count
        };

        void update(const Window& window, double dt); // call once per render frame; dt = seconds

        // Web virtual controls: OR a Key bitmask (1u<<Key) into the held state, and queue typed
        // UTF-8 text, so the touch gamepad / on-screen keyboard drive menus, the shell and editors
        // exactly like a physical keyboard. inject_text accumulates until the next update() consumes it.
        void inject_keys(u32 mask) { inj_keys_ = mask; }
        void inject_text(const std::string& s) { inj_text_ += s; }

        bool held(Key k) const;
        bool pressed(Key k) const; // clean single-press edge (no auto-repeat) — for toggles
        bool repeat(Key k) const;  // edge + auto-repeat while held — for nav/edit keys

        const std::string& text() const { return text_; } // characters typed this frame

        bool ctrl() const { return ctrl_; }
        bool shift() const { return shift_; }
        bool alt() const { return alt_; }

    private:
        bool        held_[Count] = {};
        bool        prev_[Count] = {};
        double      hold_[Count] = {}; // seconds a key has been held (for auto-repeat timing)
        double      acc_[Count]  = {}; // seconds since this key last emitted a repeat pulse
        bool        fire_[Count] = {}; // whether repeat() should return true this frame
        std::string text_;
        bool        ctrl_  = false;
        bool        shift_ = false;
        bool        alt_   = false;
        u32         inj_keys_ = 0; // web virtual-control key bitmask (1u<<Key), OR'd into held_
        std::string inj_text_;     // web virtual-keyboard typed text, drained each update()
    };
} // namespace lazy100
