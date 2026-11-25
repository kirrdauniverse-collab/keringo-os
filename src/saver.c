/* saver.c — képernyővédők: csillagmező, plazma, keringés, mátrix eső, pattogó logó
 * + Képernyővédők menü app */
#include "kernel.h"

static int saver_cur = -1;         /* -1: nem fut */
static u32 saver_last = 0;

int saver_count(void) { return 7; }
const char *saver_name(int i, int lang) {
    static const char *n[2][7] = {
        { "Csillagmező", "Plazma", "Keringés", "Mátrix eső", "Pattogó logó", "Meteorok", "Tábortűz" },
        { "Starfield", "Plasma", "Orbits", "Matrix rain", "Bouncing logo", "Meteors", "Campfire" }
    };
    return n[lang & 1][i % 7];
}

/* ---------- 1. csillagmező ---------- */
typedef struct { float x, y, z; } star_t;
static star_t stars[280];

static void starfield_init(void) {
    for (int i = 0; i < 280; i++) {
        stars[i].x = (float)((int)(app_rand() % 2000) - 1000) / 100.0f;
        stars[i].y = (float)((int)(app_rand() % 2000) - 1000) / 100.0f;
        stars[i].z = (float)(app_rand() % 900 + 20);
    }
}
static void starfield_frame(u32 tick) {
    gfx_fill_rect(0, 0, (int)gfx_width(), (int)gfx_height(), 0xFF020308);
    (void)tick;
    int cx = (int)gfx_width() / 2, cy = (int)gfx_height() / 2;
    for (int i = 0; i < 280; i++) {
        stars[i].z -= 1.6f;
        if (stars[i].z < 1.0f) {
            stars[i].x = (float)((int)(app_rand() % 2000) - 1000) / 100.0f;
            stars[i].y = (float)((int)(app_rand() % 2000) - 1000) / 100.0f;
            stars[i].z = 90.0f;
        }
        int sx = cx + (int)(stars[i].x * 380.0f / stars[i].z);
        int sy = cy + (int)(stars[i].y * 380.0f / stars[i].z);
        if (sx < 0 || sy < 0 || sx >= (int)gfx_width() || sy >= (int)gfx_height()) continue;
        int b = (int)(255.0f / stars[i].z * 4.0f);
        if (b > 255) b = 255; if (b < 40) b = 40;
        u32 c = 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
        int sz = stars[i].z < 25 ? 2 : 1;
        gfx_fill_rect(sx, sy, sz, sz, c);
    }
}

/* ---------- 2. plazma ---------- */
static u32 plasma_pal[256];
static u32 plasma_buf[128 * 96];
static void plasma_init(void) {
    for (int i = 0; i < 256; i++) {
        float ph = (float)i / 255.0f * 6.28318f;
        int r = (int)(128.0f + 127.0f * kosz_sinf(ph));
        int g = (int)(128.0f + 127.0f * kosz_sinf(ph + 2.1f));
        int b = (int)(128.0f + 127.0f * kosz_sinf(ph + 4.2f));
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;
        plasma_pal[i] = 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    }
}
static void plasma_frame(u32 tick) {
    float t = (float)tick / 14.0f;
    for (int y = 0; y < 96; y++) {
        for (int x = 0; x < 128; x++) {
            float fx = (float)x / 10.0f, fy = (float)y / 10.0f;
            float v = kosz_sinf(fx * 1.1f + t)
                    + kosz_sinf(fy * 1.3f - t * 0.7f)
                    + kosz_sinf((fx + fy) * 0.8f + t * 0.4f)
                    + kosz_sinf(kosz_cosf(fx * 0.7f + t * 0.3f) * 2.4f + fy);
            int idx = (int)((v + 4.0f) * 31.9f);
            if (idx < 0) idx = 0; if (idx > 255) idx = 255;
            plasma_buf[y * 128 + x] = plasma_pal[idx];
        }
    }
    /* felskálázás 1024x768-ra */
    u32 *bb = gfx_backbuf();
    int W = (int)gfx_width(), H = (int)gfx_height();
    for (int y = 0; y < H; y++) {
        int sy = y * 96 / H;
        const u32 *src = plasma_buf + sy * 128;
        u32 *dst = bb + y * W;
        for (int x = 0; x < W; x++) dst[x] = src[x * 128 / W];
    }
}

