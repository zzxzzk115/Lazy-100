#pragma once

#include "lazy100/audio/audio.hpp"
#include "lazy100/cart/label.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/console/pausemenu.hpp"
#include "lazy100/console/window.hpp"
#include "lazy100/editor/editor.hpp"
#include "lazy100/explore/explore.hpp"
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
#include <chrono>
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
        Editor,
        Explore // the online game browser (shell `explore`)
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
        // A cart is only "blank" when EVERY section is untouched - artists often draw or
        // compose before writing any code, and that work must still be saveable.
        bool cart_blank() const
        {
            return code_.empty() && sheet_.blank() && map_.blank() && sounds_.blank() && !label_.present;
        }
        void         capture_label(); // snapshot the framebuffer as the cart's 160x120 thumbnail (Ctrl+7)

        // Headless first-frame render: load `cart_path`, run _init + one _draw with no
        // window/GPU/audio/input, and write the 320x240 framebuffer as an RGBA PNG. Used by
        // the cartshot tool to generate catalog preview images.
        bool headless_shot(const std::string& cart_path, const std::string& out_png);

        // Render the current cart's first frame into `out` (a scratch Lua VM runs _init + one
        // _draw against this console, then per-cart draw state is reset). Used as the cart-PNG
        // cover when no label was captured with Ctrl+7 - a real frame beats the abstract
        // sprite-sheet fallback.
        void render_first_frame_label(CartLabel& out);

        // Cart save data (cartdata/dset/dget): 64 numbers persisted per cart id under
        // saves/<id>.txt. cartdata_open loads (or creates) the slot; cartdata_set writes through
        // immediately (the file is tiny) and flushes web storage.
        bool   cartdata_open(const std::string& id);
        double cartdata_get(int index) const;
        void   cartdata_set(int index, double value);
        bool   cartdata_active() const { return !cartdata_id_.empty(); }

        // camera() scroll offset: subtracted from every draw coordinate (world -> screen).
        int  cam_x() const { return cam_x_; }
        int  cam_y() const { return cam_y_; }
        void set_camera(int x, int y)
        {
            cam_x_ = x;
            cam_y_ = y;
        }

        // pal/palt drawing state (persistent across frames).
        u8*  draw_pal() { return draw_pal_.data(); } // color remap applied on blit
        bool* transparent() { return transparent_.data(); }
        void  reset_draw_pal();    // identity remap
        void  reset_transparent(); // only index 0 transparent

    private:
        bool        step_frame();            // run one frame of the main loop; false once quit
        static void frame_thunk(void* self); // Emscripten requestAnimationFrame trampoline -> step_frame

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
        ExploreHost explore_host_;
        PauseMenu   pause_menu_; // in-game ESC menu (continue/edit/explore/shell/exit)

        std::array<u8, kPaletteSize>   draw_pal_ {};
        std::array<bool, kPaletteSize> transparent_ {};

        int cam_x_ = 0, cam_y_ = 0; // camera() draw offset

        std::string            cartdata_id_;   // open save slot id; empty = cartdata() not called
        std::array<double, 64> cartdata_ {};   // the slot's 64 persisted numbers

        std::string code_;  // current cart's Lua source (edited by the code editor)
        CartLabel   label_; // captured thumbnail for the cart PNG (__label__ section)

        ConsoleMode mode_      = ConsoleMode::Shell;
        bool        has_cart_  = false;
        bool        running_   = true;

        double      boot_warm_t_ = 0.0;                 // seconds elapsed in the audio warm-up hold
        double      boot_t_      = 0.0;                 // seconds elapsed in the power-on splash
        bool        boot_jingle_ = false;               // has the power-on chime been fired yet
        bool        web_started_ = false;               // web: first gesture received (audio unlocked)
        ConsoleMode boot_next_   = ConsoleMode::Shell;  // mode to enter once the splash finishes

        std::chrono::steady_clock::time_point run_prev_ {};  // previous frame timestamp (delta timing)
        double                                run_acc_ = 0.0; // fixed-step accumulator (Running mode)

        double dt_       = 0.0; // last frame delta (seconds)
        int    hover_id_ = -1;  // hotspot currently being hover-timed
        double hover_t_  = 0.0; // seconds hovered on hover_id_
    };
} // namespace lazy100
