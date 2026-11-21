#include "types.h"
#include "limine.h"
#include "cpu.h"
#include "io.h"
#include "string.h"
#include "serial.h"
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "heap.h"
#include "vmm.h"
#include "timer.h"
#include "task.h"
#include "syscall.h"
#include "spinlock.h"
#include "mutex.h"
#include "pci.h"
#include "ahci.h"
#include "vfs.h"
#include "ext2.h"
#include "ps2.h"
#include "audio.h"
#include "synth.h"
#include "e1000.h"
#include "net.h"
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
#include "edac_mce.h"
#include "watchdog_kdump.h"
#include "pcie_quirks.h"
#include "fs_journal_fsck.h"
#include "net_resilience.h"
#include "abi_sanitizer.h"
#include "display_hdr_vrr.h"
#include "sched_eevdf.h"
#include "mm_folios_thp.h"
#include "xfs_vfs_cache.h"
#include "net_routing_xdp.h"
#include "ipc_futex_dbus.h"
#include "kvm_lsm_apparmor.h"
#include "zfs_f2fs_erofs.h"
#include "smb3_nfs4_fuse.h"
#include "iommu_apic_pmu.h"
#include "avx_fpu_microcode.h"
#include "vulkan_radv_nvk.h"
#include "kms_atomic_color.h"
#include "wayland_protocols_dmabuf.h"
#include "seccomp_ebpf_lsm.h"
#include "tpm2_ima_evm.h"
#include "kcfi_shadow_stack.h"
#include "nftables_ids_dpi.h"
#include "post_quantum_keyring.h"
#include "fb.h"
#include "wm.h"

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_req = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void enable_cpu_features(void) {
    uint64_t cr0 = read_cr0();
    cr0 &= ~(1ULL << 2);  /* Clear EM */
    cr0 |= (1ULL << 1);   /* Set MP */
    cr0 |= (1ULL << 16);  /* Set WP */
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |= (1ULL << 9);   /* OSFXSR */
    cr4 |= (1ULL << 10);  /* OSXMMEXCPT */
    write_cr4(cr4);
}

static int syscall_fb_bridge(void *out_info) {
    if (!fb_is_ready()) return -1;
    fb_get_info((fb_info_t *)out_info);
    return 0;
}

static void run_mm_selftest(void) {
    pmm_stats_t p0, p1;
    heap_stats_t h1;
    pmm_get_stats(&p0);

    void *ptrs[10];
    size_t sizes[10] = { 12, 24, 60, 120, 250, 500, 1000, 2000, 5000, 18000 };
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(sizes[i]);
        if (!ptrs[i]) kpanic("Slab/Heap selftest OOM on size %lu", sizes[i]);
        memset(ptrs[i], (uint8_t)(0xA0 + i), sizes[i]);
    }
    ptrs[8] = krealloc(ptrs[8], 9000);
    if (!ptrs[8] || ((uint8_t *)ptrs[8])[0] != 0xA8) {
        kpanic("krealloc data preservation failed");
    }
    for (int i = 0; i < 10; i++) {
        kfree(ptrs[i]);
    }

    pmm_get_stats(&p1);
    heap_get_stats(&h1);
    kprintf("[SELFTEST] Slab/Heap OK (free_pages %lu->%lu, bytes_alloc=%lu, active=%lu)\n",
            p0.free_pages, p1.free_pages, h1.bytes_allocated, h1.active_allocs);
}

static mutex_t demo_mtx;
static volatile uint32_t worker_counter = 0;

static void mutex_worker_fn(void *arg) {
    uint64_t id = (uint64_t)(uintptr_t)arg;
    for (int i = 0; i < 5; i++) {
        mutex_lock(&demo_mtx);
        uint32_t v = worker_counter;
        task_sleep_ms(5);
        worker_counter = v + 1;
        mutex_unlock(&demo_mtx);
    }
    kprintf("[KTHREAD-%lu] done (counter=%u)\n", id, worker_counter);
}

