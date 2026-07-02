#pragma once

#include <string>
#include <vector>

namespace lazy100
{
    class Framebuffer;
    class Keyboard;

    // The pause-style popup used by the game browser and by a running cart: a centered box
    // drawn over whatever is on screen. Up/down picks (wrapping), right or enter confirms.
    // The owner handles ESC (toggle) and interprets the confirmed index.
    class PauseMenu
    {
    public:
        void open(std::vector<std::string> items)
        {
            items_ = std::move(items);
            sel_   = 0;
            open_  = true;
        }
        void close() { open_ = false; }
        bool is_open() const { return open_; }

        // Navigate/confirm from this frame's input. Returns the confirmed item index (the menu
        // closes itself), or -1 if nothing was confirmed.
        int update(Keyboard& kb);

        void draw(Framebuffer& fb) const;

    private:
        std::vector<std::string> items_;
        int                      sel_  = 0;
        bool                     open_ = false;
    };
} // namespace lazy100
