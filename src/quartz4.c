/* quartz4.c — QUARTZ-4B (KIMI-MATE) gép: CPU, BASIC, videó, memória.
 * A KIMI-MATE-4bit-computer.html (QUARTZ-4B v3.0) hűséges portja, a következő
 * DOKUMENTÁLT Keringő-bővítésekkel:
 *  - a kifejezések teljes értéken értékelődnek (a 4-bit maszk a változóknál él)
 *  - WHILE/WEND ténylegesen vizsgálja a feltételt; RETURN a GOSUB-veremből visszalép
 *  - KEY() bővítés: 0=nincs, 1=bal, 2=jobb, 3=fel, 4=le, 5=tűz (játékokhoz)
 *  - WAIT n igazi várakozás; BEEP a PC-hangszórón szól; LIST/NEW/SAVE/LOAD működik */
#include "kernel.h"
#include "quartz4.h"

/* ---------- kifejezés-kiértékelő (rekurzív leszállás) ---------- */
static quartz4_t *eq;
static const char *ep;

static void eskip(void) { while (*ep == ' ' || *ep == '\t') ep++; }
static int eval_expr(void);

static int eval_factor(void) {
    eskip();
    int v = 0;
    if (*ep == '(') { ep++; v = eval_expr(); eskip(); if (*ep == ')') ep++; return v; }
    if (*ep == '-') { ep++; return -eval_factor(); }
    if (*ep == '+') { ep++; return eval_factor(); }
    if (*ep >= '0' && *ep <= '9') {
        while (*ep >= '0' && *ep <= '9') { v = v * 10 + (*ep - '0'); ep++; }
        return v;
    }
    if (!ep[1] && (*ep >= 'A' && *ep <= 'Z')) return eq->vars[*ep - 'A'];
    /* szó-utánzatok: PEEK/POINT/RND/KEY és egybetes változó */
    if (ep[0] == 'P' && ep[1] == 'E' && ep[2] == 'E' && ep[3] == 'K') {
        ep += 4; eskip(); if (*ep == '(') ep++;
        int a = eval_expr() & 0xFFF;
        eskip(); if (*ep == ')') ep++;
        return eq->ram[a];
    }
    if (ep[0] == 'P' && ep[1] == 'O' && ep[2] == 'I' && ep[3] == 'N' && ep[4] == 'T') {
        ep += 5; eskip(); if (*ep == '(') ep++;
        int x = eval_expr(); eskip(); if (*ep == ',') ep++;
        int y = eval_expr(); eskip(); if (*ep == ')') ep++;
        return q4k_point(eq, x, y);
    }
    if (ep[0] == 'R' && ep[1] == 'N' && ep[2] == 'D') {
        ep += 3; eskip(); if (*ep == '(') ep++;
        int n = eval_expr(); eskip(); if (*ep == ')') ep++;
        if (n < 1) n = 1;
        if (n > 64) n = 64;
        return (int)(app_rand() % (u32)n) + 1;
    }
    if (ep[0] == 'K' && ep[1] == 'E' && ep[2] == 'Y') {
        ep += 3; eskip(); if (*ep == '(') ep++; eskip(); if (*ep == ')') ep++;
        u8 k = eq->keys;
        if (k & 1) return 1;
        if (k & 2) return 2;
        if (k & 4) return 3;
        if (k & 8) return 4;
        if (k & 16) return 5;
        return 0;
    }
    if (*ep >= 'A' && *ep <= 'Z') return eq->vars[*ep - 'A'];
    if (*ep) ep++;
    return 0;
}
static int eval_term(void) {
    int v = eval_factor();
    for (;;) {
        eskip();
        if (*ep == '*') { ep++; v = v * eval_factor(); }
        else if (*ep == '/') { ep++; int d = eval_factor(); v = d ? v / d : 0; }
        else break;
    }
    return v;
}
static int eval_expr(void) {
    int v = eval_term();
    for (;;) {
        eskip();
        if (*ep == '+') { ep++; v = v + eval_term(); }
        else if (*ep == '-') { ep++; v = v - eval_term(); }
        else break;
    }
    return v;
}
/* kifejezés a *pp mutatott szövegből; a mutató a kifejezés végére áll */
static int qevalp(quartz4_t *q, const char **pp) {
    eq = q; ep = *pp;
    int v = eval_expr();
    *pp = ep;
    return v;
}
/* zárt kifejezés egy másolt karaktersoron */
static int qeval(quartz4_t *q, const char *s) {
    const char *p = s;
    return qevalp(q, &p);
}

/* ---------- feltétel ---------- */
static int eval_cond(quartz4_t *q, const char *c) {
    char buf[96];
    int i = 0;
    while (c[i] && i < 94) { buf[i] = c[i]; i++; }
    buf[i] = 0;
    int op = 0, pos = -1;
    for (int k = 0; buf[k]; k++) {
        if (buf[k] == '<' && buf[k + 1] == '=') { op = 1; pos = k; break; }
        if (buf[k] == '>' && buf[k + 1] == '=') { op = 2; pos = k; break; }
        if (buf[k] == '<' && buf[k + 1] == '>') { op = 3; pos = k; break; }
        if (buf[k] == '<') { op = 4; pos = k; break; }
        if (buf[k] == '>') { op = 5; pos = k; break; }
        if (buf[k] == '=') { op = 6; pos = k; break; }
    }
    if (!op) return qeval(q, c) != 0;
    char lhs[64], rhs[64];
    int n = pos < 63 ? pos : 63;
    for (int k = 0; k < n; k++) lhs[k] = buf[k];
    lhs[n] = 0;
    int extra = (op <= 3) ? 2 : 1;
    n = 0;
    for (int k = pos + extra; buf[k] && n < 62; k++) rhs[n++] = buf[k];
    rhs[n] = 0;
    int a = qeval(q, lhs), b = qeval(q, rhs);
    switch (op) {
        case 1: return a <= b;
        case 2: return a >= b;
        case 3: return a != b;
        case 4: return a < b;
        case 5: return a > b;
        default: return a == b;
    }
}

