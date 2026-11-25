# Keringő OS — Változásjegyzék

## 0.5.0 — „Műhely" (2026-11-25)

A felhasználói hibajelentésekre épülő javító- és bővítőkiadás: 5 új alkalmazás,
3 sürgős hibajavítás, monitor-biztos boot, és a bejelentett irányító-eszközök.

- **QUARTZ-4B (KIMI-MATE)**: a repóba feltöltött 4 bites gép teljes Keringő-portja —
  16 utasításos CPU, 4096 nibble RAM, szöveges (32×16) és grafikus (64×48) mód,
  teljes BASIC (PRINT @, PLOT/LINE/BOX/CIRCLE/TRIANGLE/FILL, IF/FOR/WHILE/GOSUB,
  PEEK/POKE, fájlkezelés). Saját szerkesztő bal/jobb kurzorral és
  parancs-előzményekkel (Fel/Le — pont, amit a HTML-verzióban hiányoltál!).
  Beépített játékok: PONG, METEOR, KÍGYÓ + DEMO/FILL/HELLO. A `KEY()` bővítéssel
  a nyilak játékvezérlésre is szolgálnak; F1 súgó, F5 gépi kód hex-betöltő.
- **Partíciókezelő**: MBR-tábla listázása, FAT32-partíció létrehozása (N),
  törlése (D), aktív jelző (A), FAT32-formázás (F) — mindegyik Y-megerősítéssel.
- **Telepítő**: más oprendszerek MELLÉ telepít (Linux Lite / GRUB-mellett):
  FAT32-re formáz, majd a kernelt és az adatfájlokat kimásolja, jelölőt ír,
  és GRUB_UTASITAS.TXT-t generál a Linux oldali beállításhoz.
- **Keringő Commander**: duplapaneles fájlkezelő (/cd, /mem, /sys, /usb) —
  másolás (F5), átnevezés (F6), törlés (F8), archiválás/kicsomagolás (.KPA,
  RLE, F7), és hex/dec/bin/karakter nézőke SZEKRESZTŐVEL (F3) + szövegszerkesztő (F4).
- **Távvezérlő (KRVP1)**: két Keringő gép közt — a kiszolgáló megosztja a
  képernyőjét (32×32-as csempék RLE-tömörítéssel) és a távoli billentyűzet/egér
  eseményeit a saját asztalába injektálja; fájlok is átvihetők (GET/PUT).
  QEMU-ban TCP 7331-en; a host↔vendég útvonal teljes, a vendég↔vendég
  kettős-NAT útvonalon a kézfogás megy, a stream lassabban (0.6-ban javítjuk).
- **FAT32-bővítések**: formázás (fat32_format), törlés, átnevezés.
- **ZX81**: Z80-mag a DD/FD (IX/IY) prefixekkel; a valódi 8 KB-os ROM a
  lemezről automatikusan betöltődik (adatok/ZX81.ROM), display-file renderelés
  saját karakterkészlettel. A ROM NEM érkezett meg a repóba — töltsd fel, és
  azonnal él az útvonal.
- **Monitor-biztos boot**: a multiboot-fejléc most 640×480×32-t kér (a régi
  CRT monitorok miatt!), a GRUB-menüben 1024×768 / 1280×800 is választható,
  grafikai hiba esetén pedig magyar VGA-szöveges üzenet jelenik meg.
- **Képernyővédők**: +2 (Meteorok, Tábortűz) — összesen 7.

**Hibajavítások (a bejelentéseidre):**
1. **Festő**: az eszköztár, a paletta (24 szín) és az ecsetméret most KATTINTHATÓ —
   korábban se szín, se alakzat nem választható, csak borostyán ceruzával lehetett
   rajzolni (a kattintásokat senki nem kezelte!). [+]: a gombok rajzolva is vannak.
2. **Űrkadét Flipper**: az ütők szimmetrikus geometriája (a bal ütő lefelé állt —
   ez volt az „oldalt vannak a golyó ütői"), plusz több gomb (Z/X, nyilak,
   Ctrl/Shift) és nyílfel-le indítás. Gombok: kiterjedtebb súgó.
