#include "synth.h"
#include "audio.h"
#include "spinlock.h"
#include "timer.h"
#include "string.h"
#include "serial.h"

#define MAX_VOICES 4
#define SCOPE_SAMPLES 128

typedef struct {
    bool     active;
    uint32_t phase;       /* 32-bit fixed-point phase [0 .. 2^32) */
    uint32_t phase_inc;
    uint32_t env_pos;     /* samples elapsed since note start */
    uint32_t total_samples;
    uint32_t attack_samples;
    uint32_t release_samples;
    uint8_t  wave;
    uint8_t  volume;      /* 0..100 */
} synth_voice_t;

static synth_voice_t voices[MAX_VOICES];
static synth_wave_t  default_wave = WAVE_TRIANGLE;
static spinlock_t    synth_lock = SPINLOCK_INIT;
static int8_t        scope_ring[SCOPE_SAMPLES];
static uint32_t      scope_idx = 0;

/* 64-entry quarter-wave table? Full 256-entry symmetric Q15 sine lookup */
static int16_t sine_lut[256];

static void build_sine_lut(void) {
    /* Bhaskara/parabolic fast integer sine approximation in Q15 [-32767..32767] */
    for (int i = 0; i < 256; i++) {
        int x = (i < 128) ? i : (i - 128);
        /* Map x in [0..128] -> y in [0..32767] via 4*x*(128-x)/4096 */
        int32_t num = 4 * x * (128 - x);
        /* Curve sharpen for true sine match: mix linear parabolic + square */
        int32_t s = (num * 32767) / 16384;
        if (s > 32767) s = 32767;
        sine_lut[i] = (int16_t)((i < 128) ? s : -s);
    }
}

static inline int32_t sample_osc(uint8_t wave, uint32_t phase) {
    uint32_t top8 = phase >> 24;
    switch (wave) {
    case WAVE_SINE:
        return (int32_t)sine_lut[top8];
    case WAVE_SQUARE:
        return (phase < 0x80000000U) ? 28000 : -28000;
    case WAVE_SAW:
        return (int32_t)(phase >> 16) - 32768;
    case WAVE_TRIANGLE:
    default: {
        uint32_t p = phase >> 16; /* 0..65535 */
        if (p < 32768U) return (int32_t)(p * 2U) - 32768;
        return 32767 - (int32_t)((p - 32768U) * 2U);
    }
    }
}

static void synth_mix_frames(int16_t *stereo_frames, size_t frame_count) {
    spin_lock_irqsave(&synth_lock);
    for (size_t i = 0; i < frame_count; i++) {
        int32_t mix = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            synth_voice_t *vc = &voices[v];
            if (!vc->active) continue;

            if (vc->env_pos >= vc->total_samples) {
                vc->active = false;
                continue;
            }

            int32_t osc = sample_osc(vc->wave, vc->phase);
            vc->phase += vc->phase_inc;

            /* Linear Attack / Sustain / Release envelope scaled [0..256] */
            int32_t env = 256;
            if (vc->env_pos < vc->attack_samples && vc->attack_samples > 0) {
                env = (int32_t)((vc->env_pos * 256U) / vc->attack_samples);
            } else {
                uint32_t rem = vc->total_samples - vc->env_pos;
                if (rem < vc->release_samples && vc->release_samples > 0) {
                    env = (int32_t)((rem * 256U) / vc->release_samples);
                }
            }
            vc->env_pos++;

            int32_t scaled = (osc * env) >> 8;
            scaled = (scaled * (int32_t)vc->volume) / 100;
            mix += scaled >> 2; /* Headroom for 4 voices */
        }

        if (mix > 32767)  mix = 32767;
        if (mix < -32768) mix = -32768;
        stereo_frames[i * 2 + 0] = (int16_t)mix;
        stereo_frames[i * 2 + 1] = (int16_t)mix;

        if ((i & 7U) == 0) {
            scope_ring[scope_idx] = (int8_t)(mix >> 8);
            scope_idx = (scope_idx + 1) % SCOPE_SAMPLES;
        }
    }
    spin_unlock_irqrestore(&synth_lock);
}

