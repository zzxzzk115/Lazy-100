#include "lazy100/console/console.hpp"

#include "lazy100/cart/cart.hpp"
#include "lazy100/cart/cartpng.hpp"
#include "lazy100/cart/p8.hpp"
#include "lazy100/common/log.hpp"
#include "lazy100/console/boot.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/vfs/persist.hpp"
#include "lazy100/vfs/vfs.hpp"
#include "lazy100/video/cursor.hpp"
#include "lazy100/video/font.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <thread>
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
        p8vm_.init(*this);
        new_cart();

        // Persistent author identity (git-config-like); default author when exporting a cart.
        if (std::ifstream af("saves/author.txt"); af)
        {
            std::getline(af, user_author_);
            while (!user_author_.empty() && (user_author_.back() == '\r' || user_author_.back() == '\n'))
                user_author_.pop_back();
        }

        // Every power-on plays the splash. A supplied cart is loaded now but only started once
        // the splash ends (so its _init and music don't run under the chime); a bare boot drops
        // into the game browser (the shell stays one menu action away).
        // Desktop drops into the game browser; the web build embeds in the site (which owns cart
        // discovery via its own Carts page), so it lands on the shell prompt instead.
#if defined(__EMSCRIPTEN__)
        boot_next_ = ConsoleMode::Shell;
#else
        boot_next_ = ConsoleMode::Explore;
