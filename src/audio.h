#pragma once
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Procedural audio: every sound effect is synthesized into a PCM buffer at
// startup (no .wav assets). A tiny mixer sums active one-shot voices in the
// SDL audio callback. Master volume is live-adjustable from the settings menu.
// ---------------------------------------------------------------------------
class Audio {
public:
    enum Sfx { SHOOT, ENEMY_SHOOT, HIT, PICKUP, LEVELUP, MELEE, HURT, MENU, COUNT };

    bool init() {
        SDL_AudioSpec want{};
        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = &Audio::callback;
        want.userdata = this;

        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &spec_, 0);
        if (!dev_) return false;

        build_sounds();
        SDL_PauseAudioDevice(dev_, 0);
        return true;
    }

    void play(Sfx s, float gain = 1.0f) {
        if (s < 0 || s >= COUNT) return;
        SDL_LockAudioDevice(dev_);
        voices_.push_back({s, 0, gain});
        if (voices_.size() > 24) voices_.erase(voices_.begin());
        SDL_UnlockAudioDevice(dev_);
    }

    void set_volume(float v) { volume_ = v < 0 ? 0 : (v > 1 ? 1 : v); }
    float volume() const { return volume_; }

    void shutdown() { if (dev_) SDL_CloseAudioDevice(dev_); }

private:
    struct Voice { int sfx; size_t pos; float gain; };

    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioSpec spec_{};
    std::vector<int16_t> buffers_[COUNT];
    std::vector<Voice> voices_;
    float volume_ = 0.6f;

    static float noise() { return (rand() / float(RAND_MAX)) * 2.0f - 1.0f; }

    // Build one sound from a per-sample generator lambda
    template <typename F>
    void make(Sfx s, float seconds, F gen) {
        int n = int(seconds * 44100);
        buffers_[s].resize(n);
        for (int i = 0; i < n; ++i) {
            float t = i / 44100.0f;
            float v = gen(t, i);
            v = std::fmax(-1.0f, std::fmin(1.0f, v));
            buffers_[s][i] = int16_t(v * 28000);
        }
    }

    void build_sounds() {
        // Punchy gunshot: noise burst + low body, fast decay
        make(SHOOT, 0.16f, [](float t, int) {
            float env = std::exp(-t * 38.0f);
            float body = std::sin(t * 2.0f * M_PI * 120.0f);
            return (noise() * 0.7f + body * 0.5f) * env;
        });
        // Enemy shot: thinner, higher
        make(ENEMY_SHOOT, 0.14f, [](float t, int) {
            float env = std::exp(-t * 42.0f);
            return (noise() * 0.5f +
                    std::sin(t * 2.0f * M_PI * 300.0f) * 0.4f) * env;
        });
        // Hit confirm: short bright blip
        make(HIT, 0.09f, [](float t, int) {
            float env = std::exp(-t * 55.0f);
            return std::sin(t * 2.0f * M_PI * 880.0f) * env;
        });
        // Pickup: rising two-tone
        make(PICKUP, 0.22f, [](float t, int) {
            float f = 520.0f + 480.0f * t / 0.22f;
            float env = std::exp(-t * 9.0f);
            return std::sin(t * 2.0f * M_PI * f) * env * 0.8f;
        });
        // Level up: ascending arpeggio
        make(LEVELUP, 0.5f, [](float t, int) {
            float freqs[4] = {523, 659, 784, 1047};
            int step = int(t / 0.12f);
            if (step > 3) step = 3;
            float env = std::exp(-std::fmod(t, 0.12f) * 14.0f);
            return std::sin(t * 2.0f * M_PI * freqs[step]) * env * 0.7f;
        });
        // Melee swing: whoosh
        make(MELEE, 0.18f, [](float t, int) {
            float env = std::sin(M_PI * t / 0.18f);
            float f = 200.0f + 400.0f * t / 0.18f;
            return (noise() * 0.4f +
                    std::sin(t * 2.0f * M_PI * f) * 0.3f) * env;
        });
        // Player hurt: harsh low buzz
        make(HURT, 0.2f, [](float t, int) {
            float env = std::exp(-t * 12.0f);
            return (std::sin(t * 2.0f * M_PI * 90.0f) +
                    noise() * 0.3f) * env * 0.7f;
        });
        // Menu click
        make(MENU, 0.05f, [](float t, int) {
            float env = std::exp(-t * 80.0f);
            return std::sin(t * 2.0f * M_PI * 660.0f) * env * 0.6f;
        });
    }

    static void callback(void* userdata, Uint8* stream, int len) {
        Audio* self = static_cast<Audio*>(userdata);
        int16_t* out = reinterpret_cast<int16_t*>(stream);
        int samples = len / 2;
        std::fill(out, out + samples, 0);

        for (int i = 0; i < samples; ++i) {
            float mix = 0;
            for (Voice& v : self->voices_) {
                const auto& buf = self->buffers_[v.sfx];
                if (v.pos < buf.size())
                    mix += buf[v.pos++] * v.gain;
            }
            mix *= self->volume_;
            mix = std::fmax(-32768.0f, std::fmin(32767.0f, mix));
            out[i] = int16_t(mix);
        }
        // Drop finished voices
        auto& vs = self->voices_;
        for (size_t i = vs.size(); i-- > 0;) {
            if (vs[i].pos >= self->buffers_[vs[i].sfx].size())
                vs.erase(vs.begin() + i);
        }
    }
};
