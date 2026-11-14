#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "sys/socket.h"
#include "sys/security.h"

/* Forces DT_RELA R_X86_64_RELATIVE entries in PIE + RELRO binary */
static const char *pie_reloc_ok = "FLUX-PIE-RELOC-VERIFIED";

int main(void) {
    flux_sec_info_t s0;
    if (sec_get_info(&s0) != 0) {
        printf("[ELF/sectest] FAIL: SYS_SEC_INFO\n");
        return 1;
    }

    /* 1. PIE / ASLR + Full RELRO */
    uint64_t code_va = (uint64_t)(uintptr_t)&main;
    if (code_va < 0x40000000ULL || s0.pie_base < 0x40000000ULL ||
        s0.relro_pages == 0 || strcmp(pie_reloc_ok, "FLUX-PIE-RELOC-VERIFIED") != 0) {
        printf("[ELF/sectest] FAIL: PIE/RELRO check (main=%p base=0x%lx relro=%u)\n",
               &main, s0.pie_base, s0.relro_pages);
        return 1;
    }
    printf("[ELF/sectest] PIE/ASLR=0x%lx + RELRO (%u RO pages) + R_X86_64_RELATIVE OK\n",
           s0.pie_base, s0.relro_pages);

    /* 2. UEFI SecureBoot tamper detection */
    if (sec_ctl(SEC_OP_TEST_TAMPER, 0, 0) != 0) {
        printf("[ELF/sectest] FAIL: SecureBoot tamper rejection\n");
        return 1;
    }

    /* 3. LUKS2 encrypted volume R/W & ciphertext != plaintext verification */
    if (sec_ctl(SEC_OP_LUKS_RW_TEST, 0, 0) != 0 || !s0.luks2_unlocked) {
        printf("[ELF/sectest] FAIL: LUKS2 sector cipher verification\n");
        return 1;
    }

    /* 4. Firewalld rule enforcement */
    int ud = socket(AF_INET, SOCK_DGRAM, 0);
    if (ud < 0 || bind(ud, 0, 7777) != 0) {
        printf("[ELF/sectest] FAIL: UDP bind 7777\n");
        return 1;
    }
    sec_ctl(SEC_OP_FW_BLOCK_PORT, 17, 7777);
    flux_sec_info_t fw_before;
    sec_get_info(&fw_before);
    sendto(ud, "FW-DROP", 7, ip4(127, 0, 0, 1), 7777);
    char rxbuf[16];
    uint32_t rip = 0;
    uint16_t rport = 0;
    ssize_t drop_rx = recvfrom(ud, rxbuf, sizeof(rxbuf), &rip, &rport, 40);
    flux_sec_info_t fw_after;
    sec_get_info(&fw_after);
    if (drop_rx >= 0 || fw_after.fw_dropped <= fw_before.fw_dropped) {
        printf("[ELF/sectest] FAIL: Firewalld did not drop UDP/7777 (rx=%ld)\n", drop_rx);
        close(ud);
        return 1;
    }
    sec_ctl(SEC_OP_FW_ALLOW_PORT, 17, 7777);
    sendto(ud, "FW-PASS", 7, ip4(127, 0, 0, 1), 7777);
    ssize_t ok_rx = recvfrom(ud, rxbuf, sizeof(rxbuf), &rip, &rport, 200);
    close(ud);
    if (ok_rx != 7 || memcmp(rxbuf, "FW-PASS", 7) != 0) {
        printf("[ELF/sectest] FAIL: Firewalld allow UDP/7777 failed (%ld)\n", ok_rx);
        return 1;
    }
    printf("[ELF/sectest] SecureBoot + LUKS2 (%s) + Firewalld drop/allow OK\n",
           fw_after.luks_cipher);

    /* 5. Wayland wl_shm check */
    if (fw_after.wl_commits == 0) {
        printf("[ELF/sectest] FAIL: Wayland wl_shm commits == 0\n");
        return 1;
    }

    /* 6. Flatpak + SELinux MAC sandbox confinement check */
    flux_sec_info_t mac_before;
    sec_get_info(&mac_before);
    int64_t cpid = fork();
    if (cpid < 0) {
        printf("[ELF/sectest] FAIL: fork\n");
        return 1;
    }
    if (cpid == 0) {
        sec_ctl(SEC_OP_FLATPAK_CONFINE, FLATPAK_PERM_STDIO, 0);
        int fd_etc = open("/etc/motd", O_WRONLY | O_CREAT);
        if (fd_etc >= 0) {
            close(fd_etc);
            exit(10);
        }
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s >= 0 && bind(s, 0, 9999) == 0) {
            close(s);
            exit(11);
        }
        if (s >= 0) close(s);
        exit(0);
    }
    int32_t cst = -1;
    waitpid(cpid, &cst);
    flux_sec_info_t mac_after;
    sec_get_info(&mac_after);
    if (cst != 0 || mac_after.avc_denied < mac_before.avc_denied + 2) {
        printf("[ELF/sectest] FAIL: SELinux/Flatpak confinement (st=%d denied=%u->%u)\n",
               cst, mac_before.avc_denied, mac_after.avc_denied);
        return 1;
    }

    printf("[ELF/sectest] ALL 7 SECURITY & WAYLAND CHECKS PASSED! (AVC denied=%u, wl_commits=%u)\n",
           mac_after.avc_denied, mac_after.wl_commits);
    return 0;
}
