/* ui.c — Keringő asztal: pályák, használati gravitáció, kamera, panelek, HU/EN */
#include "kernel.h"

#define PI2 6.28318531f

/* ---------- nyelv ---------- */
static int lang = 0;   /* 0=HU 1=EN */
void ui_set_lang(int l) { lang = l & 1; }
int  ui_lang(void) { return lang; }

/* ---------- panelek ---------- */
enum { P_MAG, P_ORA, P_FELADATOK, P_JEGYZET, P_LEMEZ, P_ROLA, PANEL_N };
typedef struct {
    const char *nev[2];
    float base_r;
    float angle;
    float speed;
    float grav;             /* 0..1 használati gravitáció */
    u32   last_focus;
    u32   focus_count;
    float y;                /* pályasík magassága */
    float bob;
    texture_t *tex;
} panel_t;

static panel_t panels[PANEL_N];
static int focus_panel = -1;
static int selected = 0;
static float view_blend = 0;          /* 0=galaxis, 1=fókusz */
static float view_target = 0;

/* színek */
#define C_BG       0xFF10151A
#define C_PANEL    0xE61C2530
#define C_BORDER   0xFF3A5562
#define C_ACCENT   0xFF248BBE
#define C_AMBER    0xFFE8B24A
#define C_TEXT     0xFFF0F0F0
#define C_MUTED    0xFF8FA0AC
#define C_ORBIT1   0xFF2E4A56
#define C_ORBIT2   0xFF2A424E
#define C_ORBIT3   0xFF253A46

/* jegyzet */
static char note[600];
static int  note_len = 0;
static u32  note_flash = 0;

/* lemez */
static char cd_names[16][64];  static u32 cd_sizes[16];  static u8 cd_flags[16];  static int cd_n = -1;
static char mem_names[16][64]; static u32 mem_sizes[16]; static u8 mem_flags[16]; static int mem_n = 0;
static int  fs_sel = 0;
static int  fs_ready = -1;               /* -1: még fut */
static char cd_data[16][2048];           /* bootkor előre olvasott tartalom */
static int  cd_dlen[16];
static char preview[1500];
static int  preview_len = 0;
static int  preview_show = 0;
static int  preview_src = 0;             /* 0=cd 1=mem */
static int  preview_idx = 0;

/* egér */
static int mx = 640, my = 400;
static u8  prev_btn = 0;
static int drag_total = 0;

/* gfx.c publikus segédek */
extern u32 utf8_decode_at(const char **s);
extern const u8 *glyph_lookup(u32 cp);

/* egér-kurzor alakja */
static const u8 cursor_bmp[12] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFC, 0x00, 0x00, 0x00
};

/* ---------- szövegek ---------- */
static const char *TITLE[] = { "KERINGŐ 0.1.0", "ORBIT 0.1.0" };
static const char *HINT_GAL[] = {
    "Nyilak/drag: forgatás  +-/görgő: zoom  Enter: belépés  Tab: következő  F1: nyelv",
    "Arrows/drag: rotate  +/-: zoom  Enter: enter  Tab: next  F1: language" };
static const char *HINT_FOK[] = {
    "Esc: vissza a galaxisba  F1: nyelv",
    "Esc: back to galaxy  F1: language" };
static const char *HINT_JEGY[] = {
    "ÍRJ! Backspace töröl, F2 = mentés a /mem tárolóba, Esc = vissza",
    "TYPE! Backspace erases, F2 = save to /mem storage, Esc = back" };
static const char *HINT_LEMEZ[] = {
    "Fel/Le: választás, Enter: megnyitás, Esc: vissza",
    "Up/Down: select, Enter: open, Esc: back" };
static const char *S_NOCD[]  = { "Nincs CD (a háttérfolyamat keresi...)", "No CD (background task is looking...)" };
static const char *S_SAVED[] = { "MENTVE a /mem tárolóba!", "SAVED to /mem storage!" };
static const char *S_GAL[]   = { "galaxis-nézet", "galaxy view" };
static const char *S_FOK[]   = { "fókusz-nézet", "focus view" };

