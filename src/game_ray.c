/* game_ray.c — Labirintus 3D (Wolfenstein 3D-hommage) + Pokoljáró (Doom-hommage)
 * Raycaster: textúrázott falak, sprite-billboardok, fegyver, szörnyek, minimap */
#include "kernel.h"

#define RMAPW 24
#define RMAPH 24
#define RAYW_MAX 1024         /* max sugarak (2 px oszloponként) */
#define PROJ  580.0f
static int rayw = 512;        /* sugarak száma = kepzeles szelsege/2 */
static int g_vmid = 385;      /* 3D nézet függőleges közepe */

static int rmode = 0;         /* 0: labirintus, 1: pokoljáró */
static u8 rmap[RMAPH][RMAPW];
static float rpx, rpy, rang;
static int rhp = 100, rammo = 40, rkills = 0, rmon_n = 0;
static int rstate = 0;        /* 0 játék, 1 győzelem, 2 halál */
static int rminimap = 1, rshift = 0;
static u32 rstart_tick = 0, rflash = 0, rhitflash = 0, rmsguntil = 0;
static float rbob = 0;
static const char *rmsg = "";
static u32 rnext_phys = 0;
static u32 ray_keys = 0;      /* 1 előre 2 hátra 4 balra 8 jobbra */

static u32 tex[6][64 * 64];   /* 0 tégla 1 kő 2 ajtó 3 pokolkő 4 fém 5 portál */
static u32 zbuf[RAYW_MAX];
static u32 ceil_row[832], floor_row[832];

/* szörnyek */
#define RMON_MAX 16
typedef struct { float x, y; int hp, alive, anim; float atkcd; u32 animt; } rmon_t;
static rmon_t rmons[RMON_MAX];
static u32 msp[4][32 * 32];   /* 0/1 séta, 2 támadás, 3 holt */

/* ---------- pályák ---------- */
static const char *MAP_LAB[RMAPH] = {
    "########################",
    "#P....#.......#....#..E#",
    "#.###.#.#####.#.##.#.#.#",
    "#.#...#.....#...#....#.#",
    "#.#.#######.###.#.####.#",
    "#...#.....#...#.#....#.#",
    "###.#####.###.#.#.####.#",
    "#...#...#...#...#....#.#",
    "#.###.#.#####.#####.##.#",
    "#.#...#.....#.....#..#.#",
    "#.#.#######.#####.##.#.#",
    "#.#.#.....#...#...#..#.#",
    "#.#.#.###.###.#.####.#.#",
    "#...#.#.#...#.#....#.#.#",
    "#####.#.#.#.#.####.#.#.#",
    "#.....#...#.#....#...#.#",
    "#.#########.####.###.#.#",
    "#.#.....#.....#..#...#.#",
    "#.#.###.#####.#.####..##",
    "#.#.#.#.#...#.#....#...#",
    "#.#.#.#.#.#.####.##.####",
    "#...#...#.#....#..#....#",
    "#########.####.#.####.##",
    "########################",
};
static const char *MAP_DOOM[RMAPH] = {
    "########################",
    "#P...#........#....M...#",
    "#.#..#.######.#.####.###",
    "#.#..#.#....#.#.#......#",
    "#.#..#.#.##.#.#.#.####H#",
    "#.##.#.#.M.#...#.#...#H#",
    "#..#.#.####.###.#.#.M.#H",
    "##.#.#......#...#.#...#H",
    "#..#.######.#.#.#.###.##",
    "#.##......#.#.#.#.#M#..#",
    "#.#####H#.#.#...#.#.##.#",
    "#.#...#H#.#.#####.#..#.#",
    "#.#.M.#H#.#.#...#.##.#.#",
    "#.#...#H#...#.M.#..#.#.#",
    "#.#####H#####.####.#.#.#",
    "#.....#.......#..#.#...#",
    "#####H#.#####.#.##.####.",
    "#...#H#.#...#.#..#....E#",
    "#.#.#H#.#.#.#.##.#####.#",
    "#.#....#.#.#...#.#...#.#",
    "#.######.#.#####.#.#.#.#",
    "#........#.....#...#...#",
    "####################HH##",
    "########################",
};

