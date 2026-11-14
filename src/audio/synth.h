#ifndef FLUX_SYNTH_H
#define FLUX_SYNTH_H

#include "types.h"

typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE = 1,
    WAVE_SAW = 2,
    WAVE_TRIANGLE = 3
} synth_wave_t;

void synth_init(void);
void synth_play_note(uint32_t freq_hz, uint32_t duration_ms, uint8_t wave, uint8_t volume);
void synth_play_boot_chime(void);
void synth_play_melody(uint8_t wave);
void synth_set_default_wave(synth_wave_t wave);
synth_wave_t synth_get_default_wave(void);
void synth_get_scope(int8_t *out_samples, size_t count);
uint32_t synth_active_voices(void);

#endif
