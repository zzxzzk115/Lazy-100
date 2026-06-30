#pragma once

#include "lazy100/common/types.hpp"

struct SDL_Window; // forward-declared so this header stays free of SDL includes

namespace lazy100
{
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

        // Drain the OS event queue; clears `running` when the user asks to close.
        void pump_events(bool& running);

        // Size of the drawable in pixels (0 while minimized).
        void drawable_size(u32& width, u32& height) const;

        SDL_Window* handle() const { return window_; }

    private:
        SDL_Window* window_     = nullptr;
        bool        sdl_inited_ = false;
    };
} // namespace lazy100