static char cell_at(float x, float y) {
    int mx = (int)x, my = (int)y;
    if (mx < 0 || my < 0 || mx >= RMAPW || my >= RMAPH) return '#';
    return (char)rmap[my][mx];
}
static int is_wall(char c) {
    return c == '#' || c == 'S' || c == 'D' || c == 'H' || c == 'E';
}
static int walkable(float x, float y) {
    /* négy sarok */
    if (is_wall(cell_at(x - 0.22f, y - 0.22f))) return 0;
    if (is_wall(cell_at(x + 0.22f, y - 0.22f))) return 0;
    if (is_wall(cell_at(x - 0.22f, y + 0.22f))) return 0;
    if (is_wall(cell_at(x + 0.22f, y + 0.22f))) return 0;
    return 1;
}

/* ---------- textúragenerálás ---------- */
static u32 thash(u32 x) {
    x += x << 10; x ^= x >> 6; x += x << 3; x ^= x >> 11; x += x << 15;
    return x;
}
static u32 trgb(int r, int g, int b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

static void texgen(void) {
    /* 0: tégla */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            u32 n = thash((u32)(x * 7 + y * 131)) & 15;
            int row = y / 16, off = (row & 1) ? 16 : 0;
            int mortar = (y % 16 < 2) || (((x + off) % 32) < 2);
            int base = 150 + (int)n * 3;
            if (mortar) base = 70;
            tex[0][y * 64 + x] = trgb(base + 30, base / 2 + 15, base / 3);
        }
    /* 1: kő */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            u32 n = thash((u32)(x * 31 + y * 17)) & 31;
            int b = 100 + (int)n * 2 + ((x * y) % 13);
            tex[1][y * 64 + x] = trgb(b, b + 6, b + 14);
        }
    /* 2: ajtó */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int plank = (x % 21 < 3) || (y < 3) || (y > 60);
            int b = plank ? 55 : 105 + (int)(thash((u32)(x + y * 5)) & 7);
            if (x > 44 && x < 52 && y > 30 && y < 38) b = 220;   /* kilincs */
            tex[2][y * 64 + x] = trgb(b, b * 3 / 4, b / 2);
        }
    /* 3: pokolkő */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            u32 n = thash((u32)(x * 13 + y * 29)) & 31;
            int crack = ((x + y * 2) % 23 < 2) || ((x - y) % 31 < 2);
            int b = 70 + (int)n * 2 - (crack ? 40 : 0);
            tex[3][y * 64 + x] = trgb(b + 45, b / 3, b / 4);
        }
    /* 4: fém */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int band = (y % 16 < 2) ? -35 : 0;
            int rivet = ((x % 16 == 8) && (y % 16 == 8)) ? 60 : 0;
            int b = 90 + band + rivet + (int)(thash((u32)(x * 3 + y)) & 7);
            tex[4][y * 64 + x] = trgb(b - 15, b, b + 25);
        }
    /* 5: portál (kijárat) */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int dx = x - 32, dy = y - 32;
            int d = (dx * dx + dy * dy) / 8;
            int ring = (d % 12 < 3);
            if (d > 240) {
                int b = 40 + (int)(thash((u32)(x + y * 3)) & 7);
                tex[5][y * 64 + x] = trgb(b, b + 5, b + 10);
            } else if (ring) tex[5][y * 64 + x] = trgb(90, 255, 140);
            else tex[5][y * 64 + x] = trgb(10, 60, 30);
        }
}

