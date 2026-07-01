#pragma once

#include "lazy100/audio/sound.hpp"

#include <memory>

namespace lazy100
{
    // Audio output via miniaudio. The audio thread runs a small tracker: up to 4 channels, each
    // stepping through an SfxPattern (5 waveforms + a click-free envelope), plus a music
    // sequencer that chains MusicPatterns. The main/Lua thread hands work across via a lock-free
    // queue (sfx) and an atomically-flipped SoundBank snapshot (music). miniaudio lives entirely
    // in audio.cpp.
    class Audio
    {
    public:
        Audio();
        ~Audio();

        Audio(const Audio&)            = delete;
        Audio& operator=(const Audio&) = delete;

        bool init();
        void shutdown();

        // Play `pat` on `channel` (0..3, or -1 to auto-pick a free channel). Thread-safe.
        void play_sfx(const SfxPattern& pat, int channel = -1);
        // Start the music sequencer at pattern `index` using a snapshot of `bank`. Thread-safe.
        void play_music(int index, const SoundBank& bank);
        void stop_music(); // silence the music sequencer

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };
} // namespace lazy100
