#pragma once

#include "lazy100/console/window.hpp"
#include "lazy100/gpu/present.hpp"

namespace lazy100
{
    // Orchestrates the console: owns the window + present layer (and, from later milestones,
    // the framebuffer, input, audio and Lua runtime) and runs the main loop.
    class Console
    {
    public:
        bool boot();
        void run();
        void shutdown();

    private:
        Window  window_;
        Present present_;
        bool    running_ = true;
    };
} // namespace lazy100
