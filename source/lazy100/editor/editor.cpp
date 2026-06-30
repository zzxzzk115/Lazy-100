#include "lazy100/editor/editor.hpp"

#include "lazy100/console/console.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

namespace lazy100
{
    namespace
    {
        // M6 placeholder panes. M8-M11 replace these with the real editors (own files).
        struct PlaceholderEditor : Editor
        {
            const char* label;
            explicit PlaceholderEditor(const char* l) : label(l) {}
            const char* name() const override { return label; }
            void        draw(Console&, Framebuffer& fb) override
            {
                font::print(fb, label, 8, EditorHost::kTabH + 8, 7);
                font::print(fb, "(editor coming soon)", 8, EditorHost::kTabH + 22, 5);
            }
        };
    } // namespace

    EditorHost::EditorHost()
    {
        editors_.push_back(std::make_unique<PlaceholderEditor>("CODE"));
        editors_.push_back(std::make_unique<PlaceholderEditor>("SPRITE"));
        editors_.push_back(std::make_unique<PlaceholderEditor>("MAP"));
        editors_.push_back(std::make_unique<PlaceholderEditor>("SFX"));
        editors_.push_back(std::make_unique<PlaceholderEditor>("MUSIC"));
    }

    void EditorHost::set_current(int i)
    {
        if (i >= 0 && i < count())
            current_ = i;
    }

    void EditorHost::update(Console& con)
    {
        const Mouse& m = con.mouse();
        const int    n = count();
        const int    tabW = static_cast<int>(kScreenW) / n;
        if (m.pressed(Mouse::Left) && m.y() >= 0 && m.y() < kTabH)
            set_current(m.x() / tabW);

        editors_[current_]->update(con);
    }

    void EditorHost::draw(Console& con, Framebuffer& fb)
    {
        fb.cls(0);

        // Tab bar.
        const int n    = count();
        const int tabW = static_cast<int>(kScreenW) / n;
        for (int i = 0; i < n; ++i)
        {
            const int x0 = i * tabW;
            const int x1 = (i == n - 1) ? static_cast<int>(kScreenW) - 1 : x0 + tabW - 1;
            fb.rectfill(x0, 0, x1, kTabH - 1, i == current_ ? 5 : 1);
            font::print(fb, editors_[i]->name(), x0 + 4, 3, i == current_ ? 7 : 6);
        }

        editors_[current_]->draw(con, fb);
    }
} // namespace lazy100
