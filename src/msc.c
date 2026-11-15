/* msc.c — USB tömegtároló (MSC, Bulk-Only Transport) + SCSI parancsok */
#include "kernel.h"
#include "usb.h"

static usb_dev_t *msc_dev = 0;
static u32 msc_nsecs = 0;
static u32 msc_secsize = 512;

#define CBW_SIG 0x43425355u   /* 'USBC' */
#define CSW_SIG 0x53425355u   /* 'USBS' */

static int msc_cmd(usb_dev_t *d, const u8 *cdb, u8 cdlen, int dir_in,
                   u8 *data, u32 datalen);

/* ---- BOT egy parancs ---- */
static int msc_cmd(usb_dev_t *d, const u8 *cdb, u8 cdlen, int dir_in,
                   u8 *data, u32 datalen) {
    u8 cbw[31];
    u8 csw[16];
    for (int i = 0; i < 31; i++) cbw[i] = 0;
    if (cdlen < 6) cdlen = 6;
    if (cdlen > 16) cdlen = 16;
    u32 tag = ++d->tag;
    *(u32 *)(cbw + 0)  = CBW_SIG;
    *(u32 *)(cbw + 4)  = tag;
    *(u32 *)(cbw + 8)  = datalen;
    cbw[12] = dir_in ? 0x80 : 0x00;
    cbw[13] = 0;                            /* LUN */
    cbw[14] = cdlen;
    for (int i = 0; i < cdlen; i++) cbw[15 + i] = cdb[i];

    /* CSW-várakozás segéd: adatfázis + CSW együtt */
    int r;
    r = d->bus->bulk(d->bus, d->addr, d->bulk_out, 0, d->bulk_maxpkt, cbw, 31, &d->tog_out);
    if (r != 31) { kprintf("msc: CBW hiba %d\n", r); return -1; }

    if (datalen > 0 && data) {
        r = d->bus->bulk(d->bus, d->addr, dir_in ? d->bulk_in : d->bulk_out,
                         dir_in, d->bulk_maxpkt, data, (int)datalen,
                         dir_in ? &d->tog_in : &d->tog_out);
        if (r < 0) { kprintf("msc: adatfazis hiba %d\n", r); return -2; }
    }
    r = d->bus->bulk(d->bus, d->addr, d->bulk_in, 1, d->bulk_maxpkt, csw, 13, &d->tog_in);
    if (r != 13) { kprintf("msc: CSW hiba %d\n", r); return -3; }
    u32 csig = *(u32 *)(csw + 0);
    u32 ctag = *(u32 *)(csw + 4);
    if (csig != CSW_SIG || ctag != tag) { kprintf("msc: CSW alairas gaz\n"); return -4; }
    if (csw[12] != 0) { kprintf("msc: CSW status=%d\n", csw[12]); return -5; }
    return 0;
}

/* endpoint-halt törlése (hibaelhárításhoz) */
static void msc_clear_halt(usb_dev_t *d, u8 ep, int dir_in) {
    usb_dev_req_t req;
    req.bmRequestType = 0x02;
    req.bRequest = 1;                       /* CLEAR_FEATURE */
    req.wValue = 0;                         /* ENDPOINT_HALT */
    req.wIndex = (u16)(ep | (dir_in ? 0x80 : 0x00));
    req.wLength = 0;
    d->bus->control(d->bus, d->addr, d->speed, d->ep0max, &req, 0, 0);
    /* a toggle vissza DATA0-ra áll a HALT után */
    if (dir_in) d->tog_in = 0; else d->tog_out = 0;
}

static int msc_cmd_rec(usb_dev_t *d, const u8 *cdb, u8 cdlen, int dir_in,
                       u8 *data, u32 datalen) {
    int r = msc_cmd(d, cdb, cdlen, dir_in, data, datalen);
    if (r < 0) {
        msc_clear_halt(d, d->bulk_in, 1);
        msc_clear_halt(d, d->bulk_out, 0);
        usb_msleep(30);
        r = msc_cmd(d, cdb, cdlen, dir_in, data, datalen);
    }
    return r;
}

/* ---- SCSI parancsok ---- */
static int scsi_tur(usb_dev_t *d) {
    u8 cdb[6] = { 0 };
    return msc_cmd(d, cdb, 6, 0, 0, 0);
}
static int scsi_sense(usb_dev_t *d, u8 *sense) {
    u8 cdb[6] = { 3, 0, 0, 0, 18, 0 };
    return msc_cmd(d, cdb, 6, 1, sense, 18);
}

