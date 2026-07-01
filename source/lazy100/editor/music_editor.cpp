#include "lazy100/editor/music_editor.hpp"

#include "lazy100/audio/audio.hpp"
#include "lazy100/audio/sound.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

#include <cstdio>

namespace lazy100
{
    namespace
    {
        bool inside(int mx, int my, int x, int y, int w, int h)
        {
            return mx >= x && my >= y && mx < x + w && my < y + h;
        }
        bool button(Framebuffer& fb, const Mouse& m, int x, int y, int w, int h, const char* label, u8 bg)
        {
            fb.rectfill(x, y, x + w - 1, y + h - 1, bg);
            draw::rect(fb, x, y, x + w - 1, y + h - 1, 6);
            font::print(fb, label, x + 3, y + 2, 7);
            return m.pressed(Mouse::Left) && inside(m.x(), m.y(), x, y, w, h);
        }

        // Cycle an sfx-slot value: 0..63 are patterns, 255 = off, wrapping through off.
        u8 step_slot(u8 v, int dir)
        {
            constexpr int kCount = SoundBank::kSfxCount; // 64
            if (dir > 0)
                return v == 255 ? 0 : (v + 1 >= kCount ? 255 : static_cast<u8>(v + 1));
            return v == 255 ? static_cast<u8>(kCount - 1) : (v == 0 ? 255 : static_cast<u8>(v - 1));
        }
    } // namespace

    void MusicEditor::draw(Console& con, Framebuffer& fb)
    {
        SoundBank&    bank = con.sounds();
        MusicPattern& mp   = bank.music[current_];
        const Mouse&  m    = con.mouse();

        fb.rectfill(0, EditorHost::kTabH, 319, 239, 0);

        // Header: pattern select + transport.
        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "MUSIC %02d", current_);
        font::print(fb, hdr, 8, 20, 7);
        if (button(fb, m, 78, 19, 12, 11, "<", 1))
            current_ = (current_ + SoundBank::kMusicCount - 1) % SoundBank::kMusicCount;
        if (button(fb, m, 92, 19, 12, 11, ">", 1))
            current_ = (current_ + 1) % SoundBank::kMusicCount;
        if (button(fb, m, 190, 19, 56, 12, "PLAY", 3))
            con.audio().play_music(current_, bank);
        if (button(fb, m, 252, 19, 56, 12, "STOP", 2))
            con.audio().stop_music();

        // Four channel slots.
        font::print(fb, "channels play together; sequencer chains patterns", 8, 40, 5);
        for (int c = 0; c < MusicPattern::kChannels; ++c)
        {
            const int y = 56 + c * 26;
            char      lbl[16];
            std::snprintf(lbl, sizeof(lbl), "CH %d", c);
            font::print(fb, lbl, 8, y + 3, 7);

            if (button(fb, m, 52, y, 14, 13, "-", 1))
                mp.sfx[c] = step_slot(mp.sfx[c], -1);

            char val[8];
            if (mp.sfx[c] == 255)
                std::snprintf(val, sizeof(val), "--");
            else
                std::snprintf(val, sizeof(val), "%02d", mp.sfx[c]);
            fb.rectfill(70, y, 99, y + 12, mp.sfx[c] == 255 ? 1 : 3);
            draw::rect(fb, 70, y, 99, y + 12, 6);
            font::print(fb, val, 78, y + 2, 7);

            if (button(fb, m, 104, y, 14, 13, "+", 1))
                mp.sfx[c] = step_slot(mp.sfx[c], +1);

            // Preview just this channel's sfx.
            if (mp.sfx[c] != 255 && button(fb, m, 128, y, 52, 13, "hear", 3))
                con.audio().play_sfx(bank.sfx[mp.sfx[c]], c);
        }

        font::print(fb, "PLAY: from this pattern   STOP: silence", 8, 172, 6);
    }
} // namespace lazy100
