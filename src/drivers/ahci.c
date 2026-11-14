#include "ahci.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "mutex.h"
#include "io.h"
#include "string.h"
#include "serial.h"

/* AHCI HBA register indices (32-bit dword offsets) */
#define HBA_CAP       0x00
#define HBA_GHC       0x01
#define HBA_IS        0x02
#define HBA_PI        0x03

#define GHC_AE        (1U << 31)

/* AHCI Port register indices (32-bit dword offsets from port base) */
#define PORT_CLB      0x00
#define PORT_CLBU     0x01
#define PORT_FB       0x02
#define PORT_FBU      0x03
#define PORT_IS       0x04
#define PORT_IE       0x05
#define PORT_CMD      0x06
#define PORT_TFD      0x08
#define PORT_SIG      0x09
#define PORT_SSTS     0x0A
#define PORT_SERR     0x0C
#define PORT_CI       0x0E

#define PORT_CMD_ST   (1U << 0)
#define PORT_CMD_FRE  (1U << 4)
#define PORT_CMD_FR   (1U << 14)
#define PORT_CMD_CR   (1U << 15)

#define PORT_IS_TFES  (1U << 30)
#define SATA_SIG_ATA  0x00000101U

#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_READ_DMA_EXT  0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_FLUSH_EXT     0xEA

#define FIS_TYPE_REG_H2D      0x27
#define AHCI_MAX_DRIVES       4

typedef struct PACKED {
    uint8_t  cfl : 5;
    uint8_t  a   : 1;
    uint8_t  w   : 1;
    uint8_t  p   : 1;
    uint8_t  r   : 1;
    uint8_t  b   : 1;
    uint8_t  c   : 1;
    uint8_t  rsv0 : 1;
    uint8_t  pmp : 4;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} ahci_cmd_hdr_t;

typedef struct PACKED {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc;
} ahci_prdt_entry_t;

typedef struct PACKED {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    ahci_prdt_entry_t prdt[8];
} ahci_cmd_tbl_t;

typedef struct PACKED {
    uint8_t  fis_type;
    uint8_t  pmport_c;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  featureh;
    uint8_t  countl;
    uint8_t  counth;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv[4];
} fis_reg_h2d_t;

typedef struct {
    volatile uint32_t *port;
    int                port_num;
    ahci_cmd_hdr_t    *cmd_list;
    ahci_cmd_tbl_t    *cmd_tbl;
    uint64_t           total_sectors;
    char               model[44];
} ahci_drive_t;

static volatile uint32_t *hba_regs = NULL;
static ahci_drive_t       drives[AHCI_MAX_DRIVES];
static int                num_drives = 0;
static int                cur_drive = -1;
static uint64_t           dma_phys = 0;
static uint8_t           *dma_virt = NULL;
static mutex_t            ahci_mtx;

#define DMA_BUF_PAGES   16  /* 64 KiB = 128 sectors max per single transfer */
#define DMA_MAX_SECTORS 128

static void port_stop_cmd(volatile uint32_t *port) {
    port[PORT_CMD] &= ~PORT_CMD_ST;
    for (int t = 0; t < 100000 && (port[PORT_CMD] & PORT_CMD_CR); t++) io_wait();
    port[PORT_CMD] &= ~PORT_CMD_FRE;
    for (int t = 0; t < 100000 && (port[PORT_CMD] & PORT_CMD_FR); t++) io_wait();
}

static void port_start_cmd(volatile uint32_t *port) {
    for (int t = 0; t < 100000 && (port[PORT_CMD] & PORT_CMD_CR); t++) io_wait();
    port[PORT_CMD] |= PORT_CMD_FRE;
    port[PORT_CMD] |= PORT_CMD_ST;
}

