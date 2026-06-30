#include "lazy100/console/console.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/video/font.hpp"

#include <chrono>

namespace lazy100
{
    namespace
    {
        // Fallback still-frame when no cart is given: palette bars + top/left edge markers,
        // so a bare `lazy100` run still shows that the present path works.
        void draw_test_pattern(Framebuffer& fb)
        {
            fb.cls(0);
            const int bar = static_cast<int>(kScreenW) / static_cast<int>(kPaletteSize);
            for (u32 i = 0; i < kPaletteSize; ++i)
                fb.rectfill(static_cast<int>(i) * bar, 0, static_cast<int>(i) * bar + bar - 1,
                            static_cast<int>(kScreenH) - 1, static_cast<u8>(i));
            fb.rectfill(0, 0, static_cast<int>(kScreenW) - 1, 7, 7); // top edge: white
            fb.rectfill(0, 0, 7, static_cast<int>(kScreenH) - 1, 8); // left edge: red
        }
    } // namespace

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

        // Built-in font (runtime-rasterized). Try the project-relative path (xmake run) first.
        static const char* const kFontPaths[] = {
            "assets/fonts/fusion-pixel-10px-proportional-zh_hans.ttf",
            "../assets/fonts/fusion-pixel-10px-proportional-zh_hans.ttf",
        };
        bool font_ok = false;
        for (const char* path : kFontPaths)
            if (font::init(path))
            {
                font_ok = true;
                break;
            }
        if (!font_ok)
            LZ_WARN("font not loaded; print() text will not render");

        lua_.init(*this);

        if (cart_path && lua_.load_cart(cart_path))
        {
            has_cart_ = true;
            lua_.call_init();
        }
        else
        {
            if (cart_path)
                LZ_WARN("falling back to test pattern (cart failed to load)");
            draw_test_pattern(framebuffer_);
        }

        LZ_INFO("console booted (%ux%u virtual, %ux%u window)", kScreenW, kScreenH, w, h);
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
            input_.poll(); // live held state for btn()

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
            present_.submit_frame(framebuffer_, palette_);
        }
    }

    void Console::shutdown()
    {
        font::shutdown();
        present_.shutdown();
        window_.destroy();
    }
} // namespace lazy100
