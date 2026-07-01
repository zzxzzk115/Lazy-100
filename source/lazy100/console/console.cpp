#include "lazy100/console/console.hpp"

#include "lazy100/cart/cart.hpp"
#include "lazy100/common/log.hpp"
#include "lazy100/console/boot.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/vfs/vfs.hpp"
#include "lazy100/video/cursor.hpp"
#include "lazy100/video/font.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

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

        if (cart_path && load_cart_file(cart_path))
            start_cart(); // launched with a cart -> play it straight away (no splash)
        else
        {
            if (cart_path)
                LZ_WARN("cart failed to load: %s", cart_path);
            // Bare boot -> power-on splash, then drop to the command line.
            boot_next_ = ConsoleMode::Shell;
            boot_t_    = 0.0;
            mode_      = ConsoleMode::Boot;
            audio_.play_sfx(boot::jingle(), 0);
        }

        LZ_INFO("console booted (%ux%u virtual, %ux%u window)", kScreenW, kScreenH, w, h);
        return true;
    }

    void Console::new_cart()
    {
        code_.clear();
        sheet_.clear();
        map_.clear();
        sounds_.clear();
        has_cart_ = false;
    }

    bool Console::load_cart_file(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            LZ_ERROR("cannot open cart %s", path.c_str());
            return false;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        cart::parse(ss.str(), code_, sheet_, map_, sounds_);
        has_cart_ = !code_.empty();
        LZ_INFO("cart loaded: %s", path.c_str());
        return true;
    }

    bool Console::save_cart_file(const std::string& path)
    {
        const std::filesystem::path p(path);
        std::error_code             ec;
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f)
        {
            LZ_ERROR("cannot write cart %s", path.c_str());
            return false;
        }
        f << cart::serialize(code_, sheet_, map_, sounds_);
        LZ_INFO("cart saved: %s", path.c_str());
        return true;
    }

    bool Console::start_cart()
    {
        if (code_.empty())
            return false;
        if (!lua_.load_source(code_)) // compile from the current source; errors are logged
            return false;
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

    void Console::run()
    {
        using clock = std::chrono::steady_clock;
        auto   prev = clock::now();
        double acc  = 0.0;

        while (running_)
        {
            const auto now = clock::now();
            double     dt  = std::chrono::duration<double>(now - prev).count();
            prev           = now;
            if (dt > 0.25)
                dt = 0.25; // clamp to dodge the spiral of death after a stall
            dt_ = dt;      // expose to UI (key-repeat / tooltip timing)

            window_.pump_events(running_);
            if (!running_)
                break;
            keyboard_.update(window_, dt);
            mouse_.update(window_);
            input_.poll(); // live held state for btn()

            // ESC: Running -> Editor, else toggle Shell <-> Editor. (The splash handles its own
            // keys below, so don't let ESC hijack it here.)
            if (mode_ != ConsoleMode::Boot && keyboard_.pressed(Keyboard::Escape))
            {
                const ConsoleMode prev = mode_;
                mode_                   = (mode_ == ConsoleMode::Shell) ? ConsoleMode::Editor
                                          : (mode_ == ConsoleMode::Editor) ? ConsoleMode::Shell
                                                                           : ConsoleMode::Editor;
                if (prev == ConsoleMode::Running) // leaving a running cart: silence its music
                    audio_.stop_music();
            }

            switch (mode_)
            {
                case ConsoleMode::Boot:
                {
                    acc = 0.0;
                    boot_t_ += dt;
                    boot::draw(framebuffer_, boot_t_);
                    const bool skip = keyboard_.pressed(Keyboard::Return) ||
                                      keyboard_.pressed(Keyboard::Escape) || !keyboard_.text().empty() ||
                                      mouse_.pressed(Mouse::Left);
                    if (boot_t_ >= boot::kDuration || skip)
                        mode_ = boot_next_;
                    break;
                }
                case ConsoleMode::Running:
                {
                    // 30 Hz, or 60 Hz if the cart defines _update60. Fixed logic step; render
                    // follows vsync with 0..N catch-up steps per frame.
                    const double step = lua_.wants_60hz() ? (1.0 / 60.0) : (1.0 / 30.0);
                    acc += dt;
                    while (acc >= step)
                    {
                        input_.begin_step(); // edges + auto-repeat sampled per logic step
                        lua_.call_update();
                        input_.end_step();
                        acc -= step;
                    }
                    lua_.call_draw();
                    break;
                }
                case ConsoleMode::Shell:
                    acc = 0.0;
                    shell_.update(*this);
                    shell_.draw(*this, framebuffer_);
                    break;
                case ConsoleMode::Editor:
                    acc = 0.0;
                    editor_host_.update(*this);
                    editor_host_.draw(*this, framebuffer_);
                    break;
            }

            // Software pixel cursor, drawn on top. Context-dependent in the editor/shell; the
            // running cart owns the screen, so we leave the cursor off there.
            if (mouse_.in_bounds())
            {
                if (mode_ == ConsoleMode::Editor)
                    cursor::draw(framebuffer_, editor_host_.cursor(*this), mouse_.x(), mouse_.y());
                else if (mode_ == ConsoleMode::Shell)
                    cursor::draw(framebuffer_, cursor::Arrow, mouse_.x(), mouse_.y());
            }

            present_.submit_frame(framebuffer_, palette_);
        }
    }

    void Console::shutdown()
    {
        audio_.shutdown();
        font::shutdown();
        present_.shutdown();
        window_.destroy();
    }
} // namespace lazy100
