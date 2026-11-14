#include "fb.h"
#include "pmm.h"
#include "string.h"
#include "serial.h"

static struct limine_framebuffer *hw_fb = NULL;
static uint32_t *front_buf = NULL;
static uint32_t *back_buf  = NULL;
static int32_t   fb_w = 0;
static int32_t   fb_h = 0;
static uint32_t  front_pitch_px = 0;
static bool      fast_xrgb = true;

static inline uint32_t to_hw_color(uint32_t rgb) {
    if (fast_xrgb || !hw_fb) return rgb & 0x00FFFFFFU;
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8)  & 0xFF;
    uint32_t b = rgb & 0xFF;
    return (r << hw_fb->red_mask_shift) |
           (g << hw_fb->green_mask_shift) |
           (b << hw_fb->blue_mask_shift);
}

bool fb_init(struct limine_framebuffer *fb) {
    if (!fb || !fb->address || fb->bpp != 32 || fb->width == 0 || fb->height == 0) {
        return false;
    }
    hw_fb          = fb;
    front_buf      = (uint32_t *)fb->address;
    fb_w           = (int32_t)fb->width;
    fb_h           = (int32_t)fb->height;
    front_pitch_px = (uint32_t)(fb->pitch / 4);
    fast_xrgb      = (fb->red_mask_shift == 16 && fb->green_mask_shift == 8 && fb->blue_mask_shift == 0);

    size_t bb_bytes = (size_t)fb_w * (size_t)fb_h * sizeof(uint32_t);
    size_t bb_pages = ALIGN_UP(bb_bytes, PAGE_SIZE) / PAGE_SIZE;
    uint64_t bb_phys = pmm_alloc_pages(bb_pages);
    if (!bb_phys) {
        kpanic("Failed to allocate framebuffer backbuffer (%lu pages)", bb_pages);
    }
    back_buf = (uint32_t *)phys_to_virt(bb_phys);
    memset(back_buf, 0, bb_pages * PAGE_SIZE);

    kprintf("[FB] Double-buffered %dx%d 32bpp ready (%lu KB backbuffer)\n",
            fb_w, fb_h, (bb_pages * PAGE_SIZE) / 1024);
    return true;
}

bool fb_is_ready(void) { return back_buf != NULL; }
uint32_t *fb_backbuffer(void) { return back_buf; }
uint32_t fb_width(void) { return (uint32_t)fb_w; }
uint32_t fb_height(void) { return (uint32_t)fb_h; }

void fb_get_info(fb_info_t *out) {
    if (!out) return;
    out->width        = (uint64_t)fb_w;
    out->height       = (uint64_t)fb_h;
    out->pitch_pixels = (uint64_t)fb_w;
    out->bpp          = hw_fb ? hw_fb->bpp : 32;
}

void fb_put_pixel(int32_t x, int32_t y, uint32_t rgb) {
    if (!back_buf || (uint32_t)x >= (uint32_t)fb_w || (uint32_t)y >= (uint32_t)fb_h) return;
    back_buf[(uint32_t)y * (uint32_t)fb_w + (uint32_t)x] = to_hw_color(rgb);
}

void fb_put_pixel_alpha(int32_t x, int32_t y, uint32_t rgb, uint8_t alpha) {
    if (!back_buf || (uint32_t)x >= (uint32_t)fb_w || (uint32_t)y >= (uint32_t)fb_h) return;
    if (alpha == 255) { fb_put_pixel(x, y, rgb); return; }
    if (alpha == 0) return;

    uint32_t idx = (uint32_t)y * (uint32_t)fb_w + (uint32_t)x;
    uint32_t bg  = back_buf[idx];
    uint32_t fg  = to_hw_color(rgb);

    uint32_t rb = (((fg & 0x00FF00FFU) * alpha) + ((bg & 0x00FF00FFU) * (255U - alpha))) >> 8;
    uint32_t g  = (((fg & 0x0000FF00U) * alpha) + ((bg & 0x0000FF00U) * (255U - alpha))) >> 8;
    back_buf[idx] = (rb & 0x00FF00FFU) | (g & 0x0000FF00U);
}

void fb_clear(uint32_t rgb) {
    fb_fill_rect(0, 0, fb_w, fb_h, rgb);
}

