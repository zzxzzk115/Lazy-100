#define SDL_MAIN_HANDLED // we provide our own main(); don't let SDL hijack it
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> // for SDL_SetMainReady()

#include "lazy100/common/log.hpp"
#include "lazy100/console/window.hpp"

namespace lazy100
{
    Window::~Window() { destroy(); }

    bool Window::create(const char* title, u32 width, u32 height)
    {
        SDL_SetMainReady();
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            LZ_ERROR("SDL_Init failed: %s", SDL_GetError());
            return false;
        }
        sdl_inited_ = true;

        window_ = SDL_CreateWindow(title, static_cast<int>(width), static_cast<int>(height), SDL_WINDOW_RESIZABLE);
        if (!window_)
        {
            LZ_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
            return false;
        }
        SDL_RaiseWindow(window_); // claim focus in case a backend's context grab opened us behind
        return true;
    }

    void Window::destroy()
    {
        if (window_)
        {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (sdl_inited_)
        {
            SDL_Quit();
            sdl_inited_ = false;
        }
    }

    void Window::pump_events(bool& running)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                running = false;
        }
    }

    void Window::drawable_size(u32& width, u32& height) const
    {
        int w = 0, h = 0;
        if (window_)
            SDL_GetWindowSizeInPixels(window_, &w, &h);
        width  = w > 0 ? static_cast<u32>(w) : 0u;
        height = h > 0 ? static_cast<u32>(h) : 0u;
    }
} // namespace lazy100
