/* game_elite.c — Csillagvadász (Elite-hommage): drótváz űrhajós játék
 * Pitch/yaw forgás, 3D drótváz szörnyek, lézerek, radar, hullámok */
#include "kernel.h"

#define EF 420.0f          /* fókusztávolság */
#define ELITE_MAX 10

typedef struct {
    float x, y, z;         /* helyzet a hajó koordinátarendszerében (világbeli) */
    float vx, vy, vz;
    int   hp, alive, kind; /* 0 vadász 1 cirkáló */
    u32   hit_t;
} eship_t;

static float es_yaw, es_pitch, es_roll;
static float es_x, es_y, es_z;          /* játékos pozíció a világban */
static float es_speed = 0.05f;
static eship_t eships[ELITE_MAX];
static int ewave = 1, ekills = 0, escore = 0, ehp = 4, ealive = 0;
static int estate = 0;                  /* 0 játék 1 hullám teljesítve 2 halál */
static u32 ekeys = 0;                   /* 1 pitch fel 2 le 4 yaw bal 8 jobb 16 gyors */
static u32 enext_phys = 0, eshoot_cd = 0, emsguntil = 0, eflash = 0;
static const char *emsg = "";

/* csillagok (irányvektorok, infinite távolság) */
typedef struct { float x, y, z; } vec3_t;
static vec3_t estar[220];

/* lézerek: a játékos előre tüzel; tároljuk indulási pontot és irányt */
#define ELASER_MAX 8
typedef struct { float x, y, z; float dx, dy, dz; u32 born; int on; } elaser_t;
static elaser_t elas[ELASER_MAX];

static void espawn_wave(void) {
    ealive = 0;
    for (int i = 0; i < ELITE_MAX; i++) {
        eships[i].alive = 0;
        if (i < 3 + ewave && i < ELITE_MAX) {
            float a = (float)i * 2.1f + (float)ewave;
            float d = 40.0f + (float)(app_rand() % 60);
            eships[i].x = es_x + kosz_cosf(a) * d;
            eships[i].y = es_y + (float)(app_rand() % 20 - 10);
            eships[i].z = es_z + kosz_sinf(a) * d;
            eships[i].hp = 1 + (ewave > 2 ? 1 : 0);
            eships[i].kind = (i % 4 == 3) ? 1 : 0;
            eships[i].alive = 1;
            eships[i].hit_t = 0;
            ealive++;
        }
    }
}

void elite_enter(void) {
    app_srand(tick_count() | 1);
    es_yaw = es_pitch = es_roll = 0;
    es_x = 0; es_y = 0; es_z = 0;
    ehp = 4; escore = 0; ekills = 0; ewave = 1; estate = 0;
    for (int i = 0; i < 220; i++) {
        float a = (float)(app_rand() % 628) / 100.0f;
        float b = (float)(app_rand() % 628) / 100.0f;
        float cb = kosz_cosf(b);
        estar[i].x = kosz_cosf(a) * cb;
        estar[i].y = kosz_sinf(b);
        estar[i].z = kosz_sinf(a) * cb;
    }
    for (int i = 0; i < ELASER_MAX; i++) elas[i].on = 0;
    espawn_wave();
    enext_phys = tick_count();
    sound_tune((const u16[]){ 330, 6, 392, 6, 494, 6, 659, 12 }, 4);
}
void elite_exit(void) { sound_stop(); }

/* világ -> hajó koordináta (yaw forgás Y körül, pitch X körül) */
static void to_local(float wx, float wy, float wz, float *lx, float *ly, float *lz) {
    float dx = wx - es_x, dy = wy - es_y, dz = wz - es_z;
    /* yaw */
    float x1 =  dx * kosz_cosf(es_yaw) + dz * kosz_sinf(es_yaw);
    float z1 = -dx * kosz_sinf(es_yaw) + dz * kosz_cosf(es_yaw);
    float y1 = dy;
    /* pitch */
    float y2 = y1 * kosz_cosf(es_pitch) - z1 * kosz_sinf(es_pitch);
    float z2 = y1 * kosz_sinf(es_pitch) + z1 * kosz_cosf(es_pitch);
    *lx = x1; *ly = y2; *lz = z2;
}

