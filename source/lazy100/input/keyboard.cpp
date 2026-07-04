#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include "lazy100/console/window.hpp"
#include "lazy100/input/keyboard.hpp"

namespace lazy100
{
    namespace
    {
        // Auto-repeat timing in seconds (frame-rate independent): a hold delay, then a calm
        // repeat interval. Tuned to feel deliberate, not twitchy, on any refresh rate.
        constexpr double kRepeatDelay  = 0.40; // wait before repeating
        constexpr double kRepeatPeriod = 0.09; // ~11 pulses/sec while held

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
                case Keyboard::Num7: return SDL_SCANCODE_7;
                default: return SDL_SCANCODE_UNKNOWN;
            }
        }
    } // namespace

    void Keyboard::update(const Window& window, double dt)
    {
        const bool* ks = SDL_GetKeyboardState(nullptr);

        for (int i = 0; i < Count; ++i)
        {
            prev_[i] = held_[i];
            held_[i] = ks[scancode(static_cast<Key>(i))] || (inj_keys_ & (1u << i)); // web virtual keys
            if (!held_[i])
            {
                hold_[i] = acc_[i] = 0.0;
                fire_[i]           = false;
            }
            else if (!prev_[i]) // fresh press: fire once immediately
            {
                hold_[i] = acc_[i] = 0.0;
                fire_[i]           = true;
            }
            else
            {
                hold_[i] += dt;
                fire_[i] = false;
                if (hold_[i] >= kRepeatDelay) // past the initial delay -> pulse every period
                {
                    acc_[i] += dt;
                    if (acc_[i] >= kRepeatPeriod)
                    {
                        acc_[i] -= kRepeatPeriod;
                        fire_[i] = true;
                    }
                }
            }
        }

        ctrl_  = ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];
        shift_ = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
        alt_   = ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT];

        text_ = window.raw_input().text;
        if (!inj_text_.empty()) // web on-screen keyboard: append the injected characters this frame
        {
            text_ += inj_text_;
            inj_text_.clear();
        }
    }

    bool Keyboard::held(Key k) const { return k >= 0 && k < Count && held_[k]; }

    bool Keyboard::pressed(Key k) const
    {
        return k >= 0 && k < Count && held_[k] && !prev_[k]; // clean edge, no repeat
    }

    bool Keyboard::repeat(Key k) const { return k >= 0 && k < Count && fire_[k]; }
} // namespace lazy100
