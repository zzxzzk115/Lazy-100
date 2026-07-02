#pragma once

#include "lazy100/common/types.hpp"

#include <array>

namespace lazy100
{
    // The tracker data model, shared by the audio engine, the cart (de)serializer, and the
    // sfx/music editors. Plain-old-data so a whole SfxPattern / SoundBank can be snapshotted
    // (copied) across the lock-free queue boundary to the audio thread.

    enum class Wave : u8
    {
        Square = 0,
        Pulse, // 25% duty
        Triangle,
        Saw,
        Noise,
        Count
    };

    // One step of an sfx: a pitch (semitones from C2), waveform, volume (0 = rest/silent),
    // and an effect slot (0 = none; higher values reserved for slide/vibrato/fades).
    struct SfxNote
    {
        u8 pitch  = 24; // 0..63 semitones above C2 (24 = C4)
        u8 wave   = 0;  // Wave
        u8 vol    = 0;  // 0..7 (0 = silent)
        u8 effect = 0;  // 0..7
    };

    struct SfxPattern
    {
        static constexpr int kSteps = 32;
        std::array<SfxNote, kSteps> notes {};
        u8                          speed = 8; // 1/120 s ticks per step (bigger = slower)
    };

    // One row of the song: the 4 channels play their sfx together, and rows advance in sequence.
    // A channel set to 255 rests (silent) for this pattern. The sequence ends at the first Stop
    // pattern, the first fully-empty pattern (all four 255), or the end of the table; at the end
    // playback jumps back to the most recent LoopStart pattern (default: where music() began),
    // unless Stop was set, which halts instead.
    struct MusicPattern
    {
        static constexpr int kChannels = 4;
        static constexpr u8  kLoopStart = 0x1; // flags bit: a loop returns to this pattern
        static constexpr u8  kStop      = 0x2; // flags bit: stop after this pattern (no loop)

        std::array<u8, kChannels> sfx {255, 255, 255, 255}; // sfx index per channel, 255 = rest
        u8                        flags = 0;                 // kLoopStart | kStop
    };

    struct SoundBank
    {
        static constexpr int kSfxCount   = 64;
        static constexpr int kMusicCount = 64;

        std::array<SfxPattern, kSfxCount>     sfx {};
        std::array<MusicPattern, kMusicCount> music {};

        void clear()
        {
            sfx = {};
            for (auto& m : music)
                m = MusicPattern {};
        }

        bool blank() const // no audible note and no music row authored
        {
            for (const SfxPattern& p : sfx)
                for (const SfxNote& n : p.notes)
                    if (n.vol > 0)
                        return false;
            for (const MusicPattern& m : music)
            {
                if (m.flags != 0)
                    return false;
                for (const u8 c : m.sfx)
                    if (c != 255)
                        return false;
            }
            return true;
        }
    };
} // namespace lazy100
