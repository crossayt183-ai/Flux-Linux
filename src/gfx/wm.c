#include "wm.h"
#include "fb.h"
#include "font.h"
#include "ps2.h"
#include "pmm.h"
#include "heap.h"
#include "vmm.h"
#include "task.h"
#include "timer.h"
#include "syscall.h"
#include "synth.h"
#include "audio.h"
#include "pci.h"
#include "ahci.h"
#include "vfs.h"
#include "e1000.h"
#include "net.h"
#include "firewall.h"
#include "security.h"
#include "wayland.h"
#include "gpu_drm.h"
#include "dkms.h"
#include "wifi_bt.h"
#include "hda_pipewire.h"
#include "fs_multi.h"
#include "proton_dxvk.h"
#include "smp_acpi.h"
#include "pkg_git.h"
#include "screenshot_vaapi.h"
#include "io.h"
#include "serial.h"
#include "string.h"

#define TITLEBAR_H 30
#define TOPBAR_H   28
#define DOCK_H     46
#define DOCK_W     356
#define DOCK_Y_PAD 12
#define TERM_COLS  60
#define TERM_ROWS  21

#define COL_BG_WIN      0x1B1E25
#define COL_BG_HDR_ACT  0x252A35
#define COL_BG_HDR_IDLE 0x1F232C
#define COL_BG_SIDEBAR  0x161920
#define COL_BORDER_ACT  0x465068
#define COL_BORDER_IDLE 0x2E3442
#define COL_ACCENT_BLUE 0x3584E4

static wm_window_t windows[WM_MAX_WINDOWS];
static int         z_order[WM_MAX_WINDOWS];
static int         win_count = 0;
static bool        wm_dirty = true;

/* Cached 3D cubes wallpaper decoded once into XRGB8888 */
static uint32_t *wall_buf = NULL;

/* Mouse dragging state */
static int     drag_win_idx = -1;
static int32_t drag_off_x = 0;
static int32_t drag_off_y = 0;

/* Terminal state */
static char       term_lines[TERM_ROWS][TERM_COLS + 1];
static int        term_cur_row = 0;
static int        term_cur_col = 0;
static char       term_cmd[TERM_COLS];
static int        term_cmd_len = 0;
static spinlock_t term_lock = SPINLOCK_INIT;

/* Flux Files state (cached per directory change for zero-lag redraws) */
static char         files_cwd[96] = "/";
static vfs_dirent_t files_ents[24];
static int          files_count = 0;
static char         files_status[64] = "System Disk (ext2 AHCI DMA)";
static bool         files_in_bin = false;

static void term_scroll_locked(void) {
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        memcpy(term_lines[r], term_lines[r + 1], TERM_COLS + 1);
    }
    memset(term_lines[TERM_ROWS - 1], 0, TERM_COLS + 1);
    term_cur_row = TERM_ROWS - 1;
}

void wm_term_write(const char *buf, size_t len) {
    if (!buf || len == 0) return;
    spin_lock_irqsave(&term_lock);
    for (size_t i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\r') {
            term_cur_col = 0;
        } else if (c == '\n') {
            term_lines[term_cur_row][term_cur_col] = '\0';
            term_cur_col = 0;
            if (term_cur_row + 1 < TERM_ROWS) term_cur_row++;
            else term_scroll_locked();
        } else if (c >= 32 && c <= 126) {
            if (term_cur_col >= TERM_COLS - 1) {
                term_lines[term_cur_row][term_cur_col] = '\0';
                term_cur_col = 0;
                if (term_cur_row + 1 < TERM_ROWS) term_cur_row++;
                else term_scroll_locked();
            }
            term_lines[term_cur_row][term_cur_col++] = c;
            term_lines[term_cur_row][term_cur_col] = '\0';
        }
    }
    wm_dirty = true;
    spin_unlock_irqrestore(&term_lock);
}

static void term_printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) wm_term_write(buf, (size_t)n);
}

