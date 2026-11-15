/* wifi.c — Keringő OS Wi-Fi alrendszer.
 * 802.11g STA: szken, auth/assoc, WPA2-PSK 4-way (EAPOL), CCMP.
 * A driver (ath9k.c) 802.11 kereteket ad ide; a net.c felé ethernetet
 * mutatunk (netdev_t), így a DHCP/TCP/böngésző változatlanul működik.
 * Demo rádió: QEMU-teszthez, virtuális AP-k válaszolnak (valódi kriptóval!). */
#include "wifi.h"
#include "crypto.h"

#define MAX_AP      12
#define FRAME_MAX   1700

static u32 kstrlen_(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void kmemcpy_(u8 *d, const u8 *s, u32 n) { for (u32 i = 0; i < n; i++) d[i] = s[i]; }
static int  kmemeq_(const u8 *a, const u8 *b, u32 n) { for (u32 i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
static void kmemset_(u8 *d, u8 v, u32 n) { for (u32 i = 0; i < n; i++) d[i] = v; }

static inline u16 be16_(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static inline u32 be32_(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static inline void set_be16_(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static inline void set_be32_(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

#define FC_TYPE_MGMT 0
#define FC_TYPE_DATA 2
#define FC_TODS      0x0100
#define FC_FROMDS    0x0200
#define FC_PROT      0x4000

#define KI_VER_SHA1_AES  0x0002
#define KI_ACK           0x0008
#define KI_MIC           0x0010
#define KI_SECURE        0x0020
#define KI_INSTALL       0x0400
#define KI_PAIRWISE      0x0800

static const u8 bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

/* ---- előredeklarációk (demo rádió) ---- */
void demo_radio_setup(void);
void demo_radio_set_chan(int c);
void demo_radio_tick(void);
int  wifi_demo_tx_wrap(const u8 *f, u32 len);

/* ---------------- AP-lista ---------------- */
typedef struct {
    char    ssid[33];
    u8      bssid[6];
    int     chan;
    int     rssi;
    int     sec;
    u32     seen;
} apent_t;

static apent_t  aps[MAX_AP];
static int      ap_n = 0;

/* ---------------- állapot ---------------- */
static wifi_hw_t    *hw = 0;
static int          demo_mode = 0;

static int          state = WIFI_OFF;
static int          scan_ch = 0;
static int          scan_step = 0;
static u32          scan_until = 0;
static u32          state_until = 0;

static u8           own_mac[6];
static u8           bssid[6];
static int          cur_chan = 6;
static char         ssid_used[33];
static char         pass_used[65];
static int          net_sec = 0;

static u16          tx_seq = 0;
static u64          pn_tx_uc = 1;
static u64          pn_tx_gr = 1;

static u8           anonce[32], snonce[32];
static u8           pmk[32], ptk[48];
static u8           gtk[16]; static int gtk_valid = 0;

static int          need_dhcp = 0;
static char         err_txt[48] = {0};

static netdev_t     wifi_dev;

static void set_err(const char *e) {
    int i = 0;
    for (; e[i] && i < 47; i++) err_txt[i] = e[i];
    err_txt[i] = 0;
}

/* ---------------- CCMP segédek ---------------- */
static void ccmp_aad(u8 *aad, u16 fc, const u8 *a1, const u8 *a2, u16 sc) {
    aad[0] = (u8)((fc & 0xc78f) >> 8);
    aad[1] = (u8)(fc & 0x8f);
    kmemcpy_(aad + 2, a1, 6);
    kmemcpy_(aad + 8, a2, 6);
    aad[14] = (u8)(sc & 0x0f);
    aad[15] = 0;
}

static void ccmp_nonce(u8 *n, const u8 *a2, u64 pn) {
    n[0] = 0;
    kmemcpy_(n + 1, a2, 6);
    for (int i = 0; i < 6; i++) n[7 + i] = (u8)(pn >> (8 * (5 - i)));
}

/* ---------------- adatkeret küldése (ethernet → 802.11 + CCMP) ------- */
static int build_data_frame(u8 *out, const u8 *eth, u32 elen, int encrypt) {
    const u8 *da = eth, *sa = eth + 6;
    u16 etype = (u16)((eth[12] << 8) | eth[13]);
    u32 plen = elen - 14;
    int group = (da[0] & 1) != 0;              /* multicast/broadcast */
    u16 fc = 0x0008 | FC_TODS;
    if (encrypt) fc |= FC_PROT;
    u64 pn = group ? pn_tx_gr++ : pn_tx_uc++;

    u32 idx = 0;
    out[idx++] = (u8)(fc & 0xff); out[idx++] = (u8)(fc >> 8);
    out[idx++] = 0; out[idx++] = 0;            /* duration */
    kmemcpy_(out + idx, bssid, 6); idx += 6;   /* A1 = BSSID (toDS) */
    kmemcpy_(out + idx, own_mac, 6); idx += 6; /* A2 = TA */
    kmemcpy_(out + idx, da, 6); idx += 6;      /* A3 = DA */
    /* Seq Control: a 802.11-ben KIS-ENDIAN (mint az FC)! */
    out[idx++] = (u8)((tx_seq << 4) & 0xf0);
    out[idx++] = (u8)((tx_seq >> 4) & 0xff); tx_seq++;
    (void)0;
    if (encrypt) {
        out[idx++] = (u8)pn; out[idx++] = (u8)(pn >> 8);
        out[idx++] = 0x20;                     /* extIV, keyid 0 */
        out[idx++] = (u8)(pn >> 16); out[idx++] = (u8)(pn >> 24);
        out[idx++] = (u8)(pn >> 32); out[idx++] = (u8)(pn >> 40);
        out[idx++] = 0;
    }
    /* payload: LLC/SNAP + adat */
    u8 payload[FRAME_MAX];
    u32 pi = 0;
    payload[pi++] = 0xAA; payload[pi++] = 0xAA; payload[pi++] = 0x03;
    payload[pi++] = 0; payload[pi++] = 0; payload[pi++] = 0;
    payload[pi++] = (u8)(etype >> 8); payload[pi++] = (u8)etype;
    for (u32 i = 0; i < plen; i++) payload[pi++] = eth[14 + i];
    if (encrypt) {
        u8 nonce[13], aad[16];
        ccmp_nonce(nonce, own_mac, pn);        /* A2 = TA = mi */
        ccmp_aad(aad, fc, bssid, own_mac, 0);
        const u8 *key = (group && gtk_valid) ? gtk : ptk + 32;
        ccmp_encrypt(key, nonce, aad, 16, payload, pi, payload);  /* in-place */
        kmemcpy_(out + idx, payload, pi + 8);
        idx += pi + 8;
    } else {
        kmemcpy_(out + idx, payload, pi);
        idx += pi;
    }
    return (int)idx;
}

/* ---------------- keret-adás ---------------- */
static int send_frame(const u8 *f, u32 len, u8 rate) {
    if (demo_mode) return wifi_demo_tx_wrap(f, len);
    if (!hw || !hw->tx) return 0;
    return hw->tx(f, len, rate);
}

/* ---------------- bejövő feldolgozás ---------------- */
static void handle_mgmt(const u8 *f, u32 len, u8 rssi);
static void handle_data(const u8 *f, u32 len, u8 rssi);
static void handle_eapol(const u8 *sa, const u8 *payload, u32 plen);

void wifi_rx_frame(const u8 *buf, u32 len, u8 rssi, u8 rate) {
    (void)rate;
    if (len < 10 || len > FRAME_MAX) return;
    u16 fc = (u16)(buf[0] | (buf[1] << 8));
    if ((fc & 0x000c) == FC_TYPE_MGMT) handle_mgmt(buf, len, rssi);
    else if ((fc & 0x000c) == (FC_TYPE_DATA << 2)) handle_data(buf, len, rssi);
}

/* ---------------- EAPOL-Key elemző/építő (STA és demo AP közös) -------- */
typedef struct {
    u8     type;
    u16    key_info;
    u16    key_len;
    u8     replay[8];
    u8     nonce[32];
    u8     mic[16];
    u16    dlen;
    const u8 *data;
} eapolkey_t;

int parse_eapol_key(const u8 *p, u32 n, eapolkey_t *k) {
    if (n < 4) return 0;
    u16 flen = be16_(p + 2);
    if (4 + flen > n) return 0;
    const u8 *d = p + 4;
    if (flen < 95) return 0;
    k->type = d[0];
    k->key_info = be16_(d + 1);
    k->key_len = be16_(d + 3);
    kmemcpy_(k->replay, d + 5, 8);
    kmemcpy_(k->nonce, d + 13, 32);
    kmemcpy_(k->mic, d + 77, 16);
    k->dlen = be16_(d + 93);
    k->data = d + 95;
    if (95 + (u32)k->dlen > flen) return 0;
    return 1;
}

/* teljes EAPOL-Key üzenet (EAPOL fejléccel); MIC mező a mic mutatóból */
int build_eapol_key(u8 *out, u16 key_info, u16 key_len,
                    const u8 *replay, const u8 *nonce,
                    const u8 *mic, const u8 *data, u16 dlen) {
    u32 i = 0;
    out[i++] = 1; out[i++] = 3;                /* EAPOL v1, EAPOL-Key */
    set_be16_(out + i, (u16)(95 + dlen)); i += 2;
    out[i++] = 2;                              /* RSN key descriptor */
    set_be16_(out + i, key_info); i += 2;
    set_be16_(out + i, key_len); i += 2;
    kmemcpy_(out + i, replay, 8); i += 8;
    if (nonce) kmemcpy_(out + i, nonce, 32); else kmemset_(out + i, 0, 32);
    i += 32;
    kmemset_(out + i, 0, 16); i += 16;         /* IV */
    kmemset_(out + i, 0, 8); i += 8;           /* RSC */
    kmemset_(out + i, 0, 8); i += 8;           /* ID */
    if (mic) kmemcpy_(out + i, mic, 16); else kmemset_(out + i, 0, 16);
    i += 16;
    set_be16_(out + i, dlen); i += 2;
    if (dlen) kmemcpy_(out + i, data, dlen);
    i += dlen;
    return (int)i;
}

static void minmax_copy(u8 *out, const u8 *a, const u8 *b, u32 n) {
    for (u32 i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            if (a[i] < b[i]) { kmemcpy_(out, a, n); return; }
            kmemcpy_(out, b, n); return;
        }
    }
    kmemcpy_(out, a, n);                       /* egyezés: sorrend mindegy */
}
static void maxmin_copy(u8 *out, const u8 *a, const u8 *b, u32 n) {
    for (u32 i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            if (a[i] > b[i]) { kmemcpy_(out, a, n); return; }
            kmemcpy_(out, b, n); return;
        }
    }
    kmemcpy_(out, a, n);
}

/* PTK levezetés a 802.11i szabvány szerint */
static void derive_ptk(u8 *out48, const u8 *pmk32,
                       const u8 *mac_sta, const u8 *mac_ap,
                       const u8 *nonce_sta, const u8 *nonce_ap) {
    u8 kd[76];
    minmax_copy(kd, mac_sta, mac_ap, 6);
    maxmin_copy(kd + 6, mac_sta, mac_ap, 6);
    minmax_copy(kd + 12, nonce_sta, nonce_ap, 32);
    maxmin_copy(kd + 44, nonce_sta, nonce_ap, 32);
    prf512(pmk32, 32, "Pairwise key expansion", kd, 76, out48, 48);
}

static void send_eapol_data(const u8 *ap_mac, const u8 *eapol, u16 elen) {
    u8 f11[FRAME_MAX];
    u32 i = 0;
    f11[i++] = 0x08; f11[i++] = 0x01;          /* data, toDS */
    f11[i++] = 0; f11[i++] = 0;
    kmemcpy_(f11 + i, bssid, 6); i += 6;
    kmemcpy_(f11 + i, own_mac, 6); i += 6;
    kmemcpy_(f11 + i, ap_mac, 6); i += 6;
    f11[i++] = (u8)((tx_seq << 4) & 0xf0);
    f11[i++] = (u8)((tx_seq >> 4) & 0xff); tx_seq++;
    f11[i++] = 0xAA; f11[i++] = 0xAA; f11[i++] = 0x03;
    f11[i++] = 0; f11[i++] = 0; f11[i++] = 0;
    f11[i++] = 0x88; f11[i++] = 0x8e;
    kmemcpy_(f11 + i, eapol, elen); i += elen;
    send_frame(f11, i, RATE_1M);
}

/* ---------------- STA EAPOL állapotgép ---------------- */
static void handle_eapol(const u8 *sa, const u8 *payload, u32 plen) {
    if (state != WIFI_EAPOL) return;
    eapolkey_t k;
    if (!parse_eapol_key(payload, plen, &k)) return;
    if (k.type != 2) return;

    if ((k.key_info & KI_ACK) && !(k.key_info & KI_MIC)) {
        /* M1 → M2 */
        kprintf("wifi: M1 megjott, M2 megy\n");
        kmemcpy_(anonce, k.nonce, 32);
        u32 t = tick_count();
        for (int i = 0; i < 32; i++)
            snonce[i] = (u8)(own_mac[i % 6] * 31 + t * 17 + i * 7);
        derive_ptk(ptk, pmk, own_mac, sa, snonce, anonce);
        u8 f[128];
        int n = build_eapol_key(f, KI_VER_SHA1_AES | KI_MIC | KI_PAIRWISE,
                                16, k.replay, snonce, 0, 0, 0);
        u8 mic[20];
        hmac_sha1(ptk, 16, f, n, mic);
        n = build_eapol_key(f, KI_VER_SHA1_AES | KI_MIC | KI_PAIRWISE,
                            16, k.replay, snonce, mic, 0, 0);
        send_eapol_data(sa, f, (u16)n);
        return;
    }
    if ((k.key_info & KI_MIC) && (k.key_info & KI_SECURE) && (k.key_info & KI_ACK)) {
        /* M3 → M4 + kulcsok */
        u8 f[160];
        int n = build_eapol_key(f, k.key_info, k.key_len, k.replay, k.nonce,
                                0, k.data, k.dlen);
        u8 calc[20];
        hmac_sha1(ptk, 16, f, n, calc);
        int ok = 1;
        for (int i = 0; i < 16; i++) if (k.mic[i] != calc[i]) ok = 0;
        if (!ok) { kprintf("wifi: M3 MIC HIBA\n"); set_err("M3 MIC hibas"); return; }
        kprintf("wifi: M3 MIC rendben, GTK+M4\n");
        /* GTK KDE: DD len 00:0F:AC 01 <kid> <rsv> GTK16 */
        gtk_valid = 0;
        u32 o = 0;
        while (o + 2 <= (u32)k.dlen) {
            u8 t = k.data[o], l = k.data[o + 1];
            if (o + 2 + l > (u32)k.dlen) break;
            if (t == 0xdd && l >= 22 &&
                k.data[o+2]==0x00 && k.data[o+3]==0x0f &&
                k.data[o+4]==0xac && k.data[o+5]==0x01) {
                kmemcpy_(gtk, k.data + o + 8, 16);
                gtk_valid = 1;
            }
            o += 2 + l;
        }
        u8 m4[128];
        int n4 = build_eapol_key(m4, KI_VER_SHA1_AES | KI_MIC | KI_SECURE | KI_PAIRWISE,
                                 16, k.replay, 0, 0, 0, 0);
        u8 mic4[20];
        hmac_sha1(ptk, 16, m4, n4, mic4);
        n4 = build_eapol_key(m4, KI_VER_SHA1_AES | KI_MIC | KI_SECURE | KI_PAIRWISE,
                             16, k.replay, 0, mic4, 0, 0);
        send_eapol_data(sa, m4, (u16)n4);
        state = WIFI_UP;
        kprintf("wifi: KAPCSOLODVA %s (WPA2 CCMP)\n", ssid_used);
        set_err("");
        need_dhcp = 1;
        kmemcpy_(bssid, sa, 6);
        sound_beep(880, 6);
        /* net átállás a wifi eszközre */
        wifi_dev.ip_ok = 0;
        net_dev = &wifi_dev;
        return;
    }
}

/* ---------------- bejövő adatkeret ---------------- */
static void handle_data(const u8 *f, u32 len, u8 rssi) {
    (void)rssi;
    u16 fc = (u16)(f[0] | (f[1] << 8));
    u8 subt = (fc >> 4) & 0xf;
    if (subt >= 4) return;                     /* QoS/null-adat: kihagyva */
    if (!(fc & FC_FROMDS)) return;
    const u8 *da = f + 4, *bss = f + 10, *sa = f + 16;
    u32 hl = 24;
    if (len < hl + 8) return;
    const u8 *pl = f + hl;
    u32 plen = len - hl;
    u8 eth[FRAME_MAX];
    u32 elen = 0;

    if (fc & FC_PROT) {
        if (plen < 16) return;
        u64 pn = (u64)pl[0] | ((u64)pl[1] << 8) | ((u64)pl[3] << 16) |
                 ((u64)pl[4] << 24) | ((u64)pl[5] << 32) | ((u64)pl[6] << 40);
        const u8 *enc = pl + 8;
        u32 el2 = plen - 8;                    /* MIC-et is tartalmaz */
        if (el2 < 8) return;
        u8 nonce[13], aad[16], dec[FRAME_MAX];
        ccmp_nonce(nonce, bss, pn);
        ccmp_aad(aad, fc, da, bss, (u16)(f[22] | (f[23] << 8)));
        const u8 *key = (kmemeq_(da, bcast, 6) && gtk_valid) ? gtk : ptk + 32;
        if (!ccmp_decrypt(key, nonce, aad, 16, enc, el2, dec)) {
            kprintf("sta-n: %x,%x,%x,%x,%x,%x,%x,%x pn=%x\n",
                    nonce[0], nonce[1], nonce[2], nonce[3], nonce[4], nonce[5],
                    nonce[6], nonce[12], (u32)pn);
            kprintf("wifi: CCMP dekodolasi hiba (fv=%x)\n", fc);
            return;
        }
        if (el2 < 8 || dec[0]!=0xAA || dec[1]!=0xAA || dec[2]!=0x03) return;
        u16 etype = (u16)((dec[6] << 8) | dec[7]);
        u32 dlen = el2 - 8;
        kmemcpy_(eth, da, 6); kmemcpy_(eth + 6, sa, 6);
        eth[12] = (u8)(etype >> 8); eth[13] = (u8)etype;
        if (14 + dlen > FRAME_MAX) return;
        kmemcpy_(eth + 14, dec + 8, dlen);
        elen = 14 + dlen;
    } else {
        if (plen < 8 || pl[0]!=0xAA || pl[1]!=0xAA || pl[2]!=0x03) return;
        u16 etype = (u16)((pl[6] << 8) | pl[7]);
        u32 dlen = plen - 8;
        kmemcpy_(eth, da, 6); kmemcpy_(eth + 6, sa, 6);
        eth[12] = (u8)(etype >> 8); eth[13] = (u8)etype;
        if (14 + dlen > FRAME_MAX) return;
        kmemcpy_(eth + 14, pl + 8, dlen);
        elen = 14 + dlen;
    }
    if (elen >= 16 && eth[12] == 0x88 && eth[13] == 0x8e) {
        handle_eapol(sa, eth + 14, elen - 14);
        return;
    }
    net_rx(eth, elen);
}

/* ---------------- mgmt keretek ---------------- */
static void handle_mgmt(const u8 *f, u32 len, u8 rssi) {
    u16 fc = (u16)(f[0] | (f[1] << 8));
    u8 subt = (fc >> 4) & 0xf;
    const u8 *a2 = f + 10;
    const u8 *body = f + 24;
    u32 blen = (len > 24) ? len - 24 : 0;

    if (subt == 8 || subt == 5) {              /* beacon / proberesp */
        if (blen < 12) return;
        char ssid[33]; ssid[0] = 0;
        int sec = 0;
        u32 o = 12;
        while (o + 2 <= blen) {
            u8 t = body[o], l = body[o + 1];
            if (o + 2 + l > blen) break;
            if (t == 0 && l < 33) {
                for (u8 i = 0; i < l; i++) ssid[i] = (char)body[o + 2 + i];
                ssid[l] = 0;
            } else if (t == 48) sec = 1;
            else if (t == 221 && l > 8 && body[o+2]==0x00 &&
                     body[o+3]==0x50 && body[o+4]==0xf2 && body[o+5]==0x02) sec = 1;
            o += 2 + l;
        }
        int found = -1;
        for (int i = 0; i < ap_n; i++)
            if (kmemeq_(aps[i].bssid, a2, 6)) { found = i; break; }
        if (found < 0 && ap_n < MAX_AP) {
            found = ap_n++;
            kmemset_((u8*)&aps[found], 0, sizeof(apent_t));
            kmemcpy_(aps[found].bssid, a2, 6);
        }
        if (found >= 0) {
            apent_t *ap = &aps[found];
            int i = 0;
            for (; ssid[i] && i < 32; i++) ap->ssid[i] = ssid[i];
            ap->ssid[i] = 0;
            ap->rssi = rssi;
            ap->sec = sec;
            ap->chan = (state == WIFI_SCAN) ? scan_ch : cur_chan;
            ap->seen = tick_count();
        }
        return;
    }
    if (state == WIFI_AUTH && subt == 11 && blen >= 6) {
        u16 alg = be16_(body), seq = be16_(body + 2), st = be16_(body + 4);
        if (alg == 0 && seq == 2 && st == 0) {
            kprintf("wifi: auth rendben -> assoc\n");
            state = WIFI_ASSOC;
            state_until = tick_count() + 200;
            u8 fr[128]; u32 i = 0;
            fr[i++] = 0x00; fr[i++] = 0x00;
            fr[i++] = 0; fr[i++] = 0;
            kmemcpy_(fr + i, bcast, 6); i += 6;
            kmemcpy_(fr + i, a2, 6); i += 6;
            kmemcpy_(fr + i, a2, 6); i += 6;
            fr[i++] = (u8)((tx_seq << 4) & 0xf0);
    fr[i++] = (u8)((tx_seq >> 4) & 0xff); tx_seq++;
            set_be16_(fr + i, 0x0431); i += 2;
            set_be16_(fr + i, 10); i += 2;
            u32 sl = kstrlen_(ssid_used);
            fr[i++] = 0; fr[i++] = (u8)sl;
            for (u32 s = 0; s < sl; s++) fr[i++] = (u8)ssid_used[s];
            fr[i++] = 1; fr[i++] = 4;
            fr[i++] = 0x82; fr[i++] = 0x84; fr[i++] = 0x8b; fr[i++] = 0x96;
            send_frame(fr, i, RATE_1M);
        } else if (seq == 2 && st != 0) set_err("auth elutasitva");
        return;
    }
    if (subt == 1) kprintf("wifi: assoc resp lattam state=%d len=%d\n", state, (int)len);
    if (state == WIFI_ASSOC && subt == 1 && blen >= 6) {
        u16 st = be16_(body + 2);
        if (st == 0) {
            if (net_sec == 0) {
                /* nyílt hálózat: nincs 4-way, egyből kész */
                state = WIFI_UP;
                kprintf("wifi: KAPCSOLODVA %s (nyilt)\n", ssid_used);
                set_err("");
                need_dhcp = 1;
                kmemcpy_(bssid, a2, 6);
                sound_beep(880, 6);
                wifi_dev.ip_ok = 0;
                net_dev = &wifi_dev;
                return;
            }
            state = WIFI_EAPOL;
            state_until = tick_count() + 600;
            pn_tx_uc = 1; pn_tx_gr = 1;
        } else set_err("assoc elutasitva");
        return;
    }
    if (subt == 12) {                          /* deauth */
        if (kmemeq_(f + 4, bssid, 6) || kmemeq_(f + 4, bcast, 6)) {
            if (state == WIFI_UP || state == WIFI_EAPOL) {
                state = WIFI_OFF;
                gtk_valid = 0;
                set_err("AP deauth");
                if (net_dev == &wifi_dev) net_dev = 0;
            }
        }
    }
}

/* ---------------- netdev felület ---------------- */
static int wifi_net_send(netdev_t *dev, const u8 *pkt, u32 len) {
    (void)dev;
    if (state != WIFI_UP) return 0;
    /* statikus: a TX szinkron (demo: teljesen feldolgozott; ath9k: TxDone-ra vár),
     * így a DHCP-válasz újrabelépésekor a külső keret már nem kell */
    static u8 f11[FRAME_MAX];
    int n = build_data_frame(f11, pkt, len, net_sec == 1);
    if (n <= 0) return 0;
    return send_frame(f11, (u32)n, RATE_6M);
}

static void wifi_net_poll(netdev_t *dev) {
    (void)dev;
    if (demo_mode) demo_radio_tick();
    else if (hw && hw->poll) hw->poll();
}

static void wifi_net_shutdown(netdev_t *dev) { (void)dev; wifi_disconnect(); }

/* ---------------- szken és FSM ---------------- */
static const int scan_chans[3] = {1, 6, 11};

static void send_probe_req(int chan) {
    (void)chan;
    u8 f[64]; u32 i = 0;
    f[i++] = 0x40; f[i++] = 0x00;
    f[i++] = 0; f[i++] = 0;
    kmemcpy_(f + i, bcast, 6); i += 6;
    kmemcpy_(f + i, own_mac, 6); i += 6;
    kmemcpy_(f + i, bcast, 6); i += 6;
        f[i++] = (u8)((tx_seq << 4) & 0xf0);
    f[i++] = (u8)((tx_seq >> 4) & 0xff); tx_seq++;
    f[i++] = 0; f[i++] = 0;
    f[i++] = 1; f[i++] = 4;
    f[i++] = 0x82; f[i++] = 0x84; f[i++] = 0x8b; f[i++] = 0x96;
    send_frame(f, i, RATE_1M);
}

void wifi_poll(void) {
    if (state == WIFI_SCAN) {
        if (demo_mode) demo_radio_tick();
        if ((s32)(tick_count() - scan_until) >= 0) {
            scan_step++;
            if (scan_step >= 3) {
                state = WIFI_OFF;
                return;
            }
            scan_ch = scan_chans[scan_step];
            if (demo_mode) demo_radio_set_chan(scan_ch);
            else if (hw && hw->set_chan) hw->set_chan(scan_ch);
            cur_chan = scan_ch;
            send_probe_req(scan_ch);
            scan_until = tick_count() + 120;
        }
        return;
    }
    if (state == WIFI_AUTH || state == WIFI_ASSOC || state == WIFI_EAPOL) {
        if ((s32)(tick_count() - state_until) >= 0) {
            if (state == WIFI_EAPOL) set_err("nem jott EAPOL (rossz jelszo?)");
            else set_err("idokeret lejart");
            state = WIFI_OFF;
        }
    }
}

int wifi_scan_start(void) {
    if (!wifi_hw_present()) return 0;
    ap_n = 0;
    state = WIFI_SCAN;
    scan_step = 0;
    scan_ch = scan_chans[0];
    if (demo_mode) demo_radio_set_chan(scan_ch);
    else if (hw && hw->set_chan) hw->set_chan(scan_ch);
    cur_chan = scan_ch;
    send_probe_req(scan_ch);
    scan_until = tick_count() + 120;
    set_err("");
    return 1;
}

int wifi_scan_done(void) { return state != WIFI_SCAN; }
int wifi_ap_count(void) { return ap_n; }
const char *wifi_ap_ssid(int i) { return (i >= 0 && i < ap_n) ? aps[i].ssid : ""; }
int wifi_ap_rssi(int i) {
    if (i < 0 || i >= ap_n) return 0;
    int r = aps[i].rssi;
    if (r > 100) r = 100;
    return r;
}
int wifi_ap_sec(int i) { return (i >= 0 && i < ap_n) ? aps[i].sec : 0; }
int wifi_ap_chan(int i) { return (i >= 0 && i < ap_n) ? aps[i].chan : 0; }

int wifi_connect(int ap_idx, const char *pass) {
    if (ap_idx < 0 || ap_idx >= ap_n) return 0;
    if (!wifi_hw_present()) return 0;
    kmemcpy_(bssid, aps[ap_idx].bssid, 6);
    cur_chan = aps[ap_idx].chan;
    int i = 0;
    for (; aps[ap_idx].ssid[i] && i < 32; i++) ssid_used[i] = aps[ap_idx].ssid[i];
    ssid_used[i] = 0;
    net_sec = aps[ap_idx].sec;
    int j = 0;
    for (; pass[j] && j < 63; j++) pass_used[j] = pass[j];
    pass_used[j] = 0;
    if (net_sec == 1 && j < 8) { set_err("jelszo legalabb 8 karakter"); return 0; }
    if (demo_mode) demo_radio_set_chan(cur_chan);
    else if (hw && hw->set_chan) { if (!hw->set_chan(cur_chan)) { set_err("csatornavaltas hiba"); return 0; } }
    kprintf("wifi: connect ssid=%s pass=%s\n", ssid_used, pass_used);
    if (net_sec == 1) pbkdf2_sha1(pass_used, ssid_used, 4096, pmk);
    u8 f[32]; u32 o = 0;
    f[o++] = 0xb0; f[o++] = 0x00;
    f[o++] = 0; f[o++] = 0;
    kmemcpy_(f + o, bcast, 6); o += 6;
    kmemcpy_(f + o, own_mac, 6); o += 6;
    kmemcpy_(f + o, bssid, 6); o += 6;
    f[o++] = (u8)((tx_seq << 4) & 0xf0);
    f[o++] = (u8)((tx_seq >> 4) & 0xff); tx_seq++;
    set_be16_(f + o, 0); o += 2;
    set_be16_(f + o, 1); o += 2;
    set_be16_(f + o, 0); o += 2;
    /* az állapotot a keretadás ELŐTT állítjuk: a demo/hardver válasza
     * szinkronban jön vissza a send_frame-en belül */
    state = WIFI_AUTH;
    state_until = tick_count() + 200;
    set_err("");
    send_frame(f, o, RATE_1M);
    return 1;
}

void wifi_disconnect(void) {
    if (state == WIFI_UP) {
        u8 f[32]; u32 o = 0;
        f[o++] = 0xc0; f[o++] = 0x00;
        f[o++] = 0; f[o++] = 0;
        kmemcpy_(f + o, bcast, 6); o += 6;
        kmemcpy_(f + o, own_mac, 6); o += 6;
        kmemcpy_(f + o, bssid, 6); o += 6;
        f[o++] = (u8)((tx_seq << 4) & 0xf0);
    f[o++] = (u8)((tx_seq >> 4) & 0xff); tx_seq++;
        set_be16_(f + o, 3); o += 2;
        send_frame(f, o, RATE_1M);
    }
    state = WIFI_OFF;
    gtk_valid = 0;
    if (net_dev == &wifi_dev) net_dev = 0;
}

int wifi_status(void) { return state; }
int wifi_connected(void) { return state == WIFI_UP; }
int wifi_need_dhcp(void) { return need_dhcp; }
void wifi_clear_need_dhcp(void) { need_dhcp = 0; }
const char *wifi_err(void) { return err_txt; }

const char *wifi_status_text(int lang) {
    static const char *hu[] = {"ki","szken","azonositas","tarsitas","WPA2 kezfogas","kapcsolodva"};
    static const char *en[] = {"off","scan","auth","assoc","WPA2 handshake","connected"};
    const char **t = lang ? en : hu;
    if (state < 0 || state > 5) return "?";
    return t[state];
}

/* ---------------- init ---------------- */
void wifi_hw_register(wifi_hw_t *h) { hw = h; }
void wifi_set_mac(const u8 *m) {
    kmemcpy_(own_mac, m, 6);
    kmemcpy_(wifi_dev.mac, m, 6);
}

void wifi_demo_enable(void) {
    if (demo_mode) return;
    demo_mode = 1;
    demo_radio_setup();
    for (int i = 0; i < 6; i++)
        if (!own_mac[i]) own_mac[i] = (u8)(0x4b + i * 0x11);
}
void wifi_demo_disable(void) { demo_mode = 0; }
int wifi_demo_active(void) { return demo_mode; }
const char *wifi_ssid(void) { return ssid_used; }
const u8 *wifi_bssid(void) { return bssid; }

int wifi_hw_present(void) { return demo_mode || (hw && hw->tx); }
const char *wifi_hw_name(void) {
    if (demo_mode) return "Demo";
    return (hw && hw->tx) ? "AR9285" : "-";
}

void wifi_init(void) {
    kmemset_((u8*)aps, 0, sizeof(aps));
    state = WIFI_OFF;
    wifi_dev.type = 3;
    kmemcpy_(wifi_dev.mac, own_mac, 6);
    wifi_dev.ip = 0; wifi_dev.mask = 0; wifi_dev.gw = 0; wifi_dev.dns = 0;
    wifi_dev.ip_ok = 0;
    wifi_dev.rx_pkts = 0; wifi_dev.tx_pkts = 0;
    wifi_dev.send = wifi_net_send;
    wifi_dev.poll = wifi_net_poll;
    wifi_dev.shutdown = wifi_net_shutdown;
}

/* ================= demo rádió =================
 * Két virtuális AP (1 nyílt + 1 WPA2-PSK). A demo AP oldal is a
 * valódi kripto-könyvtárat használja — ha bármi eltér, a kézfogás elhasal. */

static u8     demo_mac[2][6];
static const char *demo_ssid[2] = {"KeringoDemo", "Otthoni-WPA2"};
static const char *demo_pass[2] = {"", "keringo2026"};
static const int   demo_ap_sec[2] = {0, 1};
static int    demo_chan = 6;
static u8     demo_ap_pmk[2][32];
static u8     demo_ap_ptk[48];
static u8     demo_anonce[2][32];
static u8     demo_gtk[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                              0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00};
static u64    demo_pn = 100;
static u16    demo_seq = 100;
static int    demo_mic_fail = 0;

void demo_radio_setup(void) {
    for (int a = 0; a < 2; a++) {
        demo_mac[a][0] = 0x02; demo_mac[a][1] = 0x4b;
        demo_mac[a][2] = 0x45; demo_mac[a][3] = 0x52;
        demo_mac[a][4] = 0x01; demo_mac[a][5] = (u8)(0x01 + a);
    }
}

void demo_radio_set_chan(int c) { demo_chan = c; }

int aps_visible(int a) { (void)a; return demo_chan == 6; }

static void demo_send_beacon(int a, int proberesp) {
    u8 f[256]; u32 i = 0;
    u8 subt = proberesp ? 5 : 8;
    f[i++] = (u8)(subt << 4); f[i++] = 0x00;
    f[i++] = 0; f[i++] = 0;
    kmemcpy_(f + i, bcast, 6); i += 6;
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;
    f[i++] = (u8)((demo_seq << 4) & 0xf0);
    f[i++] = (u8)((demo_seq >> 4) & 0xff); demo_seq++;
    for (int t = 0; t < 8; t++) f[i++] = (u8)(tick_count() >> (t % 4));
    set_be16_(f + i, 100); i += 2;
    set_be16_(f + i, 0x0411); i += 2;
    u32 sl = kstrlen_(demo_ssid[a]);
    f[i++] = 0; f[i++] = (u8)sl;
    for (u32 s = 0; s < sl; s++) f[i++] = (u8)demo_ssid[a][s];
    f[i++] = 1; f[i++] = 4;
    f[i++] = 0x82; f[i++] = 0x84; f[i++] = 0x8b; f[i++] = 0x96;
    f[i++] = 3; f[i++] = 1; f[i++] = (u8)demo_chan;
    if (demo_ap_sec[a] == 1) {
        /* RSN IE: 2+20 bájt (a számlálók 2 bájtosak RSN-ben) */
        f[i++] = 48; f[i++] = 20;
        set_be16_(f + i, 1); i += 2;                    /* verzió */
        f[i++] = 0x00; f[i++] = 0x0f; f[i++] = 0xac; f[i++] = 0x04;  /* group CCMP */
        set_be16_(f + i, 1); i += 2;                    /* pairwise darab */
        f[i++] = 0x00; f[i++] = 0x0f; f[i++] = 0xac; f[i++] = 0x04;  /* CCMP */
        set_be16_(f + i, 1); i += 2;                    /* AKM darab */
        f[i++] = 0x00; f[i++] = 0x0f; f[i++] = 0xac; f[i++] = 0x02;  /* PSK */
        set_be16_(f + i, 0); i += 2;                    /* RSN caps */
    }
    wifi_rx_frame(f, i, 60, RATE_1M);
}

static void demo_send_auth_resp(const u8 *req_bssid) {
    int a = kmemeq_(req_bssid, demo_mac[1], 6) ? 1 : 0;
    u8 f[40]; u32 i = 0;
    f[i++] = 0xb0; f[i++] = 0x00;
    f[i++] = 0; f[i++] = 0;
    kmemcpy_(f + i, own_mac, 6); i += 6;
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;
    f[i++] = (u8)((demo_seq << 4) & 0xf0);
    f[i++] = (u8)((demo_seq >> 4) & 0xff); demo_seq++;
    set_be16_(f + i, 0); i += 2;
    set_be16_(f + i, 2); i += 2;
    set_be16_(f + i, 0); i += 2;
    wifi_rx_frame(f, i, 60, RATE_1M);
}

static int demo_pick_ap(const u8 *assocreq) {
    if (kmemeq_(assocreq + 16, demo_mac[1], 6)) return 1;
    return 0;
}

static void demo_send_assoc_resp(int a) {
    u8 f[40]; u32 i = 0;
    f[i++] = 0x10; f[i++] = 0x00;
    f[i++] = 0; f[i++] = 0;
    kmemcpy_(f + i, own_mac, 6); i += 6;
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;
    f[i++] = (u8)((demo_seq << 4) & 0xf0);
    f[i++] = (u8)((demo_seq >> 4) & 0xff); demo_seq++;
    set_be16_(f + i, 0x0411); i += 2;     /* capability */
    set_be16_(f + i, 0); i += 2;          /* status kód (802.11: cap,status,aid!) */
    set_be16_(f + i, 1 | 0xc000); i += 2; /* AID */
    wifi_rx_frame(f, i, 60, RATE_1M);
}

/* AP oldali EAPOL-adás (mindig titkosítatlan, mint a valódi AP-k) */
static void demo_send_eapol(int a, const u8 *eapol, u16 elen) {
    u8 f[FRAME_MAX]; u32 i = 0;
    u16 fc = FC_FROMDS | 0x0008;
    f[i++] = (u8)(fc & 0xff); f[i++] = (u8)(fc >> 8);
    f[i++] = 0; f[i++] = 0;
    kmemcpy_(f + i, own_mac, 6); i += 6;       /* A1 = DA (STA) */
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;   /* A2 = BSSID */
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;   /* A3 = SA = az AP! */
    f[i++] = (u8)((demo_seq << 4) & 0xf0);
    f[i++] = (u8)((demo_seq >> 4) & 0xff); demo_seq++;
    f[i++] = 0xAA; f[i++] = 0xAA; f[i++] = 0x03;
    f[i++] = 0; f[i++] = 0; f[i++] = 0;
    f[i++] = 0x88; f[i++] = 0x8e;
    kmemcpy_(f + i, eapol, elen); i += elen;
    wifi_rx_frame(f, i, 60, RATE_1M);
}

static u8 demo_replay[2][8];

static void demo_start_eapol(int a) {
    if (demo_ap_sec[a] == 0) return;           /* nyílt: nincs 4-way */
    kprintf("wifi-demo: pmk input ssid=%s pass=%s\n", demo_ssid[a], demo_pass[a]);
    if (kstrlen_(demo_pass[a]) >= 8)
        pbkdf2_sha1(demo_pass[a], demo_ssid[a], 4096, demo_ap_pmk[a]);
    u32 t = tick_count();
    for (int i = 0; i < 32; i++)
        demo_anonce[a][i] = (u8)(t * 31 + i * 7 + a * 13);
    for (int i = 0; i < 8; i++) demo_replay[a][i] = (u8)(demo_seq + i);
    u8 e[128];
    int n = build_eapol_key(e, KI_VER_SHA1_AES | KI_ACK | KI_PAIRWISE,
                            16, demo_replay[a], demo_anonce[a], 0, 0, 0);
    demo_send_eapol(a, e, (u16)n);
}

/* AP oldali EAPOL feldolgozás (M2, M4) */
static void demo_ap_handle_eapol(int a, const u8 *payload, u32 paylen) {
    eapolkey_t k;
    if (!parse_eapol_key(payload, paylen, &k)) return;
    if (k.type != 2) return;

    if ((k.key_info & KI_MIC) && !(k.key_info & KI_SECURE)) {
        /* M2 */
        u8 kd[76];
        minmax_copy(kd, demo_mac[a], own_mac, 6);
        maxmin_copy(kd + 6, demo_mac[a], own_mac, 6);
        minmax_copy(kd + 12, demo_anonce[a], k.nonce, 32);
        maxmin_copy(kd + 44, demo_anonce[a], k.nonce, 32);
        prf512(demo_ap_pmk[a], 32, "Pairwise key expansion", kd, 76, demo_ap_ptk, 48);
        u8 f[128];
        int n = build_eapol_key(f, k.key_info, k.key_len, k.replay, k.nonce, 0, 0, 0);
        u8 calc[20];
        hmac_sha1(demo_ap_ptk, 16, f, n, calc);
        int ok = 1;
        for (int i = 0; i < 16; i++) if (k.mic[i] != calc[i]) ok = 0;
        if (!ok) {
            kprintf("wifi-demo: kapott MIC=");
            for (int i2 = 0; i2 < 8; i2++) kprintf("%x ", k.mic[i2] >> 4);
            kprintf(" szamolt=");
            for (int i2 = 0; i2 < 8; i2++) kprintf("%x ", calc[i2] >> 4);
            kprintf("\n");
            demo_mic_fail++; set_err("M2 MIC nem egyezik (rossz jelszo)"); return;
        }
        kprintf("wifi-demo: M2 MIC rendben, M3 megy\n");
        /* M3: GTK KDE (DD len OUI type kid rsv GTK16) */
        u8 kd3[24];
        kd3[0] = 0xdd; kd3[1] = 22;
        kd3[2] = 0x00; kd3[3] = 0x0f; kd3[4] = 0xac; kd3[5] = 0x01;
        kd3[6] = 0x00;                          /* kid 0 */
        kd3[7] = 0x00;                          /* foglalt */
        kmemcpy_(kd3 + 8, demo_gtk, 16);
        u8 m3[180];
        int n3 = build_eapol_key(m3, KI_VER_SHA1_AES | KI_MIC | KI_SECURE |
                                    KI_ACK | KI_INSTALL | KI_PAIRWISE,
                                 16, k.replay, demo_anonce[a], 0, kd3, 24);
        u8 mic3[20];
        hmac_sha1(demo_ap_ptk, 16, m3, n3, mic3);
        n3 = build_eapol_key(m3, KI_VER_SHA1_AES | KI_MIC | KI_SECURE |
                                 KI_ACK | KI_INSTALL | KI_PAIRWISE,
                             16, k.replay, demo_anonce[a], mic3, kd3, 24);
        demo_send_eapol(a, m3, (u16)n3);
        return;
    }
    if ((k.key_info & KI_MIC) && (k.key_info & KI_SECURE)) {
        /* M4: kész */
        kprintf("wifi-demo: M4 rendben, AP oldali link kesz\n");
        u8 f[128];
        int n = build_eapol_key(f, k.key_info, k.key_len, k.replay, k.nonce, 0, 0, 0);
        u8 calc[20];
        hmac_sha1(demo_ap_ptk, 16, f, n, calc);
        int ok = 1;
        for (int i = 0; i < 16; i++) if (k.mic[i] != calc[i]) ok = 0;
        if (!ok) { demo_mic_fail++; return; }
    }
}

/* AP oldali ethernet-keret adás (ARP/DHCP válaszok), CCMP-vel WPA2-nél */
static void demo_tx_eth(int a, const u8 *eth, u32 elen) {
    static u8 f[FRAME_MAX];                    /* TX szinkron, újrabiztonságos */
    u32 i = 0;
    int enc = (demo_ap_sec[a] == 1) && demo_ap_ptk[0];
    int group = (eth[0] & 1) != 0;             /* DA bcast/mcast → GTK */
    const u8 *xkey = group ? demo_gtk : (demo_ap_ptk + 32);
    u16 fc = FC_FROMDS | 0x0008;
    if (enc) fc |= FC_PROT;
    f[i++] = (u8)(fc & 0xff); f[i++] = (u8)(fc >> 8);
    f[i++] = 0; f[i++] = 0;
    kmemcpy_(f + i, eth, 6); i += 6;           /* A1 = DA */
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;   /* A2 = BSSID */
    kmemcpy_(f + i, demo_mac[a], 6); i += 6;   /* A3 = SA */
    f[i++] = (u8)((demo_seq << 4) & 0xf0);
    f[i++] = (u8)((demo_seq >> 4) & 0xff); demo_seq++;
    /* sorrend: 802.11 fejléc → [CCMP fejléc] → payload → [MIC] */
    u64 pn = demo_pn++;
    if (enc) {
        f[i++] = (u8)pn; f[i++] = (u8)(pn >> 8);
        f[i++] = 0x20;
        f[i++] = (u8)(pn >> 16); f[i++] = (u8)(pn >> 24);
        f[i++] = (u8)(pn >> 32); f[i++] = (u8)(pn >> 40);
        f[i++] = 0;
    }
    u32 pi = i;
    f[pi++] = 0xAA; f[pi++] = 0xAA; f[pi++] = 0x03;
    f[pi++] = 0; f[pi++] = 0; f[pi++] = 0;
    f[pi++] = eth[12]; f[pi++] = eth[13];
    kmemcpy_(f + pi, eth + 14, elen - 14);
    pi += elen - 14;
    if (enc) {
        u8 nonce[13], aad[16];
        ccmp_nonce(nonce, demo_mac[a], pn);
        ccmp_aad(aad, fc, eth, demo_mac[a], 0);
        ccmp_encrypt(xkey, nonce, aad, 16, f + i, pi - i, f + i);    /* in-place a payloadon */
        i = pi + 8;                            /* payload vege + MIC */
    } else {
        i = pi;
    }
    wifi_rx_frame(f, i, 60, RATE_6M);
}

static void demo_handle_arp(int a, const u8 *arp, u32 alen) {
    if (alen < 28) return;
    u16 op = be16_(arp + 6);
    if (op != 1) return;
    u32 tip = be32_(arp + 24);
    if (tip != 0xC0A80D01u) return;            /* 192.168.13.1 */
    u8 eth[64];
    kmemcpy_(eth, arp + 8, 6);                 /* küldő MAC */
    kmemcpy_(eth + 6, demo_mac[a], 6);
    eth[12] = 0x08; eth[13] = 0x06;
    u8 *r = eth + 14;
    set_be16_(r, 1); set_be16_(r + 2, 0x0800);
    r[4] = 6; r[5] = 4;
    set_be16_(r + 6, 2);
    kmemcpy_(r + 8, demo_mac[a], 6);
    set_be32_(r + 14, tip);
    kmemcpy_(r + 18, arp + 8, 6);
    set_be32_(r + 24, be32_(arp + 14));
    demo_tx_eth(a, eth, 42);
}

static void demo_handle_ip(int a, const u8 *ip, u32 ilen) {
    u32 iplen = be16_(ip + 2);
    const u8 *uh = ip + 20;
    u16 dport = be16_(uh + 2);
    const u8 *d = uh + 8;
    u32 dlen = be16_(uh + 4) - 8;
    if ((ip[0] >> 4) != 4) { kprintf("g1 kilep\n"); return; }
    if (ip[9] != 17)     return;
    if (iplen < 28 || iplen > ilen)     return;
    if (dport != 67)     return;
    if (dlen < 244)     return;
    if (*(const u32 *)(d + 236) != 0x63538263u)     return;
    u8 msg = 0;
    u32 o = 240;
    u32 reqip = 0;
    while (o + 1 < dlen) {
        u8 c = d[o++];
        if (c == 255) break;
        u8 l = d[o++];
        if (c == 53) msg = d[o];
        else if (c == 50 && l >= 4) reqip = be32_(d + o);
        o += l;
    }
    if (msg != 1 && msg != 3) return;
    u8 rtype = (msg == 1) ? 2 : 5;             /* discover->OFFER, request->ACK */
    u8 eth[400];
    kmemset_(eth, 0xff, 6);
    kmemcpy_(eth + 6, demo_mac[a], 6);
    eth[12] = 0x08; eth[13] = 0x00;
    u8 *ip2 = eth + 14;
    ip2[0] = 0x45; ip2[1] = 0;
    u32 ilen2 = 20 + 8 + 300;
    set_be16_(ip2 + 2, (u16)ilen2);
    ip2[8] = 64; ip2[9] = 17;
    set_be32_(ip2 + 12, 0xC0A80D01u);
    set_be32_(ip2 + 16, 0xFFFFFFFFu);
    set_be16_(ip2 + 10, 0);
    u8 *uh2 = ip2 + 20;
    set_be16_(uh2, 67); set_be16_(uh2 + 2, 68);
    set_be16_(uh2 + 4, 8 + 300);
    set_be16_(uh2 + 6, 0);
    u8 *d2 = uh2 + 8;
    kmemset_(d2, 0, 300);
    d2[0] = 2; d2[1] = 1; d2[2] = 6; d2[3] = 0;
    for (int i = 0; i < 6; i++) d2[28 + i] = own_mac[i];
    set_be32_(d2 + 16, msg == 1 ? 0xC0A80D0Fu : (reqip ? reqip : 0xC0A80D0Fu));
    set_be32_(d2 + 20, 0xC0A80D01u);
    d2[236] = 0x63; d2[237] = 0x82; d2[238] = 0x53; d2[239] = 0x63;
    u32 po = 240;
    d2[po++] = 53; d2[po++] = 1; d2[po++] = rtype;
    d2[po++] = 1; d2[po++] = 4; set_be32_(d2 + po, 0xFFFFFF00u); po += 4;
    d2[po++] = 3; d2[po++] = 4; set_be32_(d2 + po, 0xC0A80D01u); po += 4;
    d2[po++] = 6; d2[po++] = 4; set_be32_(d2 + po, 0xC0A80D01u); po += 4;
    d2[po++] = 54; d2[po++] = 4; set_be32_(d2 + po, 0xC0A80D01u); po += 4;
    d2[po++] = 51; d2[po++] = 4; set_be32_(d2 + po, 86400); po += 4;
    d2[po++] = 255;
    demo_tx_eth(a, eth, 14 + ilen2);
}

static void demo_handle_data_from_sta(const u8 *f, u32 len) {
    u16 fc = (u16)(f[0] | (f[1] << 8));
    const u8 *da = f + 4, *bss = f + 10, *sa = f + 16;
    int a = kmemeq_(da, demo_mac[1], 6) ? 1 : 0;   /* toDS: A1 = BSSID */
    u32 hl = 24;
    if (len < hl + 8) return;
    const u8 *pl = f + hl;
    u32 plen = len - hl;
    u8 dec[FRAME_MAX]; u32 dlen = 0;

    if (fc & FC_PROT) {
        if (plen < 16) return;
        u64 pn = (u64)pl[0] | ((u64)pl[1] << 8) | ((u64)pl[3] << 16) |
                 ((u64)pl[4] << 24) | ((u64)pl[5] << 32) | ((u64)pl[6] << 40);
        u8 nonce[13], aad[16];
        ccmp_nonce(nonce, bss, pn);
        ccmp_aad(aad, fc, da, bss, (u16)(f[22] | (f[23] << 8)));
        /* toDS keretben A3 = DA: csoport-címre a GTK jár */
        const u8 *xkey = (sa[0] & 1) ? demo_gtk : (demo_ap_ptk + 32);
        if (!ccmp_decrypt(xkey, nonce, aad, 16, pl + 8, plen - 8, dec)) {
            demo_mic_fail++;
            return;
        }
        dlen = plen - 8;
    } else {
        kmemcpy_(dec, pl, plen);
        dlen = plen;
    }
    if (dlen < 10 || dec[0]!=0xAA || dec[1]!=0xAA || dec[2]!=0x03) return;
    u16 etype = (u16)((dec[6] << 8) | dec[7]);
    const u8 *payload = dec + 8;
    u32 paylen = dlen - 8;
    if (etype == 0x888e) { demo_ap_handle_eapol(a, payload, paylen); return; }
    if (etype == 0x0806) { demo_handle_arp(a, payload, paylen); return; }
    if (etype == 0x0800 && paylen >= 20) { demo_handle_ip(a, payload, paylen); return; }
}

int wifi_demo_tx_wrap(const u8 *f, u32 len) {
    u16 fc = (u16)(f[0] | (f[1] << 8));
    if ((fc & 0x000c) == FC_TYPE_MGMT) {
        u8 subt = (fc >> 4) & 0xf;
        if (subt == 4) {
            for (int a = 0; a < 2; a++)
                if (aps_visible(a)) demo_send_beacon(a, 1);
        } else if (subt == 11) {
            kprintf("wifi-demo: auth kerv\n");
            demo_send_auth_resp(f + 16);      /* A3 = kért BSSID */
        } else if (subt == 0) {
            int a = demo_pick_ap(f);
            kprintf("wifi-demo: assoc kerv, ap=%d\n", a);
            demo_send_assoc_resp(a);
            demo_start_eapol(a);
        } else if (subt == 12) {
            demo_ap_ptk[0] = 0;
        }
        return 1;
    }
    demo_handle_data_from_sta(f, len);
    return 1;
}

void demo_radio_tick(void) {
    static u32 next_bcn = 0;
    if (state == WIFI_SCAN && demo_chan == 6 &&
        (s32)(tick_count() - next_bcn) >= 0) {
        demo_send_beacon(0, 0);
        demo_send_beacon(1, 0);
        next_bcn = tick_count() + 8;
    }
}
