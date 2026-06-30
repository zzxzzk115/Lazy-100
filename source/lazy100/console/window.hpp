#pragma once

#include "lazy100/common/types.hpp"

#include <string>

struct SDL_Window; // forward-declared so this header stays free of SDL includes

namespace lazy100
{
    // Per-frame OS input snapshot the event pump fills (text + mouse). Keyboard key state is
    // read separately via SDL_GetKeyboardState in input/keyboard.
    struct RawInput
    {
        std::string text;            // UTF-8 characters typed this frame
        int         mouse_x = 0;     // window pixels
        int         mouse_y = 0;
        u32         mouse_buttons = 0; // bit0=Left, bit1=Right, bit2=Middle
        int         wheel = 0;         // accumulated wheel delta this frame
    };

    // Owns the SDL3 window + the SDL video subsystem. The native handle it exposes is fed
    // to VRI (via vriWindowHandleFromSDL3) inside gpu/present; this class itself never
    // touches VRI.
    class Window
    {
    public:
        Window() = default;
        ~Window();

        Window(const Window&)            = delete;
        Window& operator=(const Window&) = delete;

        bool create(const char* title, u32 width, u32 height);
        void destroy();

        // Drain the OS event queue; clears `running` when the user asks to close. Updates the
        // per-frame RawInput (text typed, mouse position/buttons/wheel).
        void pump_events(bool& running);

        // Size of the drawable in pixels (0 while minimized).
        void drawable_size(u32& width, u32& height) const;

        const RawInput& raw_input() const { return raw_; }

        SDL_Window* handle() const { return window_; }

    private:
        SDL_Window* window_     = nullptr;
        bool        sdl_inited_ = false;
        RawInput    raw_;
    };
} // namespace lazy100
