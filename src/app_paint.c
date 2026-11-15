/* app_paint.c — Festő: rajzolóprogram (ceruza, alakzatok, töltés, BMP mentés) */
#include "kernel.h"

#define CX 88            /* canvas bal felső sarka a képernyőn */
#define CY 23
static int CW = 936, CH = 724;   /* canvas mérete (felbontásfüggő, első indításkor) */

static u32 *canvas = 0;  /* CW*CH pixel */
static u32 *snap = 0;    /* visszavonáshoz / alakzat-előnézethez */
static int *floodstk = 0;

static int tool = 0;     /* 0 ceruza 1 vonal 2 tégla 3 teli tégla 4 ellipszis 5 szóró 6 radír 7 töltés */
static int col_idx = 6;   /* alap: borostyán - jól látszik a sötét vásznon */
static int brush = 2;
static int dragging = 0, ax = 0, ay = 0, lx = 0, ly = 0;
static u32 msg_until = 0;
static const char *msg_txt = "";

static const u32 pal[24] = {
    0xFF101010, 0xFF404040, 0xFF808080, 0xFFC0C0C0, 0xFFF0F0F0, 0xFFFFFFFF,
    0xFFE8B24A, 0xFFE86A6A, 0xFFB07AE8, 0xFF5AD2C8, 0xFF62D26F, 0xFF248BBE,
    0xFF7A4A12, 0xFFC46A1E, 0xFFF0A020, 0xFFF8D878, 0xFF2E4A2E, 0xFF4A8E3A,
    0xFF8ED84A, 0xFF1A2E4E, 0xFF2E5EA8, 0xFF6AA8E8, 0xFF8E2E5E, 0xFFD26AA8,
};
static const char *tool_hu[8] = { "ceruza", "vonal", "téglalap", "teli téglalap", "ellipszis", "szóró", "radír", "töltés" };
static const char *tool_en[8] = { "pencil", "line", "rectangle", "filled rect", "ellipse", "spray", "eraser", "fill" };

static void pmsg(const char *s, u32 t) { msg_txt = s; msg_until = t; }

static void stamp(int x, int y, u32 c, int size) {
    /* x,y canvas-koordináta */
    int h = size / 2;
    for (int j = -h; j <= h; j++)
        for (int i = -h; i <= h; i++) {
            int xx = x + i, yy = y + j;
            if (xx >= 0 && yy >= 0 && xx < CW && yy < CH) canvas[yy * CW + xx] = c;
        }
}

static void stamp_line(int x0, int y0, int x1, int y1, u32 c, int size) {
    int dx = x1 - x0, dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int n = ax > ay ? ax : ay;
    if (n == 0) { stamp(x0, y0, c, size); return; }
    for (int i = 0; i <= n; i++)
        stamp(x0 + dx * i / n, y0 + dy * i / n, c, size);
}

static void draw_ellipse(int x0, int y0, int x1, int y1, u32 c, int filled) {
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int rx = (x1 - x0) / 2; if (rx < 0) rx = -rx;
    int ry = (y1 - y0) / 2; if (ry < 0) ry = -ry;
    if (rx < 1) rx = 1; if (ry < 1) ry = 1;
    for (int y = -ry; y <= ry; y++)
        for (int x = -rx; x <= rx; x++) {
            int v = (x * x * 1000) / (rx * rx) + (y * y * 1000) / (ry * ry);
            if (filled) { if (v <= 1000) stamp(cx + x, cy + y, c, 1); }
            else if (v <= 1000 && v > 1000 - 2600 / (rx + ry) - 90) stamp(cx + x, cy + y, c, 1);
        }
}

static void do_flood(int sx, int sy) {
    if (sx < 0 || sy < 0 || sx >= CW || sy >= CH) return;
    u32 target = canvas[sy * CW + sx];
    u32 c = pal[col_idx];
    if (target == c) return;
    int sp = 0;
    floodstk[sp++] = sy * CW + sx;
    canvas[sy * CW + sx] = c;                    /* azonnal színezünk: minden cella egyszer kerül a verembe */
    while (sp > 0) {
        int idx = floodstk[--sp];
        int x = idx % CW, y = idx / CW;
        if (x > 0 && canvas[idx - 1] == target)  { canvas[idx - 1] = c;  floodstk[sp++] = idx - 1; }
        if (x < CW - 1 && canvas[idx + 1] == target) { canvas[idx + 1] = c;  floodstk[sp++] = idx + 1; }
        if (y > 0 && canvas[idx - CW] == target) { canvas[idx - CW] = c; floodstk[sp++] = idx - CW; }
        if (y < CH - 1 && canvas[idx + CW] == target) { canvas[idx + CW] = c; floodstk[sp++] = idx + CW; }
    }
    sound_beep(500, 4);
}