static int issue_ata_cmd(ahci_drive_t *d, uint8_t cmd, uint64_t lba, uint16_t sectors, bool is_write, uint32_t bytes) {
    if (!d || !d->port) return -1;
    volatile uint32_t *port = d->port;

    for (int t = 0; t < 200000; t++) {
        if ((port[PORT_TFD] & 0x88U) == 0) break;
        io_wait();
        if (t == 199999) return -2;
    }

    port[PORT_IS] = 0xFFFFFFFFU;

    ahci_cmd_hdr_t *hdr = &d->cmd_list[0];
    hdr->cfl   = sizeof(fis_reg_h2d_t) / 4;
    hdr->a     = 0;
    hdr->w     = is_write ? 1 : 0;
    hdr->p     = 0;
    hdr->prdbc = 0;
    hdr->prdtl = (bytes > 0) ? 1 : 0;

    memset(d->cmd_tbl, 0, sizeof(*d->cmd_tbl));
    if (bytes > 0) {
        d->cmd_tbl->prdt[0].dba  = (uint32_t)dma_phys;
        d->cmd_tbl->prdt[0].dbau = (uint32_t)(dma_phys >> 32);
        d->cmd_tbl->prdt[0].dbc  = (bytes - 1U) | (1U << 31);
    }

    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)d->cmd_tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;
    fis->command  = cmd;
    fis->device   = (cmd == ATA_CMD_IDENTIFY) ? 0x00 : 0x40;
    fis->lba0     = (uint8_t)(lba & 0xFFU);
    fis->lba1     = (uint8_t)((lba >> 8) & 0xFFU);
    fis->lba2     = (uint8_t)((lba >> 16) & 0xFFU);
    fis->lba3     = (uint8_t)((lba >> 24) & 0xFFU);
    fis->lba4     = (uint8_t)((lba >> 32) & 0xFFU);
    fis->lba5     = (uint8_t)((lba >> 40) & 0xFFU);
    fis->countl   = (uint8_t)(sectors & 0xFFU);
    fis->counth   = (uint8_t)((sectors >> 8) & 0xFFU);

    port[PORT_CI] = 1U;

    for (int t = 0; t < 500000; t++) {
        if ((port[PORT_CI] & 1U) == 0) {
            if (port[PORT_IS] & PORT_IS_TFES) return -3;
            if (port[PORT_TFD] & 0x01U) return -4;
            return 0;
        }
        if (port[PORT_IS] & PORT_IS_TFES) return -3;
        io_wait();
    }
    return -5;
}

