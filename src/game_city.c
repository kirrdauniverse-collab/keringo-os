/* game_city.c — Városépítő (SimCity 2000-hommage): zónák, út, erőmű,
 * gazdaság-szimuláció, egérrel építés */
#include "kernel.h"

#define CW 24
#define CHh 17
#define CELL 40
static int COX = 8;
#define COY 30

/* cellatípusok */
enum { C_EMPTY, C_ROAD, C_RES, C_COM, C_IND, C_PARK, C_POWER, C_WATER };
static u8 cgrid[CW][CHh];
static u8 ctmp[CW][CHh];

static int ctool = C_ROAD;
static int cmoney = 20000;
static int cpop = 0, cjobs = 0, cpow = 0, cpow_use = 0, cpol = 0, chappy = 70;
static int cframe_ct = 0;
static u32 cnext_sim = 0, cmsguntil = 0;
static const char *cmsg = "";
static int cdrag_road = 0;

static const char *cnames_hu[8] = { "üres", "út", "lakó", "kereskedő", "ipari", "park", "erőmű", "víz" };
static const char *cnames_en[8] = { "empty", "road", "residential", "commercial", "industrial", "park", "power", "water" };
static const int ccost[8] = { 0, 10, 100, 120, 150, 30, 1000, 50 };
static const u32 ccolors[8] = {
    0xFF2A3440, 0xFF3A3E46, 0xFF62D26F, 0xFF248BBE, 0xFFC4863A, 0xFF3A8E4A, 0xFFE8B24A, 0xFF2470A8
};

void city_enter(void) {
    COX = ((int)gfx_width() - CW * CELL) / 2;
    if (COX < 8) COX = 8;
    cframe_ct = 0;
    cnext_sim = tick_count() + 300;
    cmsguntil = 0;
    sound_beep(660, 5);
    /* alap: egy út és erőmű, hogy induljon a város */
    for (int x = 0; x < CW; x++) cgrid[x][8] = C_ROAD;
    cgrid[3][8] = C_POWER;
    cmsg = "Építs a zónákkal az út mellé!";
}
void city_exit(void) { cdrag_road = 0; }

static int road_near(int x, int y) {
    if (x > 0 && cgrid[x-1][y] == C_ROAD) return 1;
    if (x < CW-1 && cgrid[x+1][y] == C_ROAD) return 1;
    if (y > 0 && cgrid[x][y-1] == C_ROAD) return 1;
    if (y < CHh-1 && cgrid[x][y+1] == C_ROAD) return 1;
    return 0;
}

static int power_near(int x, int y) {
    /* elektromos hálózat: úton vagy 2 cellán belül erőműtől út mentén — egyszerűsítve: 4 cella sugarú */
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= CW || yy >= CHh) continue;
            if (cgrid[xx][yy] == C_POWER) return 1;
        }
    return 0;
}

static void city_sim(void) {
    /* népesség, munka, áram, szennyezés újraszámolása */
    cpop = 0; cjobs = 0; cpow = 0; cpol = 0;
    for (int y = 0; y < CHh; y++)
        for (int x = 0; x < CW; x++) {
            u8 c = cgrid[x][y];
            if (c == C_RES && road_near(x, y) && power_near(x, y)) cpop += 12;
            if (c == C_COM && road_near(x, y)) cjobs += 8;
            if (c == C_IND && road_near(x, y)) { cjobs += 14; cpol += 3; }
            if (c == C_POWER) cpow += 50;
            if (c == C_PARK) cpol -= 2;
        }
    if (cpol < 0) cpol = 0;
    cpow_use = (cpop / 4) + (cjobs / 6);

    /* bevétel/költség */
    int income = cpop * 2 / 3 + cjobs * 1 / 2;
    int upkeep = 0;
    for (int y = 0; y < CHh; y++)
        for (int x = 0; x < CW; x++) {
            u8 c = cgrid[x][y];
            if (c == C_ROAD) upkeep += 1;
            else if (c == C_POWER) upkeep += 25;
            else if (c == C_PARK) upkeep += 2;
        }
    cmoney += income - upkeep;

    /* boldogság */
    int target = 70 + (cpow_use > cpow ? -30 : 0) + (cpol > 30 ? -20 : 0) + (cjobs >= cpop / 3 * 2 ? 10 : -10);
    if (target > 100) target = 100;
    if (target < 0) target = 0;
    chappy += (target - chappy) / 4;

    /* csőd? */
    if (cmoney < 0) {
        cmoney = 0;
        cmsg = "CSŐD! bontsd le a drága épületeket (jobb klikk)";
        cmsguntil = tick_count() + 300;
    }
}

