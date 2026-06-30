#include "lazy100/console/console.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/console/config.hpp"

namespace lazy100
{
    bool Console::boot()
    {
        const u32 w = kScreenW * kDefaultScale;
        const u32 h = kScreenH * kDefaultScale;
        if (!window_.create("Lazy-100", w, h))
            return false;
        if (!present_.init(window_))
            return false;
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
            // M0: a solid dark clear proves the boot/present backbone. The 320x240
            // framebuffer arrives in M1.
            present_.present_clear(0.05f, 0.06f, 0.09f);
        }
    }

    void Console::shutdown()
    {
        present_.shutdown();
        window_.destroy();
    }
} // namespace lazy100
