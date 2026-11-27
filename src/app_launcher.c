/* app_launcher.c — 0.5.1 Indítópanel: teljes képernyős, megbízható
 * alkalmazás-indító. Nem keringő ablakokra épül — mindig ugyanott van,
 * ugyanúgy működik. Kategória-váltás (Tab), azonnali keresés (gépelés),
 * nyíl-navigáció, Enter = indítás, egérrel kattintásra is indít.
 * Szép mód: lekerekített kártyák, gradiens, lágy árnyék, élsímított él. */
#include "kernel.h"

/* alkalmazás-leírások (app_list sorrendjében) */
static const char *LEIRAS[2][24] = {
    {
        "Biztos indító: kereséssel",      /* 0 Indítópanel (önmaga) */
        "Rajzolás 24 színnel, alakzatok",    /* 1 Festő */
        "Jegyzetek írása és mentése",  /* 2 Szövegszerkesztő */
        "3D labirintus sugar-menetéssel",           /* 3 Labirintus 3D */
        "Doom-szerű, üldöző MI-szörnyek", /* 4 Pokoljáró */
        "Kereskedés és űrharcok",                   /* 5 Csillagvadász */
        "Városépítés egérrel",       /* 6 Városépítő */
        "Űr-flipper több ütővel",                   /* 7 Űrkadét Flipper */
        "7 animált képernyővédő",                   /* 8 Képernyővédők */
        "Kifejezés-számológép egérrel",             /* 9 Számológép */
        "Hálózatkeresés + WPA2 kapcsolat",          /* 10 Wi-Fi */
        "Beépített BASIC értelmező",               /* 11 Keringő BASIC */
        "Helyi MI — tanítható!",        /* 12 KIRRDA AI */
        "Csillagtérkép, forgatás, nagyítás",       /* 13 Obszervatórium */
        "Mandelbrot-halmaz + részecskék",          /* 14 Keringő Lab */
        "Kitermelő-játék pályákkal",               /* 15 Orbit Miner */
        "ZX81 emulátor (ROM-mal)",  /* 16 ZX81 Retro */
        "4 bites KIMI-MATE gép BASIC-szel",        /* 17 QUARTZ-4B */
        "MBR partíció-szerkesztő + formázó",       /* 18 Partíciókezelő */
        "Telepítés más rendszerek MELLÉ",          /* 19 Telepítő */
        "Duplapaneles kezelő + .KPA",      /* 20 Commander */
        "KRVP1 képernyő + bemenet",       /* 21 Távvezérlő */
    },
    {
        "Reliable launcher: categories + search",
        "24-color painting, shapes",
        "Notes with accents, saving",
        "3D labyrinth, ray-casting",
        "Doom-style shooter, chasing AI",
        "Trading and space combat",
        "City building with mouse",
        "Space pinball, flippers",
        "7 animated screensavers",
        "Expression calculator",
        "Network scan + WPA2 join",
        "Built-in BASIC interpreter",
        "Local AI — trainable!",
        "Star map, rotate and zoom",
        "Mandelbrot set + particles",
        "Mining game with levels",
        "ZX81 emulator (with ROM)",
        "4-bit KIMI-MATE, BASIC",
        "MBR partition editor",
        "Install next to others",
        "Dual-pane files, .KPA",
        "KRVP1 screen + input",
    }
};

static int  kat = 0;               /* 0=mind 1..5 kategória */
static int  sel = 0;               /* kurzor a szűrt listában */
static int  view0 = 0;             /* első látható kártya */
static char srch[26];
static int  slen = 0;
static int  flt[64];               /* szűrt app-indexek */
static int  nflt = 0;

static const char *KAT_NEV[2][6] = {
    { "MIND", "JÁTÉK", "MŰHELY", "RENDSZER", "RETRO", "HÁLÓ" },
    { "ALL", "GAMES", "TOOLS", "SYSTEM", "RETRO", "NET" },
};

static int chr_ci(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
    return a == b;
}

static int contains_ci(const char *s, const char *k) {
    for (int i = 0; s[i]; i++) {
        int j = 0;
        while (k[j] && chr_ci(s[i + j], k[j])) j++;
        if (!k[j]) return 1;
    }
    return 0;
}

