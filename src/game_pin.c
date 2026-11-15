/* game_pin.c — Űrkadét Flipper (Win95 Space Cadet-hommage)
 * Lemez labdával, ütőkkel, bumperekkel, küldetés-jelzőkkel */
#include "kernel.h"

/* asztal koordináták (középre igazítva a felbontáshoz) */
static int PX0 = 340, PX1 = 724;
static int bxs[3] = { 470, 560, 610 };   /* bumper x, pin_enter állítja */
#define PTOP 40
#define PBOT 720

static float bx, by, bvx, bvy;
static int balls = 3, score = 0, ball_in_play = 0, plunger = 0;
static float fl_ang_l = -0.55f, fl_ang_r = 0.55f;     /* ütő szögek */
static int fl_l = 0, fl_r = 0;                        /* lenyomva */
static u32 pnext_phys = 0;
static u32 bmsg_until = 0;
static u32 flash_t[4] = { 0, 0, 0, 0 };               /* bumper villanások */

/* bumperek: (x, y, r, pont) */
static const int bumpers[3][3] = { { 470, 250, 34, 100 }, { 560, 210, 34, 150 }, { 610, 330, 34, 100 } };
/* küldetés-célkák (felvilágító jelzők) */
static const int targets[3][2] = { { 384, 380 }, { 384, 440 }, { 384, 500 } };
static int target_on[3] = { 0, 0, 0 };

static float fsqrt(float v) {
    if (v <= 0) return 0;
    float y = v > 1 ? v : 1;
    for (int i = 0; i < 24; i++) y = (y + v / y) * 0.5f;
    return y;
}

void pin_enter(void) {
    int W = (int)gfx_width();
    PX0 = W / 2 - 192; PX1 = W / 2 + 192;
    bxs[0] = W / 2 - 42; bxs[1] = W / 2 + 48; bxs[2] = W / 2 + 98;
    balls = 3; score = 0; ball_in_play = 0;
    target_on[0] = target_on[1] = target_on[2] = 0;
    bx = (float)(PX1 - 34); by = 690; bvx = 0; bvy = 0;
    pnext_phys = tick_count();
    sound_tune((const u16[]){ 392, 5, 494, 5, 587, 5, 784, 10 }, 4);
}
void pin_exit(void) { sound_stop(); }

static void launch_ball(void) {
    if (ball_in_play || balls <= 0) return;
    balls--;
    ball_in_play = 1;
    bx = (float)(PX1 - 34); by = 690;
    bvx = 0.4f;
    bvy = -(8.5f + plunger * 0.10f);
    plunger = 0;
    sound_beep(300, 6);
}

/* ütőgeometria: forgó vonalszakaszok */
static void flipper_line(int left, float ang, float *x0, float *y0, float *x1, float *y1) {
    float cx = left ? (float)(PX0 + PX1) / 2 - 92 : (float)(PX0 + PX1) / 2 + 92, cy = 668;
    float len = 78;
    float a = left ? 0.55f + ang : -0.55f + ang;    /* alap + felhajtás */
    if (left) a = -0.5f + (-ang) * -1.0f;           /* bal ütő: felfelé nő */
    /* egyszerűbb: bal ütő szöge -0.5 (nyugalmi) -> +0.5 (felhajtva) */
    a = left ? (-0.5f - ang * 0.9f) : (0.5f - ang * 0.9f);
    if (a < -1.2f) a = -1.2f;
    if (a > 1.2f) a = 1.2f;
    *x0 = cx; *y0 = cy;
    *x1 = cx + kosz_cosf(a) * len * (left ? 1 : -1);
    *y1 = cy - kosz_sinf(a) * len;
}

/* pont-célka egyenes szakasz ütközés: labda vs. szegmens, visszapattanás */
static int seg_bounce(float x0, float y0, float x1, float y1, float bsize, float kick) {
    float ex = x1 - x0, ey = y1 - y0;
    float el2 = ex * ex + ey * ey;
    if (el2 < 0.001f) return 0;
    float t = ((bx - x0) * ex + (by - y0) * ey) / el2;
    if (t < 0) t = 0; if (t > 1) t = 1;
    float px = x0 + ex * t, py = y0 + ey * t;
    float dx = bx - px, dy = by - py;
    float d2 = dx * dx + dy * dy;
    if (d2 > bsize * bsize || d2 < 0.0001f) return 0;
    float d = fsqrt(d2);
    float nx = dx / d, ny = dy / d;
    /* visszalökés + visszaverődés */
    bx = px + nx * bsize;
    by = py + ny * bsize;
    float dot = bvx * nx + bvy * ny;
    if (dot < 0) {
        bvx -= 2 * dot * nx;
        bvy -= 2 * dot * ny;
    }
    bvx += nx * kick;
    bvy += ny * kick;
    return 1;
}