static void place_at(int mx, int my, int erase) {
    int x = (mx - COX) / CELL, y = (my - COY) / CELL;
    if (x < 0 || y < 0 || x >= CW || y >= CHh) return;
    u8 want = erase ? C_EMPTY : (u8)ctool;
    if (cgrid[x][y] == want) return;
    int cost = erase ? 5 : ccost[ctool];
    if (cmoney < cost) {
        if ((s32)(tick_count() - cmsguntil) < 0) return;
        cmsg = "NINCS ELÉG PÉNZ!";
        cmsguntil = tick_count() + 120;
        sound_beep(140, 8);
        return;
    }
    cgrid[x][y] = want;
    cmoney -= cost;
    sound_beep(want == C_POWER ? 520 : 720, 2);
    city_sim();
}

void city_mouse(int mx, int my, int dx, int dy, u8 btn, u8 prev) {
    (void)dx; (void)dy;
    if ((btn & 1) && !(prev & 1)) place_at(mx, my, 0);
    else if ((btn & 1) && (prev & 1)) {
        /* húzás: útnál folytonos vonal, másnál csak új cella */
        if (ctool == C_ROAD || ctool == 2) place_at(mx, my, 0);
    }
    if ((btn & 2) && !(prev & 2)) place_at(mx, my, 1);
}

void city_key(u8 sc, u8 rel, u8 ascii) {
    if (rel) return;
    (void)sc;
    if (ascii >= '1' && ascii <= '8') { ctool = ascii - '1'; sound_beep(700, 2); return; }
    if (ascii == 'b' || ascii == 'B') { ctool = C_EMPTY; return; }
}

static void draw_city_cell(int x, int y, u8 c, int tick) {
    int px = COX + x * CELL, py = COY + y * CELL;
    gfx_fill_rect(px, py, CELL - 1, CELL - 1, ccolors[c]);
    switch (c) {
        case C_ROAD:
            /* út: szürke csík a cellák összekötéséhez */
            gfx_fill_rect(px, py + 14, CELL - 1, 11, 0xFF23262C);
            gfx_fill_rect(px + 14, py, 11, CELL - 1, 0xFF23262C);
            for (int k = 0; k < 3; k++)
                gfx_fill_rect(px + 4 + k * 12, py + 18, 6, 2, 0xFFE8C84A);
            break;
        case C_RES: {
            /* házikók négyesével */
            for (int h = 0; h < 4; h++) {
                int hx = px + 3 + (h % 2) * 18, hy = py + 3 + (h / 2) * 17;
                gfx_fill_rect(hx, hy + 4, 13, 11, 0xFFDCE8F0);
                gfx_fill_rect(hx, hy, 13, 5, 0xFF8E3A2E);
                gfx_fill_rect(hx + 5, hy + 7, 3, 3, 0xFF248BBE);
            }
            if (!power_near(x, y)) gfx_draw_text(px + 4, py + 14, "!", 0xFFE8B24A, 0x00000000u);
            break; }
        case C_COM: {
            gfx_fill_rect(px + 3, py + 3, 32, 32, 0xFF1E5E8E);
            gfx_fill_rect(px + 6, py + 6, 26, 10, 0xFFA8D8F0);
            gfx_draw_text(px + 6, py + 20, "ABC", 0xFFF0F0F0, 0x00000000u);
            if (!road_near(x, y)) gfx_draw_text(px + 4, py + 14, "!", 0xFFE86A6A, 0x00000000u);
            break; }
        case C_IND: {
            gfx_fill_rect(px + 2, py + 8, 34, 28, 0xFF7A4E22);
            gfx_fill_rect(px + 6, py + 2, 9, 10, 0xFF5A3A18);
            gfx_fill_rect(px + 19, py + 2, 9, 10, 0xFF5A3A18);
            int sm = (tick / 20 + x * 3 + y) % 8;
            gfx_fill_rect(px + 7 + sm / 3, py - 2, 3, 5, 0xFF8FA0AC);
            break; }
        case C_PARK: {
            app_circle(px + 12, py + 26, 8, 0xFF1E5E2E, 1);
            app_circle(px + 27, py + 24, 9, 0xFF26703A, 1);
            gfx_fill_rect(px + 4, py + 33, 30, 3, 0xFF8E6A3A);
            break; }
        case C_POWER: {
            gfx_fill_rect(px + 2, py + 12, 34, 24, 0xFF8E7A2E);
            /* villám-jel */
            for (int k = 0; k < 6; k++) gfx_pixel(px + 20 + k, py + 18 + k, 0xFFFFFFFFu);
            for (int k = 0; k < 6; k++) gfx_pixel(px + 20 - k + 3, py + 24 + k, 0xFFFFFFFFu);
            int on = cpow_use <= cpow;
            gfx_fill_rect(px + 30, py + 4, 4, 8, on ? 0xFF50E878 : 0xFFE85050);
            break; }
        case C_WATER: {
            /* hullámzó víz */
            int ph = (tick / 30 + x + y) % 12;
            gfx_fill_rect(px, py, CELL - 1, CELL - 1, 0xFF1E5090);
            gfx_hline(px + 4, py + 10 + ph / 2, 14, 0xFF3A70B0);
            gfx_hline(px + 16, py + 24 - ph / 3, 12, 0xFF3A70B0);
            break; }
    }
}