static const char *ABOUT_HU[] = {
    "KERINGŐ - a keringő asztal",
    "",
    "A panelek pályákon keringenek a rendszer maga körül.",
    "A HASZNÁLAT GRAVITÁCIÓJA húzza őket befelé:",
    "amit sűrűn használsz, közelebb kering;",
    "amit nem, az kifelé sodródik.",
    "",
    "Saját kernel, saját grafika, saját fájlrendszer.",
    "Ez a lemez maga a /cd tároló - olvasd el az 0UDVOZLET.TXT-t!",
    0 };
static const char *ABOUT_EN[] = {
    "ORBIT - the orbiting desktop",
    "",
    "Panels orbit around the system core.",
    "USAGE GRAVITY pulls them inward:",
    "what you use often, orbits closer;",
    "what you ignore, drifts outward.",
    "",
    "Own kernel, own graphics, own filesystem.",
    "This disc is the /cd storage - open 0UDVOZLET.TXT!",
    0 };

/* ---------- textúra rajzolók ---------- */
static void t_fill(texture_t *t, int x, int y, int w, int h, u32 c) {
    for (int j = 0; j < h; j++) {
        int yy = y + j; if (yy < 0 || yy >= t->h) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i; if (xx < 0 || xx >= t->w) continue;
            t->px[yy * t->w + xx] = c;
        }
    }
}
static void t_text(texture_t *t, int x, int y, const char *s, u32 fg) {
    int cx = x;
    while (*s) {
        const char *sp = s;
        /* gfx_draw_text-ről ismert glyph-logika textúrára */
        u32 cp = utf8_decode_at(&sp);
        s = sp;
        const u8 *g = glyph_lookup(cp);
        for (int ry = 0; ry < 8; ry++) {
            u8 bits = g[ry];
            for (int rx = 0; rx < 8; rx++) {
                if (bits & (1 << rx)) {
                    int xx = cx + rx, yy = y + ry;
                    if (xx >= 0 && yy >= 0 && xx < t->w && yy < t->h) t->px[yy * t->w + xx] = fg;
                }
            }
        }
        cx += 8;
    }
}
static void t_text_big(texture_t *t, int x, int y, const char *s, u32 fg, int scale) {
    int cx = x;
    while (*s) {
        const char *sp = s;
        u32 cp = utf8_decode_at(&sp);
        s = sp;
        const u8 *g = glyph_lookup(cp);
        for (int ry = 0; ry < 8; ry++) {
            u8 bits = g[ry];
            for (int rx = 0; rx < 8; rx++) if (bits & (1 << rx))
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int xx = cx + rx * scale + sx, yy = y + ry * scale + sy;
                        if (xx >= 0 && yy >= 0 && xx < t->w && yy < t->h) t->px[yy * t->w + xx] = fg;
                    }
        }
        cx += 8 * scale;
    }
}
static void t_rect(texture_t *t, int x, int y, int w, int h, u32 c) {
    t_fill(t, x, y, w, 1, c); t_fill(t, x, y + h - 1, w, 1, c);
    t_fill(t, x, y, 1, h, c); t_fill(t, x + w - 1, y, 1, h, c);
}

/* ---------- panel-tartalmak ---------- */
static void draw_panel_mag(texture_t *t, u32 tick) {
    t_fill(t, 0, 0, t->w, t->h, C_PANEL);
    t_rect(t, 0, 0, t->w, t->h, C_BORDER);
    t_fill(t, 0, 0, t->w, 22, 0xFF24313C);
    t_text(t, 8, 7, panels[P_MAG].nev[lang], C_TEXT);
    t_text(t, t->w - 60, 7, "MAG", C_ACCENT);
    char b[80];
    int yy = 40;
    unsigned up_s = tick / 100;
    ksnprintf(b, sizeof(b), "Futásidő (uptime): %u.%02u s", up_s, (tick % 100));
    t_text(t, 14, yy, b, C_TEXT); yy += 18;
    ksnprintf(b, sizeof(b), "Tick (100 Hz): %u", tick);
    t_text(t, 14, yy, b, C_TEXT); yy += 18;
    ksnprintf(b, sizeof(b), "Taskok: %d", task_count());
    t_text(t, 14, yy, b, C_TEXT); yy += 18;
    ksnprintf(b, sizeof(b), "Szabad memória-lap: %u (4 KB)", mem_free_pages());
    t_text(t, 14, yy, b, C_TEXT); yy += 18;
    ksnprintf(b, sizeof(b), "Összes memória: %u KB", mem_total_kb());
    t_text(t, 14, yy, b, C_TEXT); yy += 18;
    ksnprintf(b, sizeof(b), "Nyelv / Language: %s", lang ? "EN" : "HU");
    t_text(t, 14, yy, b, C_TEXT); yy += 18;
    ksnprintf(b, sizeof(b), "Kernel: KERINGO 0.1.0 i386 sajat mag");
    t_text(t, 14, yy, b, C_MUTED); yy += 24;
    t_text(t, 14, yy, "Ez egy sajat kernelen fut!", C_AMBER);
}

