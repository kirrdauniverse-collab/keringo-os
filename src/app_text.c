/* app_text.c — Szövegszerkesztő: UTF-8 szöveg, magyar ékezetes gépelés,
 * CD/pendrájv/HDD betöltés, /mem + /sys mentés */
#include "kernel.h"

#define TBUF_MAX 60000
#define TVX 16            /* szövegterület bal felső sarka */
#define TVY 30
#define TCOLS 120
#define TLINES 72         /* 12 px sorköz */

static u8  tbuf[TBUF_MAX];
static int tlen = 0, tcur = 0, ttop = 0;    /* bájt-pozíciók, ttop = első látszó sor */
static char tfname[40] = "SZOVEG.TXT";
static int picker = 0, picker_sel = 0, picker_n = 0;
static char pick_names[40][64];
static char pick_path[40][96];
static u32 msg_until = 0;
static const char *msg_txt = "";
static int shift_down = 0, alt_down = 0;

/* magyar karakterek UTF-8 beszúráshoz */
static const char *hu_keymap(int base, int shifted) {
    switch (base) {
        case 0x1A: return shifted ? "\xC5\x90" : "\xC5\x91";   /* ő Ő */
        case 0x1B: return shifted ? "\xC3\x9A" : "\xC3\xBA";   /* ú Ú */
        case 0x27: return shifted ? "\xC3\x89" : "\xC3\xA9";   /* é É */
        case 0x28: return shifted ? "\xC3\x81" : "\xC3\xA1";   /* á Á */
        case 0x29: return shifted ? "\xC5\xB0" : "\xC5\xB1";   /* ű Ű */
        case 0x2B: return shifted ? "\xC3\x8D" : "\xC3\xAD";   /* í Í */
    }
    return 0;
}
static const char *hu_altgr(int ascii, int shifted) {
    switch (ascii) {
        case 'o': return shifted ? "\xC3\x93" : "\xC3\xB3";    /* ó Ó */
        case 'u': return shifted ? "\xC3\x9C" : "\xC3\xBC";    /* ü Ü */
        case 'p': return shifted ? "\xC3\x96" : "\xC3\xB6";    /* ö Ö */
    }
    return 0;
}

static void insert_bytes(const char *p, int n) {
    if (tlen + n >= TBUF_MAX) return;
    for (int i = tlen; i > tcur; i--) tbuf[i + n - 1] = tbuf[i - 1];
    for (int i = 0; i < n; i++) tbuf[tcur + i] = p[i];
    tcur += n; tlen += n;
}
static void insert_char(char c) { insert_bytes(&c, 1); }
static void del_at(int forward) {
    if (tlen == 0) return;
    if (!forward && tcur > 0) {
        /* UTF-8: a törlendő karakter összes bájtja */
        int s = tcur - 1;
        while (s > 0 && (tbuf[s] & 0xC0) == 0x80) s--;
        int n = tcur - s;
        for (int i = s; i < tlen - n; i++) tbuf[i] = tbuf[i + n];
        tlen -= n; tcur = s;
    } else if (forward && tcur < tlen) {
        int e = tcur + 1;
        while (e < tlen && (tbuf[e] & 0xC0) == 0x80) e++;
        int n = e - tcur;
        for (int i = tcur; i < tlen - n; i++) tbuf[i] = tbuf[i + n];
        tlen -= n;
    }
}

/* kurzor sor/oszlop + sorkezdet keresése */
static int line_start(int pos) { while (pos > 0 && tbuf[pos - 1] != '\n') pos--; return pos; }
static int next_line_start(int pos) { while (pos < tlen && tbuf[pos - 1] != '\n' && (pos == 0 || tbuf[pos - 1] != '\n')) { if (tbuf[pos] == '\n') return pos + 1; pos++; } return pos; }

