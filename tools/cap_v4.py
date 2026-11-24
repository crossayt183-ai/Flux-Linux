import socket
import subprocess
import time
from pathlib import Path

ROOT = Path(r"F:\flux-os")
BUILD = ROOT / "build"
QEMU = Path(r"D:\flux-tools\qemu\qemu-system-x86_64.exe")
OVMF_CODE = Path(r"D:\flux-tools\qemu\share\edk2-x86_64-code.fd")
OVMF_VARS = BUILD / "ovmf_vars.fd"
ESP = str(BUILD / "esp").replace("\\", "/")
DISK = str(BUILD / "disk.img").replace("\\", "/")
SER = BUILD / "serial_cap.log"
ART = Path(r"C:\Users\Артем\.gemini\antigravity\brain\59305db6-e9cc-498b-a929-ab95b09d1734")
PPM = BUILD / "flux_v4.ppm"
PNG = ART / "fluxos_security_v4.png"

if SER.exists():
    SER.unlink()
if PPM.exists():
    PPM.unlink()

cmd = [
    str(QEMU),
    "-L", str(QEMU.parent / "share").replace("\\", "/"),
    "-M", "q35", "-cpu", "qemu64", "-m", "256M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={str(OVMF_CODE).replace(chr(92), '/')}",
    "-drive", f"if=pflash,format=raw,file={str(OVMF_VARS).replace(chr(92), '/')}",
    "-drive", f"format=raw,file=fat:rw:{ESP}",
    "-drive", f"id=disk0,file={DISK},format=raw,if=none",
    "-device", "ide-hd,drive=disk0,bus=ide.1",
    "-netdev", "user,id=net0",
    "-device", "e1000,netdev=net0",
    "-audiodev", "none,id=snd0",
    "-device", "ac97,audiodev=snd0",
    "-serial", f"file:{SER}",
    "-monitor", "tcp:127.0.0.1:4445,server,nowait",
    "-display", "none",
    "-no-reboot",
]

p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
ready = False
for _ in range(90):
    time.sleep(0.5)
    if SER.exists():
        txt = SER.read_text(encoding="utf-8", errors="replace")
        if "FLUXOS READY" in txt:
            ready = True
            break

time.sleep(1.2)
try:
    with socket.create_connection(("127.0.0.1", 4445), timeout=5) as s:
        s.sendall(f"screendump {PPM.as_posix()}\n".encode("ascii"))
        time.sleep(0.8)
        s.sendall(b"quit\n")
        time.sleep(0.4)
finally:
    p.terminate()
    try:
        p.wait(timeout=5)
    except Exception:
        p.kill()

# Regenerate pristine disk.img after boot so workspace stays deterministic
data = PPM.read_bytes()
idx = 0
tokens = []
while len(tokens) < 4:
    while idx < len(data) and data[idx] in b" \t\r\n":
        idx += 1
    if data[idx:idx+1] == b"#":
        while idx < len(data) and data[idx] not in b"\r\n":
            idx += 1
        continue
    start = idx
    while idx < len(data) and data[idx] not in b" \t\r\n":
        idx += 1
    tokens.append(data[start:idx].decode("ascii"))
idx += 1
magic, w, h, maxv = tokens[0], int(tokens[1]), int(tokens[2]), int(tokens[3])
assert magic == "P6" and maxv == 255
rgb = data[idx:]
assert len(rgb) == w * h * 3

try:
    from PIL import Image
    Image.frombytes("RGB", (w, h), rgb).save(PNG)
except ImportError:
    import zlib, struct
    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    raw_rows = b"".join(b"\x00" + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw_rows, 6))
        + chunk(b"IEND", b"")
    )
    PNG.write_bytes(png)

if PPM.exists():
    PPM.unlink()
if SER.exists():
    SER.unlink()
print(f"[CAP OK] ready={ready} {w}x{h} -> {PNG}")