static void draw_panel_ora(texture_t *t, u32 tick) {
    t_fill(t, 0, 0, t->w, t->h, C_PANEL);
    t_rect(t, 0, 0, t->w, t->h, C_BORDER);
    t_fill(t, 0, 0, t->w, 22, 0xFF24313C);
    t_text(t, 8, 7, panels[P_ORA].nev[lang], C_TEXT);
    t_text(t, t->w - 60, 7, "ORA", C_ACCENT);
    /* analóg kör: a másodperc-mutató kering */
    int cx = t->w / 2, cy = t->h / 2 + 12;
    int R = 96;
    for (int i = 0; i < 60; i++) {
        float a = (float)i * PI2 / 60.0f;
        int px = cx + (int)(kosz_cosf(a) * R);
        int py = cy + (int)(kosz_sinf(a) * R);
        int big = (i % 5) == 0;
        t_fill(t, px - (big ? 2 : 1), py - (big ? 2 : 1), big ? 4 : 2, big ? 4 : 2, big ? C_ACCENT : C_MUTED);
    }
    /* másodperc mutató (kering!) */
    float sec = (float)(tick % 6000) / 100.0f;
    float a = sec / 60.0f * PI2 - PI2 / 4.0f;
    for (int k = 0; k < R - 10; k++) {
        int px = cx + (int)(kosz_cosf(a) * k);
        int py = cy + (int)(kosz_sinf(a) * k);
        t_fill(t, px, py, 2, 2, C_AMBER);
    }
    /* digitális */
    char b[40];
    unsigned s = tick / 100;
    ksnprintf(b, sizeof(b), "%02u:%02u:%02u", (s / 3600) % 100, (s / 60) % 60, s % 60);
    t_text_big(t, cx - 36, t->h - 64, b, C_TEXT, 2);
}

static void draw_panel_feladatok(texture_t *t, u32 tick) {
    t_fill(t, 0, 0, t->w, t->h, C_PANEL);
    t_rect(t, 0, 0, t->w, t->h, C_BORDER);
    t_fill(t, 0, 0, t->w, 22, 0xFF24313C);
    t_text(t, 8, 7, panels[P_FELADATOK].nev[lang], C_TEXT);
    t_text(t, t->w - 90, 7, "TASKOK", C_ACCENT);
    int yy = 38;
    u32 total = 0;
    for (int i = 0; i < task_count(); i++) total += task_ticks(i);
    if (!total) total = 1;
    for (int i = 0; i < task_count() && i < 8; i++) {
        char b[64];
        ksnprintf(b, sizeof(b), "%s", task_name(i));
        t_text(t, 14, yy, b, C_TEXT);
        int w = (int)((u32)task_ticks(i) * 300u / total);
        t_fill(t, 200, yy + 2, 300, 10, 0xFF101820);
        t_fill(t, 200, yy + 2, w, 10, C_ACCENT);
        ksnprintf(b, sizeof(b), "%u", task_ticks(i));
        t_text(t, 508, yy, b, C_MUTED);
        yy += 18;
    }
    yy += 8;
    t_text(t, 14, yy, lang ? "Panel gravity:" : "Panel gravitáció:", C_MUTED); yy += 16;
    for (int i = 0; i < PANEL_N; i++) {
        t_text(t, 14, yy, panels[i].nev[lang], C_TEXT);
        int w = (int)(panels[i].grav * 300);
        t_fill(t, 200, yy + 2, 300, 8, 0xFF101820);
        t_fill(t, 200, yy + 2, w, 8, C_AMBER);
        yy += 14;
    }
    (void)tick;
}

