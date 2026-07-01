#include "lazy100/editor/sfx_editor.hpp"

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
        // Step-list geometry.
        constexpr int kListX = 2, kListY = 38, kListW = 316, kListH = 180;
        constexpr int kRow0 = 58, kRowH = 14, kVisible = 11; // visible step rows (32 total -> scroll)
        constexpr int kBarX = 210, kBarW = 100;

        // Field columns (absolute x).
        constexpr int kcStep = 16, kcNote = 60, kcWave = 120, kcVol = 168;

        const char* kWaveShort[5] = {"SQ", "PU", "TR", "SW", "NS"};

        u8 wave_color(int w)
        {
            static const u8 c[5] = {12, 14, 11, 9, 6}; // blue/pink/green/orange/gray
            return c[std::clamp(w, 0, 4)];
        }

        // pitch 0 = C2, 24 = C4, ... -> classic tracker note text ("C-4", "C#4").
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
    } // namespace

    void SfxEditor::update(Console& con)
    {
        SoundBank&      bank = con.sounds();
        SfxPattern&     pat  = bank.sfx[current_];
        const Keyboard& kb   = con.keyboard();

        auto activate = [&](SfxNote& n)
        {
            if (n.vol == 0)
            {
                n.vol  = static_cast<u8>(cur_vol_ > 0 ? cur_vol_ : 5);
                n.wave = static_cast<u8>(cur_wave_);
            }
        };
        auto shift = [&](int d)
        {
            SfxNote& n = pat.notes[sel_step_];
            activate(n);
            n.pitch = static_cast<u8>(std::clamp(n.pitch + d, 0, 63));
        };

        if (kb.repeat(Keyboard::Up))
            sel_step_ = std::max(0, sel_step_ - 1);
        if (kb.repeat(Keyboard::Down))
            sel_step_ = std::min(31, sel_step_ + 1);
        if (kb.repeat(Keyboard::Left))
            shift(-1); // semitone down
        if (kb.repeat(Keyboard::Right))
            shift(+1); // semitone up
        if (kb.repeat(Keyboard::PageUp))
            shift(+12); // octave up
        if (kb.repeat(Keyboard::PageDown))
            shift(-12); // octave down
        if (kb.pressed(Keyboard::Home))
            sel_step_ = 0;
        if (kb.pressed(Keyboard::End))
            sel_step_ = 31;
        if (kb.repeat(Keyboard::Delete) || kb.repeat(Keyboard::Backspace))
            pat.notes[sel_step_].vol = 0; // rest
        if (!kb.ctrl() && kb.pressed(Keyboard::Tab)) // cycle the selected step's waveform
        {
            cur_wave_ = (cur_wave_ + 1) % 5;
            SfxNote& n = pat.notes[sel_step_];
            if (n.vol == 0)
                n.vol = static_cast<u8>(cur_vol_ > 0 ? cur_vol_ : 5);
            n.wave = static_cast<u8>(cur_wave_);
        }
        if (kb.pressed(Keyboard::Return))
            con.audio().play_sfx(pat, 0);

        for (char ch : kb.text())
        {
            if (ch >= '0' && ch <= '7') // type a digit to set the selected step's volume
            {
                pat.notes[sel_step_].vol = static_cast<u8>(ch - '0');
                cur_vol_                 = ch - '0';
            }
            else if (ch == ' ') // space previews the pattern
                con.audio().play_sfx(pat, 0);
        }
    }

    void SfxEditor::draw(Console& con, Framebuffer& fb)
    {
        SoundBank&   bank = con.sounds();
        SfxPattern&  pat  = bank.sfx[current_];
        const Mouse& m    = con.mouse();

        ui::clear(fb, EditorHost::kTabH);

        // ---- toolbar: pattern nav, speed, play ----
        ui::panel(fb, 4, 18, 312, 18);
        if (ui::icon_button(fb, m, 8, 20, 14, 14, icon::Prev))
            current_ = (current_ + SoundBank::kSfxCount - 1) % SoundBank::kSfxCount;
        if (ui::icon_button(fb, m, 24, 20, 14, 14, icon::Next))
            current_ = (current_ + 1) % SoundBank::kSfxCount;
        char hdr[16];
        std::snprintf(hdr, sizeof(hdr), "SFX %02d", current_);
        font::print(fb, hdr, 44, 21, ui::kText);

        font::print(fb, "SPD", 104, 21, ui::kDim);
        if (ui::icon_button(fb, m, 130, 20, 14, 14, icon::Minus))
            pat.speed = static_cast<u8>(std::max(1, pat.speed - 1));
        char spd[6];
        std::snprintf(spd, sizeof(spd), "%2d", pat.speed);
        font::print(fb, spd, 146, 21, ui::kText);
        if (ui::icon_button(fb, m, 160, 20, 14, 14, icon::Plus))
            pat.speed = static_cast<u8>(std::min(63, pat.speed + 1));

        font::print(fb, "arrows tune", 186, 21, ui::kDim);
        if (ui::icon_button(fb, m, 292, 20, 20, 14, icon::Play))
            con.audio().play_sfx(pat, 0);

        // ---- step list ----
        ui::panel(fb, kListX, kListY, kListW, kListH);
        font::print(fb, "ST", kcStep, 42, ui::kDim);
        font::print(fb, "NOTE", kcNote, 42, ui::kDim);
        font::print(fb, "WAV", kcWave, 42, ui::kDim);
        font::print(fb, "VOL", kcVol, 42, ui::kDim);
        font::print(fb, "LEVEL", kBarX, 42, ui::kDim);
        ui::divider(fb, kListX + 4, 55, kListW - 8, ui::kBorder);

        // Keep the cursor row in view.
        if (sel_step_ < top_)
            top_ = sel_step_;
        else if (sel_step_ >= top_ + kVisible)
            top_ = sel_step_ - kVisible + 1;
        top_ = std::clamp(top_, 0, 32 - kVisible);

        for (int i = 0; i < kVisible; ++i)
        {
            const int step = top_ + i;
            if (step >= 32)
                break;
            const int      y   = kRow0 + i * kRowH;
            SfxNote&       n   = pat.notes[step];
            const bool     sel = (step == sel_step_);

            if (sel)
            {
                fb.rectfill(kListX + 5, y - 2, kListX + kListW - 6, y + kRowH - 4, ui::kBtn);
                fb.rectfill(kListX + 5, y - 2, kListX + 7, y + kRowH - 4, ui::kAccent); // left accent
            }
            else if (step % 4 == 0)
                fb.rectfill(kListX + 5, y - 2, kListX + kListW - 6, y + kRowH - 4, 1); // beat tint

            char sn[4];
            std::snprintf(sn, sizeof(sn), "%02d", step);
            font::print(fb, sn, kcStep, y, sel ? ui::kText : ui::kDim);

            if (n.vol == 0)
            {
                font::print(fb, "---", kcNote, y, 5);
                font::print(fb, "--", kcWave, y, 5);
                font::print(fb, ".", kcVol, y, 5);
            }
            else
            {
                char nn[6];
                fmt_note(n, nn, sizeof(nn));
                font::print(fb, nn, kcNote, y, wave_color(n.wave));
                font::print(fb, kWaveShort[std::clamp<int>(n.wave, 0, 4)], kcWave, y, ui::kText);
                char vv[3];
                std::snprintf(vv, sizeof(vv), "%d", n.vol);
                font::print(fb, vv, kcVol, y, ui::kText);
                const int bw = n.vol * kBarW / 7;
                fb.rectfill(kBarX, y + 1, kBarX + bw, y + kRowH - 6, wave_color(n.wave));
            }

            // Row select (left) / clear (right).
            if (ui::hit(m, kListX + 5, y - 2, kListW - 10, kRowH))
            {
                if (m.pressed(Mouse::Left))
                    sel_step_ = step;
                if (m.pressed(Mouse::Right))
                    n.vol = 0;
            }
        }

        // ---- waveform picker (sets the selected step + pen) ----
        for (int w = 0; w < 5; ++w)
        {
            const int x0 = 8 + w * 20;
            if (ui::icon_button(fb, m, x0, 221, 18, 16, static_cast<icon::Id>(icon::WaveSquare + w), w == cur_wave_))
            {
                cur_wave_ = w;
                SfxNote& n = pat.notes[sel_step_];
                if (n.vol == 0)
                    n.vol = static_cast<u8>(cur_vol_ > 0 ? cur_vol_ : 5);
                n.wave = static_cast<u8>(w);
            }
        }

        // ---- volume picker (sets the selected step + pen; 0 = rest) ----
        ui::vdivider(fb, 118, 221, 16, ui::kBorder);
        for (int v = 0; v <= 7; ++v)
        {
            const int  x0  = 126 + v * 22;
            const bool sel = (v == cur_vol_);
            fb.rectfill(x0, 221, x0 + 17, 236, sel ? ui::kBtnActive : ui::kBtn);
            draw::rect(fb, x0, 221, x0 + 17, 236, sel ? ui::kBorderHi : ui::kBorder);
            const int bh = v * 2;
            if (bh > 0)
                fb.rectfill(x0 + 6, 235 - bh, x0 + 11, 235, sel ? ui::kBg : wave_color(cur_wave_));
            if (ui::hit(m, x0, 221, 18, 16) && m.pressed(Mouse::Left))
            {
                cur_vol_                 = v;
                pat.notes[sel_step_].vol = static_cast<u8>(v);
                if (v > 0)
                    pat.notes[sel_step_].wave = static_cast<u8>(cur_wave_);
            }
        }

        ui::help_button(fb, con, m, 274, 20, 3,
                        "SFX TRACKER\n"
                        "up/down: select step\n"
                        "left/right: +/- semitone\n"
                        "pgup/pgdn: +/- octave\n"
                        "0-7: volume   tab: waveform\n"
                        "del: rest   home/end: ends\n"
                        "space / enter: play\n"
                        "or click a row, then pickers");
    }
} // namespace lazy100