/* ---------- BMP mentés ---------- */
static void save_bmp(void) {
    /* teljes kép -> /sys, bélyegkép -> /mem */
    u32 rowb = (u32)((CW * 3 + 3) & ~3);
    u32 dsz = rowb * CH;
    u8 *b = (u8 *)kmalloc(54 + dsz);
    if (!b) { pmsg("nincs elég memória!", tick_count() + 200); return; }
    for (int i = 0; i < 54; i++) b[i] = 0;
    b[0] = 'B'; b[1] = 'M';
    b[2] = (u8)(54 + dsz); b[3] = (u8)((54 + dsz) >> 8); b[4] = (u8)((54 + dsz) >> 16); b[5] = (u8)((54 + dsz) >> 24);
    b[10] = 54; b[14] = 40;
    b[18] = (u8)CW; b[19] = (u8)(CW >> 8);
    b[22] = (u8)CH; b[23] = (u8)(CH >> 8);
    b[26] = 1; b[28] = 24;
    b[34] = (u8)dsz; b[35] = (u8)(dsz >> 8); b[36] = (u8)(dsz >> 16); b[37] = (u8)(dsz >> 24);
    for (int y = 0; y < CH; y++) {
        u8 *row = b + 54 + (u32)y * rowb;
        const u32 *src = canvas + (CH - 1 - y) * CW;
        for (int x = 0; x < CW; x++) {
            u32 p = src[x];
            row[x * 3]     = (u8)(p & 255);           /* B */
            row[x * 3 + 1] = (u8)((p >> 8) & 255);    /* G */
            row[x * 3 + 2] = (u8)((p >> 16) & 255);   /* R */
        }
    }
    if (vfs_write_file("/sys/KEP.BMP", (const char *)b, (int)(54 + dsz)))
        pmsg("mentve: /sys/KEP.BMP", tick_count() + 250);
    else
        pmsg("nincs /sys partíció a mentéshez!", tick_count() + 250);

    /* bélyegkép 64x49 a /mem-be (a Lemez panel előnézetéhez) */
    int tw = 64, th = CH * 64 / CW;
    u32 trowb = (u32)(tw * 3);
    u32 tsz = 54 + trowb * th;
    u8 *tb = (u8 *)kmalloc(tsz);
    if (tb) {
        for (int i = 0; i < 54; i++) tb[i] = 0;
        tb[0] = 'B'; tb[1] = 'M';
        tb[2] = (u8)tsz; tb[3] = (u8)(tsz >> 8); tb[10] = 54; tb[14] = 40;
        tb[18] = (u8)tw; tb[19] = (u8)(tw >> 8);
        tb[22] = (u8)th; tb[23] = (u8)(th >> 8);
        tb[26] = 1; tb[28] = 24;
        tb[34] = (u8)(trowb * th); tb[35] = (u8)((trowb * th) >> 8);
        for (int y = 0; y < th; y++) {
            u8 *row = tb + 54 + y * trowb;
            const u32 *src = canvas + ((CH - 1 - y * CH / th) * CW);
            for (int x = 0; x < tw; x++) {
                u32 p = src[x * CW / tw];
                row[x * 3] = (u8)(p & 255);
                row[x * 3 + 1] = (u8)((p >> 8) & 255);
                row[x * 3 + 2] = (u8)((p >> 16) & 255);
            }
        }
        vfs_write_file("/mem/KEP_KICSI.BMP", (const char *)tb, (int)tsz);
    }
    sound_tune((const u16[]){ 880, 8, 1175, 8, 1568, 10 }, 3);
}

/* ---------- app-függvények ---------- */
void paint_enter(void) {
    if (!canvas) {
        CW = (int)gfx_width() - 96;
        CH = (int)gfx_height() - 47;
        canvas = (u32 *)kmalloc(CW * CH * 4);
        snap = (u32 *)kmalloc(CW * CH * 4);
        floodstk = (int *)kmalloc(CW * CH * sizeof(int));
    }
    if (!canvas) { pmsg("MEMÓRIA HIBA", tick_count() + 500); return; }
    for (int i = 0; i < CW * CH; i++) canvas[i] = 0xFF20302A;   /* sötétzöld rajzlap */
    dragging = 0;
    sound_beep(700, 5);
}

void paint_exit(void) { dragging = 0; }