static void rebuild(void) {
    nflt = 0;
    for (int i = 0; i < APP_COUNT && nflt < (int)(sizeof(flt) / sizeof(flt[0])); i++) {
        if (i == 0) continue;                     /* önmagát ne listázza */
        /* gépeléskor GLOBÁLISAN keresünk (a kategória nem szűkíti) —
           így bármelyik fülön beírt névre azonnal rá talál */
        if (!slen && kat && app_cat[i] != kat) continue;
        if (slen && !contains_ci(app_list[i].nev[ui_lang()], srch)) continue;
        flt[nflt++] = i;
    }
    if (sel >= nflt) sel = nflt ? nflt - 1 : 0;
    if (sel < 0) sel = 0;
    if (view0 > sel) view0 = sel;
}

void launcher_enter(void)  { kat = 0; sel = 0; view0 = 0; slen = 0; rebuild(); }
void launcher_exit(void)   { }

int launcher_esc(void) {
    if (slen) { slen = 0; sel = 0; view0 = 0; rebuild(); return 1; }
    return 0;                                     /* különben: bezárás */
}

/* kártya kirajzolása */
static void tile(int x, int y, int w, int h, int appi, int is_sel) {
    u32 accent = settings_accent_color();
    if (settings.szep_mod) {
        gfx_shadow_rounded(x + 3, y + 4, w, h, 6, 60);
        gfx_rounded_fill(x, y, w, h, 6, is_sel ? gfx_lerp_color(accent, 0xFF1C2530, 55)
                                               : 0xFF1C2530);
        if (is_sel) gfx_aa_line(x + 10, y + h - 9, x + w - 11, y + h - 9, accent);
        else       gfx_aa_line(x + 10, y + h - 9, x + w - 11, y + h - 9, gfx_lerp_color(accent, 0xFF1C2530, 170));
    } else {
        gfx_fill_rect(x, y, w, h, is_sel ? 0xFF2A3A46 : 0xFF1C2530);
        gfx_rect(x, y, w, h, is_sel ? accent : 0xFF3A5562);
    }
    gfx_draw_text(x + 12, y + 10, app_list[appi].nev[ui_lang()],
                  is_sel ? 0xFFE8B24A : 0xFFF0F0F0, 0xFF000000u);
    gfx_draw_text(x + 12, y + 26, LEIRAS[ui_lang()][appi], 0xFF8FA0AC, 0xFF000000u);
    /* kategória-csipp */
    static const char *TAG[6] = { "", "JÁTÉK", "MŰHELY", "RENDSZER", "RETRO", "HÁLÓ" };
    if (app_cat[appi]) gfx_draw_text(x + 12, y + 40, TAG[app_cat[appi]],
                                     gfx_lerp_color(accent, 0xFF1C2530, 40), 0xFF000000u);
}

void launcher_frame(u32 t) {
    (void)t;
    int W = (int)gfx_width(), H = (int)gfx_height();
    char b[96];
    ksnprintf(b, sizeof(b), ui_lang()
        ? "%d apps | Tab: category  letters: search  Enter: launch"
        : "%d alkalmazás | Tab: kategória  betűk: keresés  Enter: indítás",
        APP_COUNT - 1);
    app_chrome("INDÍTÓPANEL", b, settings_accent_color());
    gfx_fill_rect(0, 23, W, H - 46, 0xFF10151A);   /* teljes háttér */

    /* kategória-sáv */
    int tabw = (W - 16) / 6;
    for (int i = 0; i < 6; i++) {
        int x = 8 + i * tabw;
        u32 accent = settings_accent_color();
        if (i == kat) {
            if (settings.szep_mod) gfx_rounded_fill(x + 1, 26, tabw - 3, 20, 4, accent);
            else   gfx_fill_rect(x + 1, 26, tabw - 3, 20, accent);
            gfx_draw_text(x + 10, 32, KAT_NEV[ui_lang()][i], 0xFF10151A, 0xFF000000u);
        } else {
            if (settings.szep_mod) gfx_rounded_fill(x + 1, 26, tabw - 3, 20, 4, 0xFF24313C);
            else   gfx_fill_rect(x + 1, 26, tabw - 3, 20, 0xFF24313C);
            gfx_draw_text(x + 10, 32, KAT_NEV[ui_lang()][i], 0xFF8FA0AC, 0xFF000000u);
        }
    }

    /* keresősor */
    int y0 = 52;
    ksnprintf(b, sizeof(b), ui_lang() ? "SEARCH: %s_" : "KERESÉS: %s_", srch);
    gfx_draw_text(10, y0, b, slen ? 0xFFE8B24A : 0xFF8FA0AC, 0xFF000000u);
    if (nflt == 0 && slen)
        gfx_draw_text(240, y0, ui_lang() ? "(no match — Backspace, Esc)" : "(nincs találat — Backspace töröl)", 0xFFE86A6A, 0xFF000000u);
    else if (slen) {
        ksnprintf(b, sizeof(b), "%d/%d", nflt, APP_COUNT - 1);
        gfx_draw_text(240, y0, b, 0xFF8FA0AC, 0xFF000000u);
    }

    /* rács */
    int cols = 2, gap = 8;
    int gx = 12, gy = 70;
    int tw = (W - gx * 2 - gap) / cols;
    int th = 64;
    int rows = (H - 30 - gy + gap) / (th + gap);
    if (rows < 1) rows = 1;
    int per = cols * rows;
    /* görgetés: a kurzor mindig látható */
    if (sel < view0) view0 = sel;
    if (sel >= view0 + per) view0 = sel - per + 1;
    if (view0 + per > nflt) view0 = nflt - per;
    if (view0 < 0) view0 = 0;
    for (int i = view0; i < view0 + per && i < nflt; i++) {
        int c = i % cols, r = i / cols;
        tile(gx + c * (tw + gap), gy + r * (th + gap), tw, th, flt[i], i == sel);
    }
    if (view0 + per < nflt) {
        ksnprintf(b, sizeof(b), ui_lang() ? "... %d more (PgDn)" : "... még %d (PgDn)", nflt - view0 - per);
        gfx_draw_text(12, gy + rows * (th + gap) + 2 > H - 30 ? H - 30 : gy + rows * (th + gap) + 2, b, 0xFF8FA0AC, 0xFF000000u);
    }
}

