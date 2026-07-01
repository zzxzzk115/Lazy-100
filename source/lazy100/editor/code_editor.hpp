#pragma once

#include "lazy100/editor/editor.hpp"

#include <string>
#include <vector>

namespace lazy100
{
    // Edit the cart's Lua source (Console::code()) as a line buffer: cursor movement, insert /
    // delete / newline, Tab indent, vertical scroll. Text is UTF-8 and the cursor steps whole
    // codepoints, so 中日韩 comments/strings edit correctly. The buffer is the single source of
    // truth kept in sync with Console::code(), so ESC -> shell `run`/`save` see the latest text.
    class CodeEditor : public Editor
    {
    public:
        const char* name() const override { return "CODE"; }
        void        update(Console& con) override;
        void        draw(Console& con, Framebuffer& fb) override;

    private:
        void sync_from(const std::string& code); // (re)load line buffer if code changed externally
        void flush_to(std::string& code) const;  // rejoin lines -> code

        std::vector<std::string> lines_ {""};
        std::string              cache_;    // last code we synced, to detect external edits
        int                      cx_   = 0; // cursor byte offset within the line (codepoint boundary)
        int                      cy_   = 0; // cursor line index
        int                      top_  = 0; // first visible line
        int                      blink_ = 0; // caret blink counter
    };
} // namespace lazy100
