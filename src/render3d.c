/* render3d.c — szoftveres 3D: kamera, vetítés, csillagtér, pályagyűrűk, mag */
#include "kernel.h"

/* kamera */
float cam_yaw = 0.95f;       /* vízszintes fordulat (radián) */
float cam_pitch = 0.50f;     /* függőleges dőlésszög */
float cam_dist = 1250.0f;    /* távolság a magtól */
float cam_target_dist = 1250.0f;

#define FOCAL 900.0f

static float stars[STARS_MAX][3];
static int   star_n = 0;

void r3d_init(void) {
    /* pszeudo-véletlen csillagok gömbhéjon */
    u32 seed = 12345;
    star_n = STARS_MAX;
    for (int i = 0; i < star_n; i++) {
        seed = seed * 1103515245 + 12345;
        float a = (float)((seed >> 8) & 0xFFFF) / 65536.0f * 6.2832f;
        seed = seed * 1103515245 + 12345;
        float b = ((float)((seed >> 8) & 0xFFFF) / 65536.0f - 0.5f) * 1.2f;
        float r = 1500.0f + (float)(i % 7) * 260.0f;
        stars[i][0] = r * kosz_cosf(a) * kosz_cosf(b);
        stars[i][1] = r * kosz_sinf(b) * 0.7f;
        stars[i][2] = r * kosz_sinf(a) * kosz_cosf(b);
    }
    kprintf("r3d: %d csillag\n", star_n);
}

/* világpont -> képernyő; visszatér: sx, sy, depth (0 = nem látszik) */
static int project(float x, float y, float z, float *sx, float *sy, float *depth) {
    /* kamera-forgatás: yaw (Y tengely), pitch (X tengely) */
    float cy = kosz_cosf(cam_yaw), ky = kosz_sinf(cam_yaw);
    float cx = kosz_cosf(cam_pitch), kx = kosz_sinf(cam_pitch);
    float x1 = x * cy + z * ky;
    float z1 = -x * ky + z * cy;
    float y1 = y * cx - z1 * kx;
    float z2 = y * kx + z1 * cx;
    float zc = z2 + cam_dist;
    if (zc < 60.0f) return 0;
    *sx = gfx_width() * 0.5f + (x1 / zc) * FOCAL;
    *sy = gfx_height() * 0.5f - (y1 / zc) * FOCAL;
    *depth = zc;
    return 1;
}

void r3d_camera_update(void) {   /* sima zoom-átmenet */
    cam_dist += (cam_target_dist - cam_dist) * 0.15f;
}

void r3d_draw_background(u32 tick) {
    /* csillagtér */
    for (int i = 0; i < star_n; i++) {
        float sx, sy, d;
        if (!project(stars[i][0], stars[i][1], stars[i][2], &sx, &sy, &d)) continue;
        u8 b = (u8)(60 + 90 / (d / 400.0f));
        if (b > 220) b = 220;
        u32 c = 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
        gfx_pixel((int)sx, (int)sy, c);
        if (i % 5 == 0) { gfx_pixel((int)sx + 1, (int)sy, c); gfx_pixel((int)sx, (int)sy + 1, c); }
    }
}

void r3d_draw_orbit_ring(float radius, float y, u32 color, u32 tick) {
    /* pályagyűrű: 72 mintapontból körszakaszok */
    float prevx = 0, prevy = 0, prevd = 0; int prev_ok = 0;
    for (int i = 0; i <= 72; i++) {
        float a = (float)i * 6.2832f / 72.0f;
        float sx, sy, d;
        if (!project(radius * kosz_cosf(a), y, radius * kosz_sinf(a), &sx, &sy, &d)) { prev_ok = 0; continue; }
        if (prev_ok) {
            /* szakasz rajzolás léptetve */
            int steps = (int)(d < 500 ? 2 : 4);
            for (int k = 0; k <= steps; k++) {
                float t = (float)k / steps;
                int px = (int)(prevx + (sx - prevx) * t);
                int py = (int)(prevy + (sy - prevy) * t);
                /* távolság szerinti halványítás */
                u8 r = (u8)((color >> 16) & 0xFF), g = (u8)((color >> 8) & 0xFF), b = (u8)(color & 0xFF);
                u8 dim = (u8)(255 - (d / 24));
                r = (u8)(r * dim / 255); g = (u8)(g * dim / 255); b = (u8)(b * dim / 255);
                gfx_pixel(px, py, 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | b);
            }
        }
        prevx = sx; prevy = sy; prev_ok = 1;
    }
    (void)tick;
}

void r3d_draw_core(u32 tick) {
    /* a mag (nap) pulzáló korongja + korona */
    float sx, sy, d;
    if (!project(0, 0, 0, &sx, &sy, &d)) return;
    float pulse = 1.0f + 0.08f * kosz_sinf((float)tick / 18.0f);
    int r = (int)(38.0f * FOCAL / d * pulse);
    if (r < 6) r = 6;
    for (int yy = -r; yy <= r; yy++) {
        for (int xx = -r; xx <= r; xx++) {
            int dist2 = xx * xx + yy * yy;
            if (dist2 <= r * r) {
                u8 k = (u8)(255 - (dist2 * 90) / (r * r));
                u32 c = 0xFF000000u | ((u32)k << 16) | ((u32)(k * 3 / 4) << 8) | (u32)(k / 2);
                gfx_pixel((int)sx + xx, (int)sy + yy, c);
            } else if (dist2 <= r * r * 2) {   /* korona */
                u8 k = (u8)(90 - (dist2 * 70) / (r * r * 2));
                u32 c = 0xFF000000u | ((u32)k << 16) | ((u32)(k * 2 / 3) << 8) | (u32)(k / 2);
                gfx_pixel((int)sx + xx, (int)sy + yy, c);
            }
        }
    }
}

/* vetítés kifelé a UI-nak: egy 3D pont képernyő-koordinátái */
int r3d_project_point(float x, float y, float z, float *sx, float *sy, float *depth) {
    return project(x, y, z, sx, sy, depth);
}

float r3d_focal(void) { return FOCAL; }
