#include "lazy100/editor/music_editor.hpp"

#include "lazy100/audio/audio.hpp"
#include "lazy100/audio/sound.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/editor/ui.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/icons.hpp"

#include <cstdio>

namespace lazy100
{
    namespace
    {
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

        ui::clear(fb, EditorHost::kTabH);

        // ---- transport toolbar ----
        ui::panel(fb, 2, 18, 316, 18);
        if (ui::icon_button(fb, m, 6, 20, 14, 14, icon::Prev))
            current_ = (current_ + SoundBank::kMusicCount - 1) % SoundBank::kMusicCount;
        if (ui::icon_button(fb, m, 22, 20, 14, 14, icon::Next))
            current_ = (current_ + 1) % SoundBank::kMusicCount;
        char hdr[16];
        std::snprintf(hdr, sizeof(hdr), "MUS %02d", current_);
        font::print(fb, hdr, 42, 21, ui::kText);
        if (ui::icon_button(fb, m, 268, 20, 20, 14, icon::Play))
            con.audio().play_music(current_, bank);
        if (ui::icon_button(fb, m, 292, 20, 20, 14, icon::Stop))
            con.audio().stop_music();

        // ---- channel arrangement ----
        const int ph = ui::titled_panel(fb, 2, 40, 316, 150, icon::TabMusic);
        font::print(fb, "channels play together; patterns chain in sequence", 8, ph + 2, ui::kDim);
        for (int c = 0; c < MusicPattern::kChannels; ++c)
        {
            const int y = ph + 16 + c * 30;
            ui::panel(fb, 8, y, 300, 26, ui::kPanel, ui::kBorder);
            char ch[8];
            std::snprintf(ch, sizeof(ch), "CH%d", c);
            font::print(fb, ch, 14, y + 9, ui::kText);

            if (ui::icon_button(fb, m, 44, y + 5, 16, 16, icon::Minus))
                mp.sfx[c] = step_slot(mp.sfx[c], -1);

            // Value well.
            fb.rectfill(64, y + 5, 99, y + 20, ui::kPanelLo);
            draw::rect(fb, 64, y + 5, 99, y + 20, ui::kBorder);
            char val[8];
            if (mp.sfx[c] == 255)
                std::snprintf(val, sizeof(val), "off");
            else
                std::snprintf(val, sizeof(val), "%02d", mp.sfx[c]);
            font::print(fb, val, 72, y + 9, mp.sfx[c] == 255 ? ui::kDim : ui::kText);

            if (ui::icon_button(fb, m, 104, y + 5, 16, 16, icon::Plus))
                mp.sfx[c] = step_slot(mp.sfx[c], +1);

            if (mp.sfx[c] != 255 && ui::icon_button(fb, m, 130, y + 5, 18, 16, icon::TabSfx))
                con.audio().play_sfx(bank.sfx[mp.sfx[c]], c);
        }

        ui::help_button(fb, con, m, 250, 20, 4,
                        "MUSIC\n"
                        "- / + : set each channel's sfx\n"
                        "< > : prev / next pattern\n"
                        "PLAY / STOP: transport\n"
                        "speaker: preview one channel");
    }
} // namespace lazy100
