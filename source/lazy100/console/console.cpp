#include "lazy100/console/console.hpp"

#include "lazy100/cart/cart.hpp"
#include "lazy100/cart/cartpng.hpp"
#include "lazy100/common/log.hpp"
#include "lazy100/console/boot.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/vfs/persist.hpp"
#include "lazy100/vfs/vfs.hpp"
#include "lazy100/video/cursor.hpp"
#include "lazy100/video/font.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(__EMSCRIPTEN__)
#    include <emscripten.h>
#endif

namespace
{
    // Append "exit console" only where quitting means something: inside a browser tab there is
    // no process to exit (halting the loop would just leave a dead canvas), so the wasm build
    // hides the item. It is always the LAST entry, so the other indices never shift.
    std::vector<std::string> with_exit(std::vector<std::string> items)
    {
#if !defined(__EMSCRIPTEN__)
        items.emplace_back("exit console");
#endif
        return items;
    }
} // namespace

namespace lazy100
{
    void Console::reset_draw_pal()
    {
        for (u32 i = 0; i < kPaletteSize; ++i)
            draw_pal_[i] = static_cast<u8>(i);
    }

    void Console::reset_transparent()
    {
        for (u32 i = 0; i < kPaletteSize; ++i)
            transparent_[i] = (i == 0); // index 0 transparent by default
    }

    bool Console::boot(const char* cart_path)
    {
        const u32 w = kScreenW * kDefaultScale;
        const u32 h = kScreenH * kDefaultScale;
        if (!window_.create("Lazy-100", w, h))
            return false;
        if (!present_.init(window_))
            return false;
        reset_draw_pal();
        reset_transparent();

        // Built-in assets are linked into the binary; mount them in the in-memory VFS, then
        // load the font from there (runtime-rasterized). No loose files needed at runtime.
        vfs::init();
        if (!font::init())
            LZ_WARN("font not loaded; print() text will not render");

        audio_.init(); // non-fatal: sfx() is a no-op if the device can't start

        lua_.init(*this);
        new_cart();

        // Every power-on plays the splash. A supplied cart is loaded now but only started once
        // the splash ends (so its _init and music don't run under the chime); a bare boot drops
        // into the game browser (the shell stays one menu action away).
        boot_next_ = ConsoleMode::Explore;
        if (cart_path && load_cart_file(cart_path))
            boot_next_ = ConsoleMode::Running;
        else if (cart_path)
            LZ_WARN("cart failed to load: %s", cart_path);

        boot_t_ = 0.0;
        mode_   = ConsoleMode::Boot;
        // The chime is fired a beat into the splash (see the Boot case), not here: playing it
        // the instant the device starts races the backend's warm-up, which swallows the first
        // few hundred ms of output and can drop the whole (short) jingle on a cold start.

        LZ_INFO("console booted (%ux%u virtual, %ux%u window)", kScreenW, kScreenH, w, h);
        return true;
    }

    void Console::new_cart()
    {
        code_.clear();
        sheet_.clear();
        map_.clear();
        sounds_.clear();
        label_.clear();
        cartdata_id_.clear(); // the save slot belongs to the previous cart
        cartdata_.fill(0.0);
        has_cart_ = false;
    }

    namespace
    {
        // A save-slot id doubles as a filename: keep it strictly [a-z0-9_-], max 32 chars.
        std::string sanitize_cartdata_id(const std::string& id)
        {
            std::string out;
            for (const char ch : id)
            {
                const char c = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
                    out += c;
                if (out.size() >= 32)
                    break;
            }
            return out;
        }

        std::string cartdata_path(const std::string& id) { return "saves/" + id + ".txt"; }
    } // namespace

    bool Console::cartdata_open(const std::string& id)
    {
        const std::string clean = sanitize_cartdata_id(id);
        if (clean.empty())
        {
            LZ_WARN("cartdata: invalid id '%s'", id.c_str());
            return false;
        }
        cartdata_id_ = clean;
        cartdata_.fill(0.0);
        std::ifstream f(cartdata_path(clean));
        for (double& v : cartdata_)
            if (!(f >> v))
                break; // missing/short file: remaining slots stay 0
        return true;
    }