void synth_init(void) {
    memset(voices, 0, sizeof(voices));
    memset(scope_ring, 0, sizeof(scope_ring));
    build_sine_lut();
    audio_init(synth_mix_frames);
    kprintf("[SYNTH] 4-voice fixed-point oscillator + ADSR mixer online (%u Hz)\n", AUDIO_SAMPLE_RATE);
}

void synth_set_default_wave(synth_wave_t wave) {
    default_wave = wave;
}

synth_wave_t synth_get_default_wave(void) {
    return default_wave;
}

void synth_play_note(uint32_t freq_hz, uint32_t duration_ms, uint8_t wave, uint8_t volume) {
    if (freq_hz < 20 || freq_hz > 16000 || duration_ms == 0) return;
    if (!audio_has_ac97()) {
        audio_pcspkr_tone(freq_hz, MIN(duration_ms, 300U));
        return;
    }

    uint64_t inc64 = ((uint64_t)freq_hz << 32) / AUDIO_SAMPLE_RATE;
    uint32_t total = (uint32_t)(((uint64_t)duration_ms * AUDIO_SAMPLE_RATE) / 1000ULL);
    if (total < 128) total = 128;

    spin_lock_irqsave(&synth_lock);
    int slot = 0;
    uint32_t max_pos = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) { slot = i; break; }
        if (voices[i].env_pos >= max_pos) { max_pos = voices[i].env_pos; slot = i; }
    }

    synth_voice_t *vc = &voices[slot];
    vc->active          = true;
    vc->phase           = 0;
    vc->phase_inc       = (uint32_t)inc64;
    vc->env_pos         = 0;
    vc->total_samples   = total;
    vc->attack_samples  = MIN(total / 8U, AUDIO_SAMPLE_RATE / 80U);
    vc->release_samples = MIN(total / 3U, AUDIO_SAMPLE_RATE / 12U);
    vc->wave            = (wave <= WAVE_TRIANGLE) ? wave : (uint8_t)default_wave;
    vc->volume          = MIN(volume, (uint8_t)100);
    spin_unlock_irqrestore(&synth_lock);

    audio_poll_refill();
}

void synth_play_boot_chime(void) {
    /* C5 - E5 - G5 - C6 warm startup arpeggio */
    synth_play_note(523, 180, WAVE_TRIANGLE, 75);
    if (audio_has_ac97()) {
        timer_sleep_ms(90);
        synth_play_note(659, 180, WAVE_TRIANGLE, 75);
        timer_sleep_ms(90);
        synth_play_note(784, 200, WAVE_SINE, 75);
        timer_sleep_ms(90);
        synth_play_note(1046, 360, WAVE_SINE, 85);
    }
}

void synth_play_melody(uint8_t wave) {
    static const uint16_t notes[] = { 440, 523, 659, 587, 523, 659, 784, 880 };
    for (size_t i = 0; i < ARRAY_LEN(notes); i++) {
        synth_play_note(notes[i], 180, wave, 80);
        timer_sleep_ms(140);
        audio_poll_refill();
    }
}

void synth_get_scope(int8_t *out_samples, size_t count) {
    if (!out_samples || count == 0) return;
    spin_lock_irqsave(&synth_lock);
    uint32_t start = scope_idx;
    size_t n = MIN(count, (size_t)SCOPE_SAMPLES);
    for (size_t i = 0; i < n; i++) {
        out_samples[i] = scope_ring[(start + i) % SCOPE_SAMPLES];
    }
    spin_unlock_irqrestore(&synth_lock);
}

uint32_t synth_active_voices(void) {
    uint32_t cnt = 0;
    spin_lock_irqsave(&synth_lock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active) cnt++;
    }
    spin_unlock_irqrestore(&synth_lock);
    return cnt;
}
