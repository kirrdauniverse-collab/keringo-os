/* app_remote.c — Távvezérlő (0.5): TeamViewer-szerű távoli irányítás két
 * Keringő OS gép közt. Kiszolgáló: háttérfeladatként osztja meg a képernyőt
 * (32x32-os csempék RLE-tömörítéssel) és a távoli bemenetet a saját asztalába
 * injektálja. Irányító: fogadja a képet, a saját billentyűzet/egér eseményeit
 * küldi, és fájlokat lehet átvinni mindkét irányba (GET/PUT).
 * Protokoll: KRVP1 a 7331-es TCP-porton. Hitelesítés nincs — bizalmi hálózaton! */
#include "kernel.h"
#include "net.h"

#define KRVP_PORT 7331
#define TILE 32

/* ---------- közös pufferek ---------- */
static u8  txbuf[4096 + 128];
static u8  filebuf[512 * 1024];

/* ---------- app-állapot ---------- */
static int mode = 0;             /* 0: módválasztás, 1: IP-bevitel, 2: ügyfél aktív */
static int msel = 0;
static char ipbuf[32] = "10.0.2.2";
static int iplen = 8, ipcur = 8;
static char rstatus[96] = "";
static tcpconn_t *cli_conn = 0;
static u32 *shadow = 0;          /* ügyfél-oldali képmás */
static int shadow_w = 0, shadow_h = 0;

/* ügyfél vételi állapotgép */
enum { RX_IDLE = 0, RX_TILE, RX_FILE };
static int rx_state = RX_IDLE;
static int rx_need = 0, rx_got = 0;
static int rx_tx, rx_ty;
static u8  rxdata[4096 + 4096];
static char rxname[40];
static int rxfilesz = 0;
static int stat_frames = 0, stat_bytes = 0;

/* ---------- kiszolgáló (háttérfeladat) ---------- */
static tcpconn_t *srv_conn = 0;
static u32 *srv_shadow = 0;
static int srv_w = 0, srv_h = 0, srv_tiles_w, srv_tiles_h;
static int srv_scan = 0;
static u32 srv_next_stream = 0;
static int srv_active = 0;

static int srv_send_str(const char *s) {
    int n = 0; while (s[n]) n++;
    return tcp_send(srv_conn, (const u8 *)s, (u32)n);
}
static int srv_send_bytes(const u8 *b, u32 n) {
    /* 0.5: darabolt küldés — a tcp_send hívásonként max ~4 KB megy,
       a csempék/fájlok ennél nagyobbak lehetnek */
    while (n > 0) {
        u32 chunk = n > 3000 ? 3000 : n;
        if (!tcp_send(srv_conn, b, chunk)) return 0;
        b += chunk; n -= chunk;
    }
    return 1;
}

/* csempék elküldése: scanelőrsor az előző árnyékhoz KÉpest */
static void srv_stream_step(void) {
    if (!srv_active || !srv_conn || !tcp_connected(srv_conn)) return;
    u32 now = tick_count();
    if ((s32)(srv_next_stream - now) > 0) return;
    srv_next_stream = now + 10;                       /* 100 ms */
    u32 *bb = gfx_backbuf();
    int sent = 0;
    int ntiles = srv_tiles_w * srv_tiles_h;
    for (int k = 0; k < ntiles && sent < 4; k++) {
        int t = (srv_scan + k) % ntiles;
        int ty = t / srv_tiles_w, tx = t % srv_tiles_w;
        int x0 = tx * TILE, y0 = ty * TILE;
        int changed = 0;
        for (int y = 0; y < TILE && !changed; y++) {
            const u32 *s = bb + (u32)(y0 + y) * srv_w + x0;
            const u32 *d = srv_shadow + (u32)(y0 + y) * srv_w + x0;
            for (int x = 0; x < TILE; x++) if (s[x] != d[x]) { changed = 1; break; }
        }
        if (!changed) continue;
        /* bájtsor-RLE a csempén (4 bájt/pixel, soronként a szerver sorstridjével) */
        const u8 *src = (const u8 *)bb + ((u32)y0 * srv_w + x0) * 4;
        int o = 0;
        for (int row = 0; row < TILE; row++) {
            const u8 *r = src + (u32)row * srv_w * 4;
            int i = 0;
            while (i < TILE * 4) {
                u8 v = r[i];
                int run = 1;
                while (i + run < TILE * 4 && r[i + run] == v && run < 255) run++;
                if (run >= 3) { rxdata[o++] = (u8)run; rxdata[o++] = v; }
                else for (int q = 0; q < run; q++) { rxdata[o++] = 1; rxdata[o++] = v; }
                i += run;
            }
        }
        char hdr[64];
        ksnprintf(hdr, sizeof(hdr), "TILE %d %d %d\n", tx, ty, o);
        if (!srv_send_str(hdr)) return;
        if (!srv_send_bytes(rxdata, (u32)o)) return;
        /* árnyék frissítése */
        for (int y = 0; y < TILE; y++)
            for (int x = 0; x < TILE; x++)
                srv_shadow[(u32)(y0 + y) * srv_w + x0 + x] = bb[(u32)(y0 + y) * srv_w + x0 + x];
        stat_bytes += o + 16;
        sent++;
    }
    srv_scan = (srv_scan + 8) % ntiles;
    stat_frames++;
}

