#include "lazy100/console/console.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/vfs/vfs.hpp"
#include "lazy100/video/font.hpp"

#include <chrono>

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

        if (cart_path && lua_.load_cart(cart_path))
        {
            has_cart_ = true;
            lua_.call_init();
            mode_ = ConsoleMode::Running; // launched with a cart -> play it
        }
        else
        {
            if (cart_path)
                LZ_WARN("cart failed to load: %s", cart_path);
            mode_ = ConsoleMode::Shell; // bare boot -> the command line
        }

        LZ_INFO("console booted (%ux%u virtual, %ux%u window)", kScreenW, kScreenH, w, h);
        return true;
    }

    bool Console::start_cart()
    {
        if (!has_cart_)
            return false;
        lua_.call_init(); // PICO-8 behavior: `run` re-inits
        mode_ = ConsoleMode::Running;
        return true;
    }

    void Console::run()
    {
        using clock = std::chrono::steady_clock;

        // Fixed logic step: 30 Hz, or 60 Hz if the cart defines _update60. Render follows
        // vsync; the accumulator runs 0..N catch-up steps per rendered frame.
        const double step = (has_cart_ && lua_.wants_60hz()) ? (1.0 / 60.0) : (1.0 / 30.0);
        auto         prev = clock::now();
        double       acc  = 0.0;

        while (running_)
        {
            const auto now = clock::now();
            double     dt  = std::chrono::duration<double>(now - prev).count();
            prev           = now;
            if (dt > 0.25)
                dt = 0.25; // clamp to dodge the spiral of death after a stall

            window_.pump_events(running_);
            if (!running_)
                break;
            keyboard_.update(window_);
            mouse_.update(window_);
            input_.poll(); // live held state for btn()

            // ESC: Running -> Editor, else toggle Shell <-> Editor.
            if (keyboard_.pressed(Keyboard::Escape))
                mode_ = (mode_ == ConsoleMode::Shell) ? ConsoleMode::Editor
                        : (mode_ == ConsoleMode::Editor) ? ConsoleMode::Shell
                                                         : ConsoleMode::Editor;

            switch (mode_)
            {
                case ConsoleMode::Running:
                    acc += dt;
                    while (acc >= step)
                    {
                        input_.begin_step(); // edges + auto-repeat sampled per logic step
                        if (has_cart_)
                            lua_.call_update();
                        input_.end_step();
                        acc -= step;
                    }
                    if (has_cart_)
                        lua_.call_draw();
                    break;
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
