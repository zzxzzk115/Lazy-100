#pragma once

#include "lazy100/common/types.hpp"

namespace lazy100
{
    class Framebuffer;

    // A small baked set of 8x8 monochrome icons for the editor UI (tabs, buttons, section
    // headers). Each icon is 8 rows; bit 7 is the leftmost column. Drawn as set-pixels in a
    // single color, so they compose over any button/panel background.
    namespace icon
    {
        enum Id
        {
            Play = 0,
            Pause,
            Stop,
            Prev,
            Next,
            Plus,
            Minus,
            Pencil,
            Eraser,
            Dropper,
            TabCode,
            TabSprite,
            TabMap,
            TabSfx,
            TabMusic,
            WaveSquare,
            WavePulse,
            WaveTriangle,
            WaveSaw,
            WaveNoise,
            Help, // '?' hint button
            Book, // manual / cheatsheet button
            Count
        };

        constexpr int kSize = 8;

        // Blit icon `id` with top-left at (x,y) in color c (only set bits are drawn).
        void draw(Framebuffer& fb, Id id, int x, int y, u8 c);
    } // namespace icon
} // namespace lazy100