    double Console::cartdata_get(int index) const
    {
        if (!cartdata_active() || index < 0 || index >= static_cast<int>(cartdata_.size()))
            return 0.0;
        return cartdata_[static_cast<size_t>(index)];
    }

    void Console::cartdata_set(int index, double value)
    {
        if (!cartdata_active() || index < 0 || index >= static_cast<int>(cartdata_.size()))
            return;
        cartdata_[static_cast<size_t>(index)] = value;

        std::error_code ec;
        std::filesystem::create_directories("saves", ec);
        std::ofstream f(cartdata_path(cartdata_id_), std::ios::trunc);
        f.precision(17); // round-trip doubles exactly
        for (const double v : cartdata_)
            f << v << '\n';
        f.close();
        vfs::persist_flush(); // web: schedule the IndexedDB sync
    }

    void Console::render_first_frame_label(CartLabel& out)
    {
        if (code_.empty())
            return;
        LuaRuntime lua; // scratch VM bound to this console; the live lua_ stays untouched
        if (!lua.init(*this) || !lua.load_source(code_))
            return;
        lua.call_init();
        lua.call_draw();
        for (int y = 0; y < CartLabel::kH; ++y)
            for (int x = 0; x < CartLabel::kW; ++x)
                out.px[static_cast<size_t>(y) * CartLabel::kW + x] = framebuffer_.pget(x, y);
        out.present = true;
        // Undo the frame's side effects (the shell redraws the framebuffer itself).
        audio_.stop_music();
        reset_draw_pal();
        reset_transparent();
        framebuffer_.clip_reset();
        set_camera(0, 0);
        LZ_INFO("cart label rendered from the first frame");
    }

    void Console::capture_label()
    {
        // Snapshot the framebuffer at full 320x240 resolution (no downscale -> stays crisp).
        for (int y = 0; y < CartLabel::kH; ++y)
            for (int x = 0; x < CartLabel::kW; ++x)
                label_.px[static_cast<size_t>(y) * CartLabel::kW + x] = framebuffer_.pget(x, y);
        label_.present = true;
        LZ_INFO("cart label captured");
    }

    bool Console::load_cart_file(const std::string& path)
    {
        if (path.ends_with(".png")) // a cart PNG: extract the hidden .lz100 text, then parse it
        {
            std::string text;
            if (!cartpng::load(path, text))
                return false;
            cart::parse(text, code_, sheet_, map_, sounds_, label_);
            has_cart_ = !code_.empty();
            LZ_INFO("cart loaded (png): %s", path.c_str());
            return true;
        }
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            LZ_ERROR("cannot open cart %s", path.c_str());
            return false;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        cart::parse(ss.str(), code_, sheet_, map_, sounds_, label_);
        has_cart_ = !code_.empty();
        LZ_INFO("cart loaded: %s", path.c_str());
        return true;
    }

