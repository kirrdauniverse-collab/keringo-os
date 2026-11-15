/* hatter.c — háttérképek: csillagmező (animált, eredeti), köd, színátmenet, fotó (JPEG/BMP) */
#include "kernel.h"

static u32 *wp_mem = 0;                   /* dinamikus háttér-puffer (fb méret) */
static int wp_w = 0, wp_h = 0;
static int wp_ok = 0;

int wallpaper_active(void) { return wp_ok; }

/* egyszerű pszeudo-véletlen (determinisztikus) */
static u32 rnd_state = 0x1234abcd;
static u32 rnd(void) {
    rnd_state = rnd_state * 1103515245 + 12345;
    return (rnd_state >> 16) & 0x7FFF;
}

static int wp_alloc(int w, int h) {
    if (wp_mem && wp_w == w && wp_h == h) return 1;
    wp_mem = (u32 *)kmalloc((u32)w * h * 4);
    if (!wp_mem) return 0;
    wp_w = w; wp_h = h;
    return 1;
}

static void nebula(void) {
    int W = (int)gfx_width(), H = (int)gfx_height();
    if (!wp_alloc(W, H)) { wp_ok = 0; return; }
    /* néhány sugárzó köd-folt + csillagok */
    struct { int x, y, r; u32 c; } blobs[5] = {
        { 300, 260, 340, 0xFF2A2038 },
        { 720, 300, 380, 0xFF1B2C44 },
        { 520, 560, 300, 0xFF33202E },
        { 180, 620, 260, 0xFF202A3A },
        { 860, 640, 240, 0xFF26203C },
    };
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            u32 c = 0xFF0E1218;
            for (int b = 0; b < 5; b++) {
                int dx = x - blobs[b].x, dy = y - blobs[b].y;
                int d2 = dx * dx + dy * dy;
                int r2 = blobs[b].r * blobs[b].r;
                if (d2 < r2) {
                    /* a folt színéből interpolálunk */
                    u32 mix = (u32)(r2 - d2) * 96u / (u32)r2;
                    if (mix > 48) mix = 48;
                    u32 br = (blobs[b].c >> 16) & 255, bg = (blobs[b].c >> 8) & 255, bb = blobs[b].c & 255;
                    u32 cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
                    cr += ((br - cr) * mix) >> 6;
                    cg += ((bg - cg) * mix) >> 6;
                    cb += ((bb - cb) * mix) >> 6;
                    c = 0xFF000000u | (cr << 16) | (cg << 8) | cb;
                }
            }
            wp_mem[y * W + x] = c;
        }
    }
    /* csillagok */
    for (int i = 0; i < 340; i++) {
        int x = (int)(rnd() % (u32)W), y = (int)(rnd() % (u32)H);
        u32 b = rnd() % 5;
        u32 c = 0xFF708090 + b * 0x00202020;
        if (b > 3) c = 0xFFE8E8F0;
        wp_mem[y * 1024 + x] = c;
        if (b > 2 && x > 0 && y > 0 && x < W - 1 && y < H - 1) {
            wp_mem[y * W + x + 1] = 0xFF505860;
            wp_mem[(y + 1) * W + x] = 0xFF505860;
        }
    }
    wp_ok = 1;
}

static void gradient(void) {
    int W = (int)gfx_width(), H = (int)gfx_height();
    if (!wp_alloc(W, H)) { wp_ok = 0; return; }
    for (int y = 0; y < H; y++) {
        float t = (float)y / (float)(H - 1);
        /* mélykék -> bíbor */
        u32 r = (u32)(14 + 42 * t * t);
        u32 g = (u32)(18 + 16 * t);
        u32 b = (u32)(34 + 60 * (1.0f - t) * t + 30 * t);
        u32 c = 0xFF000000u | (r << 16) | (g << 8) | b;
        for (int x = 0; x < W; x++) wp_mem[y * W + x] = c;
    }
    wp_ok = 1;
}

static void photo(void) {
    static u8 fbuf[512 * 1024];
    u32 got = 0;
    if (!vfs_read_any((const u8 *)settings.wp_file, fbuf, sizeof(fbuf), &got)) {
        kprintf("hatter: %s nem olvashato\n", settings.wp_file);
        wp_ok = 0;
        return;
    }
    u32 *pix; int w, h;
    if (!jpeg_decode(fbuf, got, &pix, &w, &h)) {
        if (!bmp_decode(fbuf, got, &pix, &w, &h)) {
            kprintf("hatter: nem JPEG/BMP\n");
            wp_ok = 0;
            return;
        }
    }
    int W = (int)gfx_width(), H = (int)gfx_height();
    if (!wp_alloc(W, H)) { wp_ok = 0; return; }
    u32 *scaled = img_scale(pix, w, h, W, H);
    if (!scaled) { wp_ok = 0; return; }
    for (int i = 0; i < W * H; i++) wp_mem[i] = scaled[i];
    kprintf("hatter: foto betoltve (%dx%d)\n", w, h);
    wp_ok = 1;
}

void wallpaper_apply(void) {
    switch (settings.wallpaper) {
        case 1: nebula(); break;
        case 2: gradient(); break;
        case 3: photo(); break;
        default: wp_ok = 0; break;      /* 0 = animált csillagmező */
    }
}

void wallpaper_blit(void) {
    extern u32 *gfx_backbuf(void);
    u32 *back = gfx_backbuf();
    if (!back || !wp_ok || !wp_mem) return;
    for (u32 i = 0; i < (u32)wp_w * (u32)wp_h; i++) back[i] = wp_mem[i];
}
