/* gfx.c — framebuffer, dupla puffer, primitívek, text, textúra, 3D textúra-rajzolás
 * 0.5.1: bpp-agnosztikus kimenet — a UI mindig 32 bites háttérbufferben rajzol,
 * a gfx_flip 8/16/24/32 bites VBE módba konvertál (a GRUB menü mindkettő
 * elérhető). 8 bites módnál a RAMDAC palettát RGB332-re állítjuk.
 * Szép mód segédek: élsímított (AA) vonal/kör, gradiens, lekerekítés, árnyék. */
#include "kernel.h"

static u32  fb_w = 0, fb_h = 0, fb_pitch = 0;
static u32 *fb_lin = 0;         /* valódi framebuffer (0x80000000) */
static u32 *back = 0;           /* dupla puffer */
static u8   fb_bpp = 32;         /* kimeneti bitszínesség */
static u32  clr_fg = 0xFFF0F0F0, clr_bg = 0xFF1A2126;

/* RAMDAC: 8 bites módnál RGB332 paletta a videó-DAC-be (0x3C8/0x3C9) */
static void ramdac_rgb332(void) {
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        int r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
        outb(0x3C9, (u8)(r * 63 / 7));
        outb(0x3C9, (u8)(g * 63 / 7));
        outb(0x3C9, (u8)(b * 63 / 3));
    }
}

void gfx_init(multiboot_info_t *mbi) {
    fb_w = mbi->framebuffer_width;
    fb_h = mbi->framebuffer_height;
    fb_pitch = mbi->framebuffer_pitch;
    fb_bpp = mbi->framebuffer_bpp ? mbi->framebuffer_bpp : 32;
    fb_lin = (u32 *)mem_fb_vaddr();
    if (fb_bpp == 8) ramdac_rgb332();
    back = (u32 *)kmalloc(fb_w * fb_h * 4);
    for (u32 i = 0; i < fb_w * fb_h; i++) back[i] = 0xFF10151A;
    kprintf("gfx: %dx%d bpp=%d pitch=%d buffer=%x\n", fb_w, fb_h, fb_bpp, fb_pitch, (u32)back);
}

u32 *gfx_backbuf(void) { return back; }
u32 gfx_width(void)  { return fb_w; }
u32 gfx_height(void) { return fb_h; }
u32 gfx_bpp(void)    { return fb_bpp; }

/* 32 bites 0xFFRRGGBB -> célformátum */
static u8 to332(u32 c) {
    return (u8)((((c >> 16) & 0xFF) >> 5) << 5 | ((((c >> 8) & 0xFF) >> 5) << 2) | (((c) & 0xFF) >> 6));
}

void gfx_flip(void) {
    if (!fb_lin || !back) return;
    u8 *base = (u8 *)fb_lin;
    u32 *src = back;
    if (fb_bpp == 32) {
        for (u32 y = 0; y < fb_h; y++) {
            u32 *dst = (u32 *)(base + y * fb_pitch);
            u32 *row = src + y * fb_w;
            for (u32 x = 0; x < fb_w; x++) dst[x] = row[x];
        }
    } else if (fb_bpp == 16) {
        for (u32 y = 0; y < fb_h; y++) {
            u16 *dst = (u16 *)(base + y * fb_pitch);
            u32 *row = src + y * fb_w;
            for (u32 x = 0; x < fb_w; x++) {
                u32 c = row[x];
                dst[x] = (u16)((((c >> 16) & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | ((c & 0xF8) >> 3));
            }
        }
    } else if (fb_bpp == 24) {
        for (u32 y = 0; y < fb_h; y++) {
            u8 *dst = base + y * fb_pitch;
            u32 *row = src + y * fb_w;
            for (u32 x = 0; x < fb_w; x++) {
                u32 c = row[x];
                dst[3 * x]     = (u8)(c & 0xFF);
                dst[3 * x + 1] = (u8)((c >> 8) & 0xFF);
                dst[3 * x + 2] = (u8)((c >> 16) & 0xFF);
            }
        }
    } else {                      /* 8 bites palettás */
        for (u32 y = 0; y < fb_h; y++) {
            u8 *dst = base + y * fb_pitch;
            u32 *row = src + y * fb_w;
            for (u32 x = 0; x < fb_w; x++) dst[x] = to332(row[x]);
        }
    }
}

void gfx_clear(u32 color) {
    for (u32 i = 0; i < fb_w * fb_h; i++) back[i] = color;
}

void gfx_pixel(int x, int y, u32 color) {
    if (x < 0 || y < 0 || (u32)x >= fb_w || (u32)y >= fb_h) return;
    back[(u32)y * fb_w + (u32)x] = color;
}

void gfx_fill_rect(int x, int y, int w, int h, u32 color) {
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || (u32)yy >= fb_h) continue;
        u32 *row = back + (u32)yy * fb_w;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || (u32)xx >= fb_w) continue;
            row[xx] = color;
        }
    }
}

