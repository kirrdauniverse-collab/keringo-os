/* http.c — HTTP/1.1 GET kliens + MJPEG élő videó stream (SOI/EOI keretkeresés) */
#include "kernel.h"
#include "net.h"

/* URL elemzés: "http://host[:port]/path" */
typedef struct {
    char host[64];
    u16 port;
    char path[128];
} urlinfo_t;

static u32 hstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static int parse_url(const char *url, urlinfo_t *u) {
    const char *p;
    /* https:// tiltott (TLS a 0.3-ban lesz) */
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p'
        && url[4] == 's' && url[5] == ':' && url[6] == '/' && url[7] == '/') {
        kprintf("http: HTTPS nem tamogatott meg (TLS a 0.3 cel)\n");
        return -2;
    }
    if (url[0] != 'h' || url[1] != 't' || url[2] != 't' || url[3] != 'p'
        || url[4] != ':' || url[5] != '/' || url[6] != '/') return 0;
    p = url + 7;
    int h = 0;
    u->port = 80;
    while (*p && *p != '/' && *p != ':' && h < 63) u->host[h++] = *p++;
    u->host[h] = 0;
    if (*p == ':') {
        p++;
        u32 v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (u32)(*p - '0'); p++; }
        if (v) u->port = (u16)v;
    }
    int q = 0;
    if (*p == '/') { while (*p && q < 127) u->path[q++] = *p++; }
    else { u->path[q++] = '/'; }
    u->path[q] = 0;
    if (!u->path[0]) { u->path[0] = '/'; u->path[1] = 0; }
    return 1;
}

static int http_request(tcpconn_t *c, const urlinfo_t *u, const char *host_hdr_ip) {
    char req[260];
    ksnprintf(req, sizeof(req),
              "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: KeringoOS/0.2\r\n"
              "Accept: */*\r\nConnection: close\r\n\r\n",
              u->path, host_hdr_ip ? host_hdr_ip : u->host);
    return tcp_send(c, (const u8 *)req, hstrlen(req));
}

/* fejléc-vége keresése: visszaadja a test kezdetét vagy -1 */
static s32 find_header_end(const u8 *buf, u32 len) {
    for (u32 i = 0; i + 3 < len; i++) {
        if (buf[i] == 0x0D && buf[i+1] == 0x0A && buf[i+2] == 0x0D && buf[i+3] == 0x0A)
            return (s32)(i + 4);
    }
    return -1;
}

static void parse_headers(const u8 *hdr, u32 hlen, http_result_t *res, int *chunked, u32 *clen) {
    /* státuszsor */
    res->status = 0;
    if (hlen > 12 && hdr[0] == 'H' && hdr[1] == 'T' && hdr[2] == 'T' && hdr[3] == 'P') {
        u32 o = 0;
        while (o < hlen && hdr[o] != ' ') o++;
        while (o < hlen && hdr[o] == ' ') o++;
        u32 v = 0;
        while (o < hlen && hdr[o] >= '0' && hdr[o] <= '9') { v = v * 10 + (u32)(hdr[o] - '0'); o++; }
        res->status = (int)v;
    }
    *chunked = 0;
    *clen = 0;
    for (u32 i = 0; i + 8 < hlen; i++) {
        if (hdr[i] == 0x0A) {
            const u8 *line = hdr + i + 1;
            if (line[0] == 'T' || line[0] == 'C' || line[0] == 't' || line[0] == 'c') {
                if (line[0] == 'T' || line[0] == 't') {
                    if ((line[1] == 'r' || line[1] == 'R')) {
                        /* Transfer-Encoding: chunked ? */
                        if (line[0] == 'T' && line[1] == 'r' && line[8] == '-' ) {
                            /* végignézzük a sorban a chunked szót */
                            for (u32 k = 0; k < 40 && line[k] && line[k] != 0x0D; k++)
                                if (line[k] == 'c' && line[k+1] == 'h' && line[k+2] == 'u'
                                    && line[k+3] == 'n' && line[k+4] == 'k' && line[k+5] == 'e'
                                    && line[k+6] == 'd') *chunked = 1;
                        }
                    }
                }
                if (line[0] == 'C' && line[1] == 'o' && line[2] == 'n' && line[3] == 't') {
                    if (line[8] == 'L' || line[8] == 'l') {          /* Content-Length */
                        u32 k = 0;
                        while (k < 50 && line[k] != ':') k++;
                        k++;
                        while (k < 60 && line[k] == ' ') k++;
                        u32 v = 0;
                        while (k < 70 && line[k] >= '0' && line[k] <= '9') { v = v * 10 + (u32)(line[k] - '0'); k++; }
                        *clen = v;
                    } else if (line[8] == 'T' || line[8] == 't') {    /* Content-Type */
                        u32 k = 0;
                        while (k < 50 && line[k] != ':') k++;
                        k++;
                        while (k < 60 && line[k] == ' ') k++;
                        int w = 0;
                        while (k < 60 && line[k] && line[k] != 0x0D && w < 47)
                            res->content_type[w++] = line[k++];
                        res->content_type[w] = 0;
                    }
                }
            }
        }
    }
}

