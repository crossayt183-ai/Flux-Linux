import os
import sys
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(r"F:\flux-os")
SRC = ROOT / "src"
BUILD = ROOT / "build"
OBJ = BUILD / "obj"
ESP = BUILD / "esp"

ZIG = Path(r"D:\flux-tools\zig\zig.exe")
QEMU = Path(r"D:\flux-tools\qemu\qemu-system-x86_64.exe")
OVMF_CODE = Path(r"D:\flux-tools\qemu\share\edk2-x86_64-code.fd")
OVMF_VARS = Path(r"D:\flux-tools\qemu\share\edk2-i386-vars.fd")
LIMINE_EFI = Path(r"D:\flux-tools\limine\BOOTX64.EFI")

INCLUDES = [
    SRC / "include",
    SRC / "arch" / "x86_64",
    SRC / "mm",
    SRC / "sync",
    SRC / "sched",
    SRC / "sys",
    SRC / "drivers",
    SRC / "fs",
    SRC / "gfx",
    SRC / "audio",
    SRC / "net",
    SRC / "sec",
]

CFLAGS = [
    "-target", "x86_64-freestanding-none",
    "-std=c11",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror=implicit-function-declaration",
    "-ffreestanding",
    "-fno-stack-protector",
    "-fno-pic",
    "-mcmodel=kernel",
    "-mno-red-zone",
    "-mno-mmx",
    "-mno-sse",
    "-mno-sse2",
    "-DLIMINE_API_REVISION=2",
]

ASFLAGS = [
    "-target", "x86_64-freestanding-none",
    "-c",
]


