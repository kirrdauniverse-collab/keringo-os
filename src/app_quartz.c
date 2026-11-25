/* app_quartz.c — QUARTZ-4B (KIMI-MATE) alkalmazás-burkoló a Keringő OS-ben.
 * CRT-TV-kinézet, zöld foszfor, szkennelt-sor hatás.
 * Szerkesztő: bal/jobb nyíl = kurzor a sorban, fel/le nyíl = korábbi parancsok
 * (előzmény-visszahívás — pont, amit a KIMI-MATE-ben hiányoltál!), F1 = súgó. */
#include "kernel.h"
#include "quartz4.h"

static quartz4_t q4;

/* ---- beviteli sor + előzmények ---- */
#define QH_MAX 16
static char hist[QH_MAX][80];
static int  hist_len[QH_MAX];
static int  hist_n = 0;          /* eltárolt parancsok száma */
static int  hsel = -1;           /* előzmény-kurzor (-1: friss sor) */
static char iline[80];
static int  ilen = 0, icur = 0;  /* bemenet + kurzor pozíció */
static int  irow = 3;            /* melyik szövegsoron áll a beviteli sor */

/* ---- hex-töltő mód (F5) ---- */
static int hexmode = 0;
static char hexbuf[160];
static int hexlen = 0;

/* ---- súgó (F1) ---- */
static int helpmode = 0;

static void input_row_refresh(void);
static void redraw_input(void);

static void q4_app_reset(void) {
    q4k_init(&q4);
    ilen = icur = 0;
    hist_n = 0; hsel = -1;
    hexmode = 0; helpmode = 0;
    q4.cx = 0; q4.cy = 3;
    irow = 3;
}

void quartz_app_enter(void) {
    q4_app_reset();
    sound_beep(880, 4);
}
void quartz_app_exit(void) { q4.running = 0; }

/* gépi kód hex-betöltő: a hexbufot a RAM $000-tól írja */
static void hex_load(void) {
    int n = 0;
    for (int i = 0; hexbuf[i] && n < 512; ) {
        int hi = -1, lo = -1;
        char c = hexbuf[i++];
        if (c == ' ') continue;
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        if (!hexbuf[i]) { if (hi >= 0) q4.ram[n++] = (u8)hi; break; }
        char c2 = hexbuf[i++];
        if (c2 == ' ') { if (hi >= 0) q4.ram[n++] = (u8)hi; continue; }
        if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
        else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
        if (hi >= 0 && lo >= 0) q4.ram[n++] = (u8)((hi << 4) | lo);
    }
    char m[64];
    ksnprintf(m, sizeof(m), "%d BYTES TO $000", n);
    q4k_print(&q4, m);
}

/* beviteli sor újrarajzolása a q4 képernyőn */
static void input_row_refresh(void) {
    if (irow < 0 || irow >= Q4_TEXT_H) return;
    for (int x = 0; x < Q4_TEXT_W; x++)
        q4.vt[irow * Q4_TEXT_W + x] = 0;
    for (int x = 0; x < ilen && x < 31; x++)
        q4.vt[irow * Q4_TEXT_W + x] = (u8)iline[x];
    q4.cx = icur < 31 ? icur : 31;
    q4.cy = irow;
}
static void redraw_input(void) { input_row_refresh(); }

/* parancs végrehajtása: kiírjuk a képernyőre, majd feldolgozzuk */
static void submit_line(void) {
    iline[ilen] = 0;
    /* kiírjuk visszhangként a beviteli sor helyére és új sorba lépünk */
    q4k_print_at(&q4, 0, irow, iline);
    q4k_putch(&q4, 10);
    /* előzmény */
    if (hist_n < QH_MAX) {
        for (int k = 0; k < 79 && k < ilen; k++) hist[hist_n][k] = iline[k];
        hist_len[hist_n] = ilen;
        hist_n++;
    }
    hsel = -1;
    ilen = 0; icur = 0;
    irow = q4.cy;
    q4k_command(&q4, iline);
    /* ha a kimenet görgetett, kövessük az új sort */
    irow = q4.cy;
    input_row_refresh();
}

