/* tcp.c — minimál TCP kliens: connect, send (újraküldéssel), adatgyűjtés, close */
#include "kernel.h"
#include "net.h"

static inline void tput_be32(u8 *p, u32 v) { p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }
static inline u32 tget_be32(const u8 *p) { return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }

#define TCPF_FIN 0x01
#define TCPF_SYN 0x02
#define TCPF_RST 0x04
#define TCPF_PSH 0x08
#define TCPF_ACK 0x10

#define RBUF_CAP  (768 * 1024)
#define SNDQ_CAP  4096

static tcpconn_t conns[3];

tcpconn_t *tcp_alloc(void) {
    for (int i = 0; i < 3; i++) {
        if (!conns[i].used) {
            for (u8 *p = (u8 *)&conns[i]; p < (u8 *)&conns[i] + sizeof(tcpconn_t); p++) *p = 0;
            conns[i].rbuf = (u8 *)kmalloc(RBUF_CAP);
            if (!conns[i].rbuf) return 0;
            conns[i].rcap = RBUF_CAP;
            conns[i].used = 1;
            return &conns[i];
        }
    }
    return 0;
}

int tcp_connected(tcpconn_t *c) { return c && (c->state == 2); }

static u16 lport_next = 40000;

/* 0.5 Távvezérlő: figyelő (server) socket — 4-es állapot, bármely társról elfogad */
int tcp_listen(tcpconn_t *c, u16 port) {
    if (!c) return 0;
    c->rip = 0; c->rport = 0;
    c->lport = port;
    c->iss = (tick_count() << 8) | 0x24543300u;
    c->snd_nxt = c->iss + 1;
    c->snd_una = c->iss;
    c->rcv_nxt = 0;
    c->state = 4;                          /* LISTEN */
    return 1;
}

int tcp_connect(tcpconn_t *c, u32 rip, u16 rport) {
    if (!c || !net_dev) return 0;
    c->rip = rip;
    c->rport = rport;
    c->lport = lport_next++;
    c->iss = (tick_count() << 8) | 0x00371000u;
    c->snd_nxt = c->iss + 1;
    c->snd_una = c->iss;
    c->rcv_nxt = 0;
    c->state = 1;                           /* SYN_SENT */
    /* MSS opcióval SYN */
    u8 opt[4] = { 2, 4, 0x04, 0x00 };       /* MSS = 1024 */
    {
        static u8 tbuf[1600];
        tbuf[0] = (u8)(c->lport >> 8); tbuf[1] = (u8)c->lport;
        tbuf[2] = (u8)(rport >> 8); tbuf[3] = (u8)rport;
        tput_be32(tbuf + 4, c->iss);
        tput_be32(tbuf + 8, 0);
        tbuf[12] = 0x60;                    /* 24 bájt fejléc (MSS) */
        tbuf[13] = TCPF_SYN;
        tbuf[14] = 0x20; tbuf[15] = 0x00;   /* ablak = 0x2000 (hálózati sorrend) */
        tbuf[16] = 0; tbuf[17] = 0; tbuf[18] = 0; tbuf[19] = 0;
        for (int i = 0; i < 4; i++) tbuf[20 + i] = opt[i];
        /* 0.5: ellenőrző összeg a SYN-re (bájt-bájtos írás) */
        {
            u32 sum = 0;
            u32 sip = net_self_ip();
            sum += (sip >> 16) & 0xFFFF; sum += sip & 0xFFFF;
            sum += (rip >> 16) & 0xFFFF; sum += rip & 0xFFFF;
            sum += 6; sum += 24;
            for (u32 i = 0; i < 24; i += 2) sum += (u16)(((u32)tbuf[i] << 8) | tbuf[i + 1]);
            while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
            u16 cs = (u16)(~sum & 0xFFFF);
            tbuf[16] = (u8)(cs >> 8); tbuf[17] = (u8)cs;
        }
        if (!net_send_ip(6, rip, tbuf, 24)) return 0;
    }
    u32 until = tick_count() + 40;
    while ((s32)(tick_count() - until) < 0) {
        net_poll();
        if (c->state == 2) return 1;
        if (c->state == 0) return 0;
        hlt();
    }
    kprintf("tcp: connect idotullepes\n");
    return 0;
}

/* aktuális vevőablak */
static void tcp_csum_out(u8 *tcp, u32 tcplen, tcpconn_t *c);
static u16 rwin(tcpconn_t *c) {
    u32 freeb = c->rcap - c->rlen;
    if (freeb > 0x4000) freeb = 0x4000;
    return (u16)freeb;
}

