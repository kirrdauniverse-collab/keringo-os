/* jpeg.c — baseline JPEG dekóder (DCT, Huffman, 4:2:0/4:2:2/4:4:4, szürkeárnyalatos)
 * Fixpontos AAN-szerû IDCT-pár helyett egyszerû szeparált koszinusz-IDCT (Q13).
 * Progressive JPEG elutasítása (0.3: tervezett). */
#include "kernel.h"

#define MAXCOMP 3

typedef struct {
    int id, h, v, tq;
    int dc_tbl, ac_tbl;
    int dcpred;
    /* sík */
    u8 *plane;
    int pw, ph;                             /* sík mérete (MCU-ra igazítva) */
} jcomp_t;

typedef struct {
    u16 firstcode[17];                       /* adott hosszú elsõ kód */
    u16 count[17];                           /* adott hosszú kódok száma */
    u16 firstsym[17];                        /* elsõ szimbólum-index */
    u8  syms[256];
    int nsyms;
} huff_t;

static huff_t h_dc[4], h_ac[4];
/* statikus sík-pufferek (újrahasznosíthatóak: nincs memóriaszivárgás stream-nél) */
#define PLANE_CAP (3 * 1280 * 1024)
static u8 plane_scratch[PLANE_CAP];
#define PIX_CAP (1280 * 1024)
static u32 pix_scratch[PIX_CAP];
static u16 qtab[4][64];                     /* természetes sorrendben */
static jcomp_t comp[MAXCOMP];
static int ncomp = 0;
static int width = 0, height = 0;
static int hmax = 1, vmax = 1;
static int mcux, mcuy, mcuw, mcuh;
static int restart_interval = 0;
static const u8 *bitptr, *bitend;
static u32 bitbuf = 0;
static int bitcnt = 0;

/* zigzag → természetes */
static const u8 zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* IDCT koszinusz-tábla Q13: c[u][x] = cos((2x+1)u*pi/16) * 8192 */
static const int ictab[8][8] = {
    { 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192 },
    { 11363, 9633, 6437, 2260, -2260, -6437, -9633, -11363 },
    { 10703, 4433, -4433, -10703, -10703, -4433, 4433, 10703 },
    { 9633, -2260, -11363, -6437, 6437, 11363, 2260, -9633 },
    { 8192, -8192, -8192, 8192, 8192, -8192, -8192, 8192 },
    { 6437, -11363, 2260, 9633, -9633, -2260, 11363, -6437 },
    { 4433, -10703, 10703, -4433, -4433, 10703, -10703, 4433 },
    { 2260, -6437, 9633, -11363, 11363, -9633, 6437, -2260 },
};

/* ---------- bitolvasó (stuff + marker) ---------- */
static int nextbyte(void) {
    if (bitptr >= bitend) return -1;
    u8 b = *bitptr++;
    if (b == 0xFF) {
        if (bitptr >= bitend) return -1;
        u8 b2 = *bitptr++;
        if (b2 == 0x00) return 0xFF;
        /* marker! (pl. RST) — visszatekerünk, jelezzük 0xFF ... */
        bitptr -= 2;
        return -2;                          /* marker határ */
    }
    return b;
}

static int getbit(void) {
    if (bitcnt == 0) {
        int b = nextbyte();
        bitbuf = (u32)(b < 0 ? 0xFF : b) << 8;
        bitcnt = 8;
    }
    bitcnt--;
    return (bitbuf >> (8 + bitcnt)) & 1;
}

/* n bit olvasása MSB elõször */
static int getbits(int n) {
    int v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | getbit();
    return v;
}

/* ---------- Huffman (kanonikus séta: O(kódhossz)) ---------- */
static void load_huff(huff_t *h, const u8 *counts, const u8 *syms) {
    int k = 0;
    u32 code = 0;
    h->nsyms = 0;
    for (int len = 1; len <= 16; len++) {
        int cnt = counts[len - 1];
        h->firstcode[len] = (u16)code;
        h->firstsym[len] = (u16)k;
        h->count[len] = (u16)cnt;
        for (int i = 0; i < cnt; i++) h->syms[k++] = syms[i];
        h->nsyms += cnt;
        code = (code + (u32)cnt) << 1;
    }
}