void quartz_app_key(u8 sc, u8 rel, u8 ascii) {
    if (rel) {
        /* gomb-fel: a játékbemenet bitjeit is engedjük */
        if (sc == KEY_LEFT)  q4.keys &= (u8)~1;
        if (sc == KEY_RIGHT) q4.keys &= (u8)~2;
        if (sc == KEY_UP)    q4.keys &= (u8)~4;
        if (sc == KEY_DOWN)  q4.keys &= (u8)~8;
        if (sc == 0x39 || sc == KEY_ENTER) q4.keys &= (u8)~16;
        return;
    }
    /* játék-irányok a futó programhoz (KEY()) */
    if (sc == KEY_LEFT)  q4.keys |= 1;
    if (sc == KEY_RIGHT) q4.keys |= 2;
    if (sc == KEY_UP)    q4.keys |= 4;
    if (sc == KEY_DOWN)  q4.keys |= 8;
    if (sc == 0x39 || sc == KEY_ENTER) q4.keys |= 16;

    if (sc == KEY_F1 && !rel) { helpmode ^= 1; sound_beep(700, 2); return; }
    if (sc == KEY_F5) { hexmode ^= 1; hexlen = 0; hexbuf[0] = 0; sound_beep(900, 2); return; }
    if (sc == KEY_F2) { q4k_mcu_run(&q4, 10000); return; }

    if (helpmode) { if (sc == KEY_ESC || sc == KEY_F1) helpmode = 0; return; }

    if (hexmode) {
        if (sc == KEY_ESC) { hexmode = 0; return; }
        if (sc == KEY_ENTER) { hex_load(); hexmode = 0; return; }
        if (ascii == '\b') { if (hexlen > 0) hexbuf[--hexlen] = 0; return; }
        if (((ascii >= '0' && ascii <= '9') || (ascii >= 'A' && ascii <= 'F') ||
             (ascii >= 'a' && ascii <= 'f') || ascii == ' ') && hexlen < 158) {
            hexbuf[hexlen++] = (ascii >= 'a' && ascii <= 'f') ? (char)(ascii - 32) : ascii;
            hexbuf[hexlen] = 0;
        }
        return;
    }

    /* ---- szerkesztő: teljes kurzor-vezérlés + előzmények ---- */
    if (sc == KEY_LEFT)  { if (icur > 0) icur--; input_row_refresh(); return; }
    if (sc == KEY_RIGHT) { if (icur < ilen) icur++; input_row_refresh(); return; }
    if (sc == KEY_HOME)  { icur = 0; input_row_refresh(); return; }
    if (sc == KEY_END)   { icur = ilen; input_row_refresh(); return; }
    if (sc == KEY_UP) {
        if (hsel < 0) { for (int k = 0; k < 79; k++) iline[k] = 0; ilen = 0; hsel = hist_n - 1; }
        else if (hsel > 0) hsel--;
        if (hsel >= 0 && hist_n > 0) {
            for (int k = 0; k < hist_len[hsel] && k < 79; k++) iline[k] = hist[hsel][k];
            ilen = hist_len[hsel];
            iline[ilen] = 0;
            icur = ilen;
        }
        input_row_refresh();
        sound_beep(1000, 2);
        return;
    }
    if (sc == KEY_DOWN) {
        if (hsel >= 0) {
            hsel++;
            if (hsel >= hist_n) { hsel = -1; ilen = 0; icur = 0; }
            else {
                for (int k = 0; k < hist_len[hsel] && k < 79; k++) iline[k] = hist[hsel][k];
                ilen = hist_len[hsel];
                iline[ilen] = 0;
                icur = ilen;
            }
            input_row_refresh();
            sound_beep(1100, 2);
        }
        return;
    }
    if (sc == KEY_ESC) {
        if (q4.running) { q4.running = 0; q4k_print(&q4, "STOP."); irow = q4.cy; input_row_refresh(); }
        return;
    }
    if (sc == KEY_ENTER || ascii == '\n') { submit_line(); return; }
    if (ascii == '\b') {
        if (icur > 0) {
            for (int k = icur - 1; k < ilen - 1; k++) iline[k] = iline[k + 1];
            ilen--; icur--;
            iline[ilen] = 0;
        }
        input_row_refresh();
        return;
    }
    if (ascii == '\t') return;
    if (ascii >= 32 && ascii < 127 && ilen < 79) {
        for (int k = ilen; k > icur; k--) iline[k] = iline[k - 1];
        iline[icur++] = (char)ascii;
        ilen++;
        iline[ilen] = 0;
        input_row_refresh();
        sound_beep(1400, 1);       /* gomb-útonatás a TV-ből */
    }
}

