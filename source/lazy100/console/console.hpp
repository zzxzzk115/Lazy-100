#pragma once

#include "lazy100/audio/audio.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/console/window.hpp"
#include "lazy100/editor/editor.hpp"
#include "lazy100/gpu/present.hpp"
#include "lazy100/input/input.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/script/lua_runtime.hpp"
#include "lazy100/shell/shell.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"
#include "lazy100/video/sprites.hpp"
#include "lazy100/world/map.hpp"

#include <array>
#include <string>

namespace lazy100
{
    // What the main loop is doing: the boot command line, a running cart, or the editor suite.
    // ESC toggles between them; the shell `run`/`edit` commands switch explicitly.
    enum class ConsoleMode
    {
        Boot, // power-on splash; auto-advances to Shell
        Shell,
        Running,
        Editor
    };

    // Orchestrates the console: owns every subsystem and runs the main loop, dispatching
    // update/draw by mode. The Lua API and the editors bind against the state exposed here.
    class Console
    {
    public:
        bool boot(const char* cart_path = nullptr);
        void run();
        void shutdown();

        Framebuffer& framebuffer() { return framebuffer_; }
        Palette&     palette() { return palette_; }
        Input&       input() { return input_; }
        Keyboard&    keyboard() { return keyboard_; }
        Mouse&       mouse() { return mouse_; }
        SpriteSheet& sheet() { return sheet_; }
        Map&         map() { return map_; }
        SoundBank&   sounds() { return sounds_; }
        Audio&       audio() { return audio_; }

        ConsoleMode mode() const { return mode_; }
        void        set_mode(ConsoleMode m) { mode_ = m; }
        void        quit() { running_ = false; } // exit the main loop

        double frame_dt() const { return dt_; } // seconds elapsed last frame (for UI timing)
        // Hover-delay gate for '?' tooltips: accumulates while the mouse rests on hotspot `id`,
        // returns true once it has hovered long enough. One hotspot is timed at a time.
        bool tooltip_active(int id, bool over);

        // Cart lifecycle (the .lz100 format bundles code + sprite sheet).
        std::string& code() { return code_; } // current cart's Lua source
        bool         load_cart_file(const std::string& path); // parse .lz100/.lua -> code_ + sheet_
        bool         save_cart_file(const std::string& path); // serialize code_ + sheet_ -> .lz100
        void         new_cart();                              // blank code + sprite sheet
        bool         start_cart(); // compile+init the current code and switch to Running; false if empty

        // pal/palt drawing state (persistent across frames, PICO-8 style).
        u8*  draw_pal() { return draw_pal_.data(); } // color remap applied on blit
        bool* transparent() { return transparent_.data(); }
        void  reset_draw_pal();    // identity remap
        void  reset_transparent(); // only index 0 transparent

    private:
        Window      window_;
        Present     present_;
        Framebuffer framebuffer_;
        Palette     palette_;
        Input       input_;
        Keyboard    keyboard_;
        Mouse       mouse_;
        SpriteSheet sheet_;
        Map         map_;
        SoundBank   sounds_;
        Audio       audio_;
        LuaRuntime  lua_;
        Shell       shell_;
        EditorHost  editor_host_;

        std::array<u8, kPaletteSize>   draw_pal_ {};
        std::array<bool, kPaletteSize> transparent_ {};

        std::string code_; // current cart's Lua source (edited by the code editor)

        ConsoleMode mode_      = ConsoleMode::Shell;
        bool        has_cart_  = false;
        bool        running_   = true;

        double      boot_t_    = 0.0;                 // seconds elapsed in the power-on splash
        ConsoleMode boot_next_ = ConsoleMode::Shell;  // mode to enter once the splash finishes

        double dt_       = 0.0; // last frame delta (seconds)
        int    hover_id_ = -1;  // hotspot currently being hover-timed
        double hover_t_  = 0.0; // seconds hovered on hover_id_
    };
} // namespace lazy100
