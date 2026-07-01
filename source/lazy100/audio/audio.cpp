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
            SfxPattern pat;
            int        last_step  = SfxPattern::kSteps - 1; // last step to play (trailing silence trimmed)
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
        std::atomic<int>      musicPos {-1};      // currently-playing pattern (audio->UI), -1 = stopped
        std::atomic<bool>     musicPauseFlag {false}; // main->audio: freeze the sequencer in place

        // ---- audio-thread state ----
        // Music and sfx keep independent voice banks that share the 4 output channels.
        // An sfx on channel c masks (silences) the music voice on c, but that music voice
        // keeps stepping underneath, so it resumes in sync the instant the sfx finishes.
        Channel  musicVoice[kChannels];
        Channel  sfxVoice[kChannels];
        bool     musicOn    = false;
        int      musicCur        = 0;
        int      musicStart      = 0;     // pattern music() started on
        int      musicLoopTarget = 0;     // where the song loops back to (last LoopStart seen)
        u8       musicCurFlags   = 0;     // flags of the pattern currently playing (for Stop)
        bool     musicLoad       = false; // load musicCur at the next opportunity
        uint32_t lastSeq         = 0;

        // Start `pat` on voice `c`. `trimTrailingSilence` (sfx) releases the channel right after
        // the last audible note instead of holding it through 32 steps of trailing rests — so a
        // short blip doesn't hog a channel (and mask music) for the whole pattern. Music voices
        // pass false: they must run the full 32 steps to stay in sync with the other channels.
        void start_voice(Channel& c, const SfxPattern& pat, bool trimTrailingSilence)
        {
            c.active       = true;
            c.pat          = pat;
            c.step         = 0;
            c.into_step    = 0.0;
            c.phase        = 0.0;
            c.step_samples = pat.speed * (sampleRate / 120.0);
            c.last_step    = SfxPattern::kSteps - 1;
            if (trimTrailingSilence)
            {
                c.last_step = 0;
                for (int i = SfxPattern::kSteps - 1; i >= 0; --i)
                    if (pat.notes[i].vol > 0)
                    {
                        c.last_step = i;
                        break;
                    }
            }
        }

        // Auto channel for an sfx (chan == -1): never fight music unless forced to.
        int pick_channel()
        {
            // Prefer a fully idle channel (no sfx and no music on it).
            for (int i = 0; i < kChannels; ++i)
                if (!sfxVoice[i].active && !musicVoice[i].active)
                    return i;
            // Otherwise borrow from music, highest channel first, so the lead voice on
            // channel 0 is the last thing an sfx ever steals.
            for (int i = kChannels - 1; i >= 0; --i)
                if (!sfxVoice[i].active)
                    return i;
            return 0; // every sfx voice busy: steal channel 0
        }

        void load_music_pattern()
        {
            const SoundBank& bank = bankBuf[bankActive.load(std::memory_order_acquire)];
            const MusicPattern& mp = bank.music[musicCur % SoundBank::kMusicCount];
            musicCurFlags = mp.flags;
            if (mp.flags & MusicPattern::kLoopStart)
                musicLoopTarget = musicCur; // future loops return here
            bool any = false;
            for (int c = 0; c < kChannels; ++c)
            {
                const u8 idx = mp.sfx[c];
                if (idx < SoundBank::kSfxCount)
                {
                    start_voice(musicVoice[c], bank.sfx[idx], false); // full 32 steps: keep channels in sync
                    any = true;
                }
                else
                    musicVoice[c].active = false; // 255 = this channel rests
            }
            if (!any)
            {
                // Empty pattern marks the end of the song: loop back to the loop target. If the
                // target itself is empty, there is nothing to play, so stop.
                if (musicCur != musicLoopTarget)
                {
                    musicCur  = musicLoopTarget;
                    musicLoad = true;
                }
                else
                    musicOn = false;
            }
        }

        void advance_music_if_idle()
        {
            if (!musicOn || musicLoad)
                return; // a pattern load is already queued; don't advance again meanwhile
            for (int c = 0; c < kChannels; ++c)
                if (musicVoice[c].active)
                    return; // still playing this pattern (independent of any sfx borrowing)
            // Pattern finished. A Stop flag halts here; otherwise advance to the next row (the
            // next empty/Stop row, or the table wrapping to 0, decides where the song ends/loops).
            if (musicCurFlags & MusicPattern::kStop)
            {
                musicOn = false;
                return;
            }
            musicCur  = (musicCur + 1) % SoundBank::kMusicCount;
            musicLoad = true;
        }

        static void render(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames)
        {
            auto* im = static_cast<Impl*>(dev->pUserData);
            im->render_block(static_cast<float*>(out), frames);
        }

        // Advance one voice by a single sample, mixing its output into `s` when `audible`.
        // A muted voice (audible == false) still steps, so it stays in sync underneath sfx.
        void step_voice(Channel& c, double& s, bool audible)
        {
            if (!c.active)
                return;
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
                if (audible)
                    s += raw * env * (note.vol / 7.0) * 0.22;
                c.phase += freq / sampleRate;
            }

            c.into_step += 1.0;
            if (c.into_step >= c.step_samples)
            {
                c.into_step = 0.0;
                c.phase     = 0.0;
                ++c.step;
                if (c.step > c.last_step) // past the last note to play (silence trimmed for sfx)
                    c.active = false;
            }
        }

        void render_block(float* o, ma_uint32 frames)
        {
            // Drain queued sfx triggers.
            uint32_t t = tail.load(std::memory_order_relaxed);
            while (t != head.load(std::memory_order_acquire))
            {
                const Msg& m  = queue[t % kQueueSize];
                const int  ch = (m.channel < 0 || m.channel >= kChannels) ? pick_channel() : m.channel;
                start_voice(sfxVoice[ch], m.pat, true); // trim trailing silence so the channel frees fast
                ++t;
            }
            tail.store(t, std::memory_order_release);

            // Handle a new music request (start/stop). Sfx voices are left untouched.
            const uint32_t seq = musicSeq.load(std::memory_order_acquire);
            if (seq != lastSeq)
            {
                lastSeq = seq;
                const int idx = musicReqIndex;
                if (idx < 0)
                {
                    musicOn = false;
                    for (auto& c : musicVoice)
                        c.active = false;
                }
                else
                {
                    musicOn         = true;
                    musicCur        = idx;
                    musicStart      = idx;
                    musicLoopTarget = idx; // loop back here until a LoopStart pattern overrides it
                    musicCurFlags   = 0;
                    musicLoad       = true;
                }
            }

            // Paused music freezes in place (voices keep their step/phase, sequencer doesn't
            // advance) while sfx keep playing.
            const bool paused = musicPauseFlag.load(std::memory_order_relaxed);

            for (ma_uint32 f = 0; f < frames; ++f)
            {
                if (musicLoad && !paused)
                {
                    musicLoad = false;
                    load_music_pattern();
                }

                double s = 0.0;
                for (int c = 0; c < kChannels; ++c)
                {
                    // Sfx has priority on its channel; the music voice keeps stepping
                    // underneath (muted) so it stays in sync and resumes when the sfx ends.
                    const bool sfxActive = sfxVoice[c].active;
                    if (!paused)
                        step_voice(musicVoice[c], s, !sfxActive);
                    step_voice(sfxVoice[c], s, true);
                }

                if (!paused)
                    advance_music_if_idle();

                if (s > 1.0)
                    s = 1.0;
                if (s < -1.0)
                    s = -1.0;
                o[f * 2 + 0] = static_cast<float>(s);
                o[f * 2 + 1] = static_cast<float>(s);
            }

            // Publish the playback position for the music editor's live indicator.
            musicPos.store(musicOn ? musicCur : -1, std::memory_order_relaxed);
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
        im.musicPauseFlag.store(false, std::memory_order_relaxed); // a fresh play is never paused
        im.musicSeq.fetch_add(1, std::memory_order_release);
    }

    void Audio::stop_music()
    {
        if (!p_)
            return;
        Impl& im         = *p_;
        im.musicReqIndex = -1;
        im.musicPauseFlag.store(false, std::memory_order_relaxed);
        im.musicSeq.fetch_add(1, std::memory_order_release);
    }

    void Audio::pause_music(bool paused)
    {
        if (p_)
            p_->musicPauseFlag.store(paused, std::memory_order_relaxed);
    }

    int Audio::music_pattern() const { return p_ ? p_->musicPos.load(std::memory_order_relaxed) : -1; }
} // namespace lazy100