/* ---------- nagybetűsítés (idézőjelben belül nem) ---------- */
static void upcase_line(char *dst, const char *src, int maxlen) {
    int inq = 0;
    int n = 0;
    while (src[n] && n < maxlen - 1) {
        char c = src[n];
        if (c == '"') inq ^= 1;
        if (!inq && c >= 'a' && c <= 'z') c = (char)(c - 32);
        dst[n++] = c;
    }
    dst[n] = 0;
}

/* ---------- videó ---------- */
static void scroll_up(quartz4_t *q) {
    for (int y = 0; y < Q4_TEXT_H - 1; y++)
        for (int x = 0; x < Q4_TEXT_W; x++)
            q->vt[y * Q4_TEXT_W + x] = q->vt[(y + 1) * Q4_TEXT_W + x];
    for (int x = 0; x < Q4_TEXT_W; x++) q->vt[(Q4_TEXT_H - 1) * Q4_TEXT_W + x] = 0;
}
void q4k_putch(quartz4_t *q, int code) {
    if (code == 10) { q->cx = 0; q->cy++; }
    else if (code == 8) {
        if (q->cx > 0) { q->cx--; q->vt[q->cy * Q4_TEXT_W + q->cx] = 0; }
    }
    else if (code >= 32 && code < 127) {
        q->vt[q->cy * Q4_TEXT_W + q->cx] = (u8)code;
        q->cx++;
        if (q->cx >= Q4_TEXT_W) { q->cx = 0; q->cy++; }
    }
    if (q->cy >= Q4_TEXT_H) { scroll_up(q); q->cy = Q4_TEXT_H - 1; }
}
void q4k_print(quartz4_t *q, const char *s) {
    while (*s) q4k_putch(q, (u8)*s++);
    q4k_putch(q, 10);
}
void q4k_print_at(quartz4_t *q, int x, int y, const char *s) {
    if (x < 0) x = 0; if (x > 31) x = 31;
    if (y < 0) y = 0; if (y > 15) y = 15;
    q->cx = x; q->cy = y;
    while (*s) q4k_putch(q, (u8)*s++);
}
void q4k_clear(quartz4_t *q) {
    for (int i = 0; i < Q4_TEXT_W * Q4_TEXT_H; i++) q->vt[i] = 0;
    for (int i = 0; i < Q4_GFX_W * Q4_GFX_H / 8; i++) q->vg[i] = 0;
    q->cx = q->cy = 0;
}
void q4k_set_mode(quartz4_t *q, int m) { q->videoMode = m & 1; q4k_clear(q); }
void q4k_plot(quartz4_t *q, int x, int y) {
    if (x < 0 || x >= Q4_GFX_W || y < 0 || y >= Q4_GFX_H) return;
    q->vg[y * 8 + x / 8] |= (u8)(1 << (7 - (x & 7)));
}
void q4k_unplot(quartz4_t *q, int x, int y) {
    if (x < 0 || x >= Q4_GFX_W || y < 0 || y >= Q4_GFX_H) return;
    q->vg[y * 8 + x / 8] &= (u8)~(1 << (7 - (x & 7)));
}
int q4k_point(quartz4_t *q, int x, int y) {
    if (x < 0 || x >= Q4_GFX_W || y < 0 || y >= Q4_GFX_H) return 0;
    return (q->vg[y * 8 + x / 8] >> (7 - (x & 7))) & 1;
}
void q4k_line(quartz4_t *q, int x1, int y1, int x2, int y2) {
    int dx = x2 - x1; if (dx < 0) dx = -dx;
    int dy = y2 - y1; if (dy < 0) dy = -dy;
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        q4k_plot(q, x1, y1);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx)  { err += dx; y1 += sy; }
    }
    q->lastX = x2; q->lastY = y2;
}

