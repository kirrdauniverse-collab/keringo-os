/* app_kirrdai.c — KIRRDA AI 0.2: helyi, szabály-alapú asszisztens a Keringő OS-ben.
 * Származás: a 0.2-es "Keringo Mega Pack" PR-ből került be (app_kirrdai.c),
 * karbantartói javításokat kapott — a 0.5.1-ben kinőve igazi "kis aggyá".
 * Őszinte architektúra: nincs benne nagy nyelvi modell — kulcsszó-szabályok +
 * tanítható tudásfájl (KIRRDA.TXT) + valódi képességek:
 *   - rendszerkérdések, matek-kifejezések (rekurzív leszálló parser)
 *   - alkalmazások indítása természetes nyelven ("indítsd a festőt")
 *   - fájlkeresés a lemezeken ("keres: .BAS")
 *   - jegyzetek ("jegyezd: ..."), tanítás ("tanits: kerdes = valasz")
 *   - internet: DNS-névfeloldás + egyszerű HTTP-oldal letöltése szövegként
 *     (HTTPS/TLS nincs — őszintén szól)
 * Minden válasz helyben születik; offline is működik. */
#include "kernel.h"
#include "net.h"

#define KLINE 96
#define KHIST 60

static char hist[KHIST][KLINE];
static u8   hstyle[KHIST];          /* 0=te, 1=KIRRDA, 2=info, 3=hiba */
static int  hn;
static int  hview;                  /* 0 = alul a legfrissebb; n = n sorral feljebb */
static char q[KLINE];
static int  qn;
static int  greeted = 0;

/* tanult tudás + megjegyzések (KIRRDA.TXT) */
#define KTAN 48
static char tan_q[KTAN][64], tan_a[KTAN][KLINE];
static int  tn;
#define KMEM 8
static char mem[KMEM][80];
static int  mn;
static int  loaded = 0;

static int lc(char c) { if (c >= 'A' && c <= 'Z') return c + 32; return c; }

/* kis-nagy betű független tartalmazás */
static int has(const char *s, const char *k) {
    for (int i = 0; s[i]; i++) {
        int j = 0;
        while (k[j] && lc(s[i + j]) == lc(k[j])) j++;
        if (!k[j]) return 1;
    }
    return 0;
}

static void sayx(const char *s, u8 st);

/* ---------- HTML szöveg-kivonat az internet-oldalakhoz ---------- */
static void web_text(u8 *body, u32 len) {
    char line[88];
    int li = 0, shown = 0, in_tag = 0;
    u32 i = 0;
    while (i < len && shown < 16) {
        char c = (char)body[i];
        if (c == '<') {
            const char *t = (const char *)body + i + 1;
            int tl = 0;
            while (t[tl] && t[tl] != '>' && tl < 12) tl++;
            int block = 0;
            if (tl >= 2) {
                char a = lc(t[0]), b = lc(t[1]);
                if ((a=='b'&&b=='r')||(a=='p'&&b=='>')||(a=='p'&&b==' ')||
                    (a=='/'&&b=='p')||(a=='/'&&b=='d')||(a=='/'&&b=='l')||
                    (a=='/'&&b=='t')||(a=='/'&&b=='h')||(a=='d'&&b=='i')||
                    (a=='l'&&b=='i')||(a=='t'&&b=='r')||(a=='h'&&b=='1')||
                    (a=='h'&&b=='2')||(a=='t'&&b=='i')) block = 1;
            }
            if (block && li) { line[li] = 0; sayx(line, 2); shown++; li = 0; }
            in_tag = 1; i++; continue;
        }
        if (c == '>') { in_tag = 0; i++; continue; }
        if (in_tag) { i++; continue; }
        if (c == '&') {
            if (i + 5 < len && body[i+1]=='a'&&body[i+2]=='m'&&body[i+3]=='p'&&body[i+4]==';') {
                if (li < 86) line[li++] = '&'; i += 5; continue;
            }
            if (i + 4 < len && body[i+1]=='l'&&body[i+2]=='t'&&body[i+3]==';') {
                if (li < 86) line[li++] = '<'; i += 4; continue;
            }
            if (i + 4 < len && body[i+1]=='g'&&body[i+2]=='t'&&body[i+3]==';') {
                if (li < 86) line[li++] = '>'; i += 4; continue;
            }
            while (i < len && body[i] != ';' && body[i] != '<') i++;
            if (i < len && body[i] == ';') i++;
            if (li < 86) line[li++] = ' ';
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (li) { line[li] = 0; sayx(line, 2); shown++; li = 0; }
            i++; continue;
        }
        if (c == '\t') c = ' ';
        if (c >= 32 && c < 127 && li < 86) line[li++] = c;
        if (li >= 86) { line[li] = 0; sayx(line, 2); shown++; li = 0; }
        i++;
    }
    if (li && shown < 16) { line[li] = 0; sayx(line, 2); }
}

