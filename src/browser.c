/* browser.c — Keringő Böngésző: HTML-részhalmaz elemzés + renderelés,
 * kép<em>letöltés, MJPEG élő videó. Állapotgép: üresjár -> letölt -> megjelenítés. */
#include "kernel.h"
#include "net.h"
#include "icondata.h"

#define BR_MAXLINES 96
#define LINE_CHARS 56
#define BR_MAXIMG 2
#define BR_MAXLINKS 10
static char img_srcs[BR_MAXIMG][120];
static int n_img_srcs;

static char br_url[120];
static char br_title[48];
static int br_mode;                     /* 0 üres, 1 html, 2 stream */
static char br_msg[96];
static int br_loading;

static char lines[BR_MAXLINES][LINE_CHARS + 16];
static u8  lstyle[BR_MAXLINES];         /* 1=cím, 2=link, 4=elválasztó, 8=zárójel/halvány */
static int nlines;
static int scroll;

static char links[BR_MAXLINKS][120];
static int nlinks;

static u32 *img_pix[BR_MAXIMG];
static int img_w[BR_MAXIMG], img_h[BR_MAXIMG];
static int nimgs;

static tcpconn_t *stream;
static u32 stream_frames;
static u32 stream_fps_clock;
static u32 stream_fps;
static u32 last_frame_ms;

/* publikus lekérdezések */
const char *browser_url(void) { return br_url; }
const char *browser_title(void) { return br_title; }
const char *browser_msg(void) { return br_msg; }
int browser_is_streaming(void) { return br_mode == 2 && stream; }
u32 browser_stream_frames(void) { return stream_frames; }
u32 browser_stream_fps(void) { return stream_fps; }