/* ------- teljes letöltés ------- */
static u8 *dlbuf = 0;
static u32 dlcap = 0;

static int dl_append(const u8 *data, u32 len, u32 offset) {
    if (offset + len > dlcap) {
        u32 ncap = dlcap ? dlcap : 65536;
        while (ncap < offset + len) ncap *= 2;
        u8 *nb = (u8 *)kmalloc(ncap);
        if (!nb) return 0;
        if (dlbuf && offset) for (u32 i = 0; i < offset; i++) nb[i] = dlbuf[i];
        dlbuf = nb;
        dlcap = ncap;
    }
    for (u32 i = 0; i < len; i++) dlbuf[offset + i] = data[i];
    return 1;
}

int http_fetch(const char *url, http_result_t *res) {
    urlinfo_t u;
    res->body = 0; res->body_len = 0; res->status = 0; res->content_type[0] = 0;
    kprintf("http: fetch [%s] net=%d\n", url, net_up());   /* 0.5: belépés-napló */
    int pr = parse_url(url, &u);
    if (pr != 1) return pr == -2 ? -2 : 0;
    kprintf("http: parse OK host=%s port=%u\n", u.host, u.port);
    u32 ip;
    if (!net_dns_resolve(u.host, &ip)) { kprintf("http: host nem talalhato\n"); return 0; }
    tcpconn_t *c = tcp_alloc();
    if (!c) { kprintf("http: nincs TCP slot\n"); return 0; }
    if (!tcp_connect(c, ip, u.port)) { tcp_close(c); return 0; }
    kprintf("http: kapcsolat OK (%x:%u)\n", ip, u.port);   /* D10 */
    if (!http_request(c, &u, 0)) { tcp_close(c); return 0; }
    kprintf("http: GET elkuldve\n");                        /* D10 */

    u32 total = 0;
    int header_end = -1;
    u32 deadline = tick_count() + 1500;      /* 15 s */
    while ((s32)(tick_count() - deadline) < 0) {
        int got = tcp_wait_data(c, 300);
        u8 tmp[4096];
        u32 n;
        while ((n = tcp_read(c, tmp, sizeof(tmp))) > 0) {
            if (!dl_append(tmp, n, total)) { tcp_close(c); return 0; }
            total += n;
            if (header_end < 0) {
                s32 he = find_header_end(dlbuf, total);
                if (he >= 0) header_end = he;
            }
        }
        if (header_end >= 0) {
            int chunked = 0; u32 clen = 0;
            parse_headers(dlbuf, (u32)header_end, res, &chunked, &clen);
            if (clen && total >= (u32)header_end + clen) break;
        }
        if (got == 2) break;                 /* kapcsolat zárult */
        if (got == 0 && total > 0 && header_end >= 0) {
            /* még várhatunk egy kicsit */
        }
    }
    tcp_close(c);
    if (header_end < 0) { kprintf("http: nincs fejléc-vége\n"); return 0; }
    int chunked = 0; u32 clen = 0;
    parse_headers(dlbuf, (u32)header_end, res, &chunked, &clen);
    kprintf("http: status=%u hossz=%u tipus=%s\n", res->status, total, res->content_type);   /* 0.5: letöltés-napló */

    u8 *body = dlbuf + header_end;
    u32 blen = total - (u32)header_end;

    if (chunked) {
        /* chunk-dekódolás */
        static u8 *chbuf = 0;
        static u32 chcap = 0;
        if (!chbuf) { chbuf = (u8 *)kmalloc(1024 * 1024); chcap = 1024 * 1024; }
        u32 out = 0;
        u32 i = 0;
        while (i + 2 < blen) {
            u32 sz = 0;
            while (i < blen && body[i] != 0x0D) {
                u8 ch = body[i];
                u32 v = (ch >= '0' && ch <= '9') ? (u32)(ch - '0')
                      : (ch >= 'a' && ch <= 'f') ? (u32)(ch - 'a' + 10)
                      : (ch >= 'A' && ch <= 'F') ? (u32)(ch - 'A' + 10) : 16;
                if (v > 15) break;
                sz = sz * 16 + v;
                i++;
            }
            if (i + 2 > blen) break;
            i += 2;                          /* CRLF */
            if (sz == 0) break;
            if (out + sz > chcap) sz = chcap - out;
            for (u32 k = 0; k < sz; k++) chbuf[out + k] = body[i + k];
            out += sz;
            i += sz + 2;
        }
        res->body = chbuf;
        res->body_len = out;
        return res->status ? 1 : 0;
    }
    res->body = body;
    res->body_len = blen;
    return res->status ? 1 : 0;
}

