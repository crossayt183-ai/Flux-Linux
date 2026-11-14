#include "ps2.h"
#include "idt.h"
#include "io.h"
#include "spinlock.h"
#include "serial.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define KEY_Q_SIZE 64

static key_event_t   key_q[KEY_Q_SIZE];
static volatile uint32_t kq_head = 0;
static volatile uint32_t kq_tail = 0;

static volatile bool kbd_shift = false;
static volatile bool kbd_ctrl  = false;
static volatile bool kbd_alt   = false;
static volatile bool kbd_caps  = false;
static volatile bool kbd_ext   = false;

static mouse_state_t mouse = { .x = 512, .y = 384, .buttons = 0, .prev_buttons = 0 };
static volatile bool mouse_dirty = false;
static int32_t bounds_w = 1024;
static int32_t bounds_h = 768;
static uint8_t mouse_pkt[3];
static uint8_t mouse_cycle = 0;

static spinlock_t ps2_lock = SPINLOCK_INIT;

static const char sc_normal[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0
};

static const char sc_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0
};

static void ps2_wait_write(void) {
    for (int i = 0; i < 10000; i++) {
        if ((inb(PS2_STATUS) & 0x02) == 0) return;
    }
}

static bool ps2_wait_read(void) {
    for (int i = 0; i < 10000; i++) {
        if ((inb(PS2_STATUS) & 0x01) != 0) return true;
    }
    return false;
}

static void ps2_flush(void) {
    for (int i = 0; i < 64 && (inb(PS2_STATUS) & 0x01) != 0; i++) {
        (void)inb(PS2_DATA);
    }
}

static void ps2_mouse_write(uint8_t val) {
    ps2_wait_write();
    outb(PS2_CMD, 0xD4);
    ps2_wait_write();
    outb(PS2_DATA, val);
    if (ps2_wait_read()) (void)inb(PS2_DATA);
}

static void push_key(key_event_t ev) {
    spin_lock_irqsave(&ps2_lock);
    uint32_t next = (kq_head + 1) % KEY_Q_SIZE;
    if (next != kq_tail) {
        key_q[kq_head] = ev;
        kq_head = next;
    }
    spin_unlock_irqrestore(&ps2_lock);
}

static cpu_regs_t *ps2_kbd_irq(cpu_regs_t *regs) {
    uint8_t sc = inb(PS2_DATA);

    if (sc == 0xE0) {
        kbd_ext = true;
        return regs;
    }

    bool pressed = (sc & 0x80) == 0;
    uint8_t code = sc & 0x7F;

    if (kbd_ext) {
        kbd_ext = false;
        uint8_t ch = 0;
        if (code == 0x48) ch = KEY_UP;
        else if (code == 0x50) ch = KEY_DOWN;
        else if (code == 0x4B) ch = KEY_LEFT;
        else if (code == 0x4D) ch = KEY_RIGHT;
        if (ch && pressed) {
            push_key((key_event_t){ code, ch, true, kbd_shift, kbd_ctrl, kbd_alt });
        }
        return regs;
    }

    if (code == 0x2A || code == 0x36) {
        kbd_shift = pressed;
        return regs;
    }
    if (code == 0x1D) {
        kbd_ctrl = pressed;
        return regs;
    }
    if (code == 0x38) {
        kbd_alt = pressed;
        return regs;
    }
    if (code == 0x3A && pressed) {
        kbd_caps = !kbd_caps;
        return regs;
    }

    char base = (code < 128) ? sc_normal[code] : 0;
    char sh   = (code < 128) ? sc_shift[code]  : 0;
    char out  = kbd_shift ? sh : base;
    if (kbd_caps && base >= 'a' && base <= 'z') {
        out = kbd_shift ? base : sh;
    }

    push_key((key_event_t){ code, (uint8_t)out, pressed, kbd_shift, kbd_ctrl, kbd_alt });
    return regs;
}