    bool Console::save_cart_file(const std::string& path)
    {
        if (cart_blank()) // nothing authored anywhere (code/gfx/map/sound): never write an empty file
        {
            LZ_WARN("save: refusing to write an empty cart (%s)", path.c_str());
            return false;
        }
        const std::filesystem::path p(path);
        std::error_code             ec;
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path(), ec);
        if (path.ends_with(".png")) // export as a cart PNG (visible cover + hidden cart data)
        {
            // Cover: the Ctrl+7 capture if there is one, else render the cart's first frame on
            // the spot (only if that fails too does cartpng fall back to the sprite sheet).
            CartLabel cover = label_;
            if (!cover.present)
                render_first_frame_label(cover);
            // The label is drawn on the cover but NOT hidden in the payload: a full-res screenshot
            // barely compresses and would make the PNG very tall. So embed the cart without it.
            if (!cartpng::save(path, cart::serialize(code_, sheet_, map_, sounds_, CartLabel {}), cover, sheet_,
                               palette_))
                return false;
            LZ_INFO("cart saved (png): %s", path.c_str());
            vfs::persist_flush();            // web: keep the save across reloads (IndexedDB)
            vfs::offer_download(path.c_str()); // web: also hand the .png to the browser as a file
            return true;
        }
        std::ofstream f(path, std::ios::binary);
        if (!f)
        {
            LZ_ERROR("cannot write cart %s", path.c_str());
            return false;
        }
        f << cart::serialize(code_, sheet_, map_, sounds_, label_);
        f.close();
        LZ_INFO("cart saved: %s", path.c_str());
        vfs::persist_flush();
        vfs::offer_download(path.c_str()); // web: download the .lz100
        return true;
    }

    bool Console::headless_shot(const std::string& cart_path, const std::string& out_png)
    {
        // No window/GPU/audio/input: only the CPU framebuffer, the palette, and the Lua VM.
        // audio_ is never init'd (play_sfx no-ops on a null device) and input reads all-false.
        vfs::init();
        if (!font::init())
            LZ_WARN("font not loaded; print() text will not render");
        reset_draw_pal();
        reset_transparent();
        lua_.init(*this);
        new_cart();
        if (!load_cart_file(cart_path))
            return false;
        if (!lua_.load_source(code_))
            return false;
        lua_.call_init();
        lua_.call_draw();

        std::vector<u8> rgba(static_cast<size_t>(kScreenW) * kScreenH * 4);
        for (u32 y = 0; y < kScreenH; ++y)
            for (u32 x = 0; x < kScreenW; ++x)
            {
                const Color32 c   = palette_.get(framebuffer_.pget(static_cast<int>(x), static_cast<int>(y)));
                u8*           dst = &rgba[(static_cast<size_t>(y) * kScreenW + x) * 4];
                dst[0]            = c.r;
                dst[1]            = c.g;
                dst[2]            = c.b;
                dst[3]            = 255;
            }
        return cartpng::write_rgba(out_png, static_cast<int>(kScreenW), static_cast<int>(kScreenH), rgba.data());
    }

    bool Console::start_cart()
    {
        if (code_.empty())
            return false;
        if (!lua_.load_source(code_)) // compile from the current source; errors are logged
            return false;
        set_camera(0, 0);          // per-cart draw state starts clean
        framebuffer_.clip_reset();
        has_cart_ = true;
        lua_.call_init();
        mode_ = ConsoleMode::Running;
        return true;
    }

    bool Console::tooltip_active(int id, bool over)
    {
        if (over)
        {
            if (hover_id_ != id)
            {
                hover_id_ = id;
                hover_t_  = 0.0;
            }
            else
                hover_t_ += dt_;
            return hover_t_ >= 0.5; // show after resting ~0.5s
        }
        if (hover_id_ == id) // moved off this hotspot
        {
            hover_id_ = -1;
            hover_t_  = 0.0;
        }
        return false;
    }

    bool Console::step_frame()
    {
        using clock    = std::chrono::steady_clock;
        const auto now = clock::now();
        double     dt  = std::chrono::duration<double>(now - run_prev_).count();
        run_prev_      = now;
        if (dt > 0.25)
            dt = 0.25; // clamp to dodge the spiral of death after a stall
        dt_ = dt;      // expose to UI (key-repeat / tooltip timing)

        window_.pump_events(running_);
        if (!running_)
            return false;
        keyboard_.update(window_, dt);
        mouse_.update(window_);
        input_.poll(); // live held state for btn()

        // Every mode handles ESC itself: the splash skips, everything else opens its
        // pause-style menu (see the cases below).
        switch (mode_)
        {
            case ConsoleMode::Boot:
            {
                run_acc_ = 0.0;
#if defined(__EMSCRIPTEN__)
                // Browsers can't autoplay audio: hold on a "click to start" prompt until the first
                // gesture (which resumes the AudioContext via Audio's DOM handler), then run boot
                // so the chime is actually heard. Don't fall through on the gesture frame, or the
                // same press would immediately skip the splash below.
                if (!web_started_)
                {
                    boot_warm_t_ += dt; // doubles as the prompt-blink timer
                    const bool gesture = !keyboard_.text().empty() || keyboard_.pressed(Keyboard::Return) ||
                                         keyboard_.pressed(Keyboard::Escape) || mouse_.pressed(Mouse::Left);
                    if (gesture)
                    {
                        web_started_ = true;
                        boot_warm_t_ = 0.0; // restart timing for the warm-up + splash
                    }
                    else
                        boot::draw_web_gate(framebuffer_, boot_warm_t_);
                    break;
                }
#endif
                const bool skip = keyboard_.pressed(Keyboard::Return) ||
                                  keyboard_.pressed(Keyboard::Escape) || !keyboard_.text().empty() ||
                                  mouse_.pressed(Mouse::Left);

                bool done = skip;
                if (!skip)
                {
                    // Hold the "booting..." screen until the audio warm-up has woken any
                    // power-saving speaker (e.g. Bluetooth), THEN reveal the rainbow splash and
                    // fire the chime together so the first sound isn't lost. Cap the wait: on the
                    // web the audio device stays suspended until a user gesture, so warming_up()
                    // would otherwise never clear and the splash would never appear.
                    constexpr double kWarmupMaxSecs = 2.0;
                    if (audio_.warming_up() && boot_warm_t_ < kWarmupMaxSecs)
                    {
                        boot_warm_t_ += dt;
                        boot::draw_warmup(framebuffer_, boot_warm_t_);
                        break;
                    }
                    if (!boot_jingle_)
                    {
                        audio_.play_sfx(boot::jingle(), 0);
                        boot_jingle_ = true;
                    }
                    boot_t_ += dt;
                    boot::draw(framebuffer_, boot_t_);
                    done = boot_t_ >= boot::kDuration;
                }
                if (done)
                {
                    // Hand off: start the pending cart (compile + _init, enters Running) or
                    // just switch modes. A cart that fails to compile falls back to the shell.
                    if (boot_next_ == ConsoleMode::Running)
                    {
                        if (!start_cart())
                            mode_ = ConsoleMode::Shell;
                    }
                    else
                        mode_ = boot_next_;
                }
                break;
            }
            case ConsoleMode::Running:
            {
                // ESC pauses the cart (music included) under a popup menu instead of leaving
                // immediately.
                if (keyboard_.pressed(Keyboard::Escape))
                {
                    if (pause_menu_.is_open())
                    {
                        pause_menu_.close();
                        audio_.pause_music(false);
                    }
                    else
                    {
                        pause_menu_.open(with_exit({"continue", "edit", "explore", "shell"}));
                        audio_.pause_music(true);
                    }
                }
                if (pause_menu_.is_open())
                {
                    // Cart frozen: no update/draw, so the last frame stays under the menu.
                    // Reset the cart's clip so the popup can't be scissored away by it.
                    framebuffer_.clip_reset();
                    switch (pause_menu_.update(keyboard_))
                    {
                        case 0: audio_.pause_music(false); break; // continue: resume the music
                        case 1: mode_ = ConsoleMode::Editor; break;
                        case 2: mode_ = ConsoleMode::Explore; break;
                        case 3: mode_ = ConsoleMode::Shell; break;
                        case 4: running_ = false; break;
                        default: break; // -1 = still open
                    }
                    if (mode_ != ConsoleMode::Running) // left the cart: silence its music
                        audio_.stop_music();
                    pause_menu_.draw(framebuffer_);
                    run_acc_ = 0.0; // no catch-up burst when resuming
                    break;
                }

                // 30 Hz, or 60 Hz if the cart defines _update60. Fixed logic step; render
                // follows vsync with 0..N catch-up steps per frame.
                const double step = lua_.wants_60hz() ? (1.0 / 60.0) : (1.0 / 30.0);
                run_acc_ += dt;
                while (run_acc_ >= step)
                {
                    input_.begin_step(); // edges + auto-repeat sampled per logic step
                    lua_.call_update();
                    input_.end_step();
                    run_acc_ -= step;
                }
                lua_.call_draw();
                if (keyboard_.ctrl() && keyboard_.pressed(Keyboard::Num7))
                    capture_label(); // Ctrl+7: snapshot this frame as the cart thumbnail
                break;
            }
            case ConsoleMode::Shell:
            {
                run_acc_ = 0.0;
                // ESC opens the shell's popup menu; "continue" stays right here in the shell.
                if (keyboard_.pressed(Keyboard::Escape))
                {
                    if (pause_menu_.is_open())
                        pause_menu_.close();
                    else
                        pause_menu_.open(with_exit({"continue", "edit", "explore"}));
                }
                if (pause_menu_.is_open())
                {
                    switch (pause_menu_.update(keyboard_))
                    {
                        case 1: mode_ = ConsoleMode::Editor; break;
                        case 2: mode_ = ConsoleMode::Explore; break;
                        case 3: running_ = false; break;
                        default: break; // 0 = continue, -1 = still open
                    }
                    pause_menu_.draw(framebuffer_);
                    break;
                }
                shell_.update(*this);
                shell_.draw(*this, framebuffer_);
                break;
            }
            case ConsoleMode::Editor:
            {
                run_acc_ = 0.0;
                // ESC opens the editor's popup menu (same component as the browser/cart menus);
                // while it is open the editors are frozen so menu keys can't leak into them.
                if (keyboard_.pressed(Keyboard::Escape))
                {
                    if (pause_menu_.is_open())
                        pause_menu_.close();
                    else
                        pause_menu_.open(with_exit({"continue", "run cart", "shell", "explore"}));
                }
                if (pause_menu_.is_open())
                {
                    switch (pause_menu_.update(keyboard_))
                    {
                        case 1: start_cart(); break; // no-op if there is no cart code
                        case 2: mode_ = ConsoleMode::Shell; break;
                        case 3: mode_ = ConsoleMode::Explore; break;
                        case 4: running_ = false; break;
                        default: break; // 0 = continue, -1 = still open
                    }
                    pause_menu_.draw(framebuffer_);
                    break;
                }
                editor_host_.update(*this);
                editor_host_.draw(*this, framebuffer_);
                break;
            }
            case ConsoleMode::Explore:
                run_acc_ = 0.0;
                explore_host_.update(*this);
                if (mode_ == ConsoleMode::Explore) // update may start a cart (mode -> Running)
                    explore_host_.draw(*this, framebuffer_);
                break;
        }

        // Software pixel cursor, drawn on top. Context-dependent in the editor/shell; the
        // running cart owns the screen, so we leave the cursor off there.
        if (mouse_.in_bounds())
        {
            if (mode_ == ConsoleMode::Editor)
                cursor::draw(framebuffer_, editor_host_.cursor(*this), mouse_.x(), mouse_.y());
            else if (mode_ == ConsoleMode::Shell || mode_ == ConsoleMode::Explore)
                cursor::draw(framebuffer_, cursor::Arrow, mouse_.x(), mouse_.y());
        }

        present_.submit_frame(framebuffer_, palette_);
        return running_;
    }

    void Console::frame_thunk(void* self)
    {
        auto* console = static_cast<Console*>(self);
        if (!console->step_frame())
        {
#if defined(__EMSCRIPTEN__)
            emscripten_cancel_main_loop();
#endif
        }
    }

    void Console::run()
    {
        run_prev_ = std::chrono::steady_clock::now();
        run_acc_  = 0.0;
#if defined(__EMSCRIPTEN__)
        // The browser owns the frame cadence. A blocking while-loop would never yield control
        // back (no canvas paint, no event pump, "page unresponsive"), so drive each frame from
        // requestAnimationFrame instead. fps 0 = use rAF; the final `true` simulates an infinite
        // loop, so run() does not return on the web (the frame callback keeps the app alive).
        emscripten_set_main_loop_arg(&Console::frame_thunk, this, 0, true);
#else
        while (running_)
            step_frame();
#endif
    }

    void Console::shutdown()
    {
        audio_.shutdown();
        font::shutdown();
        present_.shutdown();
        window_.destroy();
    }
} // namespace lazy100
