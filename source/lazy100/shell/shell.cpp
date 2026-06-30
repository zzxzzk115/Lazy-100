#include "lazy100/shell/shell.hpp"

#include "lazy100/console/config.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

#include <filesystem>

namespace lazy100
{
    namespace
    {
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
    } // namespace

    Shell::Shell()
    {
        print_line("Lazy-100");
        print_line("type 'help' for commands, ESC for the editor");
    }

    void Shell::print_line(const std::string& s) { lines_.push_back(s); }

    void Shell::exec(Console& con, const std::string& line)
    {
        // first token = command, remainder = argument
        size_t            b   = line.find_first_not_of(" \t");
        if (b == std::string::npos)
            return;
        size_t            e   = line.find_first_of(" \t", b);
        const std::string cmd = line.substr(b, e == std::string::npos ? std::string::npos : e - b);
        std::string       arg;
        if (e != std::string::npos)
        {
            size_t ab = line.find_first_not_of(" \t", e);
            if (ab != std::string::npos)
                arg = line.substr(ab);
        }

        if (cmd == "help")
            print_line("commands: help  ls  cls  run  edit  load  save  new");
        else if (cmd == "cls")
            lines_.clear();
        else if (cmd == "edit")
            con.set_mode(ConsoleMode::Editor);
        else if (cmd == "run")
        {
            if (!con.start_cart())
                print_line("no cart loaded");
        }
        else if (cmd == "ls")
        {
            std::error_code ec;
            const std::filesystem::path dir = "examples/carts";
            bool                        any = false;
            for (const auto& en : std::filesystem::directory_iterator(dir, ec))
            {
                const auto ext = en.path().extension().string();
                if (ext == ".lua" || ext == ".lzz")
                {
                    print_line("  " + en.path().filename().string());
                    any = true;
                }
            }
            if (!any)
                print_line("(no carts in examples/carts)");
        }
        else if (cmd == "load" || cmd == "save" || cmd == "new")
            print_line(cmd + ": not yet (M7 cart format)");
        else
            print_line("unknown command: " + cmd);
    }

    void Shell::update(Console& con)
    {
        Keyboard& kb = con.keyboard();

        // Typed characters (filter out control bytes; multibyte CJK passes through).
        for (char c : kb.text())
            if (static_cast<unsigned char>(c) >= 0x20 || (static_cast<unsigned char>(c) & 0x80))
                input_.push_back(c);

        if (kb.repeat(Keyboard::Backspace)) // hold to keep deleting
            erase_last_utf8(input_);
        if (kb.pressed(Keyboard::Return))
        {
            print_line("> " + input_);
            exec(con, input_);
            input_.clear();
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

        const std::string prompt = "> " + input_;
        const int         cx     = font::print(fb, prompt.c_str(), 4, y, 7);
        if ((blink_ / 20) % 2 == 0)
            fb.rectfill(cx, y, cx + 1, y + lh - 3, 7); // blinking cursor
    }
} // namespace lazy100
