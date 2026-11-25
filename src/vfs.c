/* vfs.c — VFS 0.2: /cd (ISO9660), /mem (RAMFS), /sys (HDD FAT32), /usb (pendrájv FAT32) */
#include "kernel.h"

/* ---------- RAMFS ---------- */
#define RAMFS_MAX 12
#define RAMFS_CAP 8192
typedef struct { char name[32]; char buf[RAMFS_CAP]; int size; int used; } ramfile_t;
static ramfile_t ramfs[RAMFS_MAX];

int ramfs_write(const char *name, const char *data, int len) {
    if (len > RAMFS_CAP) len = RAMFS_CAP;
    for (int i = 0; i < RAMFS_MAX; i++) {
        if (ramfs[i].used && ramfs_strcmp(ramfs[i].name, name)) {
            for (int j = 0; j < len; j++) ramfs[i].buf[j] = data[j];
            ramfs[i].size = len;
            return 1;
        }
    }
    for (int i = 0; i < RAMFS_MAX; i++) {
        if (!ramfs[i].used) {
            int j;
            for (j = 0; j < 31 && name[j]; j++) ramfs[i].name[j] = name[j];
            ramfs[i].name[j] = 0;
            for (int k = 0; k < len; k++) ramfs[i].buf[k] = data[k];
            ramfs[i].size = len;
            ramfs[i].used = 1;
            return 1;
        }
    }
    return 0;
}

int ramfs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

int ramfs_count(void) { int n = 0; for (int i = 0; i < RAMFS_MAX; i++) if (ramfs[i].used) n++; return n; }
const char *ramfs_name(int i) { return ramfs[i].used ? ramfs[i].name : ""; }
int ramfs_size(int i) { return ramfs[i].used ? ramfs[i].size : 0; }
int ramfs_read(const char *name, char *buf, int bufsize, int *outlen) {
    for (int i = 0; i < RAMFS_MAX; i++) {
        if (ramfs[i].used && ramfs_strcmp(ramfs[i].name, name)) {
            int n = ramfs[i].size < bufsize ? ramfs[i].size : bufsize;
            for (int j = 0; j < n; j++) buf[j] = ramfs[i].buf[j];
            *outlen = n;
            return 1;
        }
    }
    return 0;
}

/* ---------- eszköz-adapterek ---------- */
static int rd_usb(u32 lba, u32 n, void *buf, void *ctx) { (void)ctx; return usb_read_secs(lba, n, buf); }
static int wr_usb(u32 lba, u32 n, const void *buf, void *ctx) { (void)ctx; return usb_write_secs(lba, n, buf); }
static int rd_ata(u32 lba, u32 n, void *buf, void *ctx) { return ata_disk_read_secs((int)(uptr)ctx, lba, n, buf); }
static int wr_ata(u32 lba, u32 n, const void *buf, void *ctx) { return ata_disk_write_secs((int)(uptr)ctx, lba, n, buf); }

typedef int (*devrd_t)(u32, u32, void *, void *);

/* ---------- FAT32 csatolások ---------- */
static fatfs_t *sys_fs = 0;
static fatfs_t *usb_fs = 0;
static int sys_ready = 0, usb_ready_f = 0;

static int boot_done = 0;
void storage_boot_done(void) { boot_done = 1; }
int  storage_ready_for_ui(void) { return boot_done; }

int sys_mounted(void)  { return sys_ready; }
int usbfs_mounted(void) { return usb_ready_f; }
fatfs_t *vfs_sys_fs(void)  { return sys_fs; }
fatfs_t *vfs_usb_fs(void)  { return usb_fs; }

/* egy eszköz partícióinak felsorolása (MBR elsõdleges + logikai lánc).
 * devsecs: teljes eszköz mérete (szektor) — superfloppy estére. */
static int scan_partitions(devrd_t rd, void *ctx, u32 devsecs,
                           u32 *starts, u32 *counts, int max) {
    static u8 sec[512];
    if (!rd(0, 1, sec, ctx)) return 0;
    int n = 0;
    /* superfloppy? (közvetlenül FAT boot-szektor) */
    if ((sec[0] == 0xEB || sec[0] == 0xE9) && sec[510] == 0x55 && sec[511] == 0xAA) {
        if (n < max) { starts[n] = 0; counts[n] = devsecs; n++; }
        return n;
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) return 0;

    /* elsõdleges bejegyzések */
    u32 ext_start = 0;
    for (int e = 0; e < 4; e++) {
        u8 *en = sec + 446 + e * 16;
        u8 type = en[4];
        u32 lba = en[8] | ((u32)en[9] << 8) | ((u32)en[10] << 16) | ((u32)en[11] << 24);
        u32 cnt = en[12] | ((u32)en[13] << 8) | ((u32)en[14] << 16) | ((u32)en[15] << 24);
        if (type == 0 || cnt == 0) continue;
        if (type == 0x05 || type == 0x0F) {
            if (ext_start == 0) ext_start = lba;
        } else if (n < max) {
            starts[n] = lba; counts[n] = cnt; n++;
        }
    }

    /* logikai partíciók az EBR-láncon */
    if (ext_start) {
        u32 ebr = ext_start;
        for (int r = 0; r < 16 && n < max; r++) {
            if (!rd(ebr, 1, sec, ctx)) break;
            if (sec[510] != 0x55 || sec[511] != 0xAA) break;
            u8 *e0 = sec + 446;
            u8 *e1 = sec + 462;
            u32 l0 = e0[8] | ((u32)e0[9] << 8) | ((u32)e0[10] << 16) | ((u32)e0[11] << 24);
            u32 c0 = e0[12] | ((u32)e0[13] << 8) | ((u32)e0[14] << 16) | ((u32)e0[15] << 24);
            if (e0[4] != 0 && c0 > 0) {
                starts[n] = ebr + l0;            /* az EBR-hez képesti */
                counts[n] = c0;
                n++;
            }
            if (e1[4] == 0x05 || e1[4] == 0x0F) {
                u32 l1 = e1[8] | ((u32)e1[9] << 8) | ((u32)e1[10] << 16) | ((u32)e1[11] << 24);
                if (l1 == 0) break;
                ebr = ext_start + l1;           /* a lánc elejéhez képesti */
            } else break;
        }
    }
    return n;
}