static void send_ack(tcpconn_t *c, u8 extra, const u8 *data, u32 len) {
    static u8 tbuf[1600];
    u8 hdr = 20;
    tbuf[0] = (u8)(c->lport >> 8); tbuf[1] = (u8)c->lport;
    tbuf[2] = (u8)(c->rport >> 8); tbuf[3] = (u8)c->rport;
    tput_be32(tbuf + 4, c->snd_nxt);
    tput_be32(tbuf + 8, c->rcv_nxt);
    tbuf[12] = (u8)(hdr << 2);
    tbuf[13] = TCPF_ACK | extra;
    {
        u16 w = rwin(c);
        tbuf[14] = (u8)(w >> 8); tbuf[15] = (u8)w;      /* hálózati bájtsorrend! */
    }
    tbuf[16] = 0; tbuf[17] = 0; tbuf[18] = 0; tbuf[19] = 0;
    for (u32 i = 0; i < len && i < 1400; i++) tbuf[20 + i] = data[i];
    /* 0.5: VALÓDI TCP-ellenőrző összeg — a slirp/valós stackok eldobják a 0-s csumot */
    tcp_csum_out(tbuf, (u32)hdr + len, c);
    net_send_ip(6, c->rip, tbuf, hdr + len);
}

/* 0.5: TCP-fejléc + adat ellenőrző összegének kiszámítása (pszeudo-fejléccsel) */
static u16 tcp_csum_calc(const u8 *tcp, u32 tcplen, u32 sip, u32 dip) {
    u32 sum = 0;
    sum += (sip >> 16) & 0xFFFF; sum += sip & 0xFFFF;
    sum += (dip >> 16) & 0xFFFF; sum += dip & 0xFFFF;
    sum += 6; sum += tcplen;
    for (u32 i = 0; i < tcplen; i += 2) {
        u16 w = (i + 1 < tcplen) ? (u16)(((u16)tcp[i] << 8) | tcp[i + 1]) : (u16)((u16)tcp[i] << 8);
        sum += w;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)(~sum & 0xFFFF);
}
static void tcp_csum_out(u8 *tcp, u32 tcplen, tcpconn_t *c) {
    tcp[16] = 0; tcp[17] = 0;
    u32 sip = net_self_ip(), dip = c->rip;
    u16 cs = tcp_csum_calc(tcp, tcplen, sip, dip);
    tcp[16] = (u8)(cs >> 8); tcp[17] = (u8)cs;          /* hálózati bájtsorrend! */
}

void tcp_input(const u8 *pkt, u32 iplen, u32 srcip) {
    const u8 *ip = pkt;
    /* 0.5 javítás: a TCP-szakasz hossza = IP-összhossz − IP-fejléc (20).
       Korábban az ip[2..3] (IP-összhossz) mezőt olvastuk „tcplen”-ként,
       amivel a második ellenőrzés MINDEN csomagot eldobott — a 0.4.0-ban ez
       rejtve maradt, mert a HTTP a wifi demo rádión át ment. */
    u32 tcplen = iplen - 20;
    if (tcplen < 20) return;
    const u8 *t = ip + 20;
    u16 sport = (u16)((t[0] << 8) | t[1]);
    u16 dport = (u16)((t[2] << 8) | t[3]);
    u32 seq = tget_be32(t + 4);
    u32 ack = tget_be32(t + 8);
    u8 off = (u8)((t[12] >> 4) * 4);
    u8 flags = t[13];
    if (off < 20 || off > tcplen) return;
    u32 dlen = tcplen - off;
    const u8 *data = t + off;

    for (int i = 0; i < 3; i++) {
        tcpconn_t *c = &conns[i];
        if (!c->used) continue;
        if (c->rip != srcip || c->rport != sport || c->lport != dport) continue;

        if (flags & TCPF_RST) { c->state = 0; return; }

        if ((flags & TCPF_SYN) && (flags & TCPF_ACK)) {
            if (c->state == 1 && ack == c->iss + 1) {
                c->rcv_nxt = seq + 1;
                c->snd_una = ack;
                c->state = 2;
                send_ack(c, 0, 0, 0);
                return;
            }
        }
        if (flags & TCPF_ACK) {
            if ((s32)(ack - c->snd_una) > 0) c->snd_una = ack;
        }
        if (dlen > 0) {
            if (seq == c->rcv_nxt) {
                if (c->rlen + dlen <= c->rcap) {
                    for (u32 k = 0; k < dlen; k++) c->rbuf[c->rlen + k] = data[k];
                    c->rlen += dlen;
                    c->rcv_nxt = seq + dlen;
                }
                send_ack(c, 0, 0, 0);
            } else {
                send_ack(c, 0, 0, 0);        /* soron kívüli: újra-ACK */
            }
        }
        if (flags & TCPF_FIN) {
            c->rcv_nxt = seq + dlen + 1;
            send_ack(c, TCPF_FIN, 0, 0);
            c->snd_nxt++;
            c->state = 3;
        }
        return;
    }

    /* 0.5: figyelő kapcsolatok — SYN-t kapott listen-socketre elfogadjuk */
    for (int i = 0; i < 3; i++) {
        tcpconn_t *c = &conns[i];
        if (!c->used || c->state != 4 || c->lport != dport) continue;
        if ((flags & TCPF_SYN) && !(flags & TCPF_ACK)) {
            c->rip = srcip;
            c->rport = sport;
            c->rcv_nxt = seq + 1;
            c->snd_una = c->iss;
            /* SYN-ACK (iss szekvenciával) — 0.5: ellenőrző összeggel */
            {
                static u8 tbuf[1600];
                tbuf[0] = (u8)(c->lport >> 8); tbuf[1] = (u8)c->lport;
                tbuf[2] = (u8)(sport >> 8); tbuf[3] = (u8)sport;
                tput_be32(tbuf + 4, c->iss);
                tput_be32(tbuf + 8, c->rcv_nxt);
                tbuf[12] = 0x50;
                tbuf[13] = TCPF_SYN | TCPF_ACK;
                {
                    u16 w = rwin(c);
                    tbuf[14] = (u8)(w >> 8); tbuf[15] = (u8)w;
                }
                tbuf[16] = 0; tbuf[17] = 0; tbuf[18] = 0; tbuf[19] = 0;
                {
                    u32 sum = 0;
                    u32 sip = net_self_ip();
                    sum += (sip >> 16) & 0xFFFF; sum += sip & 0xFFFF;
                    sum += (srcip >> 16) & 0xFFFF; sum += srcip & 0xFFFF;
                    sum += 6; sum += 20;
                    for (u32 i = 0; i < 20; i += 2) sum += (u16)(((u32)tbuf[i] << 8) | tbuf[i + 1]);
                    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
                    u16 cs = (u16)(~sum & 0xFFFF);
                    tbuf[16] = (u8)(cs >> 8); tbuf[17] = (u8)cs;
                }
                net_send_ip(6, srcip, tbuf, 20);
            }
            c->snd_nxt = c->iss + 1;
            c->state = 2;                    /* egyszerűsített elfogadás */
            kprintf("tcp: listen-kapcsolat elfogadva (%u -> %u)\n", sport, dport);
            return;
        }
    }
}

