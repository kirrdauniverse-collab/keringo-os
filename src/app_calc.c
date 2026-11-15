/* app_calc.c — Számológép: egér + billentyűzet, 4 művelet + % , 1/x, x², gyök */
#include "kernel.h"

static char cexpr[64] = "";       /* számjegyek az aktuális bevitelben */
static double cacc = 0;           /* akkumulátor */
static char cop = 0;              /* '+', '-', '*', '/', 0 */
static int cnew = 1;              /* következő számjegy új számba kezd */
static int cerror = 0;
static u32 cmsg_until = 0;
static const char *cmsg = "";

/* double -> string (saját, %.6g jelleg) */
static void dtoa(double v, char *out, int outsz) {
    if (v != v) { ksnprintf(out, outsz, "ERR"); return; }
    int neg = v < 0;
    if (neg) v = -v;
    /* nagy szám: egészrészből */
    double ip = (double)(long long)v;
    double fp = v - ip;
    char tmp[64];
    int n = 0;
    if (ip > 1e15) { ksnprintf(out, outsz, neg ? "-NAGY" : "NAGY"); return; }
    if (ip < 1) { tmp[n++] = '0'; }
    else {
        long long i = (long long)ip;
        char rev[32]; int rn = 0;
        while (i > 0) { rev[rn++] = (char)('0' + (i % 10)); i /= 10; }
        for (int k = rn - 1; k >= 0; k--) tmp[n++] = rev[k];
    }
    /* tizedesek: max 8, záró nullák levágva */
    if (fp > 1e-9) {
        tmp[n++] = '.';
        int digits = 0;
        while (fp > 1e-9 && digits < 8) {
            fp *= 10;
            int d = (int)fp;
            if (d < 0) d = 0; if (d > 9) d = 9;
            tmp[n++] = (char)('0' + d);
            fp -= d;
            digits++;
        }
        while (n > 0 && tmp[n - 1] == '0') n--;
        if (tmp[n - 1] == '.') n--;
    }
    tmp[n] = 0;
    if (neg && !(n == 1 && tmp[0] == '0')) {
        out[0] = '-'; out[1] = 0;
        for (int k = 0; k < n && k < outsz - 2; k++) out[k + 1] = tmp[k];
        out[n + 1] = 0;
    } else {
        for (int k = 0; k < n && k < outsz - 1; k++) out[k] = tmp[k];
        out[n] = 0;
    }
}

static double catof(const char *s) {
    double v = 0, frac = 0, div = 10;
    int dot = 0;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (c == '.') { dot = 1; continue; }
        if (c < '0' || c > '9') continue;
        if (!dot) v = v * 10 + (c - '0');
        else { frac += (c - '0') / div; div *= 10; }
    }
    return v + frac;
}

static void apply_op(void) {
    double v = catof(cexpr);
    switch (cop) {
        case '+': cacc += v; break;
        case '-': cacc -= v; break;
        case '*': cacc *= v; break;
        case '/':
            if (v == 0) { cerror = 1; cacc = 0; }
            else cacc /= v;
            break;
        default: cacc = v; break;
    }
}

static void press(char c) {
    if (cerror && c != 'C') return;
    if (c >= '0' && c <= '9') {
        if (cnew) { cexpr[0] = 0; cnew = 0; }
        int L = 0; while (cexpr[L]) L++;
        if (L < 15) { cexpr[L] = c; cexpr[L + 1] = 0; }
        sound_beep(600, 2);
        return;
    }
    switch (c) {
        case '.':
            if (cnew) { cexpr[0] = '0'; cexpr[1] = '.'; cexpr[2] = 0; cnew = 0; }
            else {
                int L = 0; while (cexpr[L]) L++;
                int dot = 0;
                for (int i = 0; i < L; i++) if (cexpr[i] == '.') dot = 1;
                if (!dot && L < 15) { cexpr[L] = '.'; cexpr[L + 1] = 0; }
            }
            sound_beep(650, 2);
            break;
        case '+': case '-': case '*': case '/':
            apply_op();
            if (cerror) break;
            cop = c; cnew = 1;
            dtoa(cacc, cexpr, sizeof(cexpr));
            cnew = 1;
            sound_beep(760, 3);
            break;
        case '=':
            apply_op();
            if (cerror) { cmsg = "Nullával osztás!"; cmsg_until = tick_count() + 200; }
            dtoa(cacc, cexpr, sizeof(cexpr));
            cop = 0; cnew = 1;
            sound_beep(880, 4);
            break;
        case 'C':
            cexpr[0] = 0; cacc = 0; cop = 0; cnew = 1; cerror = 0;
            sound_beep(440, 3);
            break;
        case 'B':   /* backspace */
            if (!cnew) {
                int L = 0; while (cexpr[L]) L++;
                if (L > 0) cexpr[L - 1] = 0;
            }
            break;
        case '%': {
            double v = catof(cexpr);
            dtoa(v / 100.0, cexpr, sizeof(cexpr));
            cnew = 1;
            sound_beep(700, 3);
            break; }
        case 'R': {  /* gyök */
            double v = catof(cexpr);
            if (v < 0) { cerror = 1; cmsg = "Negatív gyök!"; cmsg_until = tick_count() + 200; }
            else {
                double y = v > 1 ? v : 1;
                for (int i = 0; i < 40; i++) y = (y + v / y) * 0.5;
                dtoa(y, cexpr, sizeof(cexpr));
            }
            cnew = 1;
            sound_beep(700, 3);
            break; }
        case 'S': {  /* négyzet */
            double v = catof(cexpr);
            dtoa(v * v, cexpr, sizeof(cexpr));
            cnew = 1;
            sound_beep(700, 3);
            break; }
        case 'I': {  /* 1/x */
            double v = catof(cexpr);
            if (v == 0) { cerror = 1; }
            else dtoa(1.0 / v, cexpr, sizeof(cexpr));
            cnew = 1;
            sound_beep(700, 3);
            break; }
    }
}

