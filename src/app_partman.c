/* app_partman.c — Partíciókezelő (0.5): MBR-tábla listázása, FAT32-partíció
 * létrehozása, törlés, aktív jelző, FAT32-formázás. Óvatosan: minden írás
 * előtt Y-t kell nyomni! */
#include "kernel.h"

typedef struct {
    int  usb;          /* 0: ATA merevlemez (index), 1: USB pendrájv */
    int  idx;          /* eszközindex */
    u32  nsecs;        /* eszköz mérete szektorban */
    char name[24];
} pm_disk_t;

static pm_disk_t disks[4];
static int ndisks = 0;
static int sel_disk = 0;

/* MBR-partíció-bejegyzések a kiválasztott lemezen */
typedef struct { u8 boot, type; u32 lba, cnt; } pm_part_t;
static pm_part_t parts[4];
static int nparts = 0;
static int sel = 0;
static int confirm = 0;          /* 0: nincs, 1: törlés vár, 2: formázás vár, 3: létrehozás vár */
static int szmode = 0;           /* méretbevitel */
static char szbuf[8];
static int szlen = 0;
static char status[96] = "";

static const char *pm_type_name(u8 t) {
    switch (t) {
        case 0x00: return "szabad";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 <32M";
        case 0x05: return "kiterjesztett";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: return "FAT32";
        case 0x0C: return "FAT32 LBA";
        case 0x0E: return "FAT16 LBA";
        case 0x0F: return "kiterj. LBA";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux";
        case 0x8E: return "Linux LVM";
        case 0xA5: return "FreeBSD";
        case 0xEE: return "GPT vedo";
        case 0xEF: return "EFI rendszer";
        default: return "ismeretlen";
    }
}

static int rd_sec(int usb, int idx, u32 lba, u8 *buf) {
    if (usb) return usb_read_secs(lba, 1, buf);
    return ata_disk_read_secs(idx, lba, 1, buf);
}
static int wr_sec(int usb, int idx, u32 lba, const u8 *buf) {
    if (usb) return usb_write_secs(lba, 1, buf);
    return ata_disk_write_secs(idx, lba, 1, buf);
}

static void pm_rescan(void) {
    static u8 sec[512];
    ndisks = 0;
    for (int d = 0; d < ata_disk_count() && ndisks < 4; d++) {
        disks[ndisks].usb = 0; disks[ndisks].idx = d;
        /* méret: MBR leolvasása; a teljes méretet a 0x1F8? — a QEMU teszthez a
           partíciós táblából becsüljük: max(lba+cnt) */
        disks[ndisks].nsecs = 0;
        if (rd_sec(0, d, 0, sec)) {
            for (int e = 0; e < 4; e++) {
                u8 *en = sec + 446 + e * 16;
                if (en[4] == 0) continue;
                u32 lba = en[8] | ((u32)en[9] << 8) | ((u32)en[10] << 16) | ((u32)en[11] << 24);
                u32 cnt = en[12] | ((u32)en[13] << 8) | ((u32)en[14] << 16) | ((u32)en[15] << 24);
                if (lba + cnt > disks[ndisks].nsecs) disks[ndisks].nsecs = lba + cnt;
            }
        }
        ksnprintf(disks[ndisks].name, sizeof(disks[ndisks].name), "HDD-%d (ATA)", d);
        ndisks++;
    }
    if (usb_ready() && ndisks < 4) {
        disks[ndisks].usb = 1; disks[ndisks].idx = 0;
        disks[ndisks].nsecs = msc_sector_count();
        ksnprintf(disks[ndisks].name, sizeof(disks[ndisks].name), "PENDRAJV (USB)");
        ndisks++;
    }
    if (sel_disk >= ndisks) sel_disk = 0;
    nparts = 0; sel = 0;
    if (!ndisks) { ksnprintf(status, sizeof(status), "Nincs egy lemez sem!"); return; }
    if (!rd_sec(disks[sel_disk].usb, disks[sel_disk].idx, 0, sec)) {
        ksnprintf(status, sizeof(status), "Olvasasi hiba!");
        return;
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        ksnprintf(status, sizeof(status), "Nincs MBR alairas a lemezen (GPT?)");
    }
    for (int e = 0; e < 4; e++) {
        u8 *en = sec + 446 + e * 16;
        parts[e].boot = en[0] & 0x80;
        parts[e].type = en[4];
        parts[e].lba = en[8] | ((u32)en[9] << 8) | ((u32)en[10] << 16) | ((u32)en[11] << 24);
        parts[e].cnt = en[12] | ((u32)en[13] << 8) | ((u32)en[14] << 16) | ((u32)en[15] << 24);
        if (parts[e].type) nparts++;
    }
}