static void add_score(int pts) {
    score += pts;
    if (score >= 100000) score = 99999;
}

static void pin_phys(void) {
    if (!ball_in_play) return;
    bvy += 0.16f;                     /* gravitáció */
    /* enyhe súrlódás */
    bvx *= 0.998f;

    /* falak */
    if (bx < PX0 + 9) { bx = (float)(PX0 + 9); bvx = -bvx * 0.85f; sound_beep(520, 2); }
    if (bx > PX1 - 9) { bx = (float)(PX1 - 9); bvx = -bvx * 0.85f; sound_beep(520, 2); }
    if (by < PTOP + 9) { by = (float)(PTOP + 9); bvy = -bvy * 0.85f; }

    /* felső ív: körvonal (középpont 532, 40+180, sugár ~192) */
    {
        float ax = bx - (float)(PX0 + PX1) / 2, ay = by - (PTOP + 180);
        float d = fsqrt(ax * ax + ay * ay);
        if (by < PTOP + 180 && d > 183 - 9 && d < 210) {
            float nx = -ax / d, ny = -ay / d;
            bx = (float)(PX0 + PX1) / 2 + ax / d * (183 - 9);
            by = (float)(PTOP + 180) + ay / d * (183 - 9);
            float dot = bvx * nx + bvy * ny;
            if (dot < 0) { bvx -= 2 * dot * nx; bvy -= 2 * dot * ny; sound_beep(560, 2); }
        }
    }

    /* bumperek */
    for (int i = 0; i < 3; i++) {
        float dx = bx - bxs[i], dy = by - (float)bumpers[i][1];
        float d2 = dx * dx + dy * dy;
        float rr = (float)(bumpers[i][2] + 9);
        if (d2 < rr * rr && d2 > 0.01f) {
            float d = fsqrt(d2);
            float nx = dx / d, ny = dy / d;
            bx = (float)bxs[i] + nx * rr;
            by = (float)bumpers[i][1] + ny * rr;
            float sp = fsqrt(bvx * bvx + bvy * bvy) + 5.2f;
            if (sp > 14) sp = 14;
            bvx = nx * sp;
            bvy = ny * sp;
            add_score(bumpers[i][3]);
            flash_t[i] = tick_count();
            sound_beep(700 + i * 120, 3);
        }
    }

    /* küldetés-célkák (bal oldali kis háromszögek) */
    for (int i = 0; i < 3; i++) {
        if (bx < PX0 + 26 && by > targets[i][1] && by < targets[i][1] + 40) {
            bx = (float)(PX0 + 26);
            if (bvx < 0) bvx = -bvx * 0.8f;
            if (!target_on[i]) {
                target_on[i] = 1;
                add_score(500);
                flash_t[3] = tick_count();
                sound_beep(980, 4);
                if (target_on[0] && target_on[1] && target_on[2]) {
                    add_score(5000);
                    sound_tune((const u16[]){ 660, 5, 880, 5, 1100, 5, 1320, 10 }, 4);
                    target_on[0] = target_on[1] = target_on[2] = 0;   /* küldetés újrakezdés */
                }
            }
        }
    }

    /* ütők */
    {
        float x0, y0, x1, y1;
        flipper_line(1, fl_ang_l, &x0, &y0, &x1, &y1);
        if (seg_bounce(x0, y0, x1, y1, 10, fl_l ? 2.4f : 0)) sound_beep(360, 2);
        flipper_line(0, fl_ang_r, &x0, &y0, &x1, &y1);
        if (seg_bounce(x0, y0, x1, y1, 10, fl_r ? 2.4f : 0)) sound_beep(360, 2);
    }

    /* lefutás */
    if (by > PBOT + 20) {
        ball_in_play = 0;
        if (balls <= 0) sound_tune((const u16[]){ 330, 10, 262, 10, 196, 18 }, 3);
        else sound_beep(180, 10);
    }

    /* sebességkorlát */
    float sp = fsqrt(bvx * bvx + bvy * bvy);
    if (sp > 16) { bvx *= 16 / sp; bvy *= 16 / sp; }
}

