#pragma once

#include <string>
#include <vector>

namespace lazy100
{
    class Console;
    class Framebuffer;

    // The boot command line (PICO-8-style): type commands (help/ls/cls/run/edit/...), see
    // scrolling output. Draws itself into the framebuffer with the built-in font. load/save/run
    // gain real behavior once the cart format lands (M7).
    class Shell
    {
    public:
        Shell();

        void update(Console& con);
        void draw(Console& con, Framebuffer& fb);

    private:
        void print_line(const std::string& s);
        void exec(Console& con, const std::string& line);

        std::vector<std::string> lines_; // output history
        std::string              input_; // current input line
        int                      blink_ = 0;
    };
} // namespace lazy100
