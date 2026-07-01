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

        const int tabW = static_cast<int>(kScreenW) / n;
        if (m.pressed(Mouse::Left) && m.y() >= 0 && m.y() < kTabH)
            set_current(m.x() / tabW);
        // Ctrl+Tab / Ctrl+Shift+Tab cycles editors from the keyboard.
        if (kb.ctrl() && kb.pressed(Keyboard::Tab))
            set_current((current_ + (kb.shift() ? n - 1 : 1)) % n);

        editors_[current_]->update(con);
    }

    void EditorHost::draw(Console& con, Framebuffer& fb)
    {
        fb.cls(ui::kBg);

        // Icon-only tab bar: a strip of centered glyphs, active tab lit with an accent underline.
        const int n    = count();
        const int W    = static_cast<int>(kScreenW);
        const int tabW = W / n;
        fb.rectfill(0, 0, W - 1, kTabH - 1, ui::kPanel);
        for (int i = 0; i < n; ++i)
        {
            const int  x0     = i * tabW;
            const int  x1     = (i == n - 1) ? W - 1 : x0 + tabW - 1;
            const bool active = (i == current_);
            if (active)
                fb.rectfill(x0, 0, x1, kTabH - 1, ui::kHeader);
            const int cx = x0 + (x1 - x0 + 1 - icon::kSize) / 2;
            icon::draw(fb, editors_[i]->icon(), cx, (kTabH - icon::kSize) / 2, active ? ui::kText : ui::kDim);
            if (i > 0)
                ui::vdivider(fb, x0, 2, kTabH - 4, ui::kBorder); // separator between tabs
            if (active)
                draw::line(fb, x0 + 2, kTabH - 2, x1 - 2, kTabH - 2, ui::kAccent);
        }
        draw::line(fb, 0, kTabH, W - 1, kTabH, ui::kBorder); // bar underline

        editors_[current_]->draw(con, fb);
    }
} // namespace lazy100