/* FAT32 csatolási kísérlet egy partícióra */
static int try_mount(devrd_t rd, int (*wr)(u32, u32, const void *, void *), void *ctx,
                     u32 start, u32 count, int is_usb, const char *plabel,
                     fatfs_t **out) {
    blkdev_t bd;
    for (u8 *p = (u8 *)&bd; p < (u8 *)&bd + sizeof(bd); p++) *p = 0;
    int j;
    for (j = 0; j < 15 && plabel[j]; j++) bd.label[j] = plabel[j];
    bd.label[j] = 0;
    bd.usb = is_usb;
    bd.read_secs = rd;
    bd.write_secs = wr;
    bd.start_lba = start;
    bd.nsecs = count;
    bd.secsize = 512;
    bd.ctx = ctx;
    return fat32_mount(&bd, out);
}

/* van-e a partíción rendszer-jelölõ fájl? */
static int fs_is_system(fatfs_t *fs) {
    u8 tmp[32];
    u32 len;
    if (fat32_read(fs, "/BOOT/KERINGO.ELF", tmp, 16, &len)) return 1;
    if (fat32_read(fs, "/KERINGO.ELF", tmp, 16, &len)) return 1;
    if (fat32_read(fs, "/KERINGO.ID", tmp, 16, &len)) return 1;
    return 0;
}

int vfs_mount_all(void) {
    sys_ready = usb_ready_f = 0;
    sys_fs = usb_fs = 0;

    /* --- USB pendrájv --- */
    if (usb_ready()) {
        static u32 st[24], ct[24];
        u32 devsecs = msc_sector_count();
        int np = scan_partitions(rd_usb, 0, devsecs, st, ct, 24);
        int mounted = 0;
        if (np == 0) {
            /* egy próbálkozás: az egész eszköz */
            if (try_mount(rd_usb, wr_usb, 0, 0, devsecs, 1, "PENDRIVE", &usb_fs)) mounted = 1;
        }
        for (int i = 0; i < np && !mounted; i++) {
            if (try_mount(rd_usb, wr_usb, 0, st[i], ct[i], 1, "PENDRIVE", &usb_fs)) mounted = 1;
        }
        if (mounted) {
            usb_ready_f = 1;
            kprintf("vfs: /usb felcsatolva\n");
        } else {
            kprintf("vfs: pendrájv nem FAT32 (probald ujra FAT32-kent)\n");
        }
    }

    /* --- ATA merevlemezek --- */
    for (int d = 0; d < ata_disk_count(); d++) {
        static u32 st[24], ct[24];
        int np = scan_partitions(rd_ata, (void *)(uptr)d, 0, st, ct, 24);
        for (int i = 0; i < np && !sys_ready; i++) {
            fatfs_t *fs = 0;
            char lbl[16];
            ksnprintf(lbl, sizeof(lbl), "sda%d", i + 1);
            if (try_mount(rd_ata, wr_ata, (void *)(uptr)d, st[i], ct[i], 0, lbl, &fs)) {
                if (fs_is_system(fs)) {
                    sys_fs = fs;
                    sys_ready = 1;
                    kprintf("vfs: /sys = %s (lba=%u)\n", lbl, st[i]);
                }
            }
        }
    }
    if (!sys_ready) kprintf("vfs: nincs rendszerpartíció (/sys) a merevlemezen\n");
    return 1;
}

/* 0.5: kézi /sys-csatolás a Telepítőnek — a megadott partíciót csatolja /sys-ként
 * (a telepítés után a vfs_mount_all() a jelölő alapján már megtalálja). */