static int huff_decode(huff_t *h) {
    u32 code = 0;
    for (int len = 1; len <= 16; len++) {
        code = (code << 1) | (u32)getbit();
        int d = (int)code - (int)h->firstcode[len];
        if (d >= 0 && d < (int)h->count[len])
            return h->syms[h->firstsym[len] + d];
    }
    return -1;
}

/* előjel-kiterjesztés */
static int extend(int v, int sz) {
    if (sz == 0) return 0;
    if (v < (1 << (sz - 1))) return v - (1 << sz) + 1;
    return v;
}

/* ---------- blokk dekódolás ---------- */
static int decode_block(int *out64, jcomp_t *c) {
    for (int i = 0; i < 64; i++) out64[i] = 0;
    huff_t *dc = &h_dc[c->dc_tbl];
    huff_t *ac = &h_ac[c->ac_tbl];
    int t = huff_decode(dc);
    if (t < 0) return 0;
    int diff = extend(getbits(t), t);
    c->dcpred += diff;
    out64[0] = c->dcpred * (int)qtab[c->tq][0];
    int k = 1;
    while (k < 64) {
        int rs = huff_decode(ac);
        if (rs < 0) return 0;
        int run = rs >> 4;
        int sz = rs & 15;
        if (sz == 0) {
            if (run == 15) { k += 16; continue; }
            break;                          /* EOB */
        }
        k += run;
        if (k > 63) break;
        int v = extend(getbits(sz), sz);
        out64[zigzag[k]] = v * (int)qtab[c->tq][k];
        k++;
    }
    return 1;
}

/* szeparált IDCT: freq[8] -> spat[8], q13 */
static void idct1d(const int *freq, int *spat) {
    for (int x = 0; x < 8; x++) {
        int sum = 0;
        const int *row = ictab[x];
        for (int u = 0; u < 8; u++) sum += row[u] * freq[u];
        spat[x] = sum;
    }
}

static void idct_block(const int *in, u8 *out, int stride) {
    int tmp[64];
    /* oszlopok */
    for (int i = 0; i < 8; i++) {
        int f[8], s[8];
        for (int u = 0; u < 8; u++) f[u] = in[u * 8 + i];
        idct1d(f, s);
        for (int y = 0; y < 8; y++) tmp[y * 8 + i] = s[y];
    }
    /* sorok */
    for (int y = 0; y < 8; y++) {
        int s[8];
        idct1d(&tmp[y * 8], s);
        u8 *row = out + y * stride;
        for (int x = 0; x < 8; x++) {
            int v = (s[x] + (1 << 12)) >> 13;
            v += 128;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            row[x] = (u8)v;
        }
    }
}

/* ---------- marker-feldolgozás ---------- */
static u16 rd16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }

static void reset_dc(void) {
    for (int i = 0; i < ncomp; i++) comp[i].dcpred = 0;
    bitcnt = 0;
}

