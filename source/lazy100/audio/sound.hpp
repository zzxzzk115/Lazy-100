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

    struct MusicPattern
    {
        static constexpr int          kChannels = 4;
        std::array<u8, kChannels>     sfx {255, 255, 255, 255}; // sfx index per channel, 255 = none
        u8                            flags = 0;                // bit0: loop-to-here (reserved)
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
    };
} // namespace lazy100
