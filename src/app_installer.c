/* app_installer.c — Telepítő (0.5): a Keringő OS-t MÁS OPRENDSZEREK MELLÉ
 * telepíti: egy FAT32-partícióra (formázva vagy meglévőre) kimásolja a kernelt
 * és az adatfájlokat, jelölőt ír, és GRUB-utasítást generál a Linux oldalra.
 * A GRUB (pl. Linux Lite) indítja majd a keringo.elf-et multiboot-tal. */
#include "kernel.h"

typedef struct { u8 boot, type; u32 lba, cnt; } ipart_t;
static ipart_t iparts[4];
static int ndisk = 0, disk_idx = 0;
static u32 disk_secs = 0;
static int sel = 0;
static int phase = 0;          /* 0: választás, 1: kész összegzés */
static int confirm = 0;        /* 0 nincs, 1: formáz+telepít, 2: telepít (formázás nélkül) */
static char msg[128] = "";
static char summary[8][72];
static int nsummary = 0;

static int iwr(u32 lba, u32 n, const void *buf, void *ctx) {
    (void)ctx;
    return ata_disk_write_secs(disk_idx, lba, n, buf);
}
static int ird(u32 lba, u32 n, void *buf, void *ctx) {
    (void)ctx;
    return ata_disk_read_secs(disk_idx, lba, n, buf);
}

static void rescan(void) {
    static u8 sec[512];
    ndisk = ata_disk_count();
    if (disk_idx >= ndisk) disk_idx = 0;
    disk_secs = 0;
    for (int e = 0; e < 4; e++) iparts[e].boot = iparts[e].type = 0, iparts[e].lba = iparts[e].cnt = 0;
    if (ndisk > 0 && ata_disk_read_secs(disk_idx, 0, 1, sec)) {
        for (int e = 0; e < 4; e++) {
            u8 *en = sec + 446 + e * 16;
            iparts[e].boot = en[0] & 0x80;
            iparts[e].type = en[4];
            iparts[e].lba = en[8] | ((u32)en[9] << 8) | ((u32)en[10] << 16) | ((u32)en[11] << 24);
            iparts[e].cnt = en[12] | ((u32)en[13] << 8) | ((u32)en[14] << 16) | ((u32)en[15] << 24);
            if (iparts[e].lba + iparts[e].cnt > disk_secs) disk_secs = iparts[e].lba + iparts[e].cnt;
        }
    }
}

/* fájl másolása /cd-ről /sys-re */
static int copy_cd_to_sys(const char *cdname, const char *sysname, char *msgout, int msgsz) {
    static u8 fbuf[4 * 1024 * 1024];
    int out = 0;
    char cdpath[96];
    ksnprintf(cdpath, sizeof(cdpath), "/cd/%s", cdname);
    if (!vfs_read_file(cdpath, (char *)fbuf, (int)sizeof(fbuf), &out) || out <= 0) {
        ksnprintf(msgout, msgsz, "nem talalhato: %s", cdname);
        return 0;
    }
    char syspath[96];
    ksnprintf(syspath, sizeof(syspath), "/sys/%s", sysname);
    if (!vfs_write_file(syspath, (const char *)fbuf, out)) {
        ksnprintf(msgout, msgsz, "iras sikertelen: %s", sysname);
        return 0;
    }
    ksnprintf(msgout, msgsz, "OK: %s (%d KB)", sysname, out / 1024);
    return 1;
}

