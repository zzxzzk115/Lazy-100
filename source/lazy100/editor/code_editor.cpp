#include "lazy100/editor/code_editor.hpp"

#include "lazy100/console/config.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

#include <algorithm>
#include <cstdio>

namespace lazy100
{
    namespace
    {
        constexpr int kTop    = EditorHost::kTabH + 2; // first text row y
        constexpr int kGutter = 22;                    // x where code text starts (line-number gutter left)

        bool is_cont(unsigned char b) { return (b & 0xC0) == 0x80; } // UTF-8 continuation byte

        // Byte offset of the codepoint before / after `pos` in `s`.
        int prev_cp(const std::string& s, int pos)
        {
            if (pos <= 0)
                return 0;
            --pos;
            while (pos > 0 && is_cont(static_cast<unsigned char>(s[pos])))
                --pos;
            return pos;
        }
        int next_cp(const std::string& s, int pos)
        {
            const int n = static_cast<int>(s.size());
            if (pos >= n)
                return n;
            ++pos;
            while (pos < n && is_cont(static_cast<unsigned char>(s[pos])))
                ++pos;
            return pos;
        }
        // Snap a byte offset back to the nearest codepoint boundary at or before it.
        int snap(const std::string& s, int pos)
        {
            pos = std::clamp(pos, 0, static_cast<int>(s.size()));
            while (pos > 0 && is_cont(static_cast<unsigned char>(s[pos])))
                --pos;
            return pos;
        }
    } // namespace

    void CodeEditor::sync_from(const std::string& code)
    {
        if (code == cache_)
            return; // no external change since our last flush
        lines_.clear();
        std::string cur;
        for (char ch : code)
        {
            if (ch == '\n')
            {
                lines_.push_back(cur);
                cur.clear();
            }
            else if (ch != '\r')
                cur += ch;
        }
        lines_.push_back(cur);
        if (lines_.empty())
            lines_.push_back("");
        cache_ = code;
        cy_    = std::min(cy_, static_cast<int>(lines_.size()) - 1);
        cx_    = snap(lines_[cy_], cx_);
    }

    void CodeEditor::flush_to(std::string& code) const
    {
        std::string out;
        for (size_t i = 0; i < lines_.size(); ++i)
        {
            out += lines_[i];
            if (i + 1 < lines_.size())
                out += '\n';
        }
        code = out;
    }

    void CodeEditor::update(Console& con)
    {
        sync_from(con.code());
        const Keyboard& kb = con.keyboard();

        std::string& line = lines_[cy_];

        // ---- navigation ----
        if (kb.repeat(Keyboard::Left))
        {
            if (cx_ > 0)
                cx_ = prev_cp(line, cx_);
            else if (cy_ > 0)
            {
                --cy_;
                cx_ = static_cast<int>(lines_[cy_].size());
            }
        }
        if (kb.repeat(Keyboard::Right))
        {
            if (cx_ < static_cast<int>(line.size()))
                cx_ = next_cp(line, cx_);
            else if (cy_ + 1 < static_cast<int>(lines_.size()))
            {
                ++cy_;
                cx_ = 0;
            }
        }
        if (kb.repeat(Keyboard::Up) && cy_ > 0)
        {
            --cy_;
            cx_ = snap(lines_[cy_], cx_);
        }
        if (kb.repeat(Keyboard::Down) && cy_ + 1 < static_cast<int>(lines_.size()))
        {
            ++cy_;
            cx_ = snap(lines_[cy_], cx_);
        }
        if (kb.repeat(Keyboard::Home))
            cx_ = 0;
        if (kb.repeat(Keyboard::End))
            cx_ = static_cast<int>(lines_[cy_].size());
        if (kb.repeat(Keyboard::PageUp))
            cy_ = std::max(0, cy_ - 10), cx_ = snap(lines_[cy_], cx_);
        if (kb.repeat(Keyboard::PageDown))
            cy_ = std::min(static_cast<int>(lines_.size()) - 1, cy_ + 10), cx_ = snap(lines_[cy_], cx_);

        // ---- edits ---- (re-fetch the line ref after cy_ may have changed above)
        std::string& cur = lines_[cy_];
        if (kb.repeat(Keyboard::Backspace))
        {
            if (cx_ > 0)
            {
                const int p = prev_cp(cur, cx_);
                cur.erase(p, cx_ - p);
                cx_ = p;
            }
            else if (cy_ > 0)
            {
                const int join = static_cast<int>(lines_[cy_ - 1].size());
                lines_[cy_ - 1] += cur;
                lines_.erase(lines_.begin() + cy_);
                --cy_;
                cx_ = join;
            }
        }
        if (kb.repeat(Keyboard::Delete))
        {
            std::string& d = lines_[cy_];
            if (cx_ < static_cast<int>(d.size()))
                d.erase(cx_, next_cp(d, cx_) - cx_);
            else if (cy_ + 1 < static_cast<int>(lines_.size()))
            {
                d += lines_[cy_ + 1];
                lines_.erase(lines_.begin() + cy_ + 1);
            }
        }
        if (kb.repeat(Keyboard::Return))
        {
            std::string& e   = lines_[cy_];
            std::string  rest = e.substr(cx_);
            e.erase(cx_);
            lines_.insert(lines_.begin() + cy_ + 1, rest);
            ++cy_;
            cx_ = 0;
        }
        if (kb.repeat(Keyboard::Tab))
        {
            lines_[cy_].insert(cx_, "  ");
            cx_ += 2;
        }
        // ---- printable text typed this frame ----
        const std::string& typed = kb.text();
        if (!typed.empty())
        {
            lines_[cy_].insert(cx_, typed);
            cx_ += static_cast<int>(typed.size());
        }

        // Push our buffer back so the shell's run/save see the latest source.
        flush_to(con.code());
        cache_ = con.code();
        ++blink_;
    }

    void CodeEditor::draw(Console& con, Framebuffer& fb)
    {
        (void)con;
        const int lh      = font::line_height();
        const int statusY = static_cast<int>(kScreenH) - lh - 1;
        const int rows    = std::max(1, (statusY - kTop) / lh);

        // Keep the cursor line on screen.
        if (cy_ < top_)
            top_ = cy_;
        else if (cy_ >= top_ + rows)
            top_ = cy_ - rows + 1;

        fb.rectfill(0, EditorHost::kTabH, static_cast<int>(kScreenW) - 1, static_cast<int>(kScreenH) - 1, 0);

        for (int r = 0; r < rows; ++r)
        {
            const int li = top_ + r;
            if (li >= static_cast<int>(lines_.size()))
                break;
            const int y = kTop + r * lh;

            char num[8];
            std::snprintf(num, sizeof(num), "%3d", li + 1);
            font::print(fb, num, 1, y, 5); // line-number gutter

            const std::string& text = lines_[li];
            font::print(fb, text.c_str(), kGutter, y, 7);

            if (li == cy_ && (blink_ / 15) % 2 == 0)
            {
                // Caret x = width of the prefix, found by re-printing it (idempotent).
                const int caret = font::print(fb, text.substr(0, cx_).c_str(), kGutter, y, 7);
                draw::line(fb, caret, y, caret, y + lh - 1, 8);
            }
        }

        // Status line.
        draw::line(fb, 0, statusY - 1, static_cast<int>(kScreenW) - 1, statusY - 1, 1);
        char st[64];
        std::snprintf(st, sizeof(st), "ln %d  col %d   ESC: shell (run/save)", cy_ + 1, cx_ + 1);
        font::print(fb, st, 2, statusY, 6);
    }
} // namespace lazy100
