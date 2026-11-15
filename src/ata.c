/* ata.c — ATAPI (CD) olvasás: soft reset + CDB-lenyelés + null-ellenőrzéses retry */
#include "kernel.h"

#define ATA_DATA      0x1F0
#define ATA_ERROR     0x1F1
#define ATA_FEATURE   0x1F1
#define ATA_SECCNT    0x1F2
#define ATA_LBA_LO    0x1F3
#define ATA_LBA_MID   0x1F4
#define ATA_LBA_HI    0x1F5
#define ATA_DRIVE     0x1F6
#define ATA_STATUS    0x1F7
#define ATA_CMD       0x1F7
#define ATA_CONTROL   0x3F6

#define ST_BSY  0x80
#define ST_DRQ  0x08
#define ST_ERR  0x01

static int ata_ready = 0;
static int ata_is_atapi = 0;

static int wait_not_bsy(u32 timeout) {
    for (u32 i = 0; i < timeout; i++) {
        if (!(inb(ATA_STATUS) & ST_BSY)) return 1;
        for (volatile int j = 0; j < 200; j++) { }
    }
    return 0;
}

static int wait_drq(u32 timeout) {
    for (u32 i = 0; i < timeout; i++) {
        u8 st = inb(ATA_STATUS);
        if (!(st & ST_BSY) && (st & ST_DRQ)) return 1;
        if (st & ST_ERR) return -1;
        for (volatile int j = 0; j < 200; j++) { }
    }
    return 0;
}

static void ata_select(void) {
    outb(ATA_DRIVE, 0xA0);          /* master, LBA mód */
    for (volatile int i = 0; i < 400; i++) { }
}

/* soft reset: a csatorna összes belső állapotát törli */
static void ata_soft_reset(void) {
    outb(ATA_CONTROL, 0x04);
    for (volatile int i = 0; i < 2000; i++) { }
    outb(ATA_CONTROL, 0x00);
    wait_not_bsy(400000);
    for (volatile int i = 0; i < 2000; i++) { }
}

int ata_init(void) {
    ata_ready = 0;
    ata_select();
    if (!wait_not_bsy(200000)) { kprintf("ata: nincs valasz\n"); return 0; }

    outb(ATA_SECCNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, 0xEC);
    u8 st = inb(ATA_STATUS);
    if (st == 0) { kprintf("ata: nincs meghajto\n"); return 0; }
    if (!wait_not_bsy(200000)) return 0;
    if (inb(ATA_STATUS) & ST_ERR) {
        outb(ATA_CMD, 0xA1);
        if (!wait_not_bsy(200000)) return 0;
        if (inb(ATA_STATUS) & ST_DRQ) {
            u16 id[256];
            for (int i = 0; i < 256; i++) id[i] = inw(ATA_DATA);
            ata_is_atapi = 1;
            ata_ready = 1;
            kprintf("ata: ATAPI CD meghajto talalva\n");
            return 1;
        }
        kprintf("ata: ATAPI de nem DRQ\n");
        return 0;
    }
    if (inb(ATA_STATUS) & ST_DRQ) {
        u16 id[256];
        for (int i = 0; i < 256; i++) id[i] = inw(ATA_DATA);
        kprintf("ata: ATA HDD (csak ATAPI tamogatott)\n");
        return 0;
    }
    return 0;
}

u32 ata_sector_size(void) { return 2048; }
int ata_present(void) { return ata_ready; }

/* EGY szektor olvasása: teljes packet-ciklus saját soft resettel */
static int ata_read_one(u32 lba, void *buf) {
    u8 *out = (u8 *)buf;
    u32 total = 2048;
    u32 done = 0;

    ata_soft_reset();
    ata_select();
    if (!wait_not_bsy(400000)) return 0;

    outb(ATA_FEATURE, 0);
    outb(ATA_SECCNT, 0);
    outb(ATA_LBA_MID, (u8)(total & 0xFF));
    outb(ATA_LBA_HI,  (u8)((total >> 8) & 0xFF));
    outb(ATA_CMD, 0xA0);

    /* CDB-fázis: DRQ=1 */
    if (!wait_drq(500000)) return 0;
    u8 pkt[12] = { 0x28, 0,
                   (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                   0, 1, 0, 0, 0, 0 };
    for (int i = 0; i < 6; i++) outw(ATA_DATA, ((u16 *)pkt)[i]);

    /* CDB lenyelése: a DRQ lemegy, a meghajtó feldolgoz */
    for (u32 i = 0; i < 2000000; i++) {
        if (!(inb(ATA_STATUS) & ST_DRQ)) break;
        for (volatile int j = 0; j < 50; j++) { }
    }
    /* adatfázis */
    while (done < total) {
        if (!wait_drq(4000000)) break;
        for (u32 i = 0; i < 2048 / 2 && done < total; i++) {
            u16 w = inw(ATA_DATA);
            out[done++] = w & 0xFF;
            out[done++] = (w >> 8) & 0xFF;
        }
    }
    wait_not_bsy(400000);
    return (int)done;
}

/* Több szektor olvasása, nullás-tartalom ellenőrzéssel és újbóli próbával.
   (Az ISO-nk valódi adatszektorai sosem teljesen nullák.) */
int ata_read_sectors(u32 lba, u32 count, void *buf) {
    if (!ata_ready || !ata_is_atapi) return 0;
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