static void spritegen(void) {
    for (int f = 0; f < 4; f++)
        for (int i = 0; i < 32 * 32; i++) msp[f][i] = 0;
    for (int f = 0; f < 3; f++) {
        int dy = (f == 1) ? 1 : 0;                  /* lépés: kissé lejjebb */
        int atk = (f == 2);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                u32 c = 0;
                int head = (x - 16) * (x - 16) + (y - 7) * (y - 7) < 30;
                int body = (x - 16) * (x - 16) / 2 + (y - 21 + dy) * (y - 21 + dy) < 55;
                int hornL = (x >= 10 && x <= 12 && y >= 0 && y <= 4) || (x >= 8 && x <= 10 && y >= 3 && y <= 6);
                int hornR = (x >= 19 && x <= 21 && y >= 0 && y <= 4) || (x >= 21 && x <= 23 && y >= 3 && y <= 6);
                int legL = (x >= 12 && x <= 14 && y >= 26 && y <= 31);
                int legR = (x >= 17 && x <= 19 && y >= 26 && y <= 31);
                int armL = !atk && (x >= 5 && x <= 7 && y >= 15 && y <= 24);
                int armR = !atk && (x >= 24 && x <= 26 && y >= 15 && y <= 24);
                int armUpL = atk && (x >= 4 && x <= 7 && y >= 8 && y <= 16);
                int armUpR = atk && (x >= 24 && x <= 27 && y >= 8 && y <= 16);
                int eyeL = (x >= 13 && x <= 14 && y >= 6 && y <= 7);
                int eyeR = (x >= 17 && x <= 18 && y >= 6 && y <= 7);
                int mouth = atk && (x >= 13 && x <= 18 && y >= 10 && y <= 13);
                if (head || body) c = trgb(170 + (x % 3) * 8, 34, 30);
                if (hornL || hornR) c = trgb(235, 220, 170);
                if (legL || legR) c = trgb(120, 24, 22);
                if (armL || armR || armUpL || armUpR) c = trgb(150, 30, 28);
                if (eyeL || eyeR) c = 0xFFFFFF00u;
                if (mouth) c = 0xFF180008u;
                if (c) msp[f][y * 32 + x] = c;
            }
    }
    /* holt (splat) */
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int dx = x - 16, dy = y - 26;
            if (dx * dx + dy * dy * 6 < 200) msp[3][y * 32 + x] = trgb(110, 18, 16);
            if ((x >= 6 && x <= 9 && y >= 24 && y <= 27) || (x >= 23 && x <= 26 && y >= 25 && y <= 28))
                msp[3][y * 32 + x] = trgb(235, 220, 170);   /* szarvak maradványa */
        }
}

/* ---------- pálya betöltése ---------- */
static void ray_reset(void) {
    const char **src = rmode ? MAP_DOOM : MAP_LAB;
    rmon_n = 0;
    for (int y = 0; y < RMAPH; y++) {
        for (int x = 0; x < RMAPW; x++) {
            char c = src[y][x];
            rmap[y][x] = (u8)c;
            if (c == 'P') { rpx = x + 0.5f; rpy = y + 0.5f; rmap[y][x] = '.'; }
            if (c == 'M' && rmode) {
                if (rmon_n < RMON_MAX) {
                    rmons[rmon_n].x = x + 0.5f;
                    rmons[rmon_n].y = y + 0.5f;
                    rmons[rmon_n].hp = rmode == 1 ? 3 : 2;
                    rmons[rmon_n].alive = 1;
                    rmons[rmon_n].anim = 0;
                    rmons[rmon_n].atkcd = 0;
                    rmons[rmon_n].animt = 0;
                    rmon_n++;
                }
                rmap[y][x] = '.';
            }
        }
    }
    rang = 0.0f;
    rhp = 100; rammo = rmode ? 40 : 0; rkills = 0;
    rstate = 0; rflash = 0; rhitflash = 0;
    rstart_tick = tick_count();
    rnext_phys = tick_count();
}

/* mennyezet/padló sorok */
static void gradgen(void) {
    int H = (int)gfx_height();
    if (H > 832) H = 832;
    for (int y = 0; y < H; y++) {
        if (y < g_vmid) {
            float t = (float)y / (float)g_vmid;
            if (rmode) ceil_row[y] = trgb(38 - (int)(t * 26), 8, 6);
            else       ceil_row[y] = trgb(14 + (int)(t * 4), 20 - (int)(t * 10), 30 - (int)(t * 16));
        } else {
            float t = (float)(y - g_vmid) / (float)(H - g_vmid);
            if (rmode) floor_row[y] = trgb(60 - (int)(t * 34), 20 - (int)(t * 12), 14 - (int)(t * 8));
            else       floor_row[y] = trgb(58 - (int)(t * 30), 46 - (int)(t * 24), 34 - (int)(t * 18));
        }
    }
}

void ray_enter_lab(void)  { g_vmid = (23 + (int)gfx_height() - 21) / 2; rmode = 0; texgen(); spritegen(); gradgen(); ray_reset(); sound_tune((const u16[]){ 392, 6, 494, 6, 587, 10 }, 3); }
void ray_enter_doom(void) { g_vmid = (23 + (int)gfx_height() - 21) / 2; rmode = 1; texgen(); spritegen(); gradgen(); ray_reset(); sound_tune((const u16[]){ 220, 8, 185, 8, 147, 14 }, 3); }
void ray_exit(void) { sound_stop(); }

