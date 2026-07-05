#include "lazy100/audio/audio.hpp"

#include "lazy100/common/log.hpp"

#ifndef NOMINMAX
#    define NOMINMAX // keep windows.h (pulled by miniaudio) from defining min/max macros
#endif
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#if defined(__EMSCRIPTEN__)
#    include <emscripten/html5.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace lazy100
{
    namespace
    {
        constexpr int kQueueSize = 64;
        constexpr int kChannels  = MusicPattern::kChannels; // 4

        // Semitone `pitch` above C2 -> Hz. pitch 24 = C4 (~261.6 Hz). Fractional pitches are
        // legal: the tracker effects (slide/vibrato/drop) bend between semitones.
        double pitch_freq(double pitch) { return 65.406 * std::pow(2.0, pitch / 12.0); }

        // One channel: a voice stepping through a copy of an SfxPattern. The synth part
        // follows fake-08: the oscillator phase carries across notes and patterns, and any
        // harsh parameter change (volume jump, frequency jump, waveform switch) triggers a
        // short CROSSFADE - the old oscillator keeps running and its output is blended out
        // over ~8 ms while the new one blends in. No per-step envelope at all.
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

            // slide (effect 1) context: the note the previous step ended on. fake-08 notes
            // that a fresh channel behaves as if the previous key were C-4 (24).
            double prev_key = 24.0;
            double prev_vol = 0.0;

            // Last rendered synth parameters (for harsh-change detection)...
            double last_freq = 0.0;
            double last_vol  = 0.0;
            int    last_wave = -1;
            // ...and the snapshot the crossfade keeps playing while it dies out.
            double fade            = 0.0; // 1 -> 0 over ~8 ms; 0 = no fade running
            double fade_phase      = 0.0;
            double fade_freq       = 0.0;
            double fade_vol        = 0.0;
            int    fade_wave       = 0;
            double fade_last_noise = 0.0;
            double fade_noise_hold = 0.0;
        };

        // Sample the waveform at unwrapped phase (cycles). Formulas and relative amplitudes
        // follow zepto8's measurements of real hardware WAV exports, so the classic timbres
        // come out right; the mixer gain compensates the lower peaks. The noise state comes in
        // by reference so the channel's main synth and its crossfade snapshot can each keep
        // their own random-walk history (they share the RNG, which is harmless).
        double wave_sample(Wave w, double phase, u32& rng, double& lastNoise, double& noiseHold,
                           double freq, double sr)
        {
            const double t = std::fmod(phase, 1.0);
            switch (w)
            {
                case Wave::Square: return t < 0.5 ? 0.25 : -0.25;
                case Wave::Pulse: return t < 0.316 ? 0.25 : -0.25;
                case Wave::Triangle: return (1.0 - std::fabs(4.0 * t - 2.0)) * 0.5;
                case Wave::Saw: return 0.653 * (t < 0.5 ? t : t - 1.0);
                case Wave::TiltedSaw:
                {
                    constexpr double a = 0.875;
                    return (t < a ? 2.0 * t / a - 1.0 : 2.0 * (1.0 - t) / (1.0 - a) - 1.0) * 0.5;
                }
                case Wave::Organ:
                    return (t < 0.5 ? 3.0 - std::fabs(24.0 * t - 6.0)
                                    : 1.0 - std::fabs(16.0 * t - 12.0)) /
                           9.0;
                case Wave::Phaser:
                    return (2.0 - std::fabs(8.0 * t - 4.0) +
                            (1.0 - std::fabs(4.0 * std::fmod(phase * 109.0 / 110.0, 1.0) - 2.0))) /
                           6.0;
                case Wave::Noise:
                {
                    // Smoothed random walk, scaled by the note's advance rate; darker at low
                    // pitches, brighter high (the classic hiss).
                    constexpr double tscale = 8.858923; // 22050 / freq(top pitch)
                    const double     scale  = (phase - noiseHold) * tscale;
                    rng ^= rng << 13;
                    rng ^= rng >> 17;
                    rng ^= rng << 5;
                    const double r  = (static_cast<double>(rng) / 2147483647.5) - 1.0;
                    const double ns = (lastNoise + scale * r) / (1.0 + scale);
                    noiseHold       = phase;
                    lastNoise       = ns;
                    const double factor =
                        std::clamp(1.0 - std::log2(std::max(freq, 1.0) / 65.406) * 12.0 / 63.0, 0.0, 1.0);
                    return ns * 1.5 * (1.0 + factor * factor);
                }
                default: return 0.0;
            }
            (void)sr;
        }
    } // namespace

    struct Audio::Impl
    {
        ma_device device {};
        bool      started    = false;
        double    sampleRate = 44100.0;

#if defined(__EMSCRIPTEN__)
        // web: false until the first user gesture starts the device (resumes the AudioContext).
        bool deviceRunning = false;

        // Start the (suspended) device from within a DOM gesture event; the browser only resumes
        // the AudioContext when this runs synchronously in a user-gesture stack. Re-arms after a
        // background device_suspend()/device_resume() cycle (deviceRunning drops back to false).
        // `started` guards against the window between suspend and resume, when there IS no device.
        static EM_BOOL try_resume(Impl* im)
        {
            if (im && im->started && !im->deviceRunning && ma_device_start(&im->device) == MA_SUCCESS)
                im->deviceRunning = true;
            return EM_FALSE; // don't consume the event
        }
        static EM_BOOL on_key(int, const EmscriptenKeyboardEvent*, void* ud) { return try_resume(static_cast<Impl*>(ud)); }
        static EM_BOOL on_mouse(int, const EmscriptenMouseEvent*, void* ud) { return try_resume(static_cast<Impl*>(ud)); }
        static EM_BOOL on_touch(int, const EmscriptenTouchEvent*, void* ud) { return try_resume(static_cast<Impl*>(ud)); }
#endif

        // ---- sfx: SPSC lock-free queue (producer = main/Lua thread) ----
        struct Msg
        {
            SfxPattern pat;
            int        channel; // 0..3, or -1 = auto (play) / all (stop, release)
            int        op;      // 0 = play, 1 = stop, 2 = release loop
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
        // Playback positions for the editors' live highlight (audio->UI, -1 = silent).
        std::atomic<int> musicStepPos[kChannels] {-1, -1, -1, -1};
        std::atomic<int> sfxStepPos[kChannels] {-1, -1, -1, -1};
        std::atomic<bool>     musicPauseFlag {false}; // main->audio: freeze the sequencer in place

        // ---- audio-thread state ----
        // Music and sfx keep independent voice banks that share the 4 output channels.
        // An sfx on channel c masks (silences) the music voice on c, but that music voice
        // keeps stepping underneath, so it resumes in sync the instant the sfx finishes.
        Channel  musicVoice[kChannels];
        Channel  sfxVoice[kChannels];

        // ---- speaker warm-up ----
        // A power-saving output (notably a Bluetooth speaker) sleeps when the stream is idle and
        // takes ~1s to wake, swallowing the start of the first sound. Right after the device
        // starts we emit a sub-audible dither for a moment so the speaker powers up before the
        // real audio (the boot chime) arrives. Counted down on the audio thread.
        std::atomic<uint32_t> warmupFrames {0};
        uint32_t              warmupNoise = 0x9E3779B9u;
        bool     musicOn   = false;
        int      musicCur  = 0;
        int      musicStartPat = 0; // pattern music() began on: the default loop-back target
        // The sequencer runs on a shared TICK CLOCK (fake-08 model): musicTicks advances every
        // sample; when it reaches musicTickLen (the pattern's duration in ticks, decided by the
        // first non-looping channel), the next pattern loads in the SAME sample - the voices
        // never sit idle between patterns, so seams are gapless by construction.
        double   musicTicks   = 0.0;
        double   musicTickLen = 0.0;
        uint32_t lastSeq      = 0;

        // Start `pat` on voice `c`. `trimTrailingSilence` (sfx) releases the channel right after
        // the last audible note instead of holding it through 32 steps of trailing rests — so a
        // short blip doesn't hog a channel (and mask music) for the whole pattern. Music voices
        // pass false: they play all 32 steps (the length-truncating loop_start form only affects
        // the pattern duration, a quirk fake-08 documents), and the shared clock cuts them off.
        void start_voice(Channel& c, const SfxPattern& pat, bool trimTrailingSilence)
        {
            // phase carries over on purpose: the channel's tone generator keeps running, so a
            // new pattern (or retriggered sfx) picks up mid-wave; the crossfade in step_voice
            // absorbs the parameter jump.
            c.active       = true;
            c.pat          = pat;
            c.step         = 0;
            c.into_step    = 0.0;
            c.step_samples = pat.speed * (sampleRate / 120.0);
            c.prev_key     = 24.0;
            c.prev_vol     = 0.0;
            c.last_step    = SfxPattern::kSteps - 1;
            if (trimTrailingSilence && !pat.loops())
            {
                c.last_step = pat.length() - 1; // loop_start truncates when loop_end == 0
                int last    = 0;
                for (int i = c.last_step; i >= 0; --i)
                    if (pat.notes[i].vol > 0)
                    {
                        last = i;
                        break;
                    }
                c.last_step = last;
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

            // Pattern duration in ticks (fake-08 rules): the FIRST non-looping channel decides
            // (its length in steps x its speed; the loop_start-as-length form counts here even
            // though the voice itself plays all 32 steps). If every channel loops, the slowest
            // one decides; with no channels at all, 32 ticks of silence keep the chain moving.
            double lenNoLoop = -1.0, lenLooping = -1.0;
            for (int c = 0; c < kChannels; ++c)
            {
                const u8 idx = mp.sfx[c];
                if (idx >= SoundBank::kSfxCount)
                    continue;
                const SfxPattern& p = bank.sfx[idx];
                if (p.loops())
                    lenLooping = std::max(lenLooping, 32.0 * p.speed);
                else
                {
                    lenNoLoop = static_cast<double>(p.length()) * p.speed;
                    break;
                }
            }
            musicTickLen = lenNoLoop > 0 ? lenNoLoop : (lenLooping > 0 ? lenLooping : 32.0);
            musicTicks   = 0.0;

            for (int c = 0; c < kChannels; ++c)
            {
                const u8 idx = mp.sfx[c];
                if (idx < SoundBank::kSfxCount)
                    start_voice(musicVoice[c], bank.sfx[idx], false);
                else
                    musicVoice[c].active = false; // rest; the crossfade absorbs the drop
            }
        }

        // Advance the shared tick clock one sample; on pattern end, chain to the next pattern
        // IMMEDIATELY (same sample) so playback is continuous across the seam.
        void advance_music_clock()
        {
            if (!musicOn)
                return;
            musicTicks += 120.0 / sampleRate;
            if (musicTicks < musicTickLen)
                return;

            const SoundBank& bank  = bankBuf[bankActive.load(std::memory_order_acquire)];
            const auto       empty = [&](int p)
            {
                if (p < 0 || p >= SoundBank::kMusicCount)
                    return true;
                for (const u8 s : bank.music[p].sfx)
                    if (s < SoundBank::kSfxCount)
                        return false;
                return true;
            };
            // Loop-back target: the nearest loop-start flag at or before the current pattern,
            // else wherever music() began - so a song without any flags still loops when it
            // runs into an empty pattern (the natural way to author a short soundtrack).
            const auto loopTarget = [&]
            {
                int t = musicCur;
                while (t > 0 && !(bank.music[t].flags & MusicPattern::kLoopStart))
                    --t;
                return (bank.music[t].flags & MusicPattern::kLoopStart) ? t : musicStartPat;
            };

            const u8 flags = bank.music[musicCur % SoundBank::kMusicCount].flags;
            int      next  = musicCur + 1;
            if (flags & MusicPattern::kStop)
                next = -1;
            else if (flags & MusicPattern::kLoopEnd)
                next = loopTarget();
            else if (empty(next))
                next = loopTarget(); // song ran off its end: loop the whole thing
            if (next < 0 || empty(next))
            {
                musicOn = false;
                for (auto& c : musicVoice)
                    c.active = false;
                return;
            }
            musicCur = next;
            load_music_pattern();
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
            // Current synth parameters: the live note's, or silence when the voice is idle.
            double freq = c.last_freq;
            double volf = 0.0;
            int    wave = c.last_wave < 0 ? 0 : c.last_wave;

            if (c.active)
            {
                const SfxNote& note = c.pat.notes[c.step];
                const double   pos  = c.into_step / c.step_samples; // 0..1 within the step
                double         key  = note.pitch;
                volf                = note.vol / 7.0;
                wave                = note.wave % static_cast<int>(Wave::Count);
                freq                = pitch_freq(key);
                if (note.vol > 0)
                {
                    // Effects bend frequency/volume across the step (fake-08 semantics).
                    switch (note.effect)
                    {
                        case 1: // slide FROM the previous note's pitch and volume
                            freq = pitch_freq(c.prev_key) + (freq - pitch_freq(c.prev_key)) * pos;
                            if (c.prev_vol > 0.0)
                                volf = c.prev_vol + (volf - c.prev_vol) * pos;
                            break;
                        case 2: // vibrato: triangle at 7.5 Hz, depth half a semitone
                        {
                            const double tSec = (c.step + pos) * (c.step_samples / sampleRate);
                            const double t =
                                std::fabs(std::fmod(7.5 * tSec, 1.0) - 0.5) - 0.25; // -.25..+.25
                            freq += freq * 0.059463 * t;
                            break;
                        }
                        case 3: // drop: frequency falls linearly to zero across the step
                            freq *= 1.0 - pos;
                            break;
                        case 4: volf *= pos; break;       // fade in
                        case 5: volf *= 1.0 - pos; break; // fade out
                        case 6:                            // arpeggio over the step's group of 4
                        case 7:
                        {
                            // fake-08: iterate the group at speed 4 (fast) / 8 (slow) ticks per
                            // note, halved when the sfx speed is <= 8.
                            const int    m = (c.pat.speed <= 8 ? 32 : 16) / (note.effect == 6 ? 4 : 8);
                            const double tSec = (c.step + pos) * (c.step_samples / sampleRate);
                            const int    n    = static_cast<int>(m * 7.5 * tSec);
                            freq = pitch_freq(c.pat.notes[(c.step & ~3) | (n & 3)].pitch);
                            break;
                        }
                        default: break;
                    }
                }
                else
                    volf = 0.0;
            }

            // Harsh parameter change? Snapshot the old synth and crossfade to the new one over
            // ~8 ms while BOTH keep oscillating (fake-08's declick). This is the only smoothing
            // in the whole engine - held notes and gentle effects pass through untouched.
            const double freqJump = std::min(freq, c.last_freq) * 0.01;
            if (std::fabs(volf - c.last_vol) > 0.1 || std::fabs(freq - c.last_freq) > freqJump ||
                wave != c.last_wave)
            {
                if (c.fade <= 0.0) // don't restart mid-fade: it would stack discontinuities
                {
                    c.fade_phase      = c.phase;
                    c.fade_freq       = c.last_freq;
                    c.fade_vol        = c.last_vol;
                    c.fade_wave       = c.last_wave < 0 ? wave : c.last_wave;
                    c.fade_last_noise = c.last_noise;
                    c.fade_noise_hold = c.noise_hold;
                }
                c.fade  = 1.0;
                c.phase = std::fmod(c.phase, 1.0); // keep precision over long sessions
                c.noise_hold = c.phase;
            }
            c.last_freq = freq;
            c.last_vol  = volf;
            c.last_wave = wave;

            double out = 0.0;
            if (volf > 0.0)
            {
                out = wave_sample(static_cast<Wave>(wave), c.phase, c.noise, c.last_noise,
                                  c.noise_hold, freq, sampleRate) *
                      volf;
                c.phase += freq / sampleRate;
            }
            if (c.fade > 0.0)
            {
                double old = 0.0;
                if (c.fade_vol > 0.0)
                {
                    old = wave_sample(static_cast<Wave>(c.fade_wave), c.fade_phase, c.noise,
                                      c.fade_last_noise, c.fade_noise_hold, c.fade_freq,
                                      sampleRate) *
                          c.fade_vol;
                    c.fade_phase += c.fade_freq / sampleRate;
                }
                out    = out + (old - out) * c.fade;
                c.fade -= 130.0 / sampleRate; // ~8 ms crossfade
            }
            if (audible)
                s += out * 0.5; // waveforms peak ~0.5

            if (!c.active)
                return;

            c.into_step += 1.0;
            if (c.into_step >= c.step_samples)
            {
                c.into_step -= c.step_samples; // keep the fractional part: no per-step drift
                const SfxNote& fin = c.pat.notes[c.step];
                c.prev_key         = fin.pitch; // slide context for the next note
                c.prev_vol         = fin.vol / 7.0;
                ++c.step;
                if (c.pat.loops() && c.step >= c.pat.loop_end)
                    c.step = c.pat.loop_start; // looping voice: wrap and keep playing
                else if (c.step > c.last_step) // past the last note to play
                    c.active = false; // phase stays: the next voice on this channel continues it
            }
        }

        void render_block(float* o, ma_uint32 frames)
        {
            // Drain queued sfx triggers.
            uint32_t t = tail.load(std::memory_order_relaxed);
            while (t != head.load(std::memory_order_acquire))
            {
                const Msg& m = queue[t % kQueueSize];
                if (m.op == 0) // play
                {
                    const int ch = (m.channel < 0 || m.channel >= kChannels) ? pick_channel() : m.channel;
                    start_voice(sfxVoice[ch], m.pat, true); // trim trailing silence: frees the channel fast
                }
                else // stop / release-loop, on one channel or all
                {
                    for (int c = 0; c < kChannels; ++c)
                    {
                        if (m.channel >= 0 && m.channel != c)
                            continue;
                        if (m.op == 1)
                            sfxVoice[c].active = false; // the crossfade absorbs the drop
                        else if (sfxVoice[c].active && sfxVoice[c].pat.loops())
                        {
                            sfxVoice[c].pat.loop_start = 0; // drop the loop; play out to the end
                            sfxVoice[c].pat.loop_end   = 0;
                            sfxVoice[c].last_step      = SfxPattern::kSteps - 1;
                        }
                    }
                }
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
                        c.active = false; // the crossfade absorbs the drop
                }
                else
                {
                    musicOn       = true;
                    musicCur      = idx;
                    musicStartPat = idx;
                    load_music_pattern();
                }
            }

            // Paused music freezes in place (voices keep their step/phase, sequencer doesn't
            // advance) while sfx keep playing.
            const bool paused = musicPauseFlag.load(std::memory_order_relaxed);

            uint32_t warm = warmupFrames.load(std::memory_order_relaxed);

            for (ma_uint32 f = 0; f < frames; ++f)
            {
                // The shared clock chains patterns before the voices render, so the first
                // sample of pattern N+1 directly follows the last sample of pattern N.
                if (!paused)
                    advance_music_clock();

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

                if (warm > 0)
                {
                    // Sub-audible dither (~-52 dB) to keep a power-saving speaker awake until the
                    // real audio arrives. Quiet enough to be inaudible on a wired output.
                    warmupNoise ^= warmupNoise << 13;
                    warmupNoise ^= warmupNoise >> 17;
                    warmupNoise ^= warmupNoise << 5;
                    s += ((static_cast<double>(warmupNoise) / 2147483647.5) - 1.0) * 0.0025;
                    --warm;
                }

                if (s > 1.0)
                    s = 1.0;
                if (s < -1.0)
                    s = -1.0;
                o[f * 2 + 0] = static_cast<float>(s);
                o[f * 2 + 1] = static_cast<float>(s);
            }

            warmupFrames.store(warm, std::memory_order_relaxed);

            // Publish the playback positions for the editors' live indicators.
            musicPos.store(musicOn ? musicCur : -1, std::memory_order_relaxed);
            for (int c = 0; c < kChannels; ++c)
            {
                musicStepPos[c].store(musicOn && musicVoice[c].active ? musicVoice[c].step : -1,
                                      std::memory_order_relaxed);
                sfxStepPos[c].store(sfxVoice[c].active ? sfxVoice[c].step : -1,
                                    std::memory_order_relaxed);
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

#if defined(__EMSCRIPTEN__)
        // The browser keeps the AudioContext suspended until a user gesture, so the device is
        // only *started* from inside the first key/mouse/touch event (which runs synchronously in
        // the DOM gesture stack and resumes the context - see libvultra's AudioSystem). Starting
        // here at init would create a permanently-suspended context that never produces sound.
        // Until the gesture, sfx/music just queue and the render callback simply doesn't run yet.
        if (ma_device_init(nullptr, &cfg, &im.device) != MA_SUCCESS)
        {
            LZ_WARN("audio: device init failed; sound disabled");
            p_.reset();
            return false;
        }
        im.started    = true; // device is live (shutdown must uninit it), even before it runs
        im.sampleRate = im.device.sampleRate;
        im.warmupFrames.store(static_cast<uint32_t>(im.sampleRate * 1.5), std::memory_order_relaxed);
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &im, EM_TRUE, &Impl::on_key);
        emscripten_set_mousedown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &im, EM_TRUE, &Impl::on_mouse);
        emscripten_set_touchstart_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &im, EM_TRUE, &Impl::on_touch);
        LZ_INFO("audio: ready (%u Hz; resumes on first gesture)", static_cast<unsigned>(im.sampleRate));
        return true;
#else
        // Right after process start the OS audio backend can briefly be unavailable (device
        // enumeration still settling), so a single attempt sometimes loses sound on a cold boot.
        // Retry a few times with a short backoff before giving up.
        constexpr int kMaxAttempts = 10;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
        {
            if (ma_device_init(nullptr, &cfg, &im.device) != MA_SUCCESS)
                LZ_WARN("audio: device init failed (attempt %d/%d)", attempt, kMaxAttempts);
            else if (ma_device_start(&im.device) != MA_SUCCESS)
            {
                LZ_WARN("audio: device start failed (attempt %d/%d)", attempt, kMaxAttempts);
                ma_device_uninit(&im.device);
            }
            else
            {
                im.started    = true;
                im.sampleRate = im.device.sampleRate;
                // ~1.5s of sub-audible dither so a sleeping speaker wakes before the boot chime.
                im.warmupFrames.store(static_cast<uint32_t>(im.sampleRate * 1.5),
                                      std::memory_order_relaxed);
                LZ_INFO("audio: started (%u Hz)", static_cast<unsigned>(im.sampleRate));
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        LZ_WARN("audio: no device after %d attempts; sound disabled", kMaxAttempts);
        p_.reset();
        return false;
#endif
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
        im.queue[h % kQueueSize] = {pat, channel, 0};
        im.head.store(h + 1, std::memory_order_release);
    }

    void Audio::stop_sfx(int channel)
    {
        if (!p_)
            return;
        Impl&          im = *p_;
        const uint32_t h  = im.head.load(std::memory_order_relaxed);
        if (h - im.tail.load(std::memory_order_acquire) >= kQueueSize)
            return;
        im.queue[h % kQueueSize] = {SfxPattern {}, channel, 1};
        im.head.store(h + 1, std::memory_order_release);
    }

    void Audio::release_sfx_loop(int channel)
    {
        if (!p_)
            return;
        Impl&          im = *p_;
        const uint32_t h  = im.head.load(std::memory_order_relaxed);
        if (h - im.tail.load(std::memory_order_acquire) >= kQueueSize)
            return;
        im.queue[h % kQueueSize] = {SfxPattern {}, channel, 2};
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

    int Audio::music_step(int channel) const
    {
        if (!p_ || channel < 0 || channel >= kChannels)
            return -1;
        return p_->musicStepPos[channel].load(std::memory_order_relaxed);
    }

    int Audio::sfx_step(int channel) const
    {
        if (!p_ || channel < 0 || channel >= kChannels)
            return -1;
        return p_->sfxStepPos[channel].load(std::memory_order_relaxed);
    }

    void Audio::rewarm()
    {
        if (p_)
            p_->warmupFrames.store(static_cast<uint32_t>(p_->sampleRate * 1.5), std::memory_order_relaxed);
    }

    void Audio::device_suspend()
    {
#if defined(__EMSCRIPTEN__)
        if (!p_ || !p_->started)
            return;
        // Kill the device outright: iOS revokes the audio session in the background, and the old
        // AudioContext is unreliable afterwards (it can claim "running" while producing silence).
        // Sequencer state (voices, song position, queues) lives in Impl and survives untouched.
        ma_device_uninit(&p_->device);
        p_->started       = false;
        p_->deviceRunning = false;
        LZ_INFO("audio: device suspended (tab backgrounded)");
#endif
    }

    void Audio::device_resume()
    {
#if defined(__EMSCRIPTEN__)
        if (!p_ || p_->started)
            return; // no Impl, or the device is already up (idempotent)
        // Rebuild the device from scratch — same config as init()'s web branch. A fresh context
        // may start suspended if we're not in a gesture stack; the window gesture handlers
        // (try_resume) and miniaudio's own unlock listeners start it on the next tap.
        ma_device_config cfg  = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate        = 44100;
        cfg.dataCallback      = &Impl::render;
        cfg.pUserData         = p_.get();
        if (ma_device_init(nullptr, &cfg, &p_->device) != MA_SUCCESS)
        {
            LZ_WARN("audio: device re-init failed after background; sound stays off");
            return;
        }
        p_->started    = true;
        p_->sampleRate = p_->device.sampleRate;
        p_->warmupFrames.store(static_cast<uint32_t>(p_->sampleRate * 1.5), std::memory_order_relaxed);
        if (ma_device_start(&p_->device) == MA_SUCCESS)
            p_->deviceRunning = true;
        LZ_INFO("audio: device rebuilt (tab foregrounded)%s", p_->deviceRunning ? "" : "; starts on next tap");
#endif
    }

    bool Audio::warming_up() const
    {
        return p_ && p_->warmupFrames.load(std::memory_order_relaxed) > 0;
    }

    bool Audio::debug_render_music(const SoundBank& bank, int index, double seconds,
                                   const std::string& wav_path)
    {
        // A bare Impl with no device: render_block only needs the sample rate and a bank.
        auto  imp = std::make_unique<Impl>();
        Impl& im  = *imp;
        im.sampleRate = 44100.0;
        im.bankBuf[0] = bank;
        im.bankActive.store(0);
        im.musicReqIndex = index;
        im.musicSeq.store(1); // lastSeq starts 0 -> the request is picked up immediately

        const auto         totalFrames = static_cast<size_t>(seconds * im.sampleRate);
        std::vector<float> mix(totalFrames * 2, 0.0f);
        constexpr size_t   kBlock = 512;
        for (size_t off = 0; off < totalFrames; off += kBlock)
            im.render_block(mix.data() + off * 2, static_cast<ma_uint32>(std::min(kBlock, totalFrames - off)));

        // Minimal 16-bit stereo PCM WAV.
        std::ofstream f(wav_path, std::ios::binary);
        if (!f)
            return false;
        const u32 dataBytes = static_cast<u32>(totalFrames * 2 * 2);
        const u32 rate = 44100, byteRate = rate * 4;
        const u16 channels = 2, bits = 16, blockAlign = 4, fmt = 1;
        const u32 riffLen = 36 + dataBytes, fmtLen = 16;
        f.write("RIFF", 4);
        f.write(reinterpret_cast<const char*>(&riffLen), 4);
        f.write("WAVEfmt ", 8);
        f.write(reinterpret_cast<const char*>(&fmtLen), 4);
        f.write(reinterpret_cast<const char*>(&fmt), 2);
        f.write(reinterpret_cast<const char*>(&channels), 2);
        f.write(reinterpret_cast<const char*>(&rate), 4);
        f.write(reinterpret_cast<const char*>(&byteRate), 4);
        f.write(reinterpret_cast<const char*>(&blockAlign), 2);
        f.write(reinterpret_cast<const char*>(&bits), 2);
        f.write("data", 4);
        f.write(reinterpret_cast<const char*>(&dataBytes), 4);
        for (const float s : mix)
        {
            const auto v = static_cast<short>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
            f.write(reinterpret_cast<const char*>(&v), 2);
        }
        return f.good();
    }
} // namespace lazy100