static int cur_line(void) {
    int l = 0;
    for (int i = 0; i < tcur && i < tlen; i++) if (tbuf[i] == '\n') l++;
    return l;
}
static int cur_col(void) {
    int c = 0, i = tcur;
    while (i > 0 && tbuf[i - 1] != '\n') { i--; c++; }
    return c;
}
static int line_count(void) {
    int l = 1;
    for (int i = 0; i < tlen; i++) if (tbuf[i] == '\n') l++;
    return l;
}
static int goto_line_start_of(int line) {
    int l = 0, i = 0;
    while (i < tlen && l < line) { if (tbuf[i] == '\n') l++; i++; }
    return i;
}

static void scroll_to_cursor(void) {
    int line = cur_line();
    if (line < ttop) ttop = line;
    if (line >= ttop + TLINES) ttop = line - TLINES + 1;
}

/* ---------- fájlválasztó ---------- */
static void picker_build(void) {
    picker_n = 0;
    static const char *tags[4] = { "/cd", "/sys", "/usb", "/mem" };
    static const char *pfx[4] = { "/cd/", "/sys/", "/usb/", "/mem/" };
    char names[16][64]; u32 sizes[16]; u8 flags[16];
    for (int v = 0; v < 4; v++) {
        int n = vfs_list((char *)tags[v], names, sizes, flags, 16);
        if (n <= 0) continue;
        for (int i = 0; i < n && picker_n < 40; i++) {
            /* szövegfájlok: .TXT .CFG .UPD vagy 8 KB alatti */
            int L = 0; while (names[i][L]) L++;
            int is_txt = 0;
            if (L >= 4) {
                char a = names[i][L-4], b = names[i][L-3], c = names[i][L-2], d = names[i][L-1];
                if (a == '.' && (b == 't' || b == 'T') && (c == 'x' || c == 'X') && (d == 't' || d == 'T')) is_txt = 1;
                if (a == '.' && (b == 'c' || b == 'C') && (c == 'f' || c == 'F') && (d == 'g' || d == 'G')) is_txt = 1;
            }
            if (is_txt || sizes[i] <= 2048) {
                if (sizes[i] > TBUF_MAX) continue;
                for (int j = 0; j < 63 && names[i][j]; j++) pick_names[picker_n][j] = names[i][j];
                pick_names[picker_n][63] = 0;
                ksnprintf(pick_path[picker_n], 96, "%s%s", pfx[v], pick_names[picker_n]);
                picker_n++;
            }
        }
    }
}

static void load_file(const char *path, const char *name) {
    int out = 0;
    char tmp[TBUF_MAX];
    if (vfs_read_file(path, tmp, TBUF_MAX - 1, &out) && out > 0) {
        for (int i = 0; i < out; i++) tbuf[i] = tmp[i];
        tlen = out; tcur = 0; ttop = 0;
        int j = 0; while (name[j] && j < 38) { tfname[j] = name[j]; j++; }
        tfname[j] = 0;
        msg_txt = "betöltve"; msg_until = tick_count() + 150;
        sound_tune((const u16[]){ 660, 6, 880, 6, 1100, 8 }, 3);
    } else {
        msg_txt = "nem sikerült olvasni"; msg_until = tick_count() + 150;
    }
}

static void save_file(void) {
    char path[96];
    ksnprintf(path, sizeof(path), "/mem/%s", tfname);
    int ok1 = vfs_write_file(path, (const char *)tbuf, tlen);
    int ok2 = 0;
    ksnprintf(path, sizeof(path), "/sys/%s", tfname);
    if (sys_mounted()) ok2 = vfs_write_file(path, (const char *)tbuf, tlen);
    if (ok1 || ok2) {
        msg_txt = ok2 ? "mentve: /sys + /mem" : "mentve: /mem (előnézet)";
        msg_until = tick_count() + 200;
        sound_tune((const u16[]){ 880, 8, 1175, 8, 1568, 10 }, 3);
    } else {
        msg_txt = "a mentés nem sikerült"; msg_until = tick_count() + 200;
    }
}