/* ---------- 3. keringés ---------- */
typedef struct { float r, a, sp; u32 c; int sz; } orb_t;
static orb_t orbs[6];
static void orbits_init(void) {
    static const u32 cols[6] = { 0xFFE8B24A, 0xFF248BBE, 0xFF62D26F, 0xFFE86A6A, 0xFFB07AE8, 0xFF5AD2C8 };
    for (int i = 0; i < 6; i++) {
        orbs[i].r = 90.0f + i * 58.0f;
        orbs[i].a = (float)i * 1.3f;
        orbs[i].sp = 0.006f - (float)i * 0.0011f;
        orbs[i].c = cols[i];
        orbs[i].sz = 5 + (int)(app_rand() % 6);
    }
}
static void orbits_frame(u32 tick) {
    gfx_fill_rect(0, 0, (int)gfx_width(), (int)gfx_height(), 0xFF04070C);
    int cx = (int)gfx_width() / 2, cy = (int)gfx_height() / 2;
    /* pályák */
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < 90; k++) {
            float a = (float)k / 90.0f * 6.28318f;
            gfx_pixel(cx + (int)(kosz_cosf(a) * orbs[i].r), cy + (int)(kosz_sinf(a) * orbs[i].r * 0.42f), 0xFF1C2A36);
        }
    /* nap */
    app_circle(cx, cy, 26, 0xFFE8B24A, 1);
    app_circle(cx, cy, 32, 0xFF7A5A20, 0);
    /* bolygók + hold */
    for (int i = 0; i < 6; i++) {
        orbs[i].a += orbs[i].sp;
        int px = cx + (int)(kosz_cosf(orbs[i].a) * orbs[i].r);
        int py = cy + (int)(kosz_sinf(orbs[i].a) * orbs[i].r * 0.42f);
        app_circle(px, py, orbs[i].sz, orbs[i].c, 1);
        if (i < 3) {  /* holdacskák */
            float ma = orbs[i].a * 3.0f + (float)i;
            app_circle(px + (int)(kosz_cosf(ma) * (orbs[i].sz + 8)), py + (int)(kosz_sinf(ma) * (orbs[i].sz + 8) * 0.5f), 2, 0xFF8FA0AC, 1);
        }
    }
    /* alkalmi üstökös */
    {
        float ca = (float)tick / 260.0f;
        int kx = cx + (int)(kosz_cosf(ca * 0.7f) * 330.0f);
        int ky = cy + (int)(kosz_sinf(ca * 0.5f) * 200.0f);
        for (int k = 0; k < 24; k++)
            gfx_pixel(kx - k * 3, ky + k * 2 - 24, 0xFFA8C8E8);
        app_circle(kx, ky, 3, 0xFFF0F0F0, 1);
    }
    gfx_draw_text(cx - 5 * 8, cy + 44, "KERINGŐ", 0xFFE8B24A, 0xFF000000u);
}

/* ---------- 4. mátrix eső ---------- */
#define MMAX 176
static int mcols = 128;
static int m_y[MMAX], m_sp[MMAX];
static char m_ch[MMAX][20];
static void matrix_init(void) {
    mcols = (int)gfx_width() / 8;
    if (mcols > MMAX) mcols = MMAX;
    for (int i = 0; i < mcols; i++) {
        m_y[i] = -(int)(app_rand() % 60);
        m_sp[i] = 1 + (int)(app_rand() % 3);
        for (int k = 0; k < 20; k++) m_ch[i][k] = (char)(33 + app_rand() % 90);
    }
}
static void matrix_frame(u32 tick) {
    gfx_fill_rect(0, 0, (int)gfx_width(), (int)gfx_height(), 0xFF000600);
    (void)tick;
    for (int i = 0; i < mcols; i++) {
        int x = i * 8;
        for (int k = 0; k < 20; k++) {
            int yy = (m_y[i] - k) * 10;
            if (yy < 0 || yy >= (int)gfx_height()) continue;
            char c[2] = { m_ch[i][k], 0 };
            int g = 250 - k * 12;
            if (g < 30) g = 30;
            u32 col = k == 0 ? 0xFFD8FFE0 : (0xFF000000u | ((u32)(g / 2) << 16) | ((u32)g << 8) | 0x30u);
            if (k > 0 && app_rand() % 97 == 0) m_ch[i][k] = (char)(33 + app_rand() % 90);
            gfx_draw_text(x, yy, c, col, 0xFF000000u);
        }
        m_y[i] += m_sp[i];
        if (m_y[i] * 10 > (int)gfx_height() + 200) {
            m_y[i] = -(int)(app_rand() % 40);
            for (int k = 0; k < 20; k++) m_ch[i][k] = (char)(33 + app_rand() % 90);
        }
    }
}