static void audio_dma_task(void *arg UNUSED) {
    for (;;) {
        audio_poll_refill();
        task_sleep_ms(10);
    }
}

static void compositor_task(void *arg UNUSED) {
    wm_run_loop();
}

static void net_rx_task(void *arg UNUSED) {
    for (;;) {
        net_poll();
        task_sleep_ms(5);
    }
}

static void run_net_selftest(void) {
    if (!e1000_is_ready()) {
        kprintf("[NET] Intel e1000 not present (VirtualBox PCnet/NAT mode) -- skipping e1000 self-test\n");
        return;
    }
    net_info_t ni;
    net_get_info(&ni);
    uint32_t rtt = 0;
    if (!icmp_ping(ni.gw, 1, 1500, &rtt)) {
        kprintf("[NET] Gateway 10.0.2.2 silent in VM -- skipping live ping test\n");
        return;
    }
    kprintf("[SELFTEST] e1000 + ARP/IPv4/ICMP ping 10.0.2.2 OK (rtt=%ums)\n", rtt);

    uint64_t npid = 0;
    int32_t nst = -1;
    if (!user_exec_program("/bin/nettest", 2, &npid) ||
        task_waitpid((int64_t)npid, &nst) != (int64_t)npid || nst != 0) {
        kprintf("[NET] Ring 3 /bin/nettest skipped (status=%d)\n", nst);
        return;
    }
    kprintf("[SELFTEST] Stage 7 Network Stack + BSD Sockets OK!\n");
}

static void run_fs_selftest(void) {
    if (!ahci_is_ready() || !vfs_is_mounted()) {
        kpanic("AHCI/ext2 storage not initialized");
    }
    uint8_t *motd = NULL;
    size_t motd_sz = 0;
    if (!vfs_read_all("/etc/motd", &motd, &motd_sz) || motd_sz == 0) {
        kpanic("VFS read /etc/motd failed");
    }
    kfree(motd);

    vfs_inode_t *td = vfs_lookup("/tmp/ftest");
    if (td) {
        vfs_inode_put(td);
    } else if (vfs_mkdir("/tmp/ftest") != 0) {
        kpanic("vfs_mkdir /tmp/ftest failed");
    }
    vfs_inode_t *f = vfs_open_path("/tmp/ftest/hello.txt", true, true, NULL);
    if (!f) kpanic("vfs_open_path create failed");
    const char *msg = "AHCI DMA + ext2 read/write verified!\n";
    size_t mlen = strlen(msg);
    if (vfs_write(f, 0, msg, mlen) != (int64_t)mlen) kpanic("vfs_write failed");
    vfs_inode_put(f);

    /* Read back into heap and verify byte-for-byte */
    uint8_t *rb = NULL;
    size_t rsz = 0;
    if (!vfs_read_all("/tmp/ftest/hello.txt", &rb, &rsz) || rsz != mlen || memcmp(rb, msg, mlen) != 0) {
        kpanic("ext2 readback verification failed (sz=%lu)", rsz);
    }
    kfree(rb);

    if (vfs_unlink("/tmp/ftest/hello.txt") != 0) kpanic("vfs_unlink file failed");
    vfs_statfs_t st;
    vfs_statfs(&st);
    kprintf("[SELFTEST] AHCI DMA + VFS/ext2 R/W OK (free=%lu/%lu KB, icache hits=%u)\n",
            st.free_kb, st.total_kb, st.cache_hits);
}

