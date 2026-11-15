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
