#include "lazy100/audio/audio.hpp"

#include "lazy100/common/log.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <atomic>
#include <cmath>

namespace lazy100
{
    namespace
    {
        constexpr int kQueueSize = 64;
        constexpr int kVoices    = 4;

        // Map a sound id to a frequency on a C-major pentatonic, so beeps sound musical.
        double note_freq(int n)
        {
            static const int penta[5] = {0, 2, 4, 7, 9}; // semitone offsets
            const int        oct       = n / 5;
            const int        deg       = ((n % 5) + 5) % 5;
            const int        semis     = penta[deg] + 12 * oct;
            return 261.63 * std::pow(2.0, semis / 12.0); // C4 base
        }

        struct Voice
        {
            bool   active = false;
            double phase  = 0.0;
            double freq   = 0.0;
            double t      = 0.0;
            double dur    = 0.0;
        };
    } // namespace

    struct Audio::Impl
    {
        ma_device device {};
        bool      started = false;

        // SPSC lock-free queue: producer = main/Lua thread, consumer = audio thread.
        std::atomic<uint32_t> head {0};
        std::atomic<uint32_t> tail {0};
        int                   queue[kQueueSize] {};

        Voice  voices[kVoices];
        double sampleRate = 44100.0;

        // miniaudio render callback (audio thread). Static so it's a plain function pointer.
        static void render(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames)
        {
            auto* im = static_cast<Impl*>(dev->pUserData);

            // Drain newly-queued triggers into free voices.
            uint32_t tail = im->tail.load(std::memory_order_relaxed);
            while (tail != im->head.load(std::memory_order_acquire))
            {
                const int n = im->queue[tail % kQueueSize];
                ++tail;
                for (auto& v : im->voices)
                    if (!v.active)
                    {
                        v        = {};
                        v.active = true;
                        v.freq   = note_freq(n);
                        v.dur    = 0.15;
                        break;
                    }
            }
            im->tail.store(tail, std::memory_order_release);

            float*       o  = static_cast<float*>(out);
            const double sr = im->sampleRate;
            for (ma_uint32 f = 0; f < frames; ++f)
            {
                double s = 0.0;
                for (auto& v : im->voices)
                {
                    if (!v.active)
                        continue;
                    const double sq  = (std::fmod(v.phase, 1.0) < 0.5) ? 1.0 : -1.0;
                    double       env = 1.0 - (v.t / v.dur);
                    if (env < 0.0)
                        env = 0.0;
                    s += sq * env * 0.2;
                    v.phase += v.freq / sr;
                    v.t     += 1.0 / sr;
                    if (v.t >= v.dur)
                        v.active = false;
                }
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
            LZ_WARN("audio: device init failed; sfx disabled");
            p_.reset();
            return false;
        }
        im.sampleRate = im.device.sampleRate;
        if (ma_device_start(&im.device) != MA_SUCCESS)
        {
            LZ_WARN("audio: device start failed; sfx disabled");
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

    void Audio::trigger_sfx(int n)
    {
        if (!p_)
            return;
        Impl&          im = *p_;
        const uint32_t h  = im.head.load(std::memory_order_relaxed);
        if (h - im.tail.load(std::memory_order_acquire) >= kQueueSize)
            return; // queue full, drop
        im.queue[h % kQueueSize] = n;
        im.head.store(h + 1, std::memory_order_release);
    }
} // namespace lazy100
