#include "lazy100/shell/shell.hpp"

#include "lazy100/console/config.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

#include <algorithm>
#include <filesystem>

namespace lazy100
{
    namespace
    {
        namespace fs = std::filesystem;

        const char* const kCommands[] = {"help", "ls",   "cd",   "pwd",  "cls", "exit",
                                         "run",  "edit", "load", "save", "new", "explore"};

        // Path shown to the user, without the sandbox "carts/" prefix.
        std::string strip_root(const std::string& p)
        {
            if (p == "carts")
                return "";
            if (p.rfind("carts/", 0) == 0)
                return p.substr(6);
            return p;
        }

        bool starts_with(const std::string& s, const std::string& prefix)
        {
            return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
        }

        // Erase the last UTF-8 code point from s (handles multibyte 中日韩 input).
        void erase_last_utf8(std::string& s)
        {
            if (s.empty())
                return;
            size_t i = s.size();
            do
            {
                --i;
            } while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
            s.erase(i);
        }

        // cwd is a real path under "carts"; show it as a sandboxed virtual root.
        std::string display_cwd(const std::string& cwd)
        {
            if (cwd == "carts")
                return "/";
            if (cwd.rfind("carts/", 0) == 0)
                return "/" + cwd.substr(6);
            return "/" + cwd;
        }

        // Split "cmd arg..." into command + argument (remainder after first space).
        void split(const std::string& line, std::string& cmd, std::string& arg)
        {
            const size_t b = line.find_first_not_of(" \t");
            if (b == std::string::npos)
            {
                cmd.clear();
                arg.clear();
                return;
            }
            const size_t e = line.find_first_of(" \t", b);
            cmd            = line.substr(b, e == std::string::npos ? std::string::npos : e - b);
            arg.clear();
            if (e != std::string::npos)
            {
                const size_t ab = line.find_first_not_of(" \t", e);
                if (ab != std::string::npos)
                    arg = line.substr(ab);
            }
        }
    } // namespace

    Shell::Shell()
    {
        print_line("LAZY-100");
        print_line("type 'help' for commands, ESC for the editor");
    }

    void Shell::print_line(const std::string& s) { lines_.push_back(s); }

    void Shell::exec(Console& con, const std::string& line)
    {
        std::string cmd, arg;
        split(line, cmd, arg);
        if (cmd.empty())
            return;

        if (cmd == "help")
        {
            print_line("commands: help ls cd pwd cls run edit load save new explore exit");
            print_line("save foo.png / load foo.png: cart as a shareable image");
            print_line("explore: browse & download games from the online catalog");
        }
        else if (cmd == "cls")
            lines_.clear();
        else if (cmd == "exit" || cmd == "quit")
            con.quit();
        else if (cmd == "pwd")
            print_line(display_cwd(cwd_));
        else if (cmd == "edit")
            con.set_mode(ConsoleMode::Editor);
        else if (cmd == "explore")
            con.set_mode(ConsoleMode::Explore);
        else if (cmd == "run")
        {
            if (!con.start_cart())
                print_line("no cart loaded");
        }
        else if (cmd == "cd")
        {
            if (arg.empty())
            {
                cwd_ = "carts"; // bare `cd` -> root
                return;
            }
            const fs::path    target = (fs::path(cwd_) / arg).lexically_normal();
            const std::string s      = target.generic_string();
            if (s != "carts" && s.rfind("carts/", 0) != 0)
            {
                print_line("outside carts/");
                return;
            }
            std::error_code ec;
            if (fs::is_directory(target, ec))
                cwd_ = s;
            else
                print_line("not a directory: " + arg);
        }
        else if (cmd == "ls")
        {
            std::error_code ec;
            const fs::path  dir = arg.empty() ? fs::path(cwd_) : (fs::path(cwd_) / arg);
            std::vector<std::string> entries;
            for (const auto& en : fs::directory_iterator(dir, ec))
                entries.push_back(en.path().filename().string() + (en.is_directory(ec) ? "/" : ""));
            std::sort(entries.begin(), entries.end());
            if (entries.empty())
                print_line("(empty)");
            for (const auto& e : entries)
                print_line("  " + e);
        }
        else if (cmd == "new")
        {
            con.new_cart();
            print_line("new cart");
        }
        else if (cmd == "load")
        {
            if (arg.empty())
            {
                print_line("usage: load <name>");
                return;
            }
            std::error_code          ec;
            std::string              found;
            for (const std::string& cand : {arg, arg + ".lz100", arg + ".png", arg + ".lua"})
            {
                const fs::path p = fs::path(cwd_) / cand;
                if (fs::exists(p, ec))
                {
                    found = p.generic_string();
                    break;
                }
            }
            if (found.empty())
                print_line("not found: " + arg);
            else if (con.load_cart_file(found))
                print_line("loaded " + strip_root(found));
            else
                print_line("load failed");
        }
        else if (cmd == "save")
        {
            if (arg.empty())
            {
                print_line("usage: save <name>");
                return;
            }
            // Blank means EVERY section is untouched - sprites-only or music-only carts (art
            // or tunes drafted before any code) are real work and must save fine.
            if (con.cart_blank())
            {
                print_line("nothing to save: cart is empty");
                print_line("load one, or draw/compose/code something first");
                return;
            }
            std::string name = arg;
            if (name.find('.') == std::string::npos)
                name += ".lz100";
            const std::string p = (fs::path(cwd_) / name).generic_string();
            if (con.save_cart_file(p))
                print_line("saved " + strip_root(p));
            else
                print_line("save failed");
        }
        else
            print_line("unknown command: " + cmd);
    }