int vfs_mount_sys_manual(int usb, int idx, u32 lba, u32 cnt) {
    sys_fs = 0; sys_ready = 0;
    if (usb) {
        if (!try_mount(rd_usb, wr_usb, 0, lba, cnt, 1, "PENDRIVE", &sys_fs)) return 0;
    } else {
        char lbl[16];
        ksnprintf(lbl, sizeof(lbl), "sda%d", idx + 1);
        if (!try_mount(rd_ata, wr_ata, (void *)(uptr)idx, lba, cnt, 0, lbl, &sys_fs)) return 0;
    }
    sys_ready = 1;
    kprintf("vfs: /sys kézzel felcsatolva (lba=%u)\n", lba);
    return 1;
}

/* ---------- VFS ---------- */
int vfs_list(const char *dir, char names[][64], u32 *sizes, u8 *flags, int max) {
    if (vfs_strcmp(dir, "/cd"))  return iso_mounted() ? iso_root_list(names, 0, sizes, flags, max) : -1;
    if (vfs_strcmp(dir, "/mem")) {
        int n = 0;
        for (int i = 0; i < RAMFS_MAX && n < max; i++) {
            if (ramfs[i].used) {
                int j;
                for (j = 0; j < 63 && ramfs[i].name[j]; j++) names[n][j] = ramfs[i].name[j];
                names[n][j] = 0;
                sizes[n] = ramfs[i].size;
                flags[n] = 0;
                n++;
            }
        }
        return n;
    }
    if (dir[0] == '/' && dir[1] == 's' && dir[2] == 'y' && dir[3] == 's') {
        if (!sys_ready) return -1;
        return fat32_list(sys_fs, dir + 4, names, sizes, flags, max);
    }
    if (dir[0] == '/' && dir[1] == 'u' && dir[2] == 's' && dir[3] == 'b') {
        if (!usb_ready_f) return -1;
        return fat32_list(usb_fs, dir + 4, names, sizes, flags, max);
    }
    return -1;
}

int vfs_read_file(const char *path, char *buf, int bufsize, int *outlen) {
    if (path[0] == '/' && path[1] == 'c' && path[2] == 'd' && path[3] == '/') {
        u32 lba, size;
        if (!iso_mounted()) return 0;
        if (iso_find(path + 4, &lba, &size)) return iso_read_file(lba, size, buf, bufsize, outlen);
        /* 0.5: alkönyvtárak az ISO-n (pl. /cd/boot/keringo.elf) */
        if (iso_find_path(path + 4, &lba, &size)) return iso_read_file(lba, size, buf, bufsize, outlen);
        return 0;
    }
    if (path[0] == '/' && path[1] == 'm' && path[2] == 'e' && path[3] == 'm' && path[4] == '/') {
        return ramfs_read(path + 5, buf, bufsize, outlen);
    }
    if (path[0] == '/' && path[1] == 's' && path[2] == 'y' && path[3] == 's' && path[4] == '/') {
        if (!sys_ready) return 0;
        u32 got;
        if (fat32_read(sys_fs, path + 5, (u8 *)buf, (u32)bufsize, &got)) { *outlen = (int)got; return 1; }
        return 0;
    }
    if (path[0] == '/' && path[1] == 'u' && path[2] == 's' && path[3] == 'b' && path[4] == '/') {
        if (!usb_ready_f) return 0;
        u32 got;
        if (fat32_read(usb_fs, path + 5, (u8 *)buf, (u32)bufsize, &got)) { *outlen = (int)got; return 1; }
        return 0;
    }
    return 0;
}

/* név alapú keresés a tárolók közt: /sys, /usb, /cd, /mem sorrendben */
int vfs_read_any(const char *name, u8 *buf, u32 bufsize, u32 *outlen) {
    char path[96];
    ksnprintf(path, sizeof(path), "/sys/%s", name);
    int out;
    if (vfs_read_file(path, (char *)buf, (int)bufsize, &out)) { *outlen = (u32)out; return 1; }
    ksnprintf(path, sizeof(path), "/usb/%s", name);
    if (vfs_read_file(path, (char *)buf, (int)bufsize, &out)) { *outlen = (u32)out; return 1; }
    ksnprintf(path, sizeof(path), "/cd/%s", name);
    if (vfs_read_file(path, (char *)buf, (int)bufsize, &out)) { *outlen = (u32)out; return 1; }
    ksnprintf(path, sizeof(path), "/mem/%s", name);
    if (vfs_read_file(path, (char *)buf, (int)bufsize, &out)) { *outlen = (u32)out; return 1; }
    return 0;
}

int vfs_write_file(const char *path, const char *data, int len) {
    if (path[0] == '/' && path[1] == 'm' && path[2] == 'e' && path[3] == 'm' && path[4] == '/') {
        return ramfs_write(path + 5, data, len);
    }
    if (path[0] == '/' && path[1] == 's' && path[2] == 'y' && path[3] == 's' && path[4] == '/') {
        if (!sys_ready) return 0;
        return fat32_write(sys_fs, path + 5, (const u8 *)data, (u32)len);
    }
    if (path[0] == '/' && path[1] == 'u' && path[2] == 's' && path[3] == 'b' && path[4] == '/') {
        if (!usb_ready_f) return 0;
        return fat32_write(usb_fs, path + 5, (const u8 *)data, (u32)len);
    }
    return 0;
}

int vfs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