void city_frame(u32 tick) {
    /* szimuláció 3 s-enként */
    if ((s32)(tick - cnext_sim) >= 0) {
        city_sim();
        cnext_sim = tick + 300;
    }

    /* rács */
    gfx_fill_rect(0, 23, 1024, 725, 0xFF151C24);
    for (int y = 0; y < CHh; y++)
        for (int x = 0; x < CW; x++)
            draw_city_cell(x, y, cgrid[x][y], tick);

    /* eszköztár alul (a keret fölött) */
    int tx = 8;
    (void)0;
    for (int t = 1; t < 8; t++) {
        char lbl[8];
        ksnprintf(lbl, sizeof(lbl), "%d", t);
        app_button(tx, 700, 118, 40, cnames_hu[t], settings_accent_color(), t == ctool);
        gfx_draw_text(tx + 104, 712, lbl, 0xFF8FA0AC, 0x00000000u);
        tx += 130;
    }

    /* egér alatti cella-előnézet */
    int mx = app_mouse_x(), my = app_mouse_y();
    int gx = (mx - COX) / CELL, gy = (my - COY) / CELL;
    if (gx >= 0 && gy >= 0 && gx < CW && gy < CHh)
        gfx_rect(COX + gx * CELL, COY + gy * CELL, CELL - 1, CELL - 1, 0xFFF0F0F0);

    u32 accent = settings_accent_color();
    char b[160];
    const char *tn = ui_lang() ? cnames_en[ctool] : cnames_hu[ctool];
    ksnprintf(b, sizeof(b), "$%d  %s: %d  %s: %d  %s: %d/%d  %s: %d%%  %s: %d (%s)  %s",
              cmoney,
              ui_lang() ? "Pop" : "Lakos", cpop,
              ui_lang() ? "Jobs" : "Munka", cjobs,
              ui_lang() ? "Power" : "Áram", cpow_use, cpow,
              ui_lang() ? "Happy" : "Boldog", chappy,
              ui_lang() ? "Tool" : "Eszköz", ctool + 1, tn,
              ui_lang() ? "1-8: tool  click: build  right-click: demolish"
                        : "1-8: eszköz  klikk: építés  jobb klikk: bontás");
    if ((s32)(tick - cmsguntil) < 0) ksnprintf(b, sizeof(b), "%s", cmsg);
    app_chrome(ui_lang() ? "City builder (SimCity-hommage)" : "Városépítő (SimCity-hommage)", b, accent);
    (void)cframe_ct; (void)ctmp;
}