/* ---------- 5. pattogó logó ---------- */
static float bx = 100, by = 100, bvx = 3.2f, bvy = 2.3f;
static int bcolor = 0;
static void bounce_init(void) { bx = 120; by = 90; bvx = 3.4f; bvy = 2.4f; }
static void bounce_frame(u32 tick) {
    static u32 pal[6] = { 0xFFE8B24A, 0xFF248BBE, 0xFF62D26F, 0xFFE86A6A, 0xFFB07AE8, 0xFF5AD2C8 };
    (void)tick;
    bx += bvx; by += bvy;
    int W = (int)gfx_width(), H = (int)gfx_height();
    int lw = 8 * 8 * 2, lh = 16;
    if (bx < 0) { bx = 0; bvx = -bvx; bcolor = (bcolor + 1) % 6; sound_beep(600, 2); }
    if (bx + lw > W) { bx = (float)W - lw; bvx = -bvx; bcolor = (bcolor + 1) % 6; sound_beep(600, 2); }
    if (by < 24) { by = 24; bvy = -bvy; bcolor = (bcolor + 1) % 6; sound_beep(700, 2); }
    if (by + lh > H) { by = (float)H - lh; bvy = -bvy; bcolor = (bcolor + 1) % 6; sound_beep(700, 2); }
    gfx_fill_rect(0, 0, W, H, 0xFF07090E);
    gfx_draw_text_scaled((int)bx, (int)by, "KERINGŐ", pal[bcolor], 2);
    gfx_draw_text_scaled((int)bx + 4, (int)by + 20, "0.5", pal[(bcolor + 3) % 6], 1);
}

/* ---------- 6. meteorok (0.5) ---------- */
#define MET_MAX 40
static int met_x[MET_MAX], met_y[MET_MAX], met_v[MET_MAX];
static int met_tail[MET_MAX][10][2];
static void meteors_init(void) {
    for (int i = 0; i < MET_MAX; i++) {
        met_x[i] = (int)(app_rand() % gfx_width());
        met_y[i] = -(int)(app_rand() % 200);
        met_v[i] = 2 + (int)(app_rand() % 6);
        for (int k = 0; k < 10; k++) { met_tail[i][k][0] = met_x[i]; met_tail[i][k][1] = met_y[i]; }
    }
}
static void meteors_frame(u32 tick) {
    (void)tick;
    int W = (int)gfx_width(), H = (int)gfx_height();
    /* éjszakai ég halvány csillagokkal */
    gfx_clear(0xFF060A12);
    for (int y = 0; y < H; y += 2)
        for (int x = 0; x < W; x += 2)
            if (((x * 7 + y * 3) & 63) == 0) gfx_pixel(x, y, 0x33FFFFFF);
    for (int i = 0; i < MET_MAX; i++) {
        met_x[i] += met_v[i];
        met_y[i] += met_v[i] / 2 + 1;
        if (met_y[i] > H || met_x[i] > W) {
            met_x[i] = (int)(app_rand() % gfx_width()) - 200;
            met_y[i] = -(int)(app_rand() % 100);
        }
        for (int k = 9; k > 0; k--) { met_tail[i][k][0] = met_tail[i][k-1][0]; met_tail[i][k][1] = met_tail[i][k-1][1]; }
        met_tail[i][0][0] = met_x[i]; met_tail[i][0][1] = met_y[i];
        for (int k = 0; k < 10; k++) {
            u32 c = k == 0 ? 0xFFFFFFFF : (k < 4 ? 0xFFE8B24A : 0x55D8E0E8);
            gfx_pixel(met_tail[i][k][0], met_tail[i][k][1], c);
            gfx_pixel(met_tail[i][k][0] + 1, met_tail[i][k][1], c);
        }
    }
}

