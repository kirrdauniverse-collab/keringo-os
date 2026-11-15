/* apps.c — Keringő OS 0.3 alkalmazás-keretrendszer:
 * regiszter, indítás/kilépés, bemenet-útvonal, közös rajzsegédek */
#include "kernel.h"

/* ---------- alkalmazás-jegyzék ---------- */
const app_t app_list[] = {
    { {"Festő",               "Paint"},          paint_enter,  paint_frame,  paint_key,  paint_mouse,  paint_exit, 0 },
    { {"Szövegszerkesztő",    "Text editor"},    text_enter,   text_frame,   text_key,   0,            text_exit,  0 },
    { {"Labirintus 3D",       "Labyrinth 3D"},   ray_enter_lab,  ray_frame,   ray_key,   0,            ray_exit,   0 },
    { {"Pokoljáró",           "Hellwalker"},     ray_enter_doom, ray_frame,   ray_key,   0,            ray_exit,   0 },
    { {"Csillagvadász",       "Star hunter"},    elite_enter,  elite_frame,  elite_key,  0,            elite_exit, 0 },
    { {"Városépítő",          "City builder"},   city_enter,   city_frame,   city_key,   city_mouse,   city_exit,  0 },
    { {"Űrkadét Flipper",     "Space Cadet Pinball"}, pin_enter, pin_frame, pin_key,   0,            pin_exit,   0 },
    { {"Képernyővédők",       "Screensavers"},   savermenu_enter, savermenu_frame, savermenu_key, 0, 0, savermenu_esc },
    { {"Számológép",          "Calculator"},     calc_enter,   calc_frame,   calc_key,   calc_mouse,   0,          0 },
};
const int APP_COUNT = sizeof(app_list) / sizeof(app_list[0]);

/* gyorsindító billentyűk (galaxis nézetben) */
const int app_hotkey_app[] = { 0, 1, 2, 3, 4, 5, 6 };
const u8  app_hotkey_key[] = { KEY_F3, KEY_F4, KEY_F7, KEY_F9, KEY_F10, KEY_F11, KEY_F12 };
const int APP_HOTKEYS = sizeof(app_hotkey_key);

static int cur = -1;                 /* aktív alkalmazás sorszáma */
static int amx = 512, amy = 384;     /* abszolút egérpozíció */
static u8  abtn = 0;

/* ---------- keretrendszer ---------- */
int  app_active(void) { return cur; }

void app_launch(int i) {
    if (i < 0 || i >= APP_COUNT) return;
    cur = i;
    amx = (int)gfx_width() / 2;
    amy = (int)gfx_height() / 2;
    if (app_list[i].enter) app_list[i].enter();
    sound_beep(880, 6);
}

void app_close(void) {
    if (cur < 0) return;
    if (app_list[cur].exit) app_list[cur].exit();
    cur = -1;
    sound_beep(440, 5);
}

void app_key_event(u8 sc, u8 rel, u8 ascii) {
    if (cur < 0) return;
    if (sc == KEY_ESC && !rel) {
        /* az app elfogyaszthatja az Escet (pl. futó képernyővédő leállítása) */
        if (app_list[cur].esc && app_list[cur].esc()) return;
        app_close();
        return;
    }
    if (app_list[cur].key) app_list[cur].key(sc, rel, ascii);
}

void app_mouse_event(int dx, int dy, u8 btn) {
    amx += dx; amy += dy;
    if (amx < 0) amx = 0;
    if (amy < 0) amy = 0;
    if (amx > (int)gfx_width() - 2)  amx = (int)gfx_width() - 2;
    if (amy > (int)gfx_height() - 2) amy = (int)gfx_height() - 2;
    u8 prev = abtn;
    abtn = btn;
    if (cur >= 0 && app_list[cur].mouse) app_list[cur].mouse(amx, amy, dx, dy, btn, prev);
}

