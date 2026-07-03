#pragma once

#include <string>
#include <vector>

namespace lazy100
{
    class Console;
    class Framebuffer;

    // The boot command line (a retro boot screen crossed with a Linux shell): a working
    // directory + ls/cd/pwd, Tab completion, and Up/Down history. Draws itself with the
    // built-in font. Cart commands (load/save/run/new) resolve relative to the cwd.
    class Shell
    {
    public:
        Shell();

        void update(Console& con);
        void draw(Console& con, Framebuffer& fb);

    private:
        void print_line(const std::string& s);
        void exec(Console& con, const std::string& line);
        void complete();          // Tab completion of the last token
        void history_prev();      // Up
        void history_next();      // Down

        std::vector<std::string> lines_;   // scrolling output
        std::vector<std::string> history_; // submitted commands
        std::string              input_;         // current input line
        std::string              cwd_ = "carts"; // working dir, sandboxed under carts/
        std::string              stash_;   // in-progress line saved while browsing history
        int                      hist_pos_ = 0; // index into history_ (== size() when editing)
        int                      blink_    = 0;
        // Save-time metadata prompt: exporting a .lz100.png asks for title then author (the
        // cartridge footer needs them). 0 = not prompting, 1 = asking title, 2 = asking author.
        int         prompt_ = 0;
        std::string pending_save_; // the .lz100.png path awaiting the title/author answers
    };
} // namespace lazy100