/* ---------- matek: rekurzív leszálló kifejezés-kiértékelő ---------- */
typedef struct { const char *p; int err; } ev_t;
static long ev_expr(ev_t *e);
static long ev_fact(ev_t *e) {
    while (*e->p == ' ') e->p++;
    if (*e->p == '(') {
        e->p++;
        long v = ev_expr(e);
        while (*e->p == ' ') e->p++;
        if (*e->p == ')') e->p++; else e->err = 1;
        return v;
    }
    if (*e->p == '-') { e->p++; return -ev_fact(e); }
    if (*e->p >= '0' && *e->p <= '9') {
        long v = 0;
        while (*e->p >= '0' && *e->p <= '9') v = v * 10 + (*e->p++ - '0');
        return v;
    }
    e->err = 1;
    return 0;
}
static long ev_term(ev_t *e) {
    long v = ev_fact(e);
    for (;;) {
        while (*e->p == ' ') e->p++;
        char op = *e->p;
        if (op != '*' && op != '/' && op != '%') return v;
        e->p++;
        long r = ev_fact(e);
        if (op == '*') v *= r;
        else if (r == 0) { e->err = 2; return 0; }
        else if (op == '/') v /= r;
        else v %= r;
    }
}
static long ev_expr(ev_t *e) {
    long v = ev_term(e);
    for (;;) {
        while (*e->p == ' ') e->p++;
        char op = *e->p;
        if (op != '+' && op != '-') return v;
        e->p++;
        long r = ev_term(e);
        v = (op == '+') ? v + r : v - r;
    }
}
static int is_mathy(const char *s) {
    int digit = 0, op = 0;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') digit = 1;
        else if (*s=='+'||*s=='-'||*s=='*'||*s=='/'||*s=='%'||*s=='('||*s==')') op = 1;
        else if (*s == ' ') ;
        else return 0;
    }
    return digit && op;
}

/* ---------- alkalmazás-keresés természetes nyelvből ---------- */
static int find_app(const char *s) {
    int best = -1, score = 0;
    for (int i = 1; i < APP_COUNT; i++)
        for (int L = 0; L < 2; L++) {
            const char *nev = app_list[i].nev[L];
            if (has(s, nev)) {
                int sl = 0; while (nev[sl]) sl++;
                if (sl > score) { score = sl; best = i; }
            }
        }
    if (best >= 0) return best;
    /* szavankénti egyeztetés magyar toldalékokkal: a szó előtagjait próbáljuk */
    int w0 = 0;
    while (s[w0]) {
        int w1 = w0;
        while (s[w1] && s[w1] != ' ') w1++;
        int wl = w1 - w0;
        if (wl >= 4) {
            char word[24];
            int k;
            for (k = 0; k < wl && k < 22; k++) word[k] = s[w0 + k];
            word[k] = 0;
            for (int pl = wl; pl >= 4; pl--) {
                word[pl] = 0;
                for (int i = 1; i < APP_COUNT; i++)
                    for (int L = 0; L < 2; L++)
                        if (has(app_list[i].nev[L], word) && pl > score) {
                            score = pl; best = i;
                        }
            }
        }
        w0 = w1 + 1;
    }
    return best;
}

/* ---------- fájlkeresés a lemezeken ---------- */
static void cmd_search_files(const char *term) {
    static char names[48][64];
    static u32 sizes[48];
    static u8  flags[48];
    const char *dirs[4] = { "/mem", "/sys", "/usb", "/cd" };
    const char *dnv[4]  = { "mem", "sys", "usb", "cd" };
    char b[KLINE];
    int hits = 0;
    for (int d = 0; d < 4; d++) {
        int n = vfs_list(dirs[d], names, sizes, flags, 48);
        if (n <= 0) continue;
        for (int i = 0; i < n && hits < 10; i++) {
            if (!has(names[i], term)) continue;
            ksnprintf(b, sizeof(b), "  /%s/%s  (%u KB)%s", dnv[d], names[i],
                      sizes[i] ? sizes[i] / 1024 : 0, (flags[i] & 1) ? " [mappa]" : "");
            sayx(b, 2);
            hits++;
        }
    }
    if (!hits) {
        ksnprintf(b, sizeof(b), "Nincs talalat erre: \"%s\" (mem, sys, usb, cd).", term);
        sayx(b, 3);
    } else {
        ksnprintf(b, sizeof(b), "Osszesen %d talalat%s.", hits, hits == 10 ? " (legfeljebb 10 mutatok)" : "");
        sayx(b, 2);
    }
}