/* ---------- beépített programok (2 eredeti + 3 Keringő-játék) ---------- */
static const struct { const char *name; const char *text; } builtins[] = {
{ "DEMO.BAS",
"10 MODE 1\n20 FOR I=0 TO 15\n30 PLOT I,I\n40 PLOT 63-I,I\n50 NEXT I\n"
"60 CIRCLE 32,24,10\n70 FILL 32,24\n" },
{ "FILL.BAS",
"10 BOX 10,10,54,38\n20 FILL 32,24\n30 PRINT \"MEGTOLTVE!\"\n" },
{ "PONG.BAS",
"10 MODE 1\n"
"20 POKE 100,30\n"
"25 POKE 101,20\n"
"30 POKE 102,1\n"
"35 POKE 103,1\n"
"40 POKE 110,28\n"
"45 LET S=0\n"
"50 UNPLOT PEEK(100),PEEK(101)\n"
"55 UNPLOT PEEK(100)+1,PEEK(101)\n"
"60 POKE 100,PEEK(100)+2*PEEK(102)-1\n"
"70 POKE 101,PEEK(101)+2*PEEK(103)-1\n"
"80 IF PEEK(100)=0 THEN POKE 102,1\n"
"90 IF PEEK(100)=62 THEN POKE 102,0\n"
"100 IF PEEK(101)=0 THEN POKE 103,1\n"
"110 IF PEEK(101)=45 THEN IF PEEK(100)>=PEEK(110) THEN IF PEEK(100)<=PEEK(110)+7 THEN POKE 103,0\n"
"120 IF PEEK(101)>46 THEN GOSUB 300\n"
"130 PLOT PEEK(100),PEEK(101)\n"
"135 PLOT PEEK(100)+1,PEEK(101)\n"
"140 IF KEY()=1 THEN POKE 110,PEEK(110)-2\n"
"150 IF KEY()=2 THEN POKE 110,PEEK(110)+2\n"
"160 IF PEEK(110)<1 THEN POKE 110,1\n"
"170 IF PEEK(110)>55 THEN POKE 110,55\n"
"180 LINE PEEK(110),47,PEEK(110)+7,47\n"
"190 WAIT 2\n"
"200 GOTO 50\n"
"300 REM LABDAVESZTES\n"
"310 POKE 100,30\n"
"320 POKE 101,20\n"
"330 INC S\n"
"340 PRINT @0,0 \"S:\"\n"
"350 PRINT @3,0 S\n"
"360 BEEP\n"
"370 RETURN\n" },
{ "METEOR.BAS",
"10 MODE 1\n"
"20 POKE 100,RND(15)*4\n"
"30 POKE 101,0\n"
"40 POKE 110,30\n"
"50 POKE 102,1\n"
"60 UNPLOT PEEK(100),PEEK(101)\n"
"70 POKE 101,PEEK(101)+PEEK(102)\n"
"80 IF PEEK(101)>42 THEN IF PEEK(100)>PEEK(110)-3 THEN IF PEEK(100)<PEEK(110)+5 THEN GOTO 300\n"
"90 PLOT PEEK(100),PEEK(101)\n"
"100 LINE PEEK(110),46,PEEK(110)+3,46\n"
"110 LINE PEEK(110),45,PEEK(110)+3,45\n"
"120 IF KEY()=1 THEN POKE 110,PEEK(110)-2\n"
"130 IF KEY()=2 THEN POKE 110,PEEK(110)+2\n"
"140 IF PEEK(110)<1 THEN POKE 110,1\n"
"150 IF PEEK(110)>59 THEN POKE 110,59\n"
"160 IF PEEK(101)>47 THEN GOTO 400\n"
"170 WAIT 1\n"
"180 GOTO 60\n"
"300 REM TALALAT\n"
"310 BEEP\n"
"320 MODE 0\n"
"330 PRINT \"TALALT! METEOR!\"\n"
"340 END\n"
"400 REM TULERTE\n"
"410 INC P\n"
"420 MODE 0\n"
"430 PRINT \"PONT:\"\n"
"440 PRINT P\n"
"450 MODE 1\n"
"460 GOTO 20\n" },
{ "KIGYO.BAS",
"10 MODE 1\n"
"20 POKE 100,32\n"
"30 POKE 101,30\n"
"40 POKE 102,28\n"
"50 POKE 103,26\n"
"60 POKE 110,24\n"
"70 POKE 111,24\n"
"80 POKE 112,24\n"
"90 POKE 113,24\n"
"100 POKE 120,1\n"
"110 POKE 105,RND(15)*4\n"
"120 POKE 115,RND(11)*4\n"
"130 PLOT PEEK(105),PEEK(115)\n"
"135 PLOT PEEK(105)+1,PEEK(115)+1\n"
"140 REM FEJ RAJZ\n"
"150 PLOT PEEK(100),PEEK(110)\n"
"160 PLOT PEEK(100)+1,PEEK(110)\n"
"170 PLOT PEEK(100),PEEK(110)+1\n"
"200 REM VEZERLES\n"
"210 IF KEY()=1 THEN IF PEEK(120)<>2 THEN POKE 120,1\n"
"220 IF KEY()=2 THEN IF PEEK(120)<>1 THEN POKE 120,2\n"
"230 IF KEY()=3 THEN IF PEEK(120)<>4 THEN POKE 120,3\n"
"240 IF KEY()=4 THEN IF PEEK(120)<>3 THEN POKE 120,4\n"
"300 REM FAROK TORLESE\n"
"310 UNPLOT PEEK(103),PEEK(113)\n"
"320 UNPLOT PEEK(103)+1,PEEK(113)\n"
"330 UNPLOT PEEK(103),PEEK(113)+1\n"
"340 UNPLOT PEEK(103)+1,PEEK(113)+1\n"
"400 REM TEST KOVETES\n"
"410 POKE 103,PEEK(102)\n"
"420 POKE 113,PEEK(112)\n"
"430 POKE 102,PEEK(101)\n"
"440 POKE 112,PEEK(111)\n"
"450 POKE 101,PEEK(100)\n"
"460 POKE 111,PEEK(110)\n"
"500 REM FEJ LEPTETES\n"
"510 IF PEEK(120)=1 THEN POKE 100,PEEK(100)+2\n"
"520 IF PEEK(120)=2 THEN POKE 100,PEEK(100)-2\n"
"530 IF PEEK(120)=3 THEN POKE 110,PEEK(110)-2\n"
"540 IF PEEK(120)=4 THEN POKE 110,PEEK(110)+2\n"
"600 REM KEREKEDES\n"
"610 IF PEEK(100)>62 THEN POKE 100,0\n"
"620 IF PEEK(110)>46 THEN POKE 110,0\n"
"630 IF PEEK(100)<0 THEN POKE 100,62\n"
"640 IF PEEK(110)<0 THEN POKE 110,46\n"
"700 REM ETEL?\n"
"710 IF PEEK(100)>PEEK(105)-2 THEN IF PEEK(100)<PEEK(105)+3 THEN IF PEEK(110)>PEEK(115)-2 THEN IF PEEK(110)<PEEK(115)+3 THEN GOSUB 800\n"
"750 WAIT 3\n"
"760 GOTO 140\n"
"800 REM ETEL\n"
"810 BEEP\n"
"820 POKE 105,RND(15)*4\n"
"830 POKE 115,RND(11)*4\n"
"840 INC S\n"
"850 PRINT @26,0 \"S:\"\n"
"860 PRINT @29,0 S\n"
"870 RETURN\n" },
{ "HELLO.BAS",
"10 PRINT \"QUARTZ-4B + KERINGO OS!\"\n"
"20 FOR I=1 TO 15\n"
"30 PRINT I\n"
"40 NEXT I\n" },
};
int q4k_builtin_count(void) { return (int)(sizeof(builtins) / sizeof(builtins[0])); }
const char *q4k_builtin_name(int i) { return builtins[i % q4k_builtin_count()].name; }
const char *q4k_builtin_text(int i) { return builtins[i % q4k_builtin_count()].text; }