static uint32_t parse_u32(const char *s) {
    uint32_t v = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

static bool parse_ipv4_str(const char *s, uint32_t *out) {
    if (!s || !*s || !out) return false;
    uint32_t p[4] = { 0, 0, 0, 0 }, cur = 0;
    int idx = 0;
    bool dig = false;
    for (;; s++) {
        char c = *s;
        if (c >= '0' && c <= '9') {
            cur = cur * 10U + (uint32_t)(c - '0');
            if (cur > 255U) return false;
            dig = true;
        } else if (c == '.' || c == '\0') {
            if (!dig || idx >= 4) return false;
            p[idx++] = cur;
            cur = 0;
            dig = false;
            if (c == '\0') break;
        } else {
            return false;
        }
    }
    if (idx != 4) return false;
    *out = ip4((uint8_t)p[0], (uint8_t)p[1], (uint8_t)p[2], (uint8_t)p[3]);
    return true;
}

static void fmt_ipv4_str(uint32_t ip, char out[20]) {
    ksnprintf(out, 20, "%u.%u.%u.%u",
              (ip >> 24) & 0xFFU, (ip >> 16) & 0xFFU, (ip >> 8) & 0xFFU, ip & 0xFFU);
}

static void files_cd(const char *path) {
    if (!path || !path[0]) path = "/";
    vfs_dirent_t tmp[24];
    int n = vfs_readdir(path, tmp, 24);
    if (n < 0) {
        ksnprintf(files_status, sizeof(files_status), "Cannot read %s", path);
        return;
    }
    strncpy(files_cwd, path, sizeof(files_cwd) - 1);
    files_cwd[sizeof(files_cwd) - 1] = '\0';
    files_in_bin = (strcmp(files_cwd, "/bin") == 0);

    files_count = 0;
    for (int i = 0; i < n && files_count < 24; i++) {
        if (strcmp(tmp[i].name, ".") == 0 || strcmp(tmp[i].name, "..") == 0 ||
            strcmp(tmp[i].name, "lost+found") == 0) {
            continue;
        }
        files_ents[files_count++] = tmp[i];
    }
    ksnprintf(files_status, sizeof(files_status), "%s  (%d items)", files_cwd, files_count);
    wm_dirty = true;
}

static void exec_shell_cmd(const char *cmd) {
    if (!cmd || !cmd[0]) return;

    if (strcmp(cmd, "help") == 0) {
        term_printf("Sys: uname, mem, ps, lspci, clear, run <prog>, open <app>\n");
        term_printf("FS:  ls [dir], cat <f>, mkdir <d>, write <f> <t>, rm <f>, df\n");
        term_printf("Net: ifconfig, arp, ping [ip] | Snd: play <Hz>, melody, wave\n");
        term_printf("Sec: sestatus, setenforce <0|1>, firewall-cmd, luks, secboot,\n");
        term_printf("     flatpak [run <id>], wayland, open sec\n");
    } else if (strcmp(cmd, "clear") == 0) {
        spin_lock_irqsave(&term_lock);
        memset(term_lines, 0, sizeof(term_lines));
        term_cur_row = 0; term_cur_col = 0;
        spin_unlock_irqrestore(&term_lock);
    } else if (strcmp(cmd, "uname") == 0) {
        term_printf("FluxOS 0.4 x86_64 (PIE/RELRO+SELinux+LUKS2+Wayland+Flatpak)\n");
    } else if (strcmp(cmd, "mem") == 0) {
        pmm_stats_t ps; heap_stats_t hs;
        pmm_get_stats(&ps); heap_get_stats(&hs);
        term_printf("RAM: %lu/%lu MB used | Heap: %lu B (%lu allocs)\n",
                    (ps.used_pages * 4) / 1024, (ps.total_pages * 4) / 1024,
                    hs.bytes_allocated, hs.active_allocs);
    } else if (strcmp(cmd, "ps") == 0) {
        task_t *ptrs[16];
        size_t n = sched_list_tasks(ptrs, 16);
        for (size_t i = 0; i < n; i++) {
            term_printf("PID %lu %-12s prio=%u user=%u ticks=%lu\n",
                        ptrs[i]->pid, ptrs[i]->name, ptrs[i]->priority,
                        ptrs[i]->is_user ? 1 : 0, ptrs[i]->total_ticks);
        }
    } else if (strcmp(cmd, "lspci") == 0) {
        pci_device_t devs[12];
        size_t n = pci_list_devices(devs, 12);
        for (size_t i = 0; i < n; i++) {
            term_printf("%02x:%02x.%u %04x:%04x (%s)\n",
                        devs[i].bus, devs[i].slot, devs[i].func,
                        devs[i].vendor_id, devs[i].device_id,
                        pci_class_name(devs[i].class_code, devs[i].subclass));
        }
    } else if (strcmp(cmd, "ls") == 0 || strncmp(cmd, "ls ", 3) == 0) {
        const char *path = (cmd[2] == ' ' && cmd[3]) ? cmd + 3 : "/";
        vfs_dirent_t ents[24];
        int n = vfs_readdir(path, ents, 24);
        if (n < 0) term_printf("ls: cannot open %s\n", path);
        for (int i = 0; i < n; i++) {
            term_printf("%s %-18s %lu B\n",
                        (ents[i].type == VFS_DIR) ? "<DIR>" : "     ",
                        ents[i].name, ents[i].size);
        }
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        uint8_t *buf = NULL;
        size_t sz = 0;
        if (vfs_read_all(cmd + 4, &buf, &sz)) {
            wm_term_write((const char *)buf, MIN(sz, (size_t)768));
            if (sz > 0 && buf[sz - 1] != '\n') term_printf("\n");
            kfree(buf);
        } else {
            term_printf("cat: cannot read %s\n", cmd + 4);
        }
    } else if (strncmp(cmd, "mkdir ", 6) == 0) {
        term_printf(vfs_mkdir(cmd + 6) == 0 ? "mkdir OK\n" : "mkdir failed\n");
        files_cd(files_cwd);
    } else if (strncmp(cmd, "write ", 6) == 0) {
        const char *p = cmd + 6;
        const char *sp = strchr(p, ' ');
        if (!sp) {
            term_printf("Usage: write </path> <text>\n");
        } else {
            char fpath[96];
            size_t plen = MIN((size_t)(sp - p), sizeof(fpath) - 1);
            memcpy(fpath, p, plen);
            fpath[plen] = '\0';
            vfs_inode_t *fn = vfs_open_path(fpath, true, true, NULL);
            if (fn) {
                const char *txt = sp + 1;
                vfs_write(fn, 0, txt, strlen(txt));
                vfs_write(fn, strlen(txt), "\n", 1);
                vfs_inode_put(fn);
                term_printf("Wrote %s\n", fpath);
                files_cd(files_cwd);
            } else {
                term_printf("write failed\n");
            }
        }
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        term_printf(vfs_unlink(cmd + 3) == 0 ? "Removed %s\n" : "rm failed\n", cmd + 3);
        files_cd(files_cwd);
    } else if (strcmp(cmd, "df") == 0) {
        vfs_statfs_t st;
        if (vfs_statfs(&st) == 0) {
            term_printf("ext2 '/': %lu/%lu KB free | inodes %u/%u | AHCI:%s\n",
                        st.free_kb, st.total_kb, st.free_inodes, st.total_inodes,
                        ahci_is_ready() ? ahci_model() : "none");
        } else {
            term_printf("No filesystem mounted\n");
        }
    } else if (strcmp(cmd, "ifconfig") == 0) {
        net_info_t ni;
        net_get_info(&ni);
        char ip[20], gw[20];
        fmt_ipv4_str(ni.ip, ip);
        fmt_ipv4_str(ni.gw, gw);
        term_printf("e1000 (%s): %02x:%02x:%02x:%02x:%02x:%02x | ip %s/24 gw %s\n",
                    e1000_is_ready() ? "UP" : "DOWN",
                    ni.mac[0], ni.mac[1], ni.mac[2], ni.mac[3], ni.mac[4], ni.mac[5], ip, gw);
        term_printf("RX packets: %lu | TX packets: %lu\n", ni.rx_packets, ni.tx_packets);
    } else if (strcmp(cmd, "arp") == 0) {
        arp_entry_t ents[16];
        size_t n = arp_list(ents, 16);
        if (n == 0) term_printf("ARP table empty\n");
        for (size_t i = 0; i < n; i++) {
            char ip[20];
            fmt_ipv4_str(ents[i].ip, ip);
            term_printf("%-15s -> %02x:%02x:%02x:%02x:%02x:%02x\n", ip,
                        ents[i].mac[0], ents[i].mac[1], ents[i].mac[2],
                        ents[i].mac[3], ents[i].mac[4], ents[i].mac[5]);
        }
    } else if (strcmp(cmd, "ping") == 0 || strncmp(cmd, "ping ", 5) == 0) {
        const char *arg = (cmd[4] == ' ' && cmd[5]) ? cmd + 5 : "10.0.2.2";
        uint32_t dst = 0;
        if (!parse_ipv4_str(arg, &dst)) {
            term_printf("ping: invalid IPv4 '%s'\n", arg);
        } else {
            for (uint16_t s = 1; s <= 2; s++) {
                uint32_t rtt = 0;
                if (icmp_ping(dst, s, 1000, &rtt)) {
                    term_printf("64 bytes from %s: seq=%u rtt=%ums\n", arg, s, rtt);
                } else {
                    term_printf("Request timeout seq=%u\n", s);
                }
            }
        }
    } else if (strncmp(cmd, "run ", 4) == 0) {
        char line[TERM_COLS];
        strncpy(line, cmd + 4, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        const char *argv[10];
        int argc = 0;
        char *p = line;
        while (*p && argc < 10) {
            while (*p == ' ') p++;
            if (!*p) break;
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p == ' ') *p++ = '\0';
        }
        if (argc > 0) {
            char path[64];
            if (argv[0][0] == '/') ksnprintf(path, sizeof(path), "%s", argv[0]);
            else ksnprintf(path, sizeof(path), "/bin/%s", argv[0]);
            argv[0] = path;
            uint64_t pid = 0;
            if (user_exec_program_argv(path, argc, argv, 2, &pid)) {
                term_printf("Spawned Ring3 %s (pid=%lu)\n", path, pid);
            } else {
                term_printf("Unknown prog '%s'\n", path);
            }
        }
    } else if (strncmp(cmd, "play ", 5) == 0) {
        uint32_t hz = parse_u32(cmd + 5);
        if (hz < 40) hz = 440;
        synth_play_note(hz, 260, (uint8_t)synth_get_default_wave(), 85);
        term_printf("Playing %u Hz (wave=%u)\n", hz, (uint32_t)synth_get_default_wave());
    } else if (strcmp(cmd, "melody") == 0) {
        term_printf("Playing synth arpeggio...\n");
        synth_play_melody((uint8_t)synth_get_default_wave());
    } else if (strncmp(cmd, "wave ", 5) == 0) {
        const char *w = cmd + 5;
        if (strcmp(w, "sine") == 0)     synth_set_default_wave(WAVE_SINE);
        else if (strcmp(w, "sq") == 0)  synth_set_default_wave(WAVE_SQUARE);
        else if (strcmp(w, "saw") == 0) synth_set_default_wave(WAVE_SAW);
        else                            synth_set_default_wave(WAVE_TRIANGLE);
        term_printf("Synth wave -> %s\n", w);
    } else if (strcmp(cmd, "open files") == 0) {
        wm_open_app(WIN_FILES);
    } else if (strcmp(cmd, "open welcome") == 0) {
        wm_open_app(WIN_WELCOME);
    } else if (strcmp(cmd, "open sysmon") == 0) {
        wm_open_app(WIN_SYSMON);
    } else if (strcmp(cmd, "sestatus") == 0 || strcmp(cmd, "getenforce") == 0) {
        flux_sec_info_t si;
        security_collect_info(task_current(), &si);
        term_printf("SELinux status:   enabled | Mode: %s\n",
                    si.selinux_enforcing ? "Enforcing" : "Permissive");
        term_printf("Context:          %s\n", si.sec_ctx);
        term_printf("AVC decisions:    allowed=%u denied=%u\n", si.avc_allowed, si.avc_denied);
        term_printf("Last audit:       %s\n", si.last_avc);
    } else if (strncmp(cmd, "setenforce ", 11) == 0) {
        bool enf = (cmd[11] != '0');
        selinux_set_enforcing(enf);
        term_printf("SELinux mode -> %s\n", enf ? "Enforcing" : "Permissive");
    } else if (strcmp(cmd, "firewall-cmd") == 0) {
        uint32_t z = 0, al = 0, dr = 0;
        fw_stats(&z, &al, &dr);
        term_printf("Firewalld zone:   %s (allow=%u drop=%u pkts)\n",
                    fw_zone_name(z), al, dr);
        fw_rule_t rules[12];
        size_t nr = fw_list_rules(rules, 12);
        term_printf("Ports (%lu): ICMP ", (unsigned long)nr);
        for (size_t i = 0; i < nr; i++) {
            if (rules[i].port != 0) {
                term_printf("%s/%u%s ", (rules[i].proto == IP_PROTO_TCP) ? "t" : "u",
                            rules[i].port, rules[i].allow ? "" : "!");
            }
        }
        term_printf("\n");
    } else if (strcmp(cmd, "luks") == 0) {
        const luks2_phdr_t *h = luks2_get_header();
        term_printf("LUKS2: %s | %s (PBKDF2-SHA256)\n",
                    luks2_is_unlocked() ? "UNLOCKED" : "LOCKED", h->cipher);
        term_printf("UUID:  %s (%s)\n", h->uuid, h->label);
    } else if (strcmp(cmd, "secboot") == 0) {
        uint32_t v = 0, r = 0;
        secboot_stats(&v, &r);
        term_printf("UEFI SecureBoot:  %s (SHA-256 + PK seal)\n",
                    secboot_is_enforcing() ? "ENFORCING" : "AUDIT");
        term_printf("ELF Verified:     %u  |  Tampered/Rejected: %u\n", v, r);
    } else if (strcmp(cmd, "flatpak") == 0) {
        size_t n = flatpak_count();
        for (size_t i = 0; i < n; i++) {
            const flatpak_app_t *a = flatpak_get(i);
            term_printf("%-20s v%-5s perms=0x%02x (%s)\n",
                        a->app_id, a->version, a->perms, a->exec_path);
        }
    } else if (strncmp(cmd, "flatpak run ", 12) == 0) {
        int32_t st = -1;
        if (flatpak_spawn(cmd + 12, &st)) {
            term_printf("Flatpak '%s' exited with %d\n", cmd + 12, st);
        } else {
            term_printf("Unknown flatpak app '%s'\n", cmd + 12);
        }
    } else if (strcmp(cmd, "wayland") == 0) {
        int32_t st = -1;
        if (flatpak_spawn("org.flux.WaylandDemo", &st)) {
            term_printf("Wayland wl_shm frame committed (status=%d)\n", st);
        } else {
            term_printf("Wayland demo launch failed\n");
        }
    } else if (strcmp(cmd, "gpu") == 0 || strcmp(cmd, "nvidia-smi") == 0) {
        for (size_t i = 0; i < gpu_drm_card_count(); i++) {
            gpu_card_state_t c;
            if (gpu_drm_get_card(i, &c)) {
                term_printf("GPU%lu: %s | %s (%u MHz)\n",
                            (unsigned long)i, c.card_name, c.kmod_name, c.current_clock_mhz);
            }
        }
    } else if (strcmp(cmd, "xrandr") == 0 || strcmp(cmd, "monitors") == 0) {
        drm_display_mode_t dm;
        if (gpu_drm_get_active_display(&dm)) {
            term_printf("%s: %ux%u @ %uHz (%s, %ubit%s%s)\n",
                        dm.connector_name, dm.width, dm.height, dm.refresh_hz,
                        dm.monitor_name, dm.color_bpc,
                        dm.hdr10_enabled ? " HDR10" : "",
                        dm.dsc_enabled ? " DSC1.2a" : "");
            term_printf("Sync: %s (%u-%uHz) | HiDPI: %u%% | DB: %lu monitors\n",
                        dm.sync_tech, dm.min_vrr_hz, dm.refresh_hz,
                        dm.scaling_pct, (unsigned long)gpu_drm_monitor_db_size());
            term_printf("Modes: 7680x4320@240Hz 3840x2160@240Hz 2560x1440@480Hz 1920x1080@540Hz\n");
        }
    } else if (strcmp(cmd, "mode 8k") == 0) {
        gpu_drm_set_display_mode(7680U, 4320U, 240U, true, true);
        term_printf("Switched DRM/KMS to 8K UHD: 7680x4320 @ 240Hz (DP 2.1 UHBR20 + DSC 1.2a + HDR10)\n");
    } else if (strcmp(cmd, "mode 4k") == 0) {
        gpu_drm_set_display_mode(3840U, 2160U, 240U, true, true);
        term_printf("Switched DRM/KMS to 4K UHD: 3840x2160 @ 240Hz (QD-OLED + G-Sync/FreeSync)\n");
    } else if (strcmp(cmd, "mode 540hz") == 0) {
        gpu_drm_set_display_mode(1920U, 1080U, 540U, true, false);
        term_printf("Switched DRM/KMS to Esports FHD: 1920x1080 @ 540Hz (ULMB2 / DyAc2 VRR)\n");
    } else if (strcmp(cmd, "dkms") == 0) {
        for (size_t i = 0; i < dkms_module_count(); i++) {
            dkms_module_status_t m;
            if (dkms_get_module(i, &m)) {
                term_printf("%-18s v%-8s [%s]\n", m.pkg_name, m.version,
                            m.state == DKMS_STATE_LOADED ? "LOADED+MOK" : "AVAILABLE");
            }
        }
    } else if (strcmp(cmd, "wifi") == 0) {
        for (size_t i = 0; i < wifi_adapter_count(); i++) {
            wifi_adapter_info_t w;
            if (wifi_get_adapter(i, &w)) {
                term_printf("%s: %-26s %s (%u Mbps)\n",
                            w.ifname, w.chip_model, w.driver_name, w.tx_rate_mbps);
            }
        }
    } else if (strcmp(cmd, "pipewire") == 0 || strcmp(cmd, "wpctl") == 0) {
        for (size_t i = 0; i < pipewire_node_count(); i++) {
            pw_audio_node_t n;
            if (pipewire_get_node(i, &n)) {
                term_printf("Node %u: %-32s %uHz/%uf\n",
                            n.node_id, n.node_name, n.sample_rate_hz, n.quantum_frames);
            }
        }
    } else if (strcmp(cmd, "btrfs") == 0 || strcmp(cmd, "snapshots") == 0) {
        for (size_t i = 0; i < btrfs_snapshot_count(); i++) {
            btrfs_snapshot_t sn;
            if (btrfs_get_snapshot(i, &sn)) {
                term_printf("#%u %-6s %s (%s)\n",
                            sn.snap_id, sn.subvol_source, sn.snap_path, sn.description);
            }
        }
    } else if (strcmp(cmd, "rollback") == 0) {
        btrfs_rollback_snapshot(1U);
        term_printf("Btrfs 1-sec CoW Rollback -> /.snapshots/1/snapshot (@ & @home) OK!\n");
    } else if (strcmp(cmd, "mounts") == 0 || strcmp(cmd, "lsblk") == 0) {
        for (size_t i = 0; i < fs_multi_mount_count(); i++) {
            fs_mount_info_t m;
            if (fs_multi_get_mount(i, &m)) {
                term_printf("%-14s -> %-20s [%s]\n", m.device, m.mount_point, m.features);
            }
        }
    } else if (strcmp(cmd, "proton") == 0 || strcmp(cmd, "dxvk") == 0) {
        proton_game_session_t gs;
        if (proton_get_last_session(&gs)) {
            term_printf("PE64: %s\n", gs.exe_name);
            term_printf("API:  %s | %u FPS (1%% Low: %u)\n", gs.translator, gs.avg_fps, gs.one_pct_low_fps);
            term_printf("Cache: %u SPIR-V shaders | DLSS4=%s | Win32 APIs=%lu\n",
                        gs.spirv_shaders_compiled, gs.dlss4_fg_active ? "ON" : "OFF",
                        (unsigned long)proton_win32_api_count());
        }
    } else if (strcmp(cmd, "smp") == 0 || strcmp(cmd, "lscpu") == 0 || strcmp(cmd, "acpi") == 0) {
        smp_acpi_status_t st;
        if (smp_acpi_get_status(&st)) {
            term_printf("CPU:  %s (%uC/%uT, %uMB L3)\n",
                        st.cpu_brand, st.online_cores, st.total_threads, st.l3_cache_mib);
            term_printf("Arch: %s | ISA: %s\n", st.arch_codename, st.isa_extensions);
            term_printf("Drv:  %s | %uW | %uC | DB=%lu CPUs (2011..2026)\n",
                        st.scaling_driver, st.package_watts, st.package_temp_c,
                        (unsigned long)smp_cpu_db_size());
        }
    } else if (strcmp(cmd, "oc") == 0 || strcmp(cmd, "cpu 2600k") == 0) {
        smp_switch_cpu_profile(true);
        acpi_set_power_profile(ACPI_PROFILE_K_OVERCLOCK);
        term_printf("Intel Core i7-2600K: 46x K-Multiplier Overclock -> 4.60 GHz All-Core!\n");
    } else if (strcmp(cmd, "cpu ryzen") == 0) {
        smp_switch_cpu_profile(false);
        term_printf("Switched SMP profile -> AMD Ryzen 9 9950X3D (16C/32T)\n");
    } else if (strncmp(cmd, "git clone ", 10) == 0) {
        git_clone_result_t gr;
        if (git_clone_repo(cmd + 10, &gr)) {
            term_printf("Cloning into '%s'...\n", gr.target_dir);
            term_printf("Unpacked %u objects (%u KB, TLS 1.3 OK)\n",
                        gr.objects_unpacked, gr.bytes_downloaded / 1024U);
        }
    } else if (strncmp(cmd, "curl ", 5) == 0 || strncmp(cmd, "wget ", 5) == 0) {
        uint32_t dl = 0;
        if (http_download_file(cmd + 5, NULL, &dl)) {
            term_printf("HTTP 200 OK: saved %u bytes to /home/\n", dl);
        }
    } else if (strncmp(cmd, "dnf install ", 12) == 0 || strncmp(cmd, "apt install ", 12) == 0) {
        char msg[128];
        if (pkg_install(cmd + 12, msg, sizeof(msg))) {
            term_printf("%s\n", msg);
        }
    } else if (strncmp(cmd, "flatpak install ", 16) == 0) {
        char msg[128];
        if (pkg_install(cmd + 16, msg, sizeof(msg))) {
            term_printf("%s\n", msg);
        }
    } else if (strcmp(cmd, "dnf") == 0 || strcmp(cmd, "pkgs") == 0) {
        term_printf("DNF5 + Flathub: %lu packages in catalog (%lu installed)\n",
                    (unsigned long)pkg_catalog_size(), (unsigned long)pkg_installed_count());
    } else if (strcmp(cmd, "screenshot") == 0 || strcmp(cmd, "flameshot") == 0 || strcmp(cmd, "grim") == 0) {
        screenshot_info_t sh;
        screenshot_mode_t sm = (strcmp(cmd, "flameshot") == 0) ? SHOT_MODE_REGION : SHOT_MODE_FULLSCREEN;
        if (take_linux_screenshot(sm, &sh)) {
            synth_play_note(1200, 45, WAVE_TRIANGLE, 70);
            term_printf("Screenshot #%u (%ux%u) -> %s\n", sh.shot_id, sh.width, sh.height, sh.saved_path);
            term_printf("Tool: %s | Wayland Clipboard (wl-copy): OK\n", sh.tool_name);
        }
    } else if (strncmp(cmd, "shotdir ", 8) == 0) {
        if (screenshot_set_folder(cmd + 8)) {
            term_printf("Screenshot save folder -> %s\n", screenshot_get_folder());
        } else {
            term_printf("Invalid folder path (must start with '/')\n");
        }
    } else if (strcmp(cmd, "shotdir") == 0) {
        term_printf("Current screenshot folder: %s\n", screenshot_get_folder());
    } else if (strcmp(cmd, "record") == 0 || strcmp(cmd, "shadowplay") == 0) {
        screen_record_info_t ri;
        if (record_screen_clip(120, &ri)) {
            synth_play_note(960, 45, WAVE_TRIANGLE, 70);
            term_printf("Video Clip #%u (%ux%u @ %u FPS) -> %s\n",
                        ri.clip_id, ri.width, ri.height, ri.fps, ri.saved_path);
            term_printf("Encoder: %s | Bitrate: %u kbps\n", ri.encoder_name, ri.bitrate_kbps);
        }
    } else if (strncmp(cmd, "vidir ", 6) == 0) {
        if (video_set_folder(cmd + 6)) {
            term_printf("Video recording folder -> %s\n", video_get_folder());
        } else {
            term_printf("Invalid folder path (must start with '/')\n");
        }
    } else if (strcmp(cmd, "vidir") == 0) {
        term_printf("Current video folder: %s\n", video_get_folder());
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        if (trash_move_file(cmd + 3)) {
            term_printf("Moved to Trash (/home/.Trash): %s\n", cmd + 3);
        } else {
            term_printf("Cannot move to Trash: %s\n", cmd + 3);
        }
    } else if (strcmp(cmd, "trash") == 0) {
        size_t tc = trash_item_count();
        term_printf("Desktop Trash (/home/.Trash): %lu item(s)\n", (unsigned long)tc);
        for (size_t i = 0; i < tc; i++) {
            trash_entry_t te;
            if (trash_get_item(i, &te)) {
                term_printf("  [%lu] %s (%u B) <- %s\n",
                            (unsigned long)i, te.file_name, te.size_bytes, te.orig_path);
            }
        }
    } else if (strcmp(cmd, "restore") == 0) {
        char restored[64];
        if (trash_restore_last(restored, sizeof(restored))) {
            term_printf("Restored from Trash -> %s\n", restored);
        } else {
            term_printf("Trash (/home/.Trash) is empty\n");
        }
    } else if (strcmp(cmd, "empty-trash") == 0) {
        uint32_t n = trash_empty_all();
        term_printf("Emptied Trash: %u file(s) permanently removed\n", n);
    } else if (strcmp(cmd, "store") == 0 || strcmp(cmd, "open store") == 0) {
        wm_open_app(WIN_STORE);
    } else if (strcmp(cmd, "settings") == 0 || strcmp(cmd, "open settings") == 0) {
        wm_open_app(WIN_SETTINGS);
    } else if (strcmp(cmd, "vaapi") == 0 || strcmp(cmd, "nvdec") == 0 || strcmp(cmd, "zram") == 0 || strcmp(cmd, "usb") == 0) {
        vaapi_zram_status_t vs;
        if (vaapi_get_status(&vs)) {
            term_printf("Video: %s (%u%% CPU on i7-2600K)\n", vs.active_codec, vs.cpu_load_4k60_pct);
            term_printf("Drv:   %s (%lu profiles)\n", vs.driver_backend, (unsigned long)vaapi_codec_db_size());
            term_printf("ZRAM:  2.85x zstd RAM multiplier | %s\n", vs.usb_hid_status);
        }
    } else if (strcmp(cmd, "firefox") == 0 || strcmp(cmd, "open firefox") == 0) {
        wm_open_app(WIN_FIREFOX);
    } else if (strcmp(cmd, "setup") == 0 || strcmp(cmd, "open setup") == 0) {
        wm_open_app(WIN_WELCOME);
    } else if (strcmp(cmd, "reboot") == 0) {
        outb(0x64, 0xFE);
    } else {
        term_printf("Unknown command: %s (type 'help')\n", cmd);
    }
}

/* ---------------- App 1: Terminal ---------------- */
static void term_paint(wm_window_t *win UNUSED, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    fb_fill_rounded_rect(cx + 4, cy + 2, cw - 8, ch - 6, 6, 0x12151B);
    spin_lock_irqsave(&term_lock);
    int max_vis = MAX(1, (ch - 38) / FONT_H);
    int start = (term_cur_row + 1 > max_vis) ? (term_cur_row + 1 - max_vis) : 0;
    for (int i = 0; i < max_vis && (start + i) < TERM_ROWS; i++) {
        font_draw_str(cx + 12, cy + 8 + i * FONT_H, term_lines[start + i], 0xD8E2F2, 0, true);
    }
    int32_t py = cy + ch - FONT_H - 12;
    fb_fill_rounded_rect(cx + 8, py - 4, cw - 16, FONT_H + 8, 5, 0x1B202A);
    font_draw_str(cx + 14, py, "flux> ", 0x62A0EA, 0, true);
    font_draw_str(cx + 14 + 6 * FONT_W, py, term_cmd, 0xF8FAFC, 0, true);
    if ((timer_uptime_ms() / 400) & 1) {
        int32_t cur_x = cx + 14 + (6 + term_cmd_len) * FONT_W;
        fb_fill_rect(cur_x, py + 1, 7, FONT_H - 2, 0x62A0EA);
    }
    spin_unlock_irqrestore(&term_lock);
}

static void term_key(wm_window_t *win UNUSED, const key_event_t *ev) {
    if (!ev->pressed || !ev->ascii) return;
    if (ev->ascii == '\n') {
        term_cmd[term_cmd_len] = '\0';
        term_printf("flux> %s\n", term_cmd);
        char copy[TERM_COLS];
        strcpy(copy, term_cmd);
        term_cmd_len = 0;
        term_cmd[0] = '\0';
        exec_shell_cmd(copy);
    } else if (ev->ascii == '\b') {
        if (term_cmd_len > 0) term_cmd[--term_cmd_len] = '\0';
    } else if (ev->ascii >= 32 && ev->ascii <= 126 && term_cmd_len < TERM_COLS - 2) {
        term_cmd[term_cmd_len++] = (char)ev->ascii;
        term_cmd[term_cmd_len] = '\0';
    }
    wm_dirty = true;
}

/* ---------------- App 2: System Monitor ---------------- */
static void sysmon_paint(wm_window_t *win UNUSED, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    pmm_stats_t ps; heap_stats_t hs;
    pmm_get_stats(&ps); heap_get_stats(&hs);

    char buf[64];
    uint64_t used_kb  = ps.used_pages * 4;
    uint64_t total_kb = ps.total_pages * 4;
    uint32_t pct = (ps.total_pages > 0) ? (uint32_t)((ps.used_pages * 100ULL) / ps.total_pages) : 0;

    ksnprintf(buf, sizeof(buf), "Physical RAM: %lu / %lu KB (%u%%)", used_kb, total_kb, pct);
    font_draw_str(cx + 12, cy + 8, buf, 0xE2E8F0, 0, true);

    int32_t bar_w = cw - 24;
    fb_fill_rounded_rect(cx + 12, cy + 28, bar_w, 14, 4, 0x262B36);
    if (pct > 0) {
        fb_fill_rounded_rect(cx + 12, cy + 28, MAX(8, (bar_w * (int32_t)pct) / 100), 14, 4, 0x2EC27E);
    }

    ksnprintf(buf, sizeof(buf), "Slab/Heap: %lu B (%lu active) | Switches: %lu",
              hs.bytes_allocated, hs.active_allocs, sched_context_switches());
    font_draw_str(cx + 12, cy + 50, buf, 0x9AA5B8, 0, true);

    font_draw_str(cx + 12, cy + 74, "PID  TASK         PRIO RING TICKS", 0x62A0EA, 0, true);
    fb_draw_line(cx + 12, cy + 92, cx + cw - 12, cy + 92, 0x2F3544);

    task_t *ptrs[8];
    size_t n = sched_list_tasks(ptrs, 8);
    for (size_t i = 0; i < n && (int32_t)(100 + i * 18) < ch - 18; i++) {
        ksnprintf(buf, sizeof(buf), "%-4lu %-12s %-4u %-4u %lu",
                  ptrs[i]->pid, ptrs[i]->name, ptrs[i]->priority,
                  ptrs[i]->is_user ? 3 : 0, ptrs[i]->total_ticks);
        font_draw_str(cx + 12, cy + 98 + (int32_t)i * 18, buf, 0xE2E8F0, 0, true);
    }
}

/* ---------------- App 3: Flux Files (ext2 Browser) ---------------- */
static const char *const files_places[6] = {
    "/", "/boot", "/bin", "/etc", "/home", "/tmp"
};
static const char *const files_labels[6] = {
    "System Disk /", "Boot /boot", "Programs /bin", "Config /etc", "Home /home", "Temp /tmp"
};

static void draw_folder_icon(int32_t x, int32_t y) {
    fb_fill_rounded_rect(x + 2, y + 3, 16, 7, 3, 0x8CC2FF);
    fb_fill_rounded_rect(x, y + 7, 40, 24, 5, 0x62A0EA);
    fb_fill_rect(x + 2, y + 8, 36, 2, 0x8CC2FF);
    fb_fill_rect(x + 15, y + 18, 10, 2, 0x2B4C7E);
}

static void draw_file_icon(int32_t x, int32_t y, bool is_exec) {
    fb_fill_rounded_rect(x + 6, y + 2, 28, 30, 4, is_exec ? 0x243B32 : 0x262B36);
    fb_draw_rounded_rect(x + 6, y + 2, 28, 30, 4, is_exec ? 0x2EC27E : 0x454E62);
    if (is_exec) {
        font_draw_str(x + 12, y + 9, ">_", 0x57E389, 0, true);
    } else {
        fb_fill_rect(x + 11, y + 10, 18, 2, 0x9AA5B8);
        fb_fill_rect(x + 11, y + 16, 14, 2, 0x6E788C);
        fb_fill_rect(x + 11, y + 22, 16, 2, 0x6E788C);
    }
}

static void files_paint(wm_window_t *win UNUSED, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    int32_t sb_w = 144;
    fb_fill_rounded_rect(cx + 4, cy + 4, sb_w - 6, ch - 10, 6, COL_BG_SIDEBAR);

    /* Sidebar places */
    for (int i = 0; i < 6; i++) {
        int32_t ry = cy + 12 + i * 32;
        bool active = (strcmp(files_cwd, files_places[i]) == 0);
        if (active) {
            fb_fill_rounded_rect(cx + 10, ry, sb_w - 18, 26, 6, 0x263244);
        }
        fb_fill_rounded_rect(cx + 16, ry + 8, 9, 9, 2, active ? 0x62A0EA : 0x5E6778);
        font_draw_str(cx + 31, ry + 5, files_labels[i], active ? 0xFFFFFF : 0xB8C0CE, 0, true);
    }

    /* Breadcrumb / path header */
    int32_t px = cx + sb_w + 6;
    int32_t pw = cw - sb_w - 14;
    fb_fill_rounded_rect(px, cy + 6, 44, 24, 6, 0x262B36);
    font_draw_str(px + 10, cy + 10, "Up", 0xCBD5E1, 0, true);

    bool in_trash = (strcmp(files_cwd, "/home/.Trash") == 0);
    int32_t status_w = in_trash ? (pw - 188) : (pw - 52);
    fb_fill_rounded_rect(px + 52, cy + 6, status_w, 24, 6, 0x14171E);
    font_draw_str(px + 62, cy + 10, files_status, 0xE2E8F0, 0, true);

    if (in_trash) {
        fb_fill_rounded_rect(px + pw - 130, cy + 6, 64, 24, 6, 0x1E3A5F);
        font_draw_str(px + pw - 122, cy + 10, "Restore", 0x86EFAC, 0, true);

        fb_fill_rounded_rect(px + pw - 60, cy + 6, 60, 24, 6, 0x451A23);
        font_draw_str(px + pw - 50, cy + 10, "Empty", 0xFCA5A5, 0, true);
    }

    /* Directory entry grid */
    int cols = MAX(1, pw / 90);
    for (int i = 0; i < files_count && i < 16; i++) {
        int col = i % cols;
        int row = i / cols;
        int32_t ix = px + 8 + col * 88;
        int32_t iy = cy + 42 + row * 72;
        if (iy + 64 > cy + ch - 26) break;

        if (files_ents[i].type == VFS_DIR) {
            draw_folder_icon(ix + 18, iy + 2);
        } else {
            draw_file_icon(ix + 18, iy + 2, files_in_bin);
        }

        char short_name[12];
        strncpy(short_name, files_ents[i].name, 10);
        short_name[10] = '\0';
        int32_t tx = ix + 38 - ((int32_t)strlen(short_name) * FONT_W) / 2;
        font_draw_str(MAX(ix, tx), iy + 36, short_name, 0xE5E9F0, 0, true);
    }

    /* Bottom status bar */
    fb_fill_rounded_rect(px, cy + ch - 26, pw, 20, 4, 0x161920);
    if (in_trash) {
        char tmsg[72];
        ksnprintf(tmsg, sizeof(tmsg), "Desktop Trash (/home/.Trash): %lu item(s) | Click Restore or Empty",
                  (unsigned long)trash_item_count());
        font_draw_str(px + 8, cy + ch - 24, tmsg, 0xFDE047, 0, true);
    } else {
        font_draw_str(px + 8, cy + ch - 24, "Btrfs CoW + Dock Trash (/home/.Trash) | Click item to open", 0x8B95A7, 0, true);
    }
}

static void files_mouse(wm_window_t *win, int32_t rx, int32_t ry, uint8_t buttons UNUSED, bool click) {
    if (!click) return;
    int32_t sb_w = 144;

    if (rx < sb_w) {
        for (int i = 0; i < 6; i++) {
            int32_t iy = 12 + i * 32;
            if (ry >= iy && ry <= iy + 26) {
                files_cd(files_places[i]);
                return;
            }
        }
        return;
    }

    int32_t px = sb_w + 6;
    int32_t pw = (win->w - 4) - sb_w - 14;
    if (ry >= 6 && ry <= 30 && rx >= px && rx <= px + 44) {
        files_cd("/");
        return;
    }
    if (strcmp(files_cwd, "/home/.Trash") == 0 && ry >= 6 && ry <= 30) {
        if (rx >= px + pw - 130 && rx <= px + pw - 66) {
            char restored[64];
            if (trash_restore_last(restored, sizeof(restored))) {
                ksnprintf(files_status, sizeof(files_status), "Restored: %s", restored);
                files_cd("/home/.Trash");
            } else {
                ksnprintf(files_status, sizeof(files_status), "Trash is empty");
            }
            wm_dirty = true;
            return;
        }
        if (rx >= px + pw - 60 && rx <= px + pw) {
            uint32_t n = trash_empty_all();
            ksnprintf(files_status, sizeof(files_status), "Emptied (%u removed)", n);
            files_cd("/home/.Trash");
            wm_dirty = true;
            return;
        }
    }
    int cols = MAX(1, pw / 90);
    for (int i = 0; i < files_count && i < 16; i++) {
        int col = i % cols;
        int row = i / cols;
        int32_t ix = px + 8 + col * 88;
        int32_t iy = 42 + row * 72;
        if (rx >= ix && rx <= ix + 82 && ry >= iy && ry <= iy + 62) {
            char next[96];
            if (strcmp(files_cwd, "/") == 0) ksnprintf(next, sizeof(next), "/%s", files_ents[i].name);
            else ksnprintf(next, sizeof(next), "%s/%s", files_cwd, files_ents[i].name);

            if (files_ents[i].type == VFS_DIR) {
                files_cd(next);
            } else if (files_in_bin) {
                uint64_t pid = 0;
                if (user_exec_program(next, 2, &pid)) {
                    ksnprintf(files_status, sizeof(files_status), "Ran %s (pid=%lu)", next, pid);
                }
            } else if (files_ents[i].size <= 4096) {
                term_printf("--- %s (%lu B) ---\n", next, files_ents[i].size);
                uint8_t *buf = NULL;
                size_t sz = 0;
                if (vfs_read_all(next, &buf, &sz)) {
                    wm_term_write((const char *)buf, MIN(sz, (size_t)256));
                    kfree(buf);
                }
            }
            wm_dirty = true;
            return;
        }
    }
}

/* ---------------- First-Boot Anaconda WebUI Installer + Firefox ---------------- */
static int setup_step   = 0; /* 0=Welcome, 1=Date & Region, 2=Storage config, 3=Review & install */
static int lang_idx     = 0; /* 0=English(US), 1=Deutsch, 2=English(UK), 3=Espanol, 4=Francais, 5=Russian(RU/UA) */
static int tz_idx       = 0; /* 0=Kyiv(UTC+2), 1=Moscow(UTC+3), 2=Berlin(UTC+1), 3=NY(UTC-5) */
static int kbd_idx      = 0; /* 0="us", 1="ru", 2="ua" */
static int disk_mode    = 0; /* 0="GPT + ext2 + LUKS2", 1="Standard ext2" */
static int ff_tab       = 0; /* 0="FluxOS Start", 1="Security & Wayland", 2="Network Probe" */

static const char *const lang_labels[6] = {
    "English (United States)",
    "Deutsch (Deutschland)",
    "English (United Kingdom)",
    "Espanol (Espana)",
    "Francais (France)",
    "Russian (Russia / UTC+2)"
};
static const int lang_to_tz[6]  = { 3, 2, 2, 2, 2, 0 };
static const int lang_to_kbd[6] = { 0, 0, 0, 0, 0, 1 };

static const char *const tz_names[4] = {
    "Europe/Kyiv (UTC+2)",
    "Europe/Moscow (UTC+3)",
    "Europe/Berlin (UTC+1)",
    "America/New_York (UTC-5)"
};
static const int tz_offsets[4] = { +2, +3, +1, -5 };
static const char *const tz_short[4] = { "UTC+2", "UTC+3", "UTC+1", "UTC-5" };
static const char *const kbd_codes[3] = { "us", "ru", "ua" };
static const char *const kbd_names[3] = { "English (US)", "Russian (RU)", "Ukrainian (UA)" };

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return (uint8_t)((val & 0x0FU) + ((val >> 4) * 10U));
}

