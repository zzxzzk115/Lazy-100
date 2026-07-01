#include "lazy100/audio/audio.hpp"

#include "lazy100/common/log.hpp"

#ifndef NOMINMAX
#    define NOMINMAX // keep windows.h (pulled by miniaudio) from defining min/max macros
#endif
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace lazy100
{
    namespace
    {
        constexpr int kQueueSize = 64;
        constexpr int kChannels  = MusicPattern::kChannels; // 4

        // Semitone `pitch` above C2 -> Hz. pitch 24 = C4 (~261.6 Hz).
        double pitch_freq(int pitch) { return 65.406 * std::pow(2.0, pitch / 12.0); }

        // One channel: a voice stepping through a copy of an SfxPattern.
        struct Channel
        {
            bool       active    = false;
            bool       from_music = false;
            SfxPattern pat;
            int        step        = 0;
            double     step_samples = 0.0; // duration of the current step in samples
            double     into_step    = 0.0; // samples elapsed in the current step
            double     phase        = 0.0;
            u32        noise        = 0x2545F491u; // per-voice PRNG state (noise wave)
            double     last_noise   = 0.0;
            double     noise_hold   = 0.0;
        };

        // Sample the waveform at phase [0,1) for the given Channel (noise needs state).
        double wave_sample(Wave w, double phase, Channel& c, double freq, double sr)
        {
            switch (w)
            {
                case Wave::Square: return (std::fmod(phase, 1.0) < 0.5) ? 1.0 : -1.0;
                case Wave::Pulse: return (std::fmod(phase, 1.0) < 0.25) ? 1.0 : -1.0;
                case Wave::Triangle: return 4.0 * std::fabs(std::fmod(phase, 1.0) - 0.5) - 1.0;
                case Wave::Saw: return 2.0 * std::fmod(phase, 1.0) - 1.0;
                case Wave::Noise:
                {
                    // Re-roll white noise at roughly the note frequency for a pitched hiss.
                    c.noise_hold -= freq / sr;
                    if (c.noise_hold <= 0.0)
                    {
                        c.noise ^= c.noise << 13;
                        c.noise ^= c.noise >> 17;
                        c.noise ^= c.noise << 5;
                        c.last_noise = (static_cast<double>(c.noise) / 2147483647.5) - 1.0;
                        c.noise_hold += 1.0;
                    }
                    return c.last_noise;
                }
                default: return 0.0;
            }
        }
    } // namespace

    struct Audio::Impl
    {
        ma_device device {};
        bool      started    = false;
        double    sampleRate = 44100.0;

        // ---- sfx: SPSC lock-free queue (producer = main/Lua thread) ----
        struct Msg
        {
            SfxPattern pat;
            int        channel; // 0..3, or -1 = auto
        };
        std::atomic<uint32_t> head {0};
        std::atomic<uint32_t> tail {0};
        Msg                   queue[kQueueSize] {};

        // ---- music: atomically-flipped SoundBank snapshot + request seqlock ----
        SoundBank             bankBuf[2] {};
        std::atomic<int>      bankActive {0};
        std::atomic<uint32_t> musicSeq {0};
        int                   musicReqIndex = -1; // written before bumping musicSeq

        // ---- audio-thread state ----
        Channel  channels[kChannels];
        bool     musicOn   = false;
        int      musicCur   = 0;
        bool     musicLoad  = false; // load musicCur at the next opportunity
        uint32_t lastSeq    = 0;

        void start_channel(int ch, const SfxPattern& pat, bool fromMusic)
        {
            Channel& c   = channels[ch];
            c.active     = true;
            c.from_music = fromMusic;
            c.pat        = pat;
            c.step       = 0;
            c.into_step  = 0.0;
            c.phase      = 0.0;
            c.step_samples = pat.speed * (sampleRate / 120.0);
        }

        int pick_channel()
        {
            for (int i = 0; i < kChannels; ++i)
                if (!channels[i].active)
                    return i;
            return 0; // all busy: steal channel 0
        }

        void load_music_pattern()
        {
            const SoundBank& bank = bankBuf[bankActive.load(std::memory_order_acquire)];
            const MusicPattern& mp = bank.music[musicCur % SoundBank::kMusicCount];
            bool any = false;
            for (int c = 0; c < kChannels; ++c)
            {
                const u8 idx = mp.sfx[c];
                if (idx < SoundBank::kSfxCount)
                {
                    start_channel(c, bank.sfx[idx], true);
                    any = true;
                }
            }
            if (!any) // empty pattern -> stop, don't spin
                musicOn = false;
        }

        void advance_music_if_idle()
        {
            if (!musicOn)
                return;
            for (int c = 0; c < kChannels; ++c)
                if (channels[c].active && channels[c].from_music)
                    return; // still playing this pattern
            // All music channels finished: advance (loop back to 0 at the end).
            musicCur = (musicCur + 1) % SoundBank::kMusicCount;
            musicLoad = true;
        }

        static void render(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames)
        {
            auto* im = static_cast<Impl*>(dev->pUserData);
            im->render_block(static_cast<float*>(out), frames);
        }

        void render_block(float* o, ma_uint32 frames)
        {
            // Drain queued sfx triggers.
            uint32_t t = tail.load(std::memory_order_relaxed);
            while (t != head.load(std::memory_order_acquire))
            {
                const Msg& m  = queue[t % kQueueSize];
                const int  ch = (m.channel < 0 || m.channel >= kChannels) ? pick_channel() : m.channel;
                start_channel(ch, m.pat, false);
                ++t;
            }
            tail.store(t, std::memory_order_release);

            // Handle a new music request (start/stop).
            const uint32_t seq = musicSeq.load(std::memory_order_acquire);
            if (seq != lastSeq)
            {
                lastSeq = seq;
                const int idx = musicReqIndex;
                if (idx < 0)
                {
                    musicOn = false;
                    for (auto& c : channels)
                        if (c.from_music)
                            c.active = false;
                }
                else
                {
                    musicOn   = true;
                    musicCur  = idx;
                    musicLoad = true;
                }
            }

            for (ma_uint32 f = 0; f < frames; ++f)
            {
                if (musicLoad)
                {
                    musicLoad = false;
                    load_music_pattern();
                }

                double s = 0.0;
                for (auto& c : channels)
                {
                    if (!c.active)
                        continue;
                    const SfxNote& note = c.pat.notes[c.step];
                    if (note.vol > 0)
                    {
                        const double freq = pitch_freq(note.pitch);
                        const double raw  = wave_sample(static_cast<Wave>(note.wave % static_cast<int>(Wave::Count)),
                                                        c.phase, c, freq, sampleRate);
                        // Click-free envelope: short attack in, short release out of each step.
                        const double atk = std::min(64.0, c.step_samples * 0.1);
                        const double rel = std::min(512.0, c.step_samples * 0.25);
                        double       env = 1.0;
                        if (c.into_step < atk)
                            env = c.into_step / atk;
                        else if (c.into_step > c.step_samples - rel)
                            env = std::max(0.0, (c.step_samples - c.into_step) / rel);
                        s += raw * env * (note.vol / 7.0) * 0.22;
                        c.phase += freq / sampleRate;
                    }

                    c.into_step += 1.0;
                    if (c.into_step >= c.step_samples)
                    {
                        c.into_step = 0.0;
                        c.phase     = 0.0;
                        ++c.step;
                        if (c.step >= SfxPattern::kSteps)
                            c.active = false;
                    }
                }

                advance_music_if_idle();

                if (s > 1.0)
                    s = 1.0;
                if (s < -1.0)
                    s = -1.0;
                o[f * 2 + 0] = static_cast<float>(s);
                o[f * 2 + 1] = static_cast<float>(s);
            }
        }
    };

    Audio::Audio()  = default;
    Audio::~Audio() { shutdown(); }

    bool Audio::init()
    {
        p_       = std::make_unique<Impl>();
        Impl& im = *p_;

        ma_device_config cfg  = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate        = 44100;
        cfg.dataCallback      = &Impl::render;
        cfg.pUserData         = &im;
        if (ma_device_init(nullptr, &cfg, &im.device) != MA_SUCCESS)
        {
            LZ_WARN("audio: device init failed; sound disabled");
            p_.reset();
            return false;
        }
        im.sampleRate = im.device.sampleRate;
        if (ma_device_start(&im.device) != MA_SUCCESS)
        {
            LZ_WARN("audio: device start failed; sound disabled");
            ma_device_uninit(&im.device);
            p_.reset();
            return false;
        }
        im.started = true;
        LZ_INFO("audio: started (%u Hz)", static_cast<unsigned>(im.sampleRate));
        return true;
    }

    void Audio::shutdown()
    {
        if (!p_)
            return;
        if (p_->started)
            ma_device_uninit(&p_->device);
        p_.reset();
    }

    void Audio::play_sfx(const SfxPattern& pat, int channel)
    {
        if (!p_)
            return;
        Impl&          im = *p_;
        const uint32_t h  = im.head.load(std::memory_order_relaxed);
        if (h - im.tail.load(std::memory_order_acquire) >= kQueueSize)
            return; // queue full, drop
        im.queue[h % kQueueSize] = {pat, channel};
        im.head.store(h + 1, std::memory_order_release);
    }

    void Audio::play_music(int index, const SoundBank& bank)
    {
        if (!p_)
            return;
        Impl&     im       = *p_;
        const int inactive = 1 - im.bankActive.load(std::memory_order_relaxed);
        im.bankBuf[inactive] = bank; // snapshot the whole bank for the audio thread
        im.bankActive.store(inactive, std::memory_order_release);
        im.musicReqIndex = index;
        im.musicSeq.fetch_add(1, std::memory_order_release);
    }

    void Audio::stop_music()
    {
        if (!p_)
            return;
        Impl& im         = *p_;
        im.musicReqIndex = -1;
        im.musicSeq.fetch_add(1, std::memory_order_release);
    }
} // namespace lazy100