/* ---------- programtár: szöveggé / szövegből ---------- */
static int prog_to_text(quartz4_t *q, char *out, int max) {
    int p = 0;
    for (int i = 0; i < q->np; i++) {
        if (p >= max - 8) break;
        ksnprintf(out + p, max - p, "%d %s\n", q->pnum[i], q->ptxt[i]);
        int n = 0;
        while (out[p + n] && p + n < max - 1) n++;
        p += n;
    }
    return p;
}
static void load_text(quartz4_t *q, const char *text) {
    q->np = 0;
    const char *p = text;
    while (*p && q->np < Q4_PROG_MAX) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (!(*p >= '0' && *p <= '9')) break;
        int num = 0;
        while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        char tmp[Q4_PROG_LEN];
        int n = 0;
        while (*p && *p != '\n' && *p != '\r' && n < Q4_PROG_LEN - 1) tmp[n++] = *p++;
        tmp[n] = 0;
        while (*p && *p != '\n' && *p != '\r') p++;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
        upcase_line(q->ptxt[q->np], tmp, Q4_PROG_LEN);
        q->pnum[q->np] = num;
        if (n > 0) q->np++;
    }
}

/* ---------- fájlkezelés a ramfs-en ---------- */
int q4k_save(quartz4_t *q, const char *name) {
    static char buf[4096];
    int len = prog_to_text(q, buf, (int)sizeof(buf));
    char rname[40];
    ksnprintf(rname, sizeof(rname), "Q_%s", name);
    return ramfs_write(rname, buf, len);
}
int q4k_load(quartz4_t *q, const char *name) {
    static char buf[4096];
    int outlen = 0;
    char rname[40];
    ksnprintf(rname, sizeof(rname), "Q_%s", name);
    if (!ramfs_read(rname, buf, (int)sizeof(buf) - 1, &outlen)) return 0;
    buf[outlen] = 0;
    load_text(q, buf);
    return 1;
}
int q4k_files(quartz4_t *q) {
    q4k_print(q, "FILES:");
    for (int i = 0; i < q4k_builtin_count(); i++) {
        q4k_putch(q, 10);
        q4k_print_at(q, 2, q->cy, builtins[i].name);
    }
    int n = ramfs_count();
    for (int i = 0; i < n; i++) {
        const char *nm = ramfs_name(i);
        if (nm[0] == 'Q' && nm[1] == '_') {
            q4k_putch(q, 10);
            q4k_print_at(q, 2, q->cy, nm + 2);
        }
    }
    return 1;
}

/* ---------- sorok tárolása ---------- */
static int find_line(quartz4_t *q, int num) {
    for (int i = 0; i < q->np; i++) if (q->pnum[i] == num) return i;
    return -1;
}
static void store_line(quartz4_t *q, int num, const char *txt) {
    char up[Q4_PROG_LEN];
    upcase_line(up, txt, Q4_PROG_LEN);
    int i = find_line(q, num);
    if (i < 0) {
        if (q->np >= Q4_PROG_MAX) return;
        i = q->np;
        for (int k = 0; k < q->np; k++) if (q->pnum[k] > num) { i = k; break; }
        for (int k = q->np; k > i; k--) {
            q->pnum[k] = q->pnum[k - 1];
            for (int c = 0; c < Q4_PROG_LEN; c++) q->ptxt[k][c] = q->ptxt[k - 1][c];
        }
        q->np++;
    }
    q->pnum[i] = num;
    int n = 0;
    while (up[n] && n < Q4_PROG_LEN - 1) { q->ptxt[i][n] = up[n]; n++; }
    q->ptxt[i][n] = 0;
}