static void elite_phys(void) {
    if (estate) return;
    if (ekeys & 1) es_pitch += 0.028f;
    if (ekeys & 2) es_pitch -= 0.028f;
    if (ekeys & 4) es_yaw   += 0.030f;
    if (ekeys & 8) es_yaw   -= 0.030f;
    float sp = (ekeys & 16) ? es_speed * 2.4f : es_speed;
    es_x += -kosz_sinf(es_yaw) * kosz_cosf(es_pitch) * sp;
    es_y +=  kosz_sinf(es_pitch) * sp;
    es_z +=  kosz_cosf(es_yaw) * kosz_cosf(es_pitch) * sp;
    if (eshoot_cd > 0) eshoot_cd--;

    /* lézerek */
    for (int i = 0; i < ELASER_MAX; i++) {
        if (!elas[i].on) continue;
        elas[i].x += elas[i].dx; elas[i].y += elas[i].dy; elas[i].z += elas[i].dz;
        /* találat? */
        for (int s = 0; s < ELITE_MAX; s++) {
            if (!eships[s].alive) continue;
            float ddx = eships[s].x - elas[i].x, ddy = eships[s].y - elas[i].y, ddz = eships[s].z - elas[i].z;
            if (ddx * ddx + ddy * ddy + ddz * ddz < 36.0f) {
                eships[s].hp--;
                eships[s].hit_t = tick_count();
                elas[i].on = 0;
                if (eships[s].hp <= 0) {
                    eships[s].alive = 0; ealive--; ekills++;
                    escore += eships[s].kind ? 250 : 100;
                    sound_tune((const u16[]){ 700, 3, 520, 3, 340, 6 }, 3);
                } else sound_beep(400, 3);
                break;
            }
        }
        /* életidő ~2.2 s */
        if ((s32)(tick_count() - elas[i].born) > 220) elas[i].on = 0;
    }

    /* ellenségek: közelednek, néha lőnek */
    for (int i = 0; i < ELITE_MAX; i++) {
        eship_t *s = &eships[i];
        if (!s->alive) continue;
        float dx = es_x - s->x, dy = es_y - s->y, dz = es_z - s->z;
        float mag = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz) + 0.001f;
        float v = s->kind ? 0.035f : 0.06f;
        if (mag > 55.0f) { s->x += dx / mag * v; s->y += dy / mag * v; s->z += dz / mag * v; }
        /* ütközés = sebzés */
        if (mag < 6.0f) {
            ehp--; eflash = tick_count();
            sound_beep(120, 14);
            /* ellenség hátrébb dobása */
            s->x -= dx / mag * 25.0f; s->y -= dy / mag * 25.0f; s->z -= dz / mag * 25.0f;
            if (ehp <= 0) { estate = 2; sound_tune((const u16[]){ 300, 8, 220, 8, 160, 8, 100, 20 }, 4); }
        }
    }
    if (ealive <= 0 && estate == 0) {
        estate = 1;
        escore += 500 + ewave * 100;
        sound_tune((const u16[]){ 523, 7, 659, 7, 784, 7, 1047, 16 }, 4);
    }
}

static void elite_fire(void) {
    if (estate || eshoot_cd > 0) return;
    for (int i = 0; i < ELASER_MAX; i++) {
        if (elas[i].on) continue;
        elas[i].x = es_x; elas[i].y = es_y; elas[i].z = es_z;
        float d = 1.6f;
        elas[i].dx = -kosz_sinf(es_yaw) * kosz_cosf(es_pitch) * d;
        elas[i].dy =  kosz_sinf(es_pitch) * d;
        elas[i].dz =  kosz_cosf(es_yaw) * kosz_cosf(es_pitch) * d;
        elas[i].born = tick_count();
        elas[i].on = 1;
        eshoot_cd = 14;
        sound_beep(1200, 2);
        return;
    }
}

void elite_key(u8 sc, u8 rel, u8 ascii) {
    int dn = !rel;
    switch (sc) {
        case KEY_UP:    if (dn) ekeys |= 1;  else ekeys &= ~1u;  break;
        case KEY_DOWN:  if (dn) ekeys |= 2;  else ekeys &= ~2u;  break;
        case KEY_LEFT:  if (dn) ekeys |= 4;  else ekeys &= ~4u;  break;
        case KEY_RIGHT: if (dn) ekeys |= 8;  else ekeys &= ~8u;  break;
    }
    if (ascii == 'w' || ascii == 'W') { if (dn) ekeys |= 16; else ekeys &= ~16u; }
    if (rel) return;
    if (sc == KEY_ENTER || ascii == ' ') {
        if (estate == 1) { ewave++; estate = 0; espawn_wave(); }
        else if (estate == 2) { elite_enter(); }
        else elite_fire();
        return;
    }
    if (estate == 1 && (ascii == 'n' || ascii == 'N')) { ewave++; estate = 0; espawn_wave(); }
}