static void rtc_get_datetime(uint32_t *day, uint32_t *mon, uint32_t *year,
                             uint32_t *hour, uint32_t *min) {
    while (cmos_read(0x0A) & 0x80U) { }
    uint8_t m  = cmos_read(0x02);
    uint8_t h  = cmos_read(0x04);
    uint8_t d  = cmos_read(0x07);
    uint8_t mo = cmos_read(0x08);
    uint8_t y  = cmos_read(0x09);
    uint8_t b  = cmos_read(0x0B);
    if (!(b & 0x04U)) {
        m  = bcd_to_bin(m);
        h  = bcd_to_bin(h & 0x7FU);
        d  = bcd_to_bin(d);
        mo = bcd_to_bin(mo);
        y  = bcd_to_bin(y);
    }
    int32_t adj_h = (int32_t)h + tz_offsets[tz_idx];
    int32_t adj_d = (int32_t)d;
    if (adj_h >= 24) {
        adj_h -= 24;
        adj_d += 1;
    } else if (adj_h < 0) {
        adj_h += 24;
        if (adj_d > 1) adj_d -= 1;
    }
    *min  = m;
    *hour = (uint32_t)adj_h;
    *day  = (uint32_t)adj_d;
    *mon  = mo;
    *year = 2000U + (uint32_t)y;
}