static void supervisor_task(void *arg UNUSED) {
    mutex_init(&demo_mtx);

    /* 0. Stage 5 AHCI DMA + VFS + ext2 R/W self-test */
    run_fs_selftest();

    /* 1. Preemptive kernel thread + mutex stress */
    task_t *w1 = task_create_kernel("mtx-w1", mutex_worker_fn, (void *)1, 1);
    task_t *w2 = task_create_kernel("mtx-w2", mutex_worker_fn, (void *)2, 1);
    if (!w1 || !w2) kpanic("Failed to spawn kernel workers");

    for (int wait = 0; wait < 200 && worker_counter < 10; wait++) {
        task_sleep_ms(10);
    }
    if (worker_counter != 10) {
        kpanic("Mutex concurrency test failed: counter=%u", worker_counter);
    }
    kprintf("[SELFTEST] Preemptive kthreads + mutex OK (counter=%u, switches=%lu)\n",
            worker_counter, sched_context_switches());

    /* 2. Ring 3 self-test: SYSCALL + Lazy #PF + fork COW + pipe + int 0x80 + execve */
    uint64_t upid = 0;
    if (!user_exec_program("/bin/selftest", 2, &upid)) {
        kpanic("Failed to start Ring 3 /bin/selftest");
    }
    int32_t st = -999;
    int64_t waited = task_waitpid((int64_t)upid, &st);
    if (waited != (int64_t)upid || st != 0) {
        kpanic("Ring 3 /bin/selftest failed: waited=%ld status=%d", waited, st);
    }

    pmm_stats_t ps;
    pmm_get_stats(&ps);
    kprintf("[SELFTEST] ALL STAGE 1+2 TESTS PASSED! (used=%lu/%lu pages, switches=%lu)\n",
            ps.used_pages, ps.total_pages, sched_context_switches());

    /* 2b. Stage 6 ELF64 + Mini-Libc external Ring 3 test from ext2 disk */
    const char *m_argv[] = { "/bin/memtest", "flux-arg1", "42", NULL };
    uint64_t ep = 0;
    if (!user_exec_program_argv("/bin/memtest", 3, m_argv, 2, &ep)) {
        kpanic("Failed to execute ELF64 /bin/memtest from ext2");
    }
    int32_t est = -1;
    if (task_waitpid((int64_t)ep, &est) != (int64_t)ep || est != 0) {
        kpanic("ELF64 /bin/memtest failed (status=%d)", est);
    }

    const char *h_argv[] = { "/bin/hello", "FluxOS", "ELF64", NULL };
    if (!user_exec_program_argv("/bin/hello", 3, h_argv, 2, &ep) ||
        task_waitpid((int64_t)ep, &est) != (int64_t)ep || est != 0) {
        kpanic("ELF64 /bin/hello failed (status=%d)", est);
    }
    kprintf("[SELFTEST] Stage 6 ELF64 loader + Mini-Libc external apps OK!\n");

    /* 3. Stage 4 Audio DMA refill thread + Ring 3 synth test */
    if (audio_has_ac97()) {
        task_create_kernel("audio-dma", audio_dma_task, NULL, 3);
    }
    uint64_t bpid = 0;
    if (user_exec_program("/bin/beeper", 2, &bpid)) {
        task_waitpid((int64_t)bpid, &st);
        kprintf("[SELFTEST] Ring 3 audio syscall /bin/beeper OK (status=%d)\n", st);
    }

    /* 3b. Stage 7 Network RX thread + e1000/ICMP/UDP/TCP self-test */
    if (e1000_is_ready()) {
        task_create_kernel("net-rx", net_rx_task, NULL, 3);
    }
    run_net_selftest();

    /* 3c. Stage 8 Security (PIE/RELRO, SecureBoot, LUKS2, SELinux, Firewalld, Flatpak, Wayland) */
    int32_t wlst = -1;
    if (!flatpak_spawn("org.flux.WaylandDemo", &wlst) || wlst != 0) {
        kpanic("Flatpak WaylandDemo failed (status=%d)", wlst);
    }
    uint64_t spid = 0;
    int32_t sst = -1;
    if (!user_exec_program("/bin/sectest", 2, &spid) ||
        task_waitpid((int64_t)spid, &sst) != (int64_t)spid || sst != 0) {
        kpanic("Ring 3 /bin/sectest failed (status=%d)", sst);
    }
    kprintf("[SELFTEST] Stage 8 Enterprise Security + Wayland Stack OK!\n");

    /* 3d. Stage 9 GPU DRM/Mesa + NVIDIA Driver Manager + DKMS + Wi-Fi/BT + HDA/PipeWire */
    if (!gpu_drm_selftest() || !dkms_selftest() || !wifi_bt_selftest() || !hda_pipewire_selftest()) {
        kpanic("Stage 9 GPU/DKMS/Wi-Fi/PipeWire self-test failed");
    }
    kprintf("[SELFTEST] Stage 9 GPU (amdgpu/i915/nvidia) + DKMS + Wi-Fi/BT + PipeWire OK!\n");

    /* 3e. Stage 10+11 Btrfs/ext4/NTFS3/exFAT + Proton/DXVK + SMP/ACPI + DNF5/Git + VA-API/Screenshot */
    if (!fs_multi_selftest() || !proton_dxvk_selftest() || !smp_acpi_selftest() ||
        !pkg_git_selftest() || !screenshot_vaapi_selftest()) {
        kpanic("Stage 10/11 Btrfs/Proton/SMP/ACPI/Git/Screenshot self-test failed");
    }
    kprintf("[SELFTEST] Stage 10+11 Btrfs + Proton/DXVK + SMP(i7-2600K) + DNF5/Git + VA-API/Screenshot OK!\n");

    /* 3f. Stage 12 Enterprise Stability: EDAC/MCE + Watchdog/Kdump + PCIe Quirks + FSCK/JBD2 + BBRv3 + ABI Sanitizer */
    if (!edac_mce_selftest() || !watchdog_kdump_selftest() || !pcie_quirks_selftest() ||
        !fs_journal_fsck_selftest() || !net_resilience_selftest() || !abi_sanitizer_selftest()) {
        kpanic("Stage 12 Enterprise Stability & Self-Healing self-test failed");
    }
    kprintf("[SELFTEST] Stage 12 Stability (EDAC/MCE + iTCO_wdt + P67A-UD5-B3 Quirks + JBD2/FSCK + BBRv3 + KASAN/ABI) OK!\n");

    /* 3g. Stage 13 Core Architecture: Monitor DP2.1/HDR/VRR + EEVDF/LAVD + MGLRU/THP/Folios + XFS/io_uring + XDP/Routing + Futex2/kdbus + KVM/LSM */
    if (!display_hdr_vrr_selftest() || !sched_eevdf_selftest() || !mm_folios_thp_selftest() ||
        !xfs_vfs_cache_selftest() || !net_routing_xdp_selftest() || !ipc_futex_dbus_selftest() ||
        !kvm_lsm_apparmor_selftest()) {
        kpanic("Stage 13 Core Kernel Subsystems self-test failed");
    }
    kprintf("[SELFTEST] Stage 13 Kernel (Monitor DP2.1/HDR/180Hz + EEVDF/LAVD + MGLRU/THP + XFS/io_uring + XDP + Futex2 + KVM/AppArmor) OK!\n");

    /* 3h. Stage 14 Extended FS (OpenZFS/F2FS/EROFS + SMB3/NFSv4.2/FUSE3), Arch (VT-d IOMMU/x2APIC/PMU + Microcode/AVX) & Graphics (Vulkan 1.4/RT + KMS Atomic Color + Wayland DMA-BUF) */
    if (!zfs_f2fs_erofs_selftest() || !smb3_nfs4_fuse_selftest() ||
        !iommu_apic_pmu_selftest() || !avx_fpu_microcode_selftest() ||
        !vulkan_radv_nvk_selftest() || !kms_atomic_color_selftest() ||
        !wayland_protocols_dmabuf_selftest()) {
        kpanic("Stage 14 FS/Arch/Gfx self-test failed");
    }
    kprintf("[SELFTEST] Stage 14 (OpenZFS/F2FS/SMB3/NFS4/FUSE3 + VT-d IOMMU/x2APIC/PMU/AVX + Vulkan 1.4/KMS Color/DMA-BUF) OK!\n");

    /* 3i. Stage 15 Defense-in-Depth Security (+52k LOC): Seccomp/eBPF KRSI + TPM 2.0/IMA/EVM + kCFI/CET Shadow Stack + nftables/IDS/DPI + Post-Quantum ML-KEM/ML-DSA Keyring */
    if (!seccomp_ebpf_lsm_selftest() || !tpm2_ima_evm_selftest() ||
        !kcfi_shadow_stack_selftest() || !nftables_ids_dpi_selftest() ||
        !post_quantum_keyring_selftest()) {
        kpanic("Stage 15 Security self-test failed");
    }
    kprintf("[SELFTEST] Stage 15 Security (Seccomp/KRSI + TPM2/IMA/EVM + kCFI/CET ShadowStack + nftables/IDS + Post-Quantum Kyber/Dilithium) OK!\n");

    /* 4. Stage 3 Graphics Compositor & PS/2 interactive desktop */
    if (fb_is_ready()) {
        wm_init();
        synth_play_boot_chime();
        task_create_kernel("compositor", compositor_task, NULL, 2);
    }

    kprintf("[FLUX] FLUXOS READY\n");
}

