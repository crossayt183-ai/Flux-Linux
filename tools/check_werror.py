import subprocess
from pathlib import Path

root = Path(r"F:\flux-os")
zig = Path(r"D:\flux-tools\zig\zig.exe")
dirs = ["include", "arch/x86_64", "mm", "sync", "sched", "sys", "drivers", "fs", "gfx", "audio", "net"]
incs = [f"-I{root / 'src' / d}" for d in dirs]
cflags = [
    "-target", "x86_64-freestanding-none", "-std=c11", "-O2",
    "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-stack-protector",
    "-fno-pic", "-mcmodel=kernel", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2",
    "-DLIMINE_API_REVISION=2", "-c",
]
fails = []
tmp_o = str(root / "build" / "chk.o")
for cf in sorted((root / "src").rglob("*.c")):
    r = subprocess.run([str(zig), "cc", *cflags, *incs, str(cf), "-o", tmp_o], capture_output=True, text=True)
    if r.returncode != 0:
        fails.append((str(cf), r.stderr))

ucflags = [
    "-target", "x86_64-freestanding-none", "-std=c11", "-O2",
    "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-stack-protector",
    "-fno-pic", "-fno-pie", f"-I{root / 'user' / 'libc' / 'include'}", "-c",
]
for cf in sorted((root / "user").rglob("*.c")):
    r = subprocess.run([str(zig), "cc", *ucflags, str(cf), "-o", tmp_o], capture_output=True, text=True)
    if r.returncode != 0:
        fails.append((str(cf), r.stderr))
Path(tmp_o).unlink(missing_ok=True)

if fails:
    for f, err in fails:
        print("FAIL:", f, err)
    raise SystemExit(1)
print("ALL KERNEL + USERLAND FILES PASSED -Wall -Wextra -Werror WITH ZERO WARNINGS")