void fb_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb) {
    if (!back_buf || w <= 0 || h <= 0) return;
    int32_t x0 = MAX(x, 0), y0 = MAX(y, 0);
    int32_t x1 = MIN(x + w, fb_w), y1 = MIN(y + h, fb_h);
    if (x0 >= x1 || y0 >= y1) return;

    uint32_t c = to_hw_color(rgb);
    for (int32_t py = y0; py < y1; py++) {
        uint32_t *row = &back_buf[py * fb_w + x0];
        for (int32_t px = 0; px < (x1 - x0); px++) {
            row[px] = c;
        }
    }
}

void fb_fill_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb, uint8_t alpha) {
    if (!back_buf || w <= 0 || h <= 0 || alpha == 0) return;
    if (alpha == 255) { fb_fill_rect(x, y, w, h, rgb); return; }
    int32_t x0 = MAX(x, 0), y0 = MAX(y, 0);
    int32_t x1 = MIN(x + w, fb_w), y1 = MIN(y + h, fb_h);
    uint32_t fg = to_hw_color(rgb);
    uint32_t inv = 255U - alpha;
    uint32_t fg_rb = (fg & 0x00FF00FFU) * alpha;
    uint32_t fg_g  = (fg & 0x0000FF00U) * alpha;

    for (int32_t py = y0; py < y1; py++) {
        uint32_t *row = &back_buf[py * fb_w + x0];
        for (int32_t px = 0; px < (x1 - x0); px++) {
            uint32_t bg = row[px];
            uint32_t rb = (fg_rb + (bg & 0x00FF00FFU) * inv) >> 8;
            uint32_t g  = (fg_g  + (bg & 0x0000FF00U) * inv) >> 8;
            row[px] = (rb & 0x00FF00FFU) | (g & 0x0000FF00U);
        }
    }
}

void fb_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb) {
    if (w <= 0 || h <= 0) return;
    fb_fill_rect(x, y, w, 1, rgb);
    fb_fill_rect(x, y + h - 1, w, 1, rgb);
    fb_fill_rect(x, y, 1, h, rgb);
    fb_fill_rect(x + w - 1, y, 1, h, rgb);
}

void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t rgb) {
    if (r <= 0 || r * 2 > w || r * 2 > h) {
        fb_fill_rect(x, y, w, h, rgb);
        return;
    }
    fb_fill_rect(x + r, y, w - 2 * r, h, rgb);
    fb_fill_rect(x, y + r, r, h - 2 * r, rgb);
    fb_fill_rect(x + w - r, y + r, r, h - 2 * r, rgb);

    for (int32_t dy = 0; dy < r; dy++) {
        for (int32_t dx = 0; dx < r; dx++) {
            int32_t ox = r - 1 - dx;
            int32_t oy = r - 1 - dy;
            if (ox * ox + oy * oy <= r * r) {
                fb_put_pixel(x + dx, y + dy, rgb);
                fb_put_pixel(x + w - 1 - dx, y + dy, rgb);
                fb_put_pixel(x + dx, y + h - 1 - dy, rgb);
                fb_put_pixel(x + w - 1 - dx, y + h - 1 - dy, rgb);
            }
        }
    }
}

void fb_fill_rounded_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t rgb, uint8_t alpha) {
    if (r <= 0 || r * 2 > w || r * 2 > h) {
        fb_fill_rect_alpha(x, y, w, h, rgb, alpha);
        return;
    }
    fb_fill_rect_alpha(x + r, y, w - 2 * r, h, rgb, alpha);
    fb_fill_rect_alpha(x, y + r, r, h - 2 * r, rgb, alpha);
    fb_fill_rect_alpha(x + w - r, y + r, r, h - 2 * r, rgb, alpha);

    for (int32_t dy = 0; dy < r; dy++) {
        for (int32_t dx = 0; dx < r; dx++) {
            int32_t ox = r - 1 - dx;
            int32_t oy = r - 1 - dy;
            if (ox * ox + oy * oy <= r * r) {
                fb_put_pixel_alpha(x + dx, y + dy, rgb, alpha);
                fb_put_pixel_alpha(x + w - 1 - dx, y + dy, rgb, alpha);
                fb_put_pixel_alpha(x + dx, y + h - 1 - dy, rgb, alpha);
                fb_put_pixel_alpha(x + w - 1 - dx, y + h - 1 - dy, rgb, alpha);
            }
        }
    }
}