int msc_attach(usb_dev_t *dev) {
    u8 buf[40];
    u8 cdb[10];
    msc_dev = dev;

    /* INQUIRY */
    for (int i = 0; i < 6; i++) cdb[i] = 0;
    cdb[0] = 0x12; cdb[4] = 36;
    if (msc_cmd_rec(dev, cdb, 6, 1, buf, 36) < 0) {
        kprintf("msc: INQUIRY hiba\n");
        msc_dev = 0;
        return 0;
    }
    kprintf("msc: INQUIRY tipus=%d ver=%d\n", buf[0], buf[2]);

    /* TEST UNIT READY — pendrive-oknak kell pár 100 ms */
    int ok = 0;
    for (int i = 0; i < 25 && !ok; i++) {
        ok = (scsi_tur(dev) == 0);
        if (!ok) {
            u8 sense[20];
            scsi_sense(dev, sense);
            usb_msleep(100);
        }
    }
    if (!ok) { kprintf("msc: nem ready\n"); msc_dev = 0; return 0; }

    /* READ CAPACITY(10) */
    for (int i = 0; i < 10; i++) cdb[i] = 0;
    cdb[0] = 0x25;
    if (msc_cmd_rec(dev, cdb, 10, 1, buf, 8) < 0) {
        kprintf("msc: READ_CAPACITY hiba\n");
        msc_dev = 0;
        return 0;
    }
    u32 last = ((u32)buf[0] << 24) | ((u32)buf[1] << 16) | ((u32)buf[2] << 8) | buf[3];
    u32 ssz  = ((u32)buf[4] << 24) | ((u32)buf[5] << 16) | ((u32)buf[6] << 8) | buf[7];
    if (ssz < 512 || ssz > 4096) ssz = 512;
    msc_secsize = ssz;
    msc_nsecs = last + 1;
    kprintf("msc: kapacitas=%u szektor x %u b = %u MB\n", msc_nsecs, ssz, (msc_nsecs / 2) * ssz / 1024 / 1024 * 2);
    return 1;
}

int msc_present(void) { return msc_dev != 0; }

void msc_shutdown(void) { msc_dev = 0; }
u32 msc_sector_count(void) { return msc_nsecs; }

int msc_read_secs(u32 lba, u32 n, u8 *buf) {
    if (!msc_dev || n == 0) return 0;
    u32 done = 0;
    while (done < n) {
        u32 chunk = n - done;
        if (chunk > 32) chunk = 32;         /* 16 KB lépés */
        u8 cdb[10] = { 0 };
        cdb[0] = 0x28;                      /* READ(10) */
        u32 l = lba + done;
        cdb[2] = (u8)(l >> 24); cdb[3] = (u8)(l >> 16); cdb[4] = (u8)(l >> 8); cdb[5] = (u8)l;
        cdb[7] = (u8)(chunk >> 8); cdb[8] = (u8)chunk;
        if (msc_cmd_rec(msc_dev, cdb, 10, 1, buf + done * msc_secsize, chunk * msc_secsize) < 0)
            return 0;
        done += chunk;
    }
    return 1;
}

int msc_write_secs(u32 lba, u32 n, const u8 *buf) {
    if (!msc_dev || n == 0) return 0;
    u32 done = 0;
    while (done < n) {
        u32 chunk = n - done;
        if (chunk > 32) chunk = 32;
        u8 cdb[10] = { 0 };
        cdb[0] = 0x2A;                      /* WRITE(10) */
        u32 l = lba + done;
        cdb[2] = (u8)(l >> 24); cdb[3] = (u8)(l >> 16); cdb[4] = (u8)(l >> 8); cdb[5] = (u8)l;
        cdb[7] = (u8)(chunk >> 8); cdb[8] = (u8)chunk;
        if (msc_cmd_rec(msc_dev, cdb, 10, 0, (u8 *)buf + done * msc_secsize, chunk * msc_secsize) < 0)
            return 0;
        done += chunk;
    }
    return 1;
}

int msc_sync(void) {
    if (!msc_dev) return 0;
    u8 cdb[10] = { 0 };
    cdb[0] = 0x35;                          /* SYNCHRONIZE CACHE(10) */
    return msc_cmd(msc_dev, cdb, 10, 0, 0, 0) == 0;
}