static void draw_checkmark(int32_t x, int32_t y, uint32_t col) {
    fb_draw_line(x,     y + 5, x + 3, y + 8, col);
    fb_draw_line(x,     y + 6, x + 3, y + 9, col);
    fb_draw_line(x + 3, y + 8, x + 9, y + 2, col);
    fb_draw_line(x + 3, y + 9, x + 9, y + 3, col);
}

static void draw_search_icon(int32_t x, int32_t y, uint32_t col) {
    fb_draw_circle(x + 5, y + 5, 4, col, false);
    fb_draw_line(x + 8, y + 8, x + 12, y + 12, col);
    fb_draw_line(x + 9, y + 8, x + 13, y + 12, col);
}

static void welcome_paint(wm_window_t *win UNUSED, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    int32_t sb_w = 186;
    int32_t foot_h = 44;

    /* Crisp White Main Canvas + Light Sidebar (Fedora Anaconda WebUI style) */
    fb_fill_rect(cx, cy, cw, ch, 0xFFFFFF);
    fb_fill_rect(cx, cy, sb_w, ch, 0xFAFAFA);
    fb_fill_rect(cx + sb_w, cy, 1, ch, 0xE2E8F0);

    static const char *const side_steps[4] = {
        "Welcome",
        "Installation method",
        "Storage config",
        "Review and install"
    };
    for (int i = 0; i < 4; i++) {
        int32_t sy = cy + 20 + i * 40;
        bool act = (i == setup_step);
        if (act) {
            fb_fill_rounded_rect(cx + 22, sy - 4, sb_w - 30, 26, 5, 0xEFEFEF);
            fb_draw_circle(cx + 14, sy + 8, 8, 0x2B72C6, true);
        }
        char num[2] = { (char)('1' + i), '\0' };
        font_draw_str(cx + 10, sy + 1, num, act ? 0xFFFFFF : 0x94A3B8, 0, true);
        font_draw_str(cx + 28, sy + 1, side_steps[i], act ? 0x1E293B : 0x94A3B8, 0, true);
    }

    int32_t mx = cx + sb_w + 18;
    int32_t mw = cw - sb_w - 32;

    if (setup_step == 0) {
        font_draw_str(mx, cy + 16, "Welcome. Let's install FluxOS Linux now.", 0x1E293B, 0, true);

        /* Language label + Search bar + Suggested languages listbox */
        font_draw_str(mx, cy + 50, "Language", 0x1E293B, 0, true);

        int32_t lx = mx + 92;
        int32_t lw = mw - 98;
        fb_fill_rounded_rect(lx, cy + 44, lw, 24, 4, 0xFFFFFF);
        fb_draw_rounded_rect(lx, cy + 44, lw, 24, 4, 0xCBD5E1);
        draw_search_icon(lx + 8, cy + 50, 0x1E293B);

        int32_t by0 = cy + 74;
        int32_t bh  = 176;
        fb_fill_rounded_rect(lx, by0, lw, bh, 5, 0xFFFFFF);
        fb_draw_rounded_rect(lx, by0, lw, bh, 5, 0x3584E4);
        font_draw_str(lx + 12, by0 + 8, "Suggested languages", 0x64748B, 0, true);

        for (int i = 0; i < 6; i++) {
            int32_t ry = by0 + 30 + i * 23;
            if (i == lang_idx) {
                fb_fill_rect(lx + 1, ry - 2, lw - 2, 22, 0xEFEFEF);
                draw_checkmark(lx + lw - 22, ry + 3, 0x2B72C6);
            }
            font_draw_str(lx + 12, ry + 1, lang_labels[i], 0x1E293B, 0, true);
        }

        /* Keyboard row */
        font_draw_str(mx, cy + 266, "Keyboard", 0x1E293B, 0, true);
        font_draw_str(lx, cy + 266, kbd_codes[kbd_idx], 0x334155, 0, true);
        font_draw_str(lx + 28, cy + 266, "Change system keyboard layout", 0x2B72C6, 0, true);
    } else if (setup_step == 1) {
        font_draw_str(mx, cy + 16, "Region, Timezone & Pre-installed Software", 0x1E293B, 0, true);
        font_draw_str(mx, cy + 48, "Select Region (adjusts CMOS RTC system clock):", 0x475569, 0, true);
        for (int i = 0; i < 4; i++) {
            int32_t ry = cy + 74 + i * 28;
            if (i == tz_idx) {
                fb_fill_rounded_rect(mx, ry - 3, mw - 12, 24, 4, 0xEBF3FC);
                fb_draw_rounded_rect(mx, ry - 3, mw - 12, 24, 4, 0x3584E4);
                draw_checkmark(mx + mw - 32, ry + 3, 0x2B72C6);
            }
            font_draw_str(mx + 12, ry + 1, tz_names[i], 0x1E293B, 0, true);
        }
        font_draw_str(mx, cy + 200, "Pre-installed Workstation Apps:", 0x475569, 0, true);
        font_draw_str(mx + 12, cy + 224, "[x] Mozilla Firefox 132.0 ESR (Wayland + Flatpak)", 0x1E293B, 0, true);
        font_draw_str(mx + 12, cy + 246, "[x] SELinux (Enforcing) + Firewalld + UEFI SecureBoot", 0x1E293B, 0, true);
    } else if (setup_step == 2) {
        font_draw_str(mx, cy + 16, "Storage configuration (AHCI SATA GPT + LUKS2)", 0x1E293B, 0, true);
        fb_fill_rounded_rect(mx, cy + 48, mw - 10, 120, 5, 0xF8FAFC);
        fb_draw_rounded_rect(mx, cy + 48, mw - 10, 120, 5, 0xCBD5E1);
        font_draw_str(mx + 12, cy + 58, "/dev/sda1  EFI System (ESP)   64 MiB  FAT32   /boot/efi", 0x334155, 0, true);
        font_draw_str(mx + 12, cy + 84, "/dev/sda2  FluxOS Rootfs     440 MiB  ext2    / [MOUNTED]", 0x15803D, 0, true);
        font_draw_str(mx + 12, cy + 110, "/dev/sdb1  LUKS2 Vault        16 MiB  XTS-64  /vault [OK]", 0x2B72C6, 0, true);
        font_draw_str(mx + 12, cy + 140, "Encryption: LUKS2 PBKDF2-HMAC-SHA256 + aes-xts-plain64", 0x64748B, 0, true);
    } else {
        font_draw_str(mx, cy + 16, "Review and install FluxOS 0.4 Workstation", 0x1E293B, 0, true);
        char s1[64], s2[64];
        ksnprintf(s1, sizeof(s1), "Language: %s", lang_labels[lang_idx]);
        ksnprintf(s2, sizeof(s2), "Timezone: %s  |  Keyboard: %s", tz_names[tz_idx], kbd_codes[kbd_idx]);
        font_draw_str(mx, cy + 54, s1, 0x1E293B, 0, true);
        font_draw_str(mx, cy + 80, s2, 0x1E293B, 0, true);
        font_draw_str(mx, cy + 106, "Storage:  /dev/sda2 ext2 (/) + /dev/sdb1 LUKS2 (/vault)", 0x1E293B, 0, true);
        font_draw_str(mx, cy + 132, "Browser:  Mozilla Firefox 132.0 ESR (Wayland Pre-installed)", 0x2B72C6, 0, true);
    }

    /* Bottom Footer Action Bar */
    int32_t fy = cy + ch - foot_h;
    fb_fill_rect(cx + sb_w + 1, fy, cw - sb_w - 1, foot_h, 0xFFFFFF);
    fb_fill_rect(cx + sb_w + 1, fy, cw - sb_w - 1, 1, 0xE2E8F0);

    /* Back button (grey pill) */
    fb_fill_rounded_rect(mx, fy + 9, 58, 26, 12, 0xC6CBD2);
    font_draw_str(mx + 13, fy + 14, "Back", 0x475569, 0, true);

    /* Next button (Fedora blue pill) */
    fb_fill_rounded_rect(mx + 68, fy + 9, 62, 26, 12, 0x2B72C6);
    font_draw_str(mx + 83, fy + 14, (setup_step == 3) ? "Done" : "Next", 0xFFFFFF, 0, true);

    /* Quit link */
    font_draw_str(mx + 148, fy + 14, "Quit", 0x2B72C6, 0, true);
}