/* ---------- tanult tudás egyeztetése ---------- */
static int match_learned(const char *s) {
    int best = -1;
    int bestsc = 55;
    for (int i = 0; i < tn; i++) {
        if (has(s, tan_q[i])) return i;
        int tot = 0, got = 0;
        for (int w = 0; tan_q[i][w]; ) {
            while (tan_q[i][w] == ' ') w++;
            int e = w;
            while (tan_q[i][e] && tan_q[i][e] != ' ') e++;
            if (e > w) {
                char word[32];
                int wl = 0;
                for (int k = w; k < e && wl < 30; k++) word[wl++] = tan_q[i][k];
                word[wl] = 0;
                tot++;
                if (wl >= 3 && has(s, word)) got++;
            }
            w = e;
        }
        if (tot > 0 && got * 100 / tot > bestsc) {
            bestsc = got * 100 / tot; best = i;
        }
    }
    return best;
}

/* ---------- KIRRDA.TXT perzisztencia ---------- */
static void save_brain(void) {
    static char buf[6 * 1024];
    int o = 0;
    for (int i = 0; i < tn && o < (int)sizeof(buf) - 220; i++) {
        buf[o++] = 'T'; buf[o++] = '|';
        for (int k = 0; tan_q[i][k] && o < (int)sizeof(buf) - 220; k++) buf[o++] = tan_q[i][k];
        buf[o++] = '|';
        for (int k = 0; tan_a[i][k] && o < (int)sizeof(buf) - 120; k++) buf[o++] = tan_a[i][k];
        buf[o++] = '\n';
    }
    for (int i = 0; i < mn && o < (int)sizeof(buf) - 100; i++) {
        buf[o++] = 'M'; buf[o++] = '|';
        for (int k = 0; mem[i][k] && o < (int)sizeof(buf) - 100; k++) buf[o++] = mem[i][k];
        buf[o++] = '\n';
    }
    buf[o] = 0;
    vfs_write_file("/mem/KIRRDA.TXT", buf, o);
    if (sys_mounted()) vfs_write_file("/sys/KIRRDA.TXT", buf, o);
}

static void load_brain(void) {
    static char buf[6 * 1024];
    int len = 0;
    tn = 0; mn = 0;
    if (!vfs_read_file("/mem/KIRRDA.TXT", buf, sizeof(buf) - 1, &len)) {
        if (!vfs_read_file("/sys/KIRRDA.TXT", buf, sizeof(buf) - 1, &len)) return;
    }
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    buf[len] = 0;
    int i = 0;
    while (buf[i]) {
        char *line = buf + i;
        while (buf[i] && buf[i] != '\n') i++;
        if (buf[i] == '\n') { buf[i] = 0; i++; }
        if (line[0] == 'T' && line[1] == '|' && tn < KTAN) {
            int k = 2, q2 = 0;
            while (line[k] && line[k] != '|' && q2 < 62) tan_q[tn][q2++] = line[k++];
            tan_q[tn][q2] = 0;
            if (line[k] == '|') k++;
            int a2 = 0;
            while (line[k] && a2 < KLINE - 1) tan_a[tn][a2++] = line[k++];
            tan_a[tn][a2] = 0;
            if (q2 && a2) tn++;
        } else if (line[0] == 'M' && line[1] == '|' && mn < KMEM) {
            int k = 2, m2 = 0;
            while (line[k] && m2 < 79) mem[mn][m2++] = line[k++];
            mem[mn][m2] = 0;
            mn++;
        }
    }
}

