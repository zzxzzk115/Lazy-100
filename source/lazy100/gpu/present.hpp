#pragma once

#include "lazy100/common/types.hpp"

#include <memory>

namespace lazy100
{
    class Window;
    class Framebuffer;
    class Palette;

    // The only layer that touches VRI. Owns the device + swapchain and the present pipeline
    // that turns the console's indexed framebuffer into on-screen pixels: each frame it
    // uploads the 320x240 R8_UINT index texture, then draws a full-screen triangle that
    // resolves index -> color through the palette uniform, integer-scaled and letterboxed.
    // All VRI types stay behind the pimpl so the rest of the kernel never sees them.
    class Present
    {
    public:
        Present();
        ~Present();

        Present(const Present&)            = delete;
        Present& operator=(const Present&) = delete;

        bool init(Window& window);
        void shutdown();

        // Upload the framebuffer (+ palette if dirty) and present it scaled to the window.
        void submit_frame(const Framebuffer& fb, Palette& palette);

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };
} // namespace lazy100
