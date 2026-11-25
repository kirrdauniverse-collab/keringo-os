/* fat32.c — FAT32 olvasás+írás blokkeszköz fölött (csatolás, lánc, írás, FSInfo)
 * 8.3 rövid nevek; a hosszú (LFN) bejegyzéseket átugorja. Többszintû útvonalak. */
#include "kernel.h"

#define MAX_FATFS 4
#define DIRBUF_SECS 96
#define DIRBUF_MAX  (DIRBUF_SECS * 4096)

struct fatfs {
    blkdev_t bd;                            /* másolat a partícióról */
    int mounted;
    u32 bps;                                /* bájt/szektor */
    u32 spc;                                /* szektor/klaszter */
    u32 nfats, fatsz, root_clus;
    u32 fat0;                               /* FAT kezdete (abszolút) */
    u32 data0;                              /* 2-es klaszter szektora (abszolút) */
    u32 nclusters;
    u32 free_hint;
    u32 fsinfo_sec;
    char label[12];
    u8 secbuf[4096];
    u32 secbuf_lba;
    int secbuf_valid;
    u8 fatbuf[4096];
    u32 fatbuf_lba;
    int fatbuf_valid;
};

static fatfs_t fs_pool[MAX_FATFS];
static u8 dirbuf_a[DIRBUF_MAX];             /* resolve_dir + read_dir közös */
static u8 dirbuf_b[DIRBUF_MAX];             /* fat32_read/write */
static u8 cbuf[DIRBUF_MAX];                 /* klaszter-puffer */

/* ---------- szektor I/O (1 szektoros gyorsítótárral) ---------- */
static int fs_read_sec(fatfs_t *fs, u32 lba, u8 *dst) {
    if (fs->secbuf_valid && fs->secbuf_lba == lba) {
        for (u32 i = 0; i < fs->bps; i++) dst[i] = fs->secbuf[i];
        return 1;
    }
    if (!fs->bd.read_secs(lba, 1, fs->secbuf, fs->bd.ctx)) return 0;
    fs->secbuf_lba = lba; fs->secbuf_valid = 1;
    for (u32 i = 0; i < fs->bps; i++) dst[i] = fs->secbuf[i];
    return 1;
}

static int fs_write_sec(fatfs_t *fs, u32 lba, const u8 *src) {
    for (u32 i = 0; i < fs->bps; i++) fs->secbuf[i] = src[i];
    fs->secbuf_lba = lba; fs->secbuf_valid = 1;
    return fs->bd.write_secs(lba, 1, fs->secbuf, fs->bd.ctx);
}

static u32 fat_get(fatfs_t *fs, u32 cluster) {
    u32 off = cluster * 4;
    u32 lba = fs->fat0 + off / fs->bps;
    if (!(fs->fatbuf_valid && fs->fatbuf_lba == lba)) {
        if (!fs->bd.read_secs(lba, 1, fs->fatbuf, fs->bd.ctx)) return 0x0FFFFFF8;
        fs->fatbuf_lba = lba; fs->fatbuf_valid = 1;
    }
    return *(u32 *)(fs->fatbuf + (off % fs->bps)) & 0x0FFFFFFFu;
}

static int fat_set(fatfs_t *fs, u32 cluster, u32 value) {
    u32 off = cluster * 4;
    u32 lba = fs->fat0 + off / fs->bps;
    if (!(fs->fatbuf_valid && fs->fatbuf_lba == lba)) {
        if (!fs->bd.read_secs(lba, 1, fs->fatbuf, fs->bd.ctx)) return 0;
        fs->fatbuf_lba = lba; fs->fatbuf_valid = 1;
    }
    u32 pos = off % fs->bps;
    u32 old = *(u32 *)(fs->fatbuf + pos);
    *(u32 *)(fs->fatbuf + pos) = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
    for (u32 f = 0; f < fs->nfats; f++)
        if (!fs->bd.write_secs(fs->fat0 + f * fs->fatsz + off / fs->bps, 1, fs->fatbuf, fs->bd.ctx))
            return 0;
    return 1;
}

