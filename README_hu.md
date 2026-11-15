# Keringő OS 0.3.0 — „Játéktér"

**Saját kernel. Saját grafika. Saját ötlet.** Nem Linux, nem fork — nulláról
írt, csupasz fém i386 operációs rendszer, magyar identitással.

## Az ötlet: a használat gravitációja

A panelek pályákon **keringenek** a rendszer magja (a „nap") körül. A trükk a
**használati gravitáció**: amit sűrűn használsz, a használat súlya alatt
**befelé kering** (közelebb a maghoz), amit nem használsz, az kifelé sodródik.
Az asztal magától átrendeződik a szokásaid szerint — ilyet még nem látott senki.

## Mit tud a 0.3.0?

| Réteg | Képesség |
|---|---|
| Kernel | GDT/IDT, PIC remap, PIT 100 Hz, **preemptív többfeladat** (round-robin, IRQ0), 4 KB-os lapozás + MMIO-térképezés, bump-heap, **PC-hangszóró** (PIT 2. csatorna) |
| Eszközök | PS/2 billentyűzet + **egér (javított sorkezeléssel!)**, ATAPI CD, ATA merevlemez, **USB pendrájv (UHCI/EHCI, FAT32 olvasás+írás)**, **PCI**, **hálózat: e1000/RTL8139 + DHCP/TCP/UDP/DNS/HTTP** |
| Fájlrendszer | ISO9660 (`/cd`), RAMFS (`/mem`), **FAT32** (`/sys` HDD-n, `/usb` pendrájvon), partíció-szkenner (MBR + logikai lánc) |
| Grafika | VBE lineáris framebuffer (a gép által felajánlott módban, pl. 1024×768 vagy 1280×800×32), dupla puffer, saját 8×8 font **magyar ékezetekkel** (ő ű ű Ű kézi bitkép), saját 3D vetítő, textúrázott keringő panelek, **raycaster** (texturált falak + sprite-billboardok) |
| Felület | 10 keringő panel: Mag, Óra, Feladatok, Jegyzet, Lemez, Róla, Frissítés, Böngésző, Beállítás, **Programok** (alkalmazásindító) — HU/EN (F1) |
| **Programok (0.3)** | **Festő**, **Szövegszerkesztő**, **Labirintus 3D**, **Pokoljáró**, **Csillagvadász**, **Városépítő**, **Űrkadét Flipper**, **Számológép**, **Képernyővédők** (5 db), egérrel és billentyűzettel is |
| Frissítés | `KERINGO.UPD` csomag **pendrájvról vagy HTTP-ről**, telepítés `/sys`-be, élő (kexec) frissítés, visszaállítás |
| Stabilitás | QEMU-ban végigvezetett, egérrel+k billentyűzettel vezérelt teljes alkalmazás-teszt: 0 kivétel |

## Programok 0.3

| Program | Mit tud | Indítás |
|---|---|---|
| **Festő** | ceruza, vonal, téglalap, teli téglalap, ellipszis, szóró, radír, **töltés**; 24 szín, ecsetméret, visszavonás, **BMP mentés** a `/sys`-re | `F3` vagy 1 |
| **Szövegszerkesztő** | UTF-8, **magyar ékezetes gépelés** (ő=[-hely, é=;-hely, AltGr+O=ó…), kurzor, görgetés, fájlmegnyitás minden tárolóról, mentés `/sys`+`/mem` | `F4` vagy 2 |
| **Labirintus 3D** | Wolfenstein-hommage raycaster: texturált falak, **minimap**, találd meg a zöld portált | `F7` vagy 3 |
| **Pokoljáró** | Doom-hommage: szörnyek üldöznek és támadnak, **fegyver, HP, lőszer, gyilkosszámláló**; minden szörny után nyílik a portál | `F9` vagy 4 |
| **Csillagvadász** | Elite-hommage: drótváz űrhajók 3D-ben, fordulózás, gyorsítás, **lézerek, radar, hullámok** | `F10` vagy 5 |
| **Városépítő** | SimCity-hommage: utak, lakó/kereskedő/ipari zónák, park, erőmű; **gazdaság: adó, karbantartás, áram, munka, boldogság** | `F11` vagy 6 |
| **Űrkadét Flipper** | Win95 Space Cadet-hommage: **üttők, bumperek, küldetés-célkák, dugattyú**, pontok, 3 golyó | `F12` vagy 7 |
| **Képernyővédők** | csillagmező, plazma, keringés, mátrix eső, pattogó logó; kézzel is indítható, **90 mp tétlenség után magától** | 8 |
| **Számológép** | 4 művelet, %, gyök, négyzet, 1/x, egérrel és billentyűzettel | 9 |

A galaxis nézetben az **1–9 számok** és az `F`-gombok bárhonnan indítják a
programokat; az appokban `Esc` = vissza az asztalra.

## Irányítás

| Billentyű / egér | Hatás |
|---|---|
| Nyilak vagy bal gombos húzás | kamera forgatás (appokban: saját kezelés) |
| `+` / `-` | zoom |
| `Tab` | következő panel; `Enter` = belépés; `Esc` = vissza |
| `F1` | magyar ⇄ angol |
| `1`–`9` (galaxisban) | program indítása |
| `F3 F4 F7 F9 F10 F11 F12` | Festő, Szöveg, Labirintus, Pokoljáró, Csillagvadász, Város, Flipper |
| `F2` | Jegyzet mentése / Paint BMP mentése / Szöveg mentése |
| `F5` | Böngésző újratöltés; Lemez-panel frissítés |
| `F8` | Beállítások mentése |

## Indítás

### QEMU-ban (teszt)
```bash
qemu-system-i386 -cdrom keringo-0.3.0.iso -boot d
```

### Valódi gépen (Ventoy)
Az ISO-t tedd Ventoy stickre, bootolj **legacy/CSM** módból
(ASUS X551MA: Secure Boot KI, Launch CSM BE).

### GRUB-menübe a többi rendszer mellé
```bash
menuentry "Keringő OS 0.3.0" {
    set gfxpayload=1024x768x32
    multiboot /boot/keringo.elf
}
```
(A `/boot/keringo.elf` az ISO-ból másolandó; a `gfxpayload` a szép,
kompakt felbontásért.)

## Frissítés pendrájvról (0.2 óta)

FAT32 pendrájv gyökérére tedd a `KERINGO.UPD` csomagot → Frissítés panel →
`U` (keresés), `T` (telepítés a `/sys`-re) vagy `L` (élő frissítés kexec-kel,
újraindítás nélkül). Hálózatról: `H` a Beállításokban megadott URL-ről.

## Újraépítés

```bash
cd src
make iso      # gcc -m32 + grub-mkimage + xorrisofs kell
make run      # QEMU soros loggal
```

## Fájlrendszer-jegyzet (érdekesség)

A QEMU 10 ATAPI emulációja a boot-képfájl régióján túl nullákat ad vissza —
ezért az ISO-t `--sort-weight`-tel úgy építjük, hogy a boot-kép a lemez
**végére** kerüljön. A `0UDVOZLET.TXT` az első adatszektorban ül — innen a neve.

## Verziótörténet

- **0.1.0** — keringő asztal, kernel, CD-olvasás, jegyzet
- **0.2.x** — pendrájv (FAT32), frissítések, böngésző + élő videó, háttérképek,
  ikontémák, hálózat (DHCP/TCP/HTTP/DNS), JPEG-dekóder
- **0.3.0** — Programok: Festő, Szövegszerkesztő, Labirintus 3D, Pokoljáró,
  Csillagvadász, Városépítő, Űrkadét Flipper, Számológép, 5 képernyővédő,
  PC-hangszóró; **egér-sor hibajavítás** (a 0.1 óta a belső eseménysor
  fejére írtunk a farka helyett — mostantól az egér valóban mozog)

## Útiterv (0.4+)

- teljesítmény: rostázott (clipped) textúrarajzolás, soronkénti gyorsított másolás
- Festő:Betöltés BMP-ből; Szöveg: keresés, mozgó blokk
- játékok: Hangok (PC speaker zene), Pokoljáró: két pálya + főnök
- hálózat: TFTP-s kernelszinkron, időszinkron (SNTP)