/* ---------- fõ dekóder ---------- */
int jpeg_decode(const u8 *buf, u32 len, u32 **out_pix, int *out_w, int *out_h) {
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {
        kprintf("jpeg: nincs SOI\n");
        return 0;
    }
    ncomp = 0;
    hmax = vmax = 1;
    restart_interval = 0;
    u32 pos = 2;
    int sof_type = -1;
    while (pos + 4 <= len) {
        if (buf[pos] != 0xFF) { pos++; continue; }
        u8 marker = buf[pos + 1];
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { pos += 2; continue; }
        if (pos + 4 > len) break;
        u32 seglen = rd16(buf + pos + 2);
        if (seglen < 2 || pos + 2 + seglen > len) break;
        const u8 *seg = buf + pos + 4;      /* szegmens-adat */
        u32 segdatalen = seglen - 2;

        if (marker == 0xDB) {               /* DQT */
            u32 o = 0;
            while (o + 65 <= segdatalen + 1 && o + 1 <= segdatalen) {
                int pq = seg[o] >> 4, tq = seg[o] & 15;
                if (pq == 0 && tq < 4) {
                    for (int i = 0; i < 64; i++) qtab[tq][zigzag[i]] = seg[o + 1 + i];
                    o += 65;
                } else if (pq == 1 && tq < 4) {
                    /* 16 bites (ritka) — átalakítjuk */
                    for (int i = 0; i < 64; i++) {
                        u16 v = rd16(seg + o + 1 + i * 2);
                        qtab[tq][zigzag[i]] = v >> 8;
                    }
                    o += 129;
                } else break;
            }
        } else if (marker == 0xC4) {        /* DHT */
            u32 o = 0;
            while (o + 17 <= segdatalen) {
                int cls = seg[o] >> 4, id = seg[o] & 15;
                const u8 *counts = seg + o + 1;
                int total = 0;
                for (int i = 0; i < 16; i++) total += counts[i];
                if (o + 17 + total > segdatalen) break;
                if (id < 4) {
                    huff_t *h = cls ? &h_ac[id] : &h_dc[id];
                    load_huff(h, counts, counts + 16);
                }
                o += 17 + (u32)total;
            }
        } else if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            sof_type = marker;
            if (marker != 0xC0 && marker != 0xC1) {
                kprintf("jpeg: nem baseline SOF (0x%x) - progressziv? elutasitva\n", marker);
                return 0;
            }
            height = rd16(seg + 1);
            width = rd16(seg + 3);
            ncomp = seg[5];
            if (ncomp < 1 || ncomp > 3 || width < 1 || height < 1) return 0;
            for (int i = 0; i < ncomp; i++) {
                comp[i].id = seg[6 + i * 3];
                comp[i].h = seg[7 + i * 3] >> 4;
                comp[i].v = seg[7 + i * 3] & 15;
                comp[i].tq = seg[8 + i * 3];
                if (comp[i].h < 1) comp[i].h = 1;
                if (comp[i].v < 1) comp[i].v = 1;
                if (comp[i].h > 2 || comp[i].v > 2) { kprintf("jpeg: tul nagy mintavetel\n"); return 0; }
            }
            for (int i = 0; i < ncomp; i++) {
                if (comp[i].h > hmax) hmax = comp[i].h;
                if (comp[i].v > vmax) vmax = comp[i].v;
            }
        } else if (marker == 0xDD) {        /* DRI */
            restart_interval = rd16(seg);
        } else if (marker == 0xDA) {        /* SOS */
            /* szken paraméterek */
            int ns = seg[0];
            for (int i = 0; i < ns && i < ncomp; i++) {
                int cs = seg[1 + i * 2];
                int td = seg[2 + i * 2] >> 4;
                int ta = seg[2 + i * 2] & 15;
                for (int j = 0; j < ncomp; j++)
                    if (comp[j].id == cs) { comp[j].dc_tbl = td; comp[j].ac_tbl = ta; }
            }
            /* síkok foglalása */
            mcuw = 8 * hmax; mcuh = 8 * vmax;
            mcux = (width + mcuw - 1) / mcuw;
            mcuy = (height + mcuh - 1) / mcuh;
            {
                u32 off = 0;
                for (int i = 0; i < ncomp; i++) {
                    comp[i].pw = mcux * comp[i].h * 8;
                    comp[i].ph = mcuy * comp[i].v * 8;
                    u32 sz = (u32)comp[i].pw * comp[i].ph;
                    if (off + sz > PLANE_CAP) { kprintf("jpeg: nagy kep\n"); return 0; }
                    comp[i].plane = plane_scratch + off;
                    off += sz;
                }
                if ((u32)width * height > PIX_CAP) { kprintf("jpeg: tul nagy kep\n"); return 0; }
            }
            /* szken-adat a szegmens után */
            bitptr = buf + pos + 2 + seglen;
            bitend = buf + len;
            bitcnt = 0;
            reset_dc();
            int rst_cnt = 0;
            int blocks[64];
            for (int my = 0; my < mcuy; my++) {
                for (int mx = 0; mx < mcux; mx++) {
                    for (int ci = 0; ci < ncomp; ci++) {
                        jcomp_t *c = &comp[ci];
                        for (int by = 0; by < c->v; by++)
                            for (int bx = 0; bx < c->h; bx++) {
                                if (!decode_block(blocks, c)) goto scan_done;
                                u8 *dst = c->plane
                                        + (u32)(my * c->v + by) * 8 * (u32)c->pw
                                        + (u32)(mx * c->h + bx) * 8;
                                idct_block(blocks, dst, c->pw);
                            }
                    }
                    rst_cnt++;
                    if (restart_interval && rst_cnt >= restart_interval) {
                        rst_cnt = 0;
                        /* RST marker keresése: bit-puffer eldobása */
                        bitcnt = 0;
                        while (bitptr + 1 < bitend) {
                            if (bitptr[0] == 0xFF && (bitptr[1] & 0xF8) == 0xD0) {
                                bitptr += 2;
                                break;
                            }
                            bitptr++;
                        }
                        reset_dc();
                    }
                }
            }
        scan_done:
            /* színkonverzió */
            {
                u32 *pix = pix_scratch;
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int yy = comp[0].plane[(u32)y * comp[0].pw + x];
                        int r, g, b;
                        if (ncomp == 3) {
                            int cb = comp[1].plane[(u32)(y / vmax) * comp[1].pw + (x / hmax)] - 128;
                            int cr = comp[2].plane[(u32)(y / vmax) * comp[2].pw + (x / hmax)] - 128;
                            r = yy + ((359 * cr) >> 8);
                            g = yy - ((88 * cb + 183 * cr) >> 8);
                            b = yy + ((454 * cb) >> 8);
                        } else {
                            r = g = b = yy;
                        }
                        if (r < 0) r = 0; if (r > 255) r = 255;
                        if (g < 0) g = 0; if (g > 255) g = 255;
                        if (b < 0) b = 0; if (b > 255) b = 255;
                        pix[(u32)y * width + x] = 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
                    }
                }
                *out_pix = pix;
                *out_w = width;
                *out_h = height;
                return 1;
            }
        }
        pos += 2 + seglen;
    }
    kprintf("jpeg: SOS nem talalhato (sof=%d)\n", sof_type);
    return 0;
}

