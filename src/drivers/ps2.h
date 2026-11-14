#ifndef FLUX_PS2_H
#define FLUX_PS2_H

#include "types.h"

#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83

typedef struct {
    uint8_t scancode;
    uint8_t ascii;
    bool    pressed;
    bool    shift;
    bool    ctrl;
    bool    alt;
} key_event_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t buttons;      /* bit 0: left, bit 1: right, bit 2: middle */
    uint8_t prev_buttons;
} mouse_state_t;

void ps2_init(int32_t screen_w, int32_t screen_h);
void ps2_set_bounds(int32_t screen_w, int32_t screen_h);

bool ps2_poll_key(key_event_t *out);
bool ps2_poll_mouse(mouse_state_t *out);
mouse_state_t ps2_get_mouse(void);

#endif