/* a teljes telepítés */
static void do_install(int format_too) {
    nsummary = 0;
    ipart_t *p = &iparts[sel];
    if (!p->type || !p->cnt) { ksnprintf(msg, sizeof(msg), "Elobb valassz particiot!"); return; }

    if (format_too) {
        blkdev_t bd;
        for (u8 *q = (u8 *)&bd; q < (u8 *)&bd + sizeof(bd); q++) *q = 0;
        bd.start_lba = p->lba; bd.nsecs = p->cnt; bd.secsize = 512;
        bd.read_secs = ird; bd.write_secs = iwr;
        if (!fat32_format(&bd, "KERINGO")) {
            ksnprintf(msg, sizeof(msg), "A formazas nem sikerult (tul kicsi?)");
            return;
        }
        ksnprintf(summary[nsummary], sizeof(summary[0]), "formázás: FAT32 OK (%u MB)", p->cnt / 2048);
        nsummary++;
    }
    /* kézi /sys-csatolás a partícióra */
    if (!vfs_mount_sys_manual(0, disk_idx, p->lba, p->cnt)) {
        ksnprintf(msg, sizeof(msg), "Nem sikerult felcsatolni (nem FAT32?)");
        return;
    }
    ksnprintf(summary[nsummary], sizeof(summary[0]), "partíció csatolva: /sys (lba=%u)", p->lba);
    nsummary++;

    /* jelölő + kernelt másoljuk */
    static const char marker[32] = "KERINGO OS 0.5.0 TELEPITVE";
    vfs_write_file("/sys/KERINGO.ID", marker, 25);
    char m[96];
    if (copy_cd_to_sys("boot/keringo.elf", "KERINGO.ELF", m, sizeof(m))) {
        ksnprintf(summary[nsummary], sizeof(summary[0]), "kernel: %s", m);
    } else {
        ksnprintf(summary[nsummary], sizeof(summary[0]), "kernel HIBA: %s", m);
    }
    nsummary++;

    /* adatfájlok */
    const char *dat[] = { "0UDVOZLET.TXT", "KERINGO.CFG", "adatok/HATTER1.JPG",
                          "adatok/HATTER2.JPG", "adatok/HATTER3.JPG", "adatok/KEP1.JPG" };
    const char *dst[] = { "0UDVOZLET.TXT", "KERINGO.CFG", "HATTER1.JPG",
                          "HATTER2.JPG", "HATTER3.JPG", "KEP1.JPG" };
    for (int i = 0; i < 6; i++) {
        char m2[96];
        copy_cd_to_sys(dat[i], dst[i], m2, sizeof(m2));
        if (nsummary < 7) { ksnprintf(summary[nsummary], sizeof(summary[0]), "%s", m2); nsummary++; }
    }

    /* GRUB-utasítás a Linux oldalra */
    static const char *grub =
        "KERINGO OS + GRUB (Linux Lite / Ubuntu oldalon)\n"
        "===============================================\n"
        "1. Nyiss terminalt, es:\n"
        "   sudo mkdir -p /boot/keringo\n"
        "2. Masold be a kernelt (FAT32 particiorol):\n"
        "   sudo mount /dev/sdaN /mnt   (a KERINGO particio)\n"
        "   sudo cp /mnt/KERINGO.ELF /boot/keringo/keringo.elf\n"
        "3. Szerkeszd: sudo nano /etc/grub.d/40_custom\n"
        "   Add hozza:\n"
        "\n"
        "   menuentry \"Keringo OS 0.5.0\" {\n"
        "       insmod vbe\n"
        "       set gfxpayload=640x480x32\n"
        "       multiboot /boot/keringo/keringo.elf\n"
        "   }\n"
        "\n"
        "4. sudo update-grub   (ha nem jelenik meg: GRUB_DISABLE_OS_PROBER=false\n"
        "    az /etc/default/grub-ban, majd megupdate)\n"
        "5. Ujrainditas: valassz 'Keringo OS'-t a GRUB menuben.\n"
        "\n"
        "A 640x480x32 biztonsagos mod regi monitorokra;\n"
        "1024x768x32-re csereld, ha a monitor birja.\n";
    vfs_write_file("/sys/GRUB_UTASITAS.TXT", grub, 700);

    /* Lemez panel frissítése */
    vfs_mount_all();
    ui_fs_announce();
    ksnprintf(msg, sizeof(msg), "TELEPITES KESZ! Olvasd el a GRUB_UTASITAS.TXT-t.");
    phase = 1;
    sound_tune((const u16[]){ 523, 6, 659, 6, 784, 6, 1047, 12 }, 4);
}

void installer_enter(void) {
    rescan();
    phase = 0; confirm = 0;
    ksnprintf(msg, sizeof(msg), "Valassz particiot, majd T = telepites, G = formaz+telepites");
    sound_beep(880, 4);
}
void installer_exit(void) { confirm = 0; }

