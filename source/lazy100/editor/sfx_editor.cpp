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
        // Shared geometry: the note area (bars or tracker rows) then the picker rows.
        constexpr int kListX = 2, kListY = 38, kListW = 316, kListH = 136;
        constexpr int kRow0 = 58, kRowH = 14, kVisible = 8; // tracker: visible rows (32 -> scroll)
        constexpr int kBarX = 210, kBarW = 100;             // tracker: level bar column

        // Bars view: 32 pitch columns inside the same panel.
        constexpr int kBarsX = 10, kBarsY = 52, kBarsW = 9, kBarsH = 100; // columns 9px wide
        constexpr int kVolY  = 158;                                       // volume strip under the bars

        // Field columns (absolute x, tracker view).
        constexpr int kcStep = 16, kcNote = 56, kcWave = 108, kcVol = 150, kcFx = 180;

        const char* kWaveShort[8] = {"SQ", "PU", "TR", "SW", "NS", "TS", "OR", "PH"};
        const char* kFxShort[8]   = {"-", "SL", "VB", "DR", "FI", "FO", "A^", "Av"};

        u8 wave_color(int w)
        {
            static const u8 c[8] = {12, 14, 11, 9, 6, 10, 13, 15}; // + yellow/mauve/peach for the new waves
            return c[std::clamp(w, 0, 7)];
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

        // One-note audition while editing: loop_start=1 with loop_end=0 plays exactly one
        // step, so the pen's pitch/wave/volume is heard the moment it lands on a note.
        void preview_note(Console& con, const SfxNote& n, int speed)
        {
            if (n.vol == 0)
                return;
            SfxPattern p;
            p.speed           = static_cast<u8>(std::clamp(speed, 6, 24)); // audible blip
            p.notes[0]        = n;
            p.notes[0].effect = 0; // effects need neighbours; audition the plain tone
            p.loop_start      = 1;
            con.audio().play_sfx(p, 0);
        }

        // Small +/- number field: draws "LBL nn" with tiny buttons, returns the new value.
        int num_field(Framebuffer& fb, const Mouse& m, int x, int y, const char* label, int v,
                      int lo, int hi)
        {
            font::print(fb, label, x, y + 1, ui::kDim);
            const int bx = x + font::text_width(label) + 3;
            if (ui::icon_button(fb, m, bx, y, 12, 14, icon::Minus))
                v = std::max(lo, v - 1);
            char t[8];
            std::snprintf(t, sizeof(t), "%2d", v);
            font::print(fb, t, bx + 13, y + 1, ui::kText);
            if (ui::icon_button(fb, m, bx + 27, y, 12, 14, icon::Plus))
                v = std::min(hi, v + 1);
            return v;
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
                n.vol    = static_cast<u8>(cur_vol_ > 0 ? cur_vol_ : 5);
                n.wave   = static_cast<u8>(cur_wave_);
                n.effect = static_cast<u8>(cur_fx_);
            }
        };
        auto shift = [&](int d)
        {
            SfxNote& n = pat.notes[sel_step_];
            activate(n);
            n.pitch = static_cast<u8>(std::clamp(n.pitch + d, 0, 63));
            preview_note(con, n, pat.speed);
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
            cur_wave_ = (cur_wave_ + 1) % 8;
            SfxNote& n = pat.notes[sel_step_];
            if (n.vol == 0)
                n.vol = static_cast<u8>(cur_vol_ > 0 ? cur_vol_ : 5);
            n.wave = static_cast<u8>(cur_wave_);
            preview_note(con, n, pat.speed);
        }
        if (kb.pressed(Keyboard::Return))
            con.audio().play_sfx(pat, 0);

        for (char ch : kb.text())
        {
            if (ch >= '0' && ch <= '7') // type a digit to set the selected step's volume
            {
                pat.notes[sel_step_].vol = static_cast<u8>(ch - '0');
                cur_vol_                 = ch - '0';
                preview_note(con, pat.notes[sel_step_], pat.speed);
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

        // ---- toolbar: pattern nav, speed, loop region, view toggle, play ----
        ui::panel(fb, 4, 18, 312, 18);
        if (ui::icon_button(fb, m, 8, 20, 14, 14, icon::Prev))
            current_ = (current_ + SoundBank::kSfxCount - 1) % SoundBank::kSfxCount;
        if (ui::icon_button(fb, m, 24, 20, 14, 14, icon::Next))
            current_ = (current_ + 1) % SoundBank::kSfxCount;
        char hdr[16];
        std::snprintf(hdr, sizeof(hdr), "SFX %02d", current_);
        font::print(fb, hdr, 44, 21, ui::kText);

        pat.speed      = static_cast<u8>(num_field(fb, m, 88, 20, "SPD", pat.speed, 1, 63));
        pat.loop_start = static_cast<u8>(num_field(fb, m, 150, 20, "LP", pat.loop_start, 0, 32));
        pat.loop_end   = static_cast<u8>(num_field(fb, m, 208, 20, ">", pat.loop_end, 0, 32));

        if (ui::icon_button(fb, m, 268, 20, 20, 14, icon::Play))
            con.audio().play_sfx(pat, 0);
        ui::help_button(fb, con, m, 296, 20, 3,
                        "SFX TRACKER\n"
                        "top-left: BAR / TRK view toggle\n"
                        "BAR: L-drag draw pitch, R erase\n"
                        "TRK: up/down select, l/r semitone\n"
                        "pgup/pgdn octave, 0-7 volume\n"
                        "tab waveform, del rest\n"
                        "SPD speed  LP > loop region\n"
                        "space / enter: play");

        ui::panel(fb, kListX, kListY, kListW, kListH);
        if (bars_)
            // ---- bars view: one pitch column per step, volume strip underneath ----
            draw_bars(con, fb, pat);
        else
            draw_tracker(con, fb, pat);

        // ---- pen pickers: waveform / volume / effect rows ----
        font::print(fb, "WAV", 8, 184, ui::kDim);
        for (int w = 0; w < 8; ++w)
        {
            const int x0 = 36 + w * 24;
            if (ui::icon_button(fb, m, x0, 180, 18, 16, static_cast<icon::Id>(icon::WaveSquare + w), w == cur_wave_))
            {
                cur_wave_ = w;
                SfxNote& n = pat.notes[sel_step_];
                if (n.vol == 0)
                    n.vol = static_cast<u8>(cur_vol_ > 0 ? cur_vol_ : 5);
                n.wave = static_cast<u8>(w);
            }
            if (w == cur_wave_)
                font::print(fb, kWaveShort[w], 236, 184, wave_color(w));
        }

        font::print(fb, "VOL", 8, 203, ui::kDim);
        for (int v = 0; v <= 7; ++v)
        {
            const int  x0  = 36 + v * 24;
            const bool sel = (v == cur_vol_);
            fb.rectfill(x0, 199, x0 + 17, 214, sel ? ui::kBtnActive : ui::kBtn);
            draw::rect(fb, x0, 199, x0 + 17, 214, sel ? ui::kBorderHi : ui::kBorder);
            const int bh = v * 2;
            if (bh > 0)
                fb.rectfill(x0 + 6, 213 - bh, x0 + 11, 213, sel ? ui::kBg : wave_color(cur_wave_));
            if (ui::hit(m, x0, 199, 18, 16) && m.pressed(Mouse::Left))
            {
                cur_vol_                 = v;
                pat.notes[sel_step_].vol = static_cast<u8>(v);
                if (v > 0)
                    pat.notes[sel_step_].wave = static_cast<u8>(cur_wave_);
            }
        }

        font::print(fb, "FX", 8, 222, ui::kDim);
        for (int e = 0; e <= 7; ++e)
        {
            const int  x0  = 36 + e * 24;
            const bool sel = (e == cur_fx_);
            fb.rectfill(x0, 218, x0 + 17, 233, sel ? ui::kBtnActive : ui::kBtn);
            draw::rect(fb, x0, 218, x0 + 17, 233, sel ? ui::kBorderHi : ui::kBorder);
            font::print(fb, kFxShort[e], x0 + 3, 222, sel ? ui::kText : ui::kDim);
            if (ui::hit(m, x0, 218, 18, 16) && m.pressed(Mouse::Left))
            {
                cur_fx_ = e;
                if (pat.notes[sel_step_].vol > 0)
                    pat.notes[sel_step_].effect = static_cast<u8>(e);
            }
        }
    }

    void SfxEditor::draw_tools(Console& con, Framebuffer& fb)
    {
        // Top-bar view toggle, classic style: two lit buttons on the bar's left side.
        const Mouse& m = con.mouse();
        struct
        {
            const char* label;
            bool        bars;
        } kViews[2] = {{"BAR", true}, {"TRK", false}};
        int x = 4;
        for (const auto& v : kViews)
        {
            const bool on = (bars_ == v.bars);
            const int  w  = font::text_width(v.label) + 8;
            fb.rectfill(x, 1, x + w, EditorHost::kTabH - 2, on ? ui::kBtnActive : ui::kBtn);
            font::print(fb, v.label, x + 4, 3, on ? ui::kText : ui::kDim);
            if (ui::hit(m, x, 1, w + 1, EditorHost::kTabH - 2) && m.pressed(Mouse::Left))
                bars_ = v.bars;
            x += w + 3;
        }
    }

    void SfxEditor::draw_bars(Console& con, Framebuffer& fb, SfxPattern& pat)
    {
        const Mouse& m = con.mouse();
        font::print(fb, "PITCH", 12, 42, ui::kDim);
        char lp[24];
        if (pat.loops())
            std::snprintf(lp, sizeof(lp), "loop %d-%d", pat.loop_start, pat.loop_end);
        else if (pat.length() < SfxPattern::kSteps)
            std::snprintf(lp, sizeof(lp), "len %d", pat.length());
        else
            std::snprintf(lp, sizeof(lp), "len 32");
        font::print(fb, lp, kListX + kListW - 8 - font::text_width(lp), 42, ui::kDim);

        // Pitch columns. Every 4th step gets a faint beat tint behind it.
        for (int s = 0; s < SfxPattern::kSteps; ++s)
        {
            const int x0 = kBarsX + s * kBarsW;
            if (s % 4 == 0)
                fb.rectfill(x0, kBarsY, x0 + kBarsW - 2, kBarsY + kBarsH, 1);
            if (s == sel_step_)
                fb.rectfill(x0, kBarsY, x0 + kBarsW - 2, kBarsY + kBarsH, ui::kBtn);
            const SfxNote& n = pat.notes[s];
            if (n.vol > 0)
            {
                const int h  = 4 + n.pitch * (kBarsH - 8) / 63;
                const int y1 = kBarsY + kBarsH;
                fb.rectfill(x0, y1 - h, x0 + kBarsW - 2, y1, wave_color(n.wave));
                fb.rectfill(x0, y1 - h, x0 + kBarsW - 2, y1 - h + 1, 7); // cap highlight
            }
            // Volume strip: a short stack under each column.
            const int vy = kVolY;
            for (int v = 0; v < n.vol; ++v)
                fb.rectfill(x0, vy + 12 - v * 2, x0 + kBarsW - 2, vy + 12 - v * 2, wave_color(n.wave));
            if (n.vol == 0)
                fb.rectfill(x0, vy + 12, x0 + kBarsW - 2, vy + 12, 5);
        }
        // Selected step readout: the exact fields the TRK view would show.
        {
            const SfxNote& n = pat.notes[sel_step_];
            char           info[48];
            if (n.vol > 0)
            {
                char nn[6];
                fmt_note(n, nn, sizeof(nn));
                std::snprintf(info, sizeof(info), "ST %02d  %s  %s  V%d  %s", sel_step_, nn,
                              kWaveShort[std::clamp<int>(n.wave, 0, 7)], n.vol,
                              kFxShort[std::clamp<int>(n.effect, 0, 7)]);
            }
            else
                std::snprintf(info, sizeof(info), "ST %02d  ---", sel_step_);
            font::print(fb, info, 12, kVolY + 6, ui::kText);
        }

        // Loop region markers along the bottom edge of the pitch area.
        if (pat.loops())
        {
            const int lx0 = kBarsX + pat.loop_start * kBarsW;
            const int lx1 = kBarsX + std::min<int>(pat.loop_end, 32) * kBarsW - 2;
            fb.rectfill(lx0, kBarsY + kBarsH + 2, lx1, kBarsY + kBarsH + 3, ui::kAccent);
        }

        // Live playback cursor: the column the engine is on right now (channel 0 preview).
        {
            const int ps = con.audio().sfx_step(0);
            if (ps >= 0 && ps < 32)
            {
                const int x0 = kBarsX + ps * kBarsW;
                draw::rect(fb, x0 - 1, kBarsY - 2, x0 + kBarsW - 1, kBarsY + kBarsH + 1, 7);
            }
        }

        // Mouse: drag in the pitch area draws (pen wave/vol/fx) and auditions each new note;
        // right-drag erases; the volume strip drags per-step volume.
        if (ui::hit(m, kBarsX, kBarsY, kBarsW * 32, kBarsH))
        {
            const int s = std::clamp((m.x() - kBarsX) / kBarsW, 0, 31);
            if (m.down(Mouse::Left))
            {
                sel_step_  = s;
                SfxNote& n = pat.notes[s];
                n.pitch    = static_cast<u8>(std::clamp((kBarsY + kBarsH - m.y()) * 63 / (kBarsH - 8), 0, 63));
                n.vol      = static_cast<u8>(cur_vol_ > 0 ? cur_vol_ : 5);
                n.wave     = static_cast<u8>(cur_wave_);
                n.effect   = static_cast<u8>(cur_fx_);
                if (s != aud_step_ || n.pitch != aud_pitch_) // audition only when it changes
                {
                    preview_note(con, n, pat.speed);
                    aud_step_  = s;
                    aud_pitch_ = n.pitch;
                }
            }
            if (m.down(Mouse::Right))
                pat.notes[s].vol = 0;
        }
        if (!m.down(Mouse::Left))
            aud_step_ = aud_pitch_ = -1; // released: the next stroke auditions again
        if (ui::hit(m, kBarsX, kVolY, kBarsW * 32, 14) && m.down(Mouse::Left))
        {
            const int s = std::clamp((m.x() - kBarsX) / kBarsW, 0, 31);
            const int v = std::clamp((kVolY + 13 - m.y()) / 2, 0, 7);
            pat.notes[s].vol = static_cast<u8>(v);
            if (v > 0)
                cur_vol_ = v;
        }
    }

    void SfxEditor::draw_tracker(Console& con, Framebuffer& fb, SfxPattern& pat)
    {
        const Mouse& m = con.mouse();
        font::print(fb, "ST", kcStep, 42, ui::kDim);
        font::print(fb, "NOTE", kcNote, 42, ui::kDim);
        font::print(fb, "WAV", kcWave, 42, ui::kDim);
        font::print(fb, "VOL", kcVol, 42, ui::kDim);
        font::print(fb, "FX", kcFx, 42, ui::kDim);
        font::print(fb, "LEVEL", kBarX, 42, ui::kDim);
        ui::divider(fb, kListX + 4, 55, kListW - 8, ui::kBorder);

        // Keep the cursor row in view.
        if (sel_step_ < top_)
            top_ = sel_step_;
        else if (sel_step_ >= top_ + kVisible)
            top_ = sel_step_ - kVisible + 1;
        // While a preview plays, page-flip to follow the playback cursor.
        {
            const int ps = con.audio().sfx_step(0);
            if (ps >= 0 && (ps < top_ || ps >= top_ + kVisible))
                top_ = ps - ps % kVisible;
        }
        top_ = std::clamp(top_, 0, 32 - kVisible);

        for (int i = 0; i < kVisible; ++i)
        {
            const int step = top_ + i;
            if (step >= 32)
                break;
            const int  y   = kRow0 + i * kRowH;
            SfxNote&   n   = pat.notes[step];
            const bool sel = (step == sel_step_);

            const int  ps      = con.audio().sfx_step(0);
            const bool playing = (step == ps);
            if (sel)
            {
                fb.rectfill(kListX + 5, y - 2, kListX + kListW - 6, y + kRowH - 4, ui::kBtn);
                fb.rectfill(kListX + 5, y - 2, kListX + 7, y + kRowH - 4, ui::kAccent); // left accent
            }
            else if (playing)
                fb.rectfill(kListX + 5, y - 2, kListX + kListW - 6, y + kRowH - 4, 3); // play row
            else if (step % 4 == 0)
                fb.rectfill(kListX + 5, y - 2, kListX + kListW - 6, y + kRowH - 4, 1); // beat tint
            if (playing)
                icon::draw(fb, icon::Play, kListX + kListW - 20, y - 1, 7);

            // Loop region marker: a thin accent line on the row edge.
            if (pat.loops() && step >= pat.loop_start && step < pat.loop_end)
                fb.rectfill(kListX + kListW - 9, y - 2, kListX + kListW - 8, y + kRowH - 4, ui::kAccent);

            char sn[4];
            std::snprintf(sn, sizeof(sn), "%02d", step);
            font::print(fb, sn, kcStep, y, sel ? ui::kText : ui::kDim);

            if (n.vol == 0)
            {
                font::print(fb, "---", kcNote, y, 5);
                font::print(fb, "--", kcWave, y, 5);
                font::print(fb, ".", kcVol, y, 5);
                font::print(fb, ".", kcFx, y, 5);
            }
            else
            {
                char nn[6];
                fmt_note(n, nn, sizeof(nn));
                font::print(fb, nn, kcNote, y, wave_color(n.wave));
                font::print(fb, kWaveShort[std::clamp<int>(n.wave, 0, 7)], kcWave, y, ui::kText);
                char vv[3];
                std::snprintf(vv, sizeof(vv), "%d", n.vol);
                font::print(fb, vv, kcVol, y, ui::kText);
                font::print(fb, kFxShort[std::clamp<int>(n.effect, 0, 7)], kcFx, y,
                            n.effect ? ui::kAccent : 5);
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
    }
} // namespace lazy100