/* ---------- beépített tudás: többsoros válaszok ---------- */
static void kb_answers(const char *s) {
    char b[KLINE];
    if (has(s, "szia") || has(s, "hello") || has(s, "hey") || has(s, "szervusz")) {
        sayx("Szia! KIRRDA 0.2 vagyok, a Keringo helyi agya. Kerdess barmit, vagy irj 'segits'!", 1);
        return;
    }
    if (has(s, "segits") || has(s, "help") || has(s, "mit tudsz") || has(s, "parancs")) {
        sayx("Ami tudok:", 1);
        sayx("- rendszer: verzio, memoria, felbontas, allapot", 1);
        sayx("- matek: pl. 12*34+7  vagy  (2+3)*4", 1);
        sayx("- inditas: 'inditsd a festot', 'nyisd a commandert'", 1);
        sayx("- lemez-kereses: 'keres: .BAS'", 1);
        sayx("- internet: 'internet: http://cim'  es 'dns: nev'", 1);
        sayx("- tanitas: 'tanits: kerdes = valasz' (+ jegyezd/emlekszel)", 1);
        return;
    }
    if (has(s, "verzio") || has(s, "version")) {
        sayx("Keringo OS 0.5.1 — Muhely kiadas. En a KIRRDA 0.2 agy vagyok benne.", 1);
        return;
    }
    if (has(s, "ido") || has(s, "time")) {
        ksnprintf(b, sizeof(b), "Boot ota eltelt: %u masodperc. RTC-oram nincs — oszinten szolok.", tick_count() / 100);
        sayx(b, 1);
        return;
    }
    if (has(s, "memoria") || has(s, "memory")) {
        ksnprintf(b, sizeof(b), "Szabad memoria: %u oldal = %u KB.", mem_free_pages(), mem_free_pages() * 4);
        sayx(b, 1);
        return;
    }
    if (has(s, "felbontas") || has(s, "kepernyo") || has(s, "resolution")) {
        ksnprintf(b, sizeof(b), "Video mod: %ux%u, %u bites szin.", gfx_width(), gfx_height(), gfx_bpp());
        sayx(b, 1);
        sayx("Nagyobb mod a GRUB menuben valaszthato bootnal (Le/Fel).", 1);
        return;
    }
    if (has(s, "appok") || has(s, "alkalmazas") || has(s, "programs")) {
        ksnprintf(b, sizeof(b), "%d alkalmazas van. F5 = Inditopanel, keressel!", APP_COUNT - 1);
        sayx(b, 1);
        return;
    }
    if (has(s, "html5") || has(s, "bongeszo") || has(s, "browser") || has(s, "weblap")
        || has(s, "weboldal") || has(s, "javascript") || has(s, "css")) {
        sayx("A Keringo bongeszoje a HTML kis reszhalmazat erti:", 1);
        sayx("cimkek, bekezdesek, linkek, listak, kepek (JPEG), MJPEG elovideo.", 1);
        sayx("JavaScript, CSS es HTML5 nincs — az teljes bongeszo-motort igenyel.", 1);
        sayx("HTTPS-t sem tud (nincs TLS). Egyszeru http:// oldalak viszont mennek.", 1);
        return;
    }
    if (has(s, "internet") || has(s, "halozat")) {
        if (net_up()) {
            char ipb[16];
            net_ip_str(net_self_ip(), ipb, sizeof(ipb));
            ksnprintf(b, sizeof(b), "Halozat FENT (IP %s). HTTP-oldalakat letudok tolteni.", ipb);
            sayx(b, 1);
        } else {
            sayx("Halozat LE. (QEMU-ban automatikus; valodi gepen e1000/rtl8139 kartya kell.)", 3);
        }
        sayx("Igy keress: 'internet: http://10.0.2.2:8000/'  vagy  'dns: pelda.hu'.", 1);
        return;
    }
    if (has(s, "quartz") || has(s, "kimi")) {
        sayx("QUARTZ-4B: a KIMI-MATE 4 bites gep portja, BASIC-szel es jatekokkal", 1);
        sayx("(PONG, METEOR, KIGYO). A 'KIMI' nev a te feltoltott HTML-edbol jon —", 1);
        sayx("nem Kimi AI, csak egy 4 bites gep :)", 1);
        return;
    }
    if (has(s, "zx81")) {
        sayx("ZX81 Retro: az eredeti ROM-ot varja: adatok/ZX81.ROM az ISO-n.", 1);
        sayx("ROM nelkul demomodot mutat. A te ROM-fajlod es a 2 programod", 1);
        sayx("meg nem ert be hozzam — ha kell, told fel a repoba!", 1);
        return;
    }
    if (has(s, "kirrda") || has(s, "ki vagy") || has(s, "mi vagy") || has(s, "who")) {
        sayx("KIRRDA AI 0.2: szabaly-alapu helyi agy (kulcsszavak + tanithato tudas).", 1);
        sayx("Nagy nyelvi modell nem vagyok — de appokat indit, szamol, keres,", 1);
        sayx("es valodi TCP/DNS/HTTP-n internetezik. Egy karbantartoi PR-bol jottem.", 1);
        return;
    }
    if (has(s, "ki keszitett") || has(s, "ki irt") || has(s, "szerzo")) {
        sayx("A Keringo-t a kirrdauniverse-collab keszitette, GLM AI tarsfejlesztovel.", 1);
        return;
    }
    if (has(s, "vicc") || has(s, "joke")) {
        static int vi = 0;
        const char *v[4] = {
            "Miert keringenek a Keringo ablakai? Mert a gravitacio nem engedi oket nyugodni.",
            "Ket programozo: 'Nalam mar a WiFi is kering.' 'En a vegen a laptopot latom.'",
            "Hany bites a KIMI-MATE? 4. Tobb igazan nem is kell.",
            "A nullpointer olyan, mint a fekete lyuk: aki beleesik, mindenhova eljut."
        };
        sayx(v[vi & 3], 1);
        vi++;
        return;
    }
    if (has(s, "pokoljaro") || has(s, "doom")) {
        sayx("Pokoljaro: Doom-stilusu lovoldezo — a szornyek egy egyszeru uldozo MI-vel", 1);
        sayx("(utszamolas + egzasdoszamat). A changelog 'uldozo MI'-ja ez, nem en :)", 1);
        return;
    }
    if (has(s, "asztro") || has(s, "csillag") || has(s, "observatory")) {
        sayx("Obszervatorium: csillagtérkép forgatassal es nagyitassal.", 1);
        return;
    }
    if (has(s, "koszonom") || has(s, "koszi") || has(s, "thanks")) {
        sayx("Nincs mit! Orulok, ha segithetek.", 1);
        return;
    }
    if (has(s, "wifi") || has(s, "wlan")) {
        sayx("Wi-Fi panel (F6): halozatkereses es WPA2 — ath9k illeszto, QEMU-ban demoradio.", 1);
        return;
    }
    if (has(s, "tavvezerlo") || has(s, "teamviewer") || has(s, "remote")) {
        sayx("Tavvezerlo (KRVP1, TCP 7331): kepernyo-megoszta's + tavoli bemenet + fajlatalas.", 1);
        return;
    }
    if (has(s, "telepito") || has(s, "telepites")) {
        sayx("Telepito: FAT32 particio keszites + masolas — mas oprendszerek melle.", 1);
        return;
    }
    if (has(s, "particio")) {
        sayx("Particiokezeleze: MBR tablazat (Tab=lemez, N=uj, D=torles, A=aktiv, F=format).", 1);
        return;
    }
    if (has(s, "commander") || has(s, "kpa") || has(s, "tomorito")) {
        sayx("Commander: duplapaneles fajlkezeleze, F3 hex/dec/bin/karakter nezo, F7 .KPA tomorito.", 1);
        return;
    }
    if (has(s, "festo") || has(s, "paint")) {
        sayx("Festo: 24 szin, 8 eszkoz, ecsetmeret +/-. Vonal, teglalap, kor, kitoltes, pipetta.", 1);
        return;
    }
    if (has(s, "basic")) {
        sayx("Keringo BASIC: sajat ertelmezo — PRINT @, PLOT, FOR, GOSUB, WHILE...", 1);
        return;
    }
    if (has(s, "indito") || has(s, "launcher")) {
        sayx("F5 = Inditopanel: kategoriak, azonnali kereses, egerrel is.", 1);
        return;
    }
    if (has(s, "szep mod") || has(s, "elsimitas") || has(s, "antialias") || has(s, "gradiens")) {
        sayx("Szep mod (Beallitas panel, F8): elsimitott vonalak, gradiensek, lakeresites, arnyek.", 1);
        return;
    }
    if (has(s, "8 bit") || has(s, "16 bit") || has(s, "szinmelyseg")) {
        sayx("Boot-modok a GRUB-ban: 640x480x16 (alap), 640x480x8 (max. kompatibilis), x32-esek es nagyobbak.", 1);
        return;
    }
    if (has(s, "grub") || has(s, "boot")) {
        sayx("GRUB menupontok: 640x480x16 alap, 8 bites, 32 bites, 1024/1280-as modok, auto.", 1);
        sayx("5 masodpercig valaszthatsz a bootnal.", 1);
        return;
    }
    if (has(s, "kepernyovedo")) {
        sayx("7 kepernyovedo: plazma, matrix, keringes, pattogo, meteorok, tabortuz, csillagmezo.", 1);
        return;
    }
    if (has(s, "allapot") || has(s, "status")) {
        ksnprintf(b, sizeof(b), "verzio=0.5.1 app=%d tanult=%d jegyzet=%d tick=%u",
                  APP_COUNT - 1, tn, mn, tick_count());
        sayx(b, 2);
        ksnprintf(b, sizeof(b), "halozat=%d video=%ux%ux%u szabadmem=%uKB",
                  net_up(), gfx_width(), gfx_height(), gfx_bpp(), mem_free_pages() * 4);
        sayx(b, 2);
        return;
    }
    if (has(s, "asztro")) { sayx("A csillagok ma is ott vannak. Az univerzum nem frissitheto USB-rol.", 1); return; }
}