void installer_key(u8 sc, u8 rel, u8 ascii) {
    if (rel) return;
    if (phase == 1) { if (sc == KEY_ESC || sc == KEY_ENTER) phase = 0; return; }
    if (confirm) {
        if (ascii == 'y' || ascii == 'Y') { int c = confirm; confirm = 0; do_install(c == 1); }
        else { confirm = 0; ksnprintf(msg, sizeof(msg), "Megse."); }
        return;
    }
    switch (sc) {
        case KEY_UP:   if (sel > 0) sel--; break;
        case KEY_DOWN: if (sel < 3) sel++; break;
        case KEY_TAB:
            disk_idx = (disk_idx + 1) % (ndisk ? ndisk : 1);
            rescan();
            break;
    }
    if (ascii == 't' || ascii == 'T') { confirm = 2; ksnprintf(msg, sizeof(msg), "Telepites a kijeloltre? Y = igen"); }
    if (ascii == 'g' || ascii == 'G') { confirm = 1; ksnprintf(msg, sizeof(msg), "FORMAZAS+FAT32 es telepites? Y = igen"); }
    if (ascii == 'r' || ascii == 'R') { rescan(); ksnprintf(msg, sizeof(msg), "Ujraolvasva."); }
}

static const char *ptype(u8 t) {
    switch (t) {
        case 0x0B: case 0x0C: return "FAT32  <- ajánlott";
        case 0x06: case 0x0E: return "FAT16";
        case 0x07: return "NTFS";
        case 0x83: return "Linux";
        case 0x82: return "swap";
        default: return "?";
    }
}

void installer_frame(u32 t) {
    (void)t;
    u32 accent = settings_accent_color();
    int W = (int)gfx_width(), H = (int)gfx_height();
    gfx_fill_rect(0, 22, W, H - 44, 0xFF10151A);
    gfx_draw_text(12, 34, "TELEPÍTŐ — KERINGŐ OS MÁS OPRENDSZEREK MELLÉ", accent, 0xFF000000u);
    char b[96];
    if (ndisk > 0)
        ksnprintf(b, sizeof(b), "HDD-%d  %u MB  (Tab = kovetkezo lemez)", disk_idx, disk_secs / 2048);
    else
        ksnprintf(b, sizeof(b), "Nincs ATA merevlemez!");
    gfx_draw_text(12, 52, b, 0xFFF0F0F0, 0xFF000000u);

    gfx_draw_text(12, 74, "   TIPUS             MERET", 0xFF8FA0AC, 0xFF000000u);
    for (int e = 0; e < 4; e++) {
        int y = 92 + e * 22;
        if (e == sel) gfx_fill_rect(8, y - 2, W - 24, 20, 0xFF1C2A38);
        if (iparts[e].type) {
            char ln[80];
            ksnprintf(ln, sizeof(ln), "%d  0x%02X %-16s %6u MB", e + 1, iparts[e].type,
                       ptype(iparts[e].type), iparts[e].cnt / 2048);
            gfx_draw_text(12, y, ln, e == sel ? 0xFFE8B24A : 0xFFF0F0F0, 0xFF000000u);
        } else {
            gfx_draw_text(12, y, "—  (szabad hely)", 0xFF5A6A76, 0xFF000000u);
        }
    }

    if (phase == 0) {
        gfx_draw_text(12, 196, ui_lang() ? "T: install here  G: format FAT32+install  R: rescan" :
                                           "T: telepites ide  G: FAT32-re es telepites  R: ujraolvas", 0xFF8FA0AC, 0xFF000000u);
        gfx_draw_text(12, 212, msg, confirm ? 0xFFE85050 : 0xFF50E878, 0xFF000000u);
        gfx_draw_text(12, 232, "A telepites NEM irtja felul a tobbi rendszert:", 0xFF8FA0AC, 0xFF000000u);
        gfx_draw_text(12, 248, "a GRUB-inditast a Linux reszrol allitod be (GRUB_UTASITAS.TXT).", 0xFF8FA0AC, 0xFF000000u);
    } else {
        gfx_fill_rect(8, 190, W - 24, H - 190 - 44, 0xFF0A1018);
        gfx_rect(8, 190, W - 24, H - 190 - 44, accent);
        gfx_draw_text(16, 200, "TELEPITES EREDMENYE:", 0xFF50E878, 0xFF000000u);
        for (int i = 0; i < nsummary; i++)
            gfx_draw_text(16, 218 + i * 16, summary[i], 0xFFF0F0F0, 0xFF000000u);
        gfx_draw_text(16, 218 + nsummary * 16 + 8, msg, 0xFFE8B24A, 0xFF000000u);
        gfx_draw_text(16, 218 + nsummary * 16 + 26, "Esc = vissza   (GRUB_utasis.txt a /sys-en)", 0xFF8FA0AC, 0xFF000000u);
    }
    ksnprintf(b, sizeof(b), "%s", "Keringo OS 0.5.0 — Telepito");
    app_chrome(ui_lang() ? "Installer" : "Telepítő", b, accent);
}
