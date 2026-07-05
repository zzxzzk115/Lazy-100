#pragma once

#include "lazy100/video/cursor.hpp"
#include "lazy100/video/icons.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace lazy100
{
    class Console;
    class Framebuffer;

    // One editor pane (code / sprite / map / sfx / music). Each draws into the shared 320x240
    // framebuffer below the tab bar and reads input via Console (keyboard/mouse).
    class Editor
    {
    public:
        virtual ~Editor()                                = default;
        virtual const char* name() const                 = 0; // short label (unused by the icon tab bar)
        virtual icon::Id    icon() const                 = 0; // tab-bar glyph
        virtual void        update(Console&)             {}
        virtual void        draw(Console&, Framebuffer&) = 0;
        // Per-editor tool buttons drawn into the LEFT side of the top bar (the right side
        // holds the mode toggles). Coordinates 0..EditorHost::kToolsW-1, y 0..kTabH-1.
        virtual void draw_tools(Console&, Framebuffer&) {}
        // First crack at ESC (e.g. closing an overlay like the code editor's cheatsheet).
        // Return true to consume it; false lets the console open its pause menu.
        virtual bool on_escape(Console&) { return false; }
        // Desired pixel cursor for the current mouse position (default: pointer).
        virtual cursor::Type cursor(Console&) const { return cursor::Arrow; }
    };

    // Owns the editor panes, draws the top tab bar, and routes to the active pane.
    class EditorHost
    {
    public:
        static constexpr int kTabH   = 16;  // tab-bar height (editors draw below this)
        static constexpr int kToolsW = 210; // left bar region reserved for the editor's tools

        EditorHost();

        void update(Console& con);
        void draw(Console& con, Framebuffer& fb);

        int  current() const { return current_; }
        void set_current(int i);
        int  count() const { return static_cast<int>(editors_.size()); }

        cursor::Type cursor(Console& con) const { return editors_[current_]->cursor(con); }

        // Offer ESC to the active editor first (overlays close themselves before the pause menu).
        bool on_escape(Console& con) { return editors_[static_cast<std::size_t>(current_)]->on_escape(con); }

    private:
        std::vector<std::unique_ptr<Editor>> editors_;
        int                                  current_ = 0;
    };
} // namespace lazy100