void paint_key(u8 sc, u8 rel, u8 ascii) {
    if (rel) return;
    (void)sc;
    if (ascii == 't' || ascii == 'T' || sc == KEY_TAB) { tool = (tool + 1) & 7; sound_beep(700, 3); }
    else if (ascii == 'c' || ascii == 'C') { col_idx = (col_idx + 1) % 24; sound_beep(900, 3); }
    else if (ascii == '[') { if (brush > 1) brush--; }
    else if (ascii == ']') { if (brush < 16) brush++; }
    else if (ascii == 'u' || ascii == 'U') {
        for (int i = 0; i < CW * CH; i++) canvas[i] = snap[i];
        pmsg("visszavonva", tick_count() + 120);
    }
    else if (ascii == 'n' || ascii == 'N') {
        for (int i = 0; i < CW * CH; i++) snap[i] = canvas[i];
        for (int i = 0; i < CW * CH; i++) canvas[i] = 0xFF20302A;
        pmsg("új rajzlap", tick_count() + 120);
    }
    else if (sc == KEY_F2) save_bmp();
}

void paint_mouse(int mx, int my, int dx, int dy, u8 btn, u8 prev) {
    (void)dx; (void)dy;
    if (!(btn & 1) && (prev & 1)) dragging = 0;                 /* gomb fel */
    if (btn & 2 && !(prev & 2)) {                               /* jobb gomb: színpipa */
        int x = mx - CX, y = my - CY;
        if (x >= 0 && y >= 0 && x < CW && y < CH) {
            u32 c = canvas[y * CW + x];
            for (int i = 0; i < 24; i++) if (pal[i] == c) { col_idx = i; break; }
            sound_beep(1100, 3);
        }
    }
    if (!(btn & 1)) return;

    int x = mx - CX, y = my - CY;
    if (x < 0 || y < 0 || x >= CW || y >= CH) return;

    if (!(prev & 1)) {                                          /* nyomás kezdete */
        for (int i = 0; i < CW * CH; i++) snap[i] = canvas[i];
        dragging = 1;
        ax = x; ay = y; lx = x; ly = y;
        if (tool == 7) { do_flood(x, y); dragging = 0; return; }
        if (tool == 0 || tool == 5 || tool == 6) {
            u32 c = (tool == 6) ? 0xFF20302A : pal[col_idx];
            stamp(x, y, c, (tool == 6) ? brush + 4 : brush);
            if (tool == 5) { /* szóró */
                for (int k = 0; k < 10; k++)
                    stamp(x + (int)(app_rand() % (brush * 3 + 3)) - brush, y + (int)(app_rand() % (brush * 3 + 3)) - brush, c, 1);
            }
            return;
        }
        return;                                                 /* alakzatok: előnézet a frame-ben */
    }
    /* húzás */
    if (tool == 0) {
        stamp_line(lx, ly, x, y, pal[col_idx], brush);
        lx = x; ly = y;
    } else if (tool == 6) {
        stamp_line(lx, ly, x, y, 0xFF20302A, brush + 4);
        lx = x; ly = y;
    } else if (tool == 5) {
        for (int k = 0; k < 12; k++)
            stamp(x + (int)(app_rand() % (brush * 4 + 4)) - brush, y + (int)(app_rand() % (brush * 4 + 4)) - brush, pal[col_idx], 1);
        lx = x; ly = y;
    } else { ax = ax; ay = ay; }                                /* alakzat: pont frissül a frame-ben */
}

static void tool_glyph(int x, int y, int t, u32 c) {
    switch (t) {
        case 0: app_line(x + 6, y + 26, x + 26, y + 6, c); break;
        case 1: app_line(x + 4, y + 24, x + 28, y + 8, c); gfx_fill_rect(x + 2, y + 22, 4, 4, c); gfx_fill_rect(x + 26, y + 6, 4, 4, c); break;
        case 2: gfx_rect(x + 5, y + 8, 22, 16, c); break;
        case 3: gfx_fill_rect(x + 5, y + 8, 22, 16, c); break;
        case 4: app_circle(x + 16, y + 16, 9, c, 0); break;
        case 5: for (int k = 0; k < 12; k++) gfx_fill_rect(x + 5 + (int)(app_rand() % 22), y + 6 + (int)(app_rand() % 20), 2, 2, c); break;
        case 6: gfx_fill_rect(x + 6, y + 10, 20, 12, 0xFFF0F0F0); gfx_rect(x + 6, y + 10, 20, 12, c); break;
        case 7: app_line(x + 6, y + 20, x + 18, y + 8, c); app_line(x + 18, y + 8, x + 26, y + 16, c); app_line(x + 26, y + 16, x + 14, y + 26, c); app_line(x + 14, y + 26, x + 6, y + 20, c); gfx_fill_rect(x + 20, y + 22, 3, 6, c); break;
    }
}