/* kiszolgáló parancsfeldolgozó (a soros vételhez) */
static u8  srvline[160];
static int srvline_len = 0;
static int srv_rx_need = 0;
static char srv_put_name[48];
static int srv_put_left = 0;
static int srv_put_off = 0;

static void srv_handle_line(const char *ln) {
    if (ln[0] == 'K' && ln[1] == 'E' && ln[2] == 'Y' && ln[3] == ' ') {
        int sc = 0, rel = 0, asc = 0;
        const char *p = ln + 4;
        sc = 0; while (*p >= '0' && *p <= '9') { sc = sc * 10 + (*p - '0'); p++; } if (*p == ' ') p++;
        rel = 0; while (*p >= '0' && *p <= '9') { rel = rel * 10 + (*p - '0'); p++; } if (*p == ' ') p++;
        asc = 0; while (*p >= '0' && *p <= '9') { asc = asc * 10 + (*p - '0'); p++; }
        ps2_inject_key((u8)sc, (u8)rel, (u8)asc);
    } else if (ln[0] == 'M' && ln[1] == 'O' && ln[2] == 'U' && ln[3] == 'S' && ln[4] == 'E' && ln[5] == ' ') {
        int dx = 0, dy = 0, bt = 0, neg = 0;
        const char *p = ln + 6;
        if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9') { dx = dx * 10 + (*p - '0'); p++; } if (neg) dx = -dx;
        while (*p == ' ') p++;
        neg = 0; if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9') { dy = dy * 10 + (*p - '0'); p++; } if (neg) dy = -dy;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { bt = bt * 10 + (*p - '0'); p++; }
        ps2_inject_mouse(dx, dy, (u8)bt);
    } else if (ln[0] == 'G' && ln[1] == 'E' && ln[2] == 'T' && ln[3] == ' ') {
        const char *p = ln + 4;
        int n = 0;
        static char path[64];
        while (*p && *p != '\n' && n < 62) path[n++] = *p++;
        path[n] = 0;
        int out = 0;
        if (vfs_read_file(path, (char *)filebuf, (int)sizeof(filebuf), &out)) {
            char hdr[80];
            ksnprintf(hdr, sizeof(hdr), "FILE %d\n%s\n", out, path);
            srv_send_str(hdr);
            srv_send_bytes(filebuf, (u32)out);
        } else {
            srv_send_str("ERR nincs\n");
        }
    } else if (ln[0] == 'P' && ln[1] == 'U' && ln[2] == 'T' && ln[3] == ' ') {
        /* PUT <nev> <meret> */
        const char *p = ln + 4;
        int n = 0;
        while (*p && *p != ' ' && n < 46) srv_put_name[n++] = *p++;
        srv_put_name[n] = 0;
        if (*p == ' ') p++;
        srv_put_left = 0;
        while (*p >= '0' && *p <= '9') { srv_put_left = srv_put_left * 10 + (*p - '0'); p++; }
        srv_put_off = 0;
        if (srv_put_left > (int)sizeof(filebuf)) srv_put_left = (int)sizeof(filebuf);
    } else if (ln[0] == 'B' && ln[1] == 'Y' && ln[2] == 'E') {
        tcp_close(srv_conn);
        srv_conn = 0;
        srv_active = 0;
        kprintf("tavvez: kapcsolat zarva\n");
    } else if (ln[0] == 'H' && ln[1] == 'E' && ln[2] == 'L' && ln[3] == 'L' && ln[4] == 'O') {
        srv_send_str("KRVP1-OK KERINO-SZERVO\n");
        kprintf("tavvez: kezdo keret\n");
    }
}