/* ---------- gépi kód (16 utasítás, a HTML hű tolmácsolásában) ---------- */
static int key_nib(quartz4_t *q) {
    u8 k = q->keys;
    if (k & 1) return 1;
    if (k & 2) return 2;
    if (k & 4) return 3;
    if (k & 8) return 4;
    if (k & 16) return 5;
    return 0;
}
void q4k_mcu_run(quartz4_t *q, int maxcycles) {
    q->halted = 0;
    for (int cyc = 0; cyc < maxcycles && !q->halted; cyc++) {
        u8 ir = q->ram[q->pc & 0xFFF];
        q->pc = (q->pc + 1) & 0xFFF;
        int op = (ir >> 4) & 0xF, n = ir & 0xF;
        switch (op) {
            case 0x0: break;
            case 0x1: q->a = n; break;
            case 0x2: { int s = q->a + n; q->fc = s > 15 ? 1 : 0; q->a = s & 0xF; break; }
            case 0x3: { int d = q->a - n; q->fc = d < 0 ? 1 : 0; q->a = (d + 16) & 0xF; break; }
            case 0x4: q->ram[(q->x + n) & 0xFFF] = (u8)q->a; break;
            case 0x5: q->pc = (q->x + n) & 0xFFF; break;
            case 0x6: if (q->fz) q->pc = (q->x + n) & 0xFFF; break;
            case 0x7: if (q->fc) q->pc = (q->x + n) & 0xFFF; break;
            case 0x8: q->x = (q->x & 0xFF0) | n; break;
            case 0x9: q->x = (q->x + 1) & 0xFFF; break;
            case 0xA: q->x = (q->x - 1 + 0x1000) & 0xFFF; break;
            case 0xB:
                q->ram[q->sp-- & 0xFFF] = (u8)(q->pc & 0xF);
                q->ram[q->sp-- & 0xFFF] = (u8)((q->pc >> 4) & 0xF);
                q->ram[q->sp-- & 0xFFF] = (u8)((q->pc >> 8) & 0xF);
                q->pc = (q->x + n) & 0xFFF;
                break;
            case 0xC: {
                int hi = q->ram[++q->sp & 0xFFF];
                int mid = q->ram[++q->sp & 0xFFF];
                int lo = q->ram[++q->sp & 0xFFF];
                q->pc = ((hi << 8) | (mid << 4) | lo) & 0xFFF;
                break; }
            case 0xD: q4k_putch(q, 0x30 + q->a); break;
            case 0xE: q->a = key_nib(q); break;        /* IN: KEY() bővítés */
            case 0xF: q->halted = 1; break;
        }
        q->fz = (q->a == 0) ? 1 : 0;
        q->fn = (q->a & 0x8) ? 1 : 0;
        q->mcu_cycles++;
    }
}

/* ---------- egy sor végrehajtása ---------- */
static int exec_line(quartz4_t *q, const char *line0);

static const char *skipsp(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }
static int wordcmp(const char **p, const char *w) {
    const char *s = skipsp(*p);
    int n = 0;
    while (w[n]) n++;
    for (int i = 0; i < n; i++) { if (s[i] != w[i]) return 0; }
    if (n && ((s[n] >= 'A' && s[n] <= 'Z') || (s[n] >= 'a' && s[n] <= 'z'))) return 0;
    *p = s + n;
    return 1;
}

