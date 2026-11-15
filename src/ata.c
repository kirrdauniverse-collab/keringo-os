/* ata.c — IDE/ATA: ATAPI CD olvasás (soft reset + CDB-lenyelés + null-ellenőrzéses
 * retry) és ATA merevlemez PIO olvasás/írás. Mind a 4 pozíciót szkenneli. */
#include "kernel.h"

/* regiszterek csatornánként */
#define R_DATA(ch)    ((ch) + 0)
#define R_ERROR(ch)   ((ch) + 1)
#define R_FEATURE(ch) ((ch) + 1)
#define R_SECCNT(ch)  ((ch) + 2)
#define R_LBA_LO(ch)  ((ch) + 3)
#define R_LBA_MID(ch) ((ch) + 4)
#define R_LBA_HI(ch)  ((ch) + 5)
#define R_DRIVE(ch)   ((ch) + 6)
#define R_STATUS(ch)  ((ch) + 7)
#define R_CMD(ch)     ((ch) + 7)
#define R_CONTROL(ch) ((ch) + 0x206)

#define CH_PRI 0x1F0
#define CH_SEC 0x170

#define ST_BSY  0x80
#define ST_DRQ  0x08
#define ST_ERR  0x01
#define ST_DF   0x20

/* ---------- CD (ATAPI) állapot ---------- */
static int ata_ready = 0;
static u16 cd_io = CH_PRI;
static u8  cd_slave = 0;

/* ---------- merevlemezek ---------- */
#define MAX_DISKS 2
typedef struct { u16 io; u8 slave; u32 nsecs; int ok; } atadisk_t;
static atadisk_t disks[MAX_DISKS];
static int ndisks = 0;

/* ================= ATAPI CD ================= */
static int wait_not_bsy(u16 io, u32 timeout) {
    for (u32 i = 0; i < timeout; i++) {
        if (!(inb(R_STATUS(io)) & ST_BSY)) return 1;
        for (volatile int j = 0; j < 200; j++) { }
    }
    return 0;
}

static int wait_drq(u16 io, u32 timeout) {
    for (u32 i = 0; i < timeout; i++) {
        u8 st = inb(R_STATUS(io));
        if (!(st & ST_BSY) && (st & ST_DRQ)) return 1;
        if (st & ST_ERR) return -1;
        for (volatile int j = 0; j < 200; j++) { }
    }
    return 0;
}

static void ata_select(u16 io, u8 slave) {
    outb(R_DRIVE(io), (u8)(0xA0 | (slave ? 0x10 : 0)));
    for (volatile int i = 0; i < 400; i++) { }
}

static void ata_soft_reset(u16 io) {
    outb(R_CONTROL(io), 0x04);
    for (volatile int i = 0; i < 2000; i++) { }
    outb(R_CONTROL(io), 0x00);
    wait_not_bsy(io, 400000);
    for (volatile int i = 0; i < 2000; i++) { }
}

/* egy pozíció osztályozása: 0=nincs, 1=ATAPI, 2=ATA lemez */
static int probe_pos(u16 io, u8 slave, u16 *id) {
    ata_select(io, slave);
    if (!wait_not_bsy(io, 100000)) return 0;
    u8 st = inb(R_STATUS(io));
    if (st == 0 || st == 0xFF) return 0;
    outb(R_SECCNT(io), 0);
    outb(R_LBA_LO(io), 0);
    outb(R_LBA_MID(io), 0);
    outb(R_LBA_HI(io), 0);
    outb(R_CMD(io), 0xEC);                        /* IDENTIFY */
    st = inb(R_STATUS(io));
    if (st == 0) return 0;
    wait_not_bsy(io, 200000);
    st = inb(R_STATUS(io));
    if (st & ST_ERR) {
        /* ATA vs ATAPI megkülönböztetés: cyclow/high regiszterek */
        u8 lo = inb(R_LBA_MID(io));
        u8 hi = inb(R_LBA_HI(io));
        if (lo == 0x14 && hi == 0xEB) {
            outb(R_CMD(io), 0xA1);                /* IDENTIFY PACKET */
            if (!wait_not_bsy(io, 200000)) return 0;
            if (inb(R_STATUS(io)) & ST_DRQ) {
                for (int i = 0; i < 256; i++) id[i] = inw(R_DATA(io));
                return 1;                         /* ATAPI */
            }
            return 0;
        }
        return 0;
    }
    if (inb(R_STATUS(io)) & ST_DRQ) {
        for (int i = 0; i < 256; i++) id[i] = inw(R_DATA(io));
        return 2;                                 /* ATA lemez */
    }
    return 0;
}