static u32 clus_sec(fatfs_t *fs, u32 cluster) {
    return fs->data0 + (cluster - 2) * fs->spc;
}

/* a klaszterlánc adott dir-szektorának abszolút LBA-ja */
static u32 dir_nth_sec(fatfs_t *fs, u32 dir_clus, u32 n) {
    u32 c = dir_clus, si = 0;
    while (c >= 2 && c < 0x0FFFFFF8) {
        if (si + fs->spc > n) return clus_sec(fs, c) + (n - si);
        si += fs->spc;
        c = fat_get(fs, c);
    }
    return 0;
}

/* ---------- névkezelés ---------- */
static void fat_name(const u8 *ent, char *out) {
    int n = 0;
    for (int i = 0; i < 8 && ent[i] != ' '; i++) out[n++] = ent[i];
    if (ent[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && ent[i] != ' '; i++) out[n++] = ent[i];
    }
    out[n] = 0;
    for (int i = 0; i < n; i++)
        if (out[i] >= 'a' && out[i] <= 'z') out[i] -= 32;
}

static int name_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
        char cb = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* ---------- könyvtár ---------- */
static int read_dir_buf(fatfs_t *fs, u32 first_clus, u8 *buf, u32 *out_bytes) {
    u32 got = 0;
    u32 c = first_clus;
    while (c >= 2 && c < 0x0FFFFFF8 && got < DIRBUF_MAX) {
        for (u32 s = 0; s < fs->spc && got < DIRBUF_MAX; s++) {
            if (!fs_read_sec(fs, clus_sec(fs, c) + s, buf + got)) return 0;
            got += fs->bps;
        }
        c = fat_get(fs, c);
    }
    *out_bytes = got;
    return 1;
}

static int dir_find(const u8 *buf, u32 nbytes, const char *name,
                    u32 *first_clus, u32 *size, u8 *attr) {
    for (u32 off = 0; off + 32 <= nbytes; off += 32) {
        const u8 *e = buf + off;
        if (e[0] == 0x00) return -1;
        if (e[0] == 0xE5) continue;
        if ((e[11] & 0x3F) == 0x0F) continue;          /* LFN */
        if (e[11] & 0x08) continue;                    /* kötetfelirat */
        char nm[16];
        fat_name(e, nm);
        if (name_eq(nm, name)) {
            if (first_clus) *first_clus = ((u32)e[20] << 16) | ((u32)e[21] << 24)
                                          | e[26] | ((u32)e[27] << 8);
            if (size) *size = e[28] | ((u32)e[29] << 8) | ((u32)e[30] << 16) | ((u32)e[31] << 24);
            if (attr) *attr = e[11];
            return (int)off;
        }
    }
    return -1;
}

/* Többszintû útvonal-feloldás.
 * Vissza: 0 és *dir_clus = a célobjektum könyvtára; lastname = a célnév.
 * Ha az útvonal könyvtárra mutat: lastname üres, dir_clus = az a könyvtár. */
static int resolve_dir(fatfs_t *fs, const char *path, char *lastname, u32 *dir_clus) {
    u32 cur = fs->root_clus;
    char comp[16];
    int ci = 0;
    u32 dbn;
    lastname[0] = 0;
    const char *p = path;
    if (p && *p == '/') p++;
    for (; ; p++) {
        if (*p == '/' || *p == 0) {
            comp[ci] = 0;
            if (ci > 0) {
                if (!read_dir_buf(fs, cur, dirbuf_a, &dbn)) return -1;
                u32 fc, sz; u8 at;
                int off = dir_find(dirbuf_a, dbn, comp, &fc, &sz, &at);
                if (off < 0) {
                    if (*p == 0) {              /* utolsó elem: új fájl célnév */
                        *dir_clus = cur;
                        for (int k = 0; k < 15 && comp[k]; k++) lastname[k] = comp[k];
                        lastname[ci < 15 ? ci : 15] = 0;
                        return 0;
                    }
                    (void)dbn;
                    return -1;
                }
                if (at & 0x10) {
                    if (*p == 0) { *dir_clus = fc; lastname[0] = 0; return 0; }
                    cur = fc;
                } else {
                    if (*p == 0) {
                        *dir_clus = cur;
                        for (int k = 0; k < 15 && comp[k]; k++) lastname[k] = comp[k];
                        lastname[ci < 15 ? ci : 15] = 0;
                        return 0;
                    }
                    return -1;
                }
            }
            ci = 0;
            if (*p == 0) break;
            continue;
        }
        if (ci < 15) comp[ci++] = *p;
    }
    *dir_clus = cur;                             /* maga az útvonal egy könyvtár */
    return 0;
}

