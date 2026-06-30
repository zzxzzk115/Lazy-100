#pragma once

#include <memory>
#include <vector>

namespace lazy100
{
    class Console;
    class Framebuffer;

    // One editor pane (code / sprite / map / sfx / music). M6 ships minimal skeletons; M8-M11
    // flesh each out (and split into its own file). All draw into the shared 320x240
    // framebuffer below the tab bar and read input via Console (keyboard/mouse).
    class Editor
    {
    public:
        virtual ~Editor()                          = default;
        virtual const char* name() const           = 0; // tab label
        virtual void        update(Console&)       {}
        virtual void        draw(Console&, Framebuffer&) = 0;
    };

    // Owns the editor panes, draws the top tab bar, and routes to the active pane.
    class EditorHost
    {
    public:
        static constexpr int kTabH = 16; // tab-bar height (editors draw below this)

        EditorHost();

        void update(Console& con);
        void draw(Console& con, Framebuffer& fb);

        int  current() const { return current_; }
        void set_current(int i);
        int  count() const { return static_cast<int>(editors_.size()); }

    private:
        std::vector<std::unique_ptr<Editor>> editors_;
        int                                  current_ = 0;
    };
} // namespace lazy100