/* ---------- 0.5.1 Szép mód: keverés, élsímítás, gradiens ---------- */

u32 gfx_lerp_color(u32 c1, u32 c2, u32 t) {   /* t: 0..255 */
    if (t > 255) t = 255;
    u32 r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    u32 r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    u32 r = r1 + ((r2 - r1) * t) / 255;
    u32 g = g1 + ((g2 - g1) * t) / 255;
    u32 b = b1 + ((b2 - b1) * t) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* egész számú négyzetgyök (freestanding: nincs libm sqrtf) */
static int isqrt_i(int v) {
    if (v <= 0) return 0;
    int r = 0, bit = 1 << 14;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

void gfx_blend_pixel(int x, int y, u32 c, u32 alpha) {  /* alpha 0..255 */
    if (x < 0 || y < 0 || (u32)x >= fb_w || (u32)y >= fb_h || alpha == 0) return;
    if (alpha >= 255) { back[(u32)y * fb_w + (u32)x] = c; return; }
    u32 d = back[(u32)y * fb_w + (u32)x];
    u32 dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
    u32 sr = (c >> 16) & 0xFF, sg = (c >> 8) & 0xFF, sb = c & 0xFF;
    u32 r = dr + ((sr - dr) * alpha) / 255;
    u32 g = dg + ((sg - dg) * alpha) / 255;
    u32 b = db + ((sb - db) * alpha) / 255;
    back[(u32)y * fb_w + (u32)x] = 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Xiaolin Wu-féle élsímított vonal */
void gfx_aa_line(int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 - x0, dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx == 0 && dy == 0) { gfx_blend_pixel(x0, y0, c, 255); return; }
    if (dx >= dy) {
        if (x0 > x1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        float grad = (x1 != x0) ? (float)(y1 - y0) / (float)(x1 - x0) : 0.0f;
        float y = (float)y0 + 0.5f;
        for (int x = x0; x <= x1; x++) {
            int yi = (int)y;
            float f = y - (float)yi;
            gfx_blend_pixel(x, yi,     c, (u32)((1.0f - f) * 255.0f));
            gfx_blend_pixel(x, yi + 1, c, (u32)(f * 255.0f));
            y += grad;
        }
    } else {
        if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
        float grad = (y1 != y0) ? (float)(x1 - x0) / (float)(y1 - y0) : 0.0f;
        float x = (float)x0 + 0.5f;
        for (int y = y0; y <= y1; y++) {
            int xi = (int)x;
            float f = x - (float)xi;
            gfx_blend_pixel(xi,     y, c, (u32)((1.0f - f) * 255.0f));
            gfx_blend_pixel(xi + 1, y, c, (u32)(f * 255.0f));
            x += grad;
        }
    }
}

/* élsímított körvonal (távolság-alapú lefedettség) */
void gfx_aa_circle(int cx, int cy, int r, u32 c) {
    for (int y = -r - 1; y <= r + 1; y++) {
        for (int x = -r - 1; x <= r + 1; x++) {
            float d = (float)isqrt_i(x * x + y * y) - (float)r;
            float a = 0.5f - d;                    /* 1: belül, 0: szélen túl 0.5-gyel */
            if (a <= 0) continue;
            if (a >= 1) gfx_blend_pixel(cx + x, cy + y, c, 255);
            else        gfx_blend_pixel(cx + x, cy + y, c, (u32)(a * 255.0f));
        }
    }
}

/* függőleges színátmenet-kitöltés */
void gfx_gradient_rect(int x, int y, int w, int h, u32 c_top, u32 c_bot) {
    if (h <= 0 || w <= 0) return;
    for (int j = 0; j < h; j++) {
        u32 c = (h > 1) ? gfx_lerp_color(c_top, c_bot, (u32)(j * 255 / (h - 1)))
                        : c_top;
        gfx_fill_rect(x, y + j, w, 1, c);
    }
}

/* lekerekített sarkú kitöltés (r: sarok-sugár) */
void gfx_rounded_fill(int x, int y, int w, int h, int r, u32 c) {
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        int inset = 0;
        if (j < r) {           /* felső sarkok */
            int dy = r - j;
            int chord = isqrt_i(r * r - dy * dy);
            inset = r - chord;
        } else if (j >= h - r) { /* alsó sarkok */
            int dy = r - (h - 1 - j);
            int chord = isqrt_i(r * r - dy * dy);
            inset = r - chord;
        }
        gfx_fill_rect(x + inset, y + j, w - 2 * inset, 1, c);
    }
}

/* lekerekített, félig átlátszó fekete árnyék */
void gfx_shadow_rounded(int x, int y, int w, int h, int r, u32 alpha) {
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        int inset = 0;
        if (j < r) { int dy = r - j; inset = r - isqrt_i(r * r - dy * dy); }
        else if (j >= h - r) { int dy = r - (h - 1 - j); inset = r - isqrt_i(r * r - dy * dy); }
        for (int i = inset; i < w - inset; i++)
            gfx_blend_pixel(x + i, y + j, 0xFF000000u, alpha);
    }
}

void gfx_hline(int x, int y, int len, u32 color) { gfx_fill_rect(x, y, len, 1, color); }
void gfx_vline(int x, int y, int len, u32 color) { gfx_fill_rect(x, y, 1, len, color); }
void gfx_rect(int x, int y, int w, int h, u32 color) {
    gfx_hline(x, y, w, color); gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color); gfx_vline(x + w - 1, y, h, color);
}

