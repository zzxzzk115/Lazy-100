#pragma once

#include "lazy100/audio/audio.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/console/window.hpp"
#include "lazy100/gpu/present.hpp"
#include "lazy100/input/input.hpp"
#include "lazy100/script/lua_runtime.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"
#include "lazy100/video/sprites.hpp"

#include <array>

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
        SpriteSheet& sheet() { return sheet_; }
        Audio&       audio() { return audio_; }

        // pal/palt drawing state (persistent across frames, PICO-8 style).
        u8*         draw_pal() { return draw_pal_.data(); } // color remap applied on blit
        bool*       transparent() { return transparent_.data(); }
        void        reset_draw_pal();    // identity remap
        void        reset_transparent(); // only index 0 transparent

    private:
        Window      window_;
        Present     present_;
        Framebuffer framebuffer_;
        Palette     palette_;
        Input       input_;
        SpriteSheet sheet_;
        Audio       audio_;
        LuaRuntime  lua_;

        std::array<u8, kPaletteSize>   draw_pal_ {};
        std::array<bool, kPaletteSize> transparent_ {};

        bool has_cart_ = false;
        bool running_  = true;
    };
} // namespace lazy100