void calc_enter(void) { sound_beep(700, 4); }

void calc_key(u8 sc, u8 rel, u8 ascii) {
    (void)sc;
    if (rel) return;
    if (ascii >= '0' && ascii <= '9') { press((char)ascii); return; }
    switch (ascii) {
        case '.': case '+': press(ascii); break;
        case '-': press('-'); break;
        case '*': press('*'); break;
        case '/': press('/'); break;
        case '%': press('%'); break;
        case '=': case '\n': press('='); break;
        case 'c': case 'C': press('C'); break;
        case 'r': case 'R': press('R'); break;
        case 's': case 'S': press('S'); break;
        case 'i': case 'I': press('I'); break;
        case '\b': press('B'); break;
    }
}

/* gombok: sor, oszlop, felirat, 'parancs' */
static const char *btn_label[5][4] = {
    { "C", "Sqrt", "x*y", "1/x" },
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "0", ".", "=", "+" },
};
static const char btn_cmd[5][4] = {
    { 'C', 'R', 'S', 'I' },
    { '7', '8', '9', '/' },
    { '4', '5', '6', '*' },
    { '1', '2', '3', '-' },
    { '0', '.', '=', '+' },
};

static int CB_X = 300;
#define CB_Y 200
#define CB_W 105
#define CB_H 72
#define CB_GAP 12

void calc_mouse(int mx, int my, int dx, int dy, u8 btn, u8 prev) {
    (void)dx; (void)dy;
    if (!(btn & 1) || (prev & 1)) return;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++) {
            int x = CB_X + c * (CB_W + CB_GAP), y = CB_Y + r * (CB_H + CB_GAP);
            if (app_in_rect(mx, my, x, y, CB_W, CB_H)) {
                press(btn_cmd[r][c]);
                return;
            }
        }
}

void calc_frame(u32 tick) {
    u32 accent = settings_accent_color();
    CB_X = ((int)gfx_width() - (4 * CB_W + 3 * CB_GAP)) / 2;
    gfx_clear(0xFF0D131A);
    gfx_draw_text_scaled(CB_X, 60, ui_lang() ? "CALCULATOR" : "SZÁMOLÓGÉP", accent, 2);

    /* kijelző */
    char disp[80];
    char accs[40];
    if (cexpr[0]) ksnprintf(disp, sizeof(disp), "%s", cexpr);
    else ksnprintf(disp, sizeof(disp), "0");
    dtoa(cacc, accs, sizeof(accs));
    gfx_fill_rect(CB_X, 110, 4 * CB_W + 3 * CB_GAP, 64, 0xFF101820);
    gfx_rect(CB_X, 110, 4 * CB_W + 3 * CB_GAP, 64, accent);
    gfx_draw_text(CB_X + 12, 118, accs, 0xFF5A7080, 0x00000000u);
    if (cop) {
        char op[2] = { cop, 0 };
        gfx_draw_text(CB_X + 12, 118, op, 0xFFE8B24A, 0x00000000u);
    }
    /* nagy szám jobb igazítva */
    int tw = gfx_text_width(disp) * 3;
    gfx_draw_text_scaled(CB_X + 4 * CB_W + 3 * CB_GAP - tw - 14, 132, disp,
                         cerror ? 0xFFE85050 : 0xFFF0F0F0, 3);

    /* gombok */
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++) {
            int x = CB_X + c * (CB_W + CB_GAP), y = CB_Y + r * (CB_H + CB_GAP);
            int is_op = (r == 0) || c == 3;
            int is_eq = (r == 4 && c == 2);
            u32 col = is_eq ? 0xFF50E878 : (is_op ? 0xFF3A5562 : 0xFF1C2530);
            const char *lb = btn_label[r][c];
            /* magyarosítás */
            if (ui_lang() == 0) {
                if (r == 0 && c == 1) lb = "Gyok";
                if (r == 0 && c == 3) lb = "1/x";
            }
            gfx_fill_rect(x, y, CB_W, CB_H, col);
            gfx_rect(x, y, CB_W, CB_H, accent);
            int lw = gfx_text_width(lb) * 2;
            gfx_draw_text_scaled(x + (CB_W - lw) / 2, y + (CB_H - 16) / 2, lb,
                                 is_eq ? 0xFF0A2010 : 0xFFF0F0F0, 2);
        }

    char st[120];
    ksnprintf(st, sizeof(st), "%s",
              ui_lang() ? "Type or click  C: clear  Backspace: erase  R: sqrt  S: square  I: 1/x"
                        : "Gépelj vagy kattints  C: törlés  Backspace: törlés  R: gyök  S: négyzet  I: 1/x");
    if ((s32)(tick - cmsg_until) < 0) ksnprintf(st, sizeof(st), "%s", cmsg);
    app_chrome(ui_lang() ? "Calculator" : "Számológép", st, accent);
}