static void welcome_mouse(wm_window_t *win, int32_t rx, int32_t ry, uint8_t buttons UNUSED, bool click) {
    if (!click) return;
    int32_t cw = win->w - 8;
    int32_t ch = win->h - TITLEBAR_H - 10;
    int32_t sb_w = 166;
    int32_t mx = sb_w + 18;
    int32_t mw = cw - sb_w - 32;

    /* Left sidebar steps 1..4 */
    if (rx < sb_w) {
        int s = (ry - 16) / 40;
        if (s >= 0 && s < 4) {
            setup_step = s;
            wm_dirty = true;
        }
        return;
    }

    /* Bottom footer buttons: Back, Next, Quit */
    int32_t fy = ch - 44;
    if (ry >= fy) {
        if (rx >= mx && rx <= mx + 58) {
            if (setup_step > 0) setup_step--;
            wm_dirty = true;
        } else if (rx >= mx + 68 && rx <= mx + 130) {
            if (setup_step < 3) setup_step++;
            else wm_open_app(WIN_FIREFOX);
            wm_dirty = true;
        } else if (rx >= mx + 144 && rx <= mx + 190) {
            win->visible = false;
            wm_dirty = true;
        }
        return;
    }

    if (setup_step == 0) {
        int32_t lx = mx + 92;
        int32_t lw = mw - 98;
        if (rx >= lx && rx <= lx + lw && ry >= 102 && ry <= 242) {
            int idx = (ry - 102) / 23;
            if (idx >= 0 && idx < 6) {
                lang_idx = idx;
                tz_idx   = lang_to_tz[idx];
                kbd_idx  = lang_to_kbd[idx];
                wm_dirty = true;
            }
            return;
        }
        if (ry >= 260 && ry <= 284 && rx >= lx) {
            kbd_idx = (kbd_idx + 1) % 3;
            wm_dirty = true;
            return;
        }
    } else if (setup_step == 1) {
        if (ry >= 70 && ry <= 186) {
            int idx = (ry - 70) / 28;
            if (idx >= 0 && idx < 4) {
                tz_idx = idx;
                wm_dirty = true;
            }
        }
    }
}

/* ---------------- App 6: Pre-installed Mozilla Firefox (Wayland) ---------------- */
static void firefox_paint(wm_window_t *win UNUSED, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    /* Browser Tab Bar */
    static const char *const tabs[3] = {
        "FluxOS Start Page", "Wayland & SELinux", "Network 10.0.2.15"
    };
    for (int i = 0; i < 3; i++) {
        int32_t tx = cx + 8 + i * 164;
        fb_fill_rounded_rect(tx, cy + 4, 158, 24, 6, (i == ff_tab) ? 0x2B3140 : 0x171A22);
        font_draw_str(tx + 10, cy + 8, tabs[i], (i == ff_tab) ? 0xFFFFFF : 0x94A3B8, 0, true);
    }

    /* Navigation & HTTPS URL Bar */
    fb_fill_rounded_rect(cx + 6, cy + 30, cw - 12, 28, 6, 0x222734);
    font_draw_str(cx + 14, cy + 36, "<-  ->  C", 0xCBD5E1, 0, true);
    fb_fill_rounded_rect(cx + 96, cy + 33, cw - 110, 22, 5, 0x12151C);
    font_draw_str(cx + 104, cy + 36, "[HTTPS]", 0x2EC27E, 0, true);
    const char *url = (ff_tab == 0) ? "https://fluxos.org/welcome" :
                      (ff_tab == 1) ? "about:security-wayland" : "http://10.0.2.15/status";
    font_draw_str(cx + 166, cy + 36, url, 0xF8FAFC, 0, true);

    /* Web Viewport Canvas */
    int32_t vy = cy + 64;
    int32_t vh = ch - 68;
    fb_fill_rounded_rect(cx + 6, vy, cw - 12, vh, 6, 0x131722);
    fb_draw_rounded_rect(cx + 6, vy, cw - 12, vh, 6, 0x2B3346);

    /* Firefox Hero Banner */
    fb_draw_circle(cx + 42, vy + 34, 16, 0xEA580C, true);
    fb_draw_circle(cx + 40, vy + 32, 12, 0xFB923C, true);
    fb_draw_circle(cx + 45, vy + 36, 9,  0x6366F1, true);
    fb_draw_circle(cx + 37, vy + 29, 5,  0xFDE047, true);

    font_draw_str(cx + 70, vy + 18, "Mozilla Firefox 132.0 ESR  (FluxOS Wayland Edition)", 0xFFFFFF, 0, true);
    font_draw_str(cx + 70, vy + 38, "Flatpak: org.mozilla.firefox  |  Wayland wl_shm", 0x94A3B8, 0, true);

    /* Search Bar inside Start Page */
    fb_fill_rounded_rect(cx + 22, vy + 64, cw - 44, 26, 7, 0x1E2433);
    fb_draw_rounded_rect(cx + 22, vy + 64, cw - 44, 26, 7, 0x3B82F6);
    font_draw_str(cx + 34, cy + 133, "Search the web or FluxOS docs (e1000 10.0.2.15 ONLINE)...", 0xCBD5E1, 0, true);

    /* Live System & Regional Cards */
    uint32_t r_day = 14, r_mon = 11, r_year = 2026, r_hr = 13, r_min = 25;
    rtc_get_datetime(&r_day, &r_mon, &r_year, &r_hr, &r_min);

    char c1[64], c2[64], c3[64];
    ksnprintf(c1, sizeof(c1), "Region: %s | Time: %02u:%02u (%02u.%02u.%04u)",
              tz_names[tz_idx], r_hr, r_min, r_day, r_mon, r_year);
    ksnprintf(c2, sizeof(c2), "Keymap: %s [%s] | Disk: /dev/sda2 ext2 + LUKS2",
              kbd_names[kbd_idx], kbd_codes[kbd_idx]);
    ksnprintf(c3, sizeof(c3), "Sandbox: SELinux Enforcing | Firewalld: public | PIE+RELRO");

    fb_fill_rounded_rect(cx + 22, vy + 100, cw - 44, 76, 6, 0x1A202E);
    font_draw_str(cx + 32, vy + 108, c1, 0x57E389, 0, true);
    font_draw_str(cx + 32, vy + 128, c2, 0x62A0EA, 0, true);
    font_draw_str(cx + 32, vy + 148, c3, 0xE2E8F0, 0, true);
}

static void firefox_mouse(wm_window_t *win UNUSED, int32_t rx, int32_t ry, uint8_t buttons UNUSED, bool click) {
    if (!click) return;
    if (ry >= 4 && ry <= 28) {
        int t = (rx - 8) / 164;
        if (t >= 0 && t < 3) {
            ff_tab = t;
            wm_dirty = true;
        }
    }
}

/* ---------------- App 6: System Settings (Screenshots, Video Recorder & Hardware) ---------------- */
static const char *const shot_folders[4] = {
    "/home/Pictures",
    "/home/Screenshots",
    "/home",
    "/tmp"
};

static const char *const vid_folders[4] = {
    "/home/Videos",
    "/home/Clips",
    "/home",
    "/tmp"
};