void http_free_result(http_result_t *res) {
    res->body = 0; res->body_len = 0;        /* bump-heap: csak elengedjük */
}

/* ------- MJPEG élő adás ------- */
static u8 *sv_buf = 0;
static u32 sv_len = 0;
#define SV_CAP (512 * 1024)
static u8 *frame_buf = 0;
#define FRAME_CAP (160 * 1024)

int http_stream_open(const char *url, tcpconn_t **out) {
    urlinfo_t u;
    int pr = parse_url(url, &u);
    if (pr != 1) return 0;
    u32 ip;
    if (!net_dns_resolve(u.host, &ip)) return 0;
    tcpconn_t *c = tcp_alloc();
    if (!c) return 0;
    if (!tcp_connect(c, ip, u.port)) { tcp_close(c); return 0; }
    if (!http_request(c, &u, 0)) { tcp_close(c); return 0; }
    if (!sv_buf) {
        sv_buf = (u8 *)kmalloc(SV_CAP);
        frame_buf = (u8 *)kmalloc(FRAME_CAP);
        sv_len = 0;
    }
    *out = c;
    return 1;
}

/* SOI (FFD8FF) és EOI (FFD9) közötti teljes keret keresése */
int http_stream_poll(tcpconn_t *c, u8 **frame_out, u32 *frame_len) {
    if (!c || c->state == 0) return -1;
    u8 tmp[4096];
    u32 n;
    while ((n = tcp_read(c, tmp, sizeof(tmp))) > 0) {
        if (sv_len + n <= SV_CAP) {
            for (u32 i = 0; i < n; i++) sv_buf[sv_len + i] = tmp[i];
            sv_len += n;
        }
    }
    net_poll();
    /* keretkeresés az elejétől: fejléc átugrása nem kell (a szerver MJPEG-et küld) */
    int found = 0;
    for (u32 i = 0; i + 3 < sv_len; i++) {
        if (sv_buf[i] == 0xFF && sv_buf[i+1] == 0xD8 && sv_buf[i+2] == 0xFF) {
            /* SOI: EOI keresése */
            for (u32 j = i + 2; j + 1 < sv_len; j++) {
                if (sv_buf[j] == 0xFF && sv_buf[j+1] == 0xD9) {
                    u32 flen = j + 2 - i;
                    if (flen > 100 && flen < FRAME_CAP) {
                        for (u32 k = 0; k < flen; k++) frame_buf[k] = sv_buf[i + k];
                        *frame_out = frame_buf;
                        *frame_len = flen;
                        found = 1;
                    }
                    /* feldolgozott adat levágása */
                    u32 consumed = j + 2;
                    for (u32 k = 0; k < sv_len - consumed; k++) sv_buf[k] = sv_buf[consumed + k];
                    sv_len -= consumed;
                    return found;
                }
            }
            break;                          /* nincs még teljes keret */
        }
        /* SOI előtti szemét elhagyása */
        if (sv_buf[i] != 0xFF) { }
    }
    if (sv_len > SV_CAP - 8192) {           /* puffer-szivárgás ellen */
        for (u32 k = 0; k < 8; k++) sv_buf[k] = 0xFF;
        sv_len = 8;
    }
    return 0;
}