    void Shell::complete()
    {
        const size_t      sp     = input_.find_last_of(" \t");
        const bool        first  = sp == std::string::npos;
        const std::string prefix = first ? input_ : input_.substr(sp + 1);
        std::string       head   = first ? std::string() : input_.substr(0, sp + 1);

        std::vector<std::string> matches;
        if (first)
        {
            for (const char* c : kCommands)
                if (starts_with(c, prefix))
                    matches.emplace_back(c);
        }
        else
        {
            namespace fs = std::filesystem;
            fs::path    base       = cwd_;
            std::string filePrefix = prefix;
            const size_t slash      = prefix.find_last_of("/\\");
            if (slash != std::string::npos)
            {
                base       = fs::path(cwd_) / prefix.substr(0, slash);
                filePrefix = prefix.substr(slash + 1);
                head += prefix.substr(0, slash + 1);
            }
            std::error_code ec;
            for (const auto& en : fs::directory_iterator(base, ec))
            {
                std::string name = en.path().filename().string();
                if (starts_with(name, filePrefix))
                    matches.push_back(name + (en.is_directory(ec) ? "/" : ""));
            }
        }

        if (matches.empty())
            return;
        if (matches.size() == 1)
        {
            input_ = head + matches[0];
            if (first)
                input_ += ' ';
            return;
        }
        // Multiple: complete to the longest common prefix and list the options.
        std::string lcp = matches[0];
        for (const auto& m : matches)
        {
            size_t i = 0;
            while (i < lcp.size() && i < m.size() && lcp[i] == m[i])
                ++i;
            lcp.resize(i);
        }
        input_ = head + lcp;
        std::string listed;
        for (const auto& m : matches)
        {
            listed += m;
            listed += "  ";
        }
        print_line(listed);
    }

    void Shell::history_prev()
    {
        if (history_.empty())
            return;
        if (hist_pos_ == static_cast<int>(history_.size()))
            stash_ = input_; // save the line being edited
        if (hist_pos_ > 0)
            --hist_pos_;
        input_ = history_[hist_pos_];
    }

    void Shell::history_next()
    {
        if (hist_pos_ >= static_cast<int>(history_.size()))
            return;
        ++hist_pos_;
        input_ = (hist_pos_ == static_cast<int>(history_.size())) ? stash_ : history_[hist_pos_];
    }

    void Shell::update(Console& con)
    {
        Keyboard& kb = con.keyboard();

        for (char c : kb.text())
            if (static_cast<unsigned char>(c) >= 0x20 || (static_cast<unsigned char>(c) & 0x80))
                input_.push_back(c);

        if (kb.repeat(Keyboard::Backspace))
            erase_last_utf8(input_);
        if (kb.pressed(Keyboard::Tab))
            complete();
        if (kb.pressed(Keyboard::Up))
            history_prev();
        if (kb.pressed(Keyboard::Down))
            history_next();
        if (kb.pressed(Keyboard::Return))
        {
            print_line(display_cwd(cwd_) + "> " + input_);
            if (!input_.empty())
                history_.push_back(input_);
            exec(con, input_);
            input_.clear();
            hist_pos_ = static_cast<int>(history_.size());
            stash_.clear();
        }
        ++blink_;
    }

    void Shell::draw(Console&, Framebuffer& fb)
    {
        fb.cls(0);
        const int lh       = font::line_height();
        const int maxlines = (static_cast<int>(kScreenH) - lh - 4) / lh;

        const int start = lines_.size() > static_cast<size_t>(maxlines)
                              ? static_cast<int>(lines_.size()) - maxlines
                              : 0;
        int       y     = 4;
        for (int i = start; i < static_cast<int>(lines_.size()); ++i)
        {
            font::print(fb, lines_[i].c_str(), 4, y, 6);
            y += lh;
        }

        const std::string prompt = display_cwd(cwd_) + "> " + input_;
        const int         cx     = font::print(fb, prompt.c_str(), 4, y, 7);
        if ((blink_ / 20) % 2 == 0)
            fb.rectfill(cx, y, cx + 1, y + lh - 3, 7); // blinking cursor
    }
} // namespace lazy100
