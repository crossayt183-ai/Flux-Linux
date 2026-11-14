#ifndef FLUX_AUDIO_H
#define FLUX_AUDIO_H

#include "types.h"

#define AUDIO_SAMPLE_RATE 48000U

typedef void (*audio_fill_cb_t)(int16_t *stereo_frames, size_t frame_count);

bool audio_init(audio_fill_cb_t fill_cb);
bool audio_has_ac97(void);
void audio_poll_refill(void);
void audio_pcspkr_tone(uint32_t freq_hz, uint32_t duration_ms);

#endif
