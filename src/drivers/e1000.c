#include "e1000.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "spinlock.h"
#include "io.h"
#include "string.h"
#include "serial.h"

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD   0x0014
#define E1000_REG_ICR    0x00C0
#define E1000_REG_IMS    0x00D0
#define E1000_REG_IMC    0x00D8
#define E1000_REG_RCTL   0x0100
#define E1000_REG_TCTL   0x0400
#define E1000_REG_RDBAL  0x2800
#define E1000_REG_RDBAH  0x2804
#define E1000_REG_RDLEN  0x2808
#define E1000_REG_RDH    0x2810
#define E1000_REG_RDT    0x2818
#define E1000_REG_TDBAL  0x3800
#define E1000_REG_TDBAH  0x3804
#define E1000_REG_TDLEN  0x3808
#define E1000_REG_TDH    0x3810
#define E1000_REG_TDT    0x3818
#define E1000_REG_MTA    0x5200
#define E1000_REG_RAL0   0x5400
#define E1000_REG_RAH0   0x5404

#define CTRL_SLU         (1U << 6)
#define CTRL_RST         (1U << 26)

#define RCTL_EN          (1U << 1)
#define RCTL_UPE         (1U << 3)
#define RCTL_MPE         (1U << 4)
#define RCTL_BAM         (1U << 15)
#define RCTL_SECRC       (1U << 26)

#define TCTL_EN          (1U << 1)
#define TCTL_PSP         (1U << 3)

#define NUM_RX_DESC      32
#define NUM_TX_DESC      16
#define PKT_BUF_SIZE     2048