/* ---------- app ---------- */
void text_enter(void) {
    if (tlen == 0) {
        const char *intro =
            "Keringo OS - Szovegszerkeszto\n"
            "\n"
            "Irj nyugodtan magyarul is: ő, ű, é, á...\n"
            "A magyar billentyűzet szerinti ékezetes\n"
            "betűk a helyükön vannak (ő=[-hely, é=;-hely...).\n"
            "AltGr+O=ó  AltGr+U=ü  AltGr+P=ö\n"
            "\n"
            "F2: mentés   F3: megnyitás   Esc: vissza\n";
        while (intro[tlen]) { tbuf[tlen] = intro[tlen]; tlen++; }
    }
    tcur = 0; ttop = 0; picker = 0;
    sound_beep(740, 5);
}

void text_exit(void) { picker = 0; }

void text_key(u8 sc, u8 rel, u8 ascii) {
    if (picker) {
        if (rel) return;
        if (sc == KEY_ESC || sc == KEY_F3) { picker = 0; return; }
        if (sc == KEY_UP   && picker_sel > 0) { picker_sel--; return; }
        if (sc == KEY_DOWN && picker_sel < picker_n - 1) { picker_sel++; return; }
        if (sc == KEY_PGUP) { picker_sel -= 8; if (picker_sel < 0) picker_sel = 0; return; }
        if (sc == KEY_PGDN) { picker_sel += 8; if (picker_sel > picker_n - 1) picker_sel = picker_n - 1; return; }
        if (sc == KEY_ENTER) {
            picker = 0;
            if (picker_n > 0) load_file(pick_path[picker_sel], pick_names[picker_sel]);
            return;
        }
        return;
    }
    if (sc == 0x2A || sc == 0x36) { shift_down = !rel; return; }
    if (sc == 0x38) { alt_down = !rel; return; }
    if (rel) return;

    if (sc == KEY_F2) { save_file(); return; }
    if (sc == KEY_F3) { picker_build(); picker = 1; picker_sel = 0; return; }

    /* magyar ékezetesek (nyers scancode) */
    if (alt_down) {
        const char *s = hu_altgr((char)ascii, shift_down);
        if (s) { insert_bytes(s, 2); sound_beep(1000, 2); return; }
    }
    const char *hu = hu_keymap(sc, shift_down);
    if (hu) { insert_bytes(hu, 2); sound_beep(1000, 2); return; }

    switch (sc) {
        case KEY_LEFT:  if (tcur > 0) { tcur--; while (tcur > 0 && (tbuf[tcur] & 0xC0) == 0x80) tcur--; } break;
        case KEY_RIGHT: if (tcur < tlen) { tcur++; while (tcur < tlen && (tbuf[tcur] & 0xC0) == 0x80) tcur++; } break;
        case KEY_UP: {
            int line = cur_line();
            if (line > 0) { int col = cur_col(); tcur = goto_line_start_of(line - 1);
                for (int i = 0; i < col && tcur < tlen && tbuf[tcur] != '\n'; i++) tcur++; }
            break; }
        case KEY_DOWN: {
            int line = cur_line(), col = cur_col();
            int nxt = goto_line_start_of(line + 1);
            if (nxt < tlen || line + 1 < line_count()) {
                tcur = nxt;
                for (int i = 0; i < col && tcur < tlen && tbuf[tcur] != '\n'; i++) tcur++;
            }
            break; }
        case KEY_HOME: tcur = line_start(tcur); break;
        case KEY_END:  while (tcur < tlen && tbuf[tcur] != '\n') tcur++; break;
        case KEY_PGUP: { int line = cur_line() - TLINES + 2; if (line < 0) line = 0; tcur = goto_line_start_of(line); break; }
        case KEY_PGDN: { int line = cur_line() + TLINES - 2; if (line >= line_count()) line = line_count() - 1; tcur = goto_line_start_of(line); break; }
        case KEY_ENTER: insert_char('\n'); break;
        case KEY_DEL: del_at(1); break;
        default:
            if (ascii == '\b') del_at(0);
            else if (ascii >= 32 && ascii < 127) insert_char((char)ascii);
            break;
    }
    scroll_to_cursor();
    (void)line_start; (void)next_line_start;
}

