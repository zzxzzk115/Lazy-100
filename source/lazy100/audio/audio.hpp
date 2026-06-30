#pragma once

#include <memory>

namespace lazy100
{
    // Audio output via miniaudio. v1 is a square-wave beeper: sfx() pushes a sound id onto a
    // lock-free queue that the audio callback drains into a few voices. That queue boundary is
    // where a real synth/tracker plugs in later. miniaudio lives entirely in audio.cpp.
    class Audio
    {
    public:
        Audio();
        ~Audio();

        Audio(const Audio&)            = delete;
        Audio& operator=(const Audio&) = delete;

        bool init();
        void shutdown();

        // Trigger sound effect `n` (maps to a note). Safe to call from the main/Lua thread.
        void trigger_sfx(int n);

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };
} // namespace lazy100