/* pont vetítése: 1 ha látszik */
static int eproject(float lx, float ly, float lz, int *sx, int *sy) {
    if (lz < 1.5f) return 0;
    *sx = (int)gfx_width() / 2 + (int)(lx * EF / lz);
    *sy = (int)gfx_height() / 2 - (int)(ly * EF / lz);
    return 1;
}

/* drótváz ellenség-hajó (helyi koordináták) */
static void edraw_ship(float lx, float ly, float lz, eship_t *s, u32 tick) {
    float sc = s->kind ? 1.8f : 1.0f;
    /* 3D csúcsok */
    float vx[8], vy[8], vz[8];
    vx[0] = lx;            vy[0] = ly + 2.5f * sc; vz[0] = lz;                 /* orr fel */
    vx[1] = lx - 3.0f*sc;  vy[1] = ly - 1.0f*sc;  vz[1] = lz - 1.0f*sc;        /* bal szárny */
    vx[2] = lx + 3.0f*sc;  vy[2] = ly - 1.0f*sc;  vz[2] = lz - 1.0f*sc;        /* jobb szárny */
    vx[3] = lx;            vy[3] = ly - 1.4f*sc;  vz[3] = lz - 2.2f*sc;        /* farok */
    vx[4] = lx;            vy[4] = ly + 0.6f*sc;  vz[4] = lz + 1.4f*sc;        /* gyomor */
    int px[5], py[5];
    for (int i = 0; i < 5; i++) if (!eproject(vx[i], vy[i], vz[i], &px[i], &py[i])) return;
    u32 col = (s->kind) ? 0xFFE86A6A : 0xFFB07AE8;
    if ((s32)(tick - s->hit_t) < 8) col = 0xFFFFFFFF;
    app_line(px[0], py[0], px[1], py[1], col);
    app_line(px[0], py[0], px[2], py[2], col);
    app_line(px[1], py[1], px[3], py[3], col);
    app_line(px[2], py[2], px[3], py[3], col);
    app_line(px[1], py[1], px[4], py[4], col);
    app_line(px[2], py[2], px[4], py[4], col);
    app_line(px[0], py[0], px[3], py[3], col);
}