/* ---------- BMP (24/32 bites, tömörítetlen) ---------- */
int bmp_decode(const u8 *buf, u32 len, u32 **out_pix, int *out_w, int *out_h) {
    if (len < 54 || buf[0] != 'B' || buf[1] != 'M') return 0;
    u32 off = buf[10] | ((u32)buf[11] << 8) | ((u32)buf[12] << 16) | ((u32)buf[13] << 24);
    u32 hdr = buf[14] | ((u32)buf[15] << 8) | ((u32)buf[16] << 16) | ((u32)buf[17] << 24);
    if (hdr < 40) return 0;
    u32 w = buf[18] | ((u32)buf[19] << 8) | ((u32)buf[20] << 16) | ((u32)buf[21] << 24);
    s32 h = (s32)(buf[22] | ((u32)buf[23] << 8) | ((u32)buf[24] << 16) | ((u32)buf[25] << 24));
    u16 bpp = (u16)(buf[28] | ((u32)buf[29] << 8));
    u32 comp = buf[30] | ((u32)buf[31] << 8) | ((u32)buf[32] << 16) | ((u32)buf[33] << 24);
    if (comp != 0 || (bpp != 24 && bpp != 32) || w == 0 || w > 4096) return 0;
    int flip = h > 0;
    int hh = flip ? h : -h;
    u32 *pix = (u32 *)kmalloc(w * (u32)hh * 4);
    if (!pix) return 0;
    u32 rowsz = ((w * bpp + 31) / 32) * 4;
    for (int y = 0; y < hh; y++) {
        int sy = flip ? hh - 1 - y : y;
        const u8 *row = buf + off + (u32)sy * rowsz;
        for (u32 x = 0; x < w; x++) {
            const u8 *p = row + x * (bpp / 8);
            u8 b = p[0], g = p[1], r = p[2];
            pix[(u32)y * w + x] = 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | b;
        }
    }
    *out_pix = pix;
    *out_w = (int)w;
    *out_h = hh;
    return 1;
}

/* ---------- kép átméretezés (legközelebbi szomszéd) ---------- */
u32 *img_scale(const u32 *src, int sw, int sh, int dw, int dh) {
    u32 *dst = (u32 *)kmalloc((u32)dw * dh * 4);
    if (!dst) return 0;
    for (int y = 0; y < dh; y++) {
        u32 sy = (u32)((y * sh) / dh);
        if (sy >= (u32)sh) sy = sh - 1;
        const u32 *srow = src + sy * sw;
        u32 *drow = dst + (u32)y * dw;
        for (int x = 0; x < dw; x++) {
            u32 sx = (u32)((x * sw) / dw);
            if (sx >= (u32)sw) sx = sw - 1;
            drow[x] = srow[sx];
        }
    }
    return dst;
}