static void srv_task(void) {
    for (;;) {
        if (!srv_active) { task_yield(); continue; }
        if (!srv_conn && !cli_conn) { task_yield(); continue; }
        net_poll();
        if (srv_conn && srv_conn->used) {
            /* vétel: sorok vagy PUT-adat */
            static u8 rbuf[2048];
            u32 n;
            while ((n = tcp_read(srv_conn, rbuf, sizeof(rbuf))) > 0) {
                for (u32 i = 0; i < n; i++) {
                    u8 ch = rbuf[i];
                    if (srv_put_left > 0) {
                        filebuf[srv_put_off++] = ch;
                        if (--srv_put_left == 0) {
                            char dst[96];
                            ksnprintf(dst, sizeof(dst), "/sys/%s", srv_put_name);
                            if (!vfs_write_file(dst, (const char *)filebuf, srv_put_off)) {
                                ksnprintf(dst, sizeof(dst), "/mem/%s", srv_put_name);
                                vfs_write_file(dst, (const char *)filebuf, srv_put_off);
                            }
                            srv_send_str("OK\n");
                            kprintf("tavvez: PUT kesz (%s, %d B)\n", srv_put_name, srv_put_off);
                        }
                        continue;
                    }
                    if (ch == '\n') {
                        srvline[srvline_len] = 0;
                        srv_handle_line((const char *)srvline);
                        srvline_len = 0;
                    } else if (srvline_len < 158) {
                        srvline[srvline_len++] = ch;
                    }
                }
            }
            srv_stream_step();
            if (srv_conn && !srv_conn->used) { srv_conn = 0; srv_active = 0; }
        }
        task_yield();
    }
}

static void start_server(void) {
    if (srv_active) return;
    srv_w = (int)gfx_width(); srv_h = (int)gfx_height();
    srv_tiles_w = (srv_w + TILE - 1) / TILE;
    srv_tiles_h = (srv_h + TILE - 1) / TILE;
    if (!srv_shadow) srv_shadow = (u32 *)kmalloc((u32)srv_w * srv_h * 4);
    if (srv_shadow) for (int i = 0; i < srv_w * srv_h; i++) srv_shadow[i] = 0xdeadbeef;
    srv_conn = tcp_alloc();
    if (!srv_conn) { ksnprintf(rstatus, sizeof(rstatus), "Nincs szabad TCP slot!"); return; }
    if (!tcp_listen(srv_conn, KRVP_PORT)) { ksnprintf(rstatus, sizeof(rstatus), "Listen hiba!"); return; }
    srv_active = 1;
    srv_scan = 0;
    srvline_len = 0;
    srv_put_left = 0;
    ksnprintf(rstatus, sizeof(rstatus), "KISZOLGALO INDULT (port %d) — a szervezo indul", KRVP_PORT);
    kprintf("tavvez: kiszolgalo indul (port %d)\n", KRVP_PORT);
}

/* ---------- ügyfél (irányító) ---------- */
static u8  cline[160];
static int cline_len = 0;
static int c_rxstate = 0;         /* 0 sor, 1 fájl-adat */
static int c_fileleft = 0, c_fileoff = 0;
static char c_fname[48];
static int prompt_mode = 0;       /* 0 nincs, 1 GET útvonal, 2 PUT útvonal */
static char promptbuf[64]; static int promptlen = 0;