static void draw_panel_jegyzet(texture_t *t, u32 tick) {
    t_fill(t, 0, 0, t->w, t->h, C_PANEL);
    t_rect(t, 0, 0, t->w, t->h, C_BORDER);
    t_fill(t, 0, 0, t->w, 22, 0xFF24313C);
    t_text(t, 8, 7, panels[P_JEGYZET].nev[lang], C_TEXT);
    t_text(t, t->w - 90, 7, "JEGYZET", C_ACCENT);
    /* szövegtördelés 56 oszloponként */
    int yy = 36;
    int col = 0;
    for (int i = 0; i < note_len && yy < t->h - 34; i++) {
        char c[2] = { note[i], 0 };
        t_text(t, 12 + col * 8, yy, c, C_TEXT);
        col++;
        if (col >= 56) { col = 0; yy += 12; }
    }
    /* kurzor */
    if ((tick / 25) % 2 == 0) {
        t_fill(t, 12 + col * 8, yy, 7, 10, C_AMBER);
    }
    if (tick - note_flash < 150) t_text(t, 12, t->h - 22, S_SAVED[lang], C_AMBER);
    else t_text(t, 12, t->h - 22, lang ? "F2 = save" : "F2 = mentés", C_MUTED);
}

static void draw_panel_lemez(texture_t *t, u32 tick) {
    t_fill(t, 0, 0, t->w, t->h, C_PANEL);
    t_rect(t, 0, 0, t->w, t->h, C_BORDER);
    t_fill(t, 0, 0, t->w, 22, 0xFF24313C);
    t_text(t, 8, 7, panels[P_LEMEZ].nev[lang], C_TEXT);
    t_text(t, t->w - 70, 7, "LEMEZ", C_ACCENT);
    int yy = 34;
    if (fs_ready < 0) { t_text(t, 14, yy, S_NOCD[lang], C_MUTED); return; }
    if (preview_show) {
        t_text(t, 14, yy, lang ? "--- opened file (Esc = close) ---" : "--- megnyitott fájl (Esc = zárás) ---", C_ACCENT);
        yy += 16;
        /* tartalom */
        int col = 0;
        for (int i = 0; i < preview_len && yy < t->h - 10; i++) {
            char c[2] = { preview[i], 0 };
            if (preview[i] == '\n') { yy += 12; col = 0; continue; }
            if (preview[i] == '\r') continue;
            t_text(t, 12 + col * 8, yy, c, C_TEXT);
            col++;
            if (col >= 56) { yy += 12; col = 0; }
        }
        return;
    }
    t_text(t, 14, yy, "/cd (ez a lemez):", C_ACCENT); yy += 14;
    int idx = 0;
    for (int i = 0; i < cd_n && i < 8; i++, idx++) {
        char b[72];
        ksnprintf(b, sizeof(b), " %s %s (%u B)", (fs_sel == idx && !preview_src) ? ">" : " ", cd_names[i], cd_sizes[i]);
        t_text(t, 12, yy, b, (fs_sel == idx && !preview_src) ? C_AMBER : C_TEXT);
        yy += 13;
    }
    t_text(t, 14, yy, "/mem (RAM-tároló):", C_ACCENT); yy += 14;
    for (int i = 0; i < mem_n && i < 6; i++, idx++) {
        char b[72];
        ksnprintf(b, sizeof(b), " %s %s (%u B)", (fs_sel == idx && preview_src) ? ">" : " ", mem_names[i], mem_sizes[i]);
        t_text(t, 12, yy, b, (fs_sel == idx && preview_src) ? C_AMBER : C_TEXT);
        yy += 13;
    }
    (void)tick;
}

static void draw_panel_rola(texture_t *t, u32 tick) {
    t_fill(t, 0, 0, t->w, t->h, C_PANEL);
    t_rect(t, 0, 0, t->w, t->h, C_BORDER);
    t_fill(t, 0, 0, t->w, 22, 0xFF24313C);
    t_text(t, 8, 7, panels[P_ROLA].nev[lang], C_TEXT);
    t_text(t, t->w - 70, 7, "ROLA", C_ACCENT);
    const char **L = lang ? ABOUT_EN : ABOUT_HU;
    int yy = 36;
    for (int i = 0; L[i] && yy < t->h - 10; i++, yy += 15) {
        t_text(t, 14, yy, L[i], i == 0 ? C_AMBER : C_TEXT);
    }
    (void)tick;
}