static bool probe_port(int p) {
    if (num_drives >= AHCI_MAX_DRIVES) return false;
    volatile uint32_t *port = (volatile uint32_t *)((uintptr_t)hba_regs + 0x100 + (size_t)p * 0x80);
    uint32_t ssts = port[PORT_SSTS];
    uint8_t det = (uint8_t)(ssts & 0x0FU);
    uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0FU);
    if (det != 3 || ipm != 1) return false;
    if (port[PORT_SIG] != SATA_SIG_ATA) return false;

    port_stop_cmd(port);

    uint64_t clb_fb_phys = pmm_alloc_page();
    uint64_t ct_phys     = pmm_alloc_page();
    if (!clb_fb_phys || !ct_phys) return false;

    ahci_drive_t *d = &drives[num_drives];
    memset(d, 0, sizeof(*d));
    d->port     = port;
    d->port_num = p;
    d->cmd_list = (ahci_cmd_hdr_t *)phys_to_virt(clb_fb_phys);
    d->cmd_tbl  = (ahci_cmd_tbl_t *)phys_to_virt(ct_phys);
    memset(d->cmd_list, 0, PAGE_SIZE);
    memset(d->cmd_tbl, 0, PAGE_SIZE);

    d->cmd_list[0].ctba  = (uint32_t)ct_phys;
    d->cmd_list[0].ctbau = (uint32_t)(ct_phys >> 32);

    uint64_t fb_phys = clb_fb_phys + 0x800;
    port[PORT_CLB]  = (uint32_t)clb_fb_phys;
    port[PORT_CLBU] = (uint32_t)(clb_fb_phys >> 32);
    port[PORT_FB]   = (uint32_t)fb_phys;
    port[PORT_FBU]  = (uint32_t)(fb_phys >> 32);
    port[PORT_IS]   = 0xFFFFFFFFU;
    port[PORT_SERR] = 0xFFFFFFFFU;
    port[PORT_IE]   = 0;

    port_start_cmd(port);

    memset(dma_virt, 0, 512);
    if (issue_ata_cmd(d, ATA_CMD_IDENTIFY, 0, 0, false, 512) != 0) {
        return false;
    }

    const uint16_t *id = (const uint16_t *)dma_virt;
    for (int i = 0; i < 20; i++) {
        uint16_t w = id[27 + i];
        d->model[i * 2 + 0] = (char)(w >> 8);
        d->model[i * 2 + 1] = (char)(w & 0xFF);
    }
    d->model[40] = '\0';
    for (int i = 39; i >= 0 && d->model[i] == ' '; i--) d->model[i] = '\0';

    d->total_sectors = ((uint64_t)id[103] << 48) | ((uint64_t)id[102] << 32) |
                       ((uint64_t)id[101] << 16) | (uint64_t)id[100];
    if (d->total_sectors == 0) {
        d->total_sectors = ((uint64_t)id[61] << 16) | (uint64_t)id[60];
    }
    if (d->total_sectors == 0) return false;

    kprintf("[AHCI] Port %d SATA disk: \"%s\" (%lu MiB, %lu sectors, DMA)\n",
            p, d->model, (d->total_sectors * 512ULL) / (1024ULL * 1024ULL), d->total_sectors);
    num_drives++;
    return true;
}

bool ahci_init(void) {
    mutex_init(&ahci_mtx);
    num_drives = 0;
    cur_drive = -1;

    pci_device_t dev;
    if (!pci_find_class(0x01, 0x06, 0x01, &dev)) {
        kprintf("[AHCI] No SATA AHCI controller found on PCI\n");
        return false;
    }

    pci_enable_bus_master(&dev);
    bool is_io = false, is_64 = false;
    uint64_t abar = pci_get_bar_addr(&dev, 5, &is_io, &is_64);
    if (!abar || is_io) {
        kprintf("[AHCI] Invalid BAR5 ABAR\n");
        return false;
    }

    hba_regs = (volatile uint32_t *)vmm_map_mmio(abar, 0x1100);
    dma_phys = pmm_alloc_pages(DMA_BUF_PAGES);
    if (!hba_regs || !dma_phys) {
        kprintf("[AHCI] Failed to allocate DMA/MMIO resources\n");
        return false;
    }
    dma_virt = (uint8_t *)phys_to_virt(dma_phys);

    hba_regs[HBA_GHC] |= GHC_AE;
    uint32_t pi = hba_regs[HBA_PI];
    for (int p = 0; p < 32; p++) {
        if (pi & (1U << p)) probe_port(p);
    }

    if (num_drives > 0) {
        cur_drive = 0;
        return true;
    }
    kprintf("[AHCI] Controller @ 0x%lx found, no attached SATA disk\n", abar);
    return false;
}

static uint8_t *ramdisk_base    = NULL;
static uint64_t ramdisk_sectors = 0;
static bool     use_ramdisk     = false;

void ahci_attach_ramdisk(void *base, uint64_t size_bytes) {
    ramdisk_base    = (uint8_t *)base;
    ramdisk_sectors = size_bytes / AHCI_SECTOR_SIZE;
    kprintf("[RAMDISK] Attached Limine ISO module @ %p (%lu KB, %lu sectors)\n",
            base, (unsigned long)(size_bytes / 1024), (unsigned long)ramdisk_sectors);
}