int  app_mouse_x(void) { return amx; }
int  app_mouse_y(void) { return amy; }
u8   app_mouse_btn(void) { return abtn; }

void app_frame(u32 tick) {
    if (cur < 0) return;
    if (app_list[cur].frame) app_list[cur].frame(tick);
    /* nyíl kurzor */
    static const u8 cur_bmp[12] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFC, 0x00, 0x00, 0x00
    };
    for (int y = 0; y < 12; y++) {
        u8 row = cur_bmp[y];
        for (int x = 0; x < 8; x++)
            if (row & (0x80 >> x)) gfx_pixel(amx + x, amy + y, 0xFFF0F0F0);
    }
    gfx_pixel(amx, amy + 12, 0xFFF0F0F0);
}

/* ---------- közös rajzsegédek ---------- */
void app_chrome(const char *title, const char *status, u32 accent) {
    int W = (int)gfx_width(), H = (int)gfx_height();
    gfx_fill_rect(0, 0, W, 22, 0xE6151B22);
    gfx_fill_rect(0, 22, W, 1, accent);
    gfx_draw_text(8, 7, title, accent, 0xFF000000u);
    const char *esc = ui_lang() ? "Esc: back to desktop" : "Esc: vissza az asztalra";
    gfx_draw_text(W - gfx_text_width(esc) - 8, 7, esc, 0xFF8FA0AC, 0xFF000000u);
    gfx_fill_rect(0, H - 20, W, 20, 0xE6151B22);
    gfx_fill_rect(0, H - 21, W, 1, accent);
    gfx_draw_text(8, H - 15, status, 0xFF8FA0AC, 0xFF000000u);
}

void app_line(int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 - x0, dy = y1 - y0;
    int ax = (dx < 0 ? -dx : dx), ay = (dy < 0 ? -dy : dy);
    if (ax == 0 && ay == 0) { gfx_pixel(x0, y0, c); return; }
    if (ax >= ay) {
        int sx = dx > 0 ? 1 : -1;
        int err = 2 * ay - ax;
        int y = y0;
        for (int i = 0; i <= ax; i++) {
            gfx_pixel(x0 + i * sx, y, c);
            if (err > 0) { y += (dy > 0 ? 1 : -1); err -= 2 * ax; }
            err += 2 * ay;
        }
    } else {
        int sy = dy > 0 ? 1 : -1;
        int err = 2 * ax - ay;
        int x = x0;
        for (int i = 0; i <= ay; i++) {
            gfx_pixel(x, y0 + i * sy, c);
            if (err > 0) { x += (dx > 0 ? 1 : -1); err -= 2 * ay; }
            err += 2 * ax;
        }
    }
}

void app_circle(int cx, int cy, int r, u32 c, int filled) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            int d2 = x * x + y * y;
            if (filled) { if (d2 <= r * r) gfx_pixel(cx + x, cy + y, c); }
            else {
                int rr = r * r;
                if (d2 <= rr && d2 > rr - 2 * r - 1) gfx_pixel(cx + x, cy + y, c);
            }
        }
    }
}

int app_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void app_button(int x, int y, int w, int h, const char *label, u32 accent, int pressed) {
    u32 bg   = pressed ? accent : 0xFF24313C;
    u32 bcol = pressed ? 0xFFF0F0F0 : accent;
    gfx_fill_rect(x, y, w, h, bg);
    gfx_rect(x, y, w, h, bcol);
    if (label) {
        int lw = gfx_text_width(label);
        gfx_draw_text(x + (w - lw) / 2, y + (h - 8) / 2, label,
                      pressed ? 0xFF10151A : 0xFFF0F0F0, 0xFF000000u);
    }
}

/* ---------- véletlenszám ---------- */
static u32 rseed = 0x12345678u;
void app_srand(u32 seed) { rseed = seed ? seed : 0x12345678u; }
u32  app_rand(void) {
    rseed = rseed * 1664525u + 1013904223u;
    return rseed ^ (rseed >> 13);
}
