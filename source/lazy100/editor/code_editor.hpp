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
        const char*  name() const override { return "CODE"; }
        icon::Id     icon() const override { return icon::TabCode; }
        void         update(Console& con) override;
        void         draw(Console& con, Framebuffer& fb) override;
        cursor::Type cursor(Console& con) const override;

    private:
        void        sync_from(const std::string& code); // (re)load line buffer if code changed externally
        void        flush_to(std::string& code) const;  // rejoin lines -> code
        std::string word_prefix() const;                // identifier chars just before the cursor
        void        refresh_completions();              // rebuild matches_ from the current prefix
        void        accept_completion();                // replace the prefix with the selected match

        std::vector<std::string> lines_ {""};
        std::string              cache_;     // last code we synced, to detect external edits
        int                      cx_    = 0; // cursor byte offset within the line (codepoint boundary)
        int                      cy_    = 0; // cursor line index
        int                      top_   = 0; // first visible line
        int                      blink_ = 0; // caret blink counter

        std::vector<const char*> matches_; // live autocomplete candidates for the current word
        int                      comp_sel_ = 0; // selected candidate in the popup
    };
} // namespace lazy100
