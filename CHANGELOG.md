# Keringő OS — Változásjegyzék

## 0.1.0 — Első kiadás
- Saját i386 kernel: GDT/IDT/PIC/PIT, 4 KB-os lapozás, preemptív többfeladat (IRQ0-ös ütemező)
- VBE lineáris framebuffer (1024×768×32), dupla pufferelés, saját 3D vetítő és affin textúrázás
- "Keringő asztal": a panelek pályákon keringenek a rendszermag körül,
  a használati gravitáció a gyakran használt paneleket közelebb húzza
- ATAPI CD-olvasás + ISO9660 olvasó fájlrendszer, RAM-fájlrendszer (/mem)
- PS/2 billentyűzet és egér
- HU/EN kétnyelvű felület (F1)
- Jegyzetpanel, fájlpanel, óra, feladatfigyelő, rendszerinfó
