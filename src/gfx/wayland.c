#include "wayland.h"
#include "security.h"
#include "pmm.h"
#include "vmm.h"
#include "fb.h"
#include "spinlock.h"
#include "serial.h"
#include "string.h"

typedef struct {
    bool     active;
    int      id;
    int      width;
    int      height;
    size_t   num_pages;
    uint64_t phys_pages[WL_MAX_PAGES];
    uint32_t commits;
    char     title[32];
} wl_surface_t;

static wl_surface_t wl_surfs[WL_MAX_SURFACES];
static int          wl_latest_id   = -1;
static uint32_t     wl_total_commits = 0;
static spinlock_t   wl_lock        = SPINLOCK_INIT;

void wayland_init(void) {
    spin_lock_irqsave(&wl_lock);
    memset(wl_surfs, 0, sizeof(wl_surfs));
    wl_latest_id = -1;
    wl_total_commits = 0;
    spin_unlock_irqrestore(&wl_lock);
    kprintf("[WAYLAND] Shared-memory compositor protocol initialized (XRGB8888 wl_shm)\n");
}

int wl_surface_create_for_task(task_t *t, const char *title, int w, int h, uint64_t *user_va_out) {
    if (!t || !t->vm || !user_va_out) return -1;

    /* Enforce Flatpak / SELinux Wayland sandbox permission */
    if (t->sandbox_perms != 0xFFFFFFFFU && !(t->sandbox_perms & FLATPAK_PERM_WAYLAND)) {
        selinux_check_fs(t, "/etc/wayland-0", true, false);
        return -1;
    }

    if (w < 64) w = 64;
    if (w > WL_MAX_W) w = WL_MAX_W;
    if (h < 48) h = 48;
    if (h > WL_MAX_H) h = WL_MAX_H;

    size_t bytes = (size_t)w * (size_t)h * sizeof(uint32_t);
    size_t pages = ALIGN_UP(bytes, PAGE_SIZE) / PAGE_SIZE;
    if (pages > WL_MAX_PAGES) return -1;

    const char *lbl = (title && *title) ? title : "wl_surface";

    spin_lock_irqsave(&wl_lock);
    int slot = -1;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (wl_surfs[i].active && strcmp(wl_surfs[i].title, lbl) == 0 &&
            wl_surfs[i].num_pages >= pages) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < WL_MAX_SURFACES; i++) {
            if (!wl_surfs[i].active) { slot = i; break; }
        }
    }
    if (slot < 0) slot = 0;

    wl_surface_t *s = &wl_surfs[slot];
    if (s->active && s->num_pages < pages) {
        for (size_t p = 0; p < s->num_pages; p++) {
            if (s->phys_pages[p]) {
                pmm_free_page(s->phys_pages[p]);
                s->phys_pages[p] = 0;
            }
        }
        s->active = false;
    }

    if (!s->active) {
        memset(s, 0, sizeof(*s));
        for (size_t p = 0; p < pages; p++) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                for (size_t k = 0; k < p; k++) pmm_free_page(s->phys_pages[k]);
                spin_unlock_irqrestore(&wl_lock);
                return -1;
            }
            memset(phys_to_virt(phys), 0, PAGE_SIZE);
            s->phys_pages[p] = phys;
        }
        s->num_pages = pages;
        s->active    = true;
        s->id        = slot + 1;
    }

    s->width  = w;
    s->height = h;
    strncpy(s->title, lbl, sizeof(s->title) - 1);
    s->title[sizeof(s->title) - 1] = '\0';

    uint64_t uva_base = 0x0000003000000000ULL + (uint64_t)slot * 0x100000ULL;
    vmm_add_vma(t->vm, uva_base, uva_base + (uint64_t)pages * PAGE_SIZE,
                VMA_READ | VMA_WRITE | VMA_USER, VMA_ANON);

    for (size_t p = 0; p < pages; p++) {
        uint64_t existing = 0;
        uint64_t va = uva_base + (uint64_t)p * PAGE_SIZE;
        if (!vmm_get_pte(t->vm, va, &existing)) {
            if (!vmm_map_page(t->vm, va, s->phys_pages[p],
                              PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_NX)) {
                spin_unlock_irqrestore(&wl_lock);
                return -1;
            }
            pmm_ref_inc(s->phys_pages[p]);
        }
    }

    *user_va_out = uva_base;
    int id = s->id;
    wl_latest_id = id;
    spin_unlock_irqrestore(&wl_lock);
    return id;
}

int wl_surface_commit(int surf_id) {
    spin_lock_irqsave(&wl_lock);
    int idx = surf_id - 1;
    if (idx < 0 || idx >= WL_MAX_SURFACES || !wl_surfs[idx].active) {
        spin_unlock_irqrestore(&wl_lock);
        return -1;
    }
    wl_surfs[idx].commits++;
    wl_total_commits++;
    wl_latest_id = surf_id;
    spin_unlock_irqrestore(&wl_lock);
    return 0;
}

void wayland_stats(uint32_t *surfaces_out, uint32_t *commits_out) {
    spin_lock_irqsave(&wl_lock);
    uint32_t cnt = 0;
    for (int i = 0; i < WL_MAX_SURFACES; i++) {
        if (wl_surfs[i].active) cnt++;
    }
    if (surfaces_out) *surfaces_out = cnt;
    if (commits_out)  *commits_out  = wl_total_commits;
    spin_unlock_irqrestore(&wl_lock);
}

bool wayland_blit_latest(int32_t dst_x, int32_t dst_y, int32_t max_w, int32_t max_h,
                         char *title_out, size_t title_cap, uint32_t *commits_out,
                         int *surf_w_out, int *surf_h_out) {
    spin_lock_irqsave(&wl_lock);
    int idx = wl_latest_id - 1;
    if (idx < 0 || idx >= WL_MAX_SURFACES || !wl_surfs[idx].active || wl_surfs[idx].commits == 0) {
        spin_unlock_irqrestore(&wl_lock);
        return false;
    }

    wl_surface_t *s = &wl_surfs[idx];
    if (title_out && title_cap > 0) {
        strncpy(title_out, s->title, title_cap - 1);
        title_out[title_cap - 1] = '\0';
    }
    if (commits_out) *commits_out = s->commits;
    if (surf_w_out)  *surf_w_out  = s->width;
    if (surf_h_out)  *surf_h_out  = s->height;

    int32_t bw = MIN(s->width, max_w);
    int32_t bh = MIN(s->height, max_h);
    for (int32_t row = 0; row < bh; row++) {
        for (int32_t col = 0; col < bw; col++) {
            size_t px_idx = (size_t)row * (size_t)s->width + (size_t)col;
            size_t boff   = px_idx * sizeof(uint32_t);
            size_t p_idx  = boff / PAGE_SIZE;
            size_t p_off  = (boff & (PAGE_SIZE - 1)) / sizeof(uint32_t);
            if (p_idx < s->num_pages && s->phys_pages[p_idx]) {
                const uint32_t *page = (const uint32_t *)phys_to_virt(s->phys_pages[p_idx]);
                fb_put_pixel(dst_x + col, dst_y + row, page[p_off]);
            }
        }
    }
    spin_unlock_irqrestore(&wl_lock);
    return true;
}
