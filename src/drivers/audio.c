#include "audio.h"
#include "pci.h"
#include "pmm.h"
#include "io.h"
#include "timer.h"
#include "string.h"
#include "serial.h"

/* Intel 82801AA AC'97 PCI IDs */
#define AC97_VENDOR_INTEL 0x8086
#define AC97_DEVICE_ICH   0x2415

/* Native Audio Mixer Registers (NAMBAR offsets) */
#define NAM_RESET         0x00
#define NAM_MASTER_VOL    0x02
#define NAM_PCM_OUT_VOL   0x18
#define NAM_EXT_AUDIO_ID  0x28
#define NAM_EXT_CTRL      0x2A
#define NAM_PCM_RATE      0x2C

/* Native Audio Bus Master Registers (NABMBAR offsets) */
#define NABM_PO_BDBAR     0x10
#define NABM_PO_CIV       0x14
#define NABM_PO_LVI       0x15
#define NABM_PO_SR        0x16
#define NABM_PO_CR        0x1B
#define NABM_GLOB_CNT     0x2C

#define AC97_BDL_COUNT    32
#define AC97_FRAMES_PER_BUF 1024 /* 4096 B = 1024 stereo 16-bit frames (~21.3ms @ 48kHz) */

typedef struct PACKED {
    uint32_t phys_addr;
    uint16_t samples; /* Number of 16-bit mono/channel samples = frames * 2 */
    uint16_t flags;
} ac97_bdl_entry_t;

static bool             ac97_ready = false;
static uint16_t         nam_base = 0;
static uint16_t         nabm_base = 0;
static ac97_bdl_entry_t *bdl = NULL;
static uint64_t         bdl_phys = 0;
static int16_t          *dma_bufs[AC97_BDL_COUNT];
static uint8_t          write_idx = 0;
static audio_fill_cb_t  fill_callback = NULL;

bool audio_init(audio_fill_cb_t fill_cb) {
    fill_callback = fill_cb;
    ac97_ready = false;

    pci_device_t dev;
    if (!pci_find_device(AC97_VENDOR_INTEL, AC97_DEVICE_ICH, &dev)) {
        kprintf("[AUDIO] AC'97 not detected; using PC Speaker fallback\n");
        return false;
    }

    pci_enable_bus_master(&dev);
    nam_base  = (uint16_t)(dev.bars[0] & ~0x3U);
    nabm_base = (uint16_t)(dev.bars[1] & ~0x3U);
    if (!nam_base || !nabm_base) {
        kprintf("[AUDIO] AC'97 invalid BAR0/BAR1, using PC Speaker fallback\n");
        return false;
    }

    /* De-assert AC'97 cold reset (Global Control bit 1) using IRQ-safe delay */
    outl(nabm_base + NABM_GLOB_CNT, 0x00000002U);
    for (int i = 0; i < 5000; i++) io_wait();
    outw(nam_base + NAM_RESET, 0x0000);
    outw(nam_base + NAM_MASTER_VOL, 0x0000);  /* Unmute, 0 dB */
    outw(nam_base + NAM_PCM_OUT_VOL, 0x0000); /* Unmute PCM, 0 dB */

    /* Enable Variable Rate Audio if supported and request 48000 Hz */
    uint16_t ext_id = inw(nam_base + NAM_EXT_AUDIO_ID);
    if (ext_id & 0x0001U) {
        outw(nam_base + NAM_EXT_CTRL, inw(nam_base + NAM_EXT_CTRL) | 0x0001U);
        outw(nam_base + NAM_PCM_RATE, AUDIO_SAMPLE_RATE);
    }

    bdl_phys = pmm_alloc_page();
    if (!bdl_phys || bdl_phys >= 0x100000000ULL) {
        kprintf("[AUDIO] Failed to allocate <4GB DMA page for BDL\n");
        return false;
    }
    bdl = (ac97_bdl_entry_t *)phys_to_virt(bdl_phys);
    memset(bdl, 0, PAGE_SIZE);

    for (uint32_t i = 0; i < AC97_BDL_COUNT; i++) {
        uint64_t p = pmm_alloc_page();
        if (!p || p >= 0x100000000ULL) {
            kprintf("[AUDIO] Failed to allocate DMA buffer %u\n", i);
            return false;
        }
        dma_bufs[i] = (int16_t *)phys_to_virt(p);
        memset(dma_bufs[i], 0, PAGE_SIZE);
        bdl[i].phys_addr = (uint32_t)p;
        bdl[i].samples   = (uint16_t)(AC97_FRAMES_PER_BUF * 2);
        bdl[i].flags     = 0;
    }

    /* Reset PCM Out channel registers */
    outb(nabm_base + NABM_PO_CR, 0x02);
    for (int t = 0; t < 10000 && (inb(nabm_base + NABM_PO_CR) & 0x02); t++) {}

    outl(nabm_base + NABM_PO_BDBAR, (uint32_t)bdl_phys);
    write_idx = 4;
    outb(nabm_base + NABM_PO_LVI, (uint8_t)(write_idx - 1));
    outb(nabm_base + NABM_PO_CR, 0x01); /* RPBM: Start DMA playback */

    ac97_ready = true;
    kprintf("[AUDIO] Intel AC'97 online (NAM=0x%x NABM=0x%x, 48kHz stereo DMA)\n",
            nam_base, nabm_base);
    return true;
}

bool audio_has_ac97(void) {
    return ac97_ready;
}

void audio_poll_refill(void) {
    if (!ac97_ready || !fill_callback) return;

    uint8_t civ = inb(nabm_base + NABM_PO_CIV);
    uint8_t queued = (uint8_t)((write_idx - civ) & (AC97_BDL_COUNT - 1));

    while (queued < 4) {
        fill_callback(dma_bufs[write_idx], AC97_FRAMES_PER_BUF);
        outb(nabm_base + NABM_PO_LVI, write_idx);
        write_idx = (uint8_t)((write_idx + 1) & (AC97_BDL_COUNT - 1));
        queued++;
    }

    uint16_t sr = inw(nabm_base + NABM_PO_SR);
    if (sr & 0x01U) {
        /* Clear DMA Halted status bit and resume playback */
        outw(nabm_base + NABM_PO_SR, sr & 0x1CU);
        outb(nabm_base + NABM_PO_CR, 0x01);
    }
}

void audio_pcspkr_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz < 20 || freq_hz > 20000) return;
    uint32_t div = 1193180U / freq_hz;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));

    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 0x03);
    timer_sleep_ms(duration_ms);
    outb(0x61, (uint8_t)(inb(0x61) & 0xFC));
}
