import socket
import subprocess
import time
from pathlib import Path
from PIL import Image

build = Path(r"F:\flux-os\build")
art = Path(r"C:\Users\Артем\.gemini\antigravity\brain\59305db6-e9cc-498b-a929-ab95b09d1734")
ser = build / "mon_serial.log"
ppm = build / "shot.ppm"
for p in (ser, ppm):
    if p.exists():
        p.unlink()

vars_fd = (build / "ovmf_vars.fd").as_posix()
esp_dir = (build / "esp").as_posix()
disk_img = (build / "disk.img").as_posix()

cmd = [
    r"D:\flux-tools\qemu\qemu-system-x86_64.exe",
    "-L", "D:/flux-tools/qemu/share",
    "-M", "q35", "-cpu", "qemu64", "-m", "256M",
    "-drive", "if=pflash,format=raw,readonly=on,file=D:/flux-tools/qemu/share/edk2-x86_64-code.fd",
    "-drive", f"if=pflash,format=raw,file={vars_fd}",
    "-drive", f"format=raw,file=fat:rw:{esp_dir}",
    "-drive", f"id=disk0,file={disk_img},format=raw,if=none",
    "-device", "ide-hd,drive=disk0,bus=ide.1",
    "-netdev", "user,id=net0", "-device", "e1000,netdev=net0",
    "-audiodev", "none,id=snd0", "-device", "ac97,audiodev=snd0",
    "-serial", f"file:{ser}",
    "-monitor", "tcp:127.0.0.1:4555,server,nowait",
    "-display", "none", "-no-reboot", "-vga", "std",
]

proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
try:
    t0 = time.time()
    ready = False
    while time.time() - t0 < 55:
        if proc.poll() is not None:
            break
        if ser.exists() and "FLUXOS READY" in ser.read_text(encoding="utf-8", errors="replace"):
            ready = True
            break
        time.sleep(0.4)
    if not ready:
        proc.kill()
        out, err = proc.communicate(timeout=3)
        stxt = ser.read_text(encoding="utf-8", errors="replace") if ser.exists() else "<no ser>"
        raise RuntimeError(f"Not ready (rc={proc.returncode})\nSER:\n{stxt}\nERR:\n{err.decode(errors='replace')}")

    time.sleep(1.0)
    with socket.create_connection(("127.0.0.1", 4555), timeout=5) as s:
        time.sleep(0.2)
        keymap = {" ": "spc", "/": "slash", ".": "dot", "\n": "ret", "-": "minus"}
        for ch in "ifconfig\ndf\nping 10.0.2.2\n":
            k = keymap.get(ch, ch)
            s.sendall(f"sendkey {k}\n".encode())
            time.sleep(0.06)
        time.sleep(1.0)
        s.sendall(f"screendump {ppm.as_posix()}\n".encode())
        time.sleep(0.8)
        s.sendall(b"quit\n")
finally:
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    if ser.exists():
        ser.unlink()

img = Image.open(ppm)
out_png = art / "fluxos_desktop_v3.png"
img.save(out_png)
ppm.unlink()
print("Saved:", out_png, img.size)