/* ---------- csatolás ---------- */
int fat32_mount(blkdev_t *bd, fatfs_t **out) {
    fatfs_t *fs = 0;
    for (int i = 0; i < MAX_FATFS; i++)
        if (!fs_pool[i].mounted) { fs = &fs_pool[i]; break; }
    if (!fs) return 0;

    u8 b[4096];
    if (!bd->read_secs(bd->start_lba, 1, b, bd->ctx)) return 0;
    if (b[510] != 0x55 || b[511] != 0xAA) return 0;
    u32 bps = b[11] | ((u32)b[12] << 8);
    u32 spc = b[13];
    u32 rsvd = b[14] | ((u32)b[15] << 8);
    u32 nfats = b[16];
    u32 fatsz = b[36] | ((u32)b[37] << 8) | ((u32)b[38] << 16) | ((u32)b[39] << 24);
    u32 root_clus = b[44] | ((u32)b[45] << 8) | ((u32)b[46] << 16) | ((u32)b[47] << 24);
    u32 totsec = b[32] | ((u32)b[33] << 8) | ((u32)b[34] << 16) | ((u32)b[35] << 24);
    if (bps < 512 || bps > 4096 || (bps & (bps - 1))) return 0;
    if (spc == 0 || (spc & (spc - 1))) return 0;
    if (fatsz == 0 || nfats == 0 || nfats > 4) return 0;    /* FAT12/16: nem jó */
    if (root_clus < 2) return 0;
    if (totsec == 0) totsec = bd->nsecs;

    for (u8 *p = (u8 *)fs; p < (u8 *)fs + sizeof(fatfs_t); p++) *p = 0;
    fs->bd = *bd;
    fs->bps = bps; fs->spc = spc;
    fs->nfats = nfats; fs->fatsz = fatsz; fs->root_clus = root_clus;
    fs->fat0 = bd->start_lba + rsvd;
    fs->data0 = fs->fat0 + nfats * fatsz;
    u32 data_secs = totsec > (fs->data0 - bd->start_lba) ? totsec - (fs->data0 - bd->start_lba) : 0;
    fs->nclusters = data_secs / spc;
    if (fs->nclusters < 65525) { kprintf("fat32: keves a klaszter (%u), FAT16?\n", fs->nclusters); return 0; }
    if (fs->spc * fs->bps > DIRBUF_MAX) { kprintf("fat32: túl nagy klaszter\n"); return 0; }
    fs->free_hint = 2;
    fs->fsinfo_sec = b[48] | ((u32)b[49] << 8);
    fs->secbuf_valid = 0;
    fs->fatbuf_valid = 0;
    for (int i = 0; i < 11; i++) fs->label[i] = ' ';
    fs->label[11] = 0;

    /* gyökér olvasása: címke */
    u32 dbn;
    if (read_dir_buf(fs, fs->root_clus, dirbuf_a, &dbn)) {
        for (u32 off = 0; off + 32 <= dbn; off += 32) {
            if (dirbuf_a[off] == 0x00) break;
            if (dirbuf_a[off + 11] == 0x08) {        /* kötetfelirat */
                for (int i = 0; i < 11; i++) fs->label[i] = dirbuf_a[off + i];
                break;
            }
        }
    }
    fs->mounted = 1;
    *out = fs;
    kprintf("fat32: felcsatolva lba=%u bps=%u spc=%u klaszter=%u\n",
            bd->start_lba, bps, spc, fs->nclusters);
    return 1;
}