/* ---------- font ---------- */
#include "font8x8_basic.h"
#include "font8x8_ext_latin.h"

/* magyar ékezetesek: ő Ő ű Ű kézi bitképek (LSB-bal) */
static const u8 hu_glyphs[4][8] = {
    /* ő (U+0151): o + kettős éles ékezet */
    { 0x44, 0x28, 0x00, 0x38, 0x44, 0x44, 0x38, 0x00 },
    /* Ő (U+0154) */
    { 0x44, 0x28, 0x00, 0x38, 0x44, 0x44, 0x38, 0x00 },
    /* ű (U+0171): u + kettős éles ékezet */
    { 0x44, 0x28, 0x00, 0x44, 0x44, 0x44, 0x38, 0x00 },
    /* Ű (U+0170) */
    { 0x44, 0x28, 0x00, 0x44, 0x44, 0x44, 0x38, 0x00 },
};

/* UTF-8 dekódolás: 1 bájt -> kódpont; 0 ha nem ékezetes folytatás */
static u32 utf8_next(const char **s) {
    const u8 *p = (const u8 *)*s;
    if (*p < 0x80) { (*s)++; return *p; }
    if ((*p & 0xE0) == 0xC0) {           /* 2 bájtos */
        u32 cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
        *s += 2;
        return cp;
    }
    (*s)++;
    return 0xFFFD;
}

static const u8 *glyph_for(u32 cp) {
    static u8 row[8];
    if (cp == 0x0151) return hu_glyphs[0];
    if (cp == 0x0150) return hu_glyphs[1];
    if (cp == 0x0171) return hu_glyphs[2];
    if (cp == 0x0170) return hu_glyphs[3];
    if (cp < 128) { for (int i = 0; i < 8; i++) row[i] = (u8)font8x8_basic[cp][i]; return row; }
    if (cp >= 0xA0 && cp < 0x100) { for (int i = 0; i < 8; i++) row[i] = (u8)font8x8_ext_latin[cp - 0xA0][i]; return row; }
    /* ismeretlen: üres doboz */
    for (int i = 0; i < 8; i++) row[i] = i == 0 || i == 7 ? 0x7E : 0x42;
    return row;
}

