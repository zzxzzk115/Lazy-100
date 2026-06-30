#include "lazy100/console/console.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/console/config.hpp"

namespace lazy100
{
    namespace
    {
        // M1 still-frame: 32 vertical palette bars (left -> right = index 0..31), a white
        // strip along the TOP edge and a red strip down the LEFT edge. Together these make
        // the image orientation unambiguous (verifies the present Y-flip / scaling) and show
        // every palette color. Replaced by Lua _draw output in M2.
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

    bool Console::boot()
    {
        const u32 w = kScreenW * kDefaultScale;
        const u32 h = kScreenH * kDefaultScale;
        if (!window_.create("Lazy-100", w, h))
            return false;
        if (!present_.init(window_))
            return false;
        draw_test_pattern(framebuffer_);
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
            present_.submit_frame(framebuffer_, palette_);
        }
    }

    void Console::shutdown()
    {
        present_.shutdown();
        window_.destroy();
    }
} // namespace lazy100