const char *fat32_label(fatfs_t *fs) { return fs->label; }

/* ---------- listázás ---------- */
int fat32_list(fatfs_t *fs, const char *path, char names[][64], u32 *sizes, u8 *flags, int max) {
    char last[16];
    u32 dir_clus;
    if (resolve_dir(fs, path, last, &dir_clus) < 0) return -1;
    if (last[0]) return -1;
    u32 dbn;
    if (!read_dir_buf(fs, dir_clus, dirbuf_b, &dbn)) return -1;
    int n = 0;
    for (u32 off = 0; off + 32 <= dbn && n < max; off += 32) {
        const u8 *e = dirbuf_b + off;
        if (e[0] == 0x00) break;
        if (e[0] == 0xE5) continue;
        if ((e[11] & 0x3F) == 0x0F) continue;
        if (e[11] & 0x08) continue;
        char nm[16];
        fat_name(e, nm);
        if (nm[0] == 0) continue;
        int j;
        for (j = 0; j < 63 && nm[j]; j++) names[n][j] = nm[j];
        names[n][j] = 0;
        if (sizes) sizes[n] = e[28] | ((u32)e[29] << 8) | ((u32)e[30] << 16) | ((u32)e[31] << 24);
        if (flags) flags[n] = (e[11] & 0x10) ? 1 : 0;
        n++;
    }
    return n;
}

/* ---------- olvasás ---------- */
int fat32_read(fatfs_t *fs, const char *path, u8 *buf, u32 bufsize, u32 *outlen) {
    char last[16];
    u32 dir_clus;
    if (resolve_dir(fs, path, last, &dir_clus) < 0 || !last[0]) return 0;
    u32 dbn;
    if (!read_dir_buf(fs, dir_clus, dirbuf_b, &dbn)) return 0;
    u32 first, size; u8 attr;
    if (dir_find(dirbuf_b, dbn, last, &first, &size, &attr) < 0) return 0;
    if (attr & 0x10) return 0;
    u32 want = size < bufsize ? size : bufsize;
    u32 got = 0;
    u32 c = first;
    while (c >= 2 && c < 0x0FFFFFF8 && got < want) {
        for (u32 s = 0; s < fs->spc && got < want; s++) {
            if (!fs_read_sec(fs, clus_sec(fs, c) + s, cbuf)) return 0;
            u32 take = fs->bps;
            if (got + take > want) take = want - got;
            for (u32 i = 0; i < take; i++) buf[got + i] = cbuf[i];
            got += take;
        }
        c = fat_get(fs, c);
    }
    *outlen = got;
    return 1;
}

/* ---------- írás ---------- */
static u32 find_free_clus(fatfs_t *fs, u32 from) {
    u32 c = from < 2 ? 2 : from;
    if (c >= fs->nclusters + 2) c = 2;
    for (u32 i = 0; i < fs->nclusters; i++) {
        if (fat_get(fs, c) == 0) return c;
        c++;
        if (c >= fs->nclusters + 2) c = 2;
    }
    return 0;
}

/* dirbuf_b-be olvasott könyvtár n. szektorának visszaírása */
static int write_dir_sec(fatfs_t *fs, u32 dir_clus, u32 n, u32 dbn) {
    if (n * fs->bps >= dbn) return 0;
    u32 lba;
    if (dir_clus == 0) return 0;
    lba = dir_nth_sec(fs, dir_clus, n);
    if (!lba) return 0;
    return fs_write_sec(fs, lba, dirbuf_b + n * fs->bps);
}

