#include "lazy100/editor/code_editor.hpp"

#include "lazy100/console/config.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/editor/ui.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_set>

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

        // Codepoint boundary in `s` whose caret x (in pixels from the text origin) is closest
        // to `px` — maps a mouse click column onto a byte offset.
        int col_from_x(const std::string& s, int px)
        {
            if (px <= 0)
                return 0;
            int pos = 0, prevW = 0;
            while (pos < static_cast<int>(s.size()))
            {
                const int nxt = next_cp(s, pos);
                const int w   = font::text_width(s.substr(0, nxt).c_str());
                if (px < (prevW + w) / 2) // click fell in the left half of this glyph
                    return pos;
                prevW = w;
                pos   = nxt;
            }
            return static_cast<int>(s.size());
        }

        bool is_word(unsigned char c) { return std::isalnum(c) || c == '_'; }

        // Lua reserved words.
        const std::unordered_set<std::string>& keywords()
        {
            static const std::unordered_set<std::string> k = {
                "and",    "break", "do",   "else", "elseif", "end",  "false", "for",  "function", "goto", "if",
                "in",     "local", "nil",  "not",  "or",     "repeat", "return", "then", "true",   "until", "while"};
            return k;
        }
        // Keywords that finish a line/expression on their own: accepting them from the popup
        // should NOT append a space (they are followed by a newline, not more code).
        bool trailing_keyword(const std::string& w)
        {
            static const std::unordered_set<std::string> t = {"end",  "break", "then",  "do",   "else",
                                                              "repeat", "nil",  "true",  "false"};
            return t.count(w) != 0;
        }
        // Lazy-100 API + math builtins + lifecycle callbacks.
        const std::unordered_set<std::string>& builtins()
        {
            static const std::unordered_set<std::string> b = {
                "cls",  "pset",  "pget",  "line",   "rect", "rectfill", "circ", "circfill", "print", "spr",
                "sspr", "sget",  "sset",  "fget",   "fset", "pal",      "palt", "btn",      "btnp",  "sfx",
                "music","mget",  "mset",  "map",    "flr",  "ceil",     "abs",  "min",      "max",   "mid",
                "sgn",  "sqrt",  "sin",   "cos",    "atan2","rnd",      "srand","t",        "time",  "_init",
                "_update", "_update60", "_draw"};
            return b;
        }
        // Flat, sorted vocabulary for autocomplete (keywords first, then builtins).
        const std::vector<const char*>& vocabulary()
        {
            static const std::vector<const char*> v = {
                // keywords
                "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
                "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
                // builtins / api
                "abs", "atan2", "btn", "btnp", "ceil", "circ", "circfill", "cls", "cos", "fget", "flr", "fset",
                "line", "map", "max", "mget", "mid", "min", "mset", "music", "pal", "palt", "pget", "print",
                "pset", "rect", "rectfill", "rnd", "sfx", "sget", "sgn", "sin", "spr", "sqrt", "srand", "sset",
                "sspr", "t", "time", "_draw", "_init", "_update", "_update60"};
            return v;
        }

        u8 word_color(const std::string& w)
        {
            if (keywords().count(w))
                return 14; // pink
            if (builtins().count(w))
                return 12; // blue
            return ui::kText; // 7 white
        }

        // Draw one line with Lua syntax colors starting at (x,y). Comments, strings, numbers,
        // keywords, and builtins get distinct hues; everything else is plain.
        void draw_line(Framebuffer& fb, const std::string& s, int x, int y)
        {
            const int n = static_cast<int>(s.size());
            int       i = 0;
            auto      emit = [&](int a, int b, u8 c)
            {
                if (b <= a)
                    return;
                const std::string tok = s.substr(a, b - a);
                x = font::print(fb, tok.c_str(), x, y, c);
            };
            while (i < n)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                if (c == '-' && i + 1 < n && s[i + 1] == '-') // line comment to EOL
                {
                    emit(i, n, 5); // gray
                    return;
                }
                if (c == '"' || c == '\'') // string literal
                {
                    int j = i + 1;
                    while (j < n && s[j] != static_cast<char>(c))
                        j += (s[j] == '\\' && j + 1 < n) ? 2 : 1;
                    j = (j < n) ? j + 1 : n;
                    emit(i, j, 15); // peach
                    i = j;
                }
                else if (std::isdigit(c))
                {
                    int j = i;
                    while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.'))
                        ++j;
                    emit(i, j, 9); // orange
                    i = j;
                }
                else if (std::isalpha(c) || c == '_')
                {
                    int j = i;
                    while (j < n && is_word(static_cast<unsigned char>(s[j])))
                        ++j;
                    emit(i, j, word_color(s.substr(i, j - i)));
                    i = j;
                }
                else // operators / punctuation / spaces / non-ASCII (strings handled above)
                {
                    int j = i;
                    while (j < n)
                    {
                        const unsigned char d = static_cast<unsigned char>(s[j]);
                        if (d == '"' || d == '\'' || std::isalnum(d) || d == '_' || (d == '-' && j + 1 < n && s[j + 1] == '-'))
                            break;
                        ++j;
                    }
                    emit(i, j, 6); // light gray punctuation
                    i = j;
                }
            }
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
        const Keyboard& kb  = con.keyboard();
        const Mouse&    m   = con.mouse();
        const int       cx0 = cx_, cy0 = cy_; // remember caret, to detect motion for scroll-follow

        // ---- mouse: wheel scrolls the view; left-click places the caret ----
        const int lh      = font::line_height();
        const int statusY = static_cast<int>(kScreenH) - lh - 1;
        const int rows    = std::max(1, (statusY - kTop) / lh);
        if (const int w = m.wheel())
            top_ = std::clamp(top_ - w * 3, 0, std::max(0, static_cast<int>(lines_.size()) - rows));
        if (m.pressed(Mouse::Left) && m.y() >= kTop && m.y() < statusY)
        {
            cy_ = std::clamp(top_ + (m.y() - kTop) / lh, 0, static_cast<int>(lines_.size()) - 1);
            cx_ = col_from_x(lines_[cy_], m.x() - kGutter + left_); // + left_: account for h-scroll
        }

        // Autocomplete popup captures Up/Down/Tab while it is open.
        refresh_completions();
        const bool popup = !matches_.empty();

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
        if (kb.repeat(Keyboard::Up))
        {
            if (popup)
                comp_sel_ = std::max(0, comp_sel_ - 1);
            else if (cy_ > 0)
            {
                --cy_;
                cx_ = snap(lines_[cy_], cx_);
            }
        }
        if (kb.repeat(Keyboard::Down))
        {
            if (popup)
                comp_sel_ = std::min(static_cast<int>(matches_.size()) - 1, comp_sel_ + 1);
            else if (cy_ + 1 < static_cast<int>(lines_.size()))
            {
                ++cy_;
                cx_ = snap(lines_[cy_], cx_);
            }
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
        if (!kb.ctrl() && kb.repeat(Keyboard::Tab)) // Ctrl+Tab is reserved for editor switching
        {
            if (popup)
                accept_completion(); // Tab accepts the highlighted candidate
            else
            {
                lines_[cy_].insert(cx_, "  ");
                cx_ += 2;
            }
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
        refresh_completions(); // reflect this frame's edits in the popup drawn below
        caret_moved_ = (cx_ != cx0 || cy_ != cy0); // scroll follows the caret only when it moves
        ++blink_;
    }

    std::string CodeEditor::word_prefix() const
    {
        const std::string& line = lines_[cy_];
        // Only complete at the end of a token: if a word char sits right after the caret
        // (e.g. the caret was clicked into the middle of an identifier), offer nothing.
        if (cx_ < static_cast<int>(line.size()) && is_word(static_cast<unsigned char>(line[cx_])))
            return {};
        int a = cx_;
        while (a > 0 && is_word(static_cast<unsigned char>(line[a - 1])))
            --a;
        if (a >= cx_ || std::isdigit(static_cast<unsigned char>(line[a])))
            return {}; // empty, or a numeric literal — no completion
        return line.substr(a, cx_ - a);
    }

    void CodeEditor::refresh_completions()
    {
        matches_.clear();
        const std::string prefix = word_prefix();
        if (prefix.empty())
        {
            comp_sel_ = 0;
            return;
        }
        // Already a complete keyword/builtin? Stop nagging — the word is done.
        if (keywords().count(prefix) || builtins().count(prefix))
        {
            comp_sel_ = 0;
            return;
        }
        for (const char* w : vocabulary())
            if (std::strncmp(w, prefix.c_str(), prefix.size()) == 0 && std::strcmp(w, prefix.c_str()) != 0)
            {
                matches_.push_back(w);
                if (matches_.size() >= 8)
                    break;
            }
        comp_sel_ = std::clamp(comp_sel_, 0, std::max(0, static_cast<int>(matches_.size()) - 1));
    }

    void CodeEditor::accept_completion()
    {
        if (matches_.empty())
            return;
        const std::string prefix = word_prefix();
        const int         start  = cx_ - static_cast<int>(prefix.size());
        const char*       word   = matches_[std::clamp(comp_sel_, 0, static_cast<int>(matches_.size()) - 1)];
        std::string       ins    = word;
        // Keywords that lead into more code get a trailing space; terminal keywords (end,
        // break, then, ...) and builtins (which take a '(' next) don't. Skip if already spaced.
        if (keywords().count(word) && !trailing_keyword(word) &&
            (cx_ >= static_cast<int>(lines_[cy_].size()) || lines_[cy_][cx_] != ' '))
            ins += ' ';
        lines_[cy_].replace(start, prefix.size(), ins);
        cx_ = start + static_cast<int>(ins.size());
        matches_.clear();
    }

    void CodeEditor::draw(Console& con, Framebuffer& fb)
    {
        (void)con;
        const int lh      = font::line_height();
        const int statusY = static_cast<int>(kScreenH) - lh - 1;
        const int rows    = std::max(1, (statusY - kTop) / lh);

        // Follow the caret only when it moved this frame, so wheel-scrolling can rest away
        // from the caret instead of snapping back every frame.
        if (caret_moved_)
        {
            if (cy_ < top_)
                top_ = cy_;
            else if (cy_ >= top_ + rows)
                top_ = cy_ - rows + 1;
        }
        top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(lines_.size()) - 1));

        // Horizontal scroll: keep the caret's pixel column in view so long lines stay reachable.
        const int cxpx  = font::text_width(lines_[cy_].substr(0, cx_).c_str());
        const int viewW = static_cast<int>(kScreenW) - kGutter - 2; // visible code width in px
        if (caret_moved_)
        {
            if (cxpx < left_)
                left_ = cxpx;
            else if (cxpx > left_ + viewW)
                left_ = cxpx - viewW;
        }
        left_ = std::max(0, left_);

        fb.rectfill(0, EditorHost::kTabH, static_cast<int>(kScreenW) - 1, static_cast<int>(kScreenH) - 1, 0);

        int caretX = kGutter, caretY = kTop;
        for (int r = 0; r < rows; ++r)
        {
            const int li = top_ + r;
            if (li >= static_cast<int>(lines_.size()))
                break;
            const int y = kTop + r * lh;

            const std::string& text = lines_[li];
            draw_line(fb, text, kGutter - left_, y); // syntax-highlighted, scrolled horizontally

            // Mask whatever scrolled under the gutter, then draw the line number over it.
            fb.rectfill(0, y, kGutter - 1, y + lh - 1, 0);
            char num[8];
            std::snprintf(num, sizeof(num), "%3d", li + 1);
            font::print(fb, num, 1, y, li == cy_ ? ui::kDim : 5); // line-number gutter

            if (li == cy_)
            {
                caretX = kGutter + cxpx - left_;
                caretY = y;
                if ((blink_ / 15) % 2 == 0)
                    draw::line(fb, caretX, y, caretX, y + lh - 1, 7);
            }
        }
        draw::line(fb, kGutter - 3, kTop, kGutter - 3, statusY - 2, ui::kBorder); // gutter divider (over masks)

        // Status line.
        draw::line(fb, 0, statusY - 1, static_cast<int>(kScreenW) - 1, statusY - 1, ui::kBorder);
        char st[72];
        std::snprintf(st, sizeof(st), "ln %d  col %d   Tab: complete   ESC: shell", cy_ + 1, cx_ + 1);
        font::print(fb, st, 2, statusY, ui::kDim);

        // Autocomplete popup, anchored under the caret.
        if (!matches_.empty())
        {
            int wmax = 0;
            for (const char* w : matches_)
                wmax = std::max(wmax, font::text_width(w));
            const int pw = wmax + 10;
            const int ph = static_cast<int>(matches_.size()) * lh + 2;
            int       px = caretX;
            int       py = caretY + lh;
            if (px + pw > static_cast<int>(kScreenW))
                px = static_cast<int>(kScreenW) - pw;
            if (py + ph > statusY)
                py = caretY - ph; // flip above the caret near the bottom
            ui::panel(fb, px, py, pw, ph, ui::kPanel, ui::kBorderHi);
            for (size_t i = 0; i < matches_.size(); ++i)
            {
                const int yy = py + 1 + static_cast<int>(i) * lh;
                if (static_cast<int>(i) == comp_sel_)
                    fb.rectfill(px + 1, yy, px + pw - 2, yy + lh - 1, ui::kBtnActive);
                font::print(fb, matches_[i], px + 4, yy, static_cast<int>(i) == comp_sel_ ? ui::kBg : ui::kText);
            }
        }

        ui::help_button(fb, con, con.mouse(), static_cast<int>(kScreenW) - 15, statusY - 1, 5,
                        "CODE\n"
                        "type to edit; Tab: autocomplete\n"
                        "click: place caret; wheel: scroll\n"
                        "up/down while list open: pick\n"
                        "arrows / home / end / pgup-dn\n"
                        "ctrl+tab: switch editor\n"
                        "ESC: shell (then run / save)");
    }

    cursor::Type CodeEditor::cursor(Console& con) const
    {
        const Mouse& m       = con.mouse();
        const int    statusY = static_cast<int>(kScreenH) - font::line_height() - 1;
        return (m.x() >= kGutter && m.y() >= kTop && m.y() < statusY) ? cursor::Ibeam : cursor::Arrow;
    }
} // namespace lazy100