static cpu_regs_t *ps2_mouse_irq(cpu_regs_t *regs) {
    uint8_t st = inb(PS2_STATUS);
    if (!(st & 0x01) || !(st & 0x20)) {
        return regs;
    }
    uint8_t b = inb(PS2_DATA);

    if (mouse_cycle == 0 && !(b & 0x08)) {
        return regs; /* Re-sync packet boundary */
    }
    mouse_pkt[mouse_cycle++] = b;
    if (mouse_cycle < 3) return regs;
    mouse_cycle = 0;

    uint8_t flags = mouse_pkt[0];
    if (flags & 0xC0) return regs; /* X/Y overflow discard */

    int32_t dx = (int32_t)mouse_pkt[1] - ((flags & 0x10) ? 256 : 0);
    int32_t dy = (int32_t)mouse_pkt[2] - ((flags & 0x20) ? 256 : 0);

    spin_lock_irqsave(&ps2_lock);
    mouse.x += dx;
    mouse.y -= dy; /* PS/2 Y axis is inverted relative to screen coordinates */
    if (mouse.x < 0) mouse.x = 0;
    if (mouse.y < 0) mouse.y = 0;
    if (mouse.x >= bounds_w) mouse.x = bounds_w - 1;
    if (mouse.y >= bounds_h) mouse.y = bounds_h - 1;
    mouse.buttons = flags & 0x07;
    mouse_dirty = true;
    spin_unlock_irqrestore(&ps2_lock);
    return regs;
}

void ps2_set_bounds(int32_t screen_w, int32_t screen_h) {
    bounds_w = (screen_w > 0) ? screen_w : 1024;
    bounds_h = (screen_h > 0) ? screen_h : 768;
    mouse.x = bounds_w / 2;
    mouse.y = bounds_h / 2;
}

void ps2_init(int32_t screen_w, int32_t screen_h) {
    ps2_set_bounds(screen_w, screen_h);
    ps2_flush();

    /* Enable PS/2 port 1 + auxiliary mouse port 2 */
    ps2_wait_write(); outb(PS2_CMD, 0xAE);
    ps2_wait_write(); outb(PS2_CMD, 0xA8);

    /* Enable IRQ1 (keyboard) & IRQ12 (mouse) in controller config */
    ps2_wait_write(); outb(PS2_CMD, 0x20);
    uint8_t cfg = ps2_wait_read() ? inb(PS2_DATA) : 0x47;
    cfg = (uint8_t)((cfg | 0x03) & ~0x30);
    ps2_wait_write(); outb(PS2_CMD, 0x60);
    ps2_wait_write(); outb(PS2_DATA, cfg);

    ps2_mouse_write(0xF6); /* Defaults */
    ps2_mouse_write(0xF4); /* Enable streaming */
    ps2_flush();

    irq_register(33, ps2_kbd_irq);   /* IRQ 1 */
    irq_register(44, ps2_mouse_irq); /* IRQ 12 */
    pic_mask_irq(1, false);
    pic_mask_irq(2, false);          /* Cascade IRQ2 required for slave PIC IRQ12 */
    pic_mask_irq(12, false);

    kprintf("[PS2] Keyboard (IRQ1) & Mouse (IRQ12) online (bounds=%dx%d)\n", bounds_w, bounds_h);
}

bool ps2_poll_key(key_event_t *out) {
    spin_lock_irqsave(&ps2_lock);
    if (kq_tail == kq_head) {
        spin_unlock_irqrestore(&ps2_lock);
        return false;
    }
    *out = key_q[kq_tail];
    kq_tail = (kq_tail + 1) % KEY_Q_SIZE;
    spin_unlock_irqrestore(&ps2_lock);
    return true;
}

bool ps2_poll_mouse(mouse_state_t *out) {
    spin_lock_irqsave(&ps2_lock);
    bool ch = mouse_dirty;
    if (ch) {
        mouse_state_t res = mouse;
        mouse.prev_buttons = mouse.buttons;
        mouse_dirty = false;
        *out = res;
    }
    spin_unlock_irqrestore(&ps2_lock);
    return ch;
}

mouse_state_t ps2_get_mouse(void) {
    spin_lock_irqsave(&ps2_lock);
    mouse_state_t m = mouse;
    spin_unlock_irqrestore(&ps2_lock);
    return m;
}
