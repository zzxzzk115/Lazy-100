#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include "lazy100/console/window.hpp"
#include "lazy100/input/keyboard.hpp"

namespace lazy100
{
    namespace
    {
        // Auto-repeat tuned for per-frame editor input (~60 fps): ~0.3s delay, then fast.
        constexpr int kRepeatDelay  = 20;
        constexpr int kRepeatPeriod = 2;

        SDL_Scancode scancode(Keyboard::Key k)
        {
            switch (k)
            {
                case Keyboard::Escape: return SDL_SCANCODE_ESCAPE;
                case Keyboard::Return: return SDL_SCANCODE_RETURN;
                case Keyboard::Backspace: return SDL_SCANCODE_BACKSPACE;
                case Keyboard::Delete: return SDL_SCANCODE_DELETE;
                case Keyboard::Tab: return SDL_SCANCODE_TAB;
                case Keyboard::Left: return SDL_SCANCODE_LEFT;
                case Keyboard::Right: return SDL_SCANCODE_RIGHT;
                case Keyboard::Up: return SDL_SCANCODE_UP;
                case Keyboard::Down: return SDL_SCANCODE_DOWN;
                case Keyboard::Home: return SDL_SCANCODE_HOME;
                case Keyboard::End: return SDL_SCANCODE_END;
                case Keyboard::PageUp: return SDL_SCANCODE_PAGEUP;
                case Keyboard::PageDown: return SDL_SCANCODE_PAGEDOWN;
                default: return SDL_SCANCODE_UNKNOWN;
            }
        }
    } // namespace

    void Keyboard::update(const Window& window)
    {
        const bool* ks = SDL_GetKeyboardState(nullptr);

        for (int i = 0; i < Count; ++i)
        {
            prev_[i] = held_[i];
            held_[i] = ks[scancode(static_cast<Key>(i))];
            if (!held_[i])
                repeat_[i] = 0;
            else if (held_[i] && !prev_[i]) // fresh press
                repeat_[i] = 0;
            else
                ++repeat_[i];
        }

        ctrl_  = ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];
        shift_ = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
        alt_   = ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT];

        text_ = window.raw_input().text;
    }

    bool Keyboard::held(Key k) const { return k >= 0 && k < Count && held_[k]; }

    bool Keyboard::pressed(Key k) const
    {
        return k >= 0 && k < Count && held_[k] && !prev_[k]; // clean edge, no repeat
    }

    bool Keyboard::repeat(Key k) const
    {
        if (k < 0 || k >= Count || !held_[k])
            return false;
        if (!prev_[k])
            return true; // initial press
        return repeat_[k] >= kRepeatDelay && (repeat_[k] - kRepeatDelay) % kRepeatPeriod == 0;
    }
} // namespace lazy100
