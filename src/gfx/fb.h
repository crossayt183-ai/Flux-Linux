#ifndef FLUX_FB_H
#define FLUX_FB_H

#include "types.h"
#include "limine.h"

typedef struct {
    uint64_t width;
    uint64_t height;
    uint64_t pitch_pixels;
    uint32_t bpp;
} fb_info_t;

bool fb_init(struct limine_framebuffer *fb);
bool fb_is_ready(void);
void fb_get_info(fb_info_t *out);

uint32_t *fb_backbuffer(void);
uint32_t  fb_width(void);
uint32_t  fb_height(void);

void fb_put_pixel(int32_t x, int32_t y, uint32_t argb);
void fb_put_pixel_alpha(int32_t x, int32_t y, uint32_t argb, uint8_t alpha);
void fb_clear(uint32_t rgb);
void fb_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb);
void fb_fill_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb, uint8_t alpha);
void fb_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb);
void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t rgb);
void fb_fill_rounded_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t rgb, uint8_t alpha);
void fb_draw_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t rgb);
void fb_fill_vgradient(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t top_rgb, uint32_t bot_rgb);
void fb_blit_screen(const uint32_t *src_xrgb);
void fb_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb);
void fb_draw_circle(int32_t cx, int32_t cy, int32_t r, uint32_t rgb, bool fill);

void fb_present(void);
void fb_present_rect(int32_t x, int32_t y, int32_t w, int32_t h);

#endif