typedef void (*drawer_t)(texture_t *, u32);
static drawer_t drawers[PANEL_N] = {
    draw_panel_mag, draw_panel_ora, draw_panel_feladatok,
    draw_panel_jegyzet, draw_panel_lemez, draw_panel_rola
};

/* ---------- fs háttérfolyamat hívások ---------- */
void ui_fs_set_cd(int n, char names[][64], u32 *sizes) {
    cd_n = n;
    for (int i = 0; i < n && i < 16; i++) {
        int j; for (j = 0; j < 63 && names[i][j]; j++) cd_names[i][j] = names[i][j];
        cd_names[i][j] = 0; cd_sizes[i] = sizes[i];
    }
    fs_ready = 1;
}
void ui_fs_set_mem(int n, char names[][64], u32 *sizes) {
    mem_n = n;
    for (int i = 0; i < n && i < 16; i++) {
        int j; for (j = 0; j < 63 && names[i][j]; j++) mem_names[i][j] = names[i][j];
        mem_names[i][j] = 0; mem_sizes[i] = sizes[i];
    }
}
void ui_cd_cache(int idx, const char *buf, int len) {
    if (idx < 0 || idx >= 16) return;
    if (len > 2048) len = 2048;
    for (int k = 0; k < len; k++) cd_data[idx][k] = buf[k];
    cd_dlen[idx] = len;
}
void ui_note_key(u8 ascii, u8 released) { (void)ascii; (void)released; }

/* ---------- UI inicializálás ---------- */
void ui_init(void) {
    const char *nevek[PANEL_N][2] = {
        { "Mag", "Core" }, { "Óra", "Clock" }, { "Feladatok", "Tasks" },
        { "Jegyzet", "Notes" }, { "Lemez", "Disk" }, { "Róla", "About" }
    };
    float alap[PANEL_N]  = { 180, 180, 340, 340, 500, 500 };
    float magy[PANEL_N]  = { 22, 22, 0, 0, -22, -22 };
    for (int i = 0; i < PANEL_N; i++) {
        panels[i].base_r = alap[i];
        panels[i].angle = (i % 2) * PI2 / 2 + (i / 2) * 0.9f;
        panels[i].speed = (i < 2 ? 0.0028f : i < 4 ? 0.0018f : 0.0011f);
        panels[i].grav = 0;
        panels[i].last_focus = 0;
        panels[i].focus_count = 0;
        panels[i].y = magy[i];
        panels[i].bob = (float)i * 1.7f;
        panels[i].nev[0] = nevek[i][0];
        panels[i].nev[1] = nevek[i][1];
        panels[i].tex = texture_new(480, 320);
    }
    /* jegyzet kezdőszöveg */
    const char *wel = lang ? "Type here!" : "Ird ide!";
    while (note_len < 590 && wel[note_len]) { note[note_len] = wel[note_len]; note_len++; }
    selected = 0;
}

/* ---------- bemenet ---------- */
static void type_char(u8 c) {
    if (note_len < 598) { note[note_len++] = (char)c; }
}

static void save_note(void) {
    vfs_write_file("/mem/jegyzet.txt", note, note_len);
    note_flash = tick_count();
    /* mem-lista frissítése */
    mem_n = vfs_list("/mem", mem_names, mem_sizes, mem_flags, 16);
}

static void open_preview(void) {
    int total = cd_n + mem_n;
    if (total <= 0) return;
    int i = fs_sel;
    if (i < cd_n) {
        preview_src = 0; preview_idx = i;
        preview_len = cd_dlen[i];
        for (int k = 0; k < preview_len && k < 1400; k++) preview[k] = cd_data[i][k];
        if (preview_len > 0) preview_show = 1;
    } else {
        int m = i - cd_n;
        if (m < mem_n) {
            char path[80];
            char *pp = path;
            *pp++ = '/'; *pp++ = 'm'; *pp++ = 'e'; *pp++ = 'm'; *pp++ = '/';
            for (int k = 0; k < 60 && mem_names[m][k]; k++) *pp++ = mem_names[m][k];
            *pp = 0;
            preview_src = 1; preview_idx = m;
            if (vfs_read_file(path, preview, 1400, &preview_len)) preview_show = 1;
        }
    }
}

