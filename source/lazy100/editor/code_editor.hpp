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
        bool         on_escape(Console& con) override; // closes the cheatsheet overlay first
        cursor::Type cursor(Console& con) const override;

    private:
        void        sync_from(const std::string& code); // (re)load line buffer if code changed externally
        void        flush_to(std::string& code) const;  // rejoin lines -> code
        std::string word_prefix() const;                // identifier chars just before the cursor
        void        refresh_completions();              // rebuild matches_ from the current prefix
        void        accept_completion();                // replace the prefix with the selected match
        void        draw_manual(Framebuffer& fb, int statusY); // the book-icon cheatsheet overlay

        // ---- selection & clipboard ----
        bool        has_sel() const;                                        // non-empty selection?
        void        sel_range(int& x0, int& y0, int& x1, int& y1) const;    // normalized (start<=end)
        std::string sel_text() const;                                       // selected text, '\n'-joined
        void        erase_sel();                                            // delete selection, caret to start
        void        insert_text(const std::string& text);                   // multi-line insert at caret
        void        clip_copy();                                            // selection -> clipboard
        void        clip_cut();                                             // copy + erase
        void        clip_paste();                                           // clipboard -> buffer
        void        select_all();

        std::vector<std::string> lines_ {""};
        std::string              cache_;     // last code we synced, to detect external edits
        int                      cx_    = 0; // cursor byte offset within the line (codepoint boundary)
        int                      cy_    = 0; // cursor line index
        int                      top_   = 0; // first visible line
        int                      left_  = 0; // horizontal scroll, in pixels (follows the caret)
        int                      blink_ = 0; // caret blink counter
        bool                     caret_moved_ = true; // did the caret move this frame? (scroll-follow gate)

        std::vector<const char*> matches_; // live autocomplete candidates for the current word
        int                      comp_sel_ = 0; // selected candidate in the popup

        bool manual_open_   = false; // book-icon cheatsheet overlay (API signatures by category)
        int  manual_scroll_ = 0;     // first visible cheatsheet row

        // Selection: anchor (set when the selection starts) .. caret. Byte offsets like cx_/cy_.
        bool sel_active_ = false;
        int  sel_ax_ = 0, sel_ay_ = 0;
        bool mouse_sel_ = false; // a left-drag selection is in progress

        // Right-click context menu (cut / copy / paste / select all).
        bool ctx_open_ = false;
        int  ctx_x_ = 0, ctx_y_ = 0; // top-left, framebuffer coords
    };
} // namespace lazy100