int ata_init(void) {
    ata_ready = 0;
    static u16 id[256];
    /* elsõsorban a boot-CD az elsõdleges master (a 0.1-gyel kompatibilis) */
    struct { u16 io; u8 slave; } order[4] = {
        { CH_PRI, 0 }, { CH_PRI, 1 }, { CH_SEC, 0 }, { CH_SEC, 1 }
    };
    for (int i = 0; i < 4; i++) {
        int kind = probe_pos(order[i].io, order[i].slave, id);
        if (kind == 1) {
            cd_io = order[i].io;
            cd_slave = order[i].slave;
            ata_ready = 1;
            kprintf("ata: ATAPI CD a %x %s pozicion\n", cd_io, cd_slave ? "slave" : "master");
            return 1;
        }
    }
    kprintf("ata: nincs CD-meghajto\n");
    return 0;
}

u32 ata_sector_size(void) { return 2048; }
int ata_present(void) { return ata_ready; }

static int ata_read_one(u32 lba, void *buf) {
    u8 *out = (u8 *)buf;
    u32 total = 2048;
    u32 done = 0;

    ata_soft_reset(cd_io);
    ata_select(cd_io, cd_slave);
    if (!wait_not_bsy(cd_io, 400000)) return 0;

    outb(R_FEATURE(cd_io), 0);
    outb(R_SECCNT(cd_io), 0);
    outb(R_LBA_MID(cd_io), (u8)(total & 0xFF));
    outb(R_LBA_HI(cd_io),  (u8)((total >> 8) & 0xFF));
    outb(R_CMD(cd_io), 0xA0);

    if (!wait_drq(cd_io, 500000)) return 0;
    u8 pkt[12] = { 0x28, 0,
                   (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                   0, 1, 0, 0, 0, 0 };
    for (int i = 0; i < 6; i++) outw(R_DATA(cd_io), ((u16 *)pkt)[i]);

    for (u32 i = 0; i < 2000000; i++) {
        if (!(inb(R_STATUS(cd_io)) & ST_DRQ)) break;
        for (volatile int j = 0; j < 50; j++) { }
    }
    while (done < total) {
        if (!wait_drq(cd_io, 4000000)) break;
        for (u32 i = 0; i < 2048 / 2 && done < total; i++) {
            u16 w = inw(R_DATA(cd_io));
            out[done++] = w & 0xFF;
            out[done++] = (w >> 8) & 0xFF;
        }
    }
    wait_not_bsy(cd_io, 400000);
    return (int)done;
}

int ata_read_sectors(u32 lba, u32 count, void *buf) {
    if (!ata_ready) return 0;
    u8 *out = (u8 *)buf;
    u32 done = 0;
    cli();
    for (u32 s = 0; s < count; s++) {
        u32 sector_done = 0;
        for (int attempt = 0; attempt < 3 && sector_done < 2048; attempt++) {
            sector_done = (u32)ata_read_one(lba + s, out + s * 2048);
            if (sector_done == 2048) {
                int allz = 1;
                for (u32 k = 0; k < 2048; k += 64) {
                    if (out[s * 2048 + k] != 0) { allz = 0; break; }
                }
                if (!allz) break;
            }
        }
        done += sector_done;
    }
    sti();
    return (int)done;
}

/* ================= ATA merevlemez ================= */
void ata_disk_scan(void) {
    static u16 id[256];
    ndisks = 0;
    struct { u16 io; u8 slave; } order[4] = {
        { CH_PRI, 0 }, { CH_PRI, 1 }, { CH_SEC, 0 }, { CH_SEC, 1 }
    };
    for (int i = 0; i < 4 && ndisks < MAX_DISKS; i++) {
        /* a CD pozícióját nem próbáljuk újra: a select regiszter írása
           DRQ alatt érvénytelen, és a CD FIFO-ja olvasatlan adatot tartalmazhat */
        if (ata_ready && order[i].io == cd_io && order[i].slave == cd_slave) continue;
        /* csatorna soft-reset: a SELECT-írás érvénytelen, ha DRQ van a buszon */
        ata_soft_reset(order[i].io);
        int kind = probe_pos(order[i].io, order[i].slave, id);
        if (kind == 2) {
            u32 n = (u32)id[60] | ((u32)id[61] << 16);
            if (id[0] == 0 || id[0] == 0xFFFF || n < 16384) continue;
            if (((id[0] >> 8) & 7) == 5) continue;    /* ATAPI konfigurációs bitminta */
            disks[ndisks].io = order[i].io;
            disks[ndisks].slave = order[i].slave;
            disks[ndisks].nsecs = n;
            disks[ndisks].ok = 1;
            kprintf("ata: merevlemez #%d a %x %s: %u szektor (%u MB)\n",
                    ndisks, order[i].io, order[i].slave ? "slave" : "master",
                    n, n / 2 / 1024);
            ndisks++;
        }
    }
    if (!ndisks) kprintf("ata: nincs merevlemez\n");
}

int ata_disk_count(void) { return ndisks; }

static int disk_wait_ready(u16 io) {
    for (u32 i = 0; i < 200000; i++) {
        u8 st = inb(R_STATUS(io));
        if (st & ST_BSY) continue;
        if (st & (ST_ERR | ST_DF)) return 0;
        return 1;
    }
    return 0;
}

int ata_disk_read_secs(int disk, u32 lba, u32 n, void *buf) {
    if (disk < 0 || disk >= ndisks || !disks[disk].ok || n == 0) return 0;
    u16 io = disks[disk].io;
    u8 *out = (u8 *)buf;
    u32 done = 0;
    while (done < n) {
        u32 chunk = n - done;
        if (chunk > 64) chunk = 64;               /* 32 KB lépés */
        ata_soft_reset(io);                       /* a csatorna tiszta állapotba (SELECT garantált) */
        cli();
        ata_select(io, disks[disk].slave);
        u32 l = lba + done;
        outb(R_DRIVE(io), (u8)(0xE0 | (disks[disk].slave ? 0x10 : 0) | ((l >> 24) & 0x0F)));
        outb(R_SECCNT(io), (u8)chunk);
        outb(R_LBA_LO(io), (u8)l);
        outb(R_LBA_MID(io), (u8)(l >> 8));
        outb(R_LBA_HI(io), (u8)(l >> 16));
        outb(R_CMD(io), 0x20);                    /* READ SECTORS */
        for (u32 s = 0; s < chunk; s++) {
            if (!wait_drq(io, 4000000)) { sti(); return 0; }
            for (u32 i = 0; i < 256; i++) {
                ((u16 *)(out + (done + s) * 512))[i] = inw(R_DATA(io));
            }
        }
        sti();
        if (!disk_wait_ready(io)) return 0;
        done += chunk;
    }
    return 1;
}

int ata_disk_write_secs(int disk, u32 lba, u32 n, const void *buf) {
    if (disk < 0 || disk >= ndisks || !disks[disk].ok || n == 0) return 0;
    u16 io = disks[disk].io;
    const u8 *src = (const u8 *)buf;
    u32 done = 0;
    while (done < n) {
        u32 chunk = n - done;
        if (chunk > 64) chunk = 64;
        ata_soft_reset(io);                       /* a csatorna tiszta állapotba */
        cli();
        ata_select(io, disks[disk].slave);
        u32 l = lba + done;
        outb(R_DRIVE(io), (u8)(0xE0 | (disks[disk].slave ? 0x10 : 0) | ((l >> 24) & 0x0F)));
        outb(R_SECCNT(io), (u8)chunk);
        outb(R_LBA_LO(io), (u8)l);
        outb(R_LBA_MID(io), (u8)(l >> 8));
        outb(R_LBA_HI(io), (u8)(l >> 16));
        outb(R_CMD(io), 0x30);                    /* WRITE SECTORS */
        for (u32 s = 0; s < chunk; s++) {
            if (!wait_drq(io, 4000000)) { sti(); kprintf("ata: write DRQ hiba lba=%u\n", l + s); return 0; }
            for (u32 i = 0; i < 256; i++) {
                outw(R_DATA(io), ((const u16 *)(src + (done + s) * 512))[i]);
            }
        }
        sti();
        if (!disk_wait_ready(io)) return 0;
        done += chunk;
    }
    return 1;
}

int ata_disk_flush(int disk) {
    if (disk < 0 || disk >= ndisks || !disks[disk].ok) return 0;
    u16 io = disks[disk].io;
    cli();
    ata_select(io, disks[disk].slave);
    outb(R_CMD(io), 0xE7);                        /* FLUSH CACHE */
    wait_not_bsy(io, 4000000);
    sti();
    return 1;
}