void ui_handle_key(u8 sc, u8 rel, u8 ascii) {
    if (rel) return;
    if (sc == KEY_F1) {
        lang ^= 1;
        /* minden panel azonnal átrajzolódik az új nyelvre */
        for (int i = 0; i < PANEL_N; i++) drawers[i](panels[i].tex, tick_count());
        return;
    }
    if (focus_panel == P_JEGYZET && !preview_show) {
        if (sc == KEY_ESC) { view_target = 0; focus_panel = -1; return; }
        if (sc == KEY_F2) { save_note(); return; }
        if (ascii == '\b') { if (note_len > 0) note_len--; return; }
        if (ascii == '\n') { if (note_len < 598) note[note_len++] = '\n'; return; }
        if (ascii >= 32 && ascii < 127) { type_char(ascii); return; }
        return;
    }
    if (focus_panel == P_LEMEZ && !preview_show) {
        int total = cd_n + mem_n;
        if (sc == KEY_UP && total > 0)   { if (fs_sel > 0) fs_sel--; return; }
        if (sc == KEY_DOWN && total > 0) { if (fs_sel < total - 1) fs_sel++; return; }
        if (sc == KEY_ENTER || ascii == '\n') { open_preview(); return; }
        if (sc == KEY_ESC) { view_target = 0; focus_panel = -1; return; }
        return;
    }
    if (focus_panel >= 0) {
        if (sc == KEY_ESC) { preview_show = 0; view_target = 0; focus_panel = -1; }
        return;
    }
    /* galaxis-nézet */
    switch (sc) {
        case KEY_LEFT:  cam_yaw -= 0.06f; break;
        case KEY_RIGHT: cam_yaw += 0.06f; break;
        case KEY_UP:    cam_pitch += 0.03f; if (cam_pitch > 1.35f) cam_pitch = 1.35f; break;
        case KEY_DOWN:  cam_pitch -= 0.03f; if (cam_pitch < 0.05f) cam_pitch = 0.05f; break;
        case '+': case '=': cam_target_dist -= 60; if (cam_target_dist < 430) cam_target_dist = 430; break;
        case '-': case '_': cam_target_dist += 60; if (cam_target_dist > 1700) cam_target_dist = 1700; break;
        case KEY_TAB: selected = (selected + 1) % PANEL_N; break;
        case KEY_ENTER: case '\n':
            focus_panel = selected;
            panels[selected].focus_count++;
            panels[selected].last_focus = tick_count();
            view_target = 1;
            break;
    }
}

void ui_handle_mouse(int dx, int dy, u8 btn) {
    mx += dx; my += dy;
    if (mx < 0) mx = 0; if (my < 0) my = 0;
    if (mx > (int)gfx_width() - 2) mx = gfx_width() - 2;
    if (my > (int)gfx_height() - 2) my = gfx_height() - 2;
    if (btn & 1) {                 /* húzás = forgatás */
        cam_yaw += dx * 0.006f;
        cam_pitch += dy * 0.004f;
        if (cam_pitch > 1.35f) cam_pitch = 1.35f;
        if (cam_pitch < 0.05f) cam_pitch = 0.05f;
        drag_total += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    }
    if (prev_btn & 1 && !(btn & 1)) {   /* elengedés: kattintás volt? */
        if (drag_total < 6 && focus_panel < 0) {
            /* melyik panelra kattintottunk? */
            /* panel-ütközés: a képernyőn legközelebbi panel */
            float best = 1e9f; int hit = -1;
            for (int i = 0; i < PANEL_N; i++) {
                float sx, sy, d;
                panel_t *p = &panels[i];
                float r = p->base_r * (1.0f - 0.3f * p->grav);
                float px = r * kosz_cosf(p->angle);
                float pz = r * kosz_sinf(p->angle);
                float py = p->y + 18.0f * kosz_sinf((float)(tick_count() % 600) / 95.7f + p->bob);
                if (!r3d_project_point(px, py, pz, &sx, &sy, &d)) continue;
                float half = 240.0f * r3d_focal() / d * 0.5f;
                float halfy = 160.0f * r3d_focal() / d * 0.5f;
                float ddx = (float)mx - sx, ddy = (float)my - sy;
                if (ddx > -half && ddx < half && ddy > -halfy && ddy < halfy) {
                    if (d < best) { best = d; hit = i; }
                }
            }
            if (hit >= 0) {
                selected = hit;
                focus_panel = hit;
                panels[hit].focus_count++;
                panels[hit].last_focus = tick_count();
                view_target = 1;
            }
        }
        drag_total = 0;
    }
    prev_btn = btn;
}