static int hstrlen_(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void set_msg(const char *m) {
    int i;
    for (i = 0; i < 95 && m[i]; i++) br_msg[i] = m[i];
    br_msg[i] = 0;
}

static void add_line(const char *txt, u8 style) {
    if (nlines >= BR_MAXLINES) return;
    int i;
    for (i = 0; i < LINE_CHARS + 15 && txt[i]; i++) lines[nlines][i] = txt[i];
    lines[nlines][i] = 0;
    lstyle[nlines] = style;
    nlines++;
}

static void clear_page(void) {
    nlines = 0;
    nlinks = 0;
    nimgs = 0;
    scroll = 0;
    stream = 0;
    br_mode = 0;
}

/* ---------- beépített kezdőoldal ---------- */
static void builtin_start(void) {
    clear_page();
    br_mode = 1;
    br_url[0] = 'k'; br_url[1] = 'e'; br_url[2] = 'r'; br_url[3] = 'i';
    br_url[4] = 'n'; br_url[5] = 'g'; br_url[6] = 'o'; br_url[7] = ':'; br_url[8] = '/';
    br_url[9] = '/';
    br_url[10] = 's'; br_url[11] = 't'; br_url[12] = 'a'; br_url[13] = 'r'; br_url[14] = 't';
    br_url[15] = 0;
    const char *t = ui_lang() ? "ORBIT BROWSER" : "KERINGŐ BÖNGÉSZŐ";
    int i;
    for (i = 0; i < 47 && t[i]; i++) br_title[i] = t[i];
    br_title[i] = 0;
    if (ui_lang()) {
        add_line("Welcome on the Keringo Browser start page!", 1);
        add_line("", 0);
        add_line("This browser speaks a small subset of HTML:", 0);
        add_line("headings, paragraphs, links, lists, images.", 0);
        add_line("Live video works as MJPEG over plain HTTP.", 0);
        add_line("", 0);
        add_line("Links:", 1);
        ksnprintf(links[0], 120, "http://10.0.2.2:8000/");
        ksnprintf(links[1], 120, "http://10.0.2.2:8000/video.mjx");
        add_line("[1]  Keringo demo page (needs the demo server)", 2);
        add_line("[2]  Live video stream (MJPEG)", 2);
        nlinks = 2;
        add_line("", 0);
        add_line("Press F6 to type a URL, then Enter.", 8);
        add_line("Note: HTTPS (TLS) is not supported yet.", 8);
    } else {
        add_line("Üdvözlünk a Keringő Böngésző kezdőoldalán!", 1);
        add_line("", 0);
        add_line("Ez a böngésző a HTML egy kis részhalmazát érti:", 0);
        add_line("címeket, bekezdéseket, linkeket, listákat, képeket.", 0);
        add_line("Az élő videó MJPEG formátumban, egyszerű HTTP-n működik.", 0);
        add_line("", 0);
        add_line("Linkek:", 1);
        ksnprintf(links[0], 120, "http://10.0.2.2:8000/");
        ksnprintf(links[1], 120, "http://10.0.2.2:8000/video.mjx");
        add_line("[1]  Keringő demó oldal (demó-szerver kell hozzá)", 2);
        add_line("[2]  Élő videó adás (MJPEG)", 2);
        nlinks = 2;
        add_line("", 0);
        add_line("F6: URL beírása, utána Enter.", 8);
        add_line("Megjegyzés: HTTPS (TLS) még nem támogatott.", 8);
    }
    set_msg(ui_lang() ? "start page" : "kezdőoldal");
}

void browser_init(void) {
    br_url[0] = 0;
    br_title[0] = 0;
    br_msg[0] = 0;
    br_loading = 0;
    clear_page();
    builtin_start();
}

/* ---------- HTML elemzés ---------- */
static int h_startswith(const char *p, const char *w) {
    int i = 0;
    while (w[i]) {
        char a = p[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (a != w[i]) return 0;
        i++;
    }
    return 1;
}

/* entitás cseréje: visszaadja a kimenetre írt hosszt */
static int decode_entity(const char *p, char *out) {
    if (p[0] == 'a' && p[1] == 'm' && p[2] == 'p' && p[3] == ';') { out[0] = '&'; return 1; }
    if (p[0] == 'l' && p[1] == 't' && p[2] == ';') { out[0] = '<'; return 1; }
    if (p[0] == 'g' && p[1] == 't' && p[2] == ';') { out[0] = '>'; return 1; }
    if (p[0] == 'q' && p[1] == 'u' && p[2] == 'o' && p[3] == 't' && p[4] == ';') { out[0] = '"'; return 1; }
    if (p[0] == 'n' && p[1] == 'b' && p[2] == 's' && p[3] == 'p' && p[4] == ';') { out[0] = ' '; return 1; }
    return 0;
}

/* látható szöveg tördelése sorokká */
static int cur_used = 0;                 /* az aktuális sor feltöltöttsége */
static void flush_word(char *word, int wlen, u8 style) {
    if (wlen == 0) {
        if (cur_used > 0 && cur_used < LINE_CHARS) { lines[nlines-1][cur_used] = ' '; cur_used++; lines[nlines-1][cur_used] = 0; }
        return;
    }
    for (int i = 0; i < wlen; i++) {
        if (cur_used >= LINE_CHARS) { cur_used = 0; }
        if (cur_used == 0) {
            if (nlines >= BR_MAXLINES) return;
            lines[nlines][0] = 0;
            lstyle[nlines] = style;
            nlines++;
        }
        int L = cur_used;
        lines[nlines-1][L] = word[i];
        lines[nlines-1][L+1] = 0;
        cur_used = L + 1;
    }
}

static void parse_html(const char *html, u32 len) {
    clear_page();
    br_mode = 1;
    u32 i = 0;
    char word[LINE_CHARS + 8];
    int wlen = 0;
    u8 style = 0;
    int in_title = 0;
    int skip = 0;                          /* script/style */
    int title_len = 0;
    int para_break = 1;
    n_img_srcs = 0;
    while (i < len) {
        char c = html[i];
        if (c == '<') {
            /* szó kiírása */
            if (wlen) { flush_word(word, wlen, style); wlen = 0; }
            const char *p = html + i + 1;
            if (h_startswith(p, "title")) { in_title = 1; i += 6; continue; }
            if (h_startswith(p, "/title")) { in_title = 0; i += 7; continue; }
            if (h_startswith(p, "script") || h_startswith(p, "style")) { skip = 1; }
            if (h_startswith(p, "/script") || h_startswith(p, "/style")) { skip = 0; }
            if (h_startswith(p, "br") || h_startswith(p, "/p") || h_startswith(p, "/h1")
                || h_startswith(p, "/h2") || h_startswith(p, "/h3") || h_startswith(p, "/li")
                || h_startswith(p, "/div")) {
                if (nlines < BR_MAXLINES) { lines[nlines][0] = 0; lstyle[nlines] = 0; nlines++; }
                cur_used = 0;
                para_break = 1;
            }
            if (h_startswith(p, "h1") || h_startswith(p, "h2") || h_startswith(p, "h3")) style = 1;
            if (h_startswith(p, "/h1") || h_startswith(p, "/h2") || h_startswith(p, "/h3")) style = 0;
            if (h_startswith(p, "hr")) {
                if (nlines < BR_MAXLINES) { lstyle[nlines] = 4; lines[nlines][0] = 0; nlines++; }
                cur_used = 0;
            }
            if (h_startswith(p, "a ")) {
                /* href keresése */
                const char *q = p + 2;
                while (*q && *q != '>') {
                    if (q[0] == 'h' && q[1] == 'r' && q[2] == 'e' && q[3] == 'f' && q[4] == '=') {
                        q += 5;
                        char qc = (*q == '"' || *q == '\'') ? *q : ' ';
                        if (qc != ' ') q++;
                        int idx = nlinks;
                        if (idx < BR_MAXLINKS) {
                            int k = 0;
                            while (*q && *q != qc && *q != '>' && *q != ' ' && k < 118) links[idx][k++] = *q++;
                            links[idx][k] = 0;
                            nlinks++;
                            char pre[8];
                            ksnprintf(pre, sizeof(pre), "[%d]", idx + 1);
                            flush_word(pre, 3, 2);
                            style = 2;
                        }
                        break;
                    }
                    q++;
                }
            }
            if (h_startswith(p, "/a")) style = 0;
            if (h_startswith(p, "img")) {
                /* src="..." — képútvonal gyűjtése: ideiglenes tároló */
                const char *q = p + 4;
                while (*q && *q != '>') {
                    if (q[0] == 's' && q[1] == 'r' && q[2] == 'c' && q[3] == '=') {
                        q += 4;
                        char qc = (*q == '"' || *q == '\'') ? *q : ' ';
                        if (qc != ' ') q++;
                        static char imgsrc[120];
                        int k = 0;
                        while (*q && *q != qc && *q != '>' && *q != ' ' && k < 118) imgsrc[k++] = *q++;
                        imgsrc[k] = 0;
                        if (n_img_srcs < BR_MAXIMG) {
                            int k2;
                            for (k2 = 0; k2 < 119 && imgsrc[k2]; k2++) img_srcs[n_img_srcs][k2] = imgsrc[k2];
                            img_srcs[n_img_srcs][k2] = 0;
                            n_img_srcs++;
                            char cap[64];
                            ksnprintf(cap, sizeof(cap), "[kép] %s", imgsrc);
                            add_line(cap, 8);
                        }
                        break;
                    }
                    q++;
                }
            }
            /* a tag végéig ugrás */
            while (i < len && html[i] != '>') i++;
            i++;
            para_break = 0;
            continue;
        }
        if (skip) { i++; continue; }
        if (in_title) {
            if (c != '>' && title_len < 47) br_title[title_len++] = c;
            i++;
            continue;
        }
        if (c == '&') {
            char out[2] = { 0, 0 };
            int n = decode_entity(html + i + 1, out);
            if (n) {
                if (wlen < LINE_CHARS + 7) word[wlen++] = out[0];
                i += (out[0] == '&') ? 5 : (out[0] == '"' || out[0] == ' ') ? 6 : 4;
                continue;
            }
            if (wlen < LINE_CHARS + 7) word[wlen++] = '&';
            i++;
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            if (wlen) { flush_word(word, wlen, style); wlen = 0; }
            i++;
            continue;
        }
        if (wlen < LINE_CHARS + 7) word[wlen++] = c;
        else { flush_word(word, wlen, style); wlen = 0; }
        i++;
    }
    if (wlen) flush_word(word, wlen, style);
    if (title_len) br_title[title_len] = 0;
    else {
        const char *def = "Keringo lap";
        for (int k = 0; k < 47 && def[k]; k++) br_title[k] = def[k];
        br_title[12] = 0;
    }
}

/* ---------- oldal megnyitása ---------- */
static int fetch_image(const char *url, int slot);

int browser_open(const char *url) {
    br_loading = 1;
    stream = 0;
    br_mode = 0;
    int i;
    for (i = 0; i < 119 && url[i]; i++) br_url[i] = url[i];
    br_url[i] = 0;
    set_msg(ui_lang() ? "loading..." : "betoltes...");

    if (url[0] == 'k' && url[1] == 'e' && url[2] == 'r') {
        builtin_start();
        br_loading = 0;
        return 1;
    }
    if (!net_up()) {
        set_msg(ui_lang() ? "no network" : "nincs halozat");
        br_loading = 0;
        clear_page();
        br_mode = 1;
        add_line(ui_lang() ? "No network connection." : "Nincs hálózati kapcsolat.", 1);
        return 0;
    }
    http_result_t res;
    int r = http_fetch(url, &res);
    if (r != 1 || !res.body) {
        set_msg(ui_lang() ? "download failed" : "letoltes nem sikerult");
        br_loading = 0;
        clear_page();
        br_mode = 1;
        add_line(ui_lang() ? "Could not load the page." : "Az oldal betöltése nem sikerült.", 1);
        add_line(ui_lang() ? "(Plain HTTP only; no HTTPS yet.)" : "(Csak egyszerű HTTP; HTTPS még nincs.)", 8);
        return 0;
    }
    /* MJPEG stream? */
    if (res.content_type[0] == 'm' && res.content_type[1] == 'u') {
        /* multipart/x-mixed-replace: élő adás indítása külön kapcsolaton */
        br_mode = 2;
        stream_frames = 0;
        stream_fps = 0;
        stream_fps_clock = tick_count();
        last_frame_ms = tick_count();
        if (http_stream_open(url, &stream)) {
            set_msg(ui_lang() ? "LIVE" : "ELO ADAS");
            br_loading = 0;
            return 1;
        }
        set_msg("stream hiba");
        br_loading = 0;
        return 0;
    }
    /* képfájl? */
    if ((res.content_type[0] == 'i' && res.content_type[1] == 'm')
        || (url[hstrlen_(url) - 4] == '.' && url[hstrlen_(url) - 3] == 'j' && url[hstrlen_(url) - 2] == 'p')) {
        u32 *pix; int w, h;
        if (jpeg_decode(res.body, res.body_len, &pix, &w, &h)) {
            img_pix[0] = img_scale(pix, w, h, 448, 240);
            img_w[0] = 448; img_h[0] = 240;
            nimgs = 1;
            nlines = 0;
            add_line(ui_lang() ? "image" : "kep", 1);
            br_mode = 1;
            set_msg(ui_lang() ? "image loaded" : "kep betoltve");
            br_loading = 0;
            return 1;
        }
    }
    /* HTML elemzés */
    parse_html((const char *)res.body, res.body_len);
    /* képek letöltése a laphoz (max 2) */
    for (int k = 0; k < n_img_srcs && k < BR_MAXIMG; k++) {
        http_result_t ires;
        if (http_fetch(img_srcs[k], &ires) == 1 && ires.body) {
            u32 *pix; int w, h;
            if (jpeg_decode(ires.body, ires.body_len, &pix, &w, &h)) {
                img_pix[k] = img_scale(pix, w, h, 224, 120);
                img_w[k] = 224; img_h[k] = 120;
                nimgs = k + 1;
            }
        }
    }
    ksnprintf(br_msg, sizeof(br_msg), "%s (%u B)", ui_lang() ? "OK" : "kesz", res.body_len);
    br_loading = 0;
    return 1;
}

/* link követése */
int browser_follow_link(int n) {
    if (n < 1 || n > nlinks) return 0;
    return browser_open(links[n - 1]);
}

/* görgetés */
void browser_scroll(int d) {
    scroll += d;
    if (scroll < 0) scroll = 0;
    int maxs = nlines - 20;
    if (maxs < 0) maxs = 0;
    if (scroll > maxs) scroll = maxs;
}

/* ---------- stream kérdezés (UI ciklusból) ---------- */
void browser_stream_poll(void) {
    if (br_mode != 2 || !stream) return;
    u8 *frame; u32 flen;
    int r = http_stream_poll(stream, &frame, &flen);
    if (r == 1) {
        u32 *pix; int w, h;
        if (jpeg_decode(frame, flen, &pix, &w, &h)) {
            img_pix[0] = pix;
            img_w[0] = w; img_h[0] = h;
            nimgs = 1;
            stream_frames++;
        }
        last_frame_ms = tick_count();
    }
    if (r < 0 || stream->state == 0) {
        set_msg(ui_lang() ? "stream ended" : "adas vege");
        br_mode = 1;
        add_line(ui_lang() ? "Stream ended." : "Az adás véget ért.", 8);
        stream = 0;
        return;
    }
    /* fps számlálás 2 másodpercenként */
    u32 now = tick_count();
    if (now - stream_fps_clock >= 200) {
        stream_fps = (stream_frames * 100) / (now - stream_fps_clock);
        stream_fps_clock = now;
        if (stream_frames > 1000000) stream_frames = 0;   /* túlcsordulás ellen */
    }
}

/* ---------- panelra rajzolás ---------- */
extern u32 utf8_decode_at(const char **s);
extern const u8 *glyph_lookup(u32 cp);
static void t_text(texture_t *t, int x, int y, const char *s, u32 fg);
static void t_fill(texture_t *t, int x, int y, int w, int h, u32 c);

static void br_draw_scaled(texture_t *t, u32 *pix, int w, int h, int dx, int dy, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int sy = y * h / dh;
        if (sy >= h) sy = h - 1;
        for (int x = 0; x < dw; x++) {
            int sx = x * w / dw;
            if (sx >= w) sx = w - 1;
            int tx = dx + x, ty = dy + y;
            if (tx >= 0 && ty >= 0 && tx < t->w && ty < t->h)
                t->px[ty * t->w + tx] = pix[sy * w + sx];
        }
    }
}

void browser_render(texture_t *t, u32 tick, int focused) {
    (void)tick; (void)focused;
    u32 accent = settings_accent_color();
    t_fill(t, 0, 0, t->w, t->h, 0xE6161B24);
    /* URL sáv */
    t_fill(t, 0, 0, t->w, 26, 0xFF24313C);
    char ub[64];
    ksnprintf(ub, sizeof(ub), "%s", br_url);
    t_text(t, 8, 8, ub, br_mode == 2 ? 0xFFE86A6A : accent);
    /* címsor */
    t_text(t, 8, 32, br_title[0] ? br_title : (char *)"-", 0xFFF0F0F0);

    if (br_mode == 2) {
        /* élő videó mód */
        char st[64];
        ksnprintf(st, sizeof(st), "%s | %u fps | %u kepkocka",
                  ui_lang() ? "LIVE" : "ELO", stream_fps, stream_frames);
        t_text(t, 8, 44, st, 0xFFE86A6A);
        if (img_pix[0]) {
            br_draw_scaled(t, img_pix[0], img_w[0], img_h[0], 8, 58, t->w - 16, t->h - 78);
        }
        if ((tick / 25) % 2 == 0) t_fill(t, t->w - 34, 8, 12, 12, 0xFFE86A6A);
        return;
    }

    /* szöveges oldal */
    int yy = 58;
    for (int i = scroll; i < nlines && yy < t->h - 30; i++, yy += 12) {
        u8 st = lstyle[i];
        if (st & 4) {
            t_fill(t, 8, yy + 5, t->w - 16, 1, 0xFF3A5562);
            continue;
        }
        u32 col = (st & 2) ? accent : ((st & 8) ? 0xFF8FA0AC : 0xFFF0F0F0);
        t_text(t, 8, yy, lines[i], col);
        if (st & 1) t_fill(t, 8, yy + 9, t->w - 16, 1, 0xFF2A3A46);
    }
    /* képek a lap alján */
    int imy = t->h - 140;
    for (int k = 0; k < nimgs && k < BR_MAXIMG; k++) {
        if (!img_pix[k]) continue;
        br_draw_scaled(t, img_pix[k], img_w[k], img_h[k], 8 + k * 232, imy, 224, 120);
    }
    /* görgetés-jelző */
    if (nlines > 20) {
        int sb = (t->h - 74) * 20 / nlines;
        int sby = 58 + (t->h - 74 - sb) * scroll / (nlines - 20);
        t_fill(t, t->w - 6, 58, 3, t->h - 74, 0xFF2A3440);
        t_fill(t, t->w - 6, sby, 3, sb, accent);
    }
    /* állapotsor */
    t_text(t, 8, t->h - 18, br_msg, 0xFF8FA0AC);
    (void)hstrlen_;
}

/* t_text/t_fill: az ui.c-ből átmásolt egyszerű rajzolók (textúrára) */
static void t_fill(texture_t *t, int x, int y, int w, int h, u32 c) {
    for (int j = 0; j < h; j++) {
        int yy = y + j; if (yy < 0 || yy >= t->h) continue;
        for (int i2 = 0; i2 < w; i2++) {
            int xx = x + i2; if (xx < 0 || xx >= t->w) continue;
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
            for (int rx = 0; rx < 8; rx++)
                if (bits & (1 << rx)) {
                    int xx = cx + rx, yy = y + ry;
                    if (xx >= 0 && yy >= 0 && xx < t->w && yy < t->h) t->px[yy * t->w + xx] = fg;
                }
        }
        cx += 8;
    }
}