static void apply_mbr(void) {
    static u8 sec[512];
    if (!rd_sec(disks[sel_disk].usb, disks[sel_disk].idx, 0, sec)) return;
    for (int e = 0; e < 4; e++) {
        u8 *en = sec + 446 + e * 16;
        en[0] = parts[e].boot ? 0x80 : 0x00;
        en[4] = parts[e].type;
        en[8] = (u8)parts[e].lba; en[9] = (u8)(parts[e].lba >> 8);
        en[10] = (u8)(parts[e].lba >> 16); en[11] = (u8)(parts[e].lba >> 24);
        en[12] = (u8)parts[e].cnt; en[13] = (u8)(parts[e].cnt >> 8);
        en[14] = (u8)(parts[e].cnt >> 16); en[15] = (u8)(parts[e].cnt >> 24);
    }
    if (wr_sec(disks[sel_disk].usb, disks[sel_disk].idx, 0, sec))
        ksnprintf(status, sizeof(status), "MBR kiirva.");
}

/* blokkeszköz-átirányítás: ctx = (usb<<4)|idx */
static int pm_rd(u32 lba, u32 n, void *buf, void *ctx) {
    int c = (int)(uptr)ctx;
    for (u32 i = 0; i < n; i++)
        if (!rd_sec((c >> 4) & 1, c & 15, lba + i, (u8 *)buf + i * 512)) return 0;
    return 1;
}
static int pm_wr(u32 lba, u32 n, const void *buf, void *ctx) {
    int c = (int)(uptr)ctx;
    for (u32 i = 0; i < n; i++)
        if (!wr_sec((c >> 4) & 1, c & 15, lba + i, (const u8 *)buf + i * 512)) return 0;
    return 1;
}

/* FAT32 formázás a kiválasztott partícióra */
static void format_selected(void) {
    pm_part_t *p = &parts[sel];
    if (!p->type || !p->cnt) { ksnprintf(status, sizeof(status), "Nincs kivalasztott particio!"); return; }
    blkdev_t bd;
    for (u8 *q = (u8 *)&bd; q < (u8 *)&bd + sizeof(bd); q++) *q = 0;
    bd.usb = disks[sel_disk].usb;
    bd.start_lba = p->lba;
    bd.nsecs = p->cnt;
    bd.secsize = 512;
    bd.read_secs = pm_rd; bd.write_secs = pm_wr;
    bd.ctx = (void *)(uptr)((disks[sel_disk].usb << 4) | disks[sel_disk].idx);
    if (fat32_format(&bd, "KERINGO")) {
        ksnprintf(status, sizeof(status), "FAT32 kesz! Ujracsatolas...");
        vfs_mount_all();
        ui_fs_announce();
    } else
        ksnprintf(status, sizeof(status), "A formazas nem sikerult (kicsi?)");
}

void partman_enter(void) {
    pm_rescan();
    sound_beep(880, 4);
}
void partman_exit(void) { confirm = 0; szmode = 0; }

