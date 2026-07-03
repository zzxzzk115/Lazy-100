#include "lazy100/editor/music_editor.hpp"

#include "lazy100/audio/audio.hpp"
#include "lazy100/audio/sound.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/editor/ui.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/icons.hpp"

#include <algorithm>
#include <cstdio>

namespace lazy100
{
    namespace
    {
        // 4 note columns spanning the pane, a slim step gutter on the left.
        constexpr int kGutterX = 4, kColW = 74, kCol0 = 22;
        constexpr int kGridY = 66, kRowH = 11, kVisible = 12; // note rows (32 total -> scroll)
        constexpr int kHeadY = 40;                            // channel header row
        constexpr int kFlagY = 202;                           // footer legend line

        const char* kWaveShort[8] = {"SQ", "PU", "TR", "SW", "NS", "TS", "OR", "PH"};

        u8 wave_color(int w)
        {
            static const u8 c[8] = {12, 14, 11, 9, 6, 10, 13, 15};
            return c[std::clamp(w, 0, 7)];
        }

        void fmt_note(const SfxNote& n, char* buf, size_t sz)
        {
            static const char* nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            const char*        s   = nm[n.pitch % 12];
            const int          oct = 2 + n.pitch / 12;
            if (s[1] == '#')
                std::snprintf(buf, sz, "%s%d", s, oct);
            else
                std::snprintf(buf, sz, "%s-%d", s, oct);
        }

        // Cycle an sfx-slot value: 0..63 are patterns, 255 = off, wrapping through off.
        u8 step_slot(u8 v, int dir)
        {
            constexpr int kCount = SoundBank::kSfxCount; // 64
            if (dir > 0)
                return v == 255 ? 0 : (v + 1 >= kCount ? 255 : static_cast<u8>(v + 1));
            return v == 255 ? static_cast<u8>(kCount - 1) : (v == 0 ? 255 : static_cast<u8>(v - 1));
        }

        // The sfx pattern behind a channel column, or nullptr when the channel is off.
        SfxPattern* channel_pat(SoundBank& bank, const MusicPattern& mp, int ch)
        {
            return mp.sfx[ch] < SoundBank::kSfxCount ? &bank.sfx[mp.sfx[ch]] : nullptr;
        }

        // One-note audition while editing (loop_start=1/loop_end=0 = exactly one step).
        void preview_note(Console& con, const SfxNote& n, int speed, int channel)
        {
            if (n.vol == 0)
                return;
            SfxPattern p;
            p.speed           = static_cast<u8>(std::clamp(speed, 6, 24));
            p.notes[0]        = n;
            p.notes[0].effect = 0;
            p.loop_start      = 1;
            con.audio().play_sfx(p, channel);
        }
    } // namespace

    void MusicEditor::update(Console& con)
    {
        SoundBank&      bank = con.sounds();
        MusicPattern&   mp   = bank.music[current_];
        const Keyboard& kb   = con.keyboard();

        SfxPattern* pat = channel_pat(bank, mp, sel_ch_);

        auto shift = [&](int d)
        {
            if (!pat)
                return;
            SfxNote& n = pat->notes[sel_row_];
            if (n.vol == 0)
                n.vol = 5;
            n.pitch = static_cast<u8>(std::clamp(n.pitch + d, 0, 63));
            preview_note(con, n, pat->speed, sel_ch_);
        };

        if (kb.repeat(Keyboard::Up))
            sel_row_ = std::max(0, sel_row_ - 1);
        if (kb.repeat(Keyboard::Down))
            sel_row_ = std::min(31, sel_row_ + 1);
        // Left/right move across channel columns (the grid is 2D here, unlike the sfx pane);
        // pitch editing uses - / + and the octave keys.
        if (kb.repeat(Keyboard::Left))
            sel_ch_ = std::max(0, sel_ch_ - 1);
        if (kb.repeat(Keyboard::Right))
            sel_ch_ = std::min(MusicPattern::kChannels - 1, sel_ch_ + 1);
        if (kb.repeat(Keyboard::PageUp))
            shift(+12);
        if (kb.repeat(Keyboard::PageDown))
            shift(-12);
        if (kb.pressed(Keyboard::Home))
            sel_row_ = 0;
        if (kb.pressed(Keyboard::End))
            sel_row_ = 31;
        if (pat && (kb.repeat(Keyboard::Delete) || kb.repeat(Keyboard::Backspace)))
            pat->notes[sel_row_].vol = 0;
        if (pat && !kb.ctrl() && kb.pressed(Keyboard::Tab))
        {
            SfxNote& n = pat->notes[sel_row_];
            n.wave     = static_cast<u8>((n.wave + 1) % 8);
            preview_note(con, n, pat->speed, sel_ch_);
        }
        if (kb.pressed(Keyboard::Return))
            con.audio().play_music(current_, bank);

        for (char ch : kb.text())
        {
            if (pat && ch >= '0' && ch <= '7')
            {
                pat->notes[sel_row_].vol = static_cast<u8>(ch - '0');
                preview_note(con, pat->notes[sel_row_], pat->speed, sel_ch_);
            }
            else if (ch == '-')
                shift(-1);
            else if (ch == '=' || ch == '+')
                shift(+1);
        }
    }

