/* net.c — hálózati mag: eszközkeresés, ARP, IPv4, ICMP, UDP, DHCP, DNS */
#include "kernel.h"
#include "net.h"

netdev_t *net_dev = 0;
static int hw_ready = 0;
static volatile int net_locked = 0;      /* 1 = a hálózati task foglalta (DHCP) */

void net_lock_all(void)  { net_locked = 1; }
void net_unlock_all(void){ net_locked = 0; }

/* ARP cache: 4 bejegyzés */
typedef struct { u32 ip; u8 mac[6]; int valid; } arpent_t;
static arpent_t arp_cache[4];

/* DNS várakozó válasz */
static volatile int dns_wait_id = -1;
static u32 dns_answer_ip = 0;

static u8 pktbuf[1600];                     /* kimenő keret készítéséhez */

/* ---------------- IP segédek ---------------- */
/* hálózati bájtrend (nagy-endian) írás/olvasás */
static inline void put_be16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static inline void put_be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static inline u16 get_be16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static inline u32 get_be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

u32 net_checksum(const u8 *buf, u32 len) {
    u32 sum = 0;
    while (len > 1) { sum += ((u32)buf[0] << 8) | buf[1]; buf += 2; len -= 2; }
    if (len) sum += (u32)buf[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

void net_ip_str(u32 ip, char *out, int outsz) {
    ksnprintf(out, outsz, "%u.%u.%u.%u",
              (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

int net_ip_parse(const char *s, u32 *out) {
    u32 a = 0, b = 0, c = 0, d = 0;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        u32 v = 0; int dig = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (u32)(*s - '0'); s++; dig++; if (dig > 3) return 0; }
        if (!dig) return 0;
        if (i < 3) { if (*s != '.') return 0; s++; }
        switch (i) {
            case 0: a = v; break; case 1: b = v; break;
            case 2: c = v; break; default: d = v; break;
        }
        n = i + 1;
    }
    if (n != 4) return 0;
    *out = (a << 24) | (b << 16) | (c << 8) | d;
    return 1;
}

int net_hw_ready(void) { return hw_ready; }
int net_up(void) { return net_dev && net_dev->ip_ok; }
u32 net_self_ip(void) { return net_dev ? net_dev->ip : 0; }
const char *net_chipname(void) { return net_dev ? (net_dev->type == 1 ? "e1000" : "rtl8139") : "-"; }

/* ---------------- keretküldés ---------------- */
int net_send_raw(const u8 *pkt, u32 len) {
    if (!net_dev) return 0;
    return net_dev->send(net_dev, pkt, len);
}

/* ARP csomag */
static int send_arp(u16 op, const u8 *dmac, u32 dpid, u32 spid) {
    u8 *p = pktbuf;
    for (int i = 0; i < 6; i++) p[i] = dmac ? dmac[i] : 0xFF;
    for (int i = 0; i < 6; i++) p[6 + i] = net_dev->mac[i];
    put_be16(p + 12, 0x0806);                /* ARP */
    put_be16(p + 14, 1);                     /* Ethernet */
    put_be16(p + 16, 0x0800);                /* IPv4 */
    p[18] = 6; p[19] = 4;
    put_be16(p + 20, op);                    /* 1 = kérés, 2 = válasz */
    for (int i = 0; i < 6; i++) p[22 + i] = net_dev->mac[i];
    put_be32(p + 28, spid);
    for (int i = 0; i < 6; i++) p[32 + i] = dmac ? dmac[i] : 0;
    put_be32(p + 38, dpid);
    if (net_send_raw(pktbuf, 42)) { net_dev->tx_pkts++; return 1; }
    return 0;
}

/* IPv4 küldése (payload = teljes L4 szegmens) */
int net_send_ip(u8 proto, u32 dip, const u8 *payload, u32 len) {
    if (!net_dev) return 0;
    u8 dmac[6];
    if ((dip >> 24) == 255 || dip == 0xFFFFFFFFu) {
        for (int i = 0; i < 6; i++) dmac[i] = 0xFF;
    } else {
        /* lokális? */
        u32 netmask = net_dev->mask ? net_dev->mask : 0xFFFFFF00u;
        u32 next;
        if ((dip & netmask) == (net_dev->ip & netmask)) next = dip;
        else next = net_dev->gw ? net_dev->gw : dip;
        if (!net_arp_resolve(next, dmac)) { kprintf("net: ARP nem sikerult (%x)\n", next); return 0; }
    }
    if (len > 1460) len = 1460;
    u8 *p = pktbuf;
    for (int i = 0; i < 6; i++) p[i] = dmac[i];
    for (int i = 0; i < 6; i++) p[6 + i] = net_dev->mac[i];
    *(u16 *)(p + 12) = 0x0008;               /* IPv4 */
    /* IP fejléc */
    u8 *ip = p + 14;
    ip[0] = 0x45;
    ip[1] = 0;
    put_be16(ip + 2, 20 + len);              /* teljes hossz */
    put_be16(ip + 4, 0x1234);                /* azonosító */
    put_be16(ip + 6, 0x4000);                /* DF */
    ip[8] = 64;
    ip[9] = proto;
    ip[10] = 0; ip[11] = 0;                  /* checksum helye */
    put_be32(ip + 12, net_dev->ip);
    put_be32(ip + 16, dip);
    put_be16(ip + 10, net_checksum(ip, 20));
    for (u32 i = 0; i < len; i++) ip[20 + i] = payload[i];
    if (net_send_raw(pktbuf, 14 + 20 + len)) { net_dev->tx_pkts++; return 1; }
    return 0;
}

int net_send_udp(u32 dip, u16 sport, u16 dport, const u8 *data, u32 len) {
    if (len > 1400) len = 1400;
    static u8 udpbuf[1500];
    udpbuf[0] = (u8)(sport >> 8); udpbuf[1] = (u8)sport;
    udpbuf[2] = (u8)(dport >> 8); udpbuf[3] = (u8)dport;
    put_be16(udpbuf + 4, 8 + len);
    udpbuf[6] = 0; udpbuf[7] = 0;            /* checksum = 0 (megengedett) */
    for (u32 i = 0; i < len; i++) udpbuf[8 + i] = data[i];
    return net_send_ip(17, dip, udpbuf, 8 + len);
}

int net_send_tcp_raw(u32 dip, u16 sport, u16 dport, u32 seq, u32 ack,
                     u8 flags, const u8 *data, u32 len) {
    static u8 tbuf[1600];
    if (len > 1400) len = 1400;
    tbuf[0] = (u8)(sport >> 8); tbuf[1] = (u8)sport;
    tbuf[2] = (u8)(dport >> 8); tbuf[3] = (u8)dport;
    put_be32(tbuf + 4, seq);
    put_be32(tbuf + 8, ack);
    tbuf[12] = 0x50;                         /* 20 bájt fejléc */
    tbuf[13] = flags;
    put_be16(tbuf + 14, 0x2000);             /* ablak 8 KB */
    tbuf[16] = 0; tbuf[17] = 0; tbuf[18] = 0; tbuf[19] = 0;
    for (u32 i = 0; i < len; i++) tbuf[20 + i] = data[i];
    return net_send_ip(6, dip, tbuf, 20 + len);
}

/* ---------------- ARP ---------------- */
static void arp_input(const u8 *p, u32 len) {
    (void)len;
    if (get_be16(p + 14) != 1 || get_be16(p + 16) != 0x0800) return;
    u16 op = get_be16(p + 20);
    u32 sip = get_be32(p + 28);
    u8 *smac = (u8 *)p + 22;
    if (op == 1) {                           /* kérés */
        u32 tip = get_be32(p + 38);
        if (net_dev && tip == net_dev->ip && net_dev->ip_ok) {
            send_arp(2, smac, sip, net_dev->ip);
        }
    } else if (op == 2) {                    /* válasz */
        for (int i = 0; i < 4; i++) {
            if (arp_cache[i].valid && arp_cache[i].ip == sip) {
                for (int k = 0; k < 6; k++) arp_cache[i].mac[k] = smac[k];
                return;
            }
        }
        for (int i = 0; i < 4; i++) {
            if (!arp_cache[i].valid) {
                arp_cache[i].ip = sip;
                for (int k = 0; k < 6; k++) arp_cache[i].mac[k] = smac[k];
                arp_cache[i].valid = 1;
                return;
            }
        }
    }
}

int net_arp_resolve(u32 ip, u8 *mac_out) {
    for (int t = 0; t < 6; t++) {
        for (int i = 0; i < 4; i++)
            if (arp_cache[i].valid && arp_cache[i].ip == ip) {
                for (int k = 0; k < 6; k++) mac_out[k] = arp_cache[i].mac[k];
                return 1;
            }
        if (t == 0) send_arp(1, 0, ip, net_dev ? net_dev->ip : 0);
        net_poll_ms(80);
    }
    return 0;
}

/* ---------------- ICMP ---------------- */
static void icmp_input(const u8 *ip, u32 iplen) {
    const u8 *icmp = ip + 20;
    if (icmp[0] != 8) return;                /* csak echo request */
    u32 len = iplen - 20;
    if (len > 1400) return;
    static u8 echo[1500];
    for (u32 i = 0; i < len; i++) echo[i] = icmp[i];
    echo[0] = 0;                             /* echo reply */
    echo[2] = 0; echo[3] = 0;
    put_be16(echo + 2, net_checksum(echo, len));
    net_send_ip(1, *(const u32 *)(ip + 12), echo, len);
}

/* ---------------- UDP: DHCP + DNS ---------------- */
#define DHCP_MAGIC 0x63538263u   /* 63 82 53 63 hálózati sorrendben */

static void dhcp_send(u8 msgtype, u32 requested_ip, u32 server_id) {
    static u8 d[300];
    for (u32 i = 0; i < sizeof(d); i++) d[i] = 0;
    d[0] = 1; d[1] = 1; d[2] = 6; d[3] = 0;
    put_be32(d + 4, 0x4B45524E);             /* xid = 'KERN' */
    put_be16(d + 10, 0x8000);                /* broadcast flag */
    for (int i = 0; i < 6; i++) d[28 + i] = net_dev->mac[i];
    put_be32(d + 236, DHCP_MAGIC);
    /* 53: üzenettípus */
    d[240] = 53; d[241] = 1; d[242] = msgtype;
    /* 61: kliensazonosító */
    d[243] = 61; d[244] = 7; d[245] = 1;
    for (int i = 0; i < 6; i++) d[246 + i] = net_dev->mac[i];
    int o = 252;
    if (requested_ip) { d[o++] = 50; d[o++] = 4; put_be32(d + o, requested_ip); o += 4; }
    if (server_id)     { d[o++] = 54; d[o++] = 4; put_be32(d + o, server_id);     o += 4; }
    /* 55: paraméterlista */
    d[o++] = 55; d[o++] = 3; d[o++] = 1; d[o++] = 3; d[o++] = 6;
    d[o++] = 255;
    net_send_udp(0xFFFFFFFFu, 68, 67, d, sizeof(d));
}

static void dhcp_input(const u8 *data, u32 len) {
    if (len < 244) return;
    if (*(const u32 *)(data + 236) != DHCP_MAGIC) return;
    u8 msgtype = 0; u32 mask = 0, gw = 0, dns = 0, sid = 0;
    u32 yiaddr = get_be32(data + 16);
    u32 o = 240;
    while (o + 1 < len) {
        u8 code = data[o++];
        if (code == 255) break;
        u8 olen = data[o++];
        if (code == 53 && olen >= 1) msgtype = data[o];
        else if (code == 1 && olen >= 4) mask = get_be32(data + o);
        else if (code == 3 && olen >= 4) gw = get_be32(data + o);
        else if (code == 6 && olen >= 4) dns = get_be32(data + o);
        else if (code == 54 && olen >= 4) sid = get_be32(data + o);
        o += olen;
    }
    if (msgtype == 2) {                      /* OFFER: kérjük */
        kprintf("dhcp: offer ip=%x\n", yiaddr);
        dhcp_send(3, yiaddr, sid);
    } else if (msgtype == 5 && yiaddr) {     /* ACK: kész */
        net_dev->ip = yiaddr;
        net_dev->mask = mask;
        net_dev->gw = gw;
        net_dev->dns = dns;
        net_dev->ip_ok = 1;
        kprintf("dhcp: cim kapaszkodik\n");
    } else if (msgtype == 6) {
        kprintf("dhcp: NAK\n");
    }
}

static void udp_input(const u8 *ip, u32 iplen) {
    const u8 *uh = ip + 20;
    u16 dport = (u16)((uh[2] << 8) | uh[3]);
    u32 ulen = ((u32)uh[4] << 8) | uh[5];
    if (ulen < 8 || ulen > iplen - 20) return;
    const u8 *data = uh + 8;
    u32 dlen = ulen - 8;
    if (dport == 68) dhcp_input(data, dlen);
    else if (dport == 53) {
        /* DNS válasz: id + címek kiemelése */
        if (dlen < 12) return;
        u16 id = (u16)((data[0] << 8) | data[1]);
        if ((s32)id != dns_wait_id) return;
        u16 ancount = (u16)((data[6] << 8) | data[7]);  /* már BE */
        if (ancount == 0) return;
        u32 o = 12;
        /* kérdésnév átugrása */
        while (o < dlen && data[o]) { o += (u32)data[o] + 1; }
        o += 5;                              /* null + típus + osztály */
        for (int a = 0; a < ancount && o + 12 <= dlen; a++) {
            u16 qtype = (u16)((data[o + 2] << 8) | data[o + 3]);
            u16 rdlen = (u16)((data[o + 8] << 8) | data[o + 9]);
            if (qtype == 1 && rdlen == 4) {
                dns_answer_ip = get_be32(data + o + 10);
                return;
            }
            o += 12 + rdlen;
        }
    }
}

/* ---------------- DNS ---------------- */
int net_dns_resolve(const char *name, u32 *ip_out) {
    if (!net_dev || !net_dev->dns) return 0;
    /* már IP? */
    if (net_ip_parse(name, ip_out)) return 1;
    static u8 q[100];
    u32 o = 0;
    q[o++] = 0x4B; q[o++] = 0x45;            /* id */
    q[o++] = 0x01; q[o++] = 0x00;            /* rekurzív kérés */
    q[o++] = 0; q[o++] = 1;                  /* 1 kérdés */
    q[o++] = 0; q[o++] = 0; q[o++] = 0; q[o++] = 0; q[o++] = 0; q[o++] = 0;
    const char *p = name;
    while (*p && o < 90) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        q[o++] = (u8)(dot - p);
        for (const char *s = p; s < dot && o < 95; s++) q[o++] = (u8)*s;
        p = *dot ? dot + 1 : dot;
    }
    q[o++] = 0;
    q[o++] = 0; q[o++] = 1;                  /* A rekord */
    q[o++] = 0; q[o++] = 1;                  /* IN */
    dns_wait_id = 0x4B45;
    dns_answer_ip = 0;
    for (int t = 0; t < 4; t++) {
        net_send_udp(net_dev->dns, 0x4B45, 53, q, o);
        for (int w = 0; w < 20; w++) {
            net_poll_ms(50);
            if (dns_answer_ip) { *ip_out = dns_answer_ip; dns_wait_id = -1; return 1; }
        }
    }
    dns_wait_id = -1;
    return 0;
}

/* ---------------- RX ---------------- */
void net_rx(u8 *pkt, u32 len) {
    if (!net_dev) return;
    net_dev->rx_pkts++;
    if (len < 14) return;
    u16 etype = (u16)((pkt[12] << 8) | pkt[13]);
    if (etype == 0x0806) { arp_input(pkt, len); return; }
    if (etype != 0x0800) return;
    const u8 *ip = pkt + 14;
    u32 iplen = get_be16(ip + 2);
    if (iplen < 20 || iplen > len - 14) return;
    u8 proto = ip[9];
    if (proto == 1) icmp_input(ip, iplen);
    else if (proto == 17) udp_input(ip, iplen);
    else if (proto == 6) tcp_input(ip, iplen, get_be32(ip + 12));
}

int net_poll(void) {
    if (!net_dev || net_locked) return 0;
    net_dev->poll(net_dev);
    return 1;
}

void net_poll_raw(void) {
    if (net_dev) net_dev->poll(net_dev);
}

void net_poll_ms(int ms) {
    u32 until = tick_count() + (u32)ms / 10 + 1;
    while ((s32)(tick_count() - until) < 0) {
        net_poll_raw();
        hlt();
    }
}

/* ---------------- DHCP folyamat (hálózati taskból) ---------------- */
void net_dhcp(void) {
    if (!net_dev) return;
    net_dev->ip = 0; net_dev->ip_ok = 0; net_dev->gw = 0; net_dev->dns = 0;
    for (int attempt = 0; attempt < 3 && !net_dev->ip_ok; attempt++) {
        kprintf("dhcp: keres (%d)\n", attempt + 1);
        dhcp_send(1, 0, 0);                  /* DISCOVER */
        u32 until = tick_count() + 80;
        while ((s32)(tick_count() - until) < 0 && !net_dev->ip_ok) {
            net_poll_raw();
            hlt();
        }
    }
    if (!net_dev->ip_ok) {
        /* QEMU-felhasználói hálózat alapbeállítása tartalékként */
        net_dev->ip = 0x0A00020Fu;           /* 10.0.2.15 */
        net_dev->mask = 0xFFFFFF00u;         /* 255.255.255.0 */
        net_dev->gw = 0x0A000202u;           /* 10.0.2.2 */
        net_dev->dns = 0x0A000203u;          /* 10.0.2.3 */
        net_dev->ip_ok = 1;
        kprintf("dhcp: nincs valasz, QEMU-alapbeallitas\n");
    }
    char b[20];
    net_ip_str(net_dev->ip, b, sizeof(b));
    kprintf("net: cimunk %s gw=%x dns=%x\n", b, net_dev->gw, net_dev->dns);
    /* átjáró ARP a gyorsításért */
    u8 mac[6];
    net_arp_resolve(net_dev->gw, mac);
}

/* ---------------- hardverkeresés ---------------- */
void net_init_hw(void) {
    hw_ready = 0;
    pci_dev_t pc;
    int n = 0;
    while (pci_scan_class(&pc, 0x02, 0x00, n)) {
        u16 vid = PCI_VENID(pc);
        u16 did = PCI_DEVID(pc);
        if (vid == 0x8086 && (did == 0x100E || did == 0x100F || did == 0x1050)) {
            if (e1000_add_controller(pc)) { hw_ready = 1; return; }
        }
        n++;
        if (n > 8) break;
    }
    n = 0;
    while (pci_scan_class(&pc, 0x02, 0x00, n)) {
        u16 vid = PCI_VENID(pc);
        u16 did = PCI_DEVID(pc);
        if (vid == 0x10EC && (did == 0x8139 || did == 0x8136 || did == 0x8138)) {
            if (rtl8139_add_controller(pc)) { hw_ready = 1; return; }
        }
        n++;
        if (n > 8) break;
    }
    kprintf("net: nincs ismert halokartya\n");
}