/* ---------- képernyő ---------- */
/* CRT-szerű TV a Keringő-vásznon: 512x384 (2x nagyítás) */
#define TV_SC 2
static void draw_tv(void) {
    int W = (int)gfx_width(), H = (int)gfx_height();
    int tw = 256 * TV_SC, th = 192 * TV_SC;
    int ox = (W - tw) / 2, oy = 24 + (H - 24 - 20 - th) / 2;
    if (ox < 8) ox = 8;
    /* TV-szekrény */
    gfx_fill_rect(ox - 16, oy - 16, tw + 32, th + 32, 0xFF1E2430);
    gfx_rect(ox - 16, oy - 16, tw + 32, th + 32, 0xFF3A4A5A);
    gfx_fill_rect(ox - 4, oy - 4, tw + 8, th + 8, 0xFF0A0D10);
    /* zöld foszfor */
    u32 fg = 0xFF3AFF3A, dim = 0xFF14571C, bg = 0xFF050805;
    gfx_fill_rect(ox, oy, tw, th, bg);
    if (q4.videoMode == 0) {
        for (int y = 0; y < Q4_TEXT_H; y++)
            for (int x = 0; x < Q4_TEXT_W; x++) {
                u8 c = q4.vt[y * Q4_TEXT_W + x];
                if (c >= 32 && c < 127) {
                    char s[2] = { (char)c, 0 };
                    gfx_draw_text(ox + x * 8 * TV_SC, oy + y * 12 * TV_SC, s, fg, bg);
                } else if (c) {
                    gfx_fill_rect(ox + x * 8 * TV_SC, oy + y * 12 * TV_SC, 8 * TV_SC, 8 * TV_SC, dim);
                }
            }
    } else {
        for (int y = 0; y < Q4_GFX_H; y++)
            for (int x = 0; x < Q4_GFX_W; x++)
                if (q4k_point(&q4, x, y))
                    gfx_fill_rect(ox + x * 8, oy + y * 8, 8, 8, fg);
    }
    /* villogó kurzor */
    if ((tick_count() / 25) % 2 == 0 && !hexmode && !helpmode) {
        int cxx = q4.cx < 32 ? q4.cx : 31, cyy = q4.cy < 16 ? q4.cy : 15;
        if (q4.videoMode == 0)
            gfx_fill_rect(ox + cxx * 8 * TV_SC, oy + cyy * 12 * TV_SC + 10 * TV_SC, 8 * TV_SC, 2 * TV_SC, fg);
    }
    /* szkennelt-sor hatás */
    for (int y = oy; y < oy + th; y += 4)
        gfx_hline(ox, y, tw, 0x18AF2310);
    /* TV-matrica */
    gfx_draw_text(ox - 16, oy - 13, "QUARTZ-4B 500HZ 4-BIT", 0xFF8FA0AC, 0xFF000000u);
}

static void draw_help(void) {
    int W = (int)gfx_width(), H = (int)gfx_height();
    gfx_fill_rect(24, 40, W - 48, H - 90, 0xE6101018);
    gfx_rect(24, 40, W - 48, H - 90, 0xFF3A4A5A);
    gfx_draw_text(40, 52, "QUARTZ-4B — VEZERLES (F1 = vissza)", 0xFF3AFF3A, 0xFF000000u);
    const char *l[] = {
        "BASIC: PRINT [@x,y] CLS MODE PLOT UNPLOT LINE DRAWTO",
        "  BOX CIRCLE TRIANGLE FILL LET/valtozo= INC DEC",
        "  IF..THEN (sorszam vagy parancs) FOR/TO/STEP/NEXT",
        "  WHILE/WEND REPEAT/UNTIL GOTO GOSUB RETURN",
        "  PEEK() POKE RND() KEY() WAIT BEEP END LIST NEW",
        "  RUN SAVE \"N.BAS\" LOAD \"N.BAS\" FILES",
        "JATEK-BEVITEL: nyilak es space = KEY() 1..5",
        "GEPKOD: F5 = hex betolto $000-ba, SYS 0 = futtatas",
        "CPU: LDA/ADD/SUB/STA/JMP/JZ/JC/LDX/INX/DEX/JSR/RTS/OUT/IN/HLT",
        "VALTOZOK: A-Z, 0-15 (4 bites!) — nagy szamokhoz POKE/PEEK",
        "Beepites: DEMO, FILL, PONG, METEOR, KIGYO, HELLO",
        "  pl: LOAD \"PONG\" majd RUN"
    };
    for (int i = 0; i < 12; i++)
        gfx_draw_text(40, 70 + i * 13, l[i], 0xFF3AE85A, 0xFF000000u);
}

static void draw_hex(void) {
    int W = (int)gfx_width();
    gfx_fill_rect(24, (int)gfx_height() - 120, W - 48, 80, 0xE6081010);
    gfx_rect(24, (int)gfx_height() - 120, W - 48, 80, 0xFF3A4A5A);
    gfx_draw_text(40, (int)gfx_height() - 108, "GEPKOD HEX-BETOLTO (F5 = megse):", 0xFF3AFF3A, 0xFF000000u);
    gfx_draw_text(40, (int)gfx_height() - 92, hexbuf, 0xFF3AE85A, 0xFF000000u);
    gfx_draw_text(40, (int)gfx_height() - 78, "pl: 11 22 F0 ... Enter = betoltes $000-ba", 0xFF8FA0AC, 0xFF000000u);
}

void quartz_app_frame(u32 t) {
    (void)t;
    /* futó program léptetése (jatek-tempo) */
    if (q4.running) q4k_poll(&q4, 8);
    u32 accent = settings_accent_color();
    draw_tv();
    if (helpmode) draw_help();
    if (hexmode) draw_hex();
    char b[96];
    ksnprintf(b, sizeof(b), "%s %s | %s",
        ui_lang() ? "KIMI-MATE QUARTZ-4B" : "KIMI-MATE QUARTZ-4B",
        q4.running ? (ui_lang() ? "[RUNNING]" : "[FUT]") : "",
        ui_lang() ? "F1: help  F5: hex code  Esc: stop  Up/Down: history"
                  : "F1: segito  F5: gepkod  Esc: stop  Fel/Le: elozmenyek");
    app_chrome(ui_lang() ? "QUARTZ-4B (KIMI-MATE)" : "QUARTZ-4B (KIMI-MATE)", b, accent);
}