    void MusicEditor::draw(Console& con, Framebuffer& fb)
    {
        SoundBank&   bank    = con.sounds();
        const int    playing = con.audio().music_pattern(); // -1 when stopped
        // While the sequencer runs, follow the pattern it's playing; otherwise show the edit
        // cursor. Stopping falls back to current_ (the row you pressed play on).
        const int     shown = playing >= 0 ? playing : current_;
        MusicPattern& mp    = bank.music[shown];
        const Mouse&  m     = con.mouse();

        ui::clear(fb, EditorHost::kTabH);

        // ---- transport toolbar ----
        ui::panel(fb, 2, 18, 316, 18);
        if (ui::icon_button(fb, m, 6, 20, 14, 14, icon::Prev))
            current_ = (current_ + SoundBank::kMusicCount - 1) % SoundBank::kMusicCount;
        if (ui::icon_button(fb, m, 22, 20, 14, 14, icon::Next))
            current_ = (current_ + 1) % SoundBank::kMusicCount;
        char hdr[16];
        const char* fmt = playing < 0 ? "MUS %02d" : (paused_ ? "MUS %02d =" : "MUS %02d >");
        std::snprintf(hdr, sizeof(hdr), fmt, shown);
        font::print(fb, hdr, 42, 21, playing >= 0 ? ui::kAccent : ui::kText);

        // Pattern chain flags as compact toolbar toggles, lit while set.
        {
            struct
            {
                u8          bit;
                const char* label;
            } kFlags[3] = {{MusicPattern::kLoopStart, "LP<"},
                           {MusicPattern::kLoopEnd, ">LP"},
                           {MusicPattern::kStop, "STOP"}};
            int fx = 106;
            for (const auto& f : kFlags)
            {
                const bool on = (mp.flags & f.bit) != 0;
                const int  w  = font::text_width(f.label) + 8;
                fb.rectfill(fx, 20, fx + w, 33, on ? ui::kBtnActive : ui::kBtn);
                draw::rect(fb, fx, 20, fx + w, 33, on ? ui::kBorderHi : ui::kBorder);
                font::print(fb, f.label, fx + 4, 23, on ? ui::kText : ui::kDim);
                if (ui::hit(m, fx, 20, w + 1, 14) && m.pressed(Mouse::Left))
                    mp.flags ^= f.bit;
                fx += w + 4;
            }
        }

        // Transport. The Play button becomes Pause while the sequencer runs, and a resume arrow
        // while paused. Stop (red) halts and rewinds.
        if (playing < 0)
        {
            paused_ = false; // nothing is running, so nothing is paused
            if (ui::icon_button(fb, m, 268, 20, 20, 14, icon::Play))
                con.audio().play_music(current_, bank);
        }
        else if (!paused_)
        {
            if (ui::icon_button(fb, m, 268, 20, 20, 14, icon::Pause))
            {
                con.audio().pause_music(true);
                paused_ = true;
            }
        }
        else if (ui::icon_button(fb, m, 268, 20, 20, 14, icon::Play))
        {
            con.audio().pause_music(false);
            paused_ = false;
        }
        const bool running = playing >= 0;
        if (ui::icon_button(fb, m, 292, 20, 20, 14, icon::Stop, false, running ? ui::kText : -1,
                            running ? 8 : -1))
        {
            con.audio().stop_music();
            paused_ = false;
        }

        // ---- channel headers: sfx slot per column ----
        ui::panel(fb, 2, 38, 316, 180);
        for (int c = 0; c < MusicPattern::kChannels; ++c)
        {
            const int x = kCol0 + c * kColW;
            char      ch[4];
            std::snprintf(ch, sizeof(ch), "%d", c);
            font::print(fb, ch, x + 2, kHeadY + 5, c == sel_ch_ ? ui::kAccent : ui::kDim);
            if (ui::icon_button(fb, m, x + 10, kHeadY + 2, 14, 14, icon::Minus))
                mp.sfx[c] = step_slot(mp.sfx[c], -1);
            char val[8];
            if (mp.sfx[c] == 255)
                std::snprintf(val, sizeof(val), "--");
            else
                std::snprintf(val, sizeof(val), "%02d", mp.sfx[c]);
            font::print(fb, val, x + 28, kHeadY + 5, mp.sfx[c] == 255 ? ui::kDim : ui::kText);
            if (ui::icon_button(fb, m, x + 44, kHeadY + 2, 14, 14, icon::Plus))
                mp.sfx[c] = step_slot(mp.sfx[c], +1);
            const SfxPattern* pat = channel_pat(bank, mp, c);
            if (pat && pat->loops())
                font::print(fb, "lp", x + 60, kHeadY + 5, ui::kAccent);
        }
        ui::divider(fb, 6, kGridY - 4, 308, ui::kBorder);

        // ---- note grid: 4 columns x visible rows, edited in place ----
        if (sel_row_ < top_)
            top_ = sel_row_;
        else if (sel_row_ >= top_ + kVisible)
            top_ = sel_row_ - kVisible + 1;
        // While the song plays, page-flip to follow the leading channel's playback row.
        if (playing >= 0)
        {
            int lead = -1;
            for (int c = 0; c < MusicPattern::kChannels && lead < 0; ++c)
                lead = con.audio().music_step(c);
            if (lead >= 0 && (lead < top_ || lead >= top_ + kVisible))
                top_ = lead - lead % kVisible;
        }
        top_ = std::clamp(top_, 0, 32 - kVisible);

        for (int i = 0; i < kVisible; ++i)
        {
            const int row = top_ + i;
            const int y   = kGridY + i * kRowH;
            char      rn[4];
            std::snprintf(rn, sizeof(rn), "%02d", row);
            font::print(fb, rn, kGutterX, y, row % 4 == 0 ? ui::kText : ui::kDim);

            for (int c = 0; c < MusicPattern::kChannels; ++c)
            {
                const int   x   = kCol0 + c * kColW;
                SfxPattern* pat = channel_pat(bank, mp, c);
                const bool  sel = (row == sel_row_ && c == sel_ch_);

                const bool playRow = (playing >= 0 && row == con.audio().music_step(c));
                if (sel)
                    fb.rectfill(x, y - 1, x + kColW - 6, y + kRowH - 2, ui::kBtn);
                else if (playRow)
                    fb.rectfill(x, y - 1, x + kColW - 6, y + kRowH - 2, 3); // live playback row
                else if (row % 4 == 0)
                    fb.rectfill(x, y - 1, x + kColW - 6, y + kRowH - 2, 1);

                if (!pat)
                    font::print(fb, ".", x + 4, y, 5);
                else
                {
                    const SfxNote& n = pat->notes[row];
                    if (n.vol == 0)
                        font::print(fb, "...", x + 4, y, 5);
                    else
                    {
                        char nn[6];
                        fmt_note(n, nn, sizeof(nn));
                        font::print(fb, nn, x + 4, y, wave_color(n.wave));
                        font::print(fb, kWaveShort[std::clamp<int>(n.wave, 0, 7)], x + 30, y, ui::kDim);
                        char vv[3];
                        std::snprintf(vv, sizeof(vv), "%d", n.vol);
                        font::print(fb, vv, x + 48, y, ui::kText);
                        fb.rectfill(x + 58, y + 1, x + 58 + n.vol, y + 5, wave_color(n.wave));
                    }
                }

                // Click selects; right-click clears the note.
                if (ui::hit(m, x, y - 1, kColW - 5, kRowH))
                {
                    if (m.pressed(Mouse::Left))
                    {
                        sel_row_ = row;
                        sel_ch_  = c;
                    }
                    if (pat && m.pressed(Mouse::Right))
                        pat->notes[row].vol = 0;
                }
            }
        }

        // Footer key legend.
        font::print(fb, "arrows move  -/+ pitch  pgup/dn octave  0-7 vol  tab wave", 6,
                    kFlagY + 22, ui::kDim);

        ui::help_button(fb, con, m, 244, 20, 4,
                        "MUSIC TRACKER\n"
                        "4 columns = 4 channels of this\n"
                        "pattern; -/+ pick each channel's\n"
                        "sfx ('--' = off), notes edit in\n"
                        "place like the sfx editor.\n"
                        "LP< >LP STOP: chain flags\n"
                        "(loop span / halt).\n"
                        "enter: play this pattern");
    }
} // namespace lazy100
