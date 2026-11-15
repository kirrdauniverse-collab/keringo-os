# Keringő OS 0.1.0 — „Első keringés"

**Saját kernel. Saját grafika. Saját ötlet.** Nem Linux, nem fork, nem fork-fork —
nulláról írt, csupasz fém i386 operációs rendszer, magyar identitással.

## Az ötlet: a használat gravitációja

A panelek pályákon **keringenek** a rendszer maga (a „nap") körül. A trükk a
**használati gravitáció**: amit sűrűn használsz, az a használat súlya alatt
**befelé kering** (közelebb a maghoz, gyorsabban), amit nem használsz, az
**kifelé sodródik**. Az asztal tehát magától átrendeződik a szokásaid szerint —
ilyet asztali rendszerben még nem látott senki.

## Mit tud az 0.1.0?

| Réteg | Képesség |
|---|---|
| Kernel | GDT/IDT, PIC remap, PIT 100 Hz, **preemptív többfeladat** (round-robin, IRQ0), 4 KB-os lapozás (identity RAM + framebuffer térkép), bump-heap |
| Eszközök | PS/2 billentyűzet + egér (IRQ1/IRQ12), PIT, soros konzol (COM1, debug) |
| Fájlrendszer | ATAPI (CD) PIO olvasás + **ISO9660** (`/cd`, olvasható) + **RAMFS** (`/mem`, írható) minimál VFS-sel; a `/cd` fájljait bootkor előreolvassa |
| Grafika | VBE lineáris framebuffer (1024×768 vagy 1280×800×32), dupla puffer, saját 8×8 font **magyar ékezetekkel** (ő Ő ű Ű kézi bitkép), csillagtér, pályagyűrűk, pulzáló mag, textúrázott 3D panelek |
| Felület | 6 panel: **Mag** (rendszerinfo), **Óra** (keringő-mutatós analóg), **Feladatok** (tick-megoszlás + gravitáció-csíkok), **Jegyzet** (írható, menthető), **Lemez** (fájlkezelő), **Róla**; **HU/EN váltó (F1)** |
| Stabilitás | QEMU-ban a teljes interakciós teszt (26+ s folyamatos forgatás-gépelés-mentés) hibátlan |

## Indítás

### QEMU-ban (teszt)
```bash
qemu-system-i386 -cdrom keringo-0.1.0.iso -boot d
```

### Valódi gépen (Ventoy)
A `keringo-0.1.0.iso`-t tedd Ventoy stickre, bootolj **legacy/CSM** módból.
A régi laptopodon (ASUS X551MA) ehhez: Secure Boot KI, Launch CSM BE,
boot a nem-UEFI USB-bejegyzésről.

### Valódi gépen (GRUB-menübe a többi rendszer mellé)
Az ISO-t csatold fel és másold ki a kernelt, majd a GRUB-ba:
```bash
menuentry "Keringő OS 0.1.0" {
    multiboot /boot/keringo.elf
}
```
(A `/boot/keringo.elf` az ISO-ból másolandó a telepítőpartíció `/boot`-jába.)

## Irányítás

| Billentyű / egér | Hatás |
|---|---|
| Nyilak vagy bal gombos húzás | kamera forgatás |
| `+` / `-` | zoom (repülj közelebb / távolodj) |
| `Tab` | következő panel kijelölése |
| `Enter` | belépés a kijelölt panelbe (fókusz-nézet) |
| `Esc` | vissza a galaxisba |
| `F1` | magyar ⇄ angol felület |
| `F2` | a Jegyzet mentése a `/mem/jegyzet.txt`-be |
| egérkattintás panelre | fókuszba veszi |

A **Jegyzet** panelben fókuszban bármit gépelhetsz; a **Lemez** panelben
Fel/Le választ, Enter megnyit — próbáld ki a `0UDVOZLET.TXT`-t: azt a rendszer
a **saját boot CD-jéről** olvasta be!

## Újraépítés

```bash
cd src
make iso      # kernel.elf + keringo-0.1.0.iso (gcc -m32 + grub-mkimage + xorrisofs kell)
make run      # QEMU-ban soros loggal
```

## Fájlrendszer-jegyzet (érdekesség)

QEMU 10 ATAPI emulációja a boot-képfájl régióján túli szektorokra nullákat ad
vissza — ezért az ISO-t úgy építjük (`--sort-weight`), hogy a boot-kép a lemez
**végére** kerüljön, minden olvasandó tartalom elé. A `0UDVOZLET.TXT` fájl az
 első adatszektorban ül — innen a neve.

## Útiterv (0.2+)

- ext2 olvasás / fájl mentése lemezre
- ring-3 user feladatok + syscall-ok
- több panel: rajzoló, zene, "orbit-térkép"
- igazi BeOS-ízesület: csúszó panel-csere (desktop-csoportok keringenek)

*Keringő OS — Z.ai építette, 2026. november — „az asztal, ami kering."*
