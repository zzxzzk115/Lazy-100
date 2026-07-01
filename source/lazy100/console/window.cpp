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
        SDL_StartTextInput(window_); // editors need typed characters (SDL_EVENT_TEXT_INPUT)
        SDL_HideCursor();            // we draw our own pixel cursor into the framebuffer
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
        raw_.text.clear(); // text + wheel are per-frame; mouse pos/buttons persist
        raw_.wheel = 0;

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    running = false;
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    raw_.text += e.text.text;
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    raw_.mouse_x = static_cast<int>(e.motion.x);
                    raw_.mouse_y = static_cast<int>(e.motion.y);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                {
                    u32 b = 0;
                    if (e.button.button == SDL_BUTTON_LEFT)
                        b = 1u << 0;
                    else if (e.button.button == SDL_BUTTON_RIGHT)
                        b = 1u << 1;
                    else if (e.button.button == SDL_BUTTON_MIDDLE)
                        b = 1u << 2;
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                        raw_.mouse_buttons |= b;
                    else
                        raw_.mouse_buttons &= ~b;
                    raw_.mouse_x = static_cast<int>(e.button.x);
                    raw_.mouse_y = static_cast<int>(e.button.y);
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL:
                    raw_.wheel += static_cast<int>(e.wheel.y);
                    break;
                default:
                    break;
            }
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