/* ---------- agy: parancsok feldolgozása ---------- */
static void brain(const char *in) {
    char b[KLINE];

    /* 1) tanítás */
    if (has(in, "tanits") || has(in, "tanitsd") || has(in, "train")) {
        const char *eq = in;
        while (*eq && *eq != '=') eq++;
        if (!*eq) { sayx("Igy tanits: 'tanits: kerdes = valasz' (az = jel kell!).", 3); return; }
        int q0 = 0;
        while (in[q0] && in[q0] != ':') q0++;
        if (in[q0] == ':') q0++;
        while (in[q0] == ' ') q0++;
        static char tq[64];
        int tql = 0;
        while (in[q0] && in[q0] != '=' && tql < 62) tq[tql++] = in[q0++];
        while (tql > 0 && tq[tql-1] == ' ') tql--;
        tq[tql] = 0;
        int a0 = (*eq == '=') ? (int)(eq - in) + 1 : (int)(eq - in);
        while (in[a0] == ' ') a0++;
        if (!tq[0] || !in[a0]) { sayx("Hianyzik a kerdes vagy a valasz: 'tanits: kerdes = valasz'.", 3); return; }
        if (tn >= KTAN) { sayx("Betelt a tanulomanyom (48 tetel) — elobb elfelejts valamit!", 3); return; }
        int al = 0;
        while (in[a0] && al < KLINE - 1) tan_a[tn][al++] = in[a0++];
        while (al > 0 && tan_a[tn][al-1] == ' ') al--;
        tan_a[tn][al] = 0;
        for (int k = 0; k <= tql; k++) tan_q[tn][k] = tq[k];
        tn++;
        save_brain();
        ksnprintf(b, sizeof(b), "Megtanultam! Most %d tetel van a fejemben. Kerdezd ugyanigy!", tn);
        sayx(b, 1);
        return;
    }

    /* 2) elfelejtés */
    if (has(in, "elfelejt")) {
        int w = 0;
        while (in[w] && in[w] != ' ') w++;
        while (in[w] == ' ') w++;
        if (!in[w]) { sayx("Mit felejtsek el? 'elfelejt: szo'.", 3); return; }
        int removed = 0, nn = 0;
        for (int i = 0; i < tn; i++) {
            if (has(tan_q[i], in + w) || has(tan_a[i], in + w)) { removed++; continue; }
            if (nn != i) {
                for (int k = 0; k < 64; k++)     { tan_q[nn][k] = tan_q[i][k]; if (!tan_q[i][k]) break; }
                for (int k = 0; k < KLINE; k++)   { tan_a[nn][k] = tan_a[i][k]; if (!tan_a[i][k]) break; }
            }
            nn++;
        }
        tn = nn;
        int mm = 0;
        for (int i = 0; i < mn; i++) {
            if (has(mem[i], in + w)) continue;
            if (mm != i) {
                for (int k = 0; k < 80; k++) { mem[mm][k] = mem[i][k]; if (!mem[i][k]) break; }
            }
            mm++;
        }
        mn = mm;
        save_brain();
        ksnprintf(b, sizeof(b), "Elfelejtettem, ami a '%s' szora illott (%d tanult tetel ment).", in + w, removed);
        sayx(b, 1);
        return;
    }

    /* 3) jegyezd meg */
    if (has(in, "jegyezd") || has(in, "remember")) {
        int w = 0;
        while (in[w] && in[w] != ':') w++;
        if (in[w] == ':') w++;
        while (in[w] == ' ') w++;
        if (!in[w]) { sayx("Mit jegyezzek meg? 'jegyezd: szoveg'.", 3); return; }
        if (mn >= KMEM) { sayx("A jegyzetmem 8 soros — elobb törölni kell valamit.", 3); return; }
        int m2 = 0;
        while (in[w] && m2 < 79) mem[mn][m2++] = in[w++];
        mem[mn][m2] = 0;
        mn++;
        save_brain();
        sayx("Elraktam a jegyzetembe. 'emlekszel' — visszaadom.", 1);
        return;
    }

    /* 4) emlékszel */
    if (has(in, "emlekszel") || has(in, "emlekeztess") || has(in, "recall")) {
        if (!mn) { sayx("Ures a jegyzetmem — 'jegyezd: szoveg' -tolthatod.", 1); return; }
        for (int i = 0; i < mn; i++) sayx(mem[i], 2);
        return;
    }

    /* 5) DNS-névfeloldás */
    if (has(in, "dns") && has(in, ":")) {
        int w = 0;
        while (in[w] && in[w] != ':') w++;
        if (in[w] == ':') w++;
        while (in[w] == ' ') w++;
        if (!in[w]) { sayx("Igy: 'dns: pelda.hu'.", 3); return; }
        if (!net_up()) { sayx("Nincs halozat — QEMU-ban automatikus, valodi gepen e1000/rtl8139 kell.", 3); return; }
        u32 ip;
        if (net_dns_resolve(in + w, &ip)) {
            char ipb[16];
            net_ip_str(ip, ipb, sizeof(ipb));
            ksnprintf(b, sizeof(b), "%s = %s", in + w, ipb);
            sayx(b, 1);
        } else {
            ksnprintf(b, sizeof(b), "A %s nevet nem sikerult feloldani.", in + w);
            sayx(b, 3);
        }
        return;
    }

    /* 6) internet: oldal letöltése */
    if (has(in, "internet")) {
        const char *u = in;
        while (*u && !(*u == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p')) u++;
        if (!*u) {
            sayx("Igy: 'internet: http://cim/'. A HTTPS-t nem tamogatom (nincs TLS).", 3);
            return;
        }
        if (u[4] == 's') { sayx("HTTPS-et nem tudok letolteni (nincs TLS-titkositasom). Sima http://-t igen.", 3); return; }
        if (!net_up()) { sayx("Nincs halozat. (QEMU: automatikus; valodi gepen e1000/rtl8139.)", 3); return; }
        char url[120];
        int ul = 0;
        while (u[ul] && u[ul] != ' ' && ul < 118) url[ul] = u[ul], ul++;
        url[ul] = 0;
        http_result_t res;
        int ok = http_fetch(url, &res);
        if (ok != 1 || !res.status) { sayx("Nem sikerult a letoltes (cim? halozat? szerver?).", 3); return; }
        ksnprintf(b, sizeof(b), "HTTP %d | %u bajt | %s", res.status, res.body_len, res.content_type);
        sayx(b, 1);
        if (res.body_len) web_text(res.body, res.body_len);
        http_free_result(&res);
        return;
    }

    /* 7) fájlkeresés */
    if (has(in, "keres")) {
        int w = 0;
        while (in[w] && in[w] != ':') w++;
        if (in[w] == ':') w++;
        while (in[w] == ' ') w++;
        if (!in[w]) { sayx("Mit keressek? 'keres: .BAS' vagy 'keres: rom'.", 3); return; }
        sayx("Keresem a mem, sys, usb es cd lemezeken...", 1);
        cmd_search_files(in + w);
        return;
    }

    /* 8) matek */
    if (is_mathy(in)) {
        ev_t e; e.p = in; e.err = 0;
        long v = ev_expr(&e);
        if (e.err == 2) { sayx("Nullaval osztani? Az meg nem fog nekem sikerulni :)", 3); return; }
        if (e.err) { sayx("Ezt a kifejezest nem ertem — zarojel hiba?", 3); return; }
        ksnprintf(b, sizeof(b), "= %d", (int)v);
        sayx(b, 1);
        return;
    }

    /* 9) alkalmazás indítása */
    if (has(in, "indit") || has(in, "nyisd") || has(in, "nyit") || has(in, "start")
        || has(in, "open") || has(in, "launch") || has(in, "megnyi")) {
        int ap = find_app(in);
        if (ap >= 0) {
            ksnprintf(b, sizeof(b), "Rendben! Megnyitom: %s (Esc-sel visszajossz hozzam.)", app_list[ap].nev[ui_lang()]);
            sayx(b, 1);
            app_launch(ap);
            return;
        }
        if (has(in, "bongeszo") || has(in, "beallitas") || has(in, "lemez") || has(in, "jegyzet")) {
            sayx("Az galaxis-panel, nem app — Tab-bal odabb, Enter-rel bele.", 1);
            return;
        }
        sayx("Ilyen appot nem talaltam — az Inditopanelen (F5) megnezheto minden.", 3);
        return;
    }

    /* 10) tanult tudás */
    int li = match_learned(in);
    if (li >= 0) { sayx(tan_a[li], 1); return; }

    /* 11) beépített tudás */
    int before = hn;
    kb_answers(in);

    /* 12) ha eddig semmit sem szóltunk → fallback */
    if (hn == before) {
        static int fbi = 0;
        static const char *FB[3] = {
            "Ezt meg nem tanultad meg nekem. 'tanits: kerdes = valasz' — es tudni fogom!",
            "Errol meg nincs szabalyom. Tanits meg, vagy kerdezz maskepp!",
            "Ezt nem ertem — de megtanithatod: 'tanits: ... = ...'"
        };
        int L = 0;
        while (in[L]) L++;
        if (L && in[L-1] == '?')
            sayx("Jo kerdes! Ezt meg nem tudom — de megtanithatsz: 'tanits: kerdes = valasz'.", 1);
        else
            sayx(FB[fbi++ % 3], 1);
    }
}

/* ---------- UI ---------- */
void kirrda_enter(void) {
    qn = 0;
    hview = 0;
    if (!loaded) { load_brain(); loaded = 1; }
    if (!greeted) {
        greeted = 1;
        sayx("KIRRDA AI 0.2 — Keringo OS 0.5.1 helyi agya.", 1);
        sayx("Szabaly-alapu vagyok (nem nagy nyelvi modell), de kepes vagyok:", 2);
        sayx("szamolasra, app-inditasra, lemez-keresesre, tanitasra es internetezesre.", 2);
        sayx("Ird: 'segits' a parancslistahoz. Tipp: 'inditsd a festot'!", 2);
    }
}
void kirrda_exit(void) { }

void kirrda_key(u8 sc, u8 rel, u8 a) {
    if (rel) return;
    if (sc == KEY_ENTER) {
        q[qn] = 0;
        if (qn) {
            kprintf("kirrda: kerdes: %s\n", q);
            int hn0 = hn;
            sayx(q, 0);
            brain(q);
            for (int i = hn0; i < hn && i < hn0 + 6; i++)
                kprintf("kirrda: valasz: %s\n", hist[i]);
        }
        qn = 0;
        return;
    }
    if (sc == 0x0E && qn) { qn--; q[qn] = 0; return; }
    if ((sc == KEY_UP || sc == KEY_DOWN) && hn) {
        int vis = ((int)gfx_height() - 46 - 60) / 14;
        if (vis < 4) vis = 4;
        if (sc == KEY_UP) { if (hview < hn - vis) hview++; }
        else { if (hview > 0) hview--; }
        return;
    }
    if (a >= 32 && a < 127 && qn < KLINE - 2) q[qn++] = (char)a;
}

void kirrda_frame(u32 t) {
    (void)t;
    int W = (int)gfx_width(), H = (int)gfx_height();
    char b[96];
    ksnprintf(b, sizeof(b), ui_lang() ? "brain=%d notes=%d net=%d" : "agyam=%d jegyzet=%d halozat=%d",
              tn, mn, net_up());
    app_chrome("KIRRDA AI 0.2", b, settings_accent_color());
    gfx_fill_rect(0, 24, W, H - 48, 0xFF10151A);
    gfx_draw_text(8, 30, ui_lang()
        ? "local rule-based brain | teachable via KIRRDA.TXT"
        : "helyi szabaly-agy | tanitas: KIRRDA.TXT (mem+sys)", 0xFF8FA0AC, 0xFF000000u);

    /* csevegő-terület */
    int top = 46, bottom = H - 44;
    int vis = (bottom - top) / 14;
    if (vis < 4) vis = 4;
    if (hview > hn - vis) hview = hn - vis;
    if (hview < 0) hview = 0;
    int start = hn - vis - hview;
    if (start < 0) start = 0;
    for (int i = start; i < hn && i < start + vis; i++) {
        u32 col = 0xFFF0F0F0;                       /* te */
        if (hstyle[i] == 1) col = 0xFF50E878;       /* KIRRDA */
        else if (hstyle[i] == 2) col = 0xFFE8B24A;  /* info */
        else if (hstyle[i] == 3) col = 0xFFE86A6A; /* hiba */
        gfx_draw_text(12, top + (i - start) * 14, hist[i], col, 0xFF10151A);
    }

    /* beviteli sor */
    if (settings.szep_mod) gfx_rounded_fill(10, H - 40, W - 20, 26, 6, 0xFF18222B);
    gfx_rect(10, H - 40, W - 20, 26, 0xFF40505A);
    ksnprintf(b, sizeof(b), "> %s", q);
    gfx_draw_text(18, H - 33, b, 0xFFF0F0F0, 0xFF10151A);
    if ((t / 20) & 1) gfx_fill_rect(18 + gfx_text_width(b), H - 33, 8, 8, 0xFF50E878);

    gfx_draw_text(8, H - 12, ui_lang()
        ? "Enter: ask  Up/Down: scroll  Esc: back"
        : "Enter: kerdes  Fel/Le: gorgetés  Esc: vissza", 0xFF8FA0AC, 0xFF000000u);
}

/* sayx: stílussal beszúrás */
static void sayx(const char *s, u8 st) {
    if (hn >= KHIST) {
        for (int i = 1; i < KHIST; i++) {
            int j = 0; while (hist[i][j]) { hist[i-1][j] = hist[i][j]; j++; }
            hist[i-1][j] = 0;
            hstyle[i-1] = hstyle[i];
        }
        hn = KHIST - 1;
    }
    int i = 0;
    while (s[i] && i < KLINE - 1) { hist[hn][i] = s[i]; i++; }
    hist[hn][i] = 0;
    hstyle[hn] = st;
    hn++;
    hview = 0;
}