int tcp_send(tcpconn_t *c, const u8 *data, u32 len) {
    if (!c || c->state != 2) return 0;
    if (len > 4096) len = 4096;
    u32 start_seq = c->snd_nxt;

    for (int attempt = 0; attempt < 3; attempt++) {
        /* a nyugtázatlan rész küldése (elsõ körben: minden) */
        u32 acked = (s32)(c->snd_una - start_seq) > 0 ? c->snd_una - start_seq : 0;
        u32 offset = acked;
        while (offset < len) {
            u32 chunk = len - offset; if (chunk > 1000) chunk = 1000;
            send_ack(c, TCPF_PSH, data + offset, chunk);
            offset += chunk;
            c->last_send = tick_count();
            net_poll_ms(20);
            if (c->state != 2) return 0;
        }
        c->snd_nxt = start_seq + len;
        /* várakozás a teljes ACK-re */
        u32 until = tick_count() + 100;
        while ((s32)(tick_count() - until) < 0) {
            net_poll();
            if ((s32)(c->snd_una - start_seq) >= (s32)len) return 1;
            hlt();
        }
    }
    kprintf("tcp: kuldes nincs nyugtazva\n");
    return 0;
}

int tcp_wait_data(tcpconn_t *c, u32 timeout_ms) {
    u32 until = tick_count() + timeout_ms / 10 + 1;
    u32 start_len = c->rlen;
    while ((s32)(tick_count() - until) < 0) {
        net_poll();
        if (c->rlen > start_len) return 1;
        if (c->state == 0 || c->state == 3) return (c->rlen > start_len) ? 1 : 2;
        hlt();
    }
    return 0;
}

u32 tcp_read(tcpconn_t *c, u8 *buf, u32 max) {
    u32 n = c->rlen < max ? c->rlen : max;
    for (u32 i = 0; i < n; i++) buf[i] = c->rbuf[i];
    /* maradék előre mozgatása */
    for (u32 i = n; i < c->rlen; i++) c->rbuf[i - n] = c->rbuf[i];
    c->rlen -= n;
    return n;
}

void tcp_close(tcpconn_t *c) {
    if (!c) return;
    if (c->state == 2) {
        send_ack(c, TCPF_FIN, 0, 0);
        c->snd_nxt++;
        for (int i = 0; i < 20; i++) { net_poll(); hlt(); }
    }
    c->state = 0;
    c->used = 0;
}

void tcp_shutdown_all(void) {
    for (int i = 0; i < 3; i++)
        if (conns[i].used) { conns[i].state = 0; conns[i].used = 0; }
}