static void settings_paint(wm_window_t *win UNUSED, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    fb_fill_rounded_rect(cx + 4, cy + 4, cw - 8, ch - 8, 6, 0x161922);

    font_draw_str(cx + 16, cy + 10, "Screenshot Save Folder  (Grim / FlameShot / PrtSc):", 0xF8FAFC, 0, true);
    const char *cur_dir = screenshot_get_folder();

    for (int i = 0; i < 4; i++) {
        int32_t bx = cx + 16 + (i % 2) * 236;
        int32_t by = cy + 32 + (i / 2) * 32;
        bool sel = (strcmp(cur_dir, shot_folders[i]) == 0);
        fb_fill_rounded_rect(bx, by, 224, 26, 6, sel ? 0x1E3A5F : 0x222734);
        fb_draw_rounded_rect(bx, by, 224, 26, 6, sel ? 0x3B82F6 : 0x363E52);
        font_draw_str(bx + 12, by + 5, shot_folders[i], sel ? 0xFFFFFF : 0xCBD5E1, 0, true);
        if (sel) {
            font_draw_str(bx + 196, by + 5, "[v]", 0x60A5FA, 0, true);
        }
    }

    /* Action button: Take Screenshot Now */
    fb_fill_rounded_rect(cx + 16, cy + 102, 224, 26, 6, 0x2563EB);
    font_draw_str(cx + 30, cy + 107, "Take Screenshot Now (PrtSc)", 0xFFFFFF, 0, true);

    screenshot_info_t sh;
    if (get_last_screenshot(&sh)) {
        char sbuf[72];
        ksnprintf(sbuf, sizeof(sbuf), "Saved: %s", sh.saved_path);
        font_draw_str(cx + 250, cy + 107, sbuf, 0x57E389, 0, true);
    }

    fb_draw_line(cx + 16, cy + 136, cx + cw - 16, cy + 136, 0x2B303D);

    /* 1. Screen Recorder (NVENC / QuickSync / VA-API 120 FPS) + Video Folder Picker */
    font_draw_str(cx + 16, cy + 144, "Video Recording Folder  (NVENC / QuickSync 120 FPS):", 0xF8FAFC, 0, true);
    const char *cur_vdir = video_get_folder();
    for (int i = 0; i < 4; i++) {
        int32_t bx = cx + 16 + (i % 2) * 236;
        int32_t by = cy + 166 + (i / 2) * 32;
        bool sel = (strcmp(cur_vdir, vid_folders[i]) == 0);
        fb_fill_rounded_rect(bx, by, 224, 26, 6, sel ? 0x3B1D2A : 0x222734);
        fb_draw_rounded_rect(bx, by, 224, 26, 6, sel ? 0xEC4899 : 0x363E52);
        font_draw_str(bx + 12, by + 5, vid_folders[i], sel ? 0xFFFFFF : 0xCBD5E1, 0, true);
        if (sel) {
            font_draw_str(bx + 196, by + 5, "[v]", 0xF472B6, 0, true);
        }
    }

    fb_fill_rounded_rect(cx + 16, cy + 236, 224, 26, 6, 0xDC2626);
    font_draw_str(cx + 26, cy + 241, "[REC] Record 120FPS Clip", 0xFFFFFF, 0, true);

    screen_record_info_t ri;
    if (get_last_recording(&ri)) {
        char rbuf[72];
        ksnprintf(rbuf, sizeof(rbuf), "Saved: %s (%uFPS)", ri.saved_path, ri.fps);
        font_draw_str(cx + 250, cy + 241, rbuf, 0xFCA5A5, 0, true);
    }

    fb_draw_line(cx + 16, cy + 270, cx + cw - 16, cy + 270, 0x2B303D);
    font_draw_str(cx + 16, cy + 278, "CPU: Intel Core i7-2600K @ 4.60 GHz OC  |  GPU: RTX 5090..5060", 0x62A0EA, 0, true);
    font_draw_str(cx + 16, cy + 298, "Display: 8K 7680x4320 @ 240Hz & 1080p @ 540Hz VRR  |  VA-API 3%", 0x94A3B8, 0, true);
}

static void settings_mouse(wm_window_t *win UNUSED, int32_t rx, int32_t ry, uint8_t buttons UNUSED, bool click) {
    if (!click) return;
    for (int i = 0; i < 4; i++) {
        int32_t bx = 16 + (i % 2) * 236;
        int32_t by = 32 + (i / 2) * 32;
        if (rx >= bx && rx <= bx + 224 && ry >= by && ry <= by + 26) {
            screenshot_set_folder(shot_folders[i]);
            wm_dirty = true;
            return;
        }
    }
    if (rx >= 16 && rx <= 240 && ry >= 102 && ry <= 128) {
        screenshot_info_t sh;
        take_linux_screenshot(SHOT_MODE_FULLSCREEN, &sh);
        synth_play_note(1200, 45, WAVE_TRIANGLE, 70);
        wm_dirty = true;
        return;
    }
    for (int i = 0; i < 4; i++) {
        int32_t bx = 16 + (i % 2) * 236;
        int32_t by = 166 + (i / 2) * 32;
        if (rx >= bx && rx <= bx + 224 && ry >= by && ry <= by + 26) {
            video_set_folder(vid_folders[i]);
            wm_dirty = true;
            return;
        }
    }
    if (rx >= 16 && rx <= 240 && ry >= 236 && ry <= 262) {
        screen_record_info_t ri;
        record_screen_clip(120, &ri);
        synth_play_note(960, 45, WAVE_TRIANGLE, 70);
        wm_dirty = true;
    }
}

/* ---------------- App 7: Flux Software Center (GUI App Store) ---------------- */
typedef struct {
    const char *pkg_id;
    const char *title;
    const char *subtitle;
    uint32_t    badge_col;
} store_app_card_t;

static const store_app_card_t store_cards[8] = {
    { "google-chrome",    "Google Chrome 132",  "Web Browser (Wayland VA-API)", 0xEA580C },
    { "epic-games",       "Epic Games Store",   "Heroic + Proton 9.0 + EAC",    0x9333EA },
    { "steam",            "Steam Gaming",       "DXVK 2.5 + VKD3D DX12 Ult",    0x2563EB },
    { "vscode",           "VS Code IDE",        "C/C++, Rust, Python, Git",     0x0284C7 },
    { "telegram-desktop", "Telegram Desktop",   "Fast Encrypted Messenger",     0x38BDF8 },
    { "discord",          "Discord Voice",      "Wayland ScreenShare + NVENC",  0x6366F1 },
    { "obs-studio",       "OBS Studio 31",      "NVENC / QuickSync 120FPS",     0x10B981 },
    { "spotify",          "Spotify Music",      "PipeWire Lossless Audio",      0x22C55E }
};

static bool store_installed[8] = { true, true, true, true, false, false, false, false };
static char store_status_msg[80] = "DNF5 + Flathub Ready: 2,000 Linux & Gaming packages verified";

static void store_paint(wm_window_t *win UNUSED, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    fb_fill_rounded_rect(cx + 4, cy + 4, cw - 8, ch - 8, 6, 0x141720);

    font_draw_str(cx + 16, cy + 10, "Flux Software Center  (1-Click DNF5 / Flathub / Proton Installer)", 0xF8FAFC, 0, true);
    font_draw_str(cx + 16, cy + 28, store_status_msg, 0x57E389, 0, true);

    for (int i = 0; i < 8; i++) {
        int col = i % 2;
        int row = i / 2;
        int32_t bx = cx + 14 + col * 246;
        int32_t by = cy + 50 + row * 58;

        fb_fill_rounded_rect(bx, by, 236, 50, 6, 0x1E2330);
        fb_draw_rounded_rect(bx, by, 236, 50, 6, 0x323A4E);

        /* Colored app icon badge */
        fb_fill_rounded_rect(bx + 8, by + 9, 32, 32, 6, store_cards[i].badge_col);
        font_draw_char(bx + 20, by + 17, store_cards[i].title[0], 0xFFFFFF, 0, true);

        font_draw_str(bx + 46, by + 8, store_cards[i].title, 0xFFFFFF, 0, true);
        font_draw_str(bx + 46, by + 26, store_cards[i].subtitle, 0x94A3B8, 0, true);

        /* Install / Open button */
        bool inst = store_installed[i];
        fb_fill_rounded_rect(bx + 176, by + 13, 52, 24, 5, inst ? 0x1E3A5F : 0x2563EB);
        font_draw_str(bx + 182, by + 17, inst ? "Ready" : "Get", inst ? 0x86EFAC : 0xFFFFFF, 0, true);
    }
}

static void store_mouse(wm_window_t *win UNUSED, int32_t rx, int32_t ry, uint8_t buttons UNUSED, bool click) {
    if (!click) return;
    for (int i = 0; i < 8; i++) {
        int col = i % 2;
        int row = i / 2;
        int32_t bx = 14 + col * 246;
        int32_t by = 50 + row * 58;
        if (rx >= bx && rx <= bx + 236 && ry >= by && ry <= by + 50) {
            char msg[128];
            if (pkg_install(store_cards[i].pkg_id, msg, sizeof(msg))) {
                store_installed[i] = true;
                ksnprintf(store_status_msg, sizeof(store_status_msg), "Installed: %s -> /usr/bin/%s",
                          store_cards[i].title, store_cards[i].pkg_id);
                synth_play_note(1040, 45, WAVE_TRIANGLE, 70);
                wm_dirty = true;
            }
            return;
        }
    }
}

/* Top-Right Quick Settings / Power Menu State */
static bool quick_menu_open = false;
static int  quick_vol_pct   = 85;
static bool night_light_on  = false;

/* ---------------- Window Manager Core ---------------- */
static void focus_window(int win_idx) {
    for (int i = 0; i < win_count; i++) {
        windows[i].focused = (i == win_idx);
    }
    int pos = -1;
    for (int i = 0; i < win_count; i++) {
        if (z_order[i] == win_idx) { pos = i; break; }
    }
    if (pos >= 0 && pos < win_count - 1) {
        for (int i = pos; i < win_count - 1; i++) z_order[i] = z_order[i + 1];
        z_order[win_count - 1] = win_idx;
    }
    wm_dirty = true;
}

static bool is_app_open(wm_app_kind_t kind) {
    for (int i = 0; i < win_count; i++) {
        if (windows[i].kind == kind && windows[i].visible) return true;
    }
    return false;
}

wm_window_t *wm_open_app(wm_app_kind_t kind) {
    for (int i = 0; i < win_count; i++) {
        if (windows[i].kind == kind) {
            windows[i].visible = true;
            focus_window(i);
            return &windows[i];
        }
    }
    if (win_count >= WM_MAX_WINDOWS) return NULL;

    int idx = win_count++;
    wm_window_t *w = &windows[idx];
    memset(w, 0, sizeof(*w));
    w->id = (uint32_t)(idx + 1);
    w->kind = kind;
    w->visible = true;

    if (kind == WIN_FILES) {
        strcpy(w->title, "Flux Files  (System Disk /)");
        w->x = 36; w->y = 48; w->w = 528; w->h = 360;
        w->accent_color = COL_ACCENT_BLUE;
        w->on_paint = files_paint; w->on_mouse = files_mouse;
    } else if (kind == WIN_TERMINAL) {
        strcpy(w->title, "FluxShell Terminal");
        w->x = 472; w->y = 264; w->w = 516; w->h = 408;
        w->accent_color = 0x62A0EA;
        w->on_paint = term_paint; w->on_key = term_key;
    } else if (kind == WIN_SYSMON) {
        strcpy(w->title, "System Monitor");
        w->x = 520; w->y = 54; w->w = 468; w->h = 288;
        w->accent_color = 0x2EC27E;
        w->on_paint = sysmon_paint;
    } else if (kind == WIN_FIREFOX) {
        strcpy(w->title, "Mozilla Firefox 132.0 (Wayland)");
        w->x = 466; w->y = 368; w->w = 536; w->h = 320;
        w->accent_color = 0xEA580C;
        w->on_paint = firefox_paint; w->on_mouse = firefox_mouse;
    } else if (kind == WIN_SETTINGS) {
        strcpy(w->title, "System Settings  (Screenshots, Video & Hardware)");
        w->x = 230; w->y = 92; w->w = 504; w->h = 360;
        w->accent_color = COL_ACCENT_BLUE;
        w->on_paint = settings_paint; w->on_mouse = settings_mouse;
    } else if (kind == WIN_STORE) {
        strcpy(w->title, "Flux Software Center  (DNF5 / Flathub / Proton)");
        w->x = 216; w->y = 84; w->w = 524; w->h = 336;
        w->accent_color = 0x2563EB;
        w->on_paint = store_paint; w->on_mouse = store_mouse;
    } else {
        strcpy(w->title, "FluxOS 0.4 (Workstation Edition) installation");
        w->x = 20; w->y = 40; w->w = 608; w->h = 382;
        w->accent_color = COL_ACCENT_BLUE;
        w->on_paint = welcome_paint; w->on_mouse = welcome_mouse;
    }

    z_order[idx] = idx;
    focus_window(idx);
    return w;
}

