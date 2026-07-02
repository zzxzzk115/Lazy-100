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
        // Warm-up filler shown before the splash while the audio device wakes a sleeping speaker:
        // a color-cycling, bouncing "booting..." so the black hold isn't dead air.
        void draw_warmup(Framebuffer& fb, double t);
        // Web-only pre-boot prompt: browsers can't autoplay audio, so hold here until the first
        // click/key (which resumes the AudioContext) before running the boot + chime. t drives a
        // gentle blink of the "click to start" line.
        void draw_web_gate(Framebuffer& fb, double t);
    } // namespace boot
} // namespace lazy100