/* ---------- lövés ---------- */
static void try_exit(void) {
    if (rmode && rkills < rmon_n) {
        rmsg = "ZÁRT! elobb meg kell olnod mind!";
        rmsguntil = tick_count() + 180;
        sound_beep(140, 12);
        return;
    }
    rstate = 1;
    sound_tune((const u16[]){ 523, 7, 659, 7, 784, 7, 1047, 16 }, 4);
}

static void shoot(void) {
    if (rstate) { ray_reset(); return; }
    if (rammo <= 0) { sound_beep(160, 8); rmsg = "NINCS LŐSZER!"; rmsguntil = tick_count() + 100; return; }
    rammo--;
    rflash = tick_count();
    sound_tune((const u16[]){ 900, 3, 620, 3, 380, 5 }, 3);
    float dc = kosz_cosf(rang), ds = kosz_sinf(rang);
    int best = -1; float bestd = 1e9f;
    for (int i = 0; i < rmon_n; i++) {
        if (!rmons[i].alive) continue;
        float ddx = rmons[i].x - rpx, ddy = rmons[i].y - rpy;
        float d2 = ddx * ddx + ddy * ddy;
        if (d2 > 120.0f || d2 < 0.01f) continue;
        float cross = ddx * ds - ddy * dc;           /* oldalirányú eltérés */
        if (cross * cross > 0.0100f * d2) continue;   /* ~5.7 fok kúpszög */
        /* látótér: falak közt legyen szabad */
        float dist = 0.2f;
        int blocked = 0;
        while (dist * dist < d2) {
            if (is_wall(cell_at(rpx + dc * dist, rpy + ds * dist))) { blocked = 1; break; }
            dist += 0.2f;
        }
        if (blocked) continue;
        if (d2 < bestd) { bestd = d2; best = i; }
    }
    if (best >= 0) {
        rmons[best].hp--;
        rmons[best].anim = 2; rmons[best].animt = tick_count();
        if (rmons[best].hp <= 0) {
            rmons[best].alive = 0;
            rkills++;
            rammo += 4;
            sound_tune((const u16[]){ 500, 4, 340, 4, 210, 7 }, 3);
        } else sound_beep(300, 4);
    }
}

/* ---------- fizika (100 Hz) ---------- */
static void ray_phys(void) {
    if (rstate) return;
    float sp = rshift ? 0.05f : 0.032f;
    float dc = kosz_cosf(rang), ds = kosz_sinf(rang);
    int fwd = 0, strafe = 0;
    if (ray_keys & 1) fwd += 1;
    if (ray_keys & 2) fwd -= 1;
    if (ray_keys & 4) strafe -= 1;
    if (ray_keys & 8) strafe += 1;
    float nx = rpx + (dc * fwd - ds * strafe) * sp;
    float ny = rpy + (ds * fwd + dc * strafe) * sp;
    if (fwd || strafe) {
        /* kijárat-érintés? */
        char ca = cell_at(nx, rpy), cb = cell_at(rpx, ny);
        if (ca == 'E' || cb == 'E') { try_exit(); return; }
    }
    if (walkable(nx, rpy)) rpx = nx;
    if (walkable(rpx, ny)) rpy = ny;
    if (fwd || strafe) rbob += 0.35f;

    /* szörnyek */
    if (rmode) {
        for (int i = 0; i < rmon_n; i++) {
            rmon_t *m = &rmons[i];
            if (!m->alive) continue;
            if (m->anim == 2 && (s32)(tick_count() - m->animt) > 25) m->anim = 0;
            if (m->atkcd > 0) { m->atkcd -= 0.01f; }
            float ddx = rpx - m->x, ddy = rpy - m->y;
            float d2 = ddx * ddx + ddy * ddy;
            if (d2 < 0.75f) {
                if (m->atkcd <= 0) {
                    rhp -= 6 + (int)(app_rand() % 8);
                    m->atkcd = 0.9f;
                    m->anim = 2; m->animt = tick_count();
                    rhitflash = tick_count();
                    sound_beep(150, 10);
                    if (rhp <= 0) {
                        rstate = 2;
                        sound_tune((const u16[]){ 300, 8, 220, 8, 160, 8, 100, 18 }, 4);
                    }
                }
            } else if (d2 < 90.0f) {
                float inv = 0.011f / 1.0f;         /* ~1.1 cella/s (maradek, nem hasznalt) */
                (void)inv;
                float mag = 0.0f;
                /* normalizálás négyzetgyök nélkül: fix lépés a irányban */
                mag = (ddx > 0 ? ddx : -ddx) + (ddy > 0 ? ddy : -ddy);
                if (mag > 0.001f) {
                    float sx = ddx / mag * 0.014f;
                    float sy = ddy / mag * 0.014f;
                    char want = cell_at(m->x + sx, m->y + sy);
                    if (!is_wall(want) || want == 'E') { m->x += sx; m->y += sy; }
                    else {
                        if (!is_wall(cell_at(m->x + sx, m->y))) m->x += sx;
                        else if (!is_wall(cell_at(m->x, m->y + sy))) m->y += sy;
                    }
                }
            }
        }
    }
}

