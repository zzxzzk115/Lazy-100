#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include "lazy100/input/input.hpp"

namespace lazy100
{
    namespace
    {
        constexpr u32 bit(int b) { return 1u << b; }

        // Classic auto-repeat: after the initial press, wait 15 steps, then fire every 4.
        constexpr int kRepeatDelay  = 15;
        constexpr int kRepeatPeriod = 4;
    } // namespace

    void Input::poll()
    {
        const bool* ks = SDL_GetKeyboardState(nullptr);
        u32         m  = 0;
        if (ks[SDL_SCANCODE_LEFT])
            m |= bit(Left);
        if (ks[SDL_SCANCODE_RIGHT])
            m |= bit(Right);
        if (ks[SDL_SCANCODE_UP])
            m |= bit(Up);
        if (ks[SDL_SCANCODE_DOWN])
            m |= bit(Down);
        if (ks[SDL_SCANCODE_Z] || ks[SDL_SCANCODE_C])
            m |= bit(O);
        if (ks[SDL_SCANCODE_X] || ks[SDL_SCANCODE_V])
            m |= bit(X);
        held_ = m | touch_; // fold in the web virtual gamepad
    }

    void Input::begin_step()
    {
        const u32 edges = held_ & ~prev_;
        pressed_        = edges;
        for (int b = 0; b < Count; ++b)
        {
            if (!(held_ & bit(b)))
            {
                repeat_[b] = 0;
                continue;
            }
            if (edges & bit(b))
            {
                repeat_[b] = 0; // fresh press already counted in `edges`
                continue;
            }
            ++repeat_[b];
            if (repeat_[b] >= kRepeatDelay && (repeat_[b] - kRepeatDelay) % kRepeatPeriod == 0)
                pressed_ |= bit(b);
        }
    }

    void Input::end_step() { prev_ = held_; }

    bool Input::held(int button, int player) const
    {
        return player == 0 && button >= 0 && button < Count && (held_ & bit(button));
    }
    bool Input::pressed(int button, int player) const
    {
        return player == 0 && button >= 0 && button < Count && (pressed_ & bit(button));
    }
    u32 Input::held_mask(int player) const { return player == 0 ? held_ : 0; }
    u32 Input::pressed_mask(int player) const { return player == 0 ? pressed_ : 0; }
} // namespace lazy100
