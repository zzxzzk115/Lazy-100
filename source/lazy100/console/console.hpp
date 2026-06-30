#pragma once

#include "lazy100/console/window.hpp"
#include "lazy100/gpu/present.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"

namespace lazy100
{
    // Orchestrates the console: owns the window, present layer, framebuffer and palette
    // (and, from later milestones, input, audio and the Lua runtime) and runs the main loop.
    class Console
    {
    public:
        bool boot();
        void run();
        void shutdown();

    private:
        Window      window_;
        Present     present_;
        Framebuffer framebuffer_;
        Palette     palette_;
        bool        running_ = true;
    };
} // namespace lazy100