void ahci_enable_ramdisk_fallback(bool enable) {
    if (enable && ramdisk_base && ramdisk_sectors > 0) {
        use_ramdisk = true;
        kprintf("[RAMDISK] Activated in-memory ext2 rootfs fallback (%lu sectors)\n",
                (unsigned long)ramdisk_sectors);
    } else {
        use_ramdisk = false;
    }
}

bool ahci_is_ready(void) {
    if (use_ramdisk && ramdisk_base && ramdisk_sectors > 0) return true;
    return cur_drive >= 0 && cur_drive < num_drives;
}
int ahci_drive_count(void) { return num_drives; }

bool ahci_select_drive(int idx) {
    if (idx < 0 || idx >= num_drives) return false;
    cur_drive = idx;
    return true;
}

uint64_t ahci_sector_count(void) {
    if (use_ramdisk && ramdisk_base) return ramdisk_sectors;
    return (cur_drive >= 0 && cur_drive < num_drives) ? drives[cur_drive].total_sectors : 0;
}

const char *ahci_model(void) {
    if (use_ramdisk && ramdisk_base) return "Limine ISO LiveRAMDisk";
    return (cur_drive >= 0 && cur_drive < num_drives) ? drives[cur_drive].model : "none";
}

int ahci_read_sectors(uint64_t lba, uint32_t count, void *buf) {
    if (!buf || count == 0) return -1;
    if (use_ramdisk && ramdisk_base) {
        if (lba + count > ramdisk_sectors) return -1;
        memcpy(buf, ramdisk_base + lba * AHCI_SECTOR_SIZE, (size_t)count * AHCI_SECTOR_SIZE);
        return 0;
    }
    if (cur_drive < 0 || cur_drive >= num_drives) return -1;
    ahci_drive_t *d = &drives[cur_drive];
    if (lba + count > d->total_sectors) return -1;

    mutex_lock(&ahci_mtx);
    uint8_t *dst = (uint8_t *)buf;
    uint32_t left = count;
    int rc = 0;

    while (left > 0) {
        uint16_t chunk = (left > DMA_MAX_SECTORS) ? DMA_MAX_SECTORS : (uint16_t)left;
        uint32_t bytes = (uint32_t)chunk * AHCI_SECTOR_SIZE;
        rc = issue_ata_cmd(d, ATA_CMD_READ_DMA_EXT, lba, chunk, false, bytes);
        if (rc != 0) break;
        memcpy(dst, dma_virt, bytes);
        dst  += bytes;
        lba  += chunk;
        left -= chunk;
    }

    mutex_unlock(&ahci_mtx);
    return rc;
}

int ahci_write_sectors(uint64_t lba, uint32_t count, const void *buf) {
    if (!buf || count == 0) return -1;
    if (use_ramdisk && ramdisk_base) {
        if (lba + count > ramdisk_sectors) return -1;
        memcpy(ramdisk_base + lba * AHCI_SECTOR_SIZE, buf, (size_t)count * AHCI_SECTOR_SIZE);
        return 0;
    }
    if (cur_drive < 0 || cur_drive >= num_drives) return -1;
    ahci_drive_t *d = &drives[cur_drive];
    if (lba + count > d->total_sectors) return -1;

    mutex_lock(&ahci_mtx);
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t left = count;
    int rc = 0;

    while (left > 0) {
        uint16_t chunk = (left > DMA_MAX_SECTORS) ? DMA_MAX_SECTORS : (uint16_t)left;
        uint32_t bytes = (uint32_t)chunk * AHCI_SECTOR_SIZE;
        memcpy(dma_virt, src, bytes);
        rc = issue_ata_cmd(d, ATA_CMD_WRITE_DMA_EXT, lba, chunk, true, bytes);
        if (rc != 0) break;
        src  += bytes;
        lba  += chunk;
        left -= chunk;
    }

    if (rc == 0) {
        (void)issue_ata_cmd(d, ATA_CMD_FLUSH_EXT, 0, 0, false, 0);
    }

    mutex_unlock(&ahci_mtx);
    return rc;
}