static int exec_line(quartz4_t *q, const char *line0) {
    const char *p = line0;
    p = skipsp(p);
    if (!*p) return 0;
    if (wordcmp(&p, "REM")) return 0;
    /* hozzárendelés: X=kifejezés */
    if (p[0] >= 'A' && p[0] <= 'Z' && p[1] == '=') {
        q->vars[p[0] - 'A'] = qeval(q, p + 2) & 0xF;
        return 0;
    }
    if (wordcmp(&p, "LET")) {
        p = skipsp(p);
        if (p[0] >= 'A' && p[0] <= 'Z' && p[1] == '=') {
            q->vars[p[0] - 'A'] = qeval(q, p + 2) & 0xF;
        }
        return 0;
    }
    if (wordcmp(&p, "CLS")) { q4k_clear(q); return 0; }
    if (wordcmp(&p, "MODE")) { q4k_set_mode(q, qeval(q, p)); return 0; }
    if (wordcmp(&p, "PRINT") || wordcmp(&p, "?")) {
        p = skipsp(p);
        int at = 0, ax = 0, ay = 0;
        if (*p == '@') {
            p++;
            ax = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++;
            ay = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++;
            at = 1;
        }
        char out[64];
        int n = 0;
        p = skipsp(p);
        while (*p && n < 60) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && n < 60) out[n++] = *p++;
                if (*p == '"') p++;
            } else if (*p == ';') { p++; }
            else if (*p == ',') { out[n++] = ' '; p++; }
            else {
                const char *start = p;
                int v = qevalp(q, &p);
                (void)start;
                char tmp[8];
                ksnprintf(tmp, sizeof(tmp), "%d", v);
                for (int k = 0; tmp[k] && n < 60; k++) out[n++] = tmp[k];
            }
            p = skipsp(p);
        }
        out[n] = 0;
        if (at) q4k_print_at(q, ax, ay, out);
        else q4k_print(q, out);
        return 0;
    }
    if (wordcmp(&p, "PLOT"))   { int a = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++; int b = qevalp(q, &p); q4k_plot(q, a, b); return 0; }
    if (wordcmp(&p, "UNPLOT")) { int a = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++; int b = qevalp(q, &p); q4k_unplot(q, a, b); return 0; }
    if (wordcmp(&p, "LINE")) {
        int a[4];
        for (int i = 0; i < 4; i++) { a[i] = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++; }
        q4k_line(q, a[0], a[1], a[2], a[3]);
        return 0;
    }
    if (wordcmp(&p, "DRAWTO")) {
        int x = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++;
        int y = qevalp(q, &p);
        q4k_line(q, q->lastX, q->lastY, x, y);
        return 0;
    }
    if (wordcmp(&p, "BOX")) {
        int a[4];
        for (int i = 0; i < 4; i++) { a[i] = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++; }
        q4k_line(q, a[0], a[1], a[2], a[1]);
        q4k_line(q, a[2], a[1], a[2], a[3]);
        q4k_line(q, a[2], a[3], a[0], a[3]);
        q4k_line(q, a[0], a[3], a[0], a[1]);
        return 0;
    }
    if (wordcmp(&p, "CIRCLE")) {
        int cx = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++;
        int cy = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++;
        int r = qevalp(q, &p);
        int x = 0, y = r, d = 3 - 2 * r;
        while (y >= x) {
            q4k_plot(q, cx + x, cy + y); q4k_plot(q, cx - x, cy + y);
            q4k_plot(q, cx + x, cy - y); q4k_plot(q, cx - x, cy - y);
            q4k_plot(q, cx + y, cy + x); q4k_plot(q, cx - y, cy + x);
            q4k_plot(q, cx + y, cy - x); q4k_plot(q, cx - y, cy - x);
            x++;
            if (d > 0) { y--; d += 4 * (x - y) + 10; }
            else d += 4 * x + 6;
        }
        return 0;
    }
    if (wordcmp(&p, "TRIANGLE")) {
        int a[6];
        for (int i = 0; i < 6; i++) { a[i] = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++; }
        q4k_line(q, a[0], a[1], a[2], a[3]);
        q4k_line(q, a[2], a[3], a[4], a[5]);
        q4k_line(q, a[4], a[5], a[0], a[1]);
        return 0;
    }
    if (wordcmp(&p, "FILL")) {
        int sx = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++;
        int sy = qevalp(q, &p);
        static short stk[Q4_GFX_W * Q4_GFX_H];
        int sp = 0;
        if (!q4k_point(q, sx, sy)) {
            stk[sp++] = (short)(sy * Q4_GFX_W + sx);
            while (sp > 0) {
                int idx = stk[--sp];
                int x = idx % Q4_GFX_W, y = idx / Q4_GFX_W;
                if (x < 0 || x >= Q4_GFX_W || y < 0 || y >= Q4_GFX_H) continue;
                if (q4k_point(q, x, y)) continue;
                q4k_plot(q, x, y);
                if (sp < Q4_GFX_W * Q4_GFX_H - 4) {
                    stk[sp++] = (short)(y * Q4_GFX_W + x + 1);
                    stk[sp++] = (short)(y * Q4_GFX_W + x - 1);
                    stk[sp++] = (short)((y + 1) * Q4_GFX_W + x);
                    stk[sp++] = (short)((y - 1) * Q4_GFX_W + x);
                }
            }
        }
        return 0;
    }
    if (wordcmp(&p, "INC")) { p = skipsp(p); if (p[0] >= 'A' && p[0] <= 'Z') q->vars[p[0] - 'A'] = (q->vars[p[0] - 'A'] + 1) & 0xF; return 0; }
    if (wordcmp(&p, "DEC")) { p = skipsp(p); if (p[0] >= 'A' && p[0] <= 'Z') q->vars[p[0] - 'A'] = (q->vars[p[0] - 'A'] + 15) & 0xF; return 0; }
    if (wordcmp(&p, "GOTO")) { q->curline = qeval(q, p); return 2; }
    if (wordcmp(&p, "GOSUB")) {
        if (q->ng < 8) q->gosub[q->ng++] = q->curline;
        q->curline = qeval(q, p);
        return 2;
    }
    if (wordcmp(&p, "RETURN")) {
        if (q->ng > 0) q->curline = q->gosub[--q->ng];
        return 2;
    }
    if (wordcmp(&p, "IF")) {
        const char *s = skipsp(p);
        const char *then = 0;
        for (const char *c = s; *c; c++) {
            if (c[0]=='T'&&c[1]=='H'&&c[2]=='E'&&c[3]=='N') { then = c; break; }
        }
        if (!then) return 0;
        char cond[80];
        int n = 0;
        for (const char *c = s; c < then && n < 78; c++) if (*c != ' ') cond[n++] = *c;
        cond[n] = 0;
        const char *body = skipsp(then + 4);
        if (eval_cond(q, cond)) {
            if (*body >= '0' && *body <= '9') { q->curline = qeval(q, body); return 2; }
            return exec_line(q, body);
        }
        return 0;
    }
    if (wordcmp(&p, "FOR")) {
        p = skipsp(p);
        if (!(p[0] >= 'A' && p[0] <= 'Z') || p[1] != '=') return 0;
        int var = p[0] - 'A';
        p += 2;
        int start = qevalp(q, &p); p = skipsp(p);
        wordcmp(&p, "TO");
        int end = qevalp(q, &p);
        int step = 1;
        const char *sp2 = 0;
        for (const char *c = p; *c; c++)
            if (c[0]=='S'&&c[1]=='T'&&c[2]=='E'&&c[3]=='P') { sp2 = c; break; }
        if (sp2) { const char *t = skipsp(sp2 + 4); step = qeval(q, t); }
        if (q->nf < 6) {
            q->forst[q->nf].var = var;
            q->forst[q->nf].end = end;
            q->forst[q->nf].step = step;
            q->forst[q->nf].line = q->curline;
            q->nf++;
        }
        q->vars[var] = start & 0xF;
        return 0;
    }
    if (wordcmp(&p, "NEXT")) {
        if (q->nf > 0) {
            int f = q->nf - 1;
            q->vars[q->forst[f].var] = (q->vars[q->forst[f].var] + q->forst[f].step) & 0xF;
            int v = q->vars[q->forst[f].var];
            if ((q->forst[f].step > 0 && v <= q->forst[f].end) ||
                (q->forst[f].step < 0 && v >= q->forst[f].end)) {
                q->curline = q->forst[f].line;
                return 2;
            }
            q->nf--;
        }
        return 0;
    }
    if (wordcmp(&p, "WHILE")) {
        /* feltétel eltárolása; ha hamis, a WEND utáni sorra állunk */
        char cond[64];
        int n = 0;
        for (const char *c = skipsp(p); *c && n < 62; c++) cond[n++] = *c;
        cond[n] = 0;
        int li = find_line(q, q->curline);
        if (q->nw < 4) {
            q->whst[q->nw].line = q->curline;
            for (int k = 0; k < 64; k++) q->whst[q->nw].cond[k] = k < n ? cond[k] : 0;
            q->nw++;
        }
        if (eval_cond(q, cond)) return 0;
        /* hamis: ugorj a WEND utáni sorra */
        int depth = q->nw;
        for (int k = li + 1; k < q->np; k++) {
            const char *t = q->ptxt[k];
            if (wordcmp(&t, "WHILE")) depth++;
            else if (wordcmp(&t, "WEND")) {
                depth--;
                if (depth == q->nw) {
                    /* WEND megtalálva: a következő sor */
                    if (k + 1 < q->np) { q->curline = q->pnum[k + 1]; return 2; }
                    q->running = 0; q4k_print(q, "READY."); return 1;
                }
            }
        }
        q->nw--;
        return 0;
    }
    if (wordcmp(&p, "WEND")) {
        if (q->nw > 0) {
            q->curline = q->whst[q->nw - 1].line;
            return 2;   /* a WHILE újra értékeli a feltételt */
        }
        return 0;
    }
    if (wordcmp(&p, "REPEAT")) {
        if (q->nr < 4) { q->rpst[q->nr].line = q->curline; q->nr++; }
        return 0;
    }
    if (wordcmp(&p, "UNTIL")) {
        if (q->nr > 0) {
            if (!eval_cond(q, p)) {
                q->curline = q->rpst[q->nr - 1].line;
                return 2;
            }
            q->nr--;
        }
        return 0;
    }
    if (wordcmp(&p, "POKE")) {
        int a = qevalp(q, &p); p = skipsp(p); if (*p == ',') p++;
        int v = qevalp(q, &p);
        q->ram[a & 0xFFF] = (u8)(v & 0xFF);
        return 0;
    }
    if (wordcmp(&p, "SYS") || wordcmp(&p, "CALL")) {
        int a = qeval(q, p) & 0xFFF;
        q->pc = a;
        q4k_mcu_run(q, 10000);
        return 0;
    }
    if (wordcmp(&p, "WAIT")) {
        int n = qeval(q, p);
        if (n < 0) n = 0;
        if (n > 100) n = 100;
        q->wait_until = tick_count() + (u32)n * 2;   /* 1 egység = 20 ms */
        return 0;
    }
    if (wordcmp(&p, "BEEP")) { sound_beep(1200, 3); return 0; }
    if (wordcmp(&p, "END") || wordcmp(&p, "STOP")) return 1;
    if (wordcmp(&p, "LIST")) {
        for (int i = 0; i < q->np; i++) {
            char ln[104];
            ksnprintf(ln, sizeof(ln), "%d %s", q->pnum[i], q->ptxt[i]);
            q4k_print(q, ln);
        }
        return 0;
    }
    if (wordcmp(&p, "NEW")) { q->np = 0; for (int i = 0; i < 26; i++) q->vars[i] = 0; return 0; }
    if (wordcmp(&p, "RUN")) return 0;
    if (wordcmp(&p, "SAVE")) {
        p = skipsp(p);
        char name[16];
        int n = 0;
        while (*p && *p != '"' && n < 14) name[n++] = *p++;
        name[n] = 0;
        if (!n) ksnprintf(name, sizeof(name), "UNTITLED.BAS");
        if (q4k_save(q, name)) {
            char m[40];
            ksnprintf(m, sizeof(m), "SAVED %s", name);
            q4k_print(q, m);
        }
        return 0;
    }
    if (wordcmp(&p, "LOAD")) {
        p = skipsp(p);
        char name[16];
        int n = 0;
        while (*p && *p != '"' && n < 14) name[n++] = *p++;
        name[n] = 0;
        /* megkönnyítés: .BAS kiterjesztés automatikusan */
        char full[20];
        int hasdot = 0;
        for (int k = 0; name[k]; k++) if (name[k] == '.') hasdot = 1;
        if (!hasdot) ksnprintf(full, sizeof(full), "%s.BAS", name);
        else ksnprintf(full, sizeof(full), "%s", name);
        for (int i = 0; i < q4k_builtin_count(); i++) {
            if (vfs_strcmp(q4k_builtin_name(i), full) || vfs_strcmp(q4k_builtin_name(i), name)) {
                load_text(q, q4k_builtin_text(i));
                char m[40];
                ksnprintf(m, sizeof(m), "LOADED %s", q4k_builtin_name(i));
                q4k_print(q, m);
                return 0;
            }
        }
        if (q4k_load(q, full) || q4k_load(q, name)) {
            char m[40];
            ksnprintf(m, sizeof(m), "LOADED %s", name);
            q4k_print(q, m);
        } else {
            q4k_print(q, "?FILE NOT FOUND");
        }
        return 0;
    }
    if (wordcmp(&p, "FILES") || wordcmp(&p, "DIR")) { q4k_files(q); return 0; }
    if (wordcmp(&p, "DELETE")) return 0;   /* ramfs törlés még nincs */
    if (wordcmp(&p, "HELP")) { q4k_print(q, "F1 = SEGEDLET A KERINGOBAN"); return 0; }
    /* ismeretlen: kifejezésként */
    int v = qeval(q, line0);
    char tmp[12];
    ksnprintf(tmp, sizeof(tmp), "%d", v);
    q4k_print(q, tmp);
    return 0;
}

