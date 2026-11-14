# FluxOS v0.4 (`x86_64`)

A modular bare-metal `x86_64` operating system kernel written in freestanding C11 + GNU/Intel x86_64 Assembly, booted via **Limine v8** (`UEFI`).

## Architecture & Subsystems

1. **Memory & Isolation (`src/mm/`)**
   - `pmm.c`: Physical frame allocator with bitmap + 16-bit per-page reference counts (`COW` sharing).
   - `heap.c`: O(1) Slab allocator (`16B..2048B` caches) + multi-page large block allocator (`kmalloc`, `kcalloc`, `krealloc`, `kfree`).
   - `vmm.c`: 4-level paging (`CR3`), HHDM physical window, per-process VMAs, MMIO BAR mapping (`vmm_map_mmio`), and `#PF` handler supporting **lazy demand-zero allocation** and **Copy-on-Write (`PTE_COW`)**.

2. **Preemptive Multitasking, Ring 3 & Syscalls (`src/sched/`, `src/sync/`, `src/sys/`)**
   - `timer.c` + `task.c` + `switch.S`: Preemptive 100 Hz PIT scheduler with priority Round-Robin (`0..3`), `fxsave`/`fxrstor` FPU/SSE context switching, `spinlock.h`, `mutex.c`, SELinux security context (`sec_ctx`), and Flatpak sandbox capability bitmask (`sandbox_perms`).
   - `syscall_entry.S` + `syscall.c`: `SYSCALL`/`SYSRET` + `int 0x80` dispatch (`44` syscalls covering `fork`, `execve`, `waitpid`, `mmap`, `munmap`, `brk`, `pipe`, `dup2`, VFS I/O, audio, BSD sockets, Wayland `wl_shm`, and security controls).
   - `user_demos_asm.S` / `user_demos.c`: Built-in Ring 3 (`CPL=3`) diagnostics and SecureBoot-verified ELF loader bridge.

3. **PCI, AHCI SATA DMA & VFS/ext2 (`src/drivers/pci.c`, `src/drivers/ahci.c`, `src/fs/`)**
   - `pci.c`: PCI config-space enumerator (`pci_find_class`, `pci_get_bar_addr`).
   - `ahci.c`: Multi-drive Intel ICH9 AHCI SATA DMA block driver with 64 KB DMA bounce buffer.
   - `vfs.c` + `ext2.c`: Root mount abstraction, 64-slot LRU inode cache, full R/W `ext2` driver (direct + singly-indirect blocks, directories, bitmaps), and `tools/mkext2.py` disk builder (generating `/etc/secboot.db`, `/etc/selinux.conf`, `/etc/flatpak.db`).

4. **PIE/ASLR ELF64 Loader & Userland Mini-Libc (`src/sys/elf.c`, `user/libc/`, `user/apps/`)**
   - `elf.c`: Position-independent `ET_DYN` + `ET_EXEC` ELF64 loader with ASLR base randomization (`0x40000000ULL + slide`), `DT_RELA` `R_X86_64_RELATIVE` relocation processing, `PT_GNU_RELRO` read-only PTE enforcement, and System V AMD64 `argc`/`argv` user stack setup.
   - `user/libc/`: Freestanding `-fPIE` Ring 3 C library (`crt0.S`, `stdio`, `stdlib` with `sbrk`/`malloc`/`free`, `string`, `unistd`, `sys/socket`, `sys/security`).
   - `user/apps/`: External PIE+RELRO (`-pie -z relro -z now`) ELF64 apps compiled into `/bin/` (`hello`, `ls`, `cat`, `echo`, `memtest`, `ping`, `nettest`, `wldemo`, `sectest`).

5. **Wayland Compositor & Libadwaita Desktop (`src/gfx/`, `src/drivers/ps2.c`)**
   - `fb.c` + `font.c`: 1024x768 32bpp GOP double-buffered framebuffer, alpha blending, rounded rectangles, gradients, 24-bit wallpaper blitting (`/etc/wall.rgb`), 8x16 monospace bitmap font.
   - `wayland.c`: Kernel shared-memory (`wl_shm`) surface protocol mapping `XRGB8888` pixel buffers into Ring 3 (`0x3000000000ULL`) with `SYS_WL_CREATE`/`SYS_WL_COMMIT`.
   - `ps2.c` + `wm.c`: 8042 PS/2 keyboard (`IRQ1`) & mouse (`IRQ12`) driver feeding a dark Libadwaita compositor with apps: **Security & Wayland Center** (live `wl_shm` preview + SELinux/Firewalld/LUKS2/SecureBoot/PIE/Flatpak telemetry), **Flux Files** (`ext2` browser), **FluxOS Setup**, **FluxShell Terminal**, **System Monitor**, and **FluxSynth Studio**.

6. **Network Stack, Firewalld & BSD Sockets (`src/drivers/e1000.c`, `src/net/`)**
   - `e1000.c`: Intel 82540EM (`0x100E`) / 82574L (`0x10D3`) MMIO NIC DMA driver.
   - `net.c` + `eth_arp.c` + `ipv4_icmp.c` + `udp.c` + `tcp.c` + `socket.c` + `firewall.c`: Ethernet II, ARP, IPv4 + loopback (`10.0.2.15/24`, `127.0.0.1`), ICMP ping, UDP, stateful RFC 793 TCP, BSD sockets, and **Firewalld** zone-based packet filter (`drop`, `public`, `trusted`) hooked into `ipv4_rx()`.

7. **Enterprise Security Stack (`src/sec/security.c`)**
   - **SecureBoot**: FIPS 180-4 SHA-256 + HMAC-SHA256 image authentication verifying `/bin/*` ELFs against `/etc/secboot.db` before execution.
   - **LUKS2 Vault**: 4096-byte `LUKS\xba\xbe` volume header with JSON metadata, PBKDF2-HMAC-SHA256 key derivation (`aes-xts-plain64`), and encrypted 64-sector block store.
   - **SELinux (`FluxMAC`)**: Security contexts (`user:role:type:level`) on `task_t`, AVC hooks in `sys_open`/`SYS_MKDIR`/`SYS_UNLINK`/`SYS_BIND`/`SYS_CONNECT`/`SYS_SENDTO`, enforcing/permissive toggle, and audit logs.
   - **Flatpak (`FluxPak`)**: Manifest registry (`org.flux.Hello`, `org.flux.NetCheck`, `org.flux.WaylandDemo`), capability bitmask (`FLATPAK_PERM_*`), and automatic SELinux domain transition (`flatpak_u:sandbox_r:flatpak_t:s0`).

8. **Audio Driver & 4-Voice Synthesizer (`src/drivers/audio.c`, `src/audio/synth.c`)**
   - `audio.c`: Intel 82801AA AC'97 48 kHz 16-bit stereo DMA driver with PC Speaker fallback.
   - `synth.c`: 4-voice polyphonic oscillator (`sine`, `square`, `saw`, `triangle`) with ADSR envelope.

## Build & Run (Windows w/ Zig + QEMU in `D:\flux-tools`)

```powershell
# Compile + headless serial boot verification
python F:\flux-os\build.py --run-headless

# Compile + interactive GUI & AC'97 audio window
python F:\flux-os\build.py --run-gui
```
