/* gfx.c — framebuffer, dupla puffer, primitívek, text, textúra, 3D textúra-rajzolás */
#include "kernel.h"

static u32  fb_w = 0, fb_h = 0, fb_pitch = 0;
static u32 *fb_lin = 0;         /* valódi framebuffer (0x80000000) */
static u32 *back = 0;           /* dupla puffer */
static u32  clr_fg = 0xFFF0F0F0, clr_bg = 0xFF1A2126;

void gfx_init(multiboot_info_t *mbi) {
    fb_w = mbi->framebuffer_width;
    fb_h = mbi->framebuffer_height;
    fb_pitch = mbi->framebuffer_pitch;
    fb_lin = (u32 *)mem_fb_vaddr();
    back = (u32 *)kmalloc(fb_w * fb_h * 4);
    for (u32 i = 0; i < fb_w * fb_h; i++) back[i] = 0xFF10151A;
    kprintf("gfx: %dx%d pitch=%d buffer=%x\n", fb_w, fb_h, fb_pitch, (u32)back);
}

u32 *gfx_backbuf(void) { return back; }
u32 gfx_width(void)  { return fb_w; }
u32 gfx_height(void) { return fb_h; }

void gfx_flip(void) {
    if (!fb_lin || !back) return;
    u32 *dst = fb_lin;
    u32 *src = back;
    for (u32 y = 0; y < fb_h; y++) {
        for (u32 x = 0; x < fb_w; x++) dst[y * (fb_pitch / 4) + x] = src[y * fb_w + x];
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