/* ---------- parancs (közvetlen mód) ---------- */
int q4k_command(quartz4_t *q, const char *line) {
    char up[Q4_PROG_LEN];
    upcase_line(up, line, Q4_PROG_LEN);
    const char *p = skipsp(up);
    if (!*p) return 0;
    if (*p >= '0' && *p <= '9') {
        int num = 0;
        while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; }
        p = skipsp(p);
        if (*p) store_line(q, num, p);
        else {
            int i = find_line(q, num);
            if (i >= 0) {
                for (int k = i; k < q->np - 1; k++) {
                    q->pnum[k] = q->pnum[k + 1];
                    for (int c = 0; c < Q4_PROG_LEN; c++) q->ptxt[k][c] = q->ptxt[k + 1][c];
                }
                q->np--;
            }
        }
        return 0;
    }
    if (wordcmp(&p, "RUN")) { q4k_run(q); return 0; }
    int r = exec_line(q, up);
    return r;
}

void q4k_run(quartz4_t *q) {
    if (q->np == 0) { q4k_print(q, "?NO PROGRAM"); return; }
    q->running = 1;
    q->curline = q->pnum[0];
    q->ng = q->nf = q->nw = q->nr = 0;
}

/* futó program léptetése: maxlines sort futtat egy hívásban */
int q4k_poll(quartz4_t *q, int maxlines) {
    if (!q->running) return 0;
    if ((s32)(tick_count() - q->wait_until) < 0) return 1;   /* WAIT aktív */
    for (int n = 0; n < maxlines; n++) {
        int li = find_line(q, q->curline);
        if (li < 0) {
            int best = -1;
            for (int i = 0; i < q->np; i++) if (q->pnum[i] > q->curline) { best = i; break; }
            if (best < 0) { q->running = 0; q4k_print(q, "READY."); return 0; }
            li = best;
            q->curline = q->pnum[li];
        }
        int r = exec_line(q, q->ptxt[li]);
        if (r == 1) { q->running = 0; q4k_print(q, "READY."); return 0; }
        if (r == 2) continue;    /* ugrás */
        int best = -1;
        for (int i = 0; i < q->np; i++) if (q->pnum[i] > q->curline) { best = i; break; }
        if (best < 0) { q->running = 0; q4k_print(q, "READY."); return 0; }
        q->curline = q->pnum[best];
    }
    return 1;
}

/* ---------- init ---------- */
void q4k_reset(quartz4_t *q) {
    for (int i = 0; i < Q4_RAM; i++) q->ram[i] = 0;
    q->pc = q->a = q->x = 0;
    q->sp = 0xFF0;
    q->halted = 0;
    q->fz = q->fn = q->fc = q->fv = 0;
    q4k_clear(q);
    q4k_set_mode(q, 0);
    for (int i = 0; i < 26; i++) q->vars[i] = 0;
    q->running = 0;
    q->ng = q->nf = q->nw = q->nr = 0;
    q->keys = 0;
    q->wait_until = 0;
    q->mcu_cycles = 0;
}
void q4k_init(quartz4_t *q) {
    for (u32 i = 0; i < sizeof(*q); i++) ((u8 *)q)[i] = 0;
    q->np = 0;
    q4k_reset(q);
    q4k_print(q, "QUARTZ-4B 500HZ");
    q4k_print(q, "4-BIT CPU 4096 NIBBLES");
    q4k_print(q, "");
    q4k_print(q, "READY.");
}