void fb_draw_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t rgb) {
    if (r <= 1 || r * 2 > w || r * 2 > h) {
        fb_draw_rect(x, y, w, h, rgb);
        return;
    }
    fb_fill_rect(x + r, y, w - 2 * r, 1, rgb);
    fb_fill_rect(x + r, y + h - 1, w - 2 * r, 1, rgb);
    fb_fill_rect(x, y + r, 1, h - 2 * r, rgb);
    fb_fill_rect(x + w - 1, y + r, 1, h - 2 * r, rgb);

    int32_t r_in = (r - 1) * (r - 1);
    int32_t r_out = r * r;
    for (int32_t dy = 0; dy < r; dy++) {
        for (int32_t dx = 0; dx < r; dx++) {
            int32_t ox = r - 1 - dx;
            int32_t oy = r - 1 - dy;
            int32_t d2 = ox * ox + oy * oy;
            if (d2 >= r_in && d2 <= r_out) {
                fb_put_pixel(x + dx, y + dy, rgb);
                fb_put_pixel(x + w - 1 - dx, y + dy, rgb);
                fb_put_pixel(x + dx, y + h - 1 - dy, rgb);
                fb_put_pixel(x + w - 1 - dx, y + h - 1 - dy, rgb);
            }
        }
    }
}

void fb_blit_screen(const uint32_t *src_xrgb) {
    if (!back_buf || !src_xrgb || fb_w <= 0 || fb_h <= 0) return;
    if (fast_xrgb) {
        size_t qwords = ((size_t)fb_w * (size_t)fb_h) / 2;
        const uint64_t *s64 = (const uint64_t *)src_xrgb;
        uint64_t *d64 = (uint64_t *)back_buf;
        for (size_t i = 0; i < qwords; i++) {
            d64[i] = s64[i];
        }
        return;
    }
    size_t total = (size_t)fb_w * (size_t)fb_h;
    for (size_t i = 0; i < total; i++) {
        back_buf[i] = to_hw_color(src_xrgb[i]);
    }
}

void fb_fill_vgradient(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t top_rgb, uint32_t bot_rgb) {
    if (h <= 0 || w <= 0) return;
    int32_t r0 = (int32_t)((top_rgb >> 16) & 0xFF), g0 = (int32_t)((top_rgb >> 8) & 0xFF), b0 = (int32_t)(top_rgb & 0xFF);
    int32_t r1 = (int32_t)((bot_rgb >> 16) & 0xFF), g1 = (int32_t)((bot_rgb >> 8) & 0xFF), b1 = (int32_t)(bot_rgb & 0xFF);

    for (int32_t row = 0; row < h; row++) {
        uint32_t r = (uint32_t)(r0 + ((r1 - r0) * row) / h);
        uint32_t g = (uint32_t)(g0 + ((g1 - g0) * row) / h);
        uint32_t b = (uint32_t)(b0 + ((b1 - b0) * row) / h);
        fb_fill_rect(x, y + row, w, 1, (r << 16) | (g << 8) | b);
    }
}

void fb_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb) {
    int32_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;

    for (;;) {
        fb_put_pixel(x0, y0, rgb);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void fb_draw_circle(int32_t cx, int32_t cy, int32_t r, uint32_t rgb, bool fill) {
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            int32_t d2 = dx * dx + dy * dy;
            if (fill ? (d2 <= r * r) : (d2 <= r * r && d2 >= (r - 1) * (r - 1))) {
                fb_put_pixel(cx + dx, cy + dy, rgb);
            }
        }
    }
}

void fb_present(void) {
    fb_present_rect(0, 0, fb_w, fb_h);
}

void fb_present_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!back_buf || !front_buf || w <= 0 || h <= 0) return;
    int32_t x0 = MAX(x, 0) & ~1;
    int32_t y0 = MAX(y, 0);
    int32_t x1 = MIN(x + w, fb_w);
    int32_t y1 = MIN(y + h, fb_h);
    if (x0 >= x1 || y0 >= y1) return;

    if (x0 == 0 && x1 == fb_w && front_pitch_px == (uint32_t)fb_w) {
        size_t qwords = ((size_t)(y1 - y0) * (size_t)fb_w) / 2;
        const uint64_t *src = (const uint64_t *)&back_buf[y0 * fb_w];
        volatile uint64_t *dst = (volatile uint64_t *)&front_buf[y0 * front_pitch_px];
        for (size_t i = 0; i < qwords; i++) {
            dst[i] = src[i];
        }
        return;
    }

    int32_t span = x1 - x0;
    for (int32_t py = y0; py < y1; py++) {
        const uint32_t *src = &back_buf[py * fb_w + x0];
        volatile uint32_t *dst = &front_buf[py * front_pitch_px + x0];
        for (int32_t px = 0; px < span; px++) {
            dst[px] = src[px];
        }
    }
}
