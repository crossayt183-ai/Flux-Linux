#include "stdio.h"
#include "sys/security.h"

int main(void) {
    const int w = 200;
    const int h = 90;
    void *shm = NULL;

    int sid = wl_surface_create("org.flux.WaylandDemo", w, h, &shm);
    if (sid <= 0 || !shm) {
        printf("[ELF/wldemo] FAIL: wl_surface_create\n");
        return 1;
    }

    uint32_t *px = (uint32_t *)shm;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t r = (uint8_t)(16 + (x * 42) / w);
            uint8_t g = (uint8_t)(24 + (y * 56) / h);
            uint8_t b = (uint8_t)(48 + ((x + y) * 90) / (w + h));

            /* Subtle perspective cyan grid */
            if ((x % 20 == 0) || (y % 18 == 0)) {
                r += 14; g += 28; b += 40;
            }
            /* Stylized neon Wayland / Flux diamond crest on left */
            int dx = x - 44;
            int dy = y - 45;
            int manhattan = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (manhattan >= 14 && manhattan <= 22) {
                r = 87; g = 227; b = 137;
            } else if (manhattan < 14) {
                r = 53; g = 132; b = 228;
            }

            /* Telemetry spectrum equalizer bars on right */
            if (x >= 84 && x < 184 && y >= 22 && y <= 68) {
                int bar = (x - 84) / 10;
                int lx  = (x - 84) % 10;
                int bar_h = 12 + ((bar * 7 + 5) % 32);
                if (lx < 7 && (68 - y) <= bar_h) {
                    r = (uint8_t)(60 + bar * 18);
                    g = (uint8_t)(210 - bar * 8);
                    b = 240;
                }
            }

            /* 1px crisp border */
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                r = 98; g = 160; b = 234;
            }

            px[y * w + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    if (wl_surface_commit(sid) != 0) {
        printf("[ELF/wldemo] FAIL: wl_surface_commit\n");
        return 1;
    }

    printf("[ELF/wldemo] Wayland wl_shm surface #%d (%dx%d) committed from Ring 3 @ %p\n",
           sid, w, h, shm);
    return 0;
}