/* ---------- panel szög- és gravitációfrissítés ---------- */
static void update_panels(u32 tick) {
    for (int i = 0; i < PANEL_N; i++) {
        panel_t *p = &panels[i];
        p->angle += p->speed;
        if (p->angle > PI2) p->angle -= PI2;
        /* gravitáció cél: fókusz-szám + frissesség */
        u32 age = tick - p->last_focus;
        float fresh = 0;
        if (age < 3000) fresh = 0.5f * (1.0f - (float)age / 3000.0f);
        float target = 0.25f * (p->focus_count > 4 ? 4 : p->focus_count) / 4.0f + fresh;
        if (target > 1) target = 1;
        p->grav += (target - p->grav) * 0.01f;
    }
}

/* ---------- renderelés ---------- */
static void draw_hud(u32 tick, float sx, float sy, float d) {
    (void)sx; (void)sy; (void)d;
    char b[64];
    /* felső sáv */
    gfx_fill_rect(0, 0, gfx_width(), 20, 0xE6151B22);
    gfx_fill_rect(0, 20, gfx_width(), 1, C_BORDER);
    gfx_draw_text(8, 6, TITLE[lang], C_AMBER, 0xFF000000u);
    unsigned s = tick / 100;
    ksnprintf(b, sizeof(b), "%s | %02u:%02u:%02u | %d task", lang ? "EN" : "HU",
              (s / 3600) % 100, (s / 60) % 60, s % 60, task_count());
    gfx_draw_text(gfx_width() - gfx_text_width(b) - 8, 6, b, C_MUTED, 0xFF000000u);
    /* alsó sáv */
    gfx_fill_rect(0, gfx_height() - 18, gfx_width(), 18, 0xE6151B22);
    gfx_fill_rect(0, gfx_height() - 19, gfx_width(), 1, C_BORDER);
    const char *h = HINT_GAL[lang];
    if (focus_panel == P_JEGYZET && !preview_show) h = HINT_JEGY[lang];
    else if (focus_panel == P_LEMEZ && !preview_show) h = HINT_LEMEZ[lang];
    else if (focus_panel >= 0) h = HINT_FOK[lang];
    gfx_draw_text(8, gfx_height() - 14, h, C_MUTED, 0xFF000000u);
}

static void draw_cursor(void) {
    for (int y = 0; y < 12; y++) {
        u8 row = cursor_bmp[y];
        for (int x = 0; x < 8; x++) {
            if (row & (0x80 >> x)) gfx_pixel(mx + x, my + y, 0xFFF0F0F0);
        }
    }
    gfx_pixel(mx, my + 12, 0xFFF0F0F0);
}

