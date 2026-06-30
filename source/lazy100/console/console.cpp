#include "lazy100/console/console.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/console/config.hpp"

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

    bool Console::boot(const char* cart_path)
    {
        const u32 w = kScreenW * kDefaultScale;
        const u32 h = kScreenH * kDefaultScale;
        if (!window_.create("Lazy-100", w, h))
            return false;
        if (!present_.init(window_))
            return false;
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
        while (running_)
        {
            window_.pump_events(running_);
            if (!running_)
                break;
            if (has_cart_)
                lua_.call_draw(); // M2: cart redraws each frame (fixed timestep + _update land in M3)
            present_.submit_frame(framebuffer_, palette_);
        }
    }

    void Console::shutdown()
    {
        present_.shutdown();
        window_.destroy();
    }
} // namespace lazy100
