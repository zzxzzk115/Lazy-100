#include "lazy100/editor/editor.hpp"

#include "lazy100/console/console.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/editor/code_editor.hpp"
#include "lazy100/editor/map_editor.hpp"
#include "lazy100/editor/music_editor.hpp"
#include "lazy100/editor/sfx_editor.hpp"
#include "lazy100/editor/sprite_editor.hpp"
#include "lazy100/editor/ui.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/icons.hpp"

namespace lazy100
{
    EditorHost::EditorHost()
    {
        editors_.push_back(std::make_unique<CodeEditor>());
        editors_.push_back(std::make_unique<SpriteEditor>());
        editors_.push_back(std::make_unique<MapEditor>());
        editors_.push_back(std::make_unique<SfxEditor>());
        editors_.push_back(std::make_unique<MusicEditor>());
    }

    void EditorHost::set_current(int i)
    {
        if (i >= 0 && i < count())
            current_ = i;
    }

    void EditorHost::update(Console& con)
    {
        const Mouse&    m  = con.mouse();
        const Keyboard& kb = con.keyboard();
        const int       n  = count();

        // Mode toggles live right-aligned in the bar; the left side belongs to the current
        // editor's tool buttons, so only clicks inside the toggle strip switch modes.
        const int cellW  = 22;
        const int stripX = static_cast<int>(kScreenW) - n * cellW - 2;
        if (m.pressed(Mouse::Left) && m.y() >= 0 && m.y() < kTabH && m.x() >= stripX)
            set_current((m.x() - stripX) / cellW);
        // Ctrl+Tab / Ctrl+Shift+Tab cycles editors from the keyboard.
        if (kb.ctrl() && kb.pressed(Keyboard::Tab))
            set_current((current_ + (kb.shift() ? n - 1 : 1)) % n);

        editors_[current_]->update(con);
    }

    void EditorHost::draw(Console& con, Framebuffer& fb)
    {
        fb.cls(ui::kBg);

        // Classic fantasy-console top bar: the current editor's tool buttons on the left,
        // the mode toggles right-aligned, lit while active.
        const int n      = count();
        const int W      = static_cast<int>(kScreenW);
        const int cellW  = 22;
        const int stripX = W - n * cellW - 2;
        fb.rectfill(0, 0, W - 1, kTabH - 1, ui::kPanel);
        for (int i = 0; i < n; ++i)
        {
            const int  x0     = stripX + i * cellW;
            const bool active = (i == current_);
            if (active)
            {
                fb.rectfill(x0, 0, x0 + cellW - 1, kTabH - 1, ui::kBtnActive);
                draw::line(fb, x0, kTabH - 1, x0 + cellW - 1, kTabH - 1, ui::kAccent);
            }
            icon::draw(fb, editors_[i]->icon(), x0 + (cellW - icon::kSize) / 2,
                       (kTabH - icon::kSize) / 2, active ? ui::kText : ui::kDim);
        }
        ui::vdivider(fb, stripX - 2, 2, kTabH - 4, ui::kBorder); // tools | toggles separator
        draw::line(fb, 0, kTabH, W - 1, kTabH, ui::kBorder);     // bar underline

        // Centered play button: run the cart straight from the editor (same as menu "run cart").
        // On a load error it stays here and the code editor's error bar explains why.
        if (ui::icon_button(fb, con.mouse(), W / 2 - 6, (kTabH - 12) / 2, 12, 12, icon::Play, false))
            con.start_cart();

        editors_[current_]->draw(con, fb);
        editors_[current_]->draw_tools(con, fb); // after draw: tool buttons sit above any clear
        ui::flush_tooltip(fb);                   // tooltips last, so nothing can cover them
    }
} // namespace lazy100