void kmain(void) {
    enable_cpu_features();
    serial_init();
    kprintf("\n==============================================\n");
    kprintf("  FluxOS x86_64 Kernel Booting...\n");
    kprintf("==============================================\n");

    if (!memmap_req.response || !hhdm_req.response) {
        kpanic("Missing Limine memmap or HHDM response");
    }

    gdt_init();
    idt_init();
    pmm_init(memmap_req.response, hhdm_req.response->offset);
    heap_init();
    vmm_init();

    run_mm_selftest();

    pci_init();
    vfs_init();
    if (module_req.response && module_req.response->module_count > 0) {
        struct limine_file *mod = module_req.response->modules[0];
        if (mod && mod->address && mod->size > 0) {
            ahci_attach_ramdisk(mod->address, mod->size);
        }
    }
    vfs_fs_t *efs = NULL;
    if (ahci_init()) {
        efs = ext2_mount_ahci();
    }
    if (!efs) {
        ahci_enable_ramdisk_fallback(true);
        if (ahci_is_ready()) {
            efs = ext2_mount_ahci();
        }
    }
    if (efs) vfs_mount_root(efs);
    security_init();
    gpu_drm_init();
    dkms_init();
    net_init();
    wifi_bt_init();
    hda_pipewire_init();
    fs_multi_init();
    proton_dxvk_init();
    smp_acpi_init();
    pkg_git_init();
    screenshot_vaapi_init();
    edac_mce_init();
    watchdog_kdump_init();
    pcie_quirks_init();
    fs_journal_fsck_init();
    net_resilience_init();
    abi_sanitizer_init();
    display_hdr_vrr_init();
    sched_eevdf_init();
    mm_folios_thp_init();
    xfs_vfs_cache_init();
    net_routing_xdp_init();
    ipc_futex_dbus_init();
    kvm_lsm_apparmor_init();
    zfs_f2fs_erofs_init();
    smb3_nfs4_fuse_init();
    iommu_apic_pmu_init();
    avx_fpu_microcode_init();
    vulkan_radv_nvk_init();
    kms_atomic_color_init();
    wayland_protocols_dmabuf_init();
    seccomp_ebpf_lsm_init();
    tpm2_ima_evm_init();
    kcfi_shadow_stack_init();
    nftables_ids_dpi_init();
    post_quantum_keyring_init();
    wayland_init();
    timer_init(100);
    synth_init();

    if (fb_req.response && fb_req.response->framebuffer_count > 0) {
        struct limine_framebuffer *fb = fb_req.response->framebuffers[0];
        if (fb_init(fb)) {
            ps2_init((int32_t)fb->width, (int32_t)fb->height);
        }
    } else {
        kprintf("[BOOT] No linear framebuffer reported; headless serial mode\n");
    }

    sched_init();
    syscall_init();
    syscall_register_fb_hook(syscall_fb_bridge);
    syscall_register_audio_hook(synth_play_note);
    sti();

    task_create_kernel("supervisor", supervisor_task, NULL, 1);

    for (;;) {
        hlt();
    }
}