void ray_key(u8 sc, u8 rel, u8 ascii) {
    int dn = !rel;
    switch (sc) {
        case KEY_UP:    if (dn) ray_keys |= 1; else ray_keys &= ~1; break;
        case KEY_DOWN:  if (dn) ray_keys |= 2; else ray_keys &= ~2; break;
        case KEY_LEFT:  if (!rel) rang -= 0.09f; break;
        case KEY_RIGHT: if (!rel) rang += 0.09f; break;
        case 0x2A: case 0x36: rshift = dn; break;         /* shift */
        default: break;
    }
    if (rel) {
        /* WASD felengedeskor is tuntetjuk a biteket */
        if (ascii == 'w' || ascii == 'W') ray_keys &= ~1u;
        if (ascii == 's' || ascii == 'S') ray_keys &= ~2u;
        if (ascii == 'a' || ascii == 'A') ray_keys &= ~4u;
        if (ascii == 'd' || ascii == 'D') ray_keys &= ~8u;
        return;
    }
    if (ascii == 'w' || ascii == 'W') { ray_keys |= 1; }
    if (ascii == 's' || ascii == 'S') { ray_keys |= 2; }
    if (ascii == 'a' || ascii == 'A') { ray_keys |= 4; }
    if (ascii == 'd' || ascii == 'D') { ray_keys |= 8; }
    if (sc == KEY_ENTER || ascii == ' ') {
        if (rmode) shoot();
        else if (rstate) ray_reset();
    }
    if (rstate && (ascii == 'r' || ascii == 'R' || ascii == ' ')) { ray_reset(); return; }
    if (ascii == 'm' || ascii == 'M') rminimap ^= 1;
    if (ascii == 'r' || ascii == 'R') ray_reset();
}
void ray_mouse(int mx, int my, int dx, int dy, u8 btn, u8 prev) {
    (void)mx; (void)my; (void)btn; (void)prev;
    rang += dx * 0.0035f;
    (void)dy;
}