void paint_frame(u32 tick) {
    if (!canvas) { app_chrome("Festő / Paint", "memória hiba", 0xFFE86A6A); return; }
    u32 accent = settings_accent_color();

    /* alakzat-előnézet: snap vissza + alakzat */
    if (dragging && tool >= 1 && tool <= 4 && !(app_mouse_btn() & 1)) dragging = 0;
    if (dragging && tool >= 1 && tool <= 4) {
        int cx = app_mouse_x() - CX, cy = app_mouse_y() - CY;
        if (cx >= 0 && cy >= 0 && cx < CW && cy < CH) {
            for (int i = 0; i < CW * CH; i++) canvas[i] = snap[i];
            u32 c = pal[col_idx];
            if (tool == 1) stamp_line(ax, ay, cx, cy, c, brush / 2 + 1);
            else if (tool == 2) {
                int x0 = ax < cx ? ax : cx, y0 = ay < cy ? ay : cy;
                stamp_line(x0, y0, x0 + (cx - ax < 0 ? ax - cx : cx - ax), y0, c, 1);
                stamp_line(x0, y0 + (cy - ay < 0 ? ay - cy : cy - ay), x0 + (cx - ax < 0 ? ax - cx : cx - ax), y0 + (cy - ay < 0 ? ay - cy : cy - ay), c, 1);
                stamp_line(x0, y0, x0, y0 + (cy - ay < 0 ? ay - cy : cy - ay), c, 1);
                stamp_line(x0 + (cx - ax < 0 ? ax - cx : cx - ax), y0, x0 + (cx - ax < 0 ? ax - cx : cx - ax), y0 + (cy - ay < 0 ? ay - cy : cy - ay), c, 1);
            }
            else if (tool == 3) {
                int x0 = ax < cx ? ax : cx, y0 = ay < cy ? ay : cy;
                int w = cx - ax < 0 ? ax - cx : cx - ax, h = cy - ay < 0 ? ay - cy : cy - ay;
                for (int j = 0; j < h; j++) for (int i = 0; i < w; i++)
                    if (x0 + i >= 0 && y0 + j >= 0 && x0 + i < CW && y0 + j < CH) canvas[(y0 + j) * CW + x0 + i] = c;
            }
            else if (tool == 4) draw_ellipse(ax, ay, cx, cy, c, 0);
        }
    }

    /* canvas kiírása gyorsan */
    u32 *bb = gfx_backbuf();
    for (int y = 0; y < CH; y++) {
        u32 *dst = bb + (u32)(CY + y) * gfx_width() + CX;
        const u32 *src = canvas + y * CW;
        for (int x = 0; x < CW; x++) dst[x] = src[x];
    }

    /* eszköztár */
    gfx_fill_rect(0, 23, CX, (int)gfx_height() - 44, 0xFF18202A);
    for (int t = 0; t < 8; t++) {
        int bx = 4 + (t & 1) * 44, by = 30 + (t >> 1) * 44;
        app_button(bx, by, 40, 40, 0, accent, t == tool);
        if (t != tool) tool_glyph(bx, by, t, 0xFFF0F0F0);
        else tool_glyph(bx, by, t, 0xFF10151A);
    }
    /* paletta */
    int py = 30 + 4 * 44 + 10;
    for (int i = 0; i < 24; i++) {
        int bx = 4 + (i % 2) * 44 + (i % 2) * 0 + ((i % 4) >= 2 ? 0 : 0);
        bx = 4 + (i & 3) * 21;
        int byy = py + (i >> 2) * 21;
        gfx_fill_rect(bx, byy, 19, 19, pal[i]);
        if (i == col_idx) gfx_rect(bx - 1, byy - 1, 21, 21, accent);
    }
    /* jelenlegi szín + ecset */
    int sy = py + 6 * 21 + 8;
    gfx_fill_rect(4, sy, 80, 26, pal[col_idx]);
    gfx_rect(4, sy, 80, 26, 0xFF3A5562);
    gfx_fill_rect(4 + 80 / 2 - brush, sy + 34, brush * 2, brush * 2, pal[col_idx]);

    /* keret + állapotsor */
    gfx_fill_rect(0, 22, (int)gfx_width(), 1, accent);
    char b[96];
    const char *tn = ui_lang() ? tool_en[tool] : tool_hu[tool];
    ksnprintf(b, sizeof(b), "%s: %s  |  Ecset: %d  |  U: %s  N: %s  F2: BMP",
              ui_lang() ? "Tool" : "Eszköz", tn, brush,
              ui_lang() ? "undo" : "visszavonás",
              ui_lang() ? "new" : "új");
    if ((s32)(tick - msg_until) < 0) ksnprintf(b, sizeof(b), "%s", msg_txt);
    app_chrome(ui_lang() ? "Paint" : "Festő", b, accent);

    /* ecsetkurzor */
    int mx = app_mouse_x(), my = app_mouse_y();
    if (mx >= CX && my >= CY) {
        app_line(mx - 10, my, mx + 10, my, 0xFFFFFFFF);
        app_line(mx, my - 10, mx, my + 10, 0xFFFFFFFF);
    }
}