void partman_key(u8 sc, u8 rel, u8 ascii) {
    if (rel) return;
    /* méretbevitel (új partíció) */
    if (szmode) {
        if (sc == KEY_ESC) { szmode = 0; szlen = 0; return; }
        if (sc == KEY_ENTER) {
            szbuf[szlen] = 0;
            int mb = 0;
            for (int i = 0; szbuf[i]; i++) if (szbuf[i] >= '0' && szbuf[i] <= '9') mb = mb * 10 + (szbuf[i] - '0');
            if (mb < 64) mb = 64;                     /* FAT32-hoz min. ~64 MB */
            if (mb > 8192) mb = 8192;
            u32 secs = (u32)mb * 2048;
            /* üres hely a lemezen? */
            u32 maxend = 2048;                          /* 1 MB igazítás */
            for (int e = 0; e < 4; e++) if (parts[e].type && parts[e].lba + parts[e].cnt > maxend) maxend = parts[e].lba + parts[e].cnt;
            u32 align = (maxend + 2047) & ~2047u;
            if (align + secs > disks[sel_disk].nsecs) secs = disks[sel_disk].nsecs - align;
            if (secs < 66000) { ksnprintf(status, sizeof(status), "Nincs eleg hely (min 64 MB kell)"); szmode = 0; return; }
            int slot = -1;
            for (int e = 0; e < 4; e++) if (!parts[e].type) { slot = e; break; }
            if (slot < 0) { ksnprintf(status, sizeof(status), "Az MBR megtelt (4 particio)"); szmode = 0; return; }
            parts[slot].boot = 0;
            parts[slot].type = 0x0C;                    /* FAT32 LBA */
            parts[slot].lba = align;
            parts[slot].cnt = secs;
            nparts++;
            sel = slot;
            apply_mbr();
            ksnprintf(status, sizeof(status), "Particio letrehozva: %d MB", secs / 2048);
            szmode = 0; szlen = 0;
            return;
        }
        if (ascii == '\b') { if (szlen > 0) szbuf[--szlen] = 0; return; }
        if (ascii >= '0' && ascii <= '9' && szlen < 6) { szbuf[szlen++] = ascii; szbuf[szlen] = 0; }
        return;
    }
    /* megerősítés */
    if (confirm) {
        if (ascii == 'y' || ascii == 'Y') {
            if (confirm == 1) {                     /* törlés */
                if (parts[sel].type) {
                    parts[sel].type = 0; parts[sel].lba = 0; parts[sel].cnt = 0; parts[sel].boot = 0;
                    nparts--;
                    apply_mbr();
                    ksnprintf(status, sizeof(status), "Particio torolve.");
                }
            } else if (confirm == 2) {              /* formázás */
                format_selected();
            }
            confirm = 0;
        } else {
            confirm = 0;
            ksnprintf(status, sizeof(status), "Megse.");
        }
        return;
    }
    switch (sc) {
        case KEY_UP:   if (sel > 0) sel--; break;
        case KEY_DOWN: if (sel < 3) sel++; break;
        case KEY_TAB:  sel_disk = (sel_disk + 1) % (ndisks ? ndisks : 1); pm_rescan(); break;
    }
    if (ascii == 'd' || ascii == 'D') { if (parts[sel].type) { confirm = 1; ksnprintf(status, sizeof(status), "TORLES? Y = igen, egyeb = megse"); } }
    if (ascii == 'a' || ascii == 'A') {
        if (parts[sel].type) { parts[sel].boot ^= 0x80; apply_mbr(); ksnprintf(status, sizeof(status), "Aktiv jelzo atkapcsolva."); }
    }
    if (ascii == 'f' || ascii == 'F') { if (parts[sel].type) { confirm = 2; ksnprintf(status, sizeof(status), "FORMAZAS (FAT32)? Y = igen, egyeb = megse"); } }
    if (ascii == 'n' || ascii == 'N') { szmode = 1; szlen = 0; szbuf[0] = 0; ksnprintf(status, sizeof(status), "Uj particio merete MB-ban (min 64):"); }
    if (ascii == 'r' || ascii == 'R') { pm_rescan(); ksnprintf(status, sizeof(status), "Ujraolvasva."); }
}

