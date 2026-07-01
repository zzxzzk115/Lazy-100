#include "lazy100/editor/sfx_editor.hpp"

#include "lazy100/audio/audio.hpp"
#include "lazy100/audio/sound.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/input/keyboard.hpp"
#include "lazy100/input/mouse.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

#include <algorithm>
#include <cstdio>

namespace lazy100
{
    namespace
    {
        constexpr int kSteps = SfxPattern::kSteps; // 32
        constexpr int kGx = 8, kGy = 40, kColW = 9, kSemi = 2;
        constexpr int kGridH  = 64 * kSemi;       // 128
        constexpr int kBottom = kGy + kGridH;     // 168

        constexpr int kWaveY = 176, kVolY = 192, kInfoY = 208;

        const char* kWaveName[5] = {"SQ", "PU", "TR", "SW", "NS"};
        u8          wave_color(int w)
        {
            static const u8 c[5] = {12, 14, 11, 9, 6}; // blue/pink/green/orange/gray
            return c[std::clamp(w, 0, 4)];
        }

        bool inside(int mx, int my, int x, int y, int w, int h)
        {
            return mx >= x && my >= y && mx < x + w && my < y + h;
        }

        // A labeled button; returns true if clicked this frame.
        bool button(Framebuffer& fb, const Mouse& m, int x, int y, int w, int h, const char* label, u8 bg)
        {
            fb.rectfill(x, y, x + w - 1, y + h - 1, bg);
            draw::rect(fb, x, y, x + w - 1, y + h - 1, 6);
            font::print(fb, label, x + 3, y + 2, 7);
            return m.pressed(Mouse::Left) && inside(m.x(), m.y(), x, y, w, h);
        }
    } // namespace

    void SfxEditor::update(Console& con)
    {
        // Interaction is resolved during draw() (buttons need the framebuffer). Only the
        // keyboard shortcuts live here so they work regardless of draw ordering.
        const Keyboard& kb = con.keyboard();
        if (kb.repeat(Keyboard::Return))
            con.audio().play_sfx(con.sounds().sfx[current_], 0);
    }

    void SfxEditor::draw(Console& con, Framebuffer& fb)
    {
        SoundBank&   bank = con.sounds();
        SfxPattern&  pat  = bank.sfx[current_];
        const Mouse& m    = con.mouse();
        const int    mx = m.x(), my = m.y();

        fb.rectfill(0, EditorHost::kTabH, static_cast<int>(320) - 1, 239, 0);

        // Header: pattern + speed + play.
        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "SFX %02d", current_);
        font::print(fb, hdr, kGx, 20, 7);
        if (button(fb, m, 60, 19, 12, 11, "<", 1))
            current_ = (current_ + SoundBank::kSfxCount - 1) % SoundBank::kSfxCount;
        if (button(fb, m, 74, 19, 12, 11, ">", 1))
            current_ = (current_ + 1) % SoundBank::kSfxCount;

        char spd[16];
        std::snprintf(spd, sizeof(spd), "spd %d", pat.speed);
        font::print(fb, spd, 100, 20, 7);
        if (button(fb, m, 148, 19, 12, 11, "-", 1))
            pat.speed = static_cast<u8>(std::max(1, pat.speed - 1));
        if (button(fb, m, 162, 19, 12, 11, "+", 1))
            pat.speed = static_cast<u8>(std::min(63, pat.speed + 1));

        if (button(fb, m, 250, 19, 62, 12, "PLAY", 3))
            con.audio().play_sfx(pat, 0);

        // Grid backdrop + beat guides.
        fb.rectfill(kGx, kGy, kGx + kSteps * kColW - 1, kBottom - 1, 1);
        for (int s = 0; s <= kSteps; s += 4)
            draw::line(fb, kGx + s * kColW, kGy, kGx + s * kColW, kBottom - 1, 0);

        // Paint / clear in the grid.
        if (inside(mx, my, kGx, kGy, kSteps * kColW, kGridH))
        {
            const int step  = (mx - kGx) / kColW;
            const int pitch = std::clamp((kBottom - my) / kSemi, 0, 63);
            if (m.down(Mouse::Left))
                pat.notes[step] = {static_cast<u8>(pitch), static_cast<u8>(cur_wave_), static_cast<u8>(cur_vol_), 0};
            if (m.down(Mouse::Right))
                pat.notes[step].vol = 0;
        }

        // Draw the notes.
        for (int s = 0; s < kSteps; ++s)
        {
            const SfxNote& note = pat.notes[s];
            if (note.vol == 0)
                continue;
            const int x0 = kGx + s * kColW;
            const int py = kBottom - note.pitch * kSemi;
            const u8  c  = wave_color(note.wave);
            fb.rectfill(x0 + 1, py - 1, x0 + kColW - 2, py + note.vol / 2, c); // marker (height ~ volume)
        }
        draw::rect(fb, kGx - 1, kGy - 1, kGx + kSteps * kColW, kBottom, 6);

        // Waveform picker.
        font::print(fb, "wave", kGx, kWaveY - 10, 5);
        for (int w = 0; w < 5; ++w)
        {
            const int x0 = kGx + w * 34;
            if (button(fb, m, x0, kWaveY, 30, 12, kWaveName[w], wave_color(w)))
                cur_wave_ = w;
            if (w == cur_wave_)
                draw::rect(fb, x0 - 1, kWaveY - 1, x0 + 30, kWaveY + 12, 7);
        }

        // Volume picker (0..7).
        font::print(fb, "vol", kGx, kVolY - 10, 5);
        for (int v = 0; v <= 7; ++v)
        {
            const int x0 = kGx + v * 20;
            char      lb[4];
            std::snprintf(lb, sizeof(lb), "%d", v);
            if (button(fb, m, x0, kVolY, 16, 12, lb, static_cast<u8>(v == 0 ? 1 : 3)))
                cur_vol_ = v;
            if (v == cur_vol_)
                draw::rect(fb, x0 - 1, kVolY - 1, x0 + 16, kVolY + 12, 7);
        }

        char info[48];
        std::snprintf(info, sizeof(info), "%s vol %d   Enter/PLAY: preview", kWaveName[std::clamp(cur_wave_, 0, 4)],
                      cur_vol_);
        font::print(fb, info, kGx, kInfoY, 6);
    }
} // namespace lazy100