typedef struct PACKED {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct PACKED {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

static volatile uint8_t *mmio_base = NULL;
static bool              nic_ready = false;
static bool              is_82574 = false;
static uint8_t           mac_addr[6];

static e1000_rx_desc_t  *rx_descs = NULL;
static uint8_t          *rx_bufs[NUM_RX_DESC];
static uint32_t          rx_cur = 0;

static e1000_tx_desc_t  *tx_descs = NULL;
static uint8_t          *tx_bufs[NUM_TX_DESC];
static uint32_t          tx_cur = 0;

static spinlock_t        nic_lock = SPINLOCK_INIT;

static inline uint32_t mmio_r32(uint32_t off) {
    return *(volatile uint32_t *)(mmio_base + off);
}

static inline void mmio_w32(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(mmio_base + off) = val;
}

static uint16_t eeprom_read(uint8_t addr) {
    uint32_t req = is_82574 ? (((uint32_t)addr << 2) | 1U) : (((uint32_t)addr << 8) | 1U);
    uint32_t done = is_82574 ? 0x02U : 0x10U;
    mmio_w32(E1000_REG_EERD, req);
    for (int t = 0; t < 20000; t++) {
        uint32_t v = mmio_r32(E1000_REG_EERD);
        if (v & done) return (uint16_t)(v >> 16);
        io_wait();
    }
    return 0;
}

static void read_mac_address(void) {
    uint32_t ral = mmio_r32(E1000_REG_RAL0);
    uint32_t rah = mmio_r32(E1000_REG_RAH0);
    if ((rah & (1U << 31)) && (ral != 0 || (rah & 0xFFFFU) != 0)) {
        mac_addr[0] = (uint8_t)(ral & 0xFFU);
        mac_addr[1] = (uint8_t)((ral >> 8) & 0xFFU);
        mac_addr[2] = (uint8_t)((ral >> 16) & 0xFFU);
        mac_addr[3] = (uint8_t)((ral >> 24) & 0xFFU);
        mac_addr[4] = (uint8_t)(rah & 0xFFU);
        mac_addr[5] = (uint8_t)((rah >> 8) & 0xFFU);
        return;
    }

    for (int i = 0; i < 3; i++) {
        uint16_t w = eeprom_read((uint8_t)i);
        mac_addr[i * 2 + 0] = (uint8_t)(w & 0xFFU);
        mac_addr[i * 2 + 1] = (uint8_t)(w >> 8);
    }

    uint32_t ral_w = (uint32_t)mac_addr[0] | ((uint32_t)mac_addr[1] << 8) |
                     ((uint32_t)mac_addr[2] << 16) | ((uint32_t)mac_addr[3] << 24);
    uint32_t rah_w = (uint32_t)mac_addr[4] | ((uint32_t)mac_addr[5] << 8) | (1U << 31);
    mmio_w32(E1000_REG_RAL0, ral_w);
    mmio_w32(E1000_REG_RAH0, rah_w);
}

bool e1000_init(void) {
    nic_ready = false;

    pci_device_t dev;
    if (!pci_find_class(0x02, 0x00, -1, &dev) || dev.vendor_id != 0x8086) {
        kprintf("[E1000] No Intel Gigabit NIC detected on PCI\n");
        return false;
    }

    is_82574 = (dev.device_id == 0x10D3U);
    pci_enable_bus_master(&dev);

    bool is_io = false, is_64 = false;
    uint64_t bar0 = pci_get_bar_addr(&dev, 0, &is_io, &is_64);
    if (!bar0 || is_io) {
        kprintf("[E1000] Invalid BAR0 MMIO base\n");
        return false;
    }

    mmio_base = (volatile uint8_t *)vmm_map_mmio(bar0, 0x20000);
    if (!mmio_base) return false;

    /* Reset NIC and bring link up */
    mmio_w32(E1000_REG_IMC, 0xFFFFFFFFU);
    mmio_w32(E1000_REG_CTRL, mmio_r32(E1000_REG_CTRL) | CTRL_RST);
    for (int i = 0; i < 2000; i++) io_wait();
    mmio_w32(E1000_REG_IMC, 0xFFFFFFFFU);
    mmio_w32(E1000_REG_CTRL, (mmio_r32(E1000_REG_CTRL) | CTRL_SLU | (1U << 5)) & ~CTRL_RST);

    for (uint32_t i = 0; i < 128; i++) {
        mmio_w32(E1000_REG_MTA + i * 4U, 0);
    }
    read_mac_address();

    /* Setup RX ring */
    uint64_t rx_ring_phys = pmm_alloc_page();
    if (!rx_ring_phys) return false;
    rx_descs = (e1000_rx_desc_t *)phys_to_virt(rx_ring_phys);
    memset(rx_descs, 0, PAGE_SIZE);

    for (uint32_t i = 0; i < NUM_RX_DESC; i += 2) {
        uint64_t p = pmm_alloc_page();
        if (!p) return false;
        memset(phys_to_virt(p), 0, PAGE_SIZE);
        rx_bufs[i]     = (uint8_t *)phys_to_virt(p);
        rx_bufs[i + 1] = (uint8_t *)phys_to_virt(p + PKT_BUF_SIZE);
        rx_descs[i].addr     = p;
        rx_descs[i + 1].addr = p + PKT_BUF_SIZE;
    }

    mmio_w32(E1000_REG_RDBAL, (uint32_t)rx_ring_phys);
    mmio_w32(E1000_REG_RDBAH, (uint32_t)(rx_ring_phys >> 32));
    mmio_w32(E1000_REG_RDLEN, NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    mmio_w32(E1000_REG_RDH, 0);
    mmio_w32(E1000_REG_RDT, NUM_RX_DESC - 1);
    rx_cur = 0;
    mmio_w32(E1000_REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    /* Setup TX ring */
    uint64_t tx_ring_phys = pmm_alloc_page();
    if (!tx_ring_phys) return false;
    tx_descs = (e1000_tx_desc_t *)phys_to_virt(tx_ring_phys);
    memset(tx_descs, 0, PAGE_SIZE);

    for (uint32_t i = 0; i < NUM_TX_DESC; i += 2) {
        uint64_t p = pmm_alloc_page();
        if (!p) return false;
        memset(phys_to_virt(p), 0, PAGE_SIZE);
        tx_bufs[i]     = (uint8_t *)phys_to_virt(p);
        tx_bufs[i + 1] = (uint8_t *)phys_to_virt(p + PKT_BUF_SIZE);
        tx_descs[i].addr     = p;
        tx_descs[i + 1].addr = p + PKT_BUF_SIZE;
        tx_descs[i].status     = 1; /* DD */
        tx_descs[i + 1].status = 1;
    }

    mmio_w32(E1000_REG_TDBAL, (uint32_t)tx_ring_phys);
    mmio_w32(E1000_REG_TDBAH, (uint32_t)(tx_ring_phys >> 32));
    mmio_w32(E1000_REG_TDLEN, NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    mmio_w32(E1000_REG_TDH, 0);
    mmio_w32(E1000_REG_TDT, 0);
    tx_cur = 0;
    mmio_w32(E1000_REG_TCTL, TCTL_EN | TCTL_PSP | (15U << 4) | (64U << 12));

    nic_ready = true;
    kprintf("[E1000] Intel %s NIC (0x%04x) online MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            is_82574 ? "82574L" : "82540EM", dev.device_id,
            mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    return true;
}

bool e1000_is_ready(void) { return nic_ready; }

void e1000_get_mac(uint8_t out_mac[6]) {
    memcpy(out_mac, mac_addr, 6);
}

int e1000_send(const void *frame, size_t len) {
    if (!nic_ready || !frame || len == 0 || len > 1518) return -1;

    spin_lock_irqsave(&nic_lock);
    uint32_t idx = tx_cur;
    for (int t = 0; t < 20000 && !(tx_descs[idx].status & 1U); t++) {
        io_wait();
    }
    if (!(tx_descs[idx].status & 1U)) {
        spin_unlock_irqrestore(&nic_lock);
        return -1;
    }

    memcpy(tx_bufs[idx], frame, len);
    tx_descs[idx].length = (uint16_t)len;
    tx_descs[idx].cmd    = 0x0B; /* EOP | IFCS | RS */
    tx_descs[idx].status = 0;

    tx_cur = (idx + 1) % NUM_TX_DESC;
    mmio_w32(E1000_REG_TDT, tx_cur);
    spin_unlock_irqrestore(&nic_lock);
    return 0;
}

void e1000_poll_rx(e1000_rx_cb_t cb) {
    if (!nic_ready || !cb) return;

    for (int budget = 0; budget < NUM_RX_DESC; budget++) {
        spin_lock_irqsave(&nic_lock);
        uint32_t idx = rx_cur;
        if (!(rx_descs[idx].status & 1U)) {
            spin_unlock_irqrestore(&nic_lock);
            break;
        }
        uint16_t len = rx_descs[idx].length;
        uint8_t copy[PKT_BUF_SIZE];
        if (len > PKT_BUF_SIZE) len = PKT_BUF_SIZE;
        memcpy(copy, rx_bufs[idx], len);
        rx_descs[idx].status = 0;
        rx_cur = (idx + 1) % NUM_RX_DESC;
        mmio_w32(E1000_REG_RDT, idx);
        spin_unlock_irqrestore(&nic_lock);

        if (len >= 14) {
            cb(copy, len);
        }
    }
}
