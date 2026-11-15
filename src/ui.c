/* ui.c — Keringő asztal 0.2: 9 panel, használati gravitáció, kamera, HU/EN,
 * fájlkezelő, frissítés, böngésző, beállítások, ikontémák, háttérképek */
#include "kernel.h"
#include "net.h"
#include "icondata.h"

#define PI2 6.28318531f

/* ---------- nyelv ---------- */
static int lang = 0;   /* 0=HU 1=EN */
void ui_set_lang(int l) { lang = l & 1; settings.lang = lang; }
int  ui_lang(void) { return lang; }

/* ---------- panelek ---------- */
enum { P_MAG, P_ORA, P_FELADATOK, P_JEGYZET, P_LEMEZ, P_ROLA,
       P_FRISSITES, P_BONGESZO, P_BEALLITAS, P_PROGRAMOK, PANEL_N };
typedef struct {
    const char *nev[2];
    int   icon;
    float base_r;
    float angle;
    float speed;
    float grav;
    u32   last_focus;
    u32   focus_count;
    float y;
    float bob;
    texture_t *tex;
} panel_t;

static panel_t panels[PANEL_N];
static int focus_panel = -1;
static int selected = 0;
static float view_blend = 0;
static float view_target = 0;

/* színek */
#define C_BG       0xFF10151A
#define C_PANEL    0xE61C2530
#define C_BORDER   0xFF3A5562
#define C_AMBER    0xFFE8B24A
#define C_TEXT     0xFFF0F0F0
#define C_MUTED    0xFF8FA0AC
#define C_ORBIT1   0xFF2E4A56
#define C_ORBIT2   0xFF2A424E
#define C_ORBIT3   0xFF253A46
static u32 C_ACCENT_DYN;
#define C_ACCENT (C_ACCENT_DYN)

/* ---------- jegyzet ---------- */
static char note[600];
static int  note_len = 0;
static u32  note_flash = 0;

/* ---------- fájlkezelő (egyesített lista) ---------- */
typedef struct { char name[64]; u32 size; u8 isdir; char tag; } fentry_t;
static fentry_t fentries[64];
static int fn = -1;                        /* -1: még nem épült fel */
static int fs_sel = 0;
static int fs_dirty = 1;
static char preview[1600];
static int  preview_len = 0;
static int  preview_show = 0;              /* 0 nincs, 1 szöveg, 2 kép */
static u32 *preview_pix = 0;
static int  preview_w, preview_h;
static char preview_name[64];

void ui_fs_announce(void) { fs_dirty = 1; }

/* ---------- egér ---------- */
static int mx = 640, my = 400;
static u8  prev_btn = 0;
static int drag_total = 0;

/* ---------- URL bevitel ---------- */
static int url_edit = 0;
static char urlbuf[120];
static int url_len = 0;

/* ---------- beállítások panel ---------- */
static int set_sel = 0;

/* gfx.c publikus segédek */
extern u32 utf8_decode_at(const char **s);
extern const u8 *glyph_lookup(u32 cp);

static const u8 cursor_bmp[12] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFC, 0x00, 0x00, 0x00
};

/* ---------- szövegek ---------- */
static const char *TITLE[] = { "KERINGŐ " KERINGO_VERZIO, "ORBIT " KERINGO_VERZIO };
static const char *HINT_GAL[] = {
    "Nyilak: forgatás  +/‑: zoom  Enter: belépés  Tab: következő  F1: nyelv",
    "Arrows: rotate  +/-: zoom  Enter: enter  Tab: next  F1: language" };
static const char *HINT_JEGY[] = {
    "ÍRJ! Backspace töröl, F2 = mentés, Esc = vissza",
    "TYPE! Backspace erases, F2 = save, Esc = back" };
static const char *HINT_LEMEZ[] = {
    "Fel/Le: választás, Enter: megnyitás, B: háttérkép, F5: frissítés, Esc: vissza",
    "Up/Down: select, Enter: open, B: set wallpaper, F5: refresh, Esc: back" };
static const char *HINT_FRISS[] = {
    "U: USB-keresés  H: letöltés  T: telepítés  L: élő frissítés  V: visszaállítás",
    "U: scan USB  H: download  T: install  L: live update  V: rollback" };
static const char *HINT_BROWSER[] = {
    "Fel/Le: görgetés  1‑9: link  F5: újratölt  F6: URL  Esc: vissza",
    "Up/Down: scroll  1-9: links  F5: reload  F6: URL  Esc: back" };
static const char *HINT_BEALL[] = {
    "Fel/Le: tétel  Bal/Jobb: érték  F8: mentés  Esc: vissza",
    "Up/Down: item  Left/Right: value  F8: save  Esc: back" };
static const char *S_SAVED[] = { "MENTVE!", "SAVED!" };
static const char *S_NOCD[]  = { "A háttérfolyamat keresi a lemezeket...", "Background task is looking for disks..." };

static const char *ABOUT_HU[] = {
    "KERINGŐ - a keringő asztal",
    "",
    "A panelek pályákon keringenek a mag körül.",
    "A HASZNÁLAT GRAVITÁCIÓJA húzza őket befelé:",
    "amit sűrűn használsz, közelebb kering.",
    "",
    "0.3 újdonságok: FESTŐ, SZÖVEGSZERKESZTŐ,",
    "Labirintus 3D, Pokoljáró, Csillagvadász,",
    "Városépítő, Űrkadét Flipper, Számológép,",
    "5 képernyővédő, PC-hangszóró.",
    "Nyisd meg a PROGRAMOK panelt!",
    "",
    "Saját kernel, saját grafika, saját fájlrendszer.",
    0 };