static int cli_send_str(const char *s) {
    int n = 0; while (s[n]) n++;
    return tcp_send(cli_conn, (const u8 *)s, (u32)n);
}

static void client_connect(void) {
    u32 ip;
    u16 port = KRVP_PORT;
    char iponly[32];
    int n = 0;
    while (ipbuf[n] && ipbuf[n] != ':' && n < 30) { iponly[n] = ipbuf[n]; n++; }
    iponly[n] = 0;
    if (ipbuf[n] == ':') {
        int p = 0;
        for (int k = n + 1; ipbuf[k] && ipbuf[k] >= '0' && ipbuf[k] <= '9' && k < n + 6; k++)
            p = p * 10 + (ipbuf[k] - '0');
        if (p > 0) port = (u16)p;
    }
    if (!net_up()) { ksnprintf(rstatus, sizeof(rstatus), "Nincs halozat! Eloszor DHCP (F6/panel)."); return; }
    if (!net_ip_parse(iponly, &ip)) { ksnprintf(rstatus, sizeof(rstatus), "Rossz IP!"); return; }
    cli_conn = tcp_alloc();
    if (!cli_conn) { ksnprintf(rstatus, sizeof(rstatus), "Nincs szabad TCP slot!"); return; }
    if (!tcp_connect(cli_conn, ip, port)) {
        tcp_close(cli_conn); cli_conn = 0;
        ksnprintf(rstatus, sizeof(rstatus), "Nem er el: %s (a szerver fel all?)", ipbuf);
        return;
    }
    if (!cli_send_str("HELLO KERINO-VEZ\n")) { ksnprintf(rstatus, sizeof(rstatus), "Kuldes hiba!"); return; }
    if (!shadow) {
        shadow_w = (int)gfx_width(); shadow_h = (int)gfx_height();
        shadow = (u32 *)kmalloc((u32)shadow_w * shadow_h * 4);
    }
    if (shadow) for (int i = 0; i < shadow_w * shadow_h; i++) shadow[i] = 0xFF0E1116;
    rx_state = RX_IDLE; cline_len = 0; c_rxstate = 0;
    stat_frames = 0; stat_bytes = 0;
    mode = 2;
    ksnprintf(rstatus, sizeof(rstatus), "Kapcsolodva: %s — iranyitod a tavoli gepet!", ipbuf);
    sound_beep(1047, 6);
}

/* csempék visszapakolása az árnyékba: a bájtsor 32 pixel/sor-stride */
static void unpack_tile(int tx, int ty, const u8 *data, int len) {
    if (!shadow) return;
    static u8 tbuf[TILE * TILE * 4];
    int o = 0, w = 0;
    while (o + 1 < len && w < TILE * TILE * 4) {
        int run = data[o++];
        u8 v = data[o++];
        for (int k = 0; k < run && w < TILE * TILE * 4; k++) tbuf[w++] = v;
    }
    int x0 = tx * TILE, y0 = ty * TILE;
    if (x0 >= shadow_w || y0 >= shadow_h) return;
    for (int y = 0; y < TILE; y++) {
        if (y0 + y >= shadow_h) break;
        for (int x = 0; x < TILE; x++) {
            if (x0 + x >= shadow_w) break;
            for (int c = 0; c < 4; c++)
                ((u8 *)shadow)[((u32)(y0 + y) * shadow_w + (x0 + x)) * 4 + c] = tbuf[(y * TILE + x) * 4 + c];
        }
    }
}

