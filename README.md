# Keringő OS — the orbiting desktop

**Own kernel. Own graphics. Own idea.** A from-scratch, bare-metal i386
operating system with a Hungarian identity — not a Linux distro, not a fork.

The desktop concept: **usage gravity**. Panels orbit the system core on
elliptical tracks; whatever you use often is pulled *inward*, whatever you
ignore drifts *outward*. The desktop rearranges itself around your habits.

![Keringo OS](https://img.shields.io/badge/kernel-i386%20bare--metal-e8b24a)
![Version](https://img.shields.io/badge/version-0.3.0-248bbe)

## What's inside (0.3.0 "Játéktér" / Playground)

| Layer | Capability |
|---|---|
| Kernel | GDT/IDT/PIC remap, PIT 100 Hz, **preemptive multitasking** (round-robin on IRQ0), 4 KB paging + MMIO mapping, bump heap, **PC speaker** driver |
| Devices | PS/2 keyboard + mouse, ATAPI CD, ATA HDD, **USB flash drives (UHCI/EHCI MSC, FAT32 read+write)**, PCI bus scan, **NICs: e1000 / RTL8139** |
| Network | DHCP, TCP, UDP, DNS, HTTP — all in-kernel, no libc |
| Filesystems | ISO9660 (`/cd`), RAMFS (`/mem`), FAT32 (`/sys` HDD, `/usb` stick), MBR + logical partition scanner |
| Graphics | VBE linear framebuffer (any mode the BIOS gives, e.g. 1024×768 or 1280×800×32), double buffering, custom 8×8 font with hand-drawn Hungarian glyphs (ő ű), custom 3D projector, textured orbiting panels, **raycaster engine** |
| Desktop | 10 orbiting panels: Core, Clock, Tasks, Notes, Files, About, Update, Browser, Settings, **Apps** — HU/EN (F1) |
| Apps | **Paint**, **Text editor** (UTF-8, Hungarian accents), **Labyrinth 3D** (Wolfenstein homage), **Hellwalker** (Doom homage), **Star Hunter** (Elite homage), **City Builder** (SimCity homage), **Space Cadet Pinball** (Win95 homage), **Calculator**, **5 screensavers** |
| Updates | `KERINGO.UPD` packages from USB stick or HTTP, install to `/sys`, **live update via kexec**, rollback |

## Quick start

```bash
qemu-system-i386 -cdrom keringo-0.3.0.iso -boot d
```

On real hardware: burn or Ventoy (legacy/CSM boot), or add to GRUB:

```
menuentry "Keringő OS 0.3.0" {
    set gfxpayload=1024x768x32
    multiboot /boot/keringo.elf
}
```

## Controls (galaxy view)

| Input | Action |
|---|---|
| Arrows / mouse drag | rotate camera |
| `+` / `-` | zoom |
| `Tab`, `Enter`, `Esc` | select / focus panel / back |
| `1`–`9` | launch application |
| `F3 F4 F7 F9 F10 F11 F12` | Paint, Text, Labyrinth, Hellwalker, Star Hunter, City, Pinball |
| `F1` | HU ⇄ EN |

## Build

```bash
cd src
make iso    # needs gcc -m32, grub-mkimage, xorrisofs
make run    # boots it in QEMU with serial logging
```

Source layout (all C + a little GAS, ~11k lines):

```
src/
  boot.S isr.S kexecasm.S     entry stubs
  kernel.h                    shared API
  cpu.c mem.c task.c ps2.c    core: GDT/IDT/PIT, paging, scheduler, input
  serial.c                    kprintf/ksnprintf
  gfx.c render3d.c kosz.c     framebuffer, font, 3D projector, math
  ata.c iso9660.c fat32.c     storage
  usb*.c msc.c pci.c          USB host + mass storage
  net.c tcp.c http.c e1000.c rtl8139.c
  vfs.c update.c kexec.c      VFS, update packages, live kernel swap
  ui.c                        orbiting desktop
  apps.c app_paint.c app_text.c app_calc.c saver.c sound.c
  game_ray.c game_elite.c game_city.c game_pin.c
```

## A note on the QEMU ATAPI quirk

QEMU 10's ATAPI emulation returns zeroes for sectors after the boot image
region — so the ISO is built with `--sort-weight` to push the boot image to
the *end* of the disc. `0UDVOZLET.TXT` ("greeting") sits in the first data
sector — hence its name.

## For AI/agent contributors

This project welcomes contributions from other models and humans alike.
The codebase is deliberately dependency-free (no libc, no external tooling
beyond gcc/grub/xorriso). Every subsystem prints diagnostics on COM1 —
boot with `-serial stdio` to see them. The UI is bilingual: all user-facing
strings are `{hu, en}` pairs — please keep both in sync.

## License

See [LICENSE](LICENSE). — Keringő OS is a hobby OS experiment; use it, break it,
extend it.

## Version history

- **0.1.0** — orbiting desktop, kernel, CD reading, notes
- **0.2.x** — USB FAT32, update packages, browser + live video, wallpapers,
  icon themes, network stack, JPEG decoder
- **0.3.0** — nine applications, five screensavers, PC speaker,
  critical mouse-event-queue fix, resolution-independent apps
