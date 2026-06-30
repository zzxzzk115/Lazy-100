#pragma once

#include "lazy100/console/window.hpp"
#include "lazy100/gpu/present.hpp"
#include "lazy100/input/input.hpp"
#include "lazy100/script/lua_runtime.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"

namespace lazy100
{
    // Orchestrates the console: owns the window, present layer, framebuffer, palette and the
    // Lua runtime (input and audio join in later milestones) and runs the main loop. The Lua
    // API binds against the framebuffer/palette exposed here.
    class Console
    {
    public:
        bool boot(const char* cart_path = nullptr);
        void run();
        void shutdown();

        Framebuffer& framebuffer() { return framebuffer_; }
        Palette&     palette() { return palette_; }
        Input&       input() { return input_; }

    private:
        Window      window_;
        Present     present_;
        Framebuffer framebuffer_;
        Palette     palette_;
        Input       input_;
        LuaRuntime  lua_;
        bool        has_cart_ = false;
        bool        running_  = true;
    };
} // namespace lazy100
