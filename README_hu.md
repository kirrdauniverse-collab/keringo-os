# Keringő OS 0.5.1 — „MI és képernyők”

**Saját kernel. Saját grafika. Saját ötlet.** Nem Linux, nem fork — nulláról
írt, csupasz fém i386 operációs rendszer, magyar identitással.

## Az ötlet: a használat gravitációja

A panelek pályákon **keringenek** a rendszer magja (a „nap") körül. A trükk a
**használati gravitáció**: amit sűrűn használsz, a használat súlya alatt
**befelé kering** (közelebb a maghoz), amit nem használsz, az kifelé sodródik.
Az asztal magától átrendeződik a szokásaid szerint — ilyet még nem látott senki.

## Mit tud a 0.5.1?

| Réteg | Képesség |
|---|---|
| Kernel | GDT/IDT, PIC remap, PIT 100 Hz, **preemptív többfeladat** (round-robin, IRQ0), 4 KB-os lapozás + MMIO-térképezés, bump-heap, **PC-hangszóró** (PIT 2. csatorna) |
| Eszközök | PS/2 billentyűzet + **egér (javított sorkezeléssel!)**, ATAPI CD, ATA merevlemez, **USB pendrájv (UHCI/EHCI, FAT32 olvasás+írás)**, **PCI**, **hálózat: e1000/RTL8139 + DHCP/TCP/UDP/DNS/HTTP** |
| **Wi-Fi (0.4)** | **Atheros AR9285 (PCIe) meghajtó** — 2.4 GHz, HT20, legacy sebességek; **802.11g STA** (szken, auth/assoc); **WPA2-PSK**: PBKDF2-SHA1 PMK, PRF PTK, 4-way EAPOL, **CCMP** (saját SHA1/HMAC/AES); **Wi-Fi panel (F6)**; QEMU-hoz **demo rádió** (virtuális AP-k valódi kriptóval) |
| Fájlrendszer | ISO9660 (`/cd`), RAMFS (`/mem`), **FAT32 olvasás+ÍRÁS** (`/sys` HDD, `/usb` pendrájv; 0.5: **formázás**, törlés, átnevezés), partíció-szkenner (MBR + logikai lánc) |
| **Műhely (0.5)** | **QUARTZ-4B** (a KIMI-MATE 4 bites gép portja BASIC-szel és játékokkal), **Partíciókezelő**, **Telepítő** (más oprendszerek mellé), **Keringő Commander** (duplapaneles fájlkezelő hex-nézővel és .KPA-tömörítővel), **Távvezérlő** (KRVP1: képernyő-megosztás + távoli bemenet) |
| **KIRRDA AI (0.5.1)** | **Helyi, szabály-alapú MI-asszisztens** — kérdezz, számoltat („12*34+7"), appot indít („indítsd a festőt"), lemezen keres („keres: cfg"), **tanítható** („tanits: kérdés = válasz", KIRRDA.TXT), és **internetezik**: „internet: http://cim" + „dns: név" (HTTPS nélkül — őszintén). Ő nem nagy nyelvi modell, de a saját agyacska a gépen fut, offline is |
| Böngésző | **0.5: valódi TCP-n működik** (a rejtett tcp_send FIN-abort hiba javítva) — HTML-részhalmaz: címek, bekezdések, listák, linkek, **JPEG-képek**, **MJPEG élő videó**. ŐSZINTÉN: **JavaScript, CSS és HTML5 nincs** (az teljes böngészőmotort igényelne), **HTTPS/TLS sincs** — csak sima `http://` oldalak |
| Grafika | **0.5.1: ALAPÉRTELMEZETT 640×480×16** (a multiboot-fejléc videó-mezőit a spec szerinti 32. offsetre tettük — eddig a GRUB sosem látta őket, és mindig EDID-auto 1280×800-cal bootolt!). GRUB-menüből: **640×480×8** (max. kompatibilis, RGB332 paletta), **640×480×32**, **1024×768×32**, **auto**. A kernel 32 bites háttérbufferben rajzol, és 8/16/24/32 bites módba konvertál. **Szép mód**: élsímított vonalak/körök, gradiensek, lekerekítés, árnyékok. Dupla puffer, saját 8×8 font **magyar ékezetekkel** (ő ű ű Ű kézi bitkép), saját 3D vetítő, textúrázott keringő panelek, **raycaster** |
| Felület | 10 keringő panel: Mag, Óra, Feladatok, Jegyzet, Lemez, Róla, Frissítés, Böngésző, Beállítás, **Programok** (alkalmazásindító) — HU/EN (F1). **0.5.1: Indítópanel (F5)** — teljes képernyős, kategóriákkal, azonnali kereséssel, egérrel is |
| **Programok (0.3)** | **Festő**, **Szövegszerkesztő**, **Labirintus 3D**, **Pokoljáró**, **Csillagvadász**, **Városépítő**, **Űrkadét Flipper**, **Számológép**, **Képernyővédők** (5 db), egérrel és billentyűzettel is |
| Frissítés | `KERINGO.UPD` csomag **pendrájvról vagy HTTP-ről**, telepítés `/sys`-be, élő (kexec) frissítés, visszaállítás |
| Stabilitás | QEMU-ban végigvezetett, egérrel+k billentyűzettel vezérelt teljes alkalmazás-teszt: 0 kivétel |

## Programok 0.3

| Program | Mit tud | Indítás |
|---|---|---|
| **Wi-Fi** | hálózatkeresés (1/6/11), jelerősség, WPA2 jelszó, állapot + IP; csatlakozás után a böngésző és minden hálózati funkció Wi-Fi-n megy | `F6` vagy 0 |
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
programokat; az appokban `Esc` = vissza az asztalra. **F5 = Indítópanel**:
kategóriák (Tab), keresés (csak gépelj!), nyilak + Enter, egérrel kattintásra
is indul — minden app egy képernyőn, rövid leírással.

### KIRRDA AI 0.2 — a helyi „kisagy" (0.5.1)

Kérdezz tőle természetesen (ékezetek nélkül gépelve):
`szia` · `help` · `12*34+7` · `keres: cfg` · `inditsd a festot` ·
`tanits: ki a fonok = en vagyok` → `ki a fonok?` · `internet: http://10.0.2.2:8000/`
· `dns: example.com` · `jegyezd: ...` / `emlekszel` / `elfelejt: szo`.
A tanult tudás a `KIRRDA.TXT`-ben (/mem + /sys) megmarad újraindítás után is.
Őszintén: szabály-alapú agy, nem nagy nyelvi modell; HTTPS-et nem tud (nincs TLS).

## Irányítás

| Billentyű / egér | Hatás |
|---|---|
| Nyilak vagy bal gombos húzás | kamera forgatás (appokban: saját kezelés) |
| `+` / `-` | zoom |
| `Tab` | következő panel; `Enter` = belépés; `Esc` = vissza |
| `F1` | magyar ⇄ angol |
| `1`–`9` (galaxisban) | program indítása |
| `F3 F4 F6 F7 F9 F10 F11 F12` | Festő, Szöveg, Wi-Fi, Labirintus, Pokoljáró, Csillagvadász, Város, Flipper |
| `F2` | Jegyzet mentése / Paint BMP mentése / Szöveg mentése |
| `F5` | **Indítópanel** (galaxisban); Böngésző újratöltés; Lemez-panel frissítés |
| `F8` | Beállítások mentése |

## Indítás

### QEMU-ban (teszt)
```bash
qemu-system-i386 -cdrom keringo-0.5.1.iso -boot d
```
A GRUB-menüben 5 mp-ig választhatsz módot: 640×480×16 (alap), 640×480×8,
640×480×32, 1024×768×32, auto. QEMU-ban a **user networking** (`-netdev user`)
ad DHCP-t, DNS-t (10.0.2.3) és kimenő TCP-t — így a KIRRDA internet-parancsai
és a böngésző is működnek (a hoston futó szervereket `10.0.2.2`-n éred el).

### Valódi gépen (Ventoy)
Az ISO-t tedd Ventoy stickre, bootolj **legacy/CSM** módból
(ASUS X551MA: Secure Boot KI, Launch CSM BE).

### Wi-Fi valódi gépen
Az ISO `KERINGO.CFG`-jében alapból `wifi_demo=1` áll (QEMU-teszthez).
Valódi kártya esetén **nem kell törölni**: ha a rendszer Atheros AR9285-öt
talál (0x168C:0x002B), a demo automatikusan kikapcsol és a valódi rádióvesz.
- Támogatott: **AR9285** (az X551MA tipikus kártyája), 2.4 GHz, WPA2-PSK
  (CCMP) és nyílt hálózat. A CCMP titkosítás szoftveres.
- **Mezőn letesztendő**: az RF-kezdeményezés (reset/ini/kalibráció) a Linux
  ath9k sorozatait követi, de valódi hardveren még nem futott — ha nem
  talál hálózatot, soros kimeneten `ath9k:` üzenetek jelzik a fázist.
- Nem támogatott (0.5 terv): AR9485/AR9003-útvonal, 802.11n, WPA3.

### GRUB-menübe a másik rendszer mellé
```bash
menuentry "Keringő OS 0.5.1" {
    multiboot /boot/keringo.elf
    set gfxpayload=1024x768x32      # a multiboot UTÁN — így tényleg érvényesül!
    boot
}
```
(A `/boot/keringo.elf` az ISO-ból másolandó. **Fontos**: a `set gfxpayload`
a `multiboot` sor UTÁN álljon — a betöltő a fejlécből eredetileg 640×480×16-ot
kér; az utána állított gfxpayload felülírja. Elérhető: `640x480x8`,
`640x480x16/32`, `1024x768x32`, `1280x1024x32` stb.)

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
- **0.4.0** — Wi-Fi (AR9285, WPA2-PSK/CCMP), KIRRDA AI 0.1 (Mega Pack),
  Obszervatórium, Lab, Orbit Miner, ZX81+QUARTZ alapok
- **0.5.0** — Műhely: QUARTZ-4B, Partíciókezelő, Telepítő, Commander,
  Távvezérlő (KRVP1); PS/2 AUX + TCP checksum javítások
- **0.5.1** — **KIRRDA AI 0.2** (matek, app-indítás, keresés, tanítás,
  internet+DNS), **Indítópanel**, **640×480×16 valódi alap** (multiboot-fejléc
  javítás — eddig mindig EDID-auto bootolt!), 8 bites mód RGB332-palettával,
  bpp-agnosztikus grafika, Szép mód (élsímítás+gradiens), DNS-fogadás javítása

## Útiterv (0.4+)

- Wi-Fi: AR9485/AR9003 meghajtó-útvonal, 802.11n (HT20/40), TX-erő az
  EEPROM-pdadc táblákból, rate adaptation, ath9k mezőn-teszt visszajelzések
- teljesítmény: rostázott (clipped) textúrarajzolás, soronkénti gyorsított másolás
- Festő: Betöltés BMP-ből; Szöveg: keresés, mozgó blokk
- játékok: Hangok (PC speaker zene), Pokoljáró: két pálya + főnök
- hálózat: TFTP-s kernelszinkron, időszinkron (SNTP)


## Keringő Mega Pack

A fejlesztői ág natív alkalmazásai: **Keringő BASIC**, **KIRRDA AI**
(a 0.5.1-ben kinőve valódi képességekkel — lásd fentebb), **Keringő
Obszervatórium**, **Keringő Lab** és **Orbit Miner**. Ezek a meglévő
app-keretrendszert és a framebufferes grafikai API-t használják, külső
függőségek nélkül.

A meglévő **Pokoljáró** a beépített DOOM-hommage raycaster. A következő portfázis célja egy külön, valódi WAD/IWAD-alapú DOOM futtatóréteg, amelyhez a játékadatok nem kerülnek a repóba.