int fat32_write(fatfs_t *fs, const char *path, const u8 *buf, u32 len) {
    char last[16];
    u32 dir_clus;
    if (resolve_dir(fs, path, last, &dir_clus) < 0 || !last[0]) return 0;
    if (len > 8u * 1024 * 1024) { kprintf("fat32: túl nagy fájl\n"); return 0; }
    u32 dbn;
    if (!read_dir_buf(fs, dir_clus, dirbuf_b, &dbn)) { kprintf("fat32w: dir olvasas hiba\n"); return 0; }

    u32 first = 0;
    int ent_off = dir_find(dirbuf_b, dbn, last, &first, 0, 0);
    u32 clusbytes = fs->spc * fs->bps;
    u32 need = (len + clusbytes - 1) / clusbytes;
    if (need == 0) need = 1;

    /* meglévő lánc hossza */
    u32 have = 0, head = 0, tail = 0;
    if (first >= 2 && first < 0x0FFFFFF8) {
        head = first;
        u32 c = first;
        while (c >= 2 && c < 0x0FFFFFF8 && have < 200000) {
            tail = c; have++;
            c = fat_get(fs, c);
        }
    }

    /* szűkítés */
    if (have > need) {
        u32 c = head;
        for (u32 k = 1; k < need; k++) c = fat_get(fs, c);
        u32 extra = fat_get(fs, c);
        fat_set(fs, c, 0x0FFFFFF8);
        while (extra >= 2 && extra < 0x0FFFFFF8) {
            u32 nx = fat_get(fs, extra);
            fat_set(fs, extra, 0);
            extra = nx;
        }
        have = need;
    }

    /* bővítés */
    if (have < need) {
        if (head == 0) {
            head = find_free_clus(fs, fs->free_hint);
            if (head == 0) { kprintf("fat32: nincs szabad klaszter\n"); return 0; }
            tail = head; have = 1;
        }
        while (have < need) {
            u32 nc = find_free_clus(fs, tail + 1);
            if (nc == 0) { kprintf("fat32: megtelt a lemez\n"); return 0; }
            fat_set(fs, tail, nc);
            tail = nc;
            have++;
        }
        fs->free_hint = tail + 1 < fs->nclusters + 2 ? tail + 1 : 2;
    }
    if (fat_get(fs, tail) < 0x0FFFFFF8) fat_set(fs, tail, 0x0FFFFFF8);

    /* adat kiírása */
    u32 c = head, done = 0;
    for (u32 k = 0; k < need && c >= 2; k++) {
        for (u32 s = 0; s < fs->spc; s++) {
            if (done >= len) break;
            u32 lba = clus_sec(fs, c) + s;
            u32 take = fs->bps;
            if (done + take <= len) {
                if (!fs->bd.write_secs(lba, 1, buf + done, fs->bd.ctx)) return 0;
                done += take;
            } else {
                if (!fs_read_sec(fs, lba, cbuf)) return 0;   /* részleges: óvatos */
                take = len - done;
                for (u32 i = 0; i < take; i++) cbuf[i] = buf[done + i];
                if (!fs_write_sec(fs, lba, cbuf)) return 0;
                done += take;
            }
        }
        c = fat_get(fs, c);
    }

    /* könyvtárbejegyzés */
    if (ent_off >= 0) {
        u8 *e = dirbuf_b + ent_off;
        e[20] = (u8)(head >> 16); e[21] = (u8)(head >> 24);
        e[26] = (u8)head; e[27] = (u8)(head >> 8);
        e[28] = (u8)len; e[29] = (u8)(len >> 8);
        e[30] = (u8)(len >> 16); e[31] = (u8)(len >> 24);
        e[11] |= 0x20;
        if (!write_dir_sec(fs, dir_clus, (u32)ent_off / fs->bps, dbn)) return 0;
    } else {
        int slot = -1;
        for (u32 off = 0; off + 32 <= dbn; off += 32)
            if (dirbuf_b[off] == 0x00 || dirbuf_b[off] == 0xE5) { slot = (int)off; break; }
        if (slot < 0) { kprintf("fat32: nincs szabad dir-bejegyzés\n"); return 0; }
        u8 *e = dirbuf_b + slot;
        for (int i = 0; i < 32; i++) e[i] = 0;
        char base[9]; char ext[4];
        int bi = 0, ei = 0, dot = -1;
        for (int i = 0; last[i]; i++) if (last[i] == '.') dot = i;
        if (dot < 0) { for (int i = 0; last[i] && bi < 8; i++) base[bi++] = last[i]; }
        else {
            for (int i = 0; i < dot && bi < 8; i++) base[bi++] = last[i];
            for (int i = dot + 1; last[i] && ei < 3; i++) ext[ei++] = last[i];
        }
        for (int i = bi; i < 8; i++) base[i] = ' ';
        for (int i = ei; i < 3; i++) ext[i] = ' ';
        for (int i = 0; i < 8; i++) e[i] = (base[i] >= 'a' && base[i] <= 'z') ? base[i] - 32 : base[i];
        for (int i = 0; i < 3; i++) e[8 + i] = (ext[i] >= 'a' && ext[i] <= 'z') ? ext[i] - 32 : ext[i];
        e[11] = 0x20;
        e[20] = (u8)(head >> 16); e[21] = (u8)(head >> 24);
        e[26] = (u8)head; e[27] = (u8)(head >> 8);
        e[28] = (u8)len; e[29] = (u8)(len >> 8);
        e[30] = (u8)(len >> 16); e[31] = (u8)(len >> 24);
        if (!write_dir_sec(fs, dir_clus, (u32)slot / fs->bps, dbn)) return 0;
    }

    /* FSInfo: szabad-darab ismeretlen, tipp mentése */
    if (fs->fsinfo_sec) {
        u8 fi[512];
        if (fs->bd.read_secs(fs->bd.start_lba + fs->fsinfo_sec, 1, fi, fs->bd.ctx)) {
            *(u32 *)(fi + 488) = 0xFFFFFFFF;
            *(u32 *)(fi + 492) = fs->free_hint;
            fs->bd.write_secs(fs->bd.start_lba + fs->fsinfo_sec, 1, fi, fs->bd.ctx);
        }
    }
    return 1;
}