static void client_handle_line(const char *ln) {
    if (ln[0] == 'T' && ln[1] == 'I' && ln[2] == 'L' && ln[3] == 'E' && ln[4] == ' ') {
        int tx = 0, ty = 0, l = 0;
        const char *p = ln + 5;
        while (*p >= '0' && *p <= '9') { tx = tx * 10 + (*p - '0'); p++; } if (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { ty = ty * 10 + (*p - '0'); p++; } if (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { l = l * 10 + (*p - '0'); p++; }
        rx_tx = tx; rx_ty = ty; rx_need = l; rx_got = 0;
        rx_state = RX_TILE;
        stat_bytes += l + 16;
        stat_frames++;
    } else if (ln[0] == 'F' && ln[1] == 'I' && ln[2] == 'L' && ln[3] == 'E' && ln[4] == ' ') {
        int sz = 0;
        const char *p = ln + 5;
        while (*p >= '0' && *p <= '9') { sz = sz * 10 + (*p - '0'); p++; }
        rxfilesz = sz;
        c_fileleft = 0; c_fileoff = 0;
        /* a név a következő sorban jön */
        c_rxstate = 3;      /* névre várunk */
    } else if (c_rxstate == 3) {
        int n = 0;
        while (ln[n] && n < 46) { c_fname[n] = ln[n]; n++; }
        c_fname[n] = 0;
        /* majd a méretet adjó sor jön — de az mar a FILE-ban? egyszerűbb: a
           méret már FILE-ban volt, most csak a hosszra váltunk */
        c_fileleft = rxfilesz;
        c_fileoff = 0;
        c_rxstate = 1;
    } else if (ln[0] == 'O' && ln[1] == 'K') {
        ksnprintf(rstatus, sizeof(rstatus), "Szerver: OK");
    } else if (ln[0] == 'E' && ln[1] == 'R' && ln[2] == 'R') {
        ksnprintf(rstatus, sizeof(rstatus), "Szerver: hiba");
    } else if (ln[0] == 'K' && ln[1] == 'R' && ln[2] == 'V' && ln[3] == 'P') {
        ksnprintf(rstatus, sizeof(rstatus), "Kézfogás rendben!");
    }
}

static void client_poll(void) {
    if (!cli_conn || !tcp_connected(cli_conn)) return;
    static u8 rb[2048];
    u32 n;
    while ((n = tcp_read(cli_conn, rb, sizeof(rb))) > 0) {
        for (u32 i = 0; i < n; i++) {
            u8 ch = rb[i];
            if (c_rxstate == 1) {                       /* fájl-adat */
                filebuf[c_fileoff++] = ch;
                if (--c_fileleft == 0) {
                    char dst[96];
                    const char *base = c_fname;
                    for (int k = 0; base[k]; k++) if (base[k] == '/') base = base + k + 1;
                    ksnprintf(dst, sizeof(dst), "/mem/%s", base);
                    vfs_write_file(dst, (const char *)filebuf, c_fileoff);
                    ksnprintf(rstatus, sizeof(rstatus), "Megjott: %s (%d B) -> /mem", base, c_fileoff);
                    c_rxstate = 0;
                    sound_beep(880, 4);
                }
            } else if (rx_state == RX_TILE) {           /* csempék */
                rxdata[rx_got++] = ch;
                if (rx_got >= rx_need) {
                    unpack_tile(rx_tx, rx_ty, rxdata, rx_need);
                    rx_state = RX_IDLE;
                }
            } else {
                if (ch == '\n') {
                    cline[cline_len] = 0;
                    client_handle_line((const char *)cline);
                    cline_len = 0;
                } else if (cline_len < 158) {
                    cline[cline_len++] = ch;
                }
            }
        }
    }
}

/* ---------- app-keretrendszer ---------- */
static int srv_task_started = 0;

void remote_enter(void) {
    mode = 0; msel = 0;
    if (!srv_task_started) {
        task_spawn("tavvez", srv_task, 8192);
        srv_task_started = 1;
    }
    ksnprintf(rstatus, sizeof(rstatus), "");
    sound_beep(880, 4);
}
void remote_exit(void) {
    /* az ügyfél-kapcsolat zárása; a kiszolgáló háttérfeladat fut tovább */
    if (cli_conn) { cli_send_str("BYE\n"); tcp_close(cli_conn); cli_conn = 0; }
    mode = 0;
}

void remote_key(u8 sc, u8 rel, u8 ascii) {
    /* prompt-módok (GET/PUT útvonal) */
    if (prompt_mode) {
        if (sc == KEY_ESC) { prompt_mode = 0; return; }
        if (sc == KEY_ENTER) {
            promptbuf[promptlen] = 0;
            if (prompt_mode == 1) {
                char ln[96];
                ksnprintf(ln, sizeof(ln), "GET %s\n", promptbuf);
                cli_send_str(ln);
                ksnprintf(rstatus, sizeof(rstatus), "Keresve: %s", promptbuf);
            } else {
                int out = 0;
                if (vfs_read_file(promptbuf, (char *)filebuf, (int)sizeof(filebuf), &out) && out > 0) {
                    char ln[96];
                    const char *base = promptbuf;
                    for (int k = 0; promptbuf[k]; k++) if (promptbuf[k] == '/') base = promptbuf + k + 1;
                    ksnprintf(ln, sizeof(ln), "PUT %s %d\n", base, out);
                    cli_send_str(ln);
                    tcp_send(cli_conn, filebuf, (u32)out);
                    ksnprintf(rstatus, sizeof(rstatus), "Kuldve: %s (%d B)", base, out);
                } else ksnprintf(rstatus, sizeof(rstatus), "Nincs ilyen fajl: %s", promptbuf);
            }
            prompt_mode = 0; promptlen = 0;
            return;
        }
        if (ascii == '\b') { if (promptlen > 0) promptbuf[--promptlen] = 0; return; }
        if (ascii >= 32 && ascii < 127 && promptlen < 62) { promptbuf[promptlen++] = ascii; promptbuf[promptlen] = 0; }
        return;
    }
    if (mode == 2 && cli_conn) {
        /* MINDENT továbbítunk (kivéve F10 = kilépés) */
        if (sc == KEY_F10) { app_close(); return; }
        char ln[64];
        ksnprintf(ln, sizeof(ln), "KEY %d %d %d\n", (int)sc, rel ? 1 : 0, (int)ascii);
        cli_send_str(ln);
        return;
    }
    if (mode == 1) {                      /* IP-bevitel */
        if (sc == KEY_ESC) { mode = 0; return; }
        if (sc == KEY_ENTER) { mode = 0; client_connect(); return; }
        if (ascii == '\b') { if (ipcur > 0) { for (int k = ipcur - 1; k < iplen - 1; k++) ipbuf[k] = ipbuf[k + 1]; iplen--; ipcur--; ipbuf[iplen] = 0; } return; }
        if (sc == KEY_LEFT && ipcur > 0) ipcur--;
        if (sc == KEY_RIGHT && ipcur < iplen) ipcur++;
        if (ascii >= 32 && ascii < 127 && iplen < 30) {
            for (int k = iplen; k > ipcur; k--) ipbuf[k] = ipbuf[k - 1];
            ipbuf[ipcur++] = ascii;
            iplen++;
            ipbuf[iplen] = 0;
        }
        return;
    }
    if (rel) return;
    /* számjegy-gyorsválasztó: 1 = kiszolgáló, 2 = irányító */
    if (mode == 0) {
        if (ascii == '1') { msel = 0; start_server(); return; }
        if (ascii == '2') { msel = 1; mode = 1; ipcur = iplen; return; }
    }
    switch (sc) {
        case KEY_UP:   if (msel > 0) msel--; break;
        case KEY_DOWN: if (msel < 1) msel++; break;
        case KEY_F5: if (mode == 2) { prompt_mode = 1; promptlen = 0; ksnprintf(rstatus, sizeof(rstatus), "GET: szerver-oldali utvonal"); } break;
        case KEY_F6: if (mode == 2) { prompt_mode = 2; promptlen = 0; ksnprintf(rstatus, sizeof(rstatus), "PUT: helyi utvonal (/mem/...)"); } break;
        case KEY_ENTER:
            if (msel == 0) start_server();
            else { mode = 1; ipcur = iplen; }
            break;
    }
}

void remote_mouse(int mx, int my, int dx, int dy, u8 btn, u8 prev) {
    (void)mx; (void)my; (void)prev;
    if (mode == 2 && cli_conn) {
        char ln[64];
        ksnprintf(ln, sizeof(ln), "MOUSE %d %d %d\n", dx, dy, (int)btn);
        cli_send_str(ln);
    }
}

void remote_frame(u32 t) {
    (void)t;
    u32 accent = settings_accent_color();
    int W = (int)gfx_width(), H = (int)gfx_height();

    /* ügyfél-képernyő: árnyék kirajzolása */
    if (mode == 2 && shadow) {
        client_poll();
        u32 *bb = gfx_backbuf();
        for (int y = 0; y < shadow_h && y < H; y++)
            for (int x = 0; x < shadow_w && x < W; x++)
                bb[y * W + x] = shadow[y * shadow_w + x];
        /* átfedő keret + állapot */
        gfx_fill_rect(0, H - 46, W, 26, 0xE6151B22);
        char b[128];
        ksnprintf(b, sizeof(b), "TAVOLI KEPERNYO: %s | csempek: %d | KB: %u | F5 get F6 put F10 kilepes", ipbuf, stat_frames, stat_bytes / 1024);
        gfx_draw_text(8, H - 38, b, 0xFF50E878, 0xFF000000u);
        gfx_draw_text(8, H - 16, rstatus, 0xFFE8B24A, 0xFF000000u);
        if (prompt_mode) {
            gfx_fill_rect(8, 24, 560, 24, 0xE6081010);
            gfx_rect(8, 24, 560, 24, accent);
            const char *lbl = prompt_mode == 1 ? "GET: " : "PUT: ";
            gfx_draw_text(16, 30, lbl, 0xFFF0F0F0, 0xFF000000u);
            gfx_draw_text(16 + 5 * 8, 30, promptbuf, 0xFFE8B24A, 0xFF000000u);
        }
        return;
    }

    /* módválasztó / IP-bevitel képernyő */
    gfx_fill_rect(0, 22, W, H - 44, 0xFF10151A);
    gfx_draw_text(12, 34, "TAVVEZERLO — ket Keringo gep kozott", accent, 0xFF000000u);
    gfx_draw_text(12, 58, ui_lang() ? "[1] SERVER — share this screen" : "[1] KISZOLGALO — kep megosztasa", msel == 0 ? 0xFFE8B24A : 0xFFF0F0F0, 0xFF000000u);
    gfx_draw_text(12, 80, ui_lang() ? "[2] CLIENT — control a remote Keringo" : "[2] IRANYITO — tavoli Keringo vezerlese", msel == 1 ? 0xFFE8B24A : 0xFFF0F0F0, 0xFF000000u);
    if (mode == 1) {
        gfx_fill_rect(12, 120, 340, 26, 0xFF0A1018);
        gfx_rect(12, 120, 340, 26, accent);
        gfx_draw_text(20, 127, "IP: ", 0xFFF0F0F0, 0xFF000000u);
        gfx_draw_text(20 + 4 * 8, 127, ipbuf, 0xFFE8B24A, 0xFF000000u);
        if ((t / 25) % 2 == 0) gfx_fill_rect(20 + (4 + ipcur) * 8, 127, 8, 10, 0xFFE8B24A);
        gfx_draw_text(12, 156, "IP[:port] pl. 10.0.2.2:10001 (QEMU hostfwd)", 0xFF8FA0AC, 0xFF000000u);
    }
    if (srv_active)
        gfx_draw_text(12, 104, "* kiszolgalo fut a hatterben (port 7331)", 0xFF50E878, 0xFF000000u);
    gfx_draw_text(12, H - 120, "Fajlok: GET (F5) = szerverrol ide | PUT (F6) => szerverre", 0xFF8FA0AC, 0xFF000000u);
    gfx_draw_text(12, H - 104, "A tavoli gepnek Keringo OS 0.5+ kell (e1000/RTL8139 halozat).", 0xFF8FA0AC, 0xFF000000u);
    gfx_draw_text(12, H - 60, rstatus, 0xFF50E878, 0xFF000000u);
    char b[96];
    ksnprintf(b, sizeof(b), "%s", ui_lang() ? "Remote control (KRVP1)" : "Tavvezérlo (KRVP1)");
    app_chrome(ui_lang() ? "Remote control" : "Távvezérlő", b, accent);
}