void launcher_key(u8 sc, u8 rel, u8 a) {
    if (rel) return;
    if (sc == KEY_TAB) { kat = (kat + 1) % 6; sel = 0; view0 = 0; rebuild(); return; }
    if (sc == KEY_ENTER) {
        if (nflt > 0 && sel < nflt) app_launch(flt[sel]);
        return;
    }
    if (sc == KEY_UP)    { if (sel >= 2) sel -= 2; else sel = 0; return; }
    if (sc == KEY_DOWN)  { if (sel + 2 < nflt) sel += 2; return; }
    if (sc == KEY_LEFT)  { if (sel > 0) sel--; return; }
    if (sc == KEY_RIGHT) { if (sel < nflt - 1) sel++; return; }
    if (sc == KEY_HOME)  { sel = 0; return; }
    if (sc == KEY_END)   { if (nflt) sel = nflt - 1; return; }
    if (sc == KEY_PGUP) { sel -= 10; if (sel < 0) sel = 0; return; }
    if (sc == KEY_PGDN) { sel += 10; if (sel > nflt - 1) sel = nflt ? nflt - 1 : 0; return; }
    if (sc == 0x0E) {                                  /* Backspace */
        if (slen) { slen--; srch[slen] = 0; sel = 0; view0 = 0; rebuild(); }
        return;
    }
    if (a >= 32 && a < 127 && slen < 24) {             /* gépelés = keresés */
        srch[slen++] = (char)a;
        srch[slen] = 0;
        sel = 0; view0 = 0;
        rebuild();
        return;
    }
}

void launcher_mouse(int mx, int my, int dx, int dy, u8 btn, u8 prev) {
    (void)dx; (void)dy;
    int W = (int)gfx_width(), H = (int)gfx_height();
    /* kattintás feldolgozása */
    int clicked = (btn & 1) && !(prev & 1);
    if (!clicked && !(btn & 1)) return;               /* csak kattintáskor/hovernél */
    /* kategória-fülek */
    if (my >= 26 && my < 46) {
        int tabw = (W - 16) / 6;
        int i = (mx - 8) / tabw;
        if (mx >= 8 && i >= 0 && i < 6) { kat = i; sel = 0; view0 = 0; rebuild(); }
        return;
    }
    /* kártyák */
    int cols = 2, gap = 8, gx = 12, gy = 70;
    int tw = (W - gx * 2 - gap) / cols, th = 64;
    int rows = (H - 30 - gy + gap) / (th + gap);
    if (rows < 1) rows = 1;
    int per = cols * rows;
    if (view0 + per > nflt) { int v = nflt - per; view0 = v > 0 ? v : 0; }
    for (int i = view0; i < view0 + per && i < nflt; i++) {
        int c = i % cols, r = i / cols;
        int x = gx + c * (tw + gap), y = gy + r * (th + gap);
        if (mx >= x && mx < x + tw && my >= y && my < y + th) {
            if (clicked) { app_launch(flt[i]); return; }
            sel = i;                                   /* hover = kiválasztás */
            return;
        }
    }
}
