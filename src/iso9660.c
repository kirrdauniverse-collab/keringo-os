/* iso9660.c — read-only ISO9660 olvasás a boot CD-ről */
#include "kernel.h"

static int  iso_ok = 0;
static u32  root_lba = 0;
static u32  root_size = 0;

static char iso_buf[2048];          /* munkapuffer egy szektorhoz */
static char dir_buf[32768];         /* munkapuffer egy könyvtárhoz (16 szektor) */

/* gyökér-könyvtár gyorsítótár: CSAK EGY ATAPI olvasás */
static char  cache_names[16][64];
static u32   cache_lbas[16];
static u32   cache_sizes[16];
static u8    cache_flags[16];
static int   cache_n = -1;

static u32 rd32_le(const u8 *p)  { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

int iso_mount(void) {
    iso_ok = 0;
    if (!ata_present()) { kprintf("iso: nincs ata\n"); return 0; }
    /* elsődleges kötet-leíró: LBA 16 */
    if (ata_read_sectors(16, 1, iso_buf) != 2048) { kprintf("iso: PVD olvasasi hiba\n"); return 0; }
    if (iso_buf[0] != 1 || iso_buf[1] != 'C' || iso_buf[2] != 'D' ||
        iso_buf[3] != '0' || iso_buf[4] != '0' || iso_buf[5] != '1') {
        kprintf("iso: nem ISO9660 (sig=%c%c%c)\n", iso_buf[1], iso_buf[2], iso_buf[3]);
        return 0;
    }
    const u8 *rootrec = (const u8 *)(iso_buf + 156);
    root_lba  = rd32_le(rootrec + 2);
    root_size = rd32_le(rootrec + 10);
    iso_ok = 1;
    kprintf("iso: felcsatolva, gyoker lba=%d meret=%d\n", root_lba, root_size);
    return 1;
}

int iso_mounted(void) { return iso_ok; }

/* könyvtár-beolvasás: names feltöltése, sizes/flags opcionális */
int iso_list(u32 dir_lba, u32 dir_size, char names[][64], u32 *lbas, u32 *sizes, u8 *flags, int max) {
    if (!iso_ok) return 0;
    u32 sectors = (dir_size + 2047) / 2048;
    if (sectors > 16) sectors = 16;
    int count = 0;
    u32 off = 0;
    for (u32 s = 0; s < sectors; s++) {
        int got = ata_read_sectors(dir_lba + s, 1, dir_buf + s * 2048);
        if (got != 2048) { kprintf("iso: dir szektor olvasas FAIL lba=%d got=%d\n", dir_lba + s, got); break; }
    }
    while (off < sectors * 2048 && count < max) {
        u8 len = (u8)dir_buf[off];
        if (len == 0) { /* rekordhatáron átlógás: következő szektorra ugrás */
            off = ((off / 2048) + 1) * 2048;
            if (off >= sectors * 2048) break;
            continue;
        }
        const u8 *rec = (const u8 *)(dir_buf + off);
        u8 nl = rec[32];
        u8 fl = rec[25];
        if (nl > 0 && !(nl == 1 && (rec[33] == 0 || rec[33] == 1))) {  /* . és .. kihagyása */
            int j;
            for (j = 0; j < 63 && j < nl; j++) {
                char c = rec[33 + j];
                if (c == ';') break;
                names[count][j] = c;
            }
            names[count][j] = 0;
            if (lbas)  lbas[count]  = rd32_le(rec + 2);
            if (sizes) sizes[count] = rd32_le(rec + 10);
            if (flags) flags[count] = fl;
            count++;
        }
        off += len;
    }
    return count;
}

int iso_root_list(char names[][64], u32 *lbas, u32 *sizes, u8 *flags, int max) {
    if (cache_n < 0) {
        char tmp_names[16][64];
        cache_n = iso_list(root_lba, root_size, tmp_names, cache_lbas, cache_sizes, cache_flags, 16);
        for (int k = 0; k < cache_n && k < 16; k++) {
            int j;
            for (j = 0; j < 63 && tmp_names[k][j]; j++) cache_names[k][j] = tmp_names[k][j];
            cache_names[k][j] = 0;
        }
        if (cache_n < 0) cache_n = 0;
    }
    int n = cache_n < max ? cache_n : max;
    for (int k = 0; k < n; k++) {
        if (names) { int j; for (j = 0; j < 63 && cache_names[k][j]; j++) names[k][j] = cache_names[k][j]; names[k][j] = 0; }
        if (lbas)  lbas[k]  = cache_lbas[k];
        if (sizes) sizes[k] = cache_sizes[k];
        if (flags) flags[k] = cache_flags[k];
    }
    return n;
}

int iso_read_file(u32 lba, u32 size, char *buf, int bufsize, int *outlen) {
    if (!iso_ok) return 0;
    int n = (size < (u32)bufsize) ? size : bufsize;
    u32 sectors = (n + 2047) / 2048;
    static char tmp[2048 * 8];
    int got = 0;
    for (u32 s = 0; s < sectors && got < n; s++) {
        u32 chunk = sectors - s; if (chunk > 8) chunk = 8;
        if (ata_read_sectors(lba + s, chunk, tmp) != chunk * 2048) return 0;
        u32 take = chunk * 2048;
        if (got + (int)take > n) take = n - got;
        for (u32 i = 0; i < take; i++) buf[got + i] = tmp[i];
        got += take;
        s += chunk - 1;
    }
    *outlen = got;
    return 1;
}

int iso_find(const char *name, u32 *lba, u32 *size) {
    /* a gyorsítótárból keres — nincs újabb ATAPI olvasás */
    if (cache_n < 0) iso_root_list(0, 0, 0, 0, 0);
    int n = cache_n;
    for (int i = 0; i < n; i++) {
        /* kis-nagybetű független összehasonlítás */
        const char *a = name, *b = cache_names[i];
        int eq = 1;
        while (*a || *b) {
            char ca = *a ? (*a >= 'a' && *a <= 'z' ? *a - 32 : *a) : 0;
            char cb = *b ? (*b >= 'a' && *b <= 'z' ? *b - 32 : *b) : 0;
            if (ca != cb) { eq = 0; break; }
            if (*a) a++; if (*b) b++;
        }
        if (eq) { *lba = cache_lbas[i]; *size = cache_sizes[i]; return 1; }
    }
    return 0;
}
