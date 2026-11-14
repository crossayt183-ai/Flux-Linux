#ifndef FLUX_WM_H
#define FLUX_WM_H

#include "types.h"
#include "ps2.h"

#define WM_MAX_WINDOWS 8

typedef enum {
    WIN_TERMINAL = 1,
    WIN_SYSMON   = 2,
    WIN_FILES    = 3,
    WIN_WELCOME  = 4,
    WIN_FIREFOX  = 5,
    WIN_SETTINGS = 6,
    WIN_STORE    = 7
} wm_app_kind_t;

typedef struct wm_window wm_window_t;

typedef void (*wm_paint_fn_t)(wm_window_t *win, int32_t cx, int32_t cy, int32_t cw, int32_t ch);
typedef void (*wm_key_fn_t)(wm_window_t *win, const key_event_t *ev);
typedef void (*wm_mouse_fn_t)(wm_window_t *win, int32_t rx, int32_t ry, uint8_t buttons, bool click);

struct wm_window {
    uint32_t      id;
    wm_app_kind_t kind;
    char          title[48];
    int32_t       x, y, w, h;
    bool          visible;
    bool          focused;
    uint32_t      accent_color;
    wm_paint_fn_t on_paint;
    wm_key_fn_t   on_key;
    wm_mouse_fn_t on_mouse;
};

void wm_init(void);
void wm_run_loop(void);
void wm_term_write(const char *buf, size_t len);
wm_window_t *wm_open_app(wm_app_kind_t kind);

#endif
