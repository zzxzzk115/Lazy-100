#pragma once

#include "lazy100/editor/editor.hpp"

namespace lazy100
{
    // Arrange the cart's music: each MusicPattern assigns an sfx index to up to 4 channels that
    // play together, and the sequencer chains patterns. This pane edits the 4 channel slots of
    // the current pattern (-/+ to pick an sfx, or "--" for off), selects patterns, and plays /
    // stops via the audio engine. Edits go into Console::sounds() -> the cart's __music__.
    class MusicEditor : public Editor
    {
    public:
        const char* name() const override { return "MUSIC"; }
        icon::Id    icon() const override { return icon::TabMusic; }
        void        draw(Console& con, Framebuffer& fb) override;

    private:
        int current_ = 0; // music pattern index 0..63
    };
} // namespace lazy100