void text_frame(u32 tick) {
    u32 accent = settings_accent_color();
    char b[96];
    if (picker) {
        app_chrome(ui_lang() ? "Text editor - open" : "Szövegszerkesztő - megnyitás",
                   ui_lang() ? "Up/Down: select  Enter: open  Esc: cancel" : "Fel/Le: választás  Enter: megnyitás  Esc: mégse", accent);
        gfx_fill_rect(60, 60, (int)gfx_width() - 120, (int)gfx_height() - 120, 0xE61C2530);
        gfx_rect(60, 60, (int)gfx_width() - 120, (int)gfx_height() - 120, accent);
        gfx_draw_text(80, 80, ui_lang() ? "OPEN FILE" : "FÁJL MEGNYITÁSA", accent, 0xFF000000u);
        int start = picker_sel - 12; if (start < 0) start = 0;
        int y = 104;
        for (int i = start; i < picker_n && y < (int)gfx_height() - 100; i++, y += 20) {
            char row[110];
            ksnprintf(row, sizeof(row), "%s %s", (i == picker_sel) ? ">" : " ", pick_names[i]);
            gfx_draw_text(80, y, row, (i == picker_sel) ? 0xFFE8B24A : 0xFFF0F0F0, 0xFF000000u);
        }
        if (picker_n == 0)
            gfx_draw_text(80, 120, ui_lang() ? "No text files found." : "Nincs szövegfájl a tárolókon.", 0xFF8FA0AC, 0xFF000000u);
        return;
    }

    /* szövegterület */
    gfx_fill_rect(0, 23, (int)gfx_width(), (int)gfx_height() - 44, 0xFF131A20);
    int pos = goto_line_start_of(ttop);
    int x = TVX, y = TVY;
    int cursor_drawn = 0;
    for (int ln = ttop; ln < ttop + TLINES && pos <= tlen; ) {
        if (pos == tcur) {
            if ((tick / 15) % 2 == 0) gfx_fill_rect(x, y + 1, 7, 11, 0xFFE8B24A);
            cursor_drawn = 1;
        }
        if (pos >= tlen) break;
        u8 c = tbuf[pos];
        if (c == '\n') { ln++; y += 12; x = TVX; pos++; continue; }
        if (c == '\r') { pos++; continue; }
        /* UTF-8 karakter kirajzolása */
        {
            char tmp[5]; int n = 0;
            tmp[n++] = (char)c;
            if ((c & 0xE0) == 0xC0 && pos + 1 < tlen) tmp[n++] = (char)tbuf[pos + 1];
            if ((c & 0xF0) == 0xE0 && pos + 2 < tlen) { tmp[n++] = (char)tbuf[pos + 1]; tmp[n++] = (char)tbuf[pos + 2]; }
            tmp[n] = 0;
            if (x < TVX + TCOLS * 8) gfx_draw_text(x, y, tmp, 0xFFF0F0F0, 0xFF000000u);
            pos += n; x += 8;
        }
    }
    if (!cursor_drawn && (tick / 15) % 2 == 0) {
        /* kurzor a fájl végén */
        gfx_fill_rect(x, y + 1, 7, 11, 0xFFE8B24A);
    }

    ksnprintf(b, sizeof(b), "%s  |  %s: %d.%d  |  %d B  |  F2: %s  F3: %s",
              tfname,
              ui_lang() ? "Pos" : "Pozíció", cur_line() + 1, cur_col(), tlen,
              ui_lang() ? "save" : "mentés",
              ui_lang() ? "open" : "megnyitás");
    if ((s32)(tick - msg_until) < 0) ksnprintf(b, sizeof(b), "%s", msg_txt);
    app_chrome(ui_lang() ? "Text editor" : "Szövegszerkesztő", b, accent);
}
