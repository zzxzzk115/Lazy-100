#pragma once

#include "lazy100/common/types.hpp"
#include "lazy100/video/icons.hpp"

namespace lazy100
{
    class Framebuffer;
    class Mouse;
    class Console;

    // Shared editor chrome: a small palette of UI colors plus bordered panels, icon-only section
    // headers, icon buttons (hover/active shading), and dividers. Keeps every editor consistent
    // and saves each from re-drawing raw rectangles.
    namespace ui
    {
        // Curated-palette indices used across the editor UI.
        constexpr u8 kBg         = 0;  // editor background
        constexpr u8 kPanel      = 1;  // panel fill (dark navy)
        constexpr u8 kPanelLo    = 0;  // inset/well fill (black)
        constexpr u8 kBorder     = 5;  // subtle border (dark gray)
        constexpr u8 kBorderHi   = 6;  // emphasized border (light gray)
        constexpr u8 kHeader      = 13; // section header bar (mauve)
        constexpr u8 kBtn         = 5;  // button face
        constexpr u8 kBtnHover    = 6;  // button hover
        constexpr u8 kBtnActive   = 12; // button pressed/selected (blue)
        constexpr u8 kText        = 7;  // primary text/icon
        constexpr u8 kDim         = 6;  // secondary text
        constexpr u8 kAccent      = 12; // active accents

        // Clear the whole editor content area (below the tab bar) to the background.
        void clear(Framebuffer& fb, int tab_h);

        // A bordered panel; returns nothing (draw content yourself inside x+1..).
        void panel(Framebuffer& fb, int x, int y, int w, int h, u8 fill = kPanel, u8 border = kBorder);

        // A panel with an icon-only header bar across the top. Returns the y where content
        // should begin (just below the header).
        int titled_panel(Framebuffer& fb, int x, int y, int w, int h, icon::Id ic);

        // An icon-only button; returns true if clicked this frame. `active` shades it as selected.
        // `ink` overrides the icon color, `bg` the fill (e.g. a red-fill Stop); -1 keeps automatic.
        bool icon_button(Framebuffer& fb, const Mouse& m, int x, int y, int w, int h, icon::Id ic,
                         bool active = false, int ink = -1, int bg = -1);

        void divider(Framebuffer& fb, int x, int y, int w, u8 c = kBorder);      // horizontal
        void vdivider(Framebuffer& fb, int x, int y, int h, u8 c = kBorder);     // vertical

        bool hit(const Mouse& m, int x, int y, int w, int h); // point-in-rect for the mouse cursor

        // A floating tooltip panel with (possibly '\n'-separated) `text`, anchored near (x, y)
        // and nudged to stay on screen. Drawn on top, so call it last.
        void tooltip(Framebuffer& fb, int x, int y, const char* text);

        // A 12x12 '?' hint button at (x, y); after the mouse rests on it, pops `text` as a
        // tooltip. `id` distinguishes hotspots for the hover timer (unique per editor).
        void help_button(Framebuffer& fb, Console& con, const Mouse& m, int x, int y, int id,
                         const char* text);
    } // namespace ui
} // namespace lazy100