#endif
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
        cart_meta_ = {};
        cartdata_id_.clear(); // the save slot belongs to the previous cart
        cartdata_.fill(0.0);
        cart_path_.clear();
        p8ram_.clear();
        p8_raw_code_.clear();
        lang_p8_  = false;
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

    void Console::set_user_author(const std::string& a)
    {
        user_author_ = a;
        std::error_code ec;
        std::filesystem::create_directories("saves", ec);
        std::ofstream f("saves/author.txt", std::ios::trunc);
        f << a << '\n';
        f.close();
        vfs::persist_flush(); // web: keep the author across reloads (IndexedDB)
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
        if (path.ends_with(".p8")) // classic text cart: import through the p8 ext layer
        {
            std::ifstream f(path, std::ios::binary);
            if (!f)
            {
                LZ_ERROR("cannot open cart %s", path.c_str());
                return false;
            }
            std::stringstream ss;
            ss << f.rdbuf();
            if (!p8::import_text(ss.str(), code_, sheet_, map_, sounds_, &p8ram_, &p8_raw_code_))
                return false;
            p8ram_.resize(0x10000, 0); // full 64KB address space; upper half survives load()
            label_.clear();
            has_cart_ = !code_.empty();
            cart_path_ = path;
            lang_p8_   = std::getenv("LZ100_TRANSPILE") == nullptr; // native z8lua by default
            LZ_INFO("cart loaded (p8 %s): %s", lang_p8_ ? "z8lua" : "transpiled", path.c_str());
            return true;
        }
        if (path.ends_with(".png") && p8::is_png_cart(path)) // 160x205 shareable p8 cart
        {
            if (!p8::import_png(path, code_, sheet_, map_, sounds_, &p8ram_, &p8_raw_code_))
                return false;
            p8ram_.resize(0x10000, 0); // full 64KB address space; upper half survives load()
            label_.clear();
            has_cart_ = !code_.empty();
            cart_path_ = path;
            lang_p8_   = std::getenv("LZ100_TRANSPILE") == nullptr;
            LZ_INFO("cart loaded (p8 png, %s): %s", lang_p8_ ? "z8lua" : "transpiled", path.c_str());
            return true;
        }
        p8ram_.clear(); // native cart: peek/poke go inert
        lang_p8_ = false;
        if (path.ends_with(".png")) // a cart PNG: extract the hidden .lz100 text, then parse it
        {
            std::string text;
            if (!cartpng::load(path, text))
                return false;
            cart::parse(text, code_, sheet_, map_, sounds_, label_, cart_meta_);
            has_cart_ = !code_.empty();
            cart_path_ = path;
            detect_language();
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
        cart::parse(ss.str(), code_, sheet_, map_, sounds_, label_, cart_meta_);
        has_cart_ = !code_.empty();
        cart_path_ = path;
        detect_language();
        LZ_INFO("cart loaded: %s", path.c_str());
        return true;
    }

    void Console::detect_language()
    {
        // A native (.lz100/.lua) cart may opt into the p8 language for superset play: the
        // p8 dialect + our full 320x240 API. `--language:p8` anywhere in the source selects it;
        // `--language:lz100` (or nothing) keeps the native Lua 5.4 VM.
        const auto has = [&](const char* tag)
        {
            const std::string t = tag;
            size_t            i = code_.find(t);
            return i != std::string::npos;
        };
        if (has("--language:p8") || has("--language: p8"))
        {
            lang_p8_      = true;
            p8_raw_code_  = code_;
            if (p8ram_.empty())
                p8ram_.assign(0x10000, 0); // superset p8 carts still get addressable RAM
        }
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
            // Footer title/author: the cart's own metadata; the title falls back to the filename.
            std::string title = cart_meta_.title;
            if (title.empty())
            {
                title           = std::filesystem::path(path).filename().string();
                const auto dot  = title.find('.');
                if (dot != std::string::npos)
                    title = title.substr(0, dot);
            }
            // The label is drawn on the cover but NOT hidden in the payload: a full-res screenshot
            // barely compresses and would make the PNG very tall. So embed the cart without it.
            if (!cartpng::save(path, cart::serialize(code_, sheet_, map_, sounds_, CartLabel {}, cart_meta_), cover,
                               sheet_, palette_, title, cart_meta_.author))
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
        f << cart::serialize(code_, sheet_, map_, sounds_, label_, cart_meta_);
        f.close();
        LZ_INFO("cart saved: %s", path.c_str());
        vfs::persist_flush();
        vfs::offer_download(path.c_str()); // web: download the .lz100
        return true;
    }

    bool Console::headless_pack(const std::string& cart_path, const std::string& out_png)
    {
        // Same minimal init as headless_shot (no window/GPU/audio/input), then export a cart
        // PNG: save_cart_file renders the first-frame cover (render_first_frame_label) itself.
        vfs::init();
        if (!font::init())
            LZ_WARN("font not loaded; cart label may be blank");
        reset_draw_pal();
        reset_transparent();
        lua_.init(*this);
        p8vm_.init(*this);
        new_cart();
        if (!load_cart_file(cart_path))
            return false;
        return save_cart_file(out_png);
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
        p8vm_.init(*this);
        new_cart();
        if (!load_cart_file(cart_path))
            return false;
        if (lang_p8_)
        {
            if (!p8vm_.load_source(p8_raw_code_))
                return false;
        }
        else if (!lua_.load_source(code_))
            return false;
        cart_init();
        // A loader cart may request another cart (multi-cart games): resolve the chain here,
        // waiting on downloads, so the shot shows the real game rather than a loading screen.
        for (int guard = 0; guard < 600 && load_pending(); ++guard) // <= ~30 s of downloading
        {
            process_pending_load();
            if (load_fetching())
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        // LZ100_FRAMES=N runs N update+draw frames before the shot (dev aid: many carts show
        // a splash/menu on frame 1; multi-cart loaders need a few frames to resolve the swap).
        int frames = 1;
        if (const char* fenv = std::getenv("LZ100_FRAMES"))
            frames = std::max(1, std::atoi(fenv));
        // LZ100_KEYS scripts per-frame gamepad input for headless flight/menu testing: one char
        // per logic frame from l/r/u/d/o/x (any others = no buttons), driving btn/btnp via the
        // begin_step/end_step edge machinery. Held is latched at the last char once exhausted.
        const char* keys    = std::getenv("LZ100_KEYS");
        const size_t keylen = keys ? std::strlen(keys) : 0;
        auto keymask = [](char ch) -> u32 {
            switch (ch)
            {
                case 'l': return 1u << Input::Left;
                case 'r': return 1u << Input::Right;
                case 'u': return 1u << Input::Up;
                case 'd': return 1u << Input::Down;
                case 'o': return 1u << Input::O;
                case 'x': return 1u << Input::X;
                default: return 0u;
            }
        };
        for (int f = 0; f < frames; ++f)
        {
            if (load_pending())
                process_pending_load();
            if (keys)
            {
                input_.inject(keymask(keys[std::min<size_t>(f, keylen ? keylen - 1 : 0)]));
                input_.begin_step();
                cart_update();
                input_.end_step();
            }
            else
                cart_update(); // the frame order is update-then-draw; carts rely on it
            cart_draw();
        }

        mode_                = ConsoleMode::Running;  // so present_framebuffer() bakes the screen palette
        Framebuffer&    disp = present_framebuffer(); // bakes the p8 screen palette into the viewport
        std::vector<u8> rgba(static_cast<size_t>(kScreenW) * kScreenH * 4);
        for (u32 y = 0; y < kScreenH; ++y)
            for (u32 x = 0; x < kScreenW; ++x)
            {
                const Color32 c   = palette_.get(disp.pget(static_cast<int>(x), static_cast<int>(y)));
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
        if (lang_p8_ ? p8_raw_code_.empty() : code_.empty())
            return false;
        clear_last_error(); // a fresh run starts clean; a failed load below re-sets it
        error_halt_ = false;
        if (lang_p8_)
        {
            if (!p8vm_.load_source(p8_raw_code_)) // z8lua runs the dialect directly
                return false;
        }
        else if (!lua_.load_source(code_)) // native: compile from source; errors are logged
            return false;
        set_camera(0, 0);          // per-cart draw state starts clean
        framebuffer_.clip_reset();
        framebuffer_.fillp_reset();
        // NOTE: the screen palette is reset inside p8vm_.load_source (above), BEFORE the cart's
        // top-level code runs - so a cart that sets its palette at load time (marble_merger) keeps
        // it. Resetting here would wipe that, which is why it is deliberately not done here.
        has_cart_ = true;
        cart_init();
        mode_ = ConsoleMode::Running;
        return true;
    }

    bool Console::restart_with_cart(const std::string& path)
    {
        if (!load_cart_file(path))
            return false;
        // Silence the outgoing cart: without this its music/sfx keep playing through the boot splash
        // and into the new cart until the new cart happens to touch those channels.
        audio_.stop_music();
        audio_.stop_sfx(-1);
        // Replay the power-on splash, then start the cart (the Boot case hands off to start_cart
        // when boot_next_ is Running). web_started_ is forced: the JS call already came from a
        // user click, so the audio is unlocked and we skip the "click to start" gate.
        boot_next_   = ConsoleMode::Running;
        web_started_ = true;
        boot_jingle_ = false;
        boot_t_      = 0.0;
        boot_warm_t_ = 0.0;
        mode_        = ConsoleMode::Boot;
        return true;
    }

    void Console::pause_from_web()
    {
        if (mode_ != ConsoleMode::Running || pause_menu_.is_open())
            return; // nothing to pause, or a menu is already up (never toggle it closed)
        // Same menu the Running-mode ESC opens (kiosk hides the developer items).
        pause_menu_.open(with_exit(kiosk_ ? std::vector<std::string> {"continue", "reset cart"}
                                          : std::vector<std::string> {
                                                "continue", "reset cart", "edit", "explore", "shell"}));
        audio_.pause_music(true);
    }

    // love2d-style crash screen: everything rendered so far is wiped for a blue background with
    // the white core error message and a hint back to the editor. Drawn every frame while halted.
    void Console::draw_error_screen()
    {
        set_camera(0, 0);
        framebuffer_.clip_reset();
        framebuffer_.fillp_reset();
        const int W  = static_cast<int>(kScreenW);
        const int H  = static_cast<int>(kScreenH);
        const int lh = font::line_height();
        framebuffer_.rectfill(0, 0, W - 1, H - 1, 12); // the classic blue
        int y = 16;
        font::print(framebuffer_, "cart error", 12, y, 7);
        y += lh * 2;

        // Core message: first line only, Lua's chunk prefix trimmed to "line N: ...".
        std::string msg = last_error_;
        if (const size_t nl = msg.find('\n'); nl != std::string::npos)
            msg = msg.substr(0, nl);
        if (const size_t p = msg.find("]:");
            p != std::string::npos && p + 2 < msg.size() && msg[p + 2] >= '0' && msg[p + 2] <= '9')
            msg = "line " + msg.substr(p + 2);

        // Greedy word-wrap into the safe width.
        const int   maxW = W - 24;
        std::string line;
        size_t      i = 0;
        while (i <= msg.size() && y < H - lh * 3)
        {
            const size_t      sp   = msg.find(' ', i);
            const std::string word = msg.substr(i, (sp == std::string::npos ? msg.size() : sp) - i);
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && font::text_width(trial.c_str()) > maxW)
            {
                font::print(framebuffer_, line.c_str(), 12, y, 7);
                y += lh + 2;
                line = word;
            }
            else
                line = trial;
            if (sp == std::string::npos)
                break;
            i = sp + 1;
        }
        if (!line.empty() && y < H - lh * 2)
            font::print(framebuffer_, line.c_str(), 12, y, 7);

        font::print(framebuffer_, "press ESC to edit and fix it", 12, H - lh - 12, 1);
    }

    bool Console::arm_cart(const std::string& path)
    {
        if (!load_cart_file(path))
            return false;
        audio_.stop_music(); // silence any previously-running cart before this one boots
        audio_.stop_sfx(-1);
        boot_next_ = ConsoleMode::Running; // what the boot gate hands off to
        // Still on the "press a key to start" gate: leave it there so the gesture that dismisses
        // it also unlocks and warms up the audio, then the gate starts this cart. If the gate is
        // already past (e.g. the console dropped to the shell), replay the splash to run it now.
        if (mode_ != ConsoleMode::Boot)
        {
            web_started_ = true;
            boot_jingle_ = false;
            boot_t_      = 0.0;
            boot_warm_t_ = 0.0;
            mode_        = ConsoleMode::Boot;
        }
        return true;
    }

    // ---- VM dispatch: the active language picks the z8lua p8 VM or the native Lua VM ----
    void Console::cart_init()
    {
        if (lang_p8_)
            p8vm_.call_init();
        else
            lua_.call_init();
    }
    void Console::cart_update()
    {
        if (lang_p8_)
            p8vm_.call_update();
        else
            lua_.call_update();
    }
    void Console::cart_draw()
    {
        if (lang_p8_)
            p8vm_.call_draw();
        else
            lua_.call_draw();
    }
    bool Console::cart_wants_60hz() const
    {
        return lang_p8_ ? p8vm_.wants_60hz() : lua_.wants_60hz();
    }

    // The framebuffer to present. When a running p8 cart has set a screen palette, bake that
    // index->index remap into a scratch copy over the 128x128 viewport only, so the game's custom
    // palette recolors its own screen without repainting our chrome/shell (which draw outside it).
    // Every other case presents the live framebuffer untouched (no per-frame copy).
    Framebuffer& Console::present_framebuffer()
    {
        // The error screen must keep true palette colors — skip the cart's screen-palette bake.
        if (!(lang_p8_ && mode_ == ConsoleMode::Running && screen_pal_active_) || !last_error_.empty())
            return framebuffer_;

        present_fb_       = framebuffer_; // pixels + clip state; we bake straight into the raw buffer
        u8*        px     = present_fb_.pixels_mut();
        const auto W      = Framebuffer::width();
        constexpr int OX = 96, OY = 56; // the centered 128x128 viewport (matches P8Vm)
        for (int y = OY; y < OY + 128; ++y)
        {
            u8* row = px + static_cast<size_t>(y) * W;
            for (int x = OX; x < OX + 128; ++x)
                row[x] = screen_pal_[row[x] & 0x0f];
        }
        return present_fb_;
    }

    void Console::process_pending_load()
    {
        namespace fs = std::filesystem;
        if (pending_load_.empty())
            return;

        // Finish (or fail) an in-flight BBS download first.
        if (load_fetching_)
        {
            if (!load_fetch_.done())
                return; // still downloading: the caller keeps the current frame frozen
            load_fetching_ = false;
            if (!load_fetch_.ok())
            {
#if defined(__EMSCRIPTEN__)
                // Browsers block the BBS host (no CORS headers). Retry once through a public
                // CORS proxy before giving up.
                if (!load_proxy_tried_ && !pending_load_.empty() && pending_load_[0] == '#')
                {
                    load_proxy_tried_ = true;
                    const std::string id = pending_load_.substr(1);
                    std::string       direct =
                        "https://www.lexaloffle.com/bbs/get_cart.php?cat=7&play_src=2&lid=" + id;
                    std::string enc;
                    for (const char c : direct)
                    {
                        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
                            enc += c;
                        else
                        {
                            char buf[4];
                            std::snprintf(buf, sizeof(buf), "%%%02X",
                                          static_cast<unsigned char>(c));
                            enc += buf;
                        }
                    }
                    LZ_WARN("load %s: direct fetch blocked (browser CORS); retrying via proxy",
                            pending_load_.c_str());
                    load_fetch_.start("https://api.allorigins.win/raw?url=" + enc);
                    load_fetching_ = true;
                    return;
                }
                LZ_ERROR("load %s: download failed (%s). Browsers block this host; upload the "
                         "cart into carts/bbs/ instead",
                         pending_load_.c_str(), load_fetch_.error().c_str());
#else
                LZ_ERROR("load %s: download failed (%s)", pending_load_.c_str(),
                         load_fetch_.error().c_str());
#endif
                pending_load_.clear();
                return;
            }
            std::error_code ec;
            fs::create_directories(fs::path(load_cache_).parent_path(), ec);
            std::ofstream f(load_cache_, std::ios::binary);
            f.write(reinterpret_cast<const char*>(load_fetch_.data().data()),
                    static_cast<std::streamsize>(load_fetch_.data().size()));
            f.close();
            vfs::persist_flush(); // web: the downloaded cart survives reloads (IndexedDB)
        }
        else if (!pending_load_.empty() && pending_load_[0] == '#')
        {
            // BBS cart: use the local cache when present, else fetch it.
            const std::string id = pending_load_.substr(1);
            load_cache_          = "carts/bbs/" + id + ".p8.png";
            if (!fs::exists(load_cache_))
            {
                LZ_INFO("load %s: fetching from the BBS", pending_load_.c_str());
                load_fetch_.start("https://www.lexaloffle.com/bbs/get_cart.php?cat=7&play_src=2&lid=" + id);
                load_fetching_ = true;
                return;
            }
        }
        else
        {
            // Local cart: as given, beside the current cart, with the usual extensions.
            std::vector<std::string> cand = {pending_load_};
            if (!cart_path_.empty())
                cand.push_back((fs::path(cart_path_).parent_path() / pending_load_).string());
            for (const std::string& base : std::vector<std::string>(cand))
                for (const char* ext : {".p8", ".p8.png", ".lz100", ".lz100.png"})
                    cand.push_back(base + ext);
            load_cache_.clear();
            for (const std::string& c : cand)
                if (fs::exists(c))
                {
                    load_cache_ = c;
                    break;
                }
            // Back-navigation fallback: multi-cart games load their launcher by its canonical
            // name, but the file on disk may carry a browser suffix ("name-1.p8.png").
            if (load_cache_.empty() && !prev_cart_path_.empty())
            {
                const std::string want = fs::path(pending_load_).stem().string();
                const std::string have = fs::path(prev_cart_path_).filename().string();
                if (!want.empty() && have.rfind(want, 0) == 0)
                    load_cache_ = prev_cart_path_;
            }
            if (load_cache_.empty())
            {
                LZ_ERROR("load %s: no such cart", pending_load_.c_str());
                pending_load_.clear();
                return;
            }
        }

        // Swap: the upper 32KB of p8 RAM carries over (multi-cart games pass data there).
        LZ_INFO("load %s -> %s", pending_load_.c_str(), load_cache_.c_str());
        pending_load_.clear();
        prev_cart_path_ = cart_path_;
        std::vector<u8> himem;
        if (p8ram_.size() >= 0x10000)
            himem.assign(p8ram_.begin() + 0x8000, p8ram_.end());
        audio_.stop_music();
        audio_.stop_sfx(-1);
        if (load_cart_file(load_cache_))
        {
            if (!himem.empty() && p8ram_.size() >= 0x10000)
                std::copy(himem.begin(), himem.end(), p8ram_.begin() + 0x8000);
            if (!start_cart())
                mode_ = ConsoleMode::Shell;
        }
        else
            mode_ = ConsoleMode::Shell;
    }

    bool Console::reset_cart()
    {
        audio_.stop_music();
        audio_.stop_sfx(-1);
        // A file-born cart re-imports so poked assets (and a p8 cart's RAM) go back to the
        // pristine ROM; an editor-born cart just re-runs its current sources.
        if (!cart_path_.empty())
            load_cart_file(cart_path_);
        return start_cart();
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
                                         keyboard_.pressed(Keyboard::Escape) || mouse_.pressed(Mouse::Left) ||
                                         input_.held_mask() != 0; // the web virtual gamepad also starts it
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
                // A p8 load() request freezes the cart on its last frame (loader carts draw
                // their own "loading" screen) while the swap resolves/downloads.
                if (load_pending())
                {
                    process_pending_load();
                    break;
                }
                // A script error halts the cart on a love2d-style error screen (blue background,
                // white message) instead of erroring on every frame, until the user jumps to the
                // code editor to fix it (which shows the same error inline).
                if (!last_error_.empty())
                {
                    if (!error_halt_) // entering the error state: silence the crashed cart once
                    {
                        error_halt_ = true;
                        pause_menu_.close();
                        audio_.stop_music();
                        audio_.stop_sfx(-1);
                    }
                    draw_error_screen();
                    if (keyboard_.pressed(Keyboard::Escape))
                        mode_ = ConsoleMode::Editor;
                    break;
                }
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
                        // Kiosk (home) hides the developer items; indices 0/1 stay put so the
                        // continue/reset cases below are unaffected.
                        pause_menu_.open(with_exit(kiosk_
                            ? std::vector<std::string>{"continue", "reset cart"}
                            : std::vector<std::string>{"continue", "reset cart", "edit", "explore", "shell"}));
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
                        case 1:                                   // reset cart: restart from scratch
                            if (!reset_cart())
                                mode_ = ConsoleMode::Shell;
                            break;
                        case 2: mode_ = ConsoleMode::Editor; break;
                        case 3: mode_ = ConsoleMode::Explore; break;
                        case 4: mode_ = ConsoleMode::Shell; break;
                        case 5: running_ = false; break;
                        default: break; // -1 = still open
                    }
                    if (mode_ != ConsoleMode::Running) // left the cart: silence its music
                        audio_.stop_music();
                    run_acc_ = 0.0; // no catch-up burst when resuming
                    break;         // menu is composited after the screen-palette bake, below
                }

                // 30 Hz, or 60 Hz if the cart defines _update60. Fixed logic step; render
                // follows vsync with 0..N catch-up steps per frame.
                const double step = cart_wants_60hz() ? (1.0 / 60.0) : (1.0 / 30.0);
                run_acc_ += dt;
                while (run_acc_ >= step)
                {
                    input_.begin_step(); // edges + auto-repeat sampled per logic step
                    cart_update();
                    input_.end_step();
                    run_acc_ -= step;
                }
                cart_draw();
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
                    break; // menu composited after the bake, below
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
                // The active editor gets first crack (e.g. the code editor closes its cheatsheet).
                if (keyboard_.pressed(Keyboard::Escape) && !editor_host_.on_escape(*this))
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
                    break; // menu composited after the bake, below
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

        // Bake the p8 screen palette into the cart's 128x128 viewport (identity no-op in every
        // other case), THEN composite the console's own UI on top - the pause menu and cursor
        // must keep the shell palette, not be recolored by the running cart's screen palette.
        Framebuffer& disp = present_framebuffer();

        if (pause_menu_.is_open())
            pause_menu_.draw(disp);

        // Software pixel cursor, drawn on top. Context-dependent in the editor/shell; the
        // running cart owns the screen, so we leave the cursor off there. The pause menu is
        // keyboard-driven, so the cursor stays hidden while it's up.
        if (mouse_.in_bounds() && !pause_menu_.is_open())
        {
            if (mode_ == ConsoleMode::Editor)
                cursor::draw(disp, editor_host_.cursor(*this), mouse_.x(), mouse_.y());
            else if (mode_ == ConsoleMode::Shell || mode_ == ConsoleMode::Explore)
                cursor::draw(disp, cursor::Arrow, mouse_.x(), mouse_.y());
        }

        present_.submit_frame(disp, palette_);
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
