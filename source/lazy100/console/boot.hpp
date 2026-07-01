#pragma once

#include "lazy100/audio/sound.hpp"

namespace lazy100
{
    class Framebuffer;

    // The power-on splash: a short rainbow warm-up that opens to the "LAZY-100" logo, a tagline,
    // and a copyright footer, with a one-shot chime. Shown once on a bare boot before the shell.
    namespace boot
    {
        inline constexpr double kDuration = 2.6; // seconds of splash before handing off

        SfxPattern jingle();                        // startup chime, played once at power-on
        void       draw(Framebuffer& fb, double t); // t = seconds elapsed since boot started
    } // namespace boot
} // namespace lazy100
