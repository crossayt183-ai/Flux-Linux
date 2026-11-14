@echo off
title FluxOS 0.4 x86_64 Workstation Edition
echo Building and launching FluxOS 0.4 x86_64 in QEMU GUI...
python "%~dp0build.py"
"D:\flux-tools\qemu\qemu-system-x86_64.exe" ^
  -L "D:/flux-tools/qemu/share" ^
  -M q35 -cpu qemu64 -m 256M ^
  -drive "if=pflash,format=raw,readonly=on,file=D:/flux-tools/qemu/share/edk2-x86_64-code.fd" ^
  -drive "if=pflash,format=raw,file=F:/flux-os/build/ovmf_vars.fd" ^
  -drive "format=raw,file=fat:rw:F:/flux-os/build/esp" ^
  -drive "id=disk0,file=F:/flux-os/build/disk.img,format=raw,if=none" ^
  -device "ide-hd,drive=disk0,bus=ide.1" ^
  -netdev user,id=net0 -device e1000,netdev=net0 ^
  -audiodev dsound,id=snd0 -device ac97,audiodev=snd0 ^
  -vga std