void pin_key(u8 sc, u8 rel, u8 ascii) {
    (void)ascii;
    int dn = !rel;
    if (sc == 0x2C || sc == KEY_LEFT)  fl_l = dn;            /* Z / bal nyíl */
    if (sc == 0x2D || sc == KEY_RIGHT) fl_r = dn;            /* X / jobb nyíl */
    if (sc == KEY_DOWN && dn) { if (!ball_in_play) plunger = plunger < 100 ? plunger + 12 : 100; }
    if (sc == KEY_ENTER && dn) launch_ball();
    if (sc == 0x39 && dn) launch_ball();                     /* space */
}

void pin_frame(u32 tick) {
    /* ütők animálása */
    float tgt_l = fl_l ? 0.62f : 0.0f;
    float tgt_r = fl_r ? 0.62f : 0.0f;
    fl_ang_l += (tgt_l - fl_ang_l) * 0.45f;
    fl_ang_r += (tgt_r - fl_ang_r) * 0.45f;

    /* 100 Hz fizika */
    while ((s32)(tick - pnext_phys) >= 0) { pin_phys(); pnext_phys += 1; }
    if (pnext_phys < tick - 200) pnext_phys = tick;

    /* asztal */
    gfx_fill_rect(0, 23, 1024, 725, 0xFF0A0D12);
    gfx_fill_rect(PX0 - 14, PTOP - 14, PX1 - PX0 + 28, PBOT - PTOP + 34, 0xFF131A26);
    gfx_rect(PX0 - 14, PTOP - 14, PX1 - PX0 + 28, PBOT - PTOP + 34, 0xFF2E4A56);
    /* felső ív */
    for (int a = 180; a <= 360; a += 2) {
        float ar = (float)a * 3.14159f / 180.0f;
        gfx_pixel((PX0 + PX1) / 2 + (int)(kosz_cosf(ar) * 183), PTOP + 180 + (int)(kosz_sinf(ar) * 183), 0xFF3E5A68);
    }
    /* küldőcső */
    gfx_fill_rect(PX1 - 8, 620, 14, 108, 0xFF1A2430);
    gfx_rect(PX1 - 8, 620, 14, 108, 0xFF2E4A56);

    /* célcák */
    for (int i = 0; i < 3; i++) {
        u32 c = target_on[i] ? 0xFF50E878 : 0xFF24402E;
        gfx_fill_rect(PX0, targets[i][1], 22, 38, c);
        gfx_rect(PX0, targets[i][1], 22, 38, 0xFF3A5A46);
        char n[2] = { (char)('1' + i), 0 };
        gfx_draw_text(PX0 + 7, targets[i][1] + 15, n, 0xFFF0F0F0, 0x00000000u);
    }
    if ((s32)(tick - flash_t[3]) < 8)
        gfx_draw_text(PX0 - 6, 320, ui_lang() ? "MISSION!" : "KÜLDETÉS!", 0xFF50E878, 0x00000000u);

    /* bumperek (űrhajó-téglatestek) */
    for (int i = 0; i < 3; i++) {
        u32 base = ((s32)(tick - flash_t[i]) < 8) ? 0xFFFFFFFFu : 0xFFB07AE8;
        app_circle(bxs[i], bumpers[i][1], bumpers[i][2], base, 1);
        app_circle(bxs[i], bumpers[i][1], bumpers[i][2] - 8,
                   ((s32)(tick - flash_t[i]) < 8) ? 0xFFE8B24A : 0xFF5A3E8E, 1);
        char p[8];
        ksnprintf(p, sizeof(p), "%d", bumpers[i][3] / 100);
        gfx_draw_text(bxs[i] - 4, bumpers[i][1] - 4, p, 0xFFF0F0F0, 0x00000000u);
    }

    /* ütők */
    {
        float x0, y0, x1, y1;
        flipper_line(1, fl_ang_l, &x0, &y0, &x1, &y1);
        for (int k = 0; k < 10; k++) {
            float t = (float)k / 10.0f;
            app_circle((int)(x0 + (x1 - x0) * t), (int)(y0 + (y1 - y0) * t), 9 - k / 2, 0xFFD8E0E8, 1);
        }
        flipper_line(0, fl_ang_r, &x0, &y0, &x1, &y1);
        for (int k = 0; k < 10; k++) {
            float t = (float)k / 10.0f;
            app_circle((int)(x0 + (x1 - x0) * t), (int)(y0 + (y1 - y0) * t), 9 - k / 2, 0xFFD8E0E8, 1);
        }
    }

    /* dugattyú */
    if (!ball_in_play) {
        int py = 700 - plunger;
        gfx_fill_rect(PX1 - 6, py, 10, 20, 0xFFE8B24A);
        gfx_draw_text(PX0 - 12 - gfx_text_width(ui_lang() ? "HOLD DOWN, THEN SPACE" : "TARTSD LENYOMVA, MAJÓ ENTER"), 640, ui_lang() ? "HOLD DOWN, THEN SPACE" : "TARTSD LENYOMVA, MAJÓ ENTER", 0xFF8FA0AC, 0x00000000u);
    }

    /* labda */
    if (ball_in_play) {
        app_circle((int)bx, (int)by, 9, 0xFFF0F0F0, 1);
        gfx_pixel((int)bx - 3, (int)by - 3, 0xFFA0A8B0);
    }

    /* oldalsó panel: pont, golyók, rekord-jellegű küldetéslista */
    u32 accent = settings_accent_color();
    int sx0 = PX0 - 300; if (sx0 < 8) sx0 = 8;
    gfx_draw_text_scaled(sx0, 60, ui_lang() ? "SPACE" : "ŰRKADÉT", accent, 2);
    gfx_draw_text_scaled(sx0, 90, ui_lang() ? "CADET" : "FLIPPER", accent, 2);
    char b[64];
    ksnprintf(b, sizeof(b), "%s", ui_lang() ? "SCORE" : "PONT");
    gfx_draw_text(sx0, 140, b, 0xFF8FA0AC, 0x00000000u);
    ksnprintf(b, sizeof(b), "%06u", (unsigned)score);
    gfx_draw_text_scaled(sx0, 156, b, 0xFFE8B24A, 2);
    ksnprintf(b, sizeof(b), "%s: %d", ui_lang() ? "BALLS" : "GOLYÓ", balls + (ball_in_play ? 1 : 0));
    gfx_draw_text(sx0, 200, b, 0xFFF0F0F0, 0x00000000u);
    gfx_draw_text(sx0, 240, ui_lang() ? "MISSION TARGETS" : "KÜLDETÉS-CÉLOK", 0xFF8FA0AC, 0x00000000u);
    for (int i = 0; i < 3; i++) {
        const char *m = i == 0 ? (ui_lang() ? "1: launch probe" : "1: szonda kilövés")
                        : i == 1 ? (ui_lang() ? "2: rover drop" : "2: rover ledobás")
                                 : (ui_lang() ? "3: docking" : "3: dokkolás");
        gfx_draw_text(sx0, 258 + i * 16, m, target_on[i] ? 0xFF50E878 : 0xFFF0F0F0, 0x00000000u);
    }
    if (!ball_in_play && balls > 0)
        gfx_draw_text(sx0, 330, ui_lang() ? "SPACE: new ball" : "SPACE: új golyó", 0xFFE8B24A, 0x00000000u);
    if (!ball_in_play && balls <= 0) {
        gfx_draw_text_scaled(((int)gfx_width() - 9 * 24) / 2, 300, ui_lang() ? "GAME OVER" : "JÁTÉK VÉGE", 0xFFE85050, 3);
        ksnprintf(b, sizeof(b), "%s %u", ui_lang() ? "Final" : "Végső pont", (unsigned)score);
        gfx_draw_text(((int)gfx_width() - gfx_text_width(b)) / 2, 360, b, 0xFFF0F0F0, 0x00000000u);
        gfx_draw_text(((int)gfx_width() - gfx_text_width(ui_lang() ? "Space: play again" : "Space: új játék")) / 2, 380, ui_lang() ? "Space: play again" : "Space: új játék", 0xFF8FA0AC, 0x00000000u);
        if (tick % 40 < 20) gfx_draw_text(sx0, 330, ui_lang() ? "SPACE: restart" : "SPACE: újrakezdés", 0xFF50E878, 0x00000000u);
    }

    ksnprintf(b, sizeof(b), "%s",
              ui_lang() ? "Z/left: left flipper  X/right: right  Down: plunger  Space: launch"
                        : "Z/bal: bal ütő  X/job: jobb ütő  Le: dugattyú  Space: indítás");
    app_chrome(ui_lang() ? "Space Cadet Pinball (Win95-hommage)" : "Űrkadét Flipper (Win95-hommage)", b, accent);
}