/* ---------- render ---------- */
static u32 shade(u32 c, int f) {
    if (f > 256) f = 256;
    int r = (int)((c >> 16 & 255) * f) >> 8;
    int g = (int)((c >> 8 & 255) * f) >> 8;
    int b = (int)((c & 255) * f) >> 8;
    return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

static void render_scene(void) {
    u32 *bb = gfx_backbuf();
    int W = (int)gfx_width();
    int VH = (int)gfx_height() - 21;
    if (rayw != W / 2) rayw = W / 2 > RAYW_MAX ? RAYW_MAX : W / 2;
    /* mennyezet + padló */
    for (int y = 23; y < VH + 1; y++) {
        u32 c = y < g_vmid ? ceil_row[y] : floor_row[y];
        u32 *row = bb + y * W;
        for (int x = 0; x < W; x++) row[x] = c;
    }

    float dc = kosz_cosf(rang), ds = kosz_sinf(rang);
    float plx = -ds * 0.66f, ply = dc * 0.66f;

    for (int col = 0; col < rayw; col++) {
        float camx = 2.0f * col / (float)rayw - 1.0f;
        float rdx = dc + plx * camx;
        float rdy = ds + ply * camx;
        int mapx = (int)rpx, mapy = (int)rpy;
        float ddx = rdx == 0 ? 1e30f : (rdx < 0 ? -1.0f / rdx : 1.0f / rdx);
        float ddy = rdy == 0 ? 1e30f : (rdy < 0 ? -1.0f / rdy : 1.0f / rdy);
        int stx = rdx < 0 ? -1 : 1, sty = rdy < 0 ? -1 : 1;
        int side = 0;
        float sdx = rdx < 0 ? ((float)mapx - rpx + 1.0f) * ddx * (rdx < 0 ? -1.0f : 1.0f) : ((float)mapx + 1.0f - rpx) * (rdx < 0 ? -ddx : ddx);
        float sdy = rdy < 0 ? ((float)mapy - rpy + 1.0f) * ddy * (rdy < 0 ? -1.0f : 1.0f) : ((float)mapy + 1.0f - rpy) * (rdy < 0 ? -ddy : ddy);
        /* Egyszerűbb és helyes kezdő lépések: */
        sdx = rdx < 0 ? ((float)mapx - rpx) * -ddx : ((float)mapx + 1.0f - rpx) * ddx;
        sdy = rdy < 0 ? ((float)mapy - rpy) * -ddy : ((float)mapy + 1.0f - rpy) * ddy;
        if (rdx == 0) sdx = 1e30f;
        if (rdy == 0) sdy = 1e30f;

        char hitc = 0;
        int guard = 0;
        while (!hitc && guard++ < 64) {
            if (sdx < sdy) { sdx += ddx; mapx += stx; side = 0; }
            else           { sdy += ddy; mapy += sty; side = 1; }
            if (mapx < 0 || mapy < 0 || mapx >= RMAPW || mapy >= RMAPH) { hitc = '#'; break; }
            char c = (char)rmap[mapy][mapx];
            if (is_wall(c)) hitc = c;
        }
        float perp = side == 0 ? ((float)mapx - rpx + (stx < 0 ? 1.0f : 0.0f)) / (rdx == 0 ? 1e-9f : rdx)
                               : ((float)mapy - rpy + (sty < 0 ? 1.0f : 0.0f)) / (rdy == 0 ? 1e-9f : rdy);
        if (perp < 0.02f) perp = 0.02f;
        zbuf[col] = (u32)(perp * 64.0f);

        int lineH = (int)(PROJ / perp);
        int y0 = g_vmid - lineH / 2, y1 = y0 + lineH;
        if (y0 < 23) y0 = 23;
        if (y1 > VH) y1 = VH;

        int ti = 0;
        if (hitc == 'S') ti = 1;
        else if (hitc == 'D') ti = 2;
        else if (hitc == 'H') ti = 3;
        else if (hitc == 'E') ti = 5;
        /* fal-x textúra koordináta */
        float wallx = side == 0 ? rpy + perp * rdy : rpx + perp * rdx;
        wallx -= (float)(int)wallx;
        int texx = (int)(wallx * 64.0f);
        if ((side == 0 && rdx > 0) || (side == 1 && rdy < 0)) texx = 63 - texx;
        if (texx < 0) texx = 0; if (texx > 63) texx = 63;

        int f = (int)(256.0f / (1.0f + perp * perp * 0.035f));
        if (side == 1) f = f * 3 / 4;
        if (hitc == 'E') f = 256;                      /* portál világít */

        int fullH = (int)(PROJ / perp);
        int top = g_vmid - fullH / 2;
        for (int y = y0; y < y1; y++) {
            int texy = (y - top) * 64 / fullH;
            if (texy < 0) texy = 0; if (texy > 63) texy = 63;
            u32 c = shade(tex[ti][texy * 64 + texx], f);
            u32 *dst = bb + y * W + col * 2;
            dst[0] = c; dst[1] = c;
        }
    }
}

/* sprite-ok rajzolása (pokoljáró) */
static void render_sprites(void) {
    if (!rmode) return;
    float dc = kosz_cosf(rang), ds = kosz_sinf(rang);
    float plx = -ds * 0.66f, ply = dc * 0.66f;
    float invDet = 1.0f / (plx * ds - dc * ply);

    int order[RMON_MAX];
    float dist[RMON_MAX];
    for (int i = 0; i < rmon_n; i++) {
        float dx = rmons[i].x - rpx, dy = rmons[i].y - rpy;
        dist[i] = dx * dx + dy * dy;
        order[i] = i;
    }
    for (int a = 0; a < rmon_n; a++)
        for (int b = a + 1; b < rmon_n; b++)
            if (dist[order[b]] > dist[order[a]]) { int t = order[a]; order[a] = order[b]; order[b] = t; }

    u32 *bb = gfx_backbuf();
    int W = (int)gfx_width();
    for (int k = 0; k < rmon_n; k++) {
        rmon_t *m = &rmons[order[k]];
        float dx = m->x - rpx, dy = m->y - rpy;
        float tx = invDet * (ds * dx - dc * dy);
        float ty = invDet * (-ply * dx + plx * dy);
        if (ty < 0.2f) continue;
        int frame = !m->alive ? 3 : m->anim;
        int screenX = (int)((W / 2) * (1.0f + tx / ty));
        int sh = (int)(PROJ / ty * 0.72f);
        int sw = sh;
        int bottom = g_vmid + (int)(PROJ / ty) / 2 + 2;
        int y0 = bottom - sh, y1 = bottom;
        int x0 = screenX - sw / 2, x1 = x0 + sw;
        for (int x = x0; x < x1; x++) {
            if (x < 0 || x >= W) continue;
            int ray = x / 2;
            if (ray < 0 || ray >= rayw) continue;
            if (zbuf[ray] < (u32)(ty * 64.0f)) continue;    /* fal takarja */
            int sx = (x - x0) * 32 / (sw > 0 ? sw : 1);
            for (int y = y0; y < y1; y++) {
                if (y < 23 || y >= (int)gfx_height() - 21) continue;
                int sy = (y - y0) * 32 / (sh > 0 ? sh : 1);
                u32 c = msp[frame][sy * 32 + sx];
                if (!c) continue;
                int f = (int)(256.0f / (1.0f + ty * ty * 0.035f));
                bb[y * W + x] = shade(c, f);
            }
        }
    }
}

/* fegyver + HUD */
static void render_gun(void) {
    if (!rmode || rstate) return;
    int bobx = (int)(kosz_sinf(rbob) * 9.0f);
    int boby = (int)(kosz_cosf(rbob * 2.0f) * 5.0f);
    int gx = (int)gfx_width() / 2 - 42 + bobx, gy = (int)gfx_height() - 21 - 118 + boby;
    /* karok */
    gfx_fill_rect(gx - 26, gy + 62, 26, 56, 0xFFB08050);
    gfx_fill_rect(gx + 84, gy + 62, 26, 56, 0xFFB08050);
    /* markolat */
    gfx_fill_rect(gx + 24, gy + 30, 36, 60, 0xFF6A4218);
    /* tok */
    gfx_fill_rect(gx + 12, gy + 10, 60, 40, 0xFF3A4048);
    gfx_fill_rect(gx + 18, gy + 16, 48, 12, 0xFF586068);
    /* cső */
    gfx_fill_rect(gx + 30, gy - 18, 24, 32, 0xFF2A2E34);
    /* torkolattűz */
    if ((s32)(tick_count() - rflash) < 6) {
        app_circle(gx + 42, gy - 22, 20, 0xFFFFFF00u, 0);
        app_circle(gx + 42, gy - 22, 12, 0xFFE8B24A, 1);
        app_circle(gx + 42, gy - 22, 5, 0xFFFFFFFFu, 1);
    }
}

static void render_minimap(void) {
    if (!rminimap) return;
    int mx0 = (int)gfx_width() - RMAPW * 5 - 16, my0 = 30, cs = 5;
    gfx_fill_rect(mx0 - 2, my0 - 2, RMAPW * cs + 4, RMAPH * cs + 4, 0xA0000000u | 0x101510);
    for (int y = 0; y < RMAPH; y++)
        for (int x = 0; x < RMAPW; x++) {
            char c = (char)rmap[y][x];
            u32 col = 0;
            if (c == '#') col = 0xFF3A5060;
            else if (c == 'S') col = 0xFF505860;
            else if (c == 'D') col = 0xFF8A6A30;
            else if (c == 'H') col = 0xFF7A2A24;
            else if (c == 'E') col = rkills >= rmon_n || !rmode ? 0xFF50E878 : 0xFF2A5A38;
            if (col) gfx_fill_rect(mx0 + x * cs, my0 + y * cs, cs - 1, cs - 1, col);
        }
    if (rmode)
        for (int i = 0; i < rmon_n; i++)
            if (rmons[i].alive)
                gfx_fill_rect(mx0 + (int)(rmons[i].x * cs), my0 + (int)(rmons[i].y * cs), 3, 3, 0xFFE85050);
    /* játékos */
    int px = mx0 + (int)(rpx * cs), py = my0 + (int)(rpy * cs);
    gfx_fill_rect(px - 1, py - 1, 3, 3, 0xFFF0F0F0);
    gfx_pixel(px + (int)(kosz_cosf(rang) * 5), py + (int)(kosz_sinf(rang) * 5), 0xFFE8B24A);
}

void ray_frame(u32 tick) {
    /* fix 100 Hz fizika */
    while ((s32)(tick - rnext_phys) >= 0) { ray_phys(); rnext_phys += 1; }
    if (rnext_phys < tick - 200) rnext_phys = tick;   /* reset ha lemaradt */

    render_scene();
    render_sprites();
    render_gun();
    render_minimap();

    /* sebzés-villanás */
    if (rmode && (s32)(tick - rhitflash) < 8) {
        u32 *bb = gfx_backbuf();
        for (int i = 0; i < (int)(gfx_width() * gfx_height()); i++)
            bb[i] = (bb[i] & 0xFF00FF00) | ((bb[i] & 0x00FF0000) >> 1) | 0x00004000u;
    }

    u32 accent = settings_accent_color();
    char b[96];
    if (rstate == 1) {
        unsigned secs = (tick - rstart_tick) / 100;
        ksnprintf(b, sizeof(b), "%s  %u:%02u", ui_lang() ? "TIME" : "IDŐ", secs / 60, secs % 60);
        app_chrome(rmode ? "Pokoljáró - GYOZELEM!" : "Labirintus 3D - KIJUTOTTAL!", b, accent);
        gfx_draw_text_scaled(((int)gfx_width() - 8 * 32) / 2, 300, ui_lang() ? "YOU WIN!" : "GYOZTEL!", 0xFF50E878, 4);
        gfx_draw_text(((int)gfx_width() - gfx_text_width(ui_lang() ? "R or Space: again" : "R vagy Space: ujra")) / 2, 360, ui_lang() ? "R or Space: again" : "R vagy Space: ujra", 0xFFF0F0F0, 0xFF000000u);
        return;
    }
    if (rstate == 2) {
        app_chrome("Pokoljáró", ui_lang() ? "you died..." : "meghaltal...", 0xFFE86A6A);
        gfx_draw_text_scaled(((int)gfx_width() - 9 * 32) / 2, 300, ui_lang() ? "YOU DIED" : "MEGHALTAL", 0xFFE85050, 4);
        gfx_draw_text(((int)gfx_width() - gfx_text_width(ui_lang() ? "R or Space: retry" : "R vagy Space: ujra")) / 2, 360, ui_lang() ? "R or Space: retry" : "R vagy Space: ujra", 0xFFF0F0F0, 0xFF000000u);
        return;
    }
    if (rmode)
        ksnprintf(b, sizeof(b), "HP: %d%%   %s: %d   %s: %d/%d   %s",
                  rhp, ui_lang() ? "AMMO" : "LŐSZER", rammo,
                  ui_lang() ? "KILLS" : "GYZM", rkills, rmon_n,
                  ui_lang() ? "Arrows/WASD: walk  Space: fire  M: map"
                            : "Nyilak/WASD: mozgas  Space: loves  M: terkep");
    else
        ksnprintf(b, sizeof(b), "%s   %s",
                  ui_lang() ? "Find the green portal!" : "Talald meg a zold portalt!",
                  ui_lang() ? "Arrows/WASD: walk  M: map" : "Nyilak/WASD: mozgas  M: terkep");
    if ((s32)(tick - rmsguntil) < 0) ksnprintf(b, sizeof(b), "%s", rmsg);
    app_chrome(rmode ? "Pokoljáró (Doom-hommage)" : "Labirintus 3D (Wolfenstein-hommage)", b, accent);

    if (rmode) {
        /* HP + lőszer sáv */
        gfx_fill_rect(16, (int)gfx_height() - 56, 200, 14, 0x80000000u);
        gfx_fill_rect(16, (int)gfx_height() - 56, rhp * 2, 14, rhp > 30 ? 0xFF50E878 : 0xFFE85050);
        gfx_rect(16, (int)gfx_height() - 56, 200, 14, 0xFFF0F0F0);
        char a[24];
        ksnprintf(a, sizeof(a), "%d", rammo);
        gfx_draw_text_scaled((int)gfx_width() - 84, (int)gfx_height() - 68, a, 0xFFE8B24A, 2);
    }
}