void gfx_draw_text(int x, int y, const char *s, u32 fg, u32 bg) {
    int cx = x;
    while (*s) {
        u32 cp = utf8_next(&s);
        const u8 *g = glyph_for(cp);
        for (int ry = 0; ry < 8; ry++) {
            u8 bits = g[ry];
            for (int rx = 0; rx < 8; rx++) {
                if (bits & (1 << rx)) gfx_pixel(cx + rx, y + ry, fg);
                else if (bg != 0xFF000000u) gfx_pixel(cx + rx, y + ry, bg);
            }
        }
        cx += 8;
    }
}

int gfx_text_width(const char *s) {
    int n = 0;
    while (*s) { utf8_next(&s); n++; }
    return n * 8;
}

void gfx_draw_text_scaled(int x, int y, const char *s, u32 fg, u32 scale) {
    int cx = x;
    while (*s) {
        u32 cp = utf8_next(&s);
        const u8 *g = glyph_for(cp);
        for (int ry = 0; ry < 8; ry++) {
            u8 bits = g[ry];
            for (int rx = 0; rx < 8; rx++) {
                u32 c = (bits & (1 << rx)) ? fg : 0xFF000000u;
                if (c == 0xFF000000u) continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        gfx_pixel(cx + rx * scale + sx, y + ry * scale + sy, c);
            }
        }
        cx += 8 * scale;
    }
}

/* ---------- textúrák ---------- */
texture_t *texture_new(int w, int h) {
    texture_t *t = (texture_t *)kmalloc(sizeof(texture_t));
    if (!t) return 0;
    t->w = w; t->h = h;
    t->px = (u32 *)kmalloc(w * h * 4);
    if (!t->px) return 0;
    for (int i = 0; i < w * h; i++) t->px[i] = 0xFF202830;
    return t;
}

void texture_free(texture_t *t) { (void)t; }   /* bump-heap: nem felszabadítunk */

void gfx_blit_texture(texture_t *t, int dx, int dy) {
    for (int y = 0; y < t->h; y++)
        for (int x = 0; x < t->w; x++)
            gfx_pixel(dx + x, dy + y, t->px[y * t->w + x]);
}

/* 3D textúra-rajzolás: 4 vetített sarokpontba affin textúrázás
   vx/vy: 0=bal-felső, 1=jobb-felső, 2=jobb-alsó, 3=bal-alsó */
void gfx_render_texture3d(texture_t *t, float vx[4], float vy[4], float depth) {
    /* trapéz két háromszögként, soronkénti interpolációval */
    int miny = (int)vy[0], maxy = (int)vy[0];
    for (int i = 1; i < 4; i++) { if ((int)vy[i] < miny) miny = (int)vy[i]; if ((int)vy[i] > maxy) maxy = (int)vy[i]; }
    if (miny < 0) miny = 0;
    if (maxy > (int)gfx_height() - 1) maxy = (int)gfx_height() - 1;

    for (int sy = miny; sy <= maxy; sy++) {
        float ty = (float)(sy - vy[0]) / ((vy[3] - vy[0]) != 0 ? (vy[3] - vy[0]) : 1);
        if (ty < 0) ty = 0; if (ty > 1) ty = 1;
        /* bal és jobb él ebben a sorban (trapéz-feltételezés) */
        float xl = vx[0] + (vx[3] - vx[0]) * ty;
        float xr = vx[1] + (vx[2] - vx[1]) * ty;
        int x0 = (int)xl, x1 = (int)xr;
        if (x0 < 0) x0 = 0;
        if (x1 > (int)gfx_width() - 1) x1 = (int)gfx_width() - 1;
        int w = x1 - x0;
        if (w <= 0) continue;
        float du = (float)t->w / (x1 - x0 > 0 ? (float)(x1 - x0) : 1);
        float texy = ty * (t->h - 1);
        int tiy = (int)texy; if (tiy >= t->h) tiy = t->h - 1;
        const u32 *src = t->px + tiy * t->w;
        for (int sx = 0; sx <= w; sx++) {
            int tx = (int)((float)sx * du); if (tx >= t->w) tx = t->w - 1;
            gfx_pixel(x0 + sx, sy, src[tx]);
        }
    }
    (void)depth;
}

/* ui.c számára: glyph és UTF-8 dekódoló publikus változatok */
u32 utf8_decode_at(const char **s) { return utf8_next(s); }
const u8 *glyph_lookup(u32 cp) { return glyph_for(cp); }