int fat32_flush(fatfs_t *fs) { (void)fs; return 1; }

/* ================= 0.5: formázás, törlés, átnevezés ================= */

/* --- FAT32 formázás egy (partíció)blokkeszközre.
 * Layout: 512 b/szektor, klaszterméret úgy választva, hogy >= 65525 klaszter legyen
 * (a FAT32 szabvány és a fat32_mount feltétele), rsvd=32, 2 FAT, gyökér a 2-es klaszter. */
int fat32_format(blkdev_t *bd, const char *label) {
    if (!bd || bd->nsecs < 70000) { kprintf("fat32f: kicsi a terulet (%u szektor)\n", bd ? bd->nsecs : 0); return 0; }

    u32 spc = 1;
    while (bd->nsecs / spc >= 140000 && spc < 8) spc <<= 1;    /* max ~2 KB klaszter */
    while (bd->nsecs / spc < 65525 && spc < 16) spc <<= 1;      /* FAT32 min klaszterszam */
    if (bd->nsecs / spc < 65525) { kprintf("fat32f: meg mindig kicsi\n"); return 0; }
    u32 nclus = bd->nsecs / spc;
    u32 fatsz = (nclus * 4 + 511) / 512;
    u32 rsvd = 32, nfats = 2, root_clus = 2;
    u32 fat0 = rsvd, data0 = rsvd + nfats * fatsz;
    u32 totsec = bd->nsecs;

    /* --- boot-szektor --- */
    static u8 bs[512];
    for (int i = 0; i < 512; i++) bs[i] = 0;
    bs[0] = 0xEB; bs[1] = 0x58; bs[2] = 0x90;
    for (int i = 0; i < 8 && "KERINGO05"[i]; i++) bs[3 + i] = "KERINGO05"[i];
    bs[11] = 512 & 255; bs[12] = 512 >> 8;      /* bps */
    bs[13] = (u8)spc;
    bs[14] = (u8)(rsvd & 255); bs[15] = (u8)(rsvd >> 8);
    bs[16] = (u8)nfats;
    bs[17] = 0; bs[18] = 0;                      /* rootent = 0 */
    bs[19] = 0; bs[20] = 0;                      /* totsec16 = 0 */
    bs[21] = 0xF8;                                /* merevlemez-media */
    bs[22] = 0; bs[23] = 0;                      /* fatsz16 = 0 */
    bs[24] = 63; bs[25] = 0; bs[26] = 255; bs[27] = 0;   /* CHS: 63 szektor/nyom, 255 fej */
    bs[28] = 0; bs[29] = 0;                      /* hidden */
    bs[32] = (u8)totsec; bs[33] = (u8)(totsec >> 8); bs[34] = (u8)(totsec >> 16); bs[35] = (u8)(totsec >> 24);
    bs[36] = (u8)(fatsz & 255); bs[37] = (u8)(fatsz >> 8); bs[38] = (u8)(fatsz >> 16); bs[39] = (u8)(fatsz >> 24);
    bs[40] = (u8)0; bs[41] = (u8)0;              /* ext flags */
    bs[42] = 0; bs[43] = 0;                       /* fsver */
    bs[44] = (u8)root_clus; bs[45] = (u8)(root_clus >> 8); bs[46] = 0; bs[47] = 0;
    bs[48] = 1; bs[49] = 0;                       /* FSInfo szektor */
    bs[50] = 6; bs[51] = 0;                       /* backup boot */
    bs[510] = 0x55; bs[511] = 0xAA;
    /* adatok a BPB végén */
    bs[64] = 0x80;                                /* meghajtószám */
    bs[66] = 0x29;                                /* kiterjesztett aláírás */
    u32 volid = tick_count() * 305419896u;
    bs[67] = (u8)volid; bs[68] = (u8)(volid >> 8); bs[69] = (u8)(volid >> 16); bs[70] = (u8)(volid >> 24);
    for (int i = 0; i < 11; i++) bs[71 + i] = ' ';
    for (int i = 0; i < 11 && label[i]; i++) bs[71 + i] = (u8)(label[i] >= 'a' && label[i] <= 'z' ? label[i] - 32 : label[i]);
    for (int i = 0; i < 8 && "FAT32   "[i]; i++) bs[82 + i] = "FAT32   "[i];
    if (!bd->write_secs(bd->start_lba, 1, bs, bd->ctx)) return 0;

    /* --- FSInfo --- */
    static u8 fi[512];
    for (int i = 0; i < 512; i++) fi[i] = 0;
    fi[0]='R'; fi[1]='R'; fi[2]='a'; fi[3]='A';              /* vezér-aláírás */
    fi[484]='r'; fi[485]='r'; fi[486]='A'; fi[487]='A';     /* struktúra-aláírás */
    *(u32 *)(fi + 488) = nclus - 3;              /* szabad klaszterek (3 foglalt) */
    *(u32 *)(fi + 492) = 3;                       /* következő szabad */
    fi[510] = 0x55; fi[511] = 0xAA;
    if (!bd->write_secs(bd->start_lba + 1, 1, fi, bd->ctx)) return 0;

    /* --- FAT[0..1] (mindkét példány) --- */
    static u8 fsec[512];
    for (int i = 0; i < 512; i++) fsec[i] = 0;
    fsec[0] = 0xF8; fsec[1] = 0xFF; fsec[2] = 0xFF; fsec[3] = 0x0F;   /* 0. bejegyzés */
    fsec[4] = 0xFF; fsec[5] = 0xFF; fsec[6] = 0xFF; fsec[7] = 0x0F;   /* 1. bejegyzés */
    fsec[8] = 0xF8; fsec[9] = 0xFF; fsec[10] = 0xFF; fsec[11] = 0x0F; /* 2. = gyökér EOC */
    for (u32 f = 0; f < nfats; f++)
        for (u32 s = 0; s < fatsz; s++) {
            if (s == 0) { if (!bd->write_secs(bd->start_lba + fat0 + f * fatsz + s, 1, fsec, bd->ctx)) return 0; }
            else {
                static u8 zer[512];
                if (!bd->write_secs(bd->start_lba + fat0 + f * fatsz + s, 1, zer, bd->ctx)) return 0;
            }
        }

    /* --- gyökérkönyvtár első szektora: kötetfelirat --- */
    static u8 root[512];
    for (int i = 0; i < 512; i++) root[i] = 0;
    for (int i = 0; i < 11 && label[i]; i++) root[i] = (u8)(label[i] >= 'a' && label[i] <= 'z' ? label[i] - 32 : label[i]);
    root[11] = 0x08;                             /* ATTR_VOLUME_ID */
    if (!bd->write_secs(bd->start_lba + data0, 1, root, bd->ctx)) return 0;
    kprintf("fat32f: OK — %u klaszter, spc=%u, fatsz=%u\n", nclus, spc, fatsz);
    return 1;
}