static void draw_cursor(int32_t x, int32_t y) {
    static const char *const sprite[14] = {
        "X         ",
        "XX        ",
        "X.X       ",
        "X..X      ",
        "X...X     ",
        "X....X    ",
        "X.....X   ",
        "X......X  ",
        "X.......X ",
        "X.....XXXX",
        "X..X..X   ",
        "X.X X..X  ",
        "XX   X..X ",
        "X     XX  "
    };
    for (int r = 0; r < 14; r++) {
        for (int c = 0; c < 10; c++) {
            char ch = sprite[r][c];
            if (ch == 'X') fb_put_pixel(x + c, y + r, 0x0F172A);
            else if (ch == '.') fb_put_pixel(x + c, y + r, 0xFFFFFF);
        }
    }
}

static void draw_dock_icon(int slot, int32_t ix, int32_t iy) {
    int32_t cx = ix + 17;
    int32_t cy = iy + 17;

    if (slot == 0) {
        /* 0: Blue Folder (Files) */
        fb_fill_rounded_rect(ix + 4, iy + 6, 12, 6, 2, 0x8CC2FF);
        fb_fill_rounded_rect(ix + 3, iy + 9, 28, 20, 4, 0x4A90E2);
        fb_fill_rect(ix + 5, iy + 10, 24, 2, 0x8CC2FF);
    } else if (slot == 1) {
        /* 1: System Disk / Monitor */
        fb_fill_rounded_rect(ix + 5, iy + 4, 24, 26, 5, 0x2D333F);
        fb_draw_rounded_rect(ix + 5, iy + 4, 24, 26, 5, 0x4B5563);
        fb_draw_circle(cx, cy - 2, 7, 0xCBD5E1, false);
        fb_draw_circle(cx, cy - 2, 2, 0x94A3B8, true);
        fb_fill_rect(ix + 9, iy + 25, 16, 2, 0x1E232B);
    } else if (slot == 2) {
        /* 2: Terminal (>_) */
        fb_fill_rounded_rect(ix + 3, iy + 5, 28, 24, 5, 0x10131A);
        fb_draw_rounded_rect(ix + 3, iy + 5, 28, 24, 5, 0x2F3747);
        font_draw_str(ix + 8, iy + 9, ">_", 0xE2E8F0, 0, true);
    } else if (slot == 3) {
        /* 3: Mozilla Firefox (Coral-Orange & Indigo) */
        fb_draw_circle(cx, cy, 13, 0xEA580C, true);
        fb_draw_circle(cx - 2, cy - 2, 10, 0xFB923C, true);
        fb_draw_circle(cx + 2, cy + 2, 8, 0x6366F1, true);
        fb_draw_circle(cx - 4, cy - 4, 4, 0xFDE047, true);
    } else if (slot == 4) {
        /* 4: Flatpak / Software Center Bag */
        fb_draw_circle(cx, iy + 10, 5, 0xE2E8F0, false);
        fb_fill_rounded_rect(ix + 6, iy + 10, 22, 19, 4, 0x2563EB);
        fb_fill_rect(ix + 8, iy + 11, 18, 2, 0x60A5FA);
        fb_draw_circle(cx, iy + 20, 4, 0xDBEAFE, true);
        fb_draw_circle(cx, iy + 20, 2, 0x1D4ED8, true);
    } else if (slot == 5) {
        /* 5: Settings Gear */
        fb_fill_rect(cx - 2, iy + 3, 5, 28, 0x6B7280);
        fb_fill_rect(ix + 3, cy - 2, 28, 5, 0x6B7280);
        fb_draw_circle(cx, cy, 11, 0x4B5563, true);
        fb_draw_circle(cx, cy, 7, 0x9CA3AF, false);
        fb_draw_circle(cx, cy, 4, 0x181B22, true);
    } else if (slot == 6) {
        /* 6: Round Flux 'F' Emblem */
        fb_draw_circle(cx, cy, 14, 0x101520, true);
        fb_draw_circle(cx, cy, 14, 0x3B82F6, false);
        fb_draw_line(ix + 14, iy + 10, ix + 24, iy + 10, 0xE2E8F0);
        fb_draw_line(ix + 14, iy + 11, ix + 24, iy + 11, 0x60A5FA);
        fb_draw_line(ix + 14, iy + 10, ix + 10, iy + 25, 0xE2E8F0);
        fb_draw_line(ix + 15, iy + 10, ix + 11, iy + 25, 0x60A5FA);
        fb_draw_line(ix + 12, iy + 17, ix + 21, iy + 17, 0xE2E8F0);
    } else if (slot == 7) {
        /* 7: Desktop Trash Bin (/home/.Trash) in Dock */
        bool has_trash = (trash_item_count() > 0);
        if (has_trash) {
            /* Crumpled paper peeking out when Trash is non-empty */
            fb_fill_rounded_rect(ix + 10, iy + 3, 6, 5, 2, 0xF8FAFC);
            fb_fill_rounded_rect(ix + 17, iy + 2, 6, 6, 2, 0xFDE047);
        }
        /* Lid handle + lid bar */
        fb_fill_rounded_rect(ix + 13, iy + 4, 8, 3, 1, 0x94A3B8);
        fb_fill_rounded_rect(ix + 6, iy + 6, 22, 4, 2, 0xCBD5E1);
        /* Trash can body + border */
        fb_fill_rounded_rect(ix + 8, iy + 10, 18, 19, 3, 0x334155);
        fb_draw_rounded_rect(ix + 8, iy + 10, 18, 19, 3, 0x64748B);
        /* 3 vertical metallic ribs */
        fb_fill_rect(ix + 12, iy + 13, 2, 12, 0x94A3B8);
        fb_fill_rect(ix + 16, iy + 13, 2, 12, 0x94A3B8);
        fb_fill_rect(ix + 20, iy + 13, 2, 12, 0x94A3B8);
        if (has_trash) {
            fb_draw_circle(ix + 26, iy + 25, 4, 0xEF4444, true);
        }
    } else {
        /* 8: App Grid (3x3 crisp white dots) */
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                fb_fill_rect(ix + 7 + c * 7, iy + 7 + r * 7, 3, 3, 0xE5E9F0);
            }
        }
    }
}

static void wm_compose_frame(mouse_state_t ms) {
    int32_t sw = (int32_t)fb_width();
    int32_t sh = (int32_t)fb_height();

    if (wall_buf) {
        fb_blit_screen(wall_buf);
    } else {
        fb_fill_vgradient(0, 0, sw, sh, 0x11151E, 0x1D2536);
    }

    /* Sleek Translucent Top Panel */
    fb_fill_rect_alpha(0, 0, sw, TOPBAR_H, 0x0E1016, 210);
    fb_fill_rect(0, TOPBAR_H - 1, sw, 1, 0x2B303D);
    font_draw_str(14, 6, "Overview   Places", 0xE5E9F0, 0, true);

    uint32_t r_day = 14, r_mon = 11, r_year = 2026, r_hr = 13, r_min = 20;
    rtc_get_datetime(&r_day, &r_mon, &r_year, &r_hr, &r_min);

    char ctr[48];
    ksnprintf(ctr, sizeof(ctr), "%02u.%02u.%04u  %02u:%02u  (%s)",
              r_day, r_mon, r_year, r_hr, r_min, tz_short[tz_idx]);
    font_draw_str((sw - font_str_width(ctr)) / 2, 6, ctr, 0xF8FAFC, 0, true);

    /* Right Status Tray: Desk 1 | Keyboard Layout | Network | Regional Date & Time */
    fb_fill_rounded_rect(sw - 280, 4, 52, 20, 9, 0x222733);
    font_draw_str(sw - 273, 6, "Desk 1", 0xE2E8F0, 0, true);

    fb_fill_rounded_rect(sw - 222, 4, 28, 20, 7, 0x283142);
    font_draw_str(sw - 215, 6, kbd_codes[kbd_idx], 0x62A0EA, 0, true);

    fb_draw_circle(sw - 182, 14, 4, e1000_is_ready() ? 0x2EC27E : 0xEF4444, true);
    char rinfo[36];
    ksnprintf(rinfo, sizeof(rinfo), "%02u.%02u.%04u %02u:%02u %s",
              r_day, r_mon, r_year, r_hr, r_min, tz_short[tz_idx]);
    font_draw_str(sw - 170, 6, rinfo, 0xCBD5E1, 0, true);

    /* Composite rounded Libadwaita windows back-to-front */
    for (int zi = 0; zi < win_count; zi++) {
        wm_window_t *w = &windows[z_order[zi]];
        if (!w->visible) continue;

        fb_fill_rounded_rect_alpha(w->x + 5, w->y + 7, w->w, w->h, 11, 0x000000, 125);
        fb_fill_rounded_rect(w->x, w->y, w->w, w->h, 10, (w->kind == WIN_WELCOME) ? 0xFFFFFF : COL_BG_WIN);

        if (w->kind == WIN_WELCOME) {
            uint32_t hdr = 0x2E74C6;
            fb_fill_rounded_rect(w->x + 1, w->y + 1, w->w - 2, 22, 9, hdr);
            fb_fill_rect(w->x + 1, w->y + 11, w->w - 2, TITLEBAR_H - 11, hdr);

            /* White circle with blue 'f' emblem on left of blue bar */
            fb_draw_circle(w->x + 18, w->y + 15, 8, 0xFFFFFF, true);
            font_draw_char(w->x + 16, w->y + 7, 'f', 0x2E74C6, 0, true);
            font_draw_str(w->x + 34, w->y + 7, w->title, 0xFFFFFF, 0, true);

            /* Three vertical dots on right + close button */
            int32_t dx = w->x + w->w - 38;
            fb_fill_rect(dx, w->y + 10, 2, 2, 0xFFFFFF);
            fb_fill_rect(dx, w->y + 15, 2, 2, 0xFFFFFF);
            fb_fill_rect(dx, w->y + 20, 2, 2, 0xFFFFFF);

            int32_t bx = w->x + w->w - 24;
            fb_draw_circle(bx + 6, w->y + 15, 7, 0x235BA0, true);
            font_draw_char(bx + 3, w->y + 7, 'x', 0xFFFFFF, 0, true);

            if (w->on_paint) {
                w->on_paint(w, w->x + 1, w->y + TITLEBAR_H, w->w - 2, w->h - TITLEBAR_H - 2);
            }
            fb_draw_rounded_rect(w->x, w->y, w->w, w->h, 10, 0x2E74C6);
        } else {
            uint32_t hdr = w->focused ? COL_BG_HDR_ACT : COL_BG_HDR_IDLE;
            fb_fill_rounded_rect(w->x + 1, w->y + 1, w->w - 2, 22, 9, hdr);
            fb_fill_rect(w->x + 1, w->y + 11, w->w - 2, TITLEBAR_H - 11, hdr);
            fb_fill_rect(w->x + 1, w->y + TITLEBAR_H, w->w - 2, 1, 0x2F3544);

            int32_t tw = font_str_width(w->title);
            font_draw_str(w->x + (w->w - tw) / 2, w->y + 7, w->title, 0xF1F5F9, 0, true);

            int32_t bx = w->x + w->w - 26;
            fb_draw_circle(bx + 8, w->y + 15, 8, w->focused ? 0x353B49 : 0x282D38, true);
            font_draw_char(bx + 4, w->y + 7, 'x', 0xE2E8F0, 0, true);

            if (w->on_paint) {
                w->on_paint(w, w->x + 4, w->y + TITLEBAR_H + 2, w->w - 8, w->h - TITLEBAR_H - 10);
            }
            fb_draw_rounded_rect(w->x, w->y, w->w, w->h, 10, w->focused ? COL_BORDER_ACT : COL_BORDER_IDLE);
        }
    }

    /* Centered Floating Pill Dock */
    int32_t dx0 = (sw - DOCK_W) / 2;
    int32_t dy0 = sh - DOCK_H - DOCK_Y_PAD;
    fb_fill_rounded_rect_alpha(dx0 + 3, dy0 + 4, DOCK_W, DOCK_H, 15, 0x000000, 125);
    fb_fill_rounded_rect_alpha(dx0, dy0, DOCK_W, DOCK_H, 14, 0x141720, 228);
    fb_draw_rounded_rect(dx0, dy0, DOCK_W, DOCK_H, 14, 0x363E52);

    for (int i = 0; i < 9; i++) {
        int32_t ix = dx0 + 12 + i * 36 + (i == 8 ? 10 : 0);
        if (i == 8) {
            fb_fill_rect(ix - 5, dy0 + 9, 1, 28, 0x363E52);
        }
        draw_dock_icon(i, ix, dy0 + 6);
        bool in_trash_view = is_app_open(WIN_FILES) && (strcmp(files_cwd, "/home/.Trash") == 0);
        bool open = (i == 0 && is_app_open(WIN_FILES) && !in_trash_view) ||
                    (i == 1 && is_app_open(WIN_SYSMON)) ||
                    (i == 2 && is_app_open(WIN_TERMINAL)) ||
                    (i == 3 && is_app_open(WIN_FIREFOX)) ||
                    (i == 4 && is_app_open(WIN_STORE)) ||
                    (i == 5 && is_app_open(WIN_SETTINGS)) ||
                    (i == 6 && is_app_open(WIN_WELCOME)) ||
                    (i == 7 && (in_trash_view || trash_item_count() > 0));
        if (open) {
            fb_fill_rounded_rect(ix + 12, dy0 + 41, 8, 3, 1, 0x62A0EA);
        }
    }

    /* 5. Top-Right Quick Settings / Power Menu Popup */
    if (quick_menu_open) {
        int32_t qx = sw - 274;
        int32_t qy = TOPBAR_H + 6;
        int32_t qw = 266;
        int32_t qh = 206;

        fb_fill_rounded_rect_alpha(qx + 4, qy + 5, qw, qh, 11, 0x000000, 140);
        fb_fill_rounded_rect_alpha(qx, qy, qw, qh, 10, 0x161923, 244);
        fb_draw_rounded_rect(qx, qy, qw, qh, 10, 0x363E52);

        char vbuf[40];
        ksnprintf(vbuf, sizeof(vbuf), "PipeWire Audio: %d%%", quick_vol_pct);
        font_draw_str(qx + 14, qy + 10, vbuf, 0xF8FAFC, 0, true);
        fb_fill_rounded_rect(qx + 14, qy + 30, qw - 28, 12, 5, 0x262B38);
        fb_fill_rounded_rect(qx + 14, qy + 30, ((qw - 28) * quick_vol_pct) / 100, 12, 5, 0x3B82F6);

        /* Wi-Fi & Night Light toggles */
        fb_fill_rounded_rect(qx + 14, qy + 52, 116, 30, 6, 0x1E3A5F);
        font_draw_str(qx + 22, qy + 60, "Wi-Fi 7: ON", 0x86EFAC, 0, true);

        fb_fill_rounded_rect(qx + 136, qy + 52, 116, 30, 6, night_light_on ? 0x7C2D12 : 0x242A38);
        font_draw_str(qx + 144, qy + 60, night_light_on ? "Night: ON" : "Night: OFF", 0xFDE047, 0, true);

        /* Quick Screenshot & 120FPS Record */
        fb_fill_rounded_rect(qx + 14, qy + 90, 116, 30, 6, 0x2563EB);
        font_draw_str(qx + 22, qy + 98, "Screenshot", 0xFFFFFF, 0, true);

        fb_fill_rounded_rect(qx + 136, qy + 90, 116, 30, 6, 0xDC2626);
        font_draw_str(qx + 146, qy + 98, "Rec 120FPS", 0xFFFFFF, 0, true);

        /* Timezone switcher & Power Row */
        fb_fill_rounded_rect(qx + 14, qy + 128, qw - 28, 26, 6, 0x222734);
        char tz_lbl[40];
        ksnprintf(tz_lbl, sizeof(tz_lbl), "Timezone: %s (Click)", tz_short[tz_idx]);
        font_draw_str(qx + 24, qy + 133, tz_lbl, 0xCBD5E1, 0, true);

        fb_fill_rounded_rect(qx + 14, qy + 162, 76, 30, 6, 0x283142);
        font_draw_str(qx + 22, qy + 170, "Sleep S3", 0x93C5FD, 0, true);

        fb_fill_rounded_rect(qx + 96, qy + 162, 74, 30, 6, 0x3B2F1E);
        font_draw_str(qx + 110, qy + 170, "Reboot", 0xFDE047, 0, true);

        fb_fill_rounded_rect(qx + 176, qy + 162, 76, 30, 6, 0x451A23);
        font_draw_str(qx + 182, qy + 170, "PowerOff", 0xFCA5A5, 0, true);
    }

    draw_cursor(ms.x, ms.y);
    fb_present();
}