void ui_task(void) {
    u32 last_draw = 0;
    for (;;) {
        u32 tick = tick_count();
        /* bemenet feldolgozása */
        key_event_t ke;
        while (key_pop(&ke)) ui_handle_key(ke.scancode, ke.released, ke.ascii);
        mouse_event_t me;
        while (mouse_pop(&me)) ui_handle_mouse(me.dx, me.dy, me.buttons);

        r3d_camera_update();
        view_blend += (view_target - view_blend) * 0.12f;
        update_panels(tick);

        /* panel-újrarajzolás: 3 másodpercenként mind, fókuszban/kijelölten gyorsan */
        {
            static u32 last_redraw[PANEL_N] = { 0, 0, 0, 0, 0, 0 };
            for (int i = 0; i < PANEL_N; i++) {
                u32 every = (i == focus_panel) ? 10 : (i == selected ? 30 : 250);
                if (tick - last_redraw[i] >= every) {
                    last_redraw[i] = tick;
                    drawers[i](panels[i].tex, tick);
                }
            }
            last_draw = tick;
        }

        /* --- jelenet --- */
        gfx_clear(C_BG);
        r3d_draw_background(tick);

        if (view_blend < 0.98f) {
            /* galaxis réteg */
            r3d_draw_orbit_ring(250, 30, C_ORBIT1, tick);
            r3d_draw_orbit_ring(450, 0, C_ORBIT2, tick);
            r3d_draw_orbit_ring(650, -30, C_ORBIT3, tick);
            r3d_draw_core(tick);
        }

        /* panelek mélység szerint */
        int order[PANEL_N];
        float depth[PANEL_N];
        float sx[PANEL_N], sy[PANEL_N];
        for (int i = 0; i < PANEL_N; i++) {
            panel_t *p = &panels[i];
            float r = p->base_r * (1.0f - 0.3f * p->grav);
            float px = r * kosz_cosf(p->angle);
            float pz = r * kosz_sinf(p->angle);
            float py = p->y + 18.0f * kosz_sinf((float)(tick % 600) / 95.7f + p->bob);
            float dd;
            if (!r3d_project_point(px, py, pz, &sx[i], &sy[i], &dd)) dd = 9999;
            depth[i] = dd;
            order[i] = i;
        }
        for (int a = 0; a < PANEL_N; a++)             /* rendezés távoli -> közeli */
            for (int b2 = a + 1; b2 < PANEL_N; b2++)
                if (depth[order[b2]] > depth[order[a]]) {
                    int tmp = order[a]; order[a] = order[b2]; order[b2] = tmp;
                }

        float blend = view_blend;
        for (int k = 0; k < PANEL_N; k++) {
            int i = order[k];
            panel_t *p = &panels[i];
            float d = depth[i];
            if (d > 5000) continue;
            float halfw = 240.0f * r3d_focal() / d;
            float halfh = 160.0f * r3d_focal() / d;
            float gx0 = sx[i] - halfw, gy0 = sy[i] - halfh;
            float gx1 = sx[i] + halfw, gy1 = sy[i] + halfh;
            /* fókusz: középre nagyítva */
            float fx0 = gfx_width() * 0.5f - 360, fy0 = gfx_height() * 0.5f - 200;
            float fx1 = gfx_width() * 0.5f + 360, fy1 = gfx_height() * 0.5f + 200;
            float is_target = (i == focus_panel) ? 1.0f : 0.0f;
            float m = blend * is_target;
            float x0 = gx0 + (fx0 - gx0) * m, y0 = gy0 + (fy0 - gy0) * m;
            float x1 = gx1 + (fx1 - gx1) * m, y1 = gy1 + (fy1 - gy1) * m;
            if (blend > 0 && is_target == 0 && blend > 0.5f) {
                /* többi panel elhalványul fókuszban */
                continue;
            }
            float vx[4] = { x0, x1, x1, x0 };
            float vy[4] = { y0, y0, y1, y1 };
            gfx_render_texture3d(p->tex, vx, vy, d);

            /* kijelölés keret + névtábla */
            if (i == selected && focus_panel < 0) {
                gfx_rect((int)x0 - 3, (int)y0 - 3, (int)(x1 - x0) + 6, (int)(y1 - y0) + 6, C_AMBER);
            }
            char label[64];
            ksnprintf(label, sizeof(label), "%s", p->nev[lang]);
            int lw = gfx_text_width(label);
            gfx_fill_rect((int)sx[i] - lw / 2 - 4, (int)(sy[i] + halfh) + 2, lw + 8, 14, 0xD010151A);
            gfx_draw_text((int)sx[i] - lw / 2, (int)(sy[i] + halfh) + 5, label,
                          i == selected ? C_AMBER : C_MUTED, 0xFF000000u);
        }

        draw_hud(tick, 0, 0, 0);
        draw_cursor();
        gfx_flip();
        task_yield();
    }
}
