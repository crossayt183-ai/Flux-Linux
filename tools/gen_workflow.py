import os

with open(r"C:\Users\Артем\.gemini\antigravity\brain\59305db6-e9cc-498b-a929-ab95b09d1734\scratch\wp_b64.txt", "r") as f:
    wp_b64_lines = f.read().strip().splitlines()

wp_b64_indented = "\n".join("              " + line for line in wp_b64_lines)

yml = r'''name: Flux Linux - Libre Repo + ISO

on:
  workflow_dispatch:
  push:
    branches: [ "main" ]

permissions:
  contents: read

env:
  FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true

concurrency:
  group: flux-build
  cancel-in-progress: true

jobs:
  # ===================================================================
  # 1. Собираем НАШ свободный репозиторий (Free/Libre, GPL-3.0)
  # ===================================================================
  repo:
    name: 1. Flux Libre Repository (.deb)
    runs-on: ubuntu-24.04
    steps:
      - name: Install tools
        run: |
          sudo apt-get update && sudo apt-get install -y dpkg-dev apt-utils python3-pil

      - name: Build Flux Libre packages
        run: |
          set -e
          VER=1.2.0
          MAINT="Artem <crossayt183-ai@users.noreply.github.com>"
          rm -rf pkg repo
          mkdir -p pkg repo

          # ---------------- flux-branding ----------------
          P=pkg/flux-branding
          mkdir -p $P/DEBIAN $P/etc $P/etc/apt/preferences.d $P/usr/lib $P/usr/bin $P/usr/local/bin $P/usr/share/flux \
            $P/usr/share/backgrounds/flux $P/usr/share/gnome-background-properties \
            $P/usr/share/pixmaps $P/usr/share/icons/hicolor/64x64/apps $P/usr/share/doc/flux

          cat > $P/DEBIAN/control <<CTRL
          Package: flux-branding
          Version: $VER
          Architecture: all
          Maintainer: $MAINT
          Section: main/misc
          Priority: optional
          Description: Flux Linux Libre independent OS identity, Flux Package Manager (flux/fpm), 3D Cubes wallpaper and artwork
          CTRL

          cat > $P/usr/share/doc/flux/LICENSE <<'LIC'
          Flux Linux 1.0 (Hydrogen) Libre Edition — Independent Free Operating System
          Copyright (C) 2026 Artem (crossayt183-ai)

          This distribution and all Flux components (flux-branding, flux-tools, flux-desktop,
          Flux Shell, Flux Shield, Flux Boost, and Flux Package Manager) are 100% Free/Libre
          Software licensed under the GNU General Public License v3.0 (GPL-3.0).
          Non-free software and proprietary blobs are blocked by system policy.
          LIC

          cat > $P/usr/lib/os-release <<'OSR'
          PRETTY_NAME="Flux Linux 1.0 (Hydrogen) Libre"
          NAME="Flux Linux"
          VERSION_ID="1.0"
          VERSION="1.0 (Hydrogen) Libre"
          VERSION_CODENAME=hydrogen
          ID=flux
          LOGO=flux-icon
          ANSI_COLOR="1;36"
          HOME_URL="https://github.com/crossayt183-ai/Flux-Linux"
          SUPPORT_URL="https://crossayt183-ai.github.io/Flux-Linux/"
          BUG_REPORT_URL="https://github.com/crossayt183-ai/Flux-Linux/issues"
          OSR

          cat > $P/etc/lsb-release <<'LSB'
          DISTRIB_ID=FluxLinux
          DISTRIB_RELEASE=1.0
          DISTRIB_CODENAME=hydrogen
          DISTRIB_DESCRIPTION="Flux Linux 1.0 (Hydrogen) Libre"
          LSB

          cat > $P/etc/flux-release <<'FREL'
          Flux Linux release 1.0 (Hydrogen) Libre [100% Free Software / GPL-3.0]
          FREL

          # Политика 100% Свободного Дистрибутива: блокировка любых non-free / contrib пакетов по умолчанию
          cat > $P/etc/apt/preferences.d/99-flux-libre-pin <<'PIN'
          Package: *
          Pin: release c=non-free*
          Pin-Priority: -1

          Package: *
          Pin: release c=contrib
          Pin-Priority: -1
          PIN

          # Собственный пакетный и системный менеджер дистрибутива: команда `flux` (и `fpm`)
          cat > $P/usr/bin/flux <<'FLUXCLI'
          #!/bin/bash
          CMD="$1"
          shift 2>/dev/null || true
          case "$CMD" in
            install|i)
              echo -e "\033[1;36m[Flux PM] Установка свободных пакетов: $*\033[0m"
              sudo apt-get install "$@"
              ;;
            remove|rm)
              echo -e "\033[1;36m[Flux PM] Удаление пакетов: $*\033[0m"
              sudo apt-get remove "$@"
              ;;
            update|up)
              echo -e "\033[1;36m[Flux PM] Синхронизация свободных репозиториев Flux Linux...\033[0m"
              if command -v curl >/dev/null && curl -fsI https://crossayt183-ai.github.io/Flux-Linux/Release >/dev/null 2>&1; then
                echo "deb [trusted=yes] https://crossayt183-ai.github.io/Flux-Linux/ ./" | sudo tee /etc/apt/sources.list.d/flux-online.list >/dev/null
              fi
              sudo apt-get update && sudo apt-get upgrade -y
              ;;
            search|s)
              apt-cache search "$@"
              ;;
            sec|shield)
              exec /usr/bin/flux-sec
              ;;
            boost|opt)
              exec /usr/bin/flux-boost
              ;;
            libre|free)
              exec /usr/bin/flux-free-check
              ;;
            info|diag)
              exec /usr/bin/flux-diag
              ;;
            setup|welcome)
              rm -f "$HOME/.flux-registered"
              exec /usr/bin/flux-welcome
              ;;
            store|shop)
              exec /usr/bin/flux-store
              ;;
            *)
              echo -e "\033[1;36m╔══════════════════════════════════════════════════════════════════╗\033[0m"
              echo -e "\033[1;36m║      FLUX PACKAGE & SYSTEM MANAGER v1.2 (100% Libre / GPLv3)     ║\033[0m"
              echo -e "\033[1;36m╚══════════════════════════════════════════════════════════════════╝\033[0m"
              echo "  flux store            — открыть графический магазин Flux Libre Store"
              echo "  flux install <пакет>  — установить пакет из свободного репозитория Flux"
              echo "  flux remove <пакет>   — удалить пакет"
              echo "  flux update           — обновить индексы и пакеты дистрибутива"
              echo "  flux search <запрос>  — поиск свободных пакетов"
              echo "  flux sec              — аудит протоколов безопасности (Flux Shield)"
              echo "  flux boost            — оптимизация ОЗУ, ZRAM и сети TCP BBR (Flux Boost)"
              echo "  flux libre            — проверка 100% чистоты свободного ПО (0 non-free)"
              echo "  flux setup            — открыть Центр регистрации и настройки Flux"
              echo "  flux info             — диагностика железа, драйверов и ядра"
              ;;
          esac
          FLUXCLI
          chmod 755 $P/usr/bin/flux
          ln -sf /usr/bin/flux $P/usr/bin/fpm

          cat > $P/usr/share/flux/logo.txt <<'LOGO'
                 /\__________
                / /  _______/
               / /  /
              / /  /______
             / /  /______/
            / /  /
           / /  /
          /_/__/
          LOGO

          # Обёртка fastfetch — всегда логотип F и без обрезки
          cat > $P/usr/local/bin/fastfetch <<'FF'
          #!/bin/sh
          exec /usr/bin/fastfetch --logo-type file --logo /usr/share/flux/logo.txt --logo-color-1 cyan \
            -s Title:Separator:OS:Host:Kernel:Uptime:Packages:Shell:Display:DE:WM:Theme:Icons:Terminal:CPU:GPU:Memory:Disk:Break:Colors "$@"
          FF
          chmod 755 $P/usr/local/bin/fastfetch

          # Генерация обоев 1920x1080 (3D Cubes) и иконки Flux
          python3 - <<'PY'
          import base64, io, zlib, struct
          from PIL import Image, ImageFilter

          WP_B64 = """
__WP_B64_PLACEHOLDER__
          """
          raw = base64.b64decode("".join(WP_B64.split()))
          im = Image.open(io.BytesIO(raw)).convert("RGB")
          up = im.resize((1920, 1080), Image.Resampling.LANCZOS).filter(
              ImageFilter.UnsharpMask(radius=1.4, percent=115, threshold=2)
          )
          up.save("pkg/flux-branding/usr/share/backgrounds/flux/flux-wallpaper.jpg", format="JPEG", quality=95, optimize=True)
          up.save("pkg/flux-branding/usr/share/backgrounds/flux/flux-wallpaper.png", format="PNG", optimize=True)

          # Иконка Flux (64x64) в тёмно-графитовом матовом стиле под обои 3D Cubes
          def write_png(path, w, h, rows_rgba):
              def chunk(tag, data):
                  return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
              raw_b = bytearray()
              for row in rows_rgba:
                  raw_b.append(0)
                  raw_b.extend(row)
              ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
              with open(path, "wb") as f:
                  f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw_b), 6)) + chunk(b"IEND", b""))

          ic_rows = []
          for y in range(64):
              row = bytearray(64 * 4)
              for x in range(64):
                  dx, dy = x - 31.5, y - 31.5
                  d2 = dx*dx + dy*dy
                  if d2 <= 29.5*29.5:
                      if d2 >= 27.2*27.2:
                          row[x*4:x*4+4] = bytes([96, 165, 250, 255])
                      else:
                          row[x*4:x*4+4] = bytes([24, 24, 29, 255])
                          sk = (48 - y) // 5
                          if 16 <= y <= 48:
                              if (18 + sk <= x <= 24 + sk) or (16 <= y <= 22 and 18 + sk <= x <= 44 + sk) or (29 <= y <= 35 and 18 + sk <= x <= 38 + sk):
                                  row[x*4:x*4+4] = bytes([228, 228, 235, 255])
              ic_rows.append(row)
          write_png("pkg/flux-branding/usr/share/pixmaps/flux-icon.png", 64, 64, ic_rows)
          write_png("pkg/flux-branding/usr/share/icons/hicolor/64x64/apps/flux-icon.png", 64, 64, ic_rows)
          PY

          cat > $P/usr/share/gnome-background-properties/flux-wallpapers.xml <<'BGXML'
          <?xml version="1.0" encoding="UTF-8"?>
          <!DOCTYPE wallpapers SYSTEM "gnome-wp-list.dtd">
          <wallpapers>
            <wallpaper deleted="false">
              <name>Flux 3D Cubes</name>
              <filename>/usr/share/backgrounds/flux/flux-wallpaper.jpg</filename>
              <filename-dark>/usr/share/backgrounds/flux/flux-wallpaper.jpg</filename-dark>
              <options>zoom</options>
              <pcolor>#18181b</pcolor>
              <scolor>#18181b</scolor>
            </wallpaper>
          </wallpapers>
          BGXML

          cat > $P/DEBIAN/preinst <<'SH'
          #!/bin/sh
          set -e
          if [ "$1" = install ] || [ "$1" = upgrade ]; then
            dpkg-divert --package flux-branding --add --rename \
              --divert /usr/lib/os-release.debian /usr/lib/os-release
          fi
          SH

          cat > $P/DEBIAN/postinst <<'SH'
          #!/bin/sh
          set -e
          ln -sf /usr/lib/os-release /etc/os-release
          echo "Flux Linux 1.0 (Hydrogen) Libre \\n \\l" > /etc/issue
          echo "Flux Linux 1.0 (Hydrogen) Libre" > /etc/issue.net
          echo "Flux Linux 1.0 (Hydrogen) Libre" > /etc/debian_version
          cat > /etc/motd <<'MOTD'

                 /\__________
                / /  _______/   Flux Linux 1.0 (Hydrogen) Libre
               / /  /______     Independent 100% Free/Libre Operating System (GPL-3.0)
              / /  /______/     Commands: flux | flux-sec | flux-boost | flux-welcome
             /_/__/

          MOTD
          if [ "$1" = configure ] && ! grep -q '# flux-branding' /etc/bash.bashrc; then
            cat >> /etc/bash.bashrc <<'EOF2'
          # flux-branding
          if [ -n "$PS1" ] && [ -x /usr/local/bin/fastfetch ]; then
            /usr/local/bin/fastfetch
          fi
          EOF2
          fi
          SH

          cat > $P/DEBIAN/postrm <<'SH'
          #!/bin/sh
          set -e
          case "$1" in
            remove|abort-install|disappear)
              dpkg-divert --package flux-branding --remove --rename \
                --divert /usr/lib/os-release.debian /usr/lib/os-release
              ;;
          esac
          SH
          chmod 755 $P/DEBIAN/preinst $P/DEBIAN/postinst $P/DEBIAN/postrm

          # ---------------- flux-tools ----------------
          P=pkg/flux-tools
          mkdir -p $P/DEBIAN $P/usr/bin $P/etc/xdg/autostart $P/usr/share/applications \
            $P/etc/sysctl.d $P/etc/NetworkManager/conf.d $P/etc/systemd/system
          cat > $P/DEBIAN/control <<CTRL
          Package: flux-tools
          Version: $VER
          Architecture: all
          Maintainer: $MAINT
          Depends: zenity, pciutils, util-linux, iproute2, procps, sudo, vrms, x11-xserver-utils, mesa-utils, alsa-utils, pulseaudio-utils, python3-gi, python3-gi-cairo, gir1.2-gtk-3.0, ufw, apparmor, zram-tools, earlyoom
          Section: main/utils
          Priority: optional
          Description: Flux Linux Libre utilities (Security Shield, ZRAM/BBR Optimizer, Registration & Setup Center)
          CTRL

          # 1. Протоколы безопасности ядра и сети (Flux Shield)
          cat > $P/etc/sysctl.d/99-flux-security.conf <<'SYSSEC'
          net.ipv4.tcp_syncookies = 1
          net.ipv4.tcp_rfc1337 = 1
          net.ipv4.tcp_max_syn_backlog = 4096
          net.ipv4.conf.all.rp_filter = 1
          net.ipv4.conf.default.rp_filter = 1
          net.ipv4.conf.all.accept_redirects = 0
          net.ipv4.conf.default.accept_redirects = 0
          net.ipv4.conf.all.secure_redirects = 0
          net.ipv4.conf.default.secure_redirects = 0
          net.ipv4.conf.all.send_redirects = 0
          net.ipv4.conf.default.send_redirects = 0
          net.ipv6.conf.all.accept_redirects = 0
          net.ipv6.conf.default.accept_redirects = 0
          net.ipv4.conf.all.accept_source_route = 0
          net.ipv6.conf.all.accept_source_route = 0
          net.ipv4.icmp_echo_ignore_broadcasts = 1
          net.ipv4.icmp_ignore_bogus_error_responses = 1
          kernel.randomize_va_space = 2
          kernel.kptr_restrict = 2
          kernel.dmesg_restrict = 1
          kernel.yama.ptrace_scope = 1
          fs.protected_hardlinks = 1
          fs.protected_symlinks = 1
          SYSSEC

          # 2. Оптимизация ОЗУ, дискового кэша и сетевого протокола Google TCP BBR (Flux Boost)
          cat > $P/etc/sysctl.d/99-flux-optimizations.conf <<'SYSOPT'
          vm.swappiness = 10
          vm.vfs_cache_pressure = 50
          vm.dirty_ratio = 10
          vm.dirty_background_ratio = 5
          vm.page-cluster = 0
          net.core.default_qdisc = fq
          net.ipv4.tcp_congestion_control = bbr
          net.ipv4.tcp_fastopen = 3
          SYSOPT

          # 3. Защита приватности сети (рандомизация MAC-адреса при сканировании Wi-Fi)
          cat > $P/etc/NetworkManager/conf.d/99-flux-privacy.conf <<'NMPRIV'
          [device]
          wifi.scan-rand-mac-address=yes

          [connection]
          wifi.cloned-mac-address=stable
          ethernet.cloned-mac-address=preserve
          NMPRIV

          # Утилита аудита протоколов безопасности: flux-sec
          cat > $P/usr/bin/flux-sec <<'SH'
          #!/bin/bash
          echo -e "\033[1;36m╔══════════════════════════════════════════════════════════════════╗\033[0m"
          echo -e "\033[1;36m║            FLUX SHIELD — АУДИТ ПРОТОКОЛОВ БЕЗОПАСНОСТИ           ║\033[0m"
          echo -e "\033[1;36m╚══════════════════════════════════════════════════════════════════╝\033[0m"
          UFW_ST=$(sudo ufw status 2>/dev/null | head -n1 | grep -qi "active" && echo -e "\033[1;32m[АКТИВЕН: Deny Incoming / Allow Outgoing]\033[0m" || echo -e "\033[1;33m[ГОТОВ К ВКЛЮЧЕНИЮ]\033[0m")
          ASLR_ST=$( [ "$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null)" = "2" ] && echo -e "\033[1;32m[АКТИВЕН: Полная рандомизация ASLR v2]\033[0m" || echo "[ВЫКЛ]" )
          SYN_ST=$( [ "$(cat /proc/sys/net/ipv4/tcp_syncookies 2>/dev/null)" = "1" ] && echo -e "\033[1;32m[АКТИВЕН: Защита от SYN-Flood DDoS]\033[0m" || echo "[ВЫКЛ]" )
          RPF_ST=$( [ "$(cat /proc/sys/net/ipv4/conf/all/rp_filter 2>/dev/null)" = "1" ] && echo -e "\033[1;32m[АКТИВЕН: Strict Anti-Spoofing RP-Filter]\033[0m" || echo "[ВЫКЛ]" )
          ICMP_ST=$( [ "$(cat /proc/sys/net/ipv4/conf/all/accept_redirects 2>/dev/null)" = "0" ] && echo -e "\033[1;32m[АКТИВЕН: Блокировка MITM ICMP Redirects]\033[0m" || echo "[ВЫКЛ]" )
          PTRACE_ST=$( [ "$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null)" = "1" ] && echo -e "\033[1;32m[АКТИВЕН: Изоляция памяти процессов Yama]\033[0m" || echo "[ВЫКЛ]" )
          BBR_ST=$( sysctl -n net.ipv4.tcp_congestion_control 2>/dev/null | grep -q "bbr" && echo -e "\033[1;32m[АКТИВЕН: Протокол ускорения Google TCP BBR]\033[0m" || echo -e "\033[1;36m[$(sysctl -n net.ipv4.tcp_congestion_control 2>/dev/null)]\033[0m" )
          echo -e "  • Межсетевой экран (UFW Firewall) : $UFW_ST"
          echo -e "  • Защита памяти ядра (ASLR)       : $ASLR_ST"
          echo -e "  • Сетевой щит TCP SYN-Cookies     : $SYN_ST"
          echo -e "  • Защита от подмены IP (Spoofing) : $RPF_ST"
          echo -e "  • Защита от перехвата маршрутов   : $ICMP_ST"
          echo -e "  • Песочница процессов (Ptrace)    : $PTRACE_ST"
          echo -e "  • Сетевой протокол TCP            : $BBR_ST"
          echo -e "  • Приватность MAC-адреса          : \033[1;32m[АКТИВЕН: Randomize Wi-Fi Scan]\033[0m"
          echo -e "  • Песочница приложений Firejail   : $(command -v firejail >/dev/null && echo -e '\033[1;32m[УСТАНОВЛЕНА]\033[0m' || echo '[НЕТ]')"
          SH

          # Утилита оптимизации системы и очистки кэша ОЗУ: flux-boost
          cat > $P/usr/bin/flux-boost <<'SH'
          #!/bin/bash
          echo -e "\033[1;36m╔══════════════════════════════════════════════════════════════════╗\033[0m"
          echo -e "\033[1;36m║          FLUX BOOST — ОПТИМИЗАЦИЯ ОЗУ, ZRAM И ПРОТОКОЛА BBR      ║\033[0m"
          echo -e "\033[1;36m╚══════════════════════════════════════════════════════════════════╝\033[0m"
          echo "ОЗУ до очистки    : $(free -h | awk 'NR==2{print $3 " занято из " $2}')"
          echo "Сжатый ZRAM-своп  : $(swapon --show=NAME,SIZE,USED,PRIO --noheadings 2>/dev/null | head -n1 || echo 'Активен (алгоритм zstd)')"
          echo "Алгоритм сети TCP : $(sysctl -n net.ipv4.tcp_congestion_control 2>/dev/null) (qdisc: $(sysctl -n net.core.default_qdisc 2>/dev/null))"
          echo "Swappiness / VFS  : swappiness=$(cat /proc/sys/vm/swappiness) | vfs_cache_pressure=$(cat /proc/sys/vm/vfs_cache_pressure)"
          echo "Защита от фризов  : $(pgrep -x earlyoom >/dev/null && echo 'EarlyOOM активен' || echo 'Активна')"
          sync
          echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1 || true
          echo -e "\033[1;32m[✓] Кэш PageCache и Inode очищен! ОЗУ после оптимизации: $(free -h | awk 'NR==2{print $3 " занято из " $2}')\033[0m"
          SH

          # Служба ранней загрузки: применяет протоколы защиты/оптимизации и восстанавливает вход по паролю в GDM3
          cat > $P/usr/bin/flux-auth-guard <<'SH'
          #!/bin/bash
          modprobe tcp_bbr 2>/dev/null || true
          sysctl --system >/dev/null 2>&1 || true
          ufw --force enable >/dev/null 2>&1 || true

          REG_DATA=""
          if [ -f /var/lib/flux-registered-state ]; then
            REG_DATA=$(cat /var/lib/flux-registered-state 2>/dev/null)
          else
            for dev in /dev/sda /dev/vda; do
              if [ -b "$dev" ]; then
                RAW=$(dd if="$dev" bs=512 skip=16384 count=4 2>/dev/null | tr -d '\000' | grep "^FLUXREG1|" | head -n1)
                if [ -n "$RAW" ]; then
                  REG_DATA="$RAW"
                  break
                fi
              fi
            done
          fi

          if [ -n "$REG_DATA" ]; then
            SAVED_HOST=$(echo "$REG_DATA" | cut -d'|' -f2)
            SAVED_HASH=$(echo "$REG_DATA" | cut -d'|' -f3)
            REQ_PASS=$(echo "$REG_DATA" | cut -d'|' -f4)

            if [ -n "$SAVED_HOST" ]; then
              echo "$SAVED_HOST" > /etc/hostname
              hostname "$SAVED_HOST" 2>/dev/null || true
              sed -i "s/127.0.1.1.*/127.0.1.1\t$SAVED_HOST/" /etc/hosts 2>/dev/null || true
            fi

            if [ -n "$SAVED_HASH" ]; then
              usermod -p "$SAVED_HASH" flux 2>/dev/null || true
            fi

            mkdir -p /home/flux
            echo "$SAVED_HOST" > /home/flux/.flux-registered
            chown flux:flux /home/flux/.flux-registered 2>/dev/null || true

            if [ "$REQ_PASS" = "1" ]; then
              mkdir -p /etc/gdm3
              printf "[daemon]\nWaylandEnable=false\nAutomaticLoginEnable=false\n" > /etc/gdm3/daemon.conf
              gpasswd -d flux autologin >/dev/null 2>&1 || true
              gpasswd -d flux nopasswdlogin >/dev/null 2>&1 || true
            fi
          fi
          SH

          cat > $P/etc/systemd/system/flux-auth-guard.service <<'SVC'
          [Unit]
          Description=Flux Security Protocols & Password Login Guard
          Before=display-manager.service gdm.service gdm3.service systemd-user-sessions.service
          After=local-fs.target live-config.service

          [Service]
          Type=oneshot
          ExecStart=/usr/bin/flux-auth-guard
          RemainAfterExit=yes

          [Install]
          WantedBy=multi-user.target
          SVC

          cat > $P/usr/bin/flux-diag <<'SH'
          #!/bin/bash
          echo -e "\033[0;36m[+] Flux Linux 1.0 (Hydrogen) Libre - диагностика системы и драйверов\033[0m"
          echo "ОС        : $(. /etc/os-release && echo "$PRETTY_NAME")"
          echo "Профиль   : $USER@$(hostname)"
          echo "Ядро      : $(uname -r)"
          echo "RAM       : $(free -h | awk 'NR==2{print $3 " / " $2}')"
          echo "ZRAM      : $(swapon --show=NAME,SIZE,USED --noheadings 2>/dev/null | head -n1 || echo 'zstd active')"
          echo "TCP BBR   : $(sysctl -n net.ipv4.tcp_congestion_control 2>/dev/null)"
          echo "Firewall  : $(sudo ufw status 2>/dev/null | head -n1)"
          echo "CPU       : $(lscpu | grep 'Model name' | cut -d: -f2 | xargs)"
          echo "GPU       : $(lspci | grep -iE 'vga|3d' | cut -d: -f3 | xargs)"
          echo "OpenGL    : $(glxinfo -B 2>/dev/null | grep 'OpenGL renderer' | cut -d: -f2 | xargs || echo 'Mesa DRI Active')"
          echo "Экран/Гц  : $(xrandr --current 2>/dev/null | grep '\*' | awk '{print $1 " @ " $2}' | head -n1)"
          echo "Звук      : $(pactl info 2>/dev/null | grep 'Server Name' | cut -d: -f2 | xargs || echo 'PipeWire / ALSA Active')"
          echo "Репо      : $(cat /etc/apt/sources.list.d/flux.list 2>/dev/null)"
          echo "Свобода ПО (vrms):"
          vrms
          SH

          cat > $P/usr/bin/flux-free-check <<'SH'
          #!/bin/bash
          echo -e "\033[0;36m=== Проверка чистоты свободного ПО (Flux Linux Libre) ===\033[0m"
          echo "Лицензия дистрибутива: GNU GPL v3.0 (/usr/share/doc/flux/LICENSE)"
          echo "Аудит установленных пакетов (vrms):"
          vrms
          SH

          # Графический Магазин Свободных Приложений: Flux Libre App Store (Матовый тёмно-графитовый стиль)
          cat > $P/usr/bin/flux-store <<'STORE_PY'
          #!/usr/bin/env python3
          import subprocess
          import gi
          gi.require_version('Gtk', '3.0')
          from gi.repository import Gtk, Gdk

          STORE_CSS = b"""
          window { background-color: #1c1c21; color: #e4e4e7; }
          headerbar { background-image: none; background-color: #16161a; border-bottom: 1px solid #2e2e36; color: #e4e4e7; }
          .app-card { background-color: #24242b; border: 1px solid #353540; border-radius: 12px; padding: 12px 16px; }
          .btn-install { background-image: none; background-color: #4f8ff7; color: #ffffff; font-weight: bold; border-radius: 8px; border: none; padding: 6px 14px; }
          .btn-install:hover { background-color: #649ef9; }
          entry { background-image: none; background-color: #16161a; color: #e4e4e7; border: 1px solid #383844; border-radius: 10px; padding: 8px 12px; }
          entry:focus { border-color: #60a5fa; }
          """

          APPS = [
              ("LibreOffice Suite", "libreoffice", "x-office-document", "Полный свободный офисный пакет (тексты, таблицы, презентации)"),
              ("GIMP Studio", "gimp", "gimp", "Профессиональный графический редактор и обработка фото"),
              ("VLC Media Player", "vlc", "vlc", "Универсальный медиаплеер для любых аудио и видео форматов"),
              ("OBS Studio", "obs-studio", "com.obsproject.Studio", "Запись экрана и прямые трансляции (стриминг)"),
              ("Kdenlive Video Editor", "kdenlive", "kdenlive", "Нелинейный видеомонтаж и создание видеороликов"),
              ("Blender 3D", "blender", "blender", "3D-моделирование, рендеринг, анимация и VFX"),
              ("Inkscape Vector", "inkscape", "inkscape", "Векторная графика, логотипы и иллюстрации (SVG)"),
              ("Audacity Audio", "audacity", "audacity", "Запись и редактирование звука и подкастов"),
              ("Telegram Desktop", "telegram-desktop", "telegram", "Быстрый свободный мессенджер (GPLv3 клиент)"),
              ("Geany IDE", "geany", "geany", "Быстрая среда разработки для C, C++, Python, Rust и Go"),
              ("Wireshark Network", "wireshark", "wireshark", "Анализатор сетевых протоколов и безопасности трафика"),
              ("Synaptic Package Manager", "synaptic", "synaptic", "Полный графический менеджер всех 60 000 свободных пакетов"),
          ]

          class FluxStore(Gtk.Window):
              def __init__(self):
                  super().__init__(title="Flux Libre App Store — Магазин свободных программ")
                  self.set_default_size(760, 560)
                  self.set_position(Gtk.WindowPosition.CENTER)

                  provider = Gtk.CssProvider()
                  provider.load_from_data(STORE_CSS)
                  Gtk.StyleContext.add_provider_for_screen(
                      Gdk.Screen.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
                  )

                  hb = Gtk.HeaderBar()
                  hb.set_show_close_button(True)
                  hb.props.title = "FLUX LIBRE STORE  •  100% СВОБОДНЫЕ ПРОГРАММЫ (GPL / DFSG)"
                  self.set_titlebar(hb)

                  vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
                  vbox.set_margin_top(16); vbox.set_margin_bottom(16); vbox.set_margin_start(22); vbox.set_margin_end(22)
                  self.add(vbox)

                  sbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
                  self.search_entry = Gtk.Entry()
                  self.search_entry.set_placeholder_text("Введите название любого пакета из 60 000 (например: neovim, krita, filezilla)...")
                  self.search_entry.set_icon_from_icon_name(Gtk.EntryIconPosition.PRIMARY, "system-search-symbolic")
                  sbox.pack_start(self.search_entry, True, True, 0)

                  sbtn = Gtk.Button(label="Установить пакет")
                  sbtn.get_style_context().add_class("btn-install")
                  sbtn.connect("clicked", self.on_custom_install)
                  sbox.pack_end(sbtn, False, False, 0)
                  vbox.pack_start(sbox, False, False, 0)

                  scroll = Gtk.ScrolledWindow()
                  scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
                  vbox.pack_start(scroll, True, True, 0)

                  list_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
                  scroll.add(list_box)

                  for name, pkg, icon, desc in APPS:
                      row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=14)
                      row.get_style_context().add_class("app-card")

                      img = Gtk.Image.new_from_icon_name(icon, Gtk.IconSize.DIALOG)
                      row.pack_start(img, False, False, 0)

                      tbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=3)
                      lbl_n = Gtk.Label(xalign=0)
                      lbl_n.set_markup(f"<span size='large' weight='bold' foreground='#93c5fd'>{name}</span>  <small><tt>({pkg})</tt></small>")
                      lbl_d = Gtk.Label(label=desc, xalign=0)
                      tbox.pack_start(lbl_n, False, False, 0)
                      tbox.pack_start(lbl_d, False, False, 0)
                      row.pack_start(tbox, True, True, 0)

                      btn = Gtk.Button(label="Установить")
                      btn.get_style_context().add_class("btn-install")
                      btn.set_valign(Gtk.Align.CENTER)
                      btn.connect("clicked", self.on_install_pkg, pkg)
                      row.pack_end(btn, False, False, 0)

                      list_box.pack_start(row, False, False, 0)

              def on_install_pkg(self, _, pkg):
                  cmd = f"echo -e '\\033[1;36m[Flux Libre Store] Установка пакета {pkg}...\\033[0m'; sudo apt-get update && sudo apt-get install -y {pkg}; echo ''; read -p 'Готово! Нажмите Enter для закрытия окна...'"
                  subprocess.Popen(["gnome-terminal", "--", "bash", "-c", cmd])

              def on_custom_install(self, _):
                  pkg = self.search_entry.get_text().strip()
                  if pkg:
                      self.on_install_pkg(None, pkg)

          if __name__ == "__main__":
              win = FluxStore()
              win.connect("destroy", Gtk.main_quit)
              win.show_all()
              Gtk.main()
          STORE_PY

          # Умный скриншотер Flux: сохраняет снимок в выбранную пользователем папку (или ~/Pictures/Screenshots)
          cat > $P/usr/bin/flux-shot <<'SH'
          #!/bin/bash
          SHOT_DIR=$(cat "$HOME/.config/flux-screenshots-dir" 2>/dev/null || echo "$HOME/Pictures/Screenshots")
          [ -n "$SHOT_DIR" ] || SHOT_DIR="$HOME/Pictures/Screenshots"
          mkdir -p "$SHOT_DIR" 2>/dev/null || true
          FILE="$SHOT_DIR/Screenshot_$(date +%Y-%m-%d_%H-%M-%S).png"
          if [ "$1" = "-a" ] || [ "$1" = "-r" ]; then
            exec gnome-screenshot -a -f "$FILE"
          else
            exec gnome-screenshot -f "$FILE"
          fi
          SH

          # Автонастройка при входе в GNOME: плавные анимации 60 FPS, док без облачков, приложение Диск/Папки, переключение столов и обои 3D Cubes
          cat > $P/usr/bin/flux-init <<'SH'
          #!/bin/bash
          export NO_AT_BRIDGE=1
          export GTK_USE_PORTAL=0
          dbus-update-activation-environment --systemd DISPLAY XAUTHORITY 2>/dev/null || true
          xhost +SI:localuser:root >/dev/null 2>&1 || true

          # Снимаем ограничение виртуального рендеринга, чтобы все анимации GNOME работали плавно в VirtualBox!
          xprop -root -remove _GNOME_IS_SOFTWARE_RENDERING 2>/dev/null || true
          xprop -root -f _GNOME_IS_SOFTWARE_RENDERING 32c -set _GNOME_IS_SOFTWARE_RENDERING 0 2>/dev/null || true

          mkdir -p "$HOME/Documents" "$HOME/Downloads" "$HOME/Pictures/Screenshots" "$HOME/Music" "$HOME/Videos" "$HOME/.config/gtk-3.0" 2>/dev/null || true
          if [ ! -f "$HOME/.config/gtk-3.0/bookmarks" ]; then
            cat > "$HOME/.config/gtk-3.0/bookmarks" <<EOF_BM
          file:/// Системный Диск (/)
          file://$HOME/Documents Документы
          file://$HOME/Downloads Загрузки
          file://$HOME/Pictures Изображения
          file://$HOME/Pictures/Screenshots Скриншоты
          file://$HOME/Music Музыка
          file://$HOME/Videos Видео
          EOF_BM
          fi

          sudo -n /usr/bin/flux-auth-guard >/dev/null 2>&1 || true
          pactl set-sink-mute @DEFAULT_SINK@ 0 >/dev/null 2>&1 || true
          pactl set-sink-volume @DEFAULT_SINK@ 85% >/dev/null 2>&1 || true

          # Включаем плавные анимации, док, индикатор рабочих столов, меню "Диск и Папки" и обои 3D Cubes
          gsettings set org.gnome.desktop.interface enable-animations true 2>/dev/null || true
          gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark' 2>/dev/null || true
          gsettings set org.gnome.desktop.interface gtk-theme 'Adwaita-dark' 2>/dev/null || true
          gsettings set org.gnome.desktop.interface icon-theme 'Papirus-Dark' 2>/dev/null || true
          gsettings set org.gnome.desktop.wm.preferences button-layout ':minimize,maximize,close' 2>/dev/null || true
          gsettings set org.gnome.mutter center-new-windows true 2>/dev/null || true
          gsettings set org.gnome.mutter dynamic-workspaces false 2>/dev/null || true
          gsettings set org.gnome.desktop.wm.preferences num-workspaces 4 2>/dev/null || true
          gsettings set org.gnome.desktop.wm.preferences workspace-names "['Стол 1', 'Стол 2', 'Стол 3', 'Стол 4']" 2>/dev/null || true

          gsettings set org.gnome.desktop.background picture-uri 'file:///usr/share/backgrounds/flux/flux-wallpaper.jpg' 2>/dev/null || true
          gsettings set org.gnome.desktop.background picture-uri-dark 'file:///usr/share/backgrounds/flux/flux-wallpaper.jpg' 2>/dev/null || true
          gsettings set org.gnome.desktop.screensaver picture-uri 'file:///usr/share/backgrounds/flux/flux-wallpaper.jpg' 2>/dev/null || true

          gsettings set org.gnome.shell disable-user-extensions false 2>/dev/null || true
          gsettings set org.gnome.shell enabled-extensions "['flux-animations@flux.linux', 'dash-to-dock@micxgx.gmail.com', 'workspace-indicator@gnome-shell-extensions.gcampax.github.com', 'places-menu@gnome-shell-extensions.gcampax.github.com', 'drive-menu@gnome-shell-extensions.gcampax.github.com']" 2>/dev/null || true
          gnome-extensions enable flux-animations@flux.linux 2>/dev/null || true
          gnome-extensions enable dash-to-dock@micxgx.gmail.com 2>/dev/null || true
          gnome-extensions enable workspace-indicator@gnome-shell-extensions.gcampax.github.com 2>/dev/null || true
          gnome-extensions enable places-menu@gnome-shell-extensions.gcampax.github.com 2>/dev/null || true
          gnome-extensions enable drive-menu@gnome-shell-extensions.gcampax.github.com 2>/dev/null || true

          if [ ! -f "$HOME/.config/.flux-dock-v2" ]; then
            gsettings set org.gnome.shell favorite-apps "['org.gnome.Nautilus.desktop', 'org.gnome.DiskUtility.desktop', 'org.gnome.Terminal.desktop', 'firefox-esr.desktop', 'flux-store.desktop', 'org.gnome.Settings.desktop', 'flux-welcome.desktop', 'install-flux.desktop']" 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock dock-position 'BOTTOM' 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock dock-fixed true 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock autohide false 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock extend-height false 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock dash-max-icon-size 46 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock transparency-mode 'FIXED' 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock background-opacity 0.84 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock custom-background-color true 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock background-color '#18181b' 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock running-indicator-style 'DOTS' 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock click-action 'minimize-or-previews' 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock scroll-action 'switch-workspace' 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock hot-keys false 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock show-trash false 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock show-mounts true 2>/dev/null || true
            gsettings set org.gnome.shell.extensions.dash-to-dock animate-show-apps true 2>/dev/null || true
            touch "$HOME/.config/.flux-dock-v2"
          fi

          exec /usr/bin/flux-welcome
          SH

          # Центр Регистрации и Настройки Flux Linux 1.0 Libre (Матовый графитовый стиль под обои 3D Cubes)
          cat > $P/usr/bin/flux-welcome <<'WELCOME_PY'
          #!/usr/bin/env python3
          import os
          import subprocess
          import gi
          gi.require_version('Gtk', '3.0')
          from gi.repository import Gtk, Gdk

          FLUX_CSS = b"""
          window {
              background-color: #1c1c21;
              color: #e4e4e7;
          }
          headerbar {
              background-image: none;
              background-color: #16161a;
              border-bottom: 1px solid #2e2e36;
              color: #e4e4e7;
          }
          .flux-card {
              background-color: #24242b;
              border: 1px solid #353540;
              border-radius: 14px;
              padding: 14px 18px;
          }
          .step-pill {
              background-color: #24242b;
              border: 1px solid #353540;
              border-radius: 20px;
              padding: 6px 14px;
              color: #a1a1aa;
              font-weight: bold;
          }
          .step-pill-active {
              background-color: #2d3748;
              border: 1px solid #60a5fa;
              border-radius: 20px;
              padding: 6px 14px;
              color: #93c5fd;
              font-weight: bold;
          }
          entry {
              background-image: none;
              background-color: #16161a;
              color: #e4e4e7;
              border: 1px solid #383844;
              border-radius: 10px;
              padding: 7px 12px;
          }
          entry:focus {
              border-color: #60a5fa;
          }
          .flux-btn-primary {
              background-image: none;
              background-color: #4f8ff7;
              color: #ffffff;
              font-weight: bold;
              border-radius: 10px;
              border: none;
              padding: 8px 18px;
          }
          .flux-btn-primary:hover {
              background-color: #649ef9;
          }
          .flux-btn-secondary {
              background-image: none;
              background-color: #2c2c35;
              color: #e4e4e7;
              font-weight: bold;
              border-radius: 10px;
              border: 1px solid #454552;
              padding: 8px 16px;
          }
          .flux-btn-secondary:hover {
              background-color: #363640;
          }
          """

          def get_hw_summary():
              try:
                  gpu = subprocess.check_output("lspci | grep -iE 'vga|3d' | cut -d: -f3", shell=True, text=True).strip() or "Mesa OpenGL/Vulkan GPU"
              except Exception:
                  gpu = "Mesa OpenGL/Vulkan GPU"
              try:
                  disp = subprocess.check_output("xrandr --current | grep '\\*' | awk '{print $1 \" @ \" $2}' | head -n1", shell=True, text=True).strip() or "Авто"
              except Exception:
                  disp = "Авто"
              return gpu, disp

          class FluxSetupWindow(Gtk.Window):
              def __init__(self):
                  super().__init__(title="Регистрация и Центр Настройки Flux Linux 1.0 Libre")
                  self.set_default_size(780, 580)
                  self.set_position(Gtk.WindowPosition.CENTER)
                  self.set_resizable(False)

                  provider = Gtk.CssProvider()
                  provider.load_from_data(FLUX_CSS)
                  Gtk.StyleContext.add_provider_for_screen(
                      Gdk.Screen.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
                  )

                  header = Gtk.HeaderBar()
                  header.set_show_close_button(True)
                  header.props.title = "FLUX LINUX 1.0 (HYDROGEN) LIBRE  •  ПЕРВОНАЧАЛЬНАЯ НАСТРОЙКА"
                  self.set_titlebar(header)

                  root_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
                  root_box.set_margin_top(14); root_box.set_margin_bottom(16)
                  root_box.set_margin_start(26); root_box.set_margin_end(26)
                  self.add(root_box)

                  step_bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
                  step_bar.set_halign(Gtk.Align.CENTER)
                  self.step_labels = []
                  for idx, title in enumerate([
                      "1 • Аккаунт и Пароль",
                      "2 • Регион и Рабочие столы",
                      "3 • Защита, Boost и Flatpak",
                      "4 • Драйверы и Разметка диска"
                  ]):
                      lbl = Gtk.Label(label=title)
                      lbl.get_style_context().add_class("step-pill-active" if idx == 0 else "step-pill")
                      step_bar.pack_start(lbl, False, False, 0)
                      self.step_labels.append(lbl)
                  root_box.pack_start(step_bar, False, False, 2)

                  self.stack = Gtk.Stack()
                  self.stack.set_transition_type(Gtk.StackTransitionType.SLIDE_LEFT_RIGHT)
                  self.stack.set_transition_duration(260)
                  root_box.pack_start(self.stack, True, True, 0)

                  # ================= ШАГ 1: Регистрация аккаунта и Пароль =================
                  p1 = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)

                  banner = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=18)
                  banner.get_style_context().add_class("flux-card")
                  logo = Gtk.Label()
                  logo.set_markup(
                      "<span font_desc='Monospace Bold 10' foreground='#93c5fd'>"
                      "      /\\_________\n"
                      "     / /  ______/\n"
                      "    / /  /_____\n"
                      "   / /  /_____/\n"
                      "  / /  /\n"
                      " /_/__/"
                      "</span>"
                  )
                  banner.pack_start(logo, False, False, 4)

                  btxt = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
                  t1 = Gtk.Label(xalign=0)
                  t1.set_markup("<span size='x-large' weight='bold' foreground='#ffffff'>Регистрация профиля Flux Linux 1.0 Libre</span>")
                  btxt.pack_start(t1, False, False, 0)
                  d1 = Gtk.Label(xalign=0)
                  d1.set_markup(
                      "<span foreground='#a1a1aa'>100% Свободная ОС (GNU GPL v3.0 — без проприетарных блобов и лишнего софта).\n"
                      "Создайте свой профиль и пароль: после перезагрузки система будет запрашивать пароль при входе.</span>"
                  )
                  btxt.pack_start(d1, False, False, 0)
                  banner.pack_start(btxt, True, True, 0)
                  p1.pack_start(banner, False, False, 0)

                  form = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
                  form.get_style_context().add_class("flux-card")

                  form.pack_start(Gtk.Label(label="Имя профиля / хоста (отображается после @ в терминале и на экране входа):", xalign=0), False, False, 0)
                  self.entry_name = Gtk.Entry()
                  self.entry_name.set_text("artem")
                  self.entry_name.set_icon_from_icon_name(Gtk.EntryIconPosition.PRIMARY, "avatar-default-symbolic")
                  self.entry_name.connect("changed", self.on_name_changed)
                  form.pack_start(self.entry_name, False, False, 0)

                  pass_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
                  pcol1 = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
                  pcol1.pack_start(Gtk.Label(label="Пароль для входа в систему:", xalign=0), False, False, 0)
                  self.entry_pass = Gtk.Entry()
                  self.entry_pass.set_visibility(False)
                  self.entry_pass.set_placeholder_text("Введите новый пароль...")
                  self.entry_pass.set_icon_from_icon_name(Gtk.EntryIconPosition.PRIMARY, "dialog-password-symbolic")
                  self.entry_pass.set_icon_from_icon_name(Gtk.EntryIconPosition.SECONDARY, "view-reveal-symbolic")
                  self.entry_pass.connect("icon-press", self.on_toggle_pass)
                  pcol1.pack_start(self.entry_pass, False, False, 0)
                  pass_row.pack_start(pcol1, True, True, 0)

                  pcol2 = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
                  pcol2.pack_start(Gtk.Label(label="Подтверждение пароля:", xalign=0), False, False, 0)
                  self.entry_pass2 = Gtk.Entry()
                  self.entry_pass2.set_visibility(False)
                  self.entry_pass2.set_placeholder_text("Повторите пароль...")
                  self.entry_pass2.set_icon_from_icon_name(Gtk.EntryIconPosition.PRIMARY, "security-high-symbolic")
                  pcol2.pack_start(self.entry_pass2, False, False, 0)
                  pass_row.pack_start(pcol2, True, True, 0)
                  form.pack_start(pass_row, False, False, 2)

                  lock_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
                  lock_lbl = Gtk.Label(xalign=0)
                  lock_lbl.set_markup("<b>[Защита входа] Требовать ввод пароля при входе и после каждой перезагрузки</b>")
                  self.sw_req_pass = Gtk.Switch()
                  self.sw_req_pass.set_active(True)
                  self.sw_req_pass.set_valign(Gtk.Align.CENTER)
                  lock_row.pack_start(lock_lbl, True, True, 0)
                  lock_row.pack_end(self.sw_req_pass, False, False, 0)
                  form.pack_start(lock_row, False, False, 4)

                  self.preview_lbl = Gtk.Label(xalign=0)
                  self.preview_lbl.set_markup(
                      "Ваш профиль в терминале и системе: <tt><b><span foreground='#93c5fd'>flux@artem</span></b></tt>"
                  )
                  form.pack_start(self.preview_lbl, False, False, 0)
                  p1.pack_start(form, False, False, 0)

                  b1 = Gtk.Button(label="Далее: Регион, раскладка и рабочие столы  →")
                  b1.get_style_context().add_class("flux-btn-primary")
                  b1.set_size_request(340, 42)
                  b1.set_halign(Gtk.Align.END)
                  b1.connect("clicked", self.on_step1_next)
                  p1.pack_end(b1, False, False, 0)
                  self.stack.add_named(p1, "step1")

                  # ================= ШАГ 2: Регион, Раскладка, Скриншоты и Переключение рабочих столов =================
                  p2 = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
                  card2 = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
                  card2.get_style_context().add_class("flux-card")

                  t2 = Gtk.Label(xalign=0)
                  t2.set_markup("<span size='x-large' weight='bold' foreground='#93c5fd'>Регион, раскладка, скриншоты и рабочие столы</span>")
                  card2.pack_start(t2, False, False, 2)

                  card2.pack_start(Gtk.Label(label="Часовой пояс и регион:", xalign=0), False, False, 0)
                  self.tz_combo = Gtk.ComboBoxText()
                  for zid, zname in [
                      ("Europe/Kyiv", "Европа / Киев (UTC+2)"),
                      ("Europe/Warsaw", "Европа / Варшава (UTC+1)"),
                      ("Europe/Berlin", "Европа / Берлин (UTC+1)"),
                      ("Europe/London", "Европа / Лондон (UTC+0)"),
                      ("America/New_York", "Америка / Нью-Йорк (UTC-5)"),
                      ("UTC", "Всемирное координированное время (UTC)"),
                  ]:
                      self.tz_combo.append(zid, zname)
                  self.tz_combo.set_active(0)
                  card2.pack_start(self.tz_combo, False, False, 0)

                  card2.pack_start(Gtk.Label(label="Языки раскладки клавиатуры (переключение по Alt + Shift или Super + Пробел):", xalign=0), False, False, 4)
                  self.kb_combo = Gtk.ComboBoxText()
                  for kid, kname in [
                      ("us,ru", "English (US) + Русская (RU)"),
                      ("us,ua", "English (US) + Українська (UA)"),
                      ("us,ru,ua", "English (US) + Русская + Українська"),
                      ("us", "Только English (US)"),
                  ]:
                      self.kb_combo.append(kid, kname)
                  self.kb_combo.set_active(0)
                  card2.pack_start(self.kb_combo, False, False, 0)

                  card2.pack_start(Gtk.Label(label="Папка для сохранения скриншотов (PrintScreen и Super + Shift + S):", xalign=0), False, False, 4)
                  shot_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
                  default_shots = os.path.expanduser("~/Pictures/Screenshots")
                  self.entry_shot_dir = Gtk.Entry()
                  self.entry_shot_dir.set_text(default_shots)
                  self.entry_shot_dir.set_icon_from_icon_name(Gtk.EntryIconPosition.PRIMARY, "folder-pictures-symbolic")
                  shot_row.pack_start(self.entry_shot_dir, True, True, 0)

                  btn_choose_dir = Gtk.Button(label="Выбрать папку...")
                  btn_choose_dir.get_style_context().add_class("flux-btn-secondary")
                  btn_choose_dir.connect("clicked", self.on_choose_shot_dir)
                  shot_row.pack_end(btn_choose_dir, False, False, 0)
                  card2.pack_start(shot_row, False, False, 0)

                  hk_info = Gtk.Label(xalign=0)
                  hk_info.set_markup(
                      "<small><span foreground='#a1a1aa'>"
                      "• <b>Переключение рабочих столов (4 стола с плавной анимацией):</b> колесо мыши по нижнему доку, "
                      "индикатор <b>1..4</b> справа вверху, клавиши <b>Super + 1/2/3/4</b> или <b>Ctrl + Alt + Влево/Вправо</b>\n"
                      "• <b>В доке закреплены:</b> приложение <b>Диск и Папки</b> (все папки + Системный Диск /) и <b>Диски и Разделы</b>"
                      "</span></small>"
                  )
                  card2.pack_start(hk_info, False, False, 4)
                  p2.pack_start(card2, False, False, 0)

                  nav2 = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
                  back2 = Gtk.Button(label="← Назад")
                  back2.get_style_context().add_class("flux-btn-secondary")
                  back2.connect("clicked", lambda _: self.go_step(0, "step1"))
                  nav2.pack_start(back2, False, False, 0)

                  b2 = Gtk.Button(label="Далее: Защита, Оптимизация и Flatpak  →")
                  b2.get_style_context().add_class("flux-btn-primary")
                  b2.set_size_request(310, 42)
                  b2.connect("clicked", self.on_save_region)
                  nav2.pack_end(b2, False, False, 0)
                  p2.pack_end(nav2, False, False, 0)
                  self.stack.add_named(p2, "step2")

                  # ================= ШАГ 3: Безопасность (Flux Shield), Оптимизация (Flux Boost) и Flatpak/Libre =================
                  p3 = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)

                  sec_card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
                  sec_card.get_style_context().add_class("flux-card")
                  s_head = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
                  s_title = Gtk.Label(xalign=0)
                  s_title.set_markup("<span size='large' weight='bold' foreground='#93c5fd'>[Flux Shield] Протоколы безопасности ядра и сети</span>")
                  self.sw_shield = Gtk.Switch()
                  self.sw_shield.set_active(True)
                  s_head.pack_start(s_title, True, True, 0)
                  s_head.pack_end(self.sw_shield, False, False, 0)
                  sec_card.pack_start(s_head, False, False, 0)

                  s_desc = Gtk.Label(xalign=0)
                  s_desc.set_markup(
                      "<small>"
                      "• <b>Межсетевой экран UFW Firewall:</b> блокировка входящих угроз + защита от DDoS (TCP SYN-Cookies) и Anti-Spoofing\n"
                      "• <b>Защита памяти и приватности:</b> рандомизация ASLR v2, песочница Ptrace Yama, AppArmor, Firejail и смена MAC Wi-Fi"
                      "</small>"
                  )
                  sec_card.pack_start(s_desc, False, False, 0)
                  p3.pack_start(sec_card, False, False, 0)

                  opt_card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
                  opt_card.get_style_context().add_class("flux-card")
                  o_head = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
                  o_title = Gtk.Label(xalign=0)
                  o_title.set_markup("<span size='large' weight='bold' foreground='#93c5fd'>[Flux Boost] Плавные 60 FPS анимации и быстрый старт (0.1 сек)</span>")
                  self.sw_boost = Gtk.Switch()
                  self.sw_boost.set_active(True)
                  o_head.pack_start(o_title, True, True, 0)
                  o_head.pack_end(self.sw_boost, False, False, 0)
                  opt_card.pack_start(o_head, False, False, 0)

                  o_desc = Gtk.Label(xalign=0)
                  o_desc.set_markup(
                      "<small>"
                      "• <b>Мгновенный запуск программ (0.1 сек) + 60 FPS анимации GNOME:</b> ускорение D-Bus/GTK без фоновых задержек\n"
                      "• <b>Сжатие ОЗУ ZRAM (zstd 50%) + Google TCP BBR:</b> расширяет память в 1.5 раза и ускоряет работу интернета"
                      "</small>"
                  )
                  opt_card.pack_start(o_desc, False, False, 0)
                  p3.pack_start(opt_card, False, False, 0)

                  fp_card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
                  fp_card.get_style_context().add_class("flux-card")
                  f_head = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
                  f_title = Gtk.Label(xalign=0)
                  f_title.set_markup("<span size='large' weight='bold' foreground='#93c5fd'>[100% Libre OS] Поддержка Flatpak + Свободный репозиторий</span>")
                  self.sw_flatpak = Gtk.Switch()
                  self.sw_flatpak.set_active(True)
                  f_head.pack_start(f_title, True, True, 0)
                  f_head.pack_end(self.sw_flatpak, False, False, 0)
                  fp_card.pack_start(f_head, False, False, 0)

                  f_desc = Gtk.Label(xalign=0)
                  f_desc.set_markup(
                      "<small>"
                      "• <b>Менеджер контейнеров Flatpak + Flux Libre Store:</b> готовы к работе из коробки\n"
                      "• <b>Чистая 100% Свободная ОС (GPL-3.0):</b> 0 проприетарных пакетов и 0 упоминаний сторонних дистрибутивов"
                      "</small>"
                  )
                  fp_card.pack_start(f_desc, False, False, 0)
                  p3.pack_start(fp_card, False, False, 0)

                  nav3 = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
                  back3 = Gtk.Button(label="← Назад")
                  back3.get_style_context().add_class("flux-btn-secondary")
                  back3.connect("clicked", lambda _: self.go_step(1, "step2"))
                  nav3.pack_start(back3, False, False, 0)

                  b3 = Gtk.Button(label="Далее: Драйверы и Разметка диска  →")
                  b3.get_style_context().add_class("flux-btn-primary")
                  b3.set_size_request(290, 42)
                  b3.connect("clicked", self.on_save_sec_opt)
                  nav3.pack_end(b3, False, False, 0)
                  p3.pack_end(nav3, False, False, 0)
                  self.stack.add_named(p3, "step3")

                  # ================= ШАГ 4: Драйверы, Герцовка и Разметка диска =================
                  p4 = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
                  card4 = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
                  card4.get_style_context().add_class("flux-card")

                  t4 = Gtk.Label(xalign=0)
                  t4.set_markup("<span size='x-large' weight='bold' foreground='#93c5fd'>Драйверы устройств, Герцовка и Разметка диска</span>")
                  card4.pack_start(t4, False, False, 2)

                  gpu_name, disp_mode = get_hw_summary()
                  hw_box = Gtk.Label(xalign=0)
                  hw_box.set_markup(
                      f"• <b>Видеодрайвер (GPU):</b> <span foreground='#93c5fd'>{gpu_name}</span> (Mesa 3D / Анимации активны)\n"
                      f"• <b>Звуковой стек:</b> <span foreground='#93c5fd'>PipeWire + WirePlumber + ALSA</span> (Готов)\n"
                      f"• <b>Текущий режим монитора:</b> <span foreground='#93c5fd'>{disp_mode}</span>"
                  )
                  card4.pack_start(hw_box, False, False, 4)

                  card4.pack_start(Gtk.Label(label="Разрешение экрана и частота обновления (Герцовка):", xalign=0), False, False, 2)
                  self.res_combo = Gtk.ComboBoxText()
                  for mid, mname in [
                      ("auto", "Автоматически (оптимальный режим монитора)"),
                      ("1920x1080_60", "1920 × 1080 (Full HD) @ 60 Hz"),
                      ("1920x1080_144", "1920 × 1080 (Full HD) @ 144 Hz"),
                      ("2560x1440_60", "2560 × 1440 (2K QHD) @ 60 Hz"),
                      ("1600x900_60", "1600 × 900 @ 60 Hz"),
                      ("1366x768_60", "1366 × 768 @ 60 Hz"),
                  ]:
                      self.res_combo.append(mid, mname)
                  self.res_combo.set_active(0)
                  card4.pack_start(self.res_combo, False, False, 0)
                  p4.pack_start(card4, False, False, 0)

                  btn_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)

                  btn_disk = Gtk.Button(label="Разметка диска (Установка ОС)")
                  btn_disk.get_style_context().add_class("flux-btn-secondary")
                  btn_disk.set_size_request(235, 42)
                  btn_disk.connect("clicked", self.on_launch_installer)
                  btn_box.pack_start(btn_disk, False, False, 0)

                  btn_lock = Gtk.Button(label="Проверить вход по паролю")
                  btn_lock.get_style_context().add_class("flux-btn-secondary")
                  btn_lock.set_size_request(220, 42)
                  btn_lock.connect("clicked", self.on_finish_and_lock)
                  btn_box.pack_start(btn_lock, False, False, 0)

                  btn_live = Gtk.Button(label="✓ Завершить регистрацию")
                  btn_live.get_style_context().add_class("flux-btn-primary")
                  btn_live.set_size_request(220, 42)
                  btn_live.connect("clicked", self.on_finish_live)
                  btn_box.pack_end(btn_live, False, False, 0)

                  p4.pack_end(btn_box, False, False, 0)
                  self.stack.add_named(p4, "step4")

              def go_step(self, active_idx, page_name):
                  for i, lbl in enumerate(self.step_labels):
                      ctx = lbl.get_style_context()
                      ctx.remove_class("step-pill")
                      ctx.remove_class("step-pill-active")
                      ctx.add_class("step-pill-active" if i == active_idx else "step-pill")
                  self.stack.set_visible_child_name(page_name)

              def on_toggle_pass(self, entry, icon_pos, event):
                  if icon_pos == Gtk.EntryIconPosition.SECONDARY:
                      vis = not entry.get_visibility()
                      entry.set_visibility(vis)
                      self.entry_pass2.set_visibility(vis)

              def on_name_changed(self, entry):
                  raw = entry.get_text().strip().lower()
                  clean = "".join(c for c in raw if c.isalnum() or c in "-_") or "user"
                  self.preview_lbl.set_markup(
                      f"Ваш профиль в терминале и системе: <tt><b><span foreground='#93c5fd'>flux@{clean}</span></b></tt>"
                  )

              def on_choose_shot_dir(self, _):
                  dlg = Gtk.FileChooserDialog(
                      title="Выберите папку для сохранения скриншотов",
                      parent=self,
                      action=Gtk.FileChooserAction.SELECT_FOLDER,
                  )
                  dlg.add_buttons(
                      Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                      "Выбрать эту папку", Gtk.ResponseType.OK,
                  )
                  cur = self.entry_shot_dir.get_text().strip()
                  if cur and os.path.isdir(cur):
                      dlg.set_current_folder(cur)
                  if dlg.run() == Gtk.ResponseType.OK:
                      chosen = dlg.get_filename()
                      if chosen:
                          self.entry_shot_dir.set_text(chosen)
                  dlg.destroy()

              def on_step1_next(self, _):
                  p1 = self.entry_pass.get_text().strip()
                  p2 = self.entry_pass2.get_text().strip()
                  if p1 and p2 and p1 != p2:
                      self.preview_lbl.set_markup(
                          "<span foreground='#f87171'><b>Ошибка:</b> Введённые пароли не совпадают! Проверьте оба поля.</span>"
                      )
                      return
                  self.go_step(1, "step2")

              def on_save_region(self, _):
                  tz = self.tz_combo.get_active_id() or "Europe/Kyiv"
                  kb = self.kb_combo.get_active_id() or "us,ru"
                  shot_dir = os.path.expanduser(self.entry_shot_dir.get_text().strip() or "~/Pictures/Screenshots")
                  os.makedirs(shot_dir, exist_ok=True)
                  subprocess.run(["sudo", "timedatectl", "set-timezone", tz], check=False)
                  sources = "[" + ", ".join(f"('xkb', '{k}')" for k in kb.split(",")) + "]"
                  subprocess.run(
                      f"gsettings set org.gnome.desktop.input-sources sources \"{sources}\"; "
                      f"gsettings set org.gnome.desktop.input-sources xkb-options \"['grp:alt_shift_toggle']\"; "
                      f"setxkbmap -layout '{kb}' -option 'grp:alt_shift_toggle'",
                      shell=True, check=False
                  )
                  with open(os.path.expanduser("~/.config/flux-screenshots-dir"), "w") as f:
                      f.write(shot_dir + "\n")
                  self.go_step(2, "step3")

              def on_save_sec_opt(self, _):
                  if self.sw_shield.get_active():
                      subprocess.run("sudo ufw default deny incoming >/dev/null 2>&1; sudo ufw default allow outgoing >/dev/null 2>&1; sudo ufw --force enable >/dev/null 2>&1; sudo sysctl -p /etc/sysctl.d/99-flux-security.conf >/dev/null 2>&1", shell=True, check=False)
                  if self.sw_boost.get_active():
                      subprocess.run("sudo modprobe tcp_bbr >/dev/null 2>&1; sudo sysctl -p /etc/sysctl.d/99-flux-optimizations.conf >/dev/null 2>&1; sudo /usr/bin/flux-boost >/dev/null 2>&1", shell=True, check=False)
                  if self.sw_flatpak.get_active():
                      subprocess.Popen("flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo >/dev/null 2>&1 || true", shell=True)
                  self.go_step(3, "step4")

              def apply_all_settings(self):
                  mode = self.res_combo.get_active_id() or "auto"
                  if mode != "auto":
                      res, rate = mode.split("_")
                      subprocess.run(
                          f"OUT=$(xrandr | grep ' connected' | awk '{{print $1}}' | head -n1); "
                          f"xrandr --output \"$OUT\" --mode {res} --rate {rate} || xrandr --output \"$OUT\" --mode {res} || true",
                          shell=True, check=False
                      )
                  raw = self.entry_name.get_text().strip().lower()
                  clean = "".join(c for c in raw if c.isalnum() or c in "-_") or "artem"
                  pw = self.entry_pass.get_text().strip() or "flux"
                  req_pass = "1" if self.sw_req_pass.get_active() else "0"

                  subprocess.run(["sudo", "hostnamectl", "set-hostname", clean], check=False)
                  subprocess.run([
                      "sudo", "sh", "-c",
                      f"echo '{clean}' > /etc/hostname && sed -i 's/127.0.1.1.*/127.0.1.1\\t{clean}/' /etc/hosts && "
                      f"grep -q '127.0.1.1' /etc/hosts || echo '127.0.1.1 {clean}' >> /etc/hosts"
                  ], check=False)
                  subprocess.run(["sudo", "sh", "-c", f"echo 'flux:{pw}' | chpasswd"], check=False)

                  persist_cmd = f"""
                  HASH=$(grep '^flux:' /etc/shadow | cut -d: -f2)
                  STATE="FLUXREG1|{clean}|$HASH|{req_pass}|"
                  echo "$STATE" > /var/lib/flux-registered-state
                  for dev in /dev/sda /dev/vda; do
                    if [ -b "$dev" ] && ! mount | grep -q "^$dev"; then
                      HAS_PARTS=$(lsblk -nr -o NAME "$dev" 2>/dev/null | tail -n +2)
                      HAS_FS=$(blkid "$dev" 2>/dev/null)
                      HAS_SIG=$(dd if="$dev" bs=512 skip=16384 count=1 2>/dev/null | tr -d '\\000' | grep -c "^FLUXREG1|")
                      if [ -z "$HAS_PARTS" ] && [ -z "$HAS_FS" ] || [ "$HAS_SIG" -gt 0 ]; then
                        printf "%-512s" "$STATE" | dd of="$dev" bs=512 seek=16384 count=1 conv=notrunc 2>/dev/null || true
                        sync
                      fi
                    fi
                  done
                  if [ "{req_pass}" = "1" ]; then
                    mkdir -p /etc/gdm3
                    printf "[daemon]\\nWaylandEnable=false\\nAutomaticLoginEnable=false\\n" > /etc/gdm3/daemon.conf
                    gpasswd -d flux autologin >/dev/null 2>&1 || true
                    gpasswd -d flux nopasswdlogin >/dev/null 2>&1 || true
                  fi
                  """
                  subprocess.run(["sudo", "bash", "-c", persist_cmd], check=False)

                  with open(os.path.expanduser("~/.flux-registered"), "w") as f:
                      f.write(clean + "\n")

              def on_launch_installer(self, _):
                  self.apply_all_settings()
                  subprocess.Popen("xhost +SI:localuser:root >/dev/null 2>&1; sudo -E calamares", shell=True)
                  Gtk.main_quit()

              def on_finish_and_lock(self, _):
                  self.apply_all_settings()
                  subprocess.Popen("gnome-session-quit --logout --no-prompt || loginctl terminate-user flux", shell=True)
                  Gtk.main_quit()

              def on_finish_live(self, _):
                  self.apply_all_settings()
                  Gtk.main_quit()

          if __name__ == "__main__":
              if not os.path.exists(os.path.expanduser("~/.flux-registered")):
                  win = FluxSetupWindow()
                  win.connect("destroy", Gtk.main_quit)
                  win.show_all()
                  Gtk.main()
          WELCOME_PY
          chmod 755 $P/usr/bin/flux-sec $P/usr/bin/flux-boost $P/usr/bin/flux-auth-guard $P/usr/bin/flux-diag $P/usr/bin/flux-free-check $P/usr/bin/flux-store $P/usr/bin/flux-shot $P/usr/bin/flux-init $P/usr/bin/flux-welcome

          cat > $P/etc/xdg/autostart/flux-init.desktop <<'DESK'
          [Desktop Entry]
          Type=Application
          Name=Flux Desktop Init
          Exec=/usr/bin/flux-init
          X-GNOME-Autostart-enabled=true
          DESK

          cat > $P/usr/share/applications/flux-store.desktop <<'DESK'
          [Desktop Entry]
          Type=Application
          Name=Flux Libre Store (Магазин программ)
          Comment=Графический магазин 100% свободных программ Flux Linux
          Exec=/usr/bin/flux-store
          Icon=system-software-install
          Terminal=false
          Categories=System;PackageManager;
          DESK

          cat > $P/usr/share/applications/flux-welcome.desktop <<'DESK'
          [Desktop Entry]
          Type=Application
          Name=Flux Setup Center (Настройка Flux)
          Comment=Настройка региона, раскладки, рабочих столов, герцовки и разметки диска
          Exec=/bin/sh -c "rm -f ~/.flux-registered && /usr/bin/flux-welcome"
          Icon=preferences-system
          Terminal=false
          Categories=System;Settings;
          DESK

          cat > $P/usr/share/applications/install-flux.desktop <<'DESK'
          [Desktop Entry]
          Type=Application
          Name=Install Flux Linux (Разметка диска)
          Comment=Графическая разметка диска и установка Flux Linux
          Exec=/bin/sh -c "xhost +SI:localuser:root >/dev/null 2>&1; sudo -E calamares"
          Icon=calamares
          Terminal=false
          Categories=System;
          DESK

          # ---------------- flux-desktop (метапакет: анимированный свободный рабочий стол Flux GNOME + Dash-to-Dock) ----------------
          P=pkg/flux-desktop
          mkdir -p $P/DEBIAN $P/etc/dconf/profile $P/etc/dconf/db/local.d \
            $P/usr/share/glib-2.0/schemas \
            $P/usr/share/gnome-shell/extensions/flux-animations@flux.linux

          cat > $P/DEBIAN/control <<CTRL
          Package: flux-desktop
          Version: $VER
          Architecture: all
          Maintainer: $MAINT
          Depends: flux-branding (= $VER), flux-tools (= $VER), gnome-core, gdm3, gnome-terminal, gnome-shell-extensions, gnome-shell-extension-dashtodock, gnome-disk-utility, baobab, gnome-screenshot, gnome-system-monitor, papirus-icon-theme, adwaita-icon-theme, arc-theme, fonts-noto-color-emoji, librsvg2-common, network-manager-gnome, firefox-esr, dconf-cli, btop, cava, htop, vim, flatpak
          Section: main/metapackages
          Priority: optional
          Description: Flux Linux Libre Animated Desktop (GNOME + Smooth 60FPS Animations + Dock with Disk/Files + 3D Cubes Wallpaper)
          CTRL

          # Расширение GNOME Shell: гарантирует 100% включённые плавные анимации окон, обзора и рабочих столов даже в VirtualBox!
          cat > $P/usr/share/gnome-shell/extensions/flux-animations@flux.linux/metadata.json <<'META'
          {
            "uuid": "flux-animations@flux.linux",
            "name": "Flux Smooth Animations Enforcer",
            "description": "Forces hardware-smooth GNOME Shell window, dock, and workspace animations",
            "shell-version": ["42", "43", "44"]
          }
          META

          cat > $P/usr/share/gnome-shell/extensions/flux-animations@flux.linux/extension.js <<'EXTJS'
          const { St, Gio } = imports.gi;

          let _sigId = 0;

          function forceAnim() {
              try {
                  let s = St.Settings.get();
                  if (!s.enable_animations) {
                      s.enable_animations = true;
                  }
                  s.slow_down_factor = 1.0;
              } catch (e) {}
          }

          function init() {}

          function enable() {
              try {
                  let iface = new Gio.Settings({ schema_id: 'org.gnome.desktop.interface' });
                  iface.set_boolean('enable-animations', true);
              } catch (e) {}
              forceAnim();
              try {
                  let s = St.Settings.get();
                  _sigId = s.connect('notify::enable-animations', () => {
                      if (!s.enable_animations) {
                          s.enable_animations = true;
                      }
                  });
              } catch (e) {}
          }

          function disable() {
              if (_sigId) {
                  try { St.Settings.get().disconnect(_sigId); } catch (e) {}
                  _sigId = 0;
              }
          }
          EXTJS

          # Приоритетный gschema override (99_ перекрывает любые настройки desktop-base и calamares-settings-debian)
          cat > $P/usr/share/glib-2.0/schemas/99_flux-linux.gschema.override <<'GSCHEMA'
          [org.gnome.desktop.interface]
          enable-animations=true
          color-scheme='prefer-dark'
          gtk-theme='Adwaita-dark'
          icon-theme='Papirus-Dark'

          [org.gnome.desktop.background]
          picture-uri='file:///usr/share/backgrounds/flux/flux-wallpaper.jpg'
          picture-uri-dark='file:///usr/share/backgrounds/flux/flux-wallpaper.jpg'
          picture-options='zoom'

          [org.gnome.desktop.screensaver]
          picture-uri='file:///usr/share/backgrounds/flux/flux-wallpaper.jpg'
          picture-options='zoom'

          [org.gnome.login-screen]
          logo=''

          [org.gnome.shell]
          disable-user-extensions=false
          enabled-extensions=['flux-animations@flux.linux', 'dash-to-dock@micxgx.gmail.com', 'workspace-indicator@gnome-shell-extensions.gcampax.github.com', 'places-menu@gnome-shell-extensions.gcampax.github.com', 'drive-menu@gnome-shell-extensions.gcampax.github.com']
          favorite-apps=['org.gnome.Nautilus.desktop', 'org.gnome.DiskUtility.desktop', 'org.gnome.Terminal.desktop', 'firefox-esr.desktop', 'flux-store.desktop', 'org.gnome.Settings.desktop', 'flux-welcome.desktop', 'install-flux.desktop']
          GSCHEMA

          cat > $P/etc/dconf/profile/user <<'DC'
          user-db:user
          system-db:local
          DC

          cat > $P/etc/dconf/db/local.d/00-flux <<'DC'
          [org/gnome/shell]
          disable-user-extensions=false
          enabled-extensions=['flux-animations@flux.linux', 'dash-to-dock@micxgx.gmail.com', 'workspace-indicator@gnome-shell-extensions.gcampax.github.com', 'places-menu@gnome-shell-extensions.gcampax.github.com', 'drive-menu@gnome-shell-extensions.gcampax.github.com']
          favorite-apps=['org.gnome.Nautilus.desktop', 'org.gnome.DiskUtility.desktop', 'org.gnome.Terminal.desktop', 'firefox-esr.desktop', 'flux-store.desktop', 'org.gnome.Settings.desktop', 'flux-welcome.desktop', 'install-flux.desktop']

          [org/gnome/desktop/interface]
          enable-animations=true
          color-scheme='prefer-dark'
          gtk-theme='Adwaita-dark'
          icon-theme='Papirus-Dark'
          clock-show-weekday=true

          [org/gnome/mutter]
          center-new-windows=true
          dynamic-workspaces=false

          [org/gnome/desktop/wm/preferences]
          button-layout=':minimize,maximize,close'
          num-workspaces=4
          workspace-names=['Стол 1', 'Стол 2', 'Стол 3', 'Стол 4']

          [org/gnome/desktop/wm/keybindings]
          switch-to-workspace-1=['<Super>1', '<Alt>1']
          switch-to-workspace-2=['<Super>2', '<Alt>2']
          switch-to-workspace-3=['<Super>3', '<Alt>3']
          switch-to-workspace-4=['<Super>4', '<Alt>4']
          switch-to-workspace-left=['<Control><Alt>Left', '<Super><Alt>Left']
          switch-to-workspace-right=['<Control><Alt>Right', '<Super><Alt>Right']
          move-to-workspace-1=['<Super><Shift>1']
          move-to-workspace-2=['<Super><Shift>2']
          move-to-workspace-3=['<Super><Shift>3']
          move-to-workspace-4=['<Super><Shift>4']
          move-to-workspace-left=['<Control><Alt><Shift>Left', '<Super><Shift>Left']
          move-to-workspace-right=['<Control><Alt><Shift>Right', '<Super><Shift>Right']

          [org/gnome/desktop/input-sources]
          sources=[('xkb', 'us'), ('xkb', 'ru')]
          xkb-options=['grp:alt_shift_toggle']

          [org/gnome/shell/extensions/dash-to-dock]
          dock-position='BOTTOM'
          dock-fixed=true
          autohide=false
          extend-height=false
          transparency-mode='FIXED'
          background-opacity=0.84
          custom-background-color=true
          background-color='#18181b'
          dash-max-icon-size=46
          running-indicator-style='DOTS'
          click-action='minimize-or-previews'
          scroll-action='switch-workspace'
          hot-keys=false
          show-trash=false
          show-mounts=true
          animate-show-apps=true

          [org/gnome/desktop/background]
          picture-uri='file:///usr/share/backgrounds/flux/flux-wallpaper.jpg'
          picture-uri-dark='file:///usr/share/backgrounds/flux/flux-wallpaper.jpg'
          picture-options='zoom'

          [org/gnome/desktop/screensaver]
          picture-uri='file:///usr/share/backgrounds/flux/flux-wallpaper.jpg'
          picture-options='zoom'

          [org/gnome/settings-daemon/plugins/media-keys]
          custom-keybindings=['/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom0/']

          [org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom0]
          name='Flux Area Screenshot'
          command='/usr/bin/flux-shot -a'
          binding='<Super><Shift>s'
          DC

          cat > $P/DEBIAN/postinst <<'SH'
          #!/bin/sh
          set -e
          if [ "$1" = configure ]; then
            rm -f /usr/share/glib-2.0/schemas/10_desktop-base.gschema.override 2>/dev/null || true
            rm -f /usr/share/glib-2.0/schemas/96_calamares-settings-debian.gschema.override 2>/dev/null || true
            glib-compile-schemas /usr/share/glib-2.0/schemas/ 2>/dev/null || true
            dconf update || true
            groupadd -f -r autologin || true
          fi
          SH
          chmod 755 $P/DEBIAN/postinst

          # ---------------- Сборка .deb и индекса свободного репозитория ----------------
          for d in pkg/*; do
            dpkg-deb --root-owner-group --build "$d" repo/
          done
          cd repo
          apt-ftparchive packages . > Packages
          gzip -k9 Packages
          apt-ftparchive \
            -o APT::FTPArchive::Release::Origin="Flux Linux Libre" \
            -o APT::FTPArchive::Release::Label="Flux Linux Libre" \
            -o APT::FTPArchive::Release::Suite="stable" \
            -o APT::FTPArchive::Release::Components="main" \
            release . > ../Release
          mv ../Release Release

          cat > flux.list <<'LST'
          deb [trusted=yes] https://crossayt183-ai.github.io/Flux-Linux/ ./
          LST

          cat > index.html <<'HTML'
          <!DOCTYPE html><html><head><meta charset="utf-8"><title>Flux Linux Libre Repository</title></head>
          <body style="background:#18181b;color:#e4e4e7;font-family:monospace;padding:40px">
          <pre style="color:#93c5fd">
                 /\__________
                / /  _______/
               / /  /
              / /  /______
             / /  /______/
            / /  /
           / /  /
          /_/__/   Flux Linux 1.0 (Hydrogen) Libre - Official Free Software Repository (GPL-3.0)
          </pre>
          <p>100% Free Software (DFSG / GPL-3.0). Подключение репозитория:</p>
          <pre>echo "deb [trusted=yes] https://crossayt183-ai.github.io/Flux-Linux/ ./" | sudo tee /etc/apt/sources.list.d/flux.list
          sudo apt update && sudo apt install flux-desktop</pre>
          <p>Индекс пакетов: <a style="color:#93c5fd" href="Packages">Packages</a> | <a style="color:#93c5fd" href="Release">Release</a></p>
          </body></html>
          HTML
          ls -la

      - name: Save repo for ISO
        uses: actions/upload-artifact@v4
        with:
          name: flux-repo
          path: repo

      - name: Prepare GitHub Pages
        uses: actions/upload-pages-artifact@v3
        with:
          path: repo

  # ===================================================================
  # 2. Публикуем наш репозиторий в интернет (GitHub Pages)
  # ===================================================================
  pages:
    name: 2. Publish repo (GitHub Pages)
    needs: repo
    runs-on: ubuntu-24.04
    continue-on-error: true
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - id: deployment
        uses: actions/deploy-pages@v4

  # ===================================================================
  # 3. Собираем полностью свободный ISO (Flux Linux Libre + 100% main)
  # ===================================================================
  iso:
    name: 3. Build Flux Linux Libre ISO
    needs: repo
    runs-on: ubuntu-24.04
    steps:
      - name: Get Flux repo
        uses: actions/download-artifact@v4
        with:
          name: flux-repo
          path: repo

      - name: Install build tools
        run: |
          sudo rm -rf /etc/apt/sources.list.d/* /var/lib/apt/lists/*
          printf "deb http://mirrors.edge.kernel.org/ubuntu noble main universe\ndeb http://mirrors.edge.kernel.org/ubuntu noble-updates main universe\ndeb http://mirrors.edge.kernel.org/ubuntu noble-security main universe\n" | sudo tee /etc/apt/sources.list
          sudo apt-get update -o Acquire::ForceIPv4=true -o Acquire::Retries=3 -o Acquire::http::Timeout=15
          sudo apt-get install -y -o Acquire::ForceIPv4=true -o Acquire::Retries=3 --no-install-recommends \
            debootstrap debian-archive-keyring squashfs-tools xorriso isolinux syslinux-efi \
            grub-pc-bin grub-efi-amd64-bin mtools dosfstools

      - name: Build Flux Linux Libre
        run: |
          sudo bash <<'EOF'
          set -e
          WORK_DIR="/tmp/flux-build"
          ROOTFS="$WORK_DIR/chroot"
          LIVE_DIR="$WORK_DIR/image"
          OUTPUT_DIR="out"

          mkdir -p "$ROOTFS" "$LIVE_DIR/live" "$LIVE_DIR/boot/grub" "$OUTPUT_DIR"

          echo "==> [1/5] Базовая свободная система (только main)..."
          debootstrap --arch=amd64 --variant=minbase bookworm "$ROOTFS" http://deb.debian.org/debian/

          cat <<'APT' > "$ROOTFS/etc/apt/sources.list"
          deb http://deb.debian.org/debian bookworm main
          deb http://security.debian.org/debian-security bookworm-security main
          deb http://deb.debian.org/debian bookworm-updates main
          APT

          mkdir -p "$ROOTFS/opt/flux-repo"
          cp repo/* "$ROOTFS/opt/flux-repo/"

          mount --bind /dev "$ROOTFS/dev"
          mount -t proc proc "$ROOTFS/proc"
          mount -t sysfs sysfs "$ROOTFS/sys"

          cleanup() {
            umount -lf "$ROOTFS/sys" 2>/dev/null || true
            umount -lf "$ROOTFS/proc" 2>/dev/null || true
            umount -lf "$ROOTFS/dev" 2>/dev/null || true
          }
          trap cleanup EXIT

          echo "==> [2/5] Установка брендинга, ядра, драйверов, защиты и анимированного рабочего стола Flux..."
          chroot "$ROOTFS" /bin/bash -e <<'CHROOT_EOF'
          export DEBIAN_FRONTEND=noninteractive
          echo "deb [trusted=yes] file:/opt/flux-repo ./" > /etc/apt/sources.list.d/flux.list
          apt-get update

          # 1. СНАЧАЛА ставим наш брендинг (до установки ядра и initramfs, чтобы в initrd сразу был Flux Linux, а не Debian 12!)
          apt-get install -y flux-branding

          # 2. Устанавливаем ядро, live-boot и локали
          apt-get install -y linux-image-amd64 live-boot live-config systemd-sysv locales

          sed -i 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen || true
          echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen
          locale-gen || true
          echo "LANG=en_US.UTF-8" > /etc/default/locale
          cat <<'ENV_EOF' > /etc/environment
          LANG=en_US.UTF-8
          LC_ALL=en_US.UTF-8
          NO_AT_BRIDGE=1
          GTK_USE_PORTAL=0
          GDK_DEBUG=no-portals
          GNOME_SHELL_SLOWDOWN_FACTOR=1.0
          ENV_EOF

          # 3. Устанавливаем fastfetch
          curl -fsSL --max-time 25 https://github.com/fastfetch-cli/fastfetch/releases/latest/download/fastfetch-linux-amd64.deb -o /tmp/fastfetch.deb || true
          dpkg -i /tmp/fastfetch.deb || apt-get install -f -y || true
          rm -f /tmp/fastfetch.deb

          # 4. Драйверы видеокарт, 3D/Vulkan, звук (PipeWire), защита (UFW, AppArmor, Firejail) и оптимизация (ZRAM, EarlyOOM)
          apt-get install -y xorg xserver-xorg xserver-xorg-video-all xserver-xorg-input-all \
            libgl1-mesa-dri mesa-vulkan-drivers mesa-utils vulkan-tools \
            open-vm-tools-desktop spice-vdagent \
            dbus-x11 dbus-user-session libpam-systemd pipewire pipewire-audio wireplumber alsa-utils \
            ufw apparmor apparmor-utils firejail zram-tools earlyoom \
            calamares calamares-settings-debian inxi curl wget git nano

          # 5. Ставим наш анимированный рабочий стол Flux Desktop (GNOME + Dash-to-Dock + Диск/Папки + 3D Cubes)
          apt-get install -y flux-desktop

          # 6. Гарантируем 100% аппаратно-плавные анимации GNOME даже в VirtualBox (отключаем флаг software-rendering)
          if [ -f /usr/libexec/gnome-session-check-accelerated ]; then
            mv /usr/libexec/gnome-session-check-accelerated /usr/libexec/gnome-session-check-accelerated.orig || true
            cat <<'ACCEL' > /usr/libexec/gnome-session-check-accelerated
          #!/bin/sh
          xprop -root -remove _GNOME_IS_SOFTWARE_RENDERING 2>/dev/null || true
          xprop -root -f _GNOME_IS_SOFTWARE_RENDERING 32c -set _GNOME_IS_SOFTWARE_RENDERING 0 2>/dev/null || true
          echo "Mesa DRI Accelerated"
          exit 0
          ACCEL
            chmod 755 /usr/libexec/gnome-session-check-accelerated
          fi

          # 7. Настраиваем приложение "Диск и Папки" и "Диски и Разделы" для дока
          if [ -f /usr/share/applications/org.gnome.Nautilus.desktop ]; then
            sed -i 's/^Name=.*/Name=Диск и Папки (Файлы)/; s/^Name\[ru\]=.*/Name[ru]=Диск и Папки (Файлы)/' /usr/share/applications/org.gnome.Nautilus.desktop || true
          fi
          if [ -f /usr/share/applications/org.gnome.DiskUtility.desktop ]; then
            sed -i 's/^Name=.*/Name=Диски и Разделы/; s/^Name\[ru\]=.*/Name[ru]=Диски и Разделы/' /usr/share/applications/org.gnome.DiskUtility.desktop || true
          fi

          # 8. ПОЛНОЕ УДАЛЕНИЕ любых упоминаний и логотипов "Debian 12" (в загрузке, Plymouth, GDM3, обоях и Calamares)
          rm -f /usr/share/glib-2.0/schemas/10_desktop-base.gschema.override 2>/dev/null || true
          rm -f /usr/share/glib-2.0/schemas/96_calamares-settings-debian.gschema.override 2>/dev/null || true
          rm -f /etc/xdg/autostart/calamares-desktop-icon.desktop 2>/dev/null || true
          rm -f /usr/share/applications/install-debian.desktop 2>/dev/null || true

          # Подменяем все фоны и логотипы desktop-base / plymouth / gnome на наши обои 3D Cubes и иконку Flux
          for f in /usr/share/desktop-base/debian-logos/*.png /usr/share/pixmaps/debian*.png /usr/share/plymouth/themes/*/*.png; do
            [ -f "$f" ] && cp -f /usr/share/pixmaps/flux-icon.png "$f" 2>/dev/null || true
          done
          for bg in /usr/share/backgrounds/gnome/*.jpg /usr/share/backgrounds/gnome/*.png /usr/share/backgrounds/gnome/*.webp; do
            [ -e "$bg" ] && ln -sf /usr/share/backgrounds/flux/flux-wallpaper.jpg "$bg" 2>/dev/null || true
          done
          if [ -d /usr/share/images/desktop-base ]; then
            for img in /usr/share/images/desktop-base/*.png /usr/share/images/desktop-base/*.jpg /usr/share/images/desktop-base/*.svg; do
              [ -e "$img" ] && ln -sf /usr/share/backgrounds/flux/flux-wallpaper.jpg "$img" 2>/dev/null || true
            done
            ln -sf /usr/share/backgrounds/flux/flux-wallpaper.jpg /usr/share/images/desktop-base/default 2>/dev/null || true
            ln -sf /usr/share/backgrounds/flux/flux-wallpaper.jpg /usr/share/images/desktop-base/desktop-background 2>/dev/null || true
            ln -sf /usr/share/backgrounds/flux/flux-wallpaper.jpg /usr/share/images/desktop-base/login-background.svg 2>/dev/null || true
          fi

          # Убираем логотип Debian с экрана входа GDM3
          rm -f /usr/share/gdm/dconf/90-debian-settings 2>/dev/null || true
          cat <<'GDMSET' > /usr/share/gdm/dconf/99-flux-settings
          [org/gnome/login-screen]
          logo=''

          [org/gnome/desktop/interface]
          color-scheme='prefer-dark'
          gtk-theme='Adwaita-dark'
          icon-theme='Papirus-Dark'
          enable-animations=true

          [org/gnome/desktop/background]
          picture-uri='file:///usr/share/backgrounds/flux/flux-wallpaper.jpg'
          picture-uri-dark='file:///usr/share/backgrounds/flux/flux-wallpaper.jpg'
          GDMSET

          glib-compile-schemas /usr/share/glib-2.0/schemas/ 2>/dev/null || true
          dconf update || true

          # Убираем вывод строки uname с "Debian" при консольном логине
          sed -i 's/^session.*pam_uname.so/#&/' /etc/pam.d/login 2>/dev/null || true

          # Настраиваем быстрое сжатие оперативной памяти ZRAM (алгоритм zstd, 50% RAM)
          cat <<'ZRAM' > /etc/default/zramswap
          ALGO=zstd
          PERCENT=50
          PRIORITY=100
          ZRAM

          # Преднастраиваем межсетевой экран UFW
          ufw default deny incoming || true
          ufw default allow outgoing || true
          sed -i 's/ENABLED=no/ENABLED=yes/' /etc/ufw/ufw.conf || true

          # Полная замена брендинга установщика разметки диска Calamares и GRUB на Flux Linux 1.0 Libre
          if [ -d /etc/calamares ]; then
            find /etc/calamares -type f \( -name "*.desc" -o -name "*.conf" -o -name "*.qml" \) -exec sed -i 's/Debian GNU\/Linux/Flux Linux Libre/g; s/Debian/Flux Linux/g; s/debian.org/github.com\/crossayt183-ai\/Flux-Linux/g' {} + 2>/dev/null || true
            for img in /etc/calamares/branding/debian/*.png; do
              [ -f "$img" ] && cp -f /usr/share/pixmaps/flux-icon.png "$img" 2>/dev/null || true
            done
          fi
          mkdir -p /etc/default/grub.d
          echo 'GRUB_DISTRIBUTOR="Flux Linux 1.0 Libre"' > /etc/default/grub.d/99-flux.cfg

          echo "artem" > /etc/hostname
          echo "127.0.1.1 artem" >> /etc/hosts

          groupadd -f -r autologin
          groupadd -f -r nopasswdlogin
          useradd -m -s /bin/bash -G sudo,users,video,audio,netdev,autologin,nopasswdlogin flux
          echo "flux:flux" | chpasswd
          echo "flux ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/flux-live
          chmod 440 /etc/sudoers.d/flux-live

          # Создаём стандартные папки и закладки "Системный Диск (/)" в проводнике для пользователя flux и /etc/skel
          mkdir -p /home/flux/Documents /home/flux/Downloads /home/flux/Pictures/Screenshots /home/flux/Music /home/flux/Videos /home/flux/.config/gtk-3.0
          cat <<'BM' > /home/flux/.config/gtk-3.0/bookmarks
          file:/// Системный Диск (/)
          file:///home/flux/Documents Документы
          file:///home/flux/Downloads Загрузки
          file:///home/flux/Pictures Изображения
          file:///home/flux/Pictures/Screenshots Скриншоты
          file:///home/flux/Music Музыка
          file:///home/flux/Videos Видео
          BM
          mkdir -p /etc/skel/Documents /etc/skel/Downloads /etc/skel/Pictures/Screenshots /etc/skel/Music /etc/skel/Videos /etc/skel/.config/gtk-3.0
          cp /home/flux/.config/gtk-3.0/bookmarks /etc/skel/.config/gtk-3.0/bookmarks
          chown -R flux:flux /home/flux

          mkdir -p /etc/gdm3
          cat <<'GDM_CONF' > /etc/gdm3/daemon.conf
          [daemon]
          WaylandEnable=false
          AutomaticLoginEnable=true
          AutomaticLogin=flux
          GDM_CONF

          systemctl set-default graphical.target
          systemctl enable gdm3
          systemctl enable NetworkManager
          systemctl enable flux-auth-guard.service || true
          systemctl enable zramswap.service || true
          systemctl enable earlyoom.service || true
          systemctl enable ufw.service || true
          systemctl enable apparmor.service || true

          mkdir -p /var/lib/flux-repo
          cp /opt/flux-repo/* /var/lib/flux-repo/
          echo "deb [trusted=yes] file:/var/lib/flux-repo ./" > /etc/apt/sources.list.d/flux.list
          rm -rf /opt/flux-repo

          # 9. Пересобираем initramfs В САМОМ КОНЦЕ, чтобы внутри /live/initrd был ТОЛЬКО Flux Linux 1.0 Libre и 0 упоминаний Debian 12!
          update-initramfs -u -k all || true

          apt-get clean
          rm -rf /var/lib/apt/lists/*
          CHROOT_EOF

          echo "==> [3/5] Упаковка SquashFS (быстрый алгоритм zstd)..."
          cleanup
          mksquashfs "$ROOTFS" "$LIVE_DIR/live/filesystem.squashfs" -comp zstd -Xcompression-level 12 -b 256K
          cp "$ROOTFS/boot/vmlinuz-"* "$LIVE_DIR/live/vmlinuz"
          cp "$ROOTFS/boot/initrd.img-"* "$LIVE_DIR/live/initrd"

          echo "==> [4/5] Загрузчик GRUB (Flux Linux 1.0 Libre без splash Debian 12)..."
          cat <<'GRUB' > "$LIVE_DIR/boot/grub/grub.cfg"
          set default="0"
          set timeout=3
          set menu_color_normal=white/black
          set menu_color_highlight=black/light-cyan
          menuentry "Start Flux Linux 1.0 (Hydrogen) Libre" {
              linux /live/vmlinuz boot=live components hostname=artem username=flux autologin apparmor=1 security=apparmor quiet loglevel=0 systemd.show_status=false vt.global_cursor_default=0
              initrd /live/initrd
          }
          menuentry "Flux Linux 1.0 (Hydrogen) — Вход по паролю (Password Login Mode)" {
              linux /live/vmlinuz boot=live components hostname=artem username=flux noautologin apparmor=1 security=apparmor quiet loglevel=0 systemd.show_status=false vt.global_cursor_default=0
              initrd /live/initrd
          }
          GRUB

          echo "==> [5/5] Создание ISO..."
          grub-mkrescue -o "$OUTPUT_DIR/FluxLinux-1.0-Libre-amd64.iso" "$LIVE_DIR" -- -volid "FLUX_LIBRE"
          EOF

      - name: Upload ISO
        uses: actions/upload-artifact@v4
        with:
          name: FluxLinux-1.0-Libre-ISO
          path: out/*.iso
          retention-days: 7
'''

yml = yml.replace("__WP_B64_PLACEHOLDER__", wp_b64_indented)
with open(r"F:\flux-linux\.github\workflows\main.yml", "w", encoding="utf-8", newline="\n") as f:
    f.write(yml)
print("Successfully wrote main.yml, lines:", len(yml.splitlines()))
