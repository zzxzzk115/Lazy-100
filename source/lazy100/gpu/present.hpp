#pragma once

#include "lazy100/common/types.hpp"

#include <memory>

namespace lazy100
{
    class Window;

    // The only layer that touches VRI. Owns the device + swapchain and turns the console's
    // output into on-screen pixels. M0: brings the device up and presents a solid clear.
    // M1 extends this to upload the 320x240 index framebuffer and resolve the palette in a
    // full-screen pass. All VRI types live behind the pimpl so the rest of the kernel never
    // sees them.
    class Present
    {
    public:
        Present();
        ~Present();

        Present(const Present&)            = delete;
        Present& operator=(const Present&) = delete;

        bool init(Window& window);
        void shutdown();

        // Clear the backbuffer to a color and present. Keeps the swapchain matched to the
        // window size and tolerates minimize / resize. (M0 stand-in for submit_frame.)
        void present_clear(float r, float g, float b, float a = 1.0f);

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };
} // namespace lazy100