void partman_frame(u32 t) {
    (void)t;
    u32 accent = settings_accent_color();
    int W = (int)gfx_width(), H = (int)gfx_height();
    gfx_fill_rect(0, 22, W, H - 44, 0xFF10151A);
    gfx_draw_text(12, 34, "PARTICIOKEZELO — MBR TABLAZAT", accent, 0xFF000000u);
    char b[96];
    if (ndisks > 0)
        ksnprintf(b, sizeof(b), "%s  %u MB  (%d/%d lemez, Tab = kovetkezo)",
                  disks[sel_disk].name, disks[sel_disk].nsecs / 2048, sel_disk + 1, ndisks);
    else
        ksnprintf(b, sizeof(b), "nincs lemez");
    gfx_draw_text(12, 50, b, 0xFFF0F0F0, 0xFF000000u);

    /* partíciós tábla */
    static u8 sec[512];
    u8 *mbr = sec;
    if (ndisks) rd_sec(disks[sel_disk].usb, disks[sel_disk].idx, 0, sec);
    gfx_draw_text(12, 70, "  BOOT  TIPUS    ELEJE (LBA)   MERET", 0xFF8FA0AC, 0xFF000000u);
    for (int e = 0; e < 4; e++) {
        pm_part_t *p = &parts[e];
        int y = 88 + e * 22;
        if (e == sel) gfx_fill_rect(8, y - 2, W - 24, 20, 0xFF1C2A38);
        char ln[80];
        if (p->type) {
            u8 *en = mbr + 446 + e * 16;
            char chs[16];
            ksnprintf(chs, sizeof(chs), "%02X%02X%02X", en[5], en[6], en[7]);
            ksnprintf(ln, sizeof(ln), "%d %s  0x%02X  %-13s %10u  %6u MB   %s",
                       e + 1,
                       p->boot ? "[AKT]" : "     ",
                       p->type, pm_type_name(p->type), p->lba, p->cnt / 2048, chs);
            gfx_draw_text(12, y, ln, e == sel ? 0xFFE8B24A : 0xFFF0F0F0, 0xFF000000u);
        } else {
            ksnprintf(ln, sizeof(ln), "%d  ----  szabad bejegyzes (N = uj particio ide)", e + 1);
            gfx_draw_text(12, y, ln, 0xFF5A6A76, 0xFF000000u);
        }
    }
    /* alsó súgó */
    gfx_draw_text(12, 190, ui_lang() ? "N:new  D:delete  A:active flag  F:format FAT32  R:rescan" :
                                      "N:uj  D:torol  A:aktiv  F:FAT32-re  R:ujraolvas", 0xFF8FA0AC, 0xFF000000u);
    gfx_draw_text(12, 206, status, 0xFF50E878, 0xFF000000u);
    if (szmode) {
        gfx_fill_rect(12, 224, 260, 24, 0xFF0A1018);
        gfx_rect(12, 224, 260, 24, accent);
        gfx_draw_text(20, 232, "MERET (MB): ", 0xFFF0F0F0, 0xFF000000u);
        gfx_draw_text(20 + 12 * 8, 232, szbuf, 0xFFE8B24A, 0xFF000000u);
        if ((t / 25) % 2 == 0) gfx_fill_rect(20 + (12 + szlen) * 8, 232, 8, 8, 0xFFE8B24A);
    }
    ksnprintf(b, sizeof(b), "%s", ui_lang() ? "MBR editor — careful, destructive!" : "MBR-szerkeszto — vigyazz, torlo!");
    app_chrome(ui_lang() ? "Partitions" : "Partíciókezelő", b, accent);
}
