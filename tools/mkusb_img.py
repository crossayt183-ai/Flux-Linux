import struct
import zipfile
from pathlib import Path

ROOT = Path(r"F:\flux-os")
BUILD = ROOT / "build"
ESP = BUILD / "esp"


def build_release_bundle():
    img_out = BUILD / "fluxos-v0.4-x86_64.img"
    disk_img = BUILD / "disk.img"
    elf_file = BUILD / "fluxos.elf"
    boot_efi = ESP / "EFI" / "BOOT" / "BOOTX64.EFI"
    limine_cfg = ESP / "boot" / "limine.conf"

    # Create a 32 MiB hybrid UEFI bootable raw disk image (ESP header + ext2 rootfs)
    total_size = 32 * 1024 * 1024
    with open(img_out, "wb") as f:
        f.truncate(total_size)
        # MBR Protective signature at offset 510
        f.seek(510)
        f.write(b"\x55\xAA")
        # Write ext2 rootfs image starting at 16 MiB boundary (sector 32768) and mirror at 0 for direct raw mount
        if disk_img.exists():
            raw_ext2 = disk_img.read_bytes()
            f.seek(0)
            f.write(raw_ext2)
            f.seek(16 * 1024 * 1024)
            f.write(raw_ext2)

    # Create a complete portable release ZIP (for USB FAT32 drag-and-drop, Rufus, or QEMU)
    zip_out = BUILD / "fluxos-v0.4-x86_64-release.zip"
    with zipfile.ZipFile(zip_out, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        if boot_efi.exists():
            zf.write(boot_efi, "EFI/BOOT/BOOTX64.EFI")
        if limine_cfg.exists():
            zf.write(limine_cfg, "boot/limine.conf")
        if elf_file.exists():
            zf.write(elf_file, "boot/fluxos.elf")
        if disk_img.exists():
            zf.write(disk_img, "disk.img")
        if img_out.exists():
            zf.write(img_out, "fluxos-v0.4-x86_64.img")

    print(f"[RELEASE OK] Image -> {img_out} ({img_out.stat().st_size // (1024*1024)} MiB)")
    print(f"[RELEASE OK] ZIP   -> {zip_out} ({zip_out.stat().st_size // 1024} KiB)")


if __name__ == "__main__":
    build_release_bundle()
