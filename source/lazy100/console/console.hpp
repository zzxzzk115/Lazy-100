#pragma once

#include "lazy100/audio/audio.hpp"
#include "lazy100/cart/cart.hpp"
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
#include "lazy100/net/fetch.hpp"
#include "lazy100/script/lua_runtime.hpp"
#include "lazy100/script/p8_vm.hpp"
#include "lazy100/shell/shell.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"
#include "lazy100/video/sprites.hpp"
#include "lazy100/world/map.hpp"

#include <array>
#include <chrono>
#include <string>
#include <vector>

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
        // Kiosk (consumer) mode: the play-only web pages (home) hide the developer pause-menu items
        // (edit / explore / shell), leaving just continue / reset. The /console page stays full.
        void        set_kiosk(bool on) { kiosk_ = on; }
        bool        kiosk() const { return kiosk_; }
        // Web: auto-pause when the tab goes to background — opens the cart pause menu (as ESC
        // would), which also pauses the music. No-op unless a cart is running menu-less, so it
        // never toggles an already-open menu closed.
        void        pause_from_web();
        void        quit() { running_ = false; } // exit the main loop

        double frame_dt() const { return dt_; } // seconds elapsed last frame (for UI timing)
        // Hover-delay gate for '?' tooltips: accumulates while the mouse rests on hotspot `id`,
        // returns true once it has hovered long enough. One hotspot is timed at a time.
        bool tooltip_active(int id, bool over);

        // Cart lifecycle (the .lz100 format bundles code + sprite sheet).
        std::string& code() { return code_; } // current cart's Lua source
        CartMeta&    cart_meta() { return cart_meta_; } // title/author (cart header + cart PNG)

        // Persistent author identity (like `git config user.name`): remembered across carts in a
        // small settings file, and used as the default author when exporting a cart.
        const std::string& user_author() const { return user_author_; }
        void               set_user_author(const std::string& a); // stores + persists
        bool         load_cart_file(const std::string& path); // parse .lz100/.lua -> code_ + sheet_
        bool         save_cart_file(const std::string& path); // serialize code_ + sheet_ -> .lz100
        void         new_cart();                              // blank code + sprite sheet
        bool         start_cart(); // compile+init the current code and switch to Running; false if empty
        // Load a cart and replay the power-on splash before starting it (ceremony) - the web site
        // uses this so a clicked cartridge always boots with the animation. False if it won't load.
        bool         restart_with_cart(const std::string& path);
        // Load a cart but leave it for the "press a key to start" boot gate to run (so the gate's
        // gesture unlocks/warms the audio). If the gate is already past, replays the splash now.
        bool         arm_cart(const std::string& path);
        // The current cart runs in one of two VMs: the p8-dialect z8lua VM (p8 carts, or any
        // cart tagged `--language:p8`) or the native Lua 5.4 + sol2 VM. These dispatch the
        // lifecycle to whichever is active.
        void cart_init();
        void         cart_update();
        void         cart_draw();
        bool         cart_wants_60hz() const;
        Framebuffer& present_framebuffer(); // framebuffer to display (bakes p8 screen palette)
        void detect_language(); // scan code_ for a --language directive (native carts)
        // Restart the running cart from scratch: re-import the cart file when one was loaded
        // (pristine sprites/map/sfx and p8 RAM), else just recompile + _init. The classic
        // pause-menu "reset cart".
        bool reset_cart();
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

        // Headless cart-image pack: load `cart_path` and export it as a `.lz100.png` (a visible
        // first-frame cover with the cart data hidden in the pixels). Used to produce the
        // catalog's PNG cartridges without a window/GPU.
        bool headless_pack(const std::string& cart_path, const std::string& out_png);

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

        // p8 screen palette (poke 0x5f10-0x5f1f / pal(c,d,1)): a display-time LUT that recolors
        // drawn index c to hardware color d. Unlike the global palette LUT it must NOT touch our
        // chrome/shell - a p8 cart's screen palette applies only inside its 128x128 viewport. So
        // it lives here as a separate index->index remap that is baked into a viewport-only copy
        // at present time (see step_frame). Identity by default; reset on every cart start.
        std::array<u8, 16>& screen_pal() { return screen_pal_; }
        void                mark_screen_pal_active() { screen_pal_active_ = true; }
        void                reset_screen_pal()
        {
            for (u8 i = 0; i < 16; ++i)
                screen_pal_[i] = i;
            screen_pal_active_ = false;
        }

        // p8 ext cart RAM: 64KB, the low half filled from the imported ROM, addressable by
        // the cart via peek/poke. Empty for native carts (peek/poke are then inert). Carts
        // with their own tracker poke the audio region and expect sfx()/music() to pick the
        // edits up; multi-cart games stash data in the upper half, which survives load().
        std::vector<u8>& p8ram() { return p8ram_; }
        bool             p8_mode() const { return !p8ram_.empty(); }

        // p8 load(): a cart asks to be replaced by another cart ("name.p8" beside it, or
        // "#id" fetched from the BBS and cached). The swap is deferred to the main loop - the
        // Lua VM cannot be destroyed while a cart call is still on the stack.
        void request_cart_load(const std::string& name)
        {
            pending_load_      = name;
            load_proxy_tried_ = false;
        }
        bool load_pending() const { return !pending_load_.empty(); }
        void process_pending_load(); // advance resolve/fetch/swap one step (main loop + tools)
        bool load_fetching() const { return load_fetching_; }

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
        P8Vm        p8vm_;
        Shell       shell_;
        EditorHost  editor_host_;
        ExploreHost explore_host_;
        PauseMenu   pause_menu_; // in-game ESC menu (continue/edit/explore/shell/exit)

        std::array<u8, kPaletteSize>   draw_pal_ {};
        std::array<bool, kPaletteSize> transparent_ {};

        int cam_x_ = 0, cam_y_ = 0; // camera() draw offset

        // p8 screen palette remap (viewport-only), identity by default. See screen_pal().
        std::array<u8, 16> screen_pal_ {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        bool               screen_pal_active_ = false;
        Framebuffer        present_fb_; // scratch: framebuffer with screen palette baked into the viewport

        std::string            cartdata_id_;   // open save slot id; empty = cartdata() not called
        std::array<double, 64> cartdata_ {};   // the slot's 64 persisted numbers

        std::string cart_path_; // file the current cart was loaded from ("" = editor-born)
        std::string code_;  // current cart's Lua source (edited by the code editor)
        std::string p8_raw_code_; // p8 carts: the untranslated dialect source, run by the z8lua VM
        bool        lang_p8_ = false; // the current cart runs in the p8 VM
        CartLabel   label_; // captured thumbnail for the cart PNG (__label__ section)
        CartMeta    cart_meta_; // title/author, carried in the cart header + shown on the cart PNG
        std::string user_author_; // persistent default author (saves/author.txt), git-config-like

        std::vector<u8> p8ram_; // p8 ext cart RAM (empty unless a p8 cart is loaded)

        std::string pending_load_;   // cart name a p8 load() asked for ("" = none)
        std::string prev_cart_path_; // cart running before the last load() swap (back-nav)
        net::Fetch  load_fetch_;     // BBS download in flight for pending_load_
        bool        load_fetching_    = false;
        bool        load_proxy_tried_ = false; // web: one CORS-proxy retry per request
        std::string load_cache_;     // where the downloaded cart gets cached

        ConsoleMode mode_      = ConsoleMode::Shell;
        bool        has_cart_  = false;
        bool        running_   = true;
        bool        kiosk_     = false; // web home: hide developer pause-menu items (edit/explore/shell)

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