3. **A keringő ablakok (panelek)**: „P" = keringés megállítása (a panelek
   helyben maradnak — végre elérhetők!), egér-közeledésre a panel lelassul
   („mágneses panelek"), és a kattintási tűréshatár is nőtt (+36 px).

**Mélyebb javítások, amelyeket a fentiek ástak elő:**
- **PS/2 AUX-elválasztás**: a billentyűzet-IRQ elolvasta az egér bájtjait és
  fordítva (a 8042 kimeneti puffere közös) — megtörténtek billentyűvesztések.
  Most mindkét kezelő ellenőrzi a státusz-AUX bitet.
- **TCP**: (a) a `tcplen`-et az IP-összhossz-mezőből olvastuk — a második
  ellenőrzés minden bejövő TCP-csomagot eldobott (eddig rejtve, mert a HTTP
  a demo rádión át ment); (b) valódi TCP-ellenőrző összeg + hálózati bájtsorrend
  az ablak/összeg mezőknél (a korábbi kis-endian írásokat a valós stackok eldobják).
- **Multiboot-info**: a framebuffer_bpp BÁJT (nem u32) — az ellenőrzés most helyes.

Megjegyzések: ext2/NTFS-olvasás még nincs (FAT32 olvasás+írás van) — a 0.6-os
tervben szerepel; a QUARTZ-4B-ben a kifejezések teljes értéken értékelődnek
(a 4 bites maszk a változóknál él) — ld. az F1 súgót.

## 0.4.0 — „Wi-Fi" (2026-11-16)

- **Wi-Fi alrendszer nulláról**: 802.11g STA (szken 1/6/11, auth/assoc),
  WPA2-PSK 4-way EAPOL kézfogás, CCMP titkosított adatkeretek,
  ethernet-híd a meglévő DHCP/TCP/DNS/HTTP verem alá.
- **Saját kripto (crypto.c)**: SHA-1, HMAC-SHA1, PBKDF2, 802.11i PRF
  (hostap-kompatibilis), AES-128, CCMP; boot-idő önteszt hivatalos
  vektorokkal (FIPS-197, RFC 2202, RFC 6070) + 50x336B CCMP terheléses
  önteszt — QEMU-ban átmegy.
- **Atheros AR9285 (PCIe) meghajtó (ath9k.c)**: 2.4 GHz HT20, legacy
  sebességek, polling; reset/ini/kalibráció a Linux ath9k (GPL-2.0)
  regisztersorozatai alapján (generált fejlécek); valódi hardveren
  **mezőn letesztendő**.
- **Wi-Fi panel (F6)**: hálózatlista jelerősséggel, WPA2 jelszóbevitel,
  állapot + IP.
- **Demo rádió** (KERINGO.CFG: wifi_demo=1): két virtuális AP valódi
  kriptóval — a teljes kézfogás+DHCP QEMU-ban tesztelhető; valódi kártya
  esetén automatikusan kikapcsol.
- QEMU-ban végigtesztve: szken → WPA2 kézfogás → CCMP adat → DHCP cím
  (192.168.13.15), illetve a nyílt hálózati útvonal.
- **Javítások**: a heap a .bss-be lógott (~35 KB, linker __kernel_end);
  CCMP keystream puffertúlcsordulás; PBKDF2 blokk-számláló (4 bájt BE);
  PRF konstrukció a valós hostap-formára igazítva; Seq Control mező
  kis-endianen; DHCP mágus wire-sorrendben; assoc-válasz mezőrend;
  demo rádió csoport-kulcs (GTK) kezelése.


## 0.3.0 — „Játéktér"
- **Programok** (új 10. panel: alkalmazásindító; galaxisban 1–9 és F-gyorsgombok):
  - **Festő**: ceruza, vonal, téglalap, teli téglalap, ellipszis, szóró, radír,
    flood-töltés; 24 szín, ecsetméret, visszavonás, BMP-mentés a /sys-re
  - **Szövegszerkesztő**: UTF-8, magyar ékezetes gépelés (ő ű é á í ú ö ü ó),
    kurzorvezérlés, görgetés, fájlmegnyitás (/cd, /sys, /usb, /mem), mentés
  - **Labirintus 3D** (Wolfenstein-hommage): raycaster, 6 generált textúra,
    DDA, minimap, kijárat-portal
  - **Pokoljáró** (Doom-hommage): szörny-spriteok z-bufferrel, üldöző MI,
    fegyver animált torkolattűzzel, HP/lőszer/gyilkosszámláló, kötelező öldöklés
  - **Csillagvadász** (Elite-hommage): drótváz 3D, yaw/pitch fordulózás,
    lézerek, 3D-s radar, ellenség-hullámok, pontozás
  - **Városépítő** (SimCity-hommage): 24×17-es rács, 7 cellatípus, egér-kattintásos
    építés/bontás, gazdasági szimuláció (adó, karbantartás, áram, munka, boldogság)
  - **Űrkadét Flipper** (Win95-hommage): szegmens-ütközéses labdafizika, ütők,
    bumperek, küldetés-célkák, dugattyú, 3 golyó
  - **Számológép**: 4 művelet + % + gyök + x² + 1/x, saját double→szöveg
  - **Képernyővédők**: csillagmező, plazma, keringés, mátrix eső, pattogó logó;
    90 mp tétlenség után automatikus
- **PC-hangszóró**: PIT 2. csatornás beep + dallam-lejátszó (induló, találat, mentés)
- **Alkalmazás-keretrendszer** (apps.c): regiszter, enter/frame/key/mouse/exit
  hookok, egér-útvonal, közös UI-elemek, PRNG
- **EGÉR-HIBAJAVÍTÁS** (kritikus): a belső egér-eseménysor a fej-indexbe írt
  a farok helyett — a 0.1.0 óta az egér eseményei nem jutottak el a UI-hoz;
  továbbá a PS/2 inicializálás most fogyasztja a 0xFA nyugtákat és üríti a
  puffert, így nem csúszik el a csomag-értelmező
- Felbontás-független appok (a VBE által adott bármely módhoz igazodnak)
- F-gyorsbillentyűk minden nézetben; új billentyű-térkép (F1–F12, Del, Home, End)

## 0.2.1 (belső fejlesztői kiadás)
- élő frissítés kexec-kel (update_apply(1)), visszaállítás
- böngésző: élő MJPEG-szerű videó demó, HTML elemzés, linkek
- ikontémák, háttérképek (JPEG/BMP), kiemelőszín
- UHCI + EHCI USB Storage, FAT32 írás, ATA HDD, partíció-szkenner
- e1000/RTL8139 + DHCP/TCP/UDP/DNS/HTTP, kexec

## 0.1.0 — Első kiadás
- Saját i386 kernel: GDT/IDT/PIC/PIT, 4 KB-os lapozás, preemptív többfeladat
- VBE lineáris framebuffer, dupla pufferelés, saját 3D vetítő és affin textúrázás
- „Keringő asztal": pályákon keringő panelek, használati gravitáció
- ATAPI CD-olvasás + ISO9660, RAM-fájlrendszer, PS/2 billentyűzet és egér
- HU/EN kétnyelvű felület (F1)
# Keringő OS — Változásjegyzék

## 0.4.0 — „Wi-Fi" (2026-11-16)

- **Wi-Fi alrendszer nulláról**: 802.11g STA (szken 1/6/11, auth/assoc),
  WPA2-PSK 4-way EAPOL kézfogás, CCMP titkosított adatkeretek,
  ethernet-híd a meglévő DHCP/TCP/DNS/HTTP verem alá.
- **Saját kripto (crypto.c)**: SHA-1, HMAC-SHA1, PBKDF2, 802.11i PRF
  (hostap-kompatibilis), AES-128, CCMP; boot-idő önteszt hivatalos
  vektorokkal (FIPS-197, RFC 2202, RFC 6070) + 50x336B CCMP terheléses
  önteszt — QEMU-ban átmegy.
- **Atheros AR9285 (PCIe) meghajtó (ath9k.c)**: 2.4 GHz HT20, legacy
  sebességek, polling; reset/ini/kalibráció a Linux ath9k (GPL-2.0)
  regisztersorozatai alapján (generált fejlécek); valódi hardveren
  **mezőn letesztendő**.
- **Wi-Fi panel (F6)**: hálózatlista jelerősséggel, WPA2 jelszóbevitel,
  állapot + IP.
- **Demo rádió** (KERINGO.CFG: wifi_demo=1): két virtuális AP valódi
  kriptóval — a teljes kézfogás+DHCP QEMU-ban tesztelhető; valódi kártya
  esetén automatikusan kikapcsol.
- QEMU-ban végigtesztve: szken → WPA2 kézfogás → CCMP adat → DHCP cím
  (192.168.13.15), illetve a nyílt hálózati útvonal.
- **Javítások**: a heap a .bss-be lógott (~35 KB, linker __kernel_end);
  CCMP keystream puffertúlcsordulás; PBKDF2 blokk-számláló (4 bájt BE);
  PRF konstrukció a valós hostap-formára igazítva; Seq Control mező
  kis-endianen; DHCP mágus wire-sorrendben; assoc-válasz mezőrend;
  demo rádió csoport-kulcs (GTK) kezelése.


## 0.3.0 — „Játéktér"
- **Programok** (új 10. panel: alkalmazásindító; galaxisban 1–9 és F-gyorsgombok):
  - **Festő**: ceruza, vonal, téglalap, teli téglalap, ellipszis, szóró, radír,
    flood-töltés; 24 szín, ecsetméret, visszavonás, BMP-mentés a /sys-re
  - **Szövegszerkesztő**: UTF-8, magyar ékezetes gépelés (ő ű é á í ú ö ü ó),
    kurzorvezérlés, görgetés, fájlmegnyitás (/cd, /sys, /usb, /mem), mentés
  - **Labirintus 3D** (Wolfenstein-hommage): raycaster, 6 generált textúra,
    DDA, minimap, kijárat-portal
  - **Pokoljáró** (Doom-hommage): szörny-spriteok z-bufferrel, üldöző MI,
    fegyver animált torkolattűzzel, HP/lőszer/gyilkosszámláló, kötelező öldöklés
  - **Csillagvadász** (Elite-hommage): drótváz 3D, yaw/pitch fordulózás,
    lézerek, 3D-s radar, ellenség-hullámok, pontozás
  - **Városépítő** (SimCity-hommage): 24×17-es rács, 7 cellatípus, egér-kattintásos
    építés/bontás, gazdasági szimuláció (adó, karbantartás, áram, munka, boldogság)
  - **Űrkadét Flipper** (Win95-hommage): szegmens-ütközéses labdafizika, ütők,
    bumperek, küldetés-célkák, dugattyú, 3 golyó
  - **Számológép**: 4 művelet + % + gyök + x² + 1/x, saját double→szöveg
  - **Képernyővédők**: csillagmező, plazma, keringés, mátrix eső, pattogó logó;
    90 mp tétlenség után automatikus
- **PC-hangszóró**: PIT 2. csatornás beep + dallam-lejátszó (induló, találat, mentés)
- **Alkalmazás-keretrendszer** (apps.c): regiszter, enter/frame/key/mouse/exit
  hookok, egér-útvonal, közös UI-elemek, PRNG
- **EGÉR-HIBAJAVÍTÁS** (kritikus): a belső egér-eseménysor a fej-indexbe írt
  a farok helyett — a 0.1.0 óta az egér eseményei nem jutottak el a UI-hoz;
  továbbá a PS/2 inicializálás most fogyasztja a 0xFA nyugtákat és üríti a
  puffert, így nem csúszik el a csomag-értelmező
- Felbontás-független appok (a VBE által adott bármely módhoz igazodnak)
- F-gyorsbillentyűk minden nézetben; új billentyű-térkép (F1–F12, Del, Home, End)

## 0.2.1 (belső fejlesztői kiadás)
- élő frissítés kexec-kel (update_apply(1)), visszaállítás
- böngésző: élő MJPEG-szerű videó demó, HTML elemzés, linkek
- ikontémák, háttérképek (JPEG/BMP), kiemelőszín
- UHCI + EHCI USB Storage, FAT32 írás, ATA HDD, partíció-szkenner
- e1000/RTL8139 + DHCP/TCP/UDP/DNS/HTTP, kexec

## 0.1.0 — Első kiadás
- Saját i386 kernel: GDT/IDT/PIC/PIT, 4 KB-os lapozás, preemptív többfeladat
- VBE lineáris framebuffer, dupla pufferelés, saját 3D vetítő és affin textúrázás
- „Keringő asztal": pályákon keringő panelek, használati gravitáció
- ATAPI CD-olvasás + ISO9660, RAM-fájlrendszer, PS/2 billentyűzet és egér
- HU/EN kétnyelvű felület (F1)