/* ---------- 7. tábortűz (0.5) ---------- */
static void fire_frame(u32 tick) {
    int W = (int)gfx_width(), H = (int)gfx_height();
    static u8 heat[1024];
    if (tick % 2 == 0) {
        for (int x = 0; x < W && x < 1024; x++)
            heat[x] = (u8)(x > W / 2 - 90 && x < W / 2 + 90
                            ? 160 + (app_rand() % 90) : (app_rand() % 20));
    }
    /* minden sor a lelő parazsból halványul felfelé */
    for (int y = 0; y < H - 30; y++) {
        int hr = (H - 30 - y) * 30 / (H - 30);        /* hő-sűrűség sor szerint */
        for (int x = 1; x < W - 1; x++) {
            int h = heat[x] - hr - (int)(app_rand() % 8);
            if (h < 0) h = 0;
            u32 c;
            if (h > 200)      c = 0xFFFFE8B0;
            else if (h > 150) c = 0xFFF0A020;
            else if (h > 100) c = 0xFFE86010;
            else if (h > 60)  c = 0xFFC03010;
            else if (h > 25)  c = 0xFF6E1A0A;
            else              c = 0xFF0D0605;
            gfx_pixel(x, H - 1 - y, c);
        }
    }
}

/* ---------- API ---------- */
void saver_start(int which) {
    if (which < 0) which = (int)(app_rand() % 7);
    saver_cur = which % 7;
    app_srand(tick_count() ^ 0x5A5A0000u);
    switch (saver_cur) {
        case 0: starfield_init(); break;
        case 1: plasma_init(); break;
        case 2: orbits_init(); break;
        case 3: matrix_init(); break;
        case 4: bounce_init(); break;
        case 5: meteors_init(); break;
        case 6: break;                      /* tábortűz: nincs init */
    }
    saver_last = tick_count();
    kprintf("saver: %s elindult\n", saver_name(saver_cur, 0));
}
int saver_running(void) { return saver_cur >= 0; }
void saver_stop(void) {
    if (saver_cur >= 0) sound_stop();
    saver_cur = -1;
}
void saver_frame(u32 tick) {
    if (saver_cur < 0) return;
    saver_last = tick;
    switch (saver_cur) {
        case 0: starfield_frame(tick); break;
        case 1: plasma_frame(tick); break;
        case 2: orbits_frame(tick); break;
        case 3: matrix_frame(tick); break;
        case 4: bounce_frame(tick); break;
        case 5: meteors_frame(tick); break;
        case 6: fire_frame(tick); break;
    }
}

/* ---------- Képernyővédők menü app ---------- */
static int sm_sel = 0;

int savermenu_esc(void) { return saver_running(); }

void savermenu_enter(void) {
    sm_sel = 0;
    sound_beep(660, 5);
}
void savermenu_key(u8 sc, u8 rel, u8 ascii) {
    (void)ascii;
    if (rel) return;
    if (saver_running()) { saver_stop(); return; }      /* esc lenyomásra is ez fut le előbb */
    if (sc == KEY_UP)   { if (sm_sel > 0) sm_sel--; }
    if (sc == KEY_DOWN) { if (sm_sel < saver_count()) sm_sel++; }
    if (sc == KEY_ENTER) {
        if (sm_sel < saver_count()) saver_start(sm_sel);
        else saver_start(-1);
    }
    if (sc == KEY_F2) {           /* azonnali teszt: véletlen */
        saver_start(-1);
    }
}
void savermenu_frame(u32 tick) {
    u32 accent = settings_accent_color();
    if (saver_running()) { saver_frame(tick); return; }
    gfx_clear(0xFF0D131A);
    gfx_draw_text_scaled(360, 60, ui_lang() ? "SCREENSAVERS" : "KÉPERNYŐVÉDŐK", accent, 2);
    int y = 170;
    for (int i = 0; i <= saver_count(); i++) {
        const char *n = (i == saver_count())
            ? (ui_lang() ? "Random" : "Véletlen")
            : saver_name(i, ui_lang());
        app_button(330, y, 360, 34, n, accent, i == sm_sel);
        y += 42;
    }
    app_chrome(ui_lang() ? "Screensavers" : "Képernyővédők",
               ui_lang() ? "Up/Down: select  Enter: start  (idle 90 s starts one on the desktop too)"
                         : "Fel/Le: választás  Enter: indítás  (90 s tétlenség után az asztalon is indul)",
               accent);
}