/* --- fájl törlése (könyvtárbejegyzés 0xE5 + klaszterlánc felszabadítása) --- */
int fat32_delete(fatfs_t *fs, const char *path) {
    char last[16];
    u32 dir_clus;
    if (resolve_dir(fs, path, last, &dir_clus) < 0 || !last[0]) return 0;
    u32 dbn;
    if (!read_dir_buf(fs, dir_clus, dirbuf_b, &dbn)) return 0;
    u32 first; u8 attr;
    int off = dir_find(dirbuf_b, dbn, last, &first, 0, &attr);
    if (off < 0) return 0;
    if (attr & 0x10) return 0;                    /* könyvtár törlése most nem */
    /* lánc felszabadítása */
    u32 c = first;
    while (c >= 2 && c < 0x0FFFFFF8) {
        u32 nx = fat_get(fs, c);
        fat_set(fs, c, 0);
        c = nx;
    }
    /* bejegyzés törlése */
    dirbuf_b[off] = 0xE5;
    if (!write_dir_sec(fs, dir_clus, (u32)off / fs->bps, dbn)) return 0;
    return 1;
}

/* --- átnevezés (8.3 mező újraírása) --- */
int fat32_rename(fatfs_t *fs, const char *path, const char *newname) {
    char last[16];
    u32 dir_clus;
    if (resolve_dir(fs, path, last, &dir_clus) < 0 || !last[0]) return 0;
    u32 dbn;
    if (!read_dir_buf(fs, dir_clus, dirbuf_b, &dbn)) return 0;
    u32 first; u8 attr;
    int off = dir_find(dirbuf_b, dbn, last, &first, 0, &attr);
    if (off < 0) return 0;
    u8 *e = dirbuf_b + off;
    char base[9]; char ext[4];
    int bi = 0, ei = 0, dot = -1;
    for (int i = 0; newname[i]; i++) if (newname[i] == '.') dot = i;
    if (dot < 0) { for (int i = 0; newname[i] && bi < 8; i++) base[bi++] = newname[i]; }
    else {
        for (int i = 0; i < dot && bi < 8; i++) base[bi++] = newname[i];
        for (int i = dot + 1; newname[i] && ei < 3; i++) ext[ei++] = newname[i];
    }
    for (int i = bi; i < 8; i++) base[i] = ' ';
    for (int i = ei; i < 3; i++) ext[i] = ' ';
    for (int i = 0; i < 8; i++) e[i] = (u8)((base[i] >= 'a' && base[i] <= 'z') ? base[i] - 32 : base[i]);
    for (int i = 0; i < 3; i++) e[8 + i] = (u8)((ext[i] >= 'a' && ext[i] <= 'z') ? ext[i] - 32 : ext[i]);
    if (!write_dir_sec(fs, dir_clus, (u32)off / fs->bps, dbn)) return 0;
    return 1;
}