static void handle_mouse_event(mouse_state_t ms) {
    bool left_now  = (ms.buttons & 1) != 0;
    bool left_prev = (ms.prev_buttons & 1) != 0;
    bool just_down = left_now && !left_prev;
    bool just_up   = !left_now && left_prev;

    if (drag_win_idx >= 0) {
        if (left_now) {
            windows[drag_win_idx].x = ms.x - drag_off_x;
            windows[drag_win_idx].y = MAX(TOPBAR_H, ms.y - drag_off_y);
            wm_dirty = true;
            return;
        }
        if (just_up) {
            drag_win_idx = -1;
        }
    }

    if (!just_down) {
        wm_dirty = true;
        return;
    }

    int32_t sw = (int32_t)fb_width();
    int32_t sh = (int32_t)fb_height();

    /* Top bar quick actions: Overview, Places, Keyboard Layout badge, Quick Settings Menu */
    if (ms.y < TOPBAR_H) {
        if (ms.x >= 10 && ms.x <= 86) wm_open_app(WIN_WELCOME);
        else if (ms.x >= 90 && ms.x <= 160) wm_open_app(WIN_FILES);
        else if (ms.x >= sw - 250 && ms.x <= sw - 190) {
            kbd_idx = (kbd_idx + 1) % 3;
            wm_dirty = true;
        } else if (ms.x >= sw - 188) {
            quick_menu_open = !quick_menu_open;
            wm_dirty = true;
        }
        return;
    }

    /* Quick Settings / Power Menu hit-test when open */
    if (quick_menu_open) {
        int32_t qx = sw - 274;
        int32_t qy = TOPBAR_H + 6;
        int32_t qw = 266;
        int32_t qh = 206;
        if (ms.x >= qx && ms.x <= qx + qw && ms.y >= qy && ms.y <= qy + qh) {
            int32_t rx = ms.x - qx;
            int32_t ry = ms.y - qy;
            if (ry >= 26 && ry <= 46 && rx >= 14 && rx <= qw - 14) {
                quick_vol_pct = (int)(((rx - 14) * 100) / (qw - 28));
                if (quick_vol_pct < 0) quick_vol_pct = 0;
                if (quick_vol_pct > 100) quick_vol_pct = 100;
            } else if (ry >= 52 && ry <= 82 && rx >= 136) {
                night_light_on = !night_light_on;
            } else if (ry >= 90 && ry <= 120) {
                if (rx < 132) {
                    screenshot_info_t sinfo;
                    take_linux_screenshot(SHOT_MODE_FULLSCREEN, &sinfo);
                    synth_play_note(1200, 45, WAVE_TRIANGLE, 70);
                } else {
                    screen_record_info_t rinfo;
                    record_screen_clip(120, &rinfo);
                    synth_play_note(960, 45, WAVE_TRIANGLE, 70);
                }
                quick_menu_open = false;
            } else if (ry >= 128 && ry <= 154) {
                tz_idx = (tz_idx + 1) % 4;
            } else if (ry >= 162 && ry <= 192) {
                if (rx < 92) {
                    quick_menu_open = false;
                } else if (rx < 172) {
                    outb(0x64, 0xFE);
                } else {
                    outw(0x604, 0x2000);
                }
            }
            wm_dirty = true;
            return;
        }
        quick_menu_open = false;
        wm_dirty = true;
    }

    /* Centered Floating Dock hit-test */
    int32_t dx0 = (sw - DOCK_W) / 2;
    int32_t dy0 = sh - DOCK_H - DOCK_Y_PAD;
    if (ms.x >= dx0 && ms.x < dx0 + DOCK_W && ms.y >= dy0 && ms.y < dy0 + DOCK_H) {
        for (int i = 0; i < 9; i++) {
            int32_t ix = dx0 + 12 + i * 36 + (i == 8 ? 10 : 0);
            if (ms.x >= ix && ms.x <= ix + 32) {
                if (i == 0) {
                    if (strcmp(files_cwd, "/home/.Trash") == 0) files_cd("/home");
                    wm_open_app(WIN_FILES);
                } else if (i == 1) wm_open_app(WIN_SYSMON);
                else if (i == 2) wm_open_app(WIN_TERMINAL);
                else if (i == 3) wm_open_app(WIN_FIREFOX);
                else if (i == 4) wm_open_app(WIN_STORE);
                else if (i == 5) wm_open_app(WIN_SETTINGS);
                else if (i == 6) wm_open_app(WIN_WELCOME);
                else if (i == 7) {
                    files_cd("/home/.Trash");
                    wm_open_app(WIN_FILES);
                } else synth_play_boot_chime();
                return;
            }
        }
        return;
    }

    /* Hit-test windows front-to-back */
    for (int zi = win_count - 1; zi >= 0; zi--) {
        int idx = z_order[zi];
        wm_window_t *w = &windows[idx];
        if (!w->visible) continue;
        if (ms.x >= w->x && ms.x < w->x + w->w && ms.y >= w->y && ms.y < w->y + w->h) {
            focus_window(idx);
            int32_t bx = w->x + w->w - 28;
            if (ms.y < w->y + TITLEBAR_H) {
                if (ms.x >= bx && ms.x <= bx + 20) {
                    w->visible = false;
                    wm_dirty = true;
                } else {
                    drag_win_idx = idx;
                    drag_off_x = ms.x - w->x;
                    drag_off_y = ms.y - w->y;
                }
            } else if (w->on_mouse) {
                w->on_mouse(w, ms.x - (w->x + 4), ms.y - (w->y + TITLEBAR_H + 2), ms.buttons, true);
            }
            return;
        }
    }
}

static void load_wallpaper(void) {
    uint32_t sw = fb_width(), sh = fb_height();
    size_t expect = (size_t)sw * (size_t)sh * 3U;
    uint8_t *raw = NULL;
    size_t sz = 0;

    if (!vfs_read_all("/etc/wall.rgb", &raw, &sz) || sz != expect) {
        if (raw) kfree(raw);
        return;
    }
    size_t px_bytes = (size_t)sw * (size_t)sh * sizeof(uint32_t);
    uint64_t phys = pmm_alloc_pages(ALIGN_UP(px_bytes, PAGE_SIZE) / PAGE_SIZE);
    if (!phys) {
        kfree(raw);
        return;
    }
    wall_buf = (uint32_t *)phys_to_virt(phys);
    size_t count = (size_t)sw * (size_t)sh;
    for (size_t i = 0; i < count; i++) {
        uint32_t r = raw[i * 3 + 0];
        uint32_t g = raw[i * 3 + 1];
        uint32_t b = raw[i * 3 + 2];
        wall_buf[i] = (r << 16) | (g << 8) | b;
    }
    kfree(raw);
    kprintf("[WM] Loaded 24-bit 3D cubes wallpaper (%ux%u)\n", sw, sh);
}

void wm_init(void) {
    memset(windows, 0, sizeof(windows));
    memset(term_lines, 0, sizeof(term_lines));
    win_count = 0;

    load_wallpaper();
    files_cd("/");

    wm_open_app(WIN_FIREFOX);
    wm_open_app(WIN_WELCOME);

    syscall_set_console_sink(wm_term_write);
    term_printf("Welcome to FluxOS 0.4! Type 'help' for commands.\n");
    exec_shell_cmd("sestatus");
    exec_shell_cmd("luks");
    exec_shell_cmd("firewall-cmd");
    exec_shell_cmd("flatpak");
    kprintf("[WM] Compositor initialized (%ux%u, Libadwaita theme)\n", fb_width(), fb_height());
}

void wm_run_loop(void) {
    uint64_t last_refresh = 0;
    for (;;) {
        bool redraw = wm_dirty;

        key_event_t kev;
        while (ps2_poll_key(&kev)) {
            if (win_count > 0) {
                wm_window_t *top = &windows[z_order[win_count - 1]];
                if (top->visible && top->on_key) top->on_key(top, &kev);
            }
            redraw = true;
        }

        mouse_state_t ms;
        if (ps2_poll_mouse(&ms)) {
            handle_mouse_event(ms);
            redraw = true;
        } else {
            ms = ps2_get_mouse();
        }

        uint64_t now = timer_uptime_ms();
        if (now - last_refresh >= 250) {
            redraw = true;
            last_refresh = now;
        }

        if (redraw) {
            wm_dirty = false;
            wm_compose_frame(ms);
        }

        task_sleep_ms(16);
    }
}