def build():
    if not ZIG.exists():
        raise FileNotFoundError(f"Missing Zig compiler: {ZIG}")

    OBJ.mkdir(parents=True, exist_ok=True)
    inc_flags = [f"-I{p}" for p in INCLUDES if p.exists()]

    c_files = sorted(SRC.rglob("*.c"))
    s_files = sorted(SRC.rglob("*.S"))
    objs = []

    for src_file in c_files:
        rel = src_file.relative_to(SRC)
        out_o = OBJ / (str(rel).replace("\\", "_").replace("/", "_") + ".o")
        cmd = [str(ZIG), "cc", *CFLAGS, *inc_flags, "-c", str(src_file), "-o", str(out_o)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[CC FAIL] {rel}:\n{res.stderr}")
            sys.exit(1)
        objs.append(str(out_o))

    for asm_file in s_files:
        rel = asm_file.relative_to(SRC)
        out_o = OBJ / (str(rel).replace("\\", "_").replace("/", "_") + ".o")
        cmd = [str(ZIG), "cc", *ASFLAGS, str(asm_file), "-o", str(out_o)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[AS FAIL] {rel}:\n{res.stderr}")
            sys.exit(1)
        objs.append(str(out_o))

    elf_out = BUILD / "fluxos.elf"
    ld_cmd = [
        str(ZIG), "ld.lld",
        "-T", str(ROOT / "linker.ld"),
        "-nostdlib", "-static", "-z", "max-page-size=0x1000",
        *objs,
        "-o", str(elf_out),
    ]
    res = subprocess.run(ld_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[LD FAIL]:\n{res.stderr}")
        sys.exit(1)

    # Stage UEFI ESP directory for Limine + QEMU fat:rw
    efi_boot = ESP / "EFI" / "BOOT"
    boot_dir = ESP / "boot"
    efi_boot.mkdir(parents=True, exist_ok=True)
    boot_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(LIMINE_EFI, efi_boot / "BOOTX64.EFI")
    shutil.copy2(ROOT / "limine.conf", boot_dir / "limine.conf")
    shutil.copy2(elf_out, boot_dir / "fluxos.elf")

    USER_SRC = ROOT / "user"
    USER_LIBC = USER_SRC / "libc"
    USER_BUILD = BUILD / "user"
    USER_BUILD.mkdir(parents=True, exist_ok=True)
    user_cflags = [
        "-target", "x86_64-freestanding-none",
        "-std=c11", "-O2", "-Wall", "-Wextra",
        "-Werror=implicit-function-declaration",
        "-ffreestanding", "-fno-stack-protector", "-fPIE",
        f"-I{USER_LIBC / 'include'}",
    ]
    libc_objs = []
    for cf in sorted((USER_LIBC / "src").glob("*.c")):
        uo = USER_BUILD / f"libc_{cf.stem}.o"
        res = subprocess.run([str(ZIG), "cc", *user_cflags, "-c", str(cf), "-o", str(uo)],
                             capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[USER LIBC CC FAIL] {cf.name}:\n{res.stderr}")
            sys.exit(1)
        libc_objs.append(str(uo))
    for sf in sorted((USER_LIBC / "src").glob("*.S")):
        uo = USER_BUILD / f"libc_{sf.stem}.o"
        res = subprocess.run([str(ZIG), "cc", *ASFLAGS, str(sf), "-o", str(uo)],
                             capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[USER LIBC AS FAIL] {sf.name}:\n{res.stderr}")
            sys.exit(1)
        libc_objs.append(str(uo))

    user_bins = []
    for app_c in sorted((USER_SRC / "apps").glob("*.c")):
        app_o = USER_BUILD / f"{app_c.stem}.o"
        app_elf = USER_BUILD / f"{app_c.stem}.elf"
        res = subprocess.run([str(ZIG), "cc", *user_cflags, "-c", str(app_c), "-o", str(app_o)],
                             capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[USER APP CC FAIL] {app_c.name}:\n{res.stderr}")
            sys.exit(1)
        res = subprocess.run([
            str(ZIG), "ld.lld", "-pie", "-z", "relro", "-z", "now",
            "-nostdlib", "-z", "max-page-size=0x1000",
            *libc_objs, str(app_o), "-o", str(app_elf)
        ], capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[USER APP LD FAIL] {app_c.name}:\n{res.stderr}")
            sys.exit(1)
        user_bins.append(f"/bin/{app_c.stem}={app_elf}")

    disk_img = BUILD / "disk.img"
    res = subprocess.run([sys.executable, str(ROOT / "tools" / "mkext2.py"), str(disk_img), *user_bins],
                         capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[DISK FAIL]:\n{res.stderr}")
        sys.exit(1)
    shutil.copy2(disk_img, boot_dir / "disk.img")

    total_loc = sum(len(f.read_text(encoding="utf-8", errors="ignore").splitlines())
                    for f in [*c_files, *s_files, *sorted(SRC.rglob("*.h"))])
    print(f"[BUILD OK] {len(c_files)} C + {len(s_files)} ASM files ({total_loc} LOC) -> {elf_out}")


def run_qemu(headless: bool, timeout_sec: int = 60):
    if not QEMU.exists() or not OVMF_CODE.exists():
        raise FileNotFoundError("QEMU or OVMF firmware not found in D:\\flux-tools\\qemu")

    vars_copy = BUILD / "ovmf_vars.fd"
    if not vars_copy.exists():
        shutil.copy2(OVMF_VARS, vars_copy)

    serial_log = BUILD / "serial.log"
    if serial_log.exists():
        serial_log.unlink()

    esp_posix = str(ESP).replace("\\", "/")
    cmd = [
        str(QEMU),
        "-L", str(QEMU.parent / "share").replace("\\", "/"),
        "-M", "q35",
        "-cpu", "qemu64",
        "-m", "256M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={str(OVMF_CODE).replace(chr(92), '/')}",
        "-drive", f"if=pflash,format=raw,file={str(vars_copy).replace(chr(92), '/')}",
        "-drive", f"format=raw,file=fat:rw:{esp_posix}",
        "-drive", f"id=disk0,file={str(BUILD / 'disk.img').replace(chr(92), '/')},format=raw,if=none",
        "-device", "ide-hd,drive=disk0,bus=ide.1",
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-audiodev", f"{'none' if headless else 'dsound'},id=snd0",
        "-device", "ac97,audiodev=snd0",
        "-serial", f"file:{serial_log}" if headless else "stdio",
        "-no-reboot",
        "-vga", "std",
    ]
    if headless:
        cmd.extend(["-display", "none"])

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    deadline = time.time() + timeout_sec
    last_len = 0
    found_pass = False
    found_panic = False

    while time.time() < deadline:
        time.sleep(0.4)
        if serial_log.exists():
            txt = serial_log.read_text(encoding="utf-8", errors="replace")
            if len(txt) > last_len:
                last_len = len(txt)
            if "PANIC" in txt or "Unhandled Exception" in txt or "TEST FAILED" in txt:
                found_panic = True
                break
            if "FLUXOS READY" in txt:
                found_pass = True
                time.sleep(0.5)
                break
        if proc.poll() is not None:
            break

    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    err_txt = proc.stderr.read().decode(errors="replace").strip()
    if err_txt:
        print("===== QEMU STDERR =====")
        print(err_txt)

    if serial_log.exists():
        print("===== QEMU SERIAL OUTPUT =====")
        print(serial_log.read_text(encoding="utf-8", errors="replace"))
        print("==============================")
    else:
        print(f"[QEMU] No serial.log produced! (rc={proc.returncode})")

    if found_panic or (headless and not found_pass):
        sys.exit(1)


if __name__ == "__main__":
    build()
    if "--run-headless" in sys.argv:
        run_qemu(headless=True)
    elif "--run-gui" in sys.argv:
        run_qemu(headless=False, timeout_sec=60)