static const char *ABOUT_EN[] = {
    "ORBIT - the orbiting desktop",
    "",
    "Panels orbit around the system core.",
    "USAGE GRAVITY pulls them inward:",
    "what you use often, orbits closer.",
    "",
    "New in 0.3: Paint, Text editor, Labyrinth 3D,",
    "Hellwalker, Star hunter, City builder,",
    "Space Cadet Pinball, Calculator, 5 screensavers,",
    "PC speaker sound.",
    "Open the APPS panel!",
    "",
    "Own kernel, own graphics, own filesystems.",
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
static void t_scaled(texture_t *t, u32 *pix, int w, int h, int dx, int dy, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int sy = y * h / dh; if (sy >= h) sy = h - 1;
        for (int x = 0; x < dw; x++) {
            int sx = x * w / dw; if (sx >= w) sx = w - 1;
            int tx = dx + x, ty = dy + y;
            if (tx >= 0 && ty >= 0 && tx < t->w && ty < t->h) t->px[ty * t->w + tx] = pix[sy * w + sx];
        }
    }
}

/* ikon rajzolása a panel fejlécébe */
static void t_icon(texture_t *t, int px, int py, int theme, int icon, u32 color) {
    if (theme < 0) theme = 0;
    if (theme > 2) theme = 2;
    if (icon < 0 || icon > 8) icon = 2;   /* 0.2-es ikonkészlet: 9 ikon */
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 24; x++)
            if (panel_icons[theme][icon][y][x / 8] & (0x80 >> (x % 8))) {
                int xx = px + x, yy = py + y;
                if (xx >= 0 && yy >= 0 && xx < t->w && yy < t->h) t->px[yy * t->w + xx] = color;
            }
}

/* ---------- fájllista felépítése ---------- */
static void fs_build(void) {
    if (!storage_ready_for_ui()) { fn = -1; return; }
    fn = 0;
    char names[16][64]; u32 sizes[16]; u8 flags[16];
    static const char *tags[4] = { "/sys", "/usb", "/cd", "/mem" };
    static const char tagc[4] = { 'S', 'U', 'C', 'M' };
    for (int v = 0; v < 4; v++) {
        int n = vfs_list((char *)tags[v], names, sizes, flags, 16);
        for (int i = 0; i < n && fn < 64; i++) {
            int j;
            for (j = 0; j < 63 && names[i][j]; j++) fentries[fn].name[j] = names[i][j];
            fentries[fn].name[j] = 0;
            fentries[fn].size = sizes[i];
            fentries[fn].isdir = flags[i];
            fentries[fn].tag = tagc[v];
            fn++;
        }
    }
    fs_dirty = 0;
    if (fs_sel >= fn) fs_sel = 0;
}

/* kiterjesztés egyezés */
static int ext_is(const char *name, char a, char b, char c) {
    int L = 0;
    while (name[L]) L++;
    if (L < 5) return 0;
    const char *p = name + L - 4;
    if (p[0] != '.') return 0;
    return (p[1] == a || p[1] == a - 32) && (p[2] == b || p[2] == b - 32)
        && (p[3] == c || p[3] == c - 32);
}