void elite_frame(u32 tick) {
    while ((s32)(tick - enext_phys) >= 0) { elite_phys(); enext_phys += 1; }
    if (enext_phys < tick - 200) enext_phys = tick;

    /* űr + csillagok */
    gfx_fill_rect(0, 23, (int)gfx_width(), (int)gfx_height() - 43, 0xFF02040A);
    for (int i = 0; i < 220; i++) {
        float lx, ly, lz;
        to_local(es_x + estar[i].x * 5000.0f, es_y + estar[i].y * 5000.0f, es_z + estar[i].z * 5000.0f, &lx, &ly, &lz);
        int sx, sy;
        if (!eproject(lx, ly, lz, &sx, &sy)) continue;
        int b = (i % 7 == 0) ? 200 : 120;
        u32 c = 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
        gfx_pixel(sx, sy, c);
    }

    /* ellenségek (távolság szerint: messzebbi előbb) */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < ELITE_MAX; i++) {
            if (!eships[i].alive) continue;
            float lx, ly, lz;
            to_local(eships[i].x, eships[i].y, eships[i].z, &lx, &ly, &lz);
            if (pass == 0 && lz < 40.0f) continue;
            if (pass == 1 && lz >= 40.0f) continue;
            edraw_ship(lx, ly, lz, &eships[i], tick);
        }

    /* lézerek */
    for (int i = 0; i < ELASER_MAX; i++) {
        if (!elas[i].on) continue;
        float lx, ly, lz;
        to_local(elas[i].x, elas[i].y, elas[i].z, &lx, &ly, &lz);
        float l2x, l2y, l2z;
        to_local(elas[i].x - elas[i].dx * 3.0f, elas[i].y - elas[i].dy * 3.0f, elas[i].z - elas[i].dz * 3.0f, &l2x, &l2y, &l2z);
        int x0, y0, x1, y1;
        if (eproject(lx, ly, lz, &x0, &y0) && eproject(l2x, l2y, l2z, &x1, &y1)) {
            app_line(x0, y0, x1, y1, 0xFF50E878);
            app_line(x0 - 1, y0, x1 - 1, y1, 0xFF185A28);
            app_line(x0 + 1, y0, x1 + 1, y1, 0xFF185A28);
        }
    }

    /* célkereszt */
    {
        int ccx = (int)gfx_width() / 2, ccy = (int)gfx_height() / 2;
        app_line(ccx - 8, ccy, ccx + 8, ccy, 0xFF3A5562);
        app_line(ccx, ccy - 8, ccx, ccy + 8, 0xFF3A5562);
        gfx_rect(ccx - 16, ccy - 16, 32, 32, 0xFF22343E);
    }

    /* radar */
    int rx = (int)gfx_width() / 2, ry = (int)gfx_height() - 128, rr = 78;
    gfx_fill_rect(rx - rr - 4, ry - rr - 4, rr * 2 + 8, rr * 2 + 8, 0x90001018u);
    app_circle(rx, ry, rr, 0xFF2E4A56, 0);
    app_circle(rx, ry, rr / 2, 0xFF1E3440, 0);
    app_line(rx - rr, ry, rx + rr, ry, 0xFF1E3440);
    app_line(rx, ry - rr, rx, ry + rr, 0xFF1E3440);
    for (int i = 0; i < ELITE_MAX; i++) {
        if (!eships[i].alive) continue;
        float lx, ly, lz;
        to_local(eships[i].x, eships[i].y, eships[i].z, &lx, &ly, &lz);
        if (lz <= 0.5f) continue;
        float d = (lx < 0 ? -lx : lx) + (lz < 0 ? -lz : lz);
        if (d > 130.0f) continue;
        float px = lx / 130.0f * rr, pz = lz / 130.0f * rr;
        gfx_fill_rect(rx + (int)pz - 1, ry - (int)(ly / 130.0f * rr) - 1, 3, 3, 0xFFE85050);
    }
    /* játékos-jel a középpontban */
    gfx_fill_rect(rx - 1, ry - 1, 3, 3, 0xFF50E878);

    /* találat-villanás */
    if ((s32)(tick - eflash) < 7) {
        gfx_rect(0, 23, (int)gfx_width(), (int)gfx_height() - 43, 0xFFE85050);
        gfx_rect(2, 25, (int)gfx_width() - 4, (int)gfx_height() - 47, 0x80E85050u);
    }

    /* HUD */
    u32 accent = settings_accent_color();
    char b[96];
    if (estate == 1) {
        ksnprintf(b, sizeof(b), "%s %d!  Pont: %u  %s: Enter",
                  ui_lang() ? "WAVE CLEARED" : "HULLÁM TELJESÍTVE", ewave, escore,
                  ui_lang() ? "next" : "következő");
        gfx_draw_text_scaled(((int)gfx_width() - 12 * 24) / 2, 300, ui_lang() ? "WAVE CLEAR!" : "HULLÁM TELJES!", 0xFF50E878, 3);
    } else if (estate == 2) {
        ksnprintf(b, sizeof(b), "%s  Pont: %u  %s: Enter",
                  ui_lang() ? "GAME OVER" : "JÁÉK VÉGE", escore, ui_lang() ? "retry" : "újra");
        gfx_draw_text_scaled(((int)gfx_width() - 9 * 24) / 2, 300, ui_lang() ? "GAME OVER" : "JÁTÉK VÉGE", 0xFFE85050, 3);
    } else {
        ksnprintf(b, sizeof(b), "HP: %d  %s: %u  %s: %d/%d  %s",
                  ehp, ui_lang() ? "Score" : "Pont", escore,
                  ui_lang() ? "Wave" : "Hullám", ewave, ealive,
                  ui_lang() ? "Arrows: steer  W: boost  Space: fire"
                            : "Nyilak: kormányzás  W: gyorsítás  Space: tüzelés");
    }
    app_chrome(ui_lang() ? "Star hunter (Elite-hommage)" : "Csillagvadász (Elite-hommage)", b, accent);
    /* hajótest-páncél */
    gfx_fill_rect(16, (int)gfx_height() - 56, 160, 14, 0x80000000u);
    gfx_fill_rect(16, (int)gfx_height() - 56, ehp * 40, 14, ehp > 1 ? 0xFF50E878 : 0xFFE85050);
    gfx_rect(16, (int)gfx_height() - 56, 160, 14, 0xFFF0F0F0);
}