/* net_ip_str local wrapper (kernel.h-s névütközés elkerülésére) */
static void net_ip_str_local(u32 ip, char *out, int outsz) {
    ksnprintf(out, outsz, "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

/* ---------- panel-tartalmak ---------- */
static void panel_head(texture_t *t, int idx, const char *tag) {
    t_fill(t, 0, 0, t->w, t->h, C_PANEL);
    t_rect(t, 0, 0, t->w, t->h, C_BORDER);
    t_fill(t, 0, 0, t->w, 26, 0xFF24313C);
    t_icon(t, 6, 1, settings.icon_theme, panels[idx].icon, C_ACCENT);
    t_text(t, 36, 8, panels[idx].nev[lang], C_TEXT);
    t_text(t, t->w - 10 * 8 - 8, 8, tag, C_ACCENT);
}

static void draw_panel_mag(texture_t *t, u32 tick) {
    panel_head(t, P_MAG, "MAG");
    char b[80];
    int yy = 42;
    unsigned up_s = tick / 100;
    ksnprintf(b, sizeof(b), "%s", lang ? "SYSTEM CORE" : "RENDSZERMAG");
    t_text(t, 14, yy, b, C_AMBER); yy += 20;
    ksnprintf(b, sizeof(b), "Keringo OS %s (i386 sajat mag)", KERINGO_VERZIO);
    t_text(t, 14, yy, b, C_TEXT); yy += 18;
    ksnprintf(b, sizeof(b), "Futásidő: %u.%02u s   Tick: %u", up_s, tick % 100, tick);
    t_text(t, 14, yy, b, C_TEXT); yy += 18;
    ksnprintf(b, sizeof(b), "Taskok: %d   Szabad lap: %u", task_count(), mem_free_pages());
    t_text(t, 14, yy, b, C_TEXT); yy += 18;
    ksnprintf(b, sizeof(b), "Memória: %u KB", mem_total_kb());
    t_text(t, 14, yy, b, C_TEXT); yy += 22;
    /* tárolók */
    ksnprintf(b, sizeof(b), "%s", lang ? "STORAGE" : "TÁROLÓK");
    t_text(t, 14, yy, b, C_AMBER); yy += 18;
    ksnprintf(b, sizeof(b), "  /sys  %s", sys_mounted() ? (lang ? "rendben (HDD)" : "ok (HDD)") : "-");
    t_text(t, 14, yy, b, sys_mounted() ? C_TEXT : C_MUTED); yy += 16;
    ksnprintf(b, sizeof(b), "  /usb  %s", usbfs_mounted() ? (lang ? "pendrájv csatlakozik" : "pendrive attached") : "-");
    t_text(t, 14, yy, b, usbfs_mounted() ? C_TEXT : C_MUTED); yy += 16;
    ksnprintf(b, sizeof(b), "  /cd   %s", iso_mounted() ? (lang ? "ISO9660" : "ISO9660") : "-");
    t_text(t, 14, yy, b, iso_mounted() ? C_TEXT : C_MUTED); yy += 22;
    /* hálózat */
    ksnprintf(b, sizeof(b), "%s", lang ? "NETWORK" : "HÁLÓZAT");
    t_text(t, 14, yy, b, C_AMBER); yy += 18;
    if (net_up()) {
        char ip[20];
        net_ip_str_local(net_self_ip(), ip, sizeof(ip));
        ksnprintf(b, sizeof(b), "  %s @ %s", net_chipname(), ip);
        t_text(t, 14, yy, b, C_TEXT);
    } else {
        ksnprintf(b, sizeof(b), "  %s", lang ? "nincs kapcsolat" : "no connection");
        t_text(t, 14, yy, b, C_MUTED);
    }
}

static void draw_panel_ora(texture_t *t, u32 tick) {
    panel_head(t, P_ORA, "ORA");
    int cx = t->w / 2, cy = t->h / 2 + 14;
    int R = 96;
    for (int i = 0; i < 60; i++) {
        float a = (float)i * PI2 / 60.0f;
        int px = cx + (int)(kosz_cosf(a) * R);
        int py = cy + (int)(kosz_sinf(a) * R);
        int big = (i % 5) == 0;
        t_fill(t, px - (big ? 2 : 1), py - (big ? 2 : 1), big ? 4 : 2, big ? 4 : 2, big ? C_ACCENT : C_MUTED);
    }
    float sec = (float)(tick % 6000) / 100.0f;
    float a = sec / 60.0f * PI2 - PI2 / 4.0f;
    for (int k = 0; k < R - 10; k++) {
        int px = cx + (int)(kosz_cosf(a) * k);
        int py = cy + (int)(kosz_sinf(a) * k);
        t_fill(t, px, py, 2, 2, C_AMBER);
    }
    char b[40];
    unsigned s = tick / 100;
    ksnprintf(b, sizeof(b), "%02u:%02u:%02u", (s / 3600) % 100, (s / 60) % 60, s % 60);
    t_text_big(t, cx - 36, t->h - 64, b, C_TEXT, 2);
}

static void draw_panel_feladatok(texture_t *t, u32 tick) {
    panel_head(t, P_FELADATOK, "TASKOK");
    int yy = 40;
    u32 total = 0;
    for (int i = 0; i < task_count(); i++) total += task_ticks(i);
    if (!total) total = 1;
    for (int i = 0; i < task_count() && i < 7; i++) {
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
    yy += 6;
    t_text(t, 14, yy, lang ? "Panel gravity:" : "Panel gravitáció:", C_MUTED); yy += 16;
    for (int i = 0; i < PANEL_N && yy < t->h - 8; i++) {
        t_text(t, 14, yy, panels[i].nev[lang], C_TEXT);
        int w = (int)(panels[i].grav * 300);
        t_fill(t, 200, yy + 2, 300, 8, 0xFF101820);
        t_fill(t, 200, yy + 2, w, 8, C_AMBER);
        yy += 14;
    }
    (void)tick;
}

static void draw_panel_jegyzet(texture_t *t, u32 tick) {
    panel_head(t, P_JEGYZET, "JEGYZET");
    int yy = 40;
    int col = 0;
    for (int i = 0; i < note_len && yy < t->h - 34; i++) {
        char c[2] = { note[i], 0 };
        t_text(t, 12 + col * 8, yy, c, C_TEXT);
        col++;
        if (col >= 56) { col = 0; yy += 12; }
    }
    if ((tick / 25) % 2 == 0) t_fill(t, 12 + col * 8, yy, 7, 10, C_AMBER);
    if (tick - note_flash < 150) t_text(t, 12, t->h - 22, S_SAVED[lang], C_AMBER);
    else t_text(t, 12, t->h - 22, lang ? "F2 = save (/sys)" : "F2 = mentés (/sys)", C_MUTED);
}

static void draw_panel_lemez(texture_t *t, u32 tick) {
    panel_head(t, P_LEMEZ, "LEMEZ");
    int yy = 36;
    if (fs_dirty) fs_build();
    if (fn < 0) { t_text(t, 14, yy, storage_ready_for_ui() ? S_NOCD[lang] : S_NOCD[lang], C_MUTED); return; }
    if (preview_show == 1) {
        char b[80];
        ksnprintf(b, sizeof(b), "%s (%u B) - Esc: bezaras", preview_name, preview_len);
        t_text(t, 14, yy, b, C_ACCENT); yy += 16;
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
    if (preview_show == 2 && preview_pix) {
        char b[80];
        ksnprintf(b, sizeof(b), "%s (%dx%d)  B = %s", preview_name, preview_w, preview_h,
                  lang ? "hatterkep" : "háttérkép");
        t_text(t, 14, yy, b, C_ACCENT);
        t_scaled(t, preview_pix, preview_w, preview_h, 10, 50, t->w - 20, t->h - 70);
        return;
    }
    /* fejléc: melyik tárolók élnek */
    char st[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    int si = 0;
    if (sys_mounted()) st[si++] = 'S';
    if (usbfs_mounted()) st[si++] = 'U';
    if (iso_mounted()) st[si++] = 'C';
    st[si++] = 'M';
    char b[72];
    ksnprintf(b, sizeof(b), "%s: %s", lang ? "STORES" : "TÁROLÓK", st);
    t_text(t, 14, yy, b, C_ACCENT); yy += 15;
    int start = fs_sel - 13;
    if (start < 0) start = 0;
    for (int i = start; i < fn && i < start + 15; i++) {
        ksnprintf(b, sizeof(b), "%c %s %s (%u B)", fentries[i].tag,
                  (fs_sel == i) ? ">" : " ", fentries[i].name, fentries[i].size);
        t_text(t, 12, yy, b, (fs_sel == i) ? C_AMBER : C_TEXT);
        yy += 15;
    }
    (void)tick;
}

static void draw_panel_rola(texture_t *t, u32 tick) {
    panel_head(t, P_ROLA, "ROLA");
    const char **L = lang ? ABOUT_EN : ABOUT_HU;
    int yy = 38;
    for (int i = 0; L[i] && yy < t->h - 10; i++, yy += 15) {
        t_text(t, 14, yy, L[i], i == 0 ? C_AMBER : C_TEXT);
    }
    (void)tick;
}

static void draw_panel_frissites(texture_t *t, u32 tick) {
    panel_head(t, P_FRISSITES, "FRISS");
    char b[80];
    int yy = 40;
    ksnprintf(b, sizeof(b), "%s: Keringo OS %s", lang ? "Current" : "Jelenlegi", KERINGO_VERZIO);
    t_text(t, 14, yy, b, C_TEXT); yy += 20;
    const char *v = update_version();
    if (v) {
        int newer = update_available_newer();
        ksnprintf(b, sizeof(b), "%s: %s %s", lang ? "Found" : "Talált", v,
                  newer ? (lang ? "(NEW!)" : "(ÚJ!)") : "");
        t_text(t, 14, yy, b, newer ? C_AMBER : C_MUTED); yy += 18;
        ksnprintf(b, sizeof(b), "%s: %d", lang ? "Files" : "Fájlok", update_nfiles());
        t_text(t, 14, yy, b, C_TEXT); yy += 16;
        for (int i = 0; i < update_nfiles() && i < 5; i++) {
            ksnprintf(b, sizeof(b), "  %s (%u B)", update_fname(i), update_fsize(i));
            t_text(t, 14, yy, b, C_MUTED); yy += 14;
        }
        yy += 4;
        if (!sys_mounted()) {
            t_text(t, 14, yy, lang ? "! no /sys partition" : "! nincs /sys partíció", 0xFFE86A6A); yy += 16;
        }
    } else {
        t_text(t, 14, yy, lang ? "No update package yet." : "Nincs frissítési csomag.", C_MUTED); yy += 16;
        t_text(t, 14, yy, lang ? "Put KERINGO.UPD on a FAT32" : "Tegyél KERINGO.UPD-ot egy FAT32", C_MUTED); yy += 14;
        t_text(t, 14, yy, lang ? "pendrive, or download from net." : "pendrájvra, vagy töltsd le hálózatról.", C_MUTED); yy += 20;
    }
    ksnprintf(b, sizeof(b), "%s", update_status());
    t_text(t, 14, yy, b, C_ACCENT); yy += 18;
    if ((tick / 30) % 2 == 0 && v && update_available_newer()) {
        t_text(t, 14, t->h - 40, lang ? "T = install, L = live update!" : "T = install, L = live update!", C_AMBER);
    }
    t_text(t, 14, t->h - 22, lang ? "U H T L V" : "U H T L V", C_MUTED);
}

static void draw_panel_bongeszo(texture_t *t, u32 tick) {
    panel_head(t, P_BONGESZO, "BONGESZO");
    if (url_edit) {
        char b[64];
        ksnprintf(b, sizeof(b), "URL: %s_", urlbuf);
        t_text(t, 14, 40, b, C_AMBER);
        t_text(t, 14, 56, lang ? "Enter: mehet  Esc: mégse" : "Enter: go  Esc: cancel", C_MUTED);
        return;
    }
    browser_render(t, tick, focus_panel == P_BONGESZO);
}

static const char *SET_LABELS[2][6] = {
    { "Háttérkép", "Fotó fájlja", "Ikontéma", "Kiemelő szín", "Nyelv / Language", "Frissítési URL" },
    { "Wallpaper", "Photo file", "Icon theme", "Accent color", "Language / Nyelv", "Update URL" },
};

static void draw_panel_beallitas(texture_t *t, u32 tick) {
    panel_head(t, P_BEALLITAS, "BEALL");
    int yy = 42;
    char b[80];
    for (int i = 0; i < 6; i++) {
        const char *val = "";
        switch (i) {
            case 0: val = settings_wp_names(settings.wallpaper); break;
            case 1: val = settings.wp_file; break;
            case 2: val = settings_icon_names(settings.icon_theme); break;
            case 3: {
                static char cbuf[2] = { 0, 0 };
                cbuf[0] = (char)('0' + settings.accent);
                val = cbuf;
                break;
            }
            case 4: val = settings.lang ? "EN" : "HU"; break;
            case 5: val = settings.update_url; break;
        }
        ksnprintf(b, sizeof(b), "%s %s: %s", (set_sel == i) ? ">" : " ", SET_LABELS[lang][i], val);
        t_text(t, 12, yy, b, (set_sel == i) ? C_AMBER : C_TEXT);
        yy += 18;
    }
    yy += 8;
    t_text(t, 12, yy, lang ? "F8 = save settings" : "F8 = beállítások mentése", C_MUTED); yy += 16;
    /* szín-előnézet */
    for (int i = 0; i < 6; i++) {
        static const u32 pal[6] = { 0xFF248BBE, 0xFFE8B24A, 0xFF62D26F, 0xFFE86A6A, 0xFFB07AE8, 0xFF5AD2C8 };
        t_fill(t, 12 + i * 32, yy, 28, 16, pal[i]);
        if (settings.accent == i) t_rect(t, 12 + i * 32, yy, 28, 16, C_TEXT);
    }
}

/* ---------- Programok panel (0.3 alkalmazásinditó) ---------- */
static int app_sel = 0;
static const char *HINT_PROG[] = {
    "Fel/Le: választás  Enter: indítás  1-9: gyorsindítás  Esc: vissza",
    "Up/Down: select  Enter: launch  1-9: quick launch  Esc: back"
};

static void draw_panel_programok(texture_t *t, u32 tick) {
    panel_head(t, P_PROGRAMOK, "APPS");
    char b[80];
    int yy = 40;
    t_text(t, 14, yy, lang ? "APPLICATIONS" : "ALKALMAZÁSOK", C_AMBER); yy += 20;
    for (int i = 0; i < APP_COUNT && yy < t->h - 66; i++) {
        ksnprintf(b, sizeof(b), "%s %s", (app_sel == i) ? ">" : " ", app_list[i].nev[lang]);
        t_text(t, 14, yy, b, (app_sel == i) ? C_AMBER : C_TEXT);
        /* gyorsgomb jelzés */
        const char *hk = 0;
        for (int k = 0; k < APP_HOTKEYS; k++)
            if (app_hotkey_app[k] == i) {
                hk = (app_hotkey_key[k] == KEY_F3) ? "F3" : (app_hotkey_key[k] == KEY_F4) ? "F4"
                   : (app_hotkey_key[k] == KEY_F7) ? "F7" : (app_hotkey_key[k] == KEY_F9) ? "F9"
                   : (app_hotkey_key[k] == KEY_F10) ? "F10" : (app_hotkey_key[k] == KEY_F11) ? "F11"
                   : (app_hotkey_key[k] == KEY_F12) ? "F12" : "";
            }
        if (hk) t_text(t, 400, yy, hk, C_MUTED);
        yy += 18;
    }
    yy += 8;
    t_text(t, 14, yy, lang ? "Galaxy: F3 Paint F4 Text F7 Labirintus" : "Galaxisban: F3 Paint F4 Szöveg F7 Labirintus", C_MUTED); yy += 15;
    t_text(t, 14, yy, lang ? "F9 Doom F10 Elite F11 City F12 Pinball" : "F9 Pokoljáró F10 Csillagvadász F11 Város F12 Flipper", C_MUTED);
    t_text(t, 12, t->h - 22, HINT_PROG[lang], C_MUTED);
    (void)tick;
}

typedef void (*drawer_t)(texture_t *, u32);
static drawer_t drawers[PANEL_N] = {
    draw_panel_mag, draw_panel_ora, draw_panel_feladatok,
    draw_panel_jegyzet, draw_panel_lemez, draw_panel_rola,
    draw_panel_frissites, draw_panel_bongeszo, draw_panel_beallitas,
    draw_panel_programok
};

/* ---------- fájl megnyitása ---------- */
static void open_preview(void) {
    if (fn <= 0 || fs_sel >= fn || !storage_ready_for_ui()) return;
    fentry_t *e = &fentries[fs_sel];
    char path[96];
    ksnprintf(path, sizeof(path), "/%c%c%c%c/%s",
              e->tag == 'S' ? 's' : e->tag == 'U' ? 'u' : e->tag == 'C' ? 'c' : 'm',
              e->tag == 'S' ? 'y' : e->tag == 'U' ? 's' : e->tag == 'C' ? 'd' : 'e',
              e->tag == 'S' ? 's' : e->tag == 'U' ? 'b' : 'd',
              e->tag == 'S' ? 0 : 'm', e->name);
    if (e->tag == 'M') {
        ksnprintf(path, sizeof(path), "/mem/%s", e->name);
    } else if (e->tag == 'S') {
        ksnprintf(path, sizeof(path), "/sys/%s", e->name);
    } else if (e->tag == 'U') {
        ksnprintf(path, sizeof(path), "/usb/%s", e->name);
    } else {
        ksnprintf(path, sizeof(path), "/cd/%s", e->name);
    }
    for (int k = 0; k < 63 && e->name[k]; k++) preview_name[k] = e->name[k];
    preview_name[63] = 0;

    /* kép? */
    if (ext_is(e->name, 'j', 'p', 'g') || ext_is(e->name, 'b', 'm', 'p')) {
        static u8 ibuf[400 * 1024];
        int outlen = 0;
        if (vfs_read_file(path, (char *)ibuf, sizeof(ibuf), &outlen) && outlen > 0) {
            u32 *pix; int w, h;
            if (jpeg_decode(ibuf, (u32)outlen, &pix, &w, &h)
                || bmp_decode(ibuf, (u32)outlen, &pix, &w, &h)) {
                preview_pix = pix; preview_w = w; preview_h = h;
                preview_show = 2;
                return;
            }
        }
        return;
    }
    /* szöveg? */
    int outlen = 0;
    if (vfs_read_file(path, preview, 1500, &outlen) && outlen > 0) {
        preview_len = outlen;
        preview_show = 1;
    }
}

/* kép beállítása háttérként */
static void set_wallpaper_from_preview(void) {
    if (preview_show != 2 || !preview_pix) return;
    /* háttér: fájl útvonala alapján; a beállítás nevét mentjük */
    settings.wallpaper = 3;
    int i;
    for (i = 0; i < 63 && preview_name[i]; i++) settings.wp_file[i] = preview_name[i];
    settings.wp_file[i] = 0;
    wallpaper_apply();
    if (!wallpaper_active()) {
        /* a fájl más tárolón van: másoljuk /mem-be? — egyszerűbb: közvetlen dekódolás megtörtént */
        settings.wallpaper = 0;
    }
    settings_save();
}

/* ---------- bemenet ---------- */
static void type_char(u8 c) {
    if (note_len < 598) note[note_len++] = (char)c;
}

static void save_note(void) {
    vfs_write_file("/sys/jegyzet.txt", note, note_len);
    settings_note_save(note, note_len);
    note_flash = tick_count();
    if (fs_dirty == 0) fs_dirty = 1;
}

void ui_handle_key(u8 sc, u8 rel, u8 ascii) {
    if (rel) return;
    C_ACCENT_DYN = settings_accent_color();
    if (sc == KEY_F1) {
        lang ^= 1;
        settings.lang = lang;
        return;
    }
    /* ---- 0.3: futó alkalmazás ---- */
    if (app_active() >= 0) {
        app_key_event(sc, rel, ascii);
        return;
    }
    /* ---- 0.3: alkalmazás gyorsbillentyűk (minden nézetben) ---- */
    for (int k = 0; k < APP_HOTKEYS; k++) {
        if (sc == app_hotkey_key[k]) { app_launch(app_hotkey_app[k]); return; }
    }
    /* ---- 0.3: futó képernyővédő: bármi leállítja ---- */
    if (saver_running()) {
        saver_stop();
        return;
    }
    /* URL beviteli mód (böngészőben) */
    if (url_edit) {
        if (sc == KEY_ESC) { url_edit = 0; return; }
        if (sc == KEY_ENTER || ascii == '\n') {
            url_edit = 0;
            urlbuf[url_len] = 0;
            if (url_len > 7) browser_open(urlbuf);
            return;
        }
        if (ascii == '\b') { if (url_len > 0) url_len--; return; }
        if (ascii >= 32 && ascii < 127 && url_len < 118) { urlbuf[url_len++] = ascii; return; }
        return;
    }
    /* ---- Jegyzet ---- */
    if (focus_panel == P_JEGYZET && !preview_show) {
        if (sc == KEY_ESC) { view_target = 0; focus_panel = -1; return; }
        if (sc == KEY_F2) { save_note(); return; }
        if (ascii == '\b') { if (note_len > 0) note_len--; return; }
        if (ascii == '\n') { if (note_len < 598) note[note_len++] = '\n'; return; }
        if (ascii >= 32 && ascii < 127) { type_char(ascii); return; }
        return;
    }
    /* ---- Lemez ---- */
    if (focus_panel == P_LEMEZ) {
        if (preview_show) {
            if (sc == KEY_ESC) { preview_show = 0; return; }
            if (ascii == 'b' || ascii == 'B') { set_wallpaper_from_preview(); return; }
            return;
        }
        if (sc == KEY_ESC) { view_target = 0; focus_panel = -1; return; }
        if (fs_dirty) fs_build();
        if (sc == KEY_UP && fn > 0)   { if (fs_sel > 0) fs_sel--; return; }
        if (sc == KEY_DOWN && fn > 0) { if (fs_sel < fn - 1) fs_sel++; return; }
        if (sc == KEY_F5) { fs_dirty = 1; fs_build(); return; }
        if (sc == KEY_ENTER || ascii == '\n') { open_preview(); return; }
        return;
    }
    /* ---- Frissítés ---- */
    if (focus_panel == P_FRISSITES) {
        if (sc == KEY_ESC) { view_target = 0; focus_panel = -1; return; }
        if (!storage_ready_for_ui()) return;
        if (ascii == 'u' || ascii == 'U') { update_scan_usb(); return; }
        if (ascii == 'h' || ascii == 'H') { update_scan_http(settings.update_url); return; }
        if (ascii == 't' || ascii == 'T') { update_apply(0); return; }
        if (ascii == 'l' || ascii == 'L') { update_apply(1); return; }
        if (ascii == 'v' || ascii == 'V') { update_rollback(); return; }
        return;
    }
    /* ---- Böngésző ---- */
    if (focus_panel == P_BONGESZO) {
        if (sc == KEY_ESC) { view_target = 0; focus_panel = -1; return; }
        if (sc == KEY_F5) { browser_open(browser_url()); return; }
        if (sc == KEY_F6) { url_edit = 1; url_len = 0; urlbuf[0] = 0; return; }
        if (sc == KEY_UP)   { browser_scroll(-1); return; }
        if (sc == KEY_DOWN) { browser_scroll(1); return; }
        if (sc == KEY_PGUP) { browser_scroll(-10); return; }
        if (sc == KEY_PGDN) { browser_scroll(10); return; }
        if (ascii >= '1' && ascii <= '9') { browser_follow_link(ascii - '0'); return; }
        return;
    }
    /* ---- Beállítások ---- */
    if (focus_panel == P_BEALLITAS) {
        if (sc == KEY_ESC) { view_target = 0; focus_panel = -1; return; }
        if (sc == KEY_UP)   { if (set_sel > 0) set_sel--; return; }
        if (sc == KEY_DOWN) { if (set_sel < 5) set_sel++; return; }
        if (sc == KEY_F8) { settings_save(); note_flash = tick_count(); return; }
        int delta = (sc == KEY_LEFT) ? -1 : (sc == KEY_RIGHT) ? 1 : 0;
        if (delta) {
            switch (set_sel) {
                case 0:
                    settings.wallpaper += delta;
                    if (settings.wallpaper < 0) settings.wallpaper = 3;
                    if (settings.wallpaper > 3) settings.wallpaper = 0;
                    wallpaper_apply();
                    if (!wallpaper_active() && settings.wallpaper == 3) settings.wallpaper = 2, wallpaper_apply();
                    break;
                case 1:
                    /* fotó: a fájlböngészőből választható (B billentyű) */
                    break;
                case 2:
                    settings.icon_theme += delta;
                    if (settings.icon_theme < 0) settings.icon_theme = 2;
                    if (settings.icon_theme > 2) settings.icon_theme = 0;
                    break;
                case 3:
                    settings.accent += delta;
                    if (settings.accent < 0) settings.accent = 5;
                    if (settings.accent > 5) settings.accent = 0;
                    C_ACCENT_DYN = settings_accent_color();
                    break;
                case 4:
                    lang ^= 1;
                    settings.lang = lang;
                    break;
            }
            return;
        }
        return;
    }
    /* ---- Programok (0.3 alkalmazásinditó) ---- */
    if (focus_panel == P_PROGRAMOK) {
        if (sc == KEY_ESC) { view_target = 0; focus_panel = -1; return; }
        if (sc == KEY_UP)   { if (app_sel > 0) app_sel--; return; }
        if (sc == KEY_DOWN) { if (app_sel < APP_COUNT - 1) app_sel++; return; }
        if (sc == KEY_ENTER) { app_launch(app_sel); return; }
        if (ascii >= '1' && ascii <= '9') {
            int idx = ascii - '1';
            if (idx < APP_COUNT) app_launch(idx);
            return;
        }
        return;
    }
    if (focus_panel >= 0) {
        if (sc == KEY_ESC) { preview_show = 0; view_target = 0; focus_panel = -1; }
        return;
    }
    /* ---- galaxis-nézet ---- */
    /* számjegy: alkalmazás indítása (1-9) */
    if (ascii >= '1' && ascii <= '9') {
        int idx = ascii - '1';
        if (idx < APP_COUNT) { app_launch(idx); return; }
    }
    switch (sc) {
        case KEY_LEFT:  cam_yaw -= 0.06f; break;
        case KEY_RIGHT: cam_yaw += 0.06f; break;
        case KEY_UP:    cam_pitch += 0.03f; if (cam_pitch > 1.35f) cam_pitch = 1.35f; break;
        case KEY_DOWN:  cam_pitch -= 0.03f; if (cam_pitch < 0.05f) cam_pitch = 0.05f; break;
        case '+': case '=': cam_target_dist -= 60; if (cam_target_dist < 430) cam_target_dist = 430; break;
        case '-': case '_': cam_target_dist += 60; if (cam_target_dist > 1700) cam_target_dist = 1700; break;
        case KEY_TAB: selected = (selected + 1) % PANEL_N; break;
        case KEY_ENTER: case '\n':
            if (selected == P_FRISSITES) update_scan_usb();
            if (selected == P_LEMEZ) { fs_dirty = 1; }
            focus_panel = selected;
            panels[selected].focus_count++;
            panels[selected].last_focus = tick_count();
            view_target = 1;
            break;
    }
}

void ui_handle_mouse(int dx, int dy, u8 btn) {
    /* 0.3: futó alkalmazás kapja a bemenetet */
    if (app_active() >= 0) {
        app_mouse_event(dx, dy, btn);
        return;
    }
    if (saver_running() && (dx || dy || btn)) {
        saver_stop();
        return;
    }
    mx += dx; my += dy;
    if (mx < 0) mx = 0; if (my < 0) my = 0;
    if (mx > (int)gfx_width() - 2) mx = gfx_width() - 2;
    if (my > (int)gfx_height() - 2) my = gfx_height() - 2;
    if (btn & 1) {
        cam_yaw += dx * 0.006f;
        cam_pitch += dy * 0.004f;
        if (cam_pitch > 1.35f) cam_pitch = 1.35f;
        if (cam_pitch < 0.05f) cam_pitch = 0.05f;
        drag_total += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    }
    if (prev_btn & 1 && !(btn & 1)) {
        if (drag_total < 6 && focus_panel < 0) {
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

/* ---------- gravitáció ---------- */
static void update_panels(u32 tick) {
    for (int i = 0; i < PANEL_N; i++) {
        panel_t *p = &panels[i];
        p->angle += p->speed;
        if (p->angle > PI2) p->angle -= PI2;
        u32 age = tick - p->last_focus;
        float fresh = 0;
        if (age < 3000) fresh = 0.5f * (1.0f - (float)age / 3000.0f);
        float target = 0.25f * (p->focus_count > 4 ? 4 : p->focus_count) / 4.0f + fresh;
        if (target > 1) target = 1;
        p->grav += (target - p->grav) * 0.01f;
    }
}

/* ---------- HUD ---------- */
static void draw_hud(u32 tick) {
    char b[96];
    gfx_fill_rect(0, 0, gfx_width(), 20, 0xE6151B22);
    gfx_fill_rect(0, 20, gfx_width(), 1, C_BORDER);
    gfx_draw_text(8, 6, TITLE[lang], C_AMBER, 0xFF000000u);
    /* állapot-jelzők */
    int x = 240;
    gfx_draw_text(x, 6, usbfs_mounted() ? "[USB]" : "[usb]", usbfs_mounted() ? C_ACCENT_DYN : C_MUTED, 0xFF000000u);
    x += 54;
    gfx_draw_text(x, 6, net_up() ? "[NET]" : "[net]", net_up() ? C_ACCENT_DYN : C_MUTED, 0xFF000000u);
    x += 54;
    gfx_draw_text(x, 6, sys_mounted() ? "[SYS]" : "[sys]", sys_mounted() ? C_ACCENT_DYN : C_MUTED, 0xFF000000u);
    x += 54;
    if (update_available_newer() && (tick / 30) % 2 == 0) {
        gfx_draw_text(x, 6, lang ? "[UPDATE!]" : "[FRISSITES!]", C_AMBER, 0xFF000000u);
    }
    unsigned s = tick / 100;
    ksnprintf(b, sizeof(b), "%s | %02u:%02u:%02u | %d task", lang ? "EN" : "HU",
              (s / 3600) % 100, (s / 60) % 60, s % 60, task_count());
    gfx_draw_text(gfx_width() - gfx_text_width(b) - 8, 6, b, C_MUTED, 0xFF000000u);
    /* alsó sáv */
    gfx_fill_rect(0, gfx_height() - 18, gfx_width(), 18, 0xE6151B22);
    gfx_fill_rect(0, gfx_height() - 19, gfx_width(), 1, C_BORDER);
    const char *h = HINT_GAL[lang];
    if (url_edit) h = lang ? "URL: Enter = mehet, Esc = mégse" : "URL: Enter = go, Esc = cancel";
    else if (focus_panel == P_JEGYZET) h = HINT_JEGY[lang];
    else if (focus_panel == P_LEMEZ) h = preview_show == 2 ? (lang ? "B = set as wallpaper, Esc = back" : "B = háttérképnek, Esc = vissza") : HINT_LEMEZ[lang];
    else if (focus_panel == P_FRISSITES) h = HINT_FRISS[lang];
    else if (focus_panel == P_BONGESZO) h = HINT_BROWSER[lang];
    else if (focus_panel == P_BEALLITAS) h = HINT_BEALL[lang];
    else if (focus_panel == P_PROGRAMOK) h = HINT_PROG[lang];
    else if (focus_panel >= 0) h = lang ? "Esc: back to galaxy" : "Esc: vissza a galaxisba";
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

/* ---------- UI inicializálás ---------- */
void ui_settings_reloaded(void) {
    /* a beállítások betöltése után hívódik (lemez task) */
    C_ACCENT_DYN = settings_accent_color();
    lang = settings.lang & 1;
    wallpaper_apply();
    /* jegyzet visszatöltése, ha eddig alap volt */
    static int loaded_once = 0;
    if (!loaded_once) {
        int nl = settings_note_load(note, 590);
        if (nl > 0) note_len = nl;
        loaded_once = 1;
    }
    fs_dirty = 1;
}

void ui_init(void) {
    const char *nevek[PANEL_N][2] = {
        { "Mag", "Core" }, { "Óra", "Clock" }, { "Feladatok", "Tasks" },
        { "Jegyzet", "Notes" }, { "Lemez", "Disk" }, { "Róla", "About" },
        { "Frissítés", "Update" }, { "Böngésző", "Browser" }, { "Beállítás", "Settings" },
        { "Programok", "Apps" }
    };
    const int ikonok[PANEL_N] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 2 };
    float alap[PANEL_N] = { 190, 190, 350, 350, 510, 510, 350, 510, 190, 510 };
    float magy[PANEL_N] = { 24, 24, 0, 0, -24, -24, 24, 0, 0, -24 };
    C_ACCENT_DYN = settings_accent_color();
    lang = settings.lang & 1;
    for (int i = 0; i < PANEL_N; i++) {
        panels[i].base_r = alap[i];
        panels[i].icon = ikonok[i];
        panels[i].angle = (float)i * PI2 / (float)PANEL_N + 0.35f;
        panels[i].speed = 0.0011f + 0.0004f * (float)((i * 7) % 3);
        panels[i].grav = 0;
        panels[i].last_focus = 0;
        panels[i].focus_count = 0;
        panels[i].y = magy[i];
        panels[i].bob = (float)i * 1.7f;
        panels[i].nev[0] = nevek[i][0];
        panels[i].nev[1] = nevek[i][1];
        panels[i].tex = texture_new(480, 320);
    }
    /* jegyzet: mentett szöveg visszatöltése */
    int nl = settings_note_load(note, 590);
    if (nl > 0) note_len = nl;
    else {
        const char *wel = lang ? "Type here!" : "Ird ide!";
        while (note_len < 590 && wel[note_len]) { note[note_len] = wel[note_len]; note_len++; }
    }
    browser_init();
    selected = 0;
    wallpaper_apply();
}

/* ---------- fõ ciklus ---------- */
void ui_task(void) {
    u32 last_input = tick_count();
    for (;;) {
        u32 tick = tick_count();
        key_event_t ke;
        while (key_pop(&ke)) {
            last_input = tick;
            ui_handle_key(ke.scancode, ke.released, ke.ascii);
        }
        mouse_event_t me;
        while (mouse_pop(&me)) {
            last_input = tick;
            ui_handle_mouse(me.dx, me.dy, me.buttons);
        }

        /* hálózati stream életben tartása + hang */
        net_poll();
        browser_stream_poll();
        sound_poll();

        /* ---- 0.3: futó alkalmazás ---- */
        if (app_active() >= 0) {
            app_frame(tick);
            gfx_flip();
            task_yield();
            continue;
        }

        /* ---- 0.3: tétlen képernyővédő (90 s) ---- */
        if (!saver_running() && focus_panel < 0 && !url_edit
            && (s32)(tick - last_input) > 9000) {
            saver_start(-1);
        }
        if (saver_running()) {
            saver_frame(tick);
            const char *w = lang ? "Keringo OS - press any key" : "Keringő OS - nyomj meg egy gombot";
            gfx_draw_text((gfx_width() - gfx_text_width(w)) / 2, gfx_height() - 30, w, 0xFF5A7080, 0xFF000000u);
            gfx_flip();
            task_yield();
            continue;
        }

        r3d_camera_update();
        view_blend += (view_target - view_blend) * 0.12f;
        update_panels(tick);

        /* panel-újrarajzolás */
        {
            static u32 last_redraw[PANEL_N] = { 0 };
            for (int i = 0; i < PANEL_N; i++) {
                u32 every = (i == focus_panel) ? 10 : (i == selected ? 30 : 250);
                if (i == P_BONGESZO && focus_panel == P_BONGESZO) every = 10;
                if (tick - last_redraw[i] >= every) {
                    last_redraw[i] = tick;
                    drawers[i](panels[i].tex, tick);
                }
            }
        }

        /* --- jelenet --- */
        if (wallpaper_active()) {
            wallpaper_blit();
        } else {
            gfx_clear(C_BG);
            r3d_draw_background(tick);
        }

        if (view_blend < 0.98f) {
            r3d_draw_orbit_ring(250, 30, C_ORBIT1, tick);
            r3d_draw_orbit_ring(450, 0, C_ORBIT2, tick);
            r3d_draw_orbit_ring(650, -30, C_ORBIT3, tick);
            r3d_draw_core(tick);
        }

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
        for (int a = 0; a < PANEL_N; a++)
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
            float fx0 = gfx_width() * 0.5f - 360, fy0 = gfx_height() * 0.5f - 200;
            float fx1 = gfx_width() * 0.5f + 360, fy1 = gfx_height() * 0.5f + 200;
            float is_target = (i == focus_panel) ? 1.0f : 0.0f;
            float m = blend * is_target;
            float x0 = gx0 + (fx0 - gx0) * m, y0 = gy0 + (fy0 - gy0) * m;
            float x1 = gx1 + (fx1 - gx1) * m, y1 = gy1 + (fy1 - gy1) * m;
            if (blend > 0 && is_target == 0 && blend > 0.5f) continue;
            float vx[4] = { x0, x1, x1, x0 };
            float vy[4] = { y0, y0, y1, y1 };
            gfx_render_texture3d(p->tex, vx, vy, d);

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

        draw_hud(tick);
        draw_cursor();
        gfx_flip();
        task_yield();
    }
}
