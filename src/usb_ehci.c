/* usb_ehci.c — EHCI (USB 2.0 nagy-sebesség) hoszt-vezérlő meghajtó.
 * Aszinkron menetrend egyetlen önmagára mutató QH-val, qTD-láncokkal.
 * A bitek a Linux ehci_def.h / ehci.h alapján. */
#include "kernel.h"
#include "usb.h"

/* ---- képesség-regiszterek ---- */
#define E_CAPLEN      0x00
#define E_HCSPARAMS   0x04
#define E_HCCPARAMS   0x08
/* ---- működési regiszterek (CAPLEN-től) ---- */
#define E_USBCMD      0x00
#define E_USBSTS      0x08
#define E_USBINTR     0x10
#define E_FRINDEX     0x0C
#define E_ASYNCLIST   0x18
#define E_TXFILL      0x24
#define E_CONFIGFLAG  0x40
#define E_PORTSC(n)   (0x44 + (n) * 4)

#define ECMD_RUN      (1u << 0)
#define ECMD_RESET    (1u << 1)
#define ECMD_ASE      (1u << 5)
#define ESTS_ASS      (1u << 15)
#define ESTS_HLT      (1u << 12)

#define EP_CONN       (1u << 0)
#define EP_CSC        (1u << 1)
#define EP_PE         (1u << 2)
#define EP_PEC        (1u << 3)
#define EP_OCC        (1u << 5)
#define EP_RESET      (1u << 8)
#define EP_LS_MASK    (3u << 10)
#define EP_POWER      (1u << 12)
#define EP_RWC        (EP_CSC | EP_PEC | EP_OCC)

/* ---- qTD ---- */
typedef struct {
    u32 next, altnext, token;
    u32 buf[5];
} __attribute__((aligned(32))) ehci_qtd_t;

#define QT_TOGGLE     (1u << 31)
#define QT_IOC        (1u << 15)
#define QT_ACTIVE     (1u << 7)
#define QT_HALT       (1u << 6)
#define QT_DBE        (1u << 5)
#define QT_BABBLE     (1u << 4)
#define QT_XACT       (1u << 3)
#define QT_ERR        (QT_HALT | QT_DBE | QT_BABBLE | QT_XACT)
#define QT_CERR(x)    ((u32)(x) << 10)
#define QT_PID_OUT    0
#define QT_PID_IN     1
#define QT_PID_SETUP  2
#define QT_LEN(t)     (int)(((t) >> 16) & 0x7FFF)

/* ---- QH (17 dword = 68 bájt, 32-ig igazítva) ---- */
typedef struct {
    u32 next;          /* bit1 = QH típus, bit0 = T */
    u32 info1;         /* addr | ep<<8 | speed<<12 | DTC<<14 | H<<15 | maxp<<16 | C<<27 */
    u32 info2;         /* MULT = 1<<30 */
    u32 current;       /* aktuális qTD vagy 1 (vége) */
    u32 ovl_next, ovl_altnext, ovl_token;
    u32 ovl_buf[5];
    u32 pad[5];
} __attribute__((aligned(32))) ehci_qh_t;

#define QH_DTC        (1u << 14)
#define QH_HEAD       (1u << 15)
#define QH_HS         (2u << 12)
#define QH_CTRLEP     (1u << 27)
#define QH_MULT1      (1u << 30)

/* ---- statikus tárolók ---- */
static ehci_qh_t e_qh __attribute__((aligned(32)));
static ehci_qtd_t e_qtds[64] __attribute__((aligned(32)));
static usb_hcd_t ehci_bus;
static u32 e_mmio = 0;
static int ehci_active = 0;
static int e_qtd_used = 0;
static u8 e_opbase = 0x10;

static inline u32 erd(u32 off)  { return *(volatile u32 *)(e_mmio + e_opbase + off); }
static inline void ewr(u32 off, u32 v) { *(volatile u32 *)(e_mmio + e_opbase + off) = v; }

static ehci_qtd_t *eq_get(void) {
    if (e_qtd_used >= 64) return 0;
    return &e_qtds[e_qtd_used++];
}

static void eq_fill(ehci_qtd_t *q, u32 next, int pid, u8 tog, int len, u32 buf) {
    q->next = next;
    q->altnext = 1;                        /* vége (nincs alternatíva) */
    q->token = QT_ACTIVE | QT_CERR(3)
             | ((u32)pid << 8)
             | (tog ? QT_TOGGLE : 0)
             | (((u32)len & 0x7FFF) << 16);
    q->buf[0] = buf;
    q->buf[1] = (buf + 4096) & 0xFFFFF000u;  /* következő lap, ha átlógna */
    q->buf[2] = 0;
    q->buf[3] = 0;
    q->buf[4] = 0;
}

/* ---- QH konfiguráció átvitelhez ---- */
static void ehci_setup_qh(u8 addr, u8 ep, int speed, u16 maxpkt, int control) {
    e_qh.info1 = ((u32)addr & 0x7F)
               | (((u32)ep & 0xF) << 8)
               | (speed == USB_SPEED_HS ? QH_HS : 0)
               | QH_DTC                     /* adat-toggle a qTD-ből */
               | QH_HEAD
               | (((u32)maxpkt & 0x3FF) << 16)
               | (control ? QH_CTRLEP : 0);
    e_qh.info2 = QH_MULT1;
    e_qh.current = 1;
    e_qh.ovl_next = 1;
    e_qh.ovl_altnext = 1;
    e_qh.ovl_token = 0;                    /* inaktív */
}

/* qTD-lánc indítása: QH overlay-be írjuk az elsőt */
static int ehci_run(ehci_qtd_t *first, ehci_qtd_t *last, int timeout_ms) {
    (void)last;
    /* overlay aktíválása */
    e_qh.ovl_next = (u32)first;
    e_qh.ovl_altnext = 1;
    e_qh.ovl_token = QT_ACTIVE | QT_CERR(0);
    e_qh.current = 1;

    u32 until = tick_count() + (u32)timeout_ms / 10 + 1;
    for (;;) {
        /* lánc vizsgálata */
        int all_done = 1, total = 0, err = 0;
        for (ehci_qtd_t *q = first; ; q++) {
            u32 t = q->token;
            if (t & QT_ACTIVE) { all_done = 0; break; }
            if (t & QT_ERR) { err = 1; break; }
            total += QT_LEN(t);
            if (q == last) break;
        }
        if (err) { kprintf("ehci: qtd hiba tok=%x\n", last->token); e_qtd_used = 0; return -3; }
        if (all_done) return total;
        if ((s32)(tick_count() - until) >= 0) { kprintf("ehci: idotullepes\n"); e_qtd_used = 0; return -1; }
        hlt();
    }
}

/* ---- vezérlőátvitel ---- */
static int ehci_control(usb_hcd_t *bus, u8 addr, int speed, int ep0max,
                        const usb_dev_req_t *req, void *data, int datalen) {
    (void)bus;
    if (!ehci_active) return -9;
    ehci_qtd_t *first = 0, *last = 0;
    ehci_setup_qh(addr, 0, speed, ep0max, 1);

    /* SETUP (DATA0) */
    ehci_qtd_t *qs = eq_get(); first = qs; last = qs;
    eq_fill(qs, 1, QT_PID_SETUP, 0, 8, (u32)req);
    /* ADAT (DATA1-től) */
    u8 tog = 1;
    int done = 0;
    while (done < datalen) {
        int chunk = datalen - done; if (chunk > 4096) chunk = 4096;
        ehci_qtd_t *q = eq_get(); if (!q) { e_qtd_used = 0; return -8; }
        last->next = (u32)q;
        last = q;
        eq_fill(q, 1, (req->bmRequestType & 0x80) ? QT_PID_IN : QT_PID_OUT,
                tog, chunk, (u32)((u8 *)data + done));
        tog ^= 1;
        done += chunk;
    }
    /* STATUS (DATA1, ellentétes irány) */
    ehci_qtd_t *qt = eq_get();
    last->next = (u32)qt;
    last = qt;
    eq_fill(qt, 1, (req->bmRequestType & 0x80) ? QT_PID_OUT : QT_PID_IN, 1, 0, 0);

    int r = ehci_run(first, last, 2000);
    e_qtd_used = 0;
    return r;
}

/* ---- bulk átvitel (4 KB-os qTD-k) ---- */
static int ehci_bulk(usb_hcd_t *bus, u8 addr, u8 ep, int dir_in, int maxpkt,
                     void *data, int len, u8 *toggle) {
    (void)bus;
    if (!ehci_active) return -9;
    if (len <= 0) return 0;
    if (maxpkt < 8 || maxpkt > 1024) maxpkt = 512;
    if (len > 60 * 4096) len = 60 * 4096;
    ehci_setup_qh(addr, ep & 15, USB_SPEED_HS, 512, 0);

    ehci_qtd_t *first = 0, *last = 0;
    u8 tog = *toggle;
    int done = 0;
    while (done < len) {
        int chunk = len - done; if (chunk > 4096) chunk = 4096;
        ehci_qtd_t *q = eq_get(); if (!q) { e_qtd_used = 0; return -8; }
        if (last) last->next = (u32)q;
        first = first ? first : q;
        last = q;
        eq_fill(q, 1, dir_in ? QT_PID_IN : QT_PID_OUT, tog, chunk,
                (u32)((u8 *)data + done));
        /* toggle csomagonként vált, egy qTD-ben a HW maga váltogatja */
        tog ^= (((u32)chunk + maxpkt - 1) / maxpkt) & 1;
        done += chunk;
    }
    int r = ehci_run(first, last, 8000);
    e_qtd_used = 0;
    if (r > 0) *toggle = tog;
    return r;
}

/* ---- port reset ---- */
static int ehci_port_reset(int port, int ppc) {
    u32 reg = E_PORTSC(port);
    ewr(reg, erd(reg) | EP_RWC);           /* change bitek törlése */
    usb_msleep(5);
    u32 st = erd(reg);
    if (!(st & EP_CONN)) return 0;
    if (ppc) ewr(reg, EP_POWER);           /* táp be (ha kell) */
    usb_msleep(5);
    /* reset: az EHCI magától törli a bitet ~50-100 ms után */
    ewr(reg, EP_POWER | EP_RESET);
    int waited = 0;
    while (erd(reg) & EP_RESET) {
        usb_msleep(10);
        if (++waited > 12) { ewr(reg, EP_POWER); break; }   /* kézi törlés tartalék */
    }
    usb_msleep(25);                        /* helyreállási idő */
    ewr(reg, erd(reg) | EP_RWC);
    usb_msleep(5);
    st = erd(reg);
    if (!(st & EP_CONN)) return 0;
    if (((st & EP_LS_MASK) == (1u << 10))) {
        kprintf("ehci: p%d nem nagy-sebseges (TT kellene) - atugorva\n", port);
        return 0;
    }
    if (!(st & EP_PE)) { kprintf("ehci: p%d nem engedelyezett\n", port); return 0; }
    return 1;
}

static void ehci_shutdown(usb_hcd_t *bus) {
    (void)bus;
    if (!ehci_active) return;
    ewr(E_USBCMD, 0);
    usb_msleep(20);
    ewr(E_USBCMD, ECMD_RESET);
    usb_msleep(50);
    ewr(E_CONFIGFLAG, 0);
    ehci_active = 0;
}

int ehci_add_controller(pci_dev_t pcidev) {
    if (ehci_active) return 0;
    u32 mmio = pci_bar_mmio(pcidev, 0);
    if (!mmio) { kprintf("ehci: nincs MMIO BAR\n"); return 0; }
    e_mmio = (u32)mem_map_mmio(mmio, 0x1000);
    if (!e_mmio) { kprintf("ehci: MMIO lekerpezes nem sikerult\n"); return 0; }
    e_opbase = (u8)(*(volatile u32 *)e_mmio & 0xFF);   /* CAPLENGTH a bázison */
    u32 hcs = erd(E_HCSPARAMS);
    u32 hcc = erd(E_HCCPARAMS);
    int nports = hcs & 0xF;
    int ppc = (hcs >> 4) & 1;
    kprintf("ehci: MMIO=%x op=%x portok=%d ppc=%d\n", mmio, e_opbase, nports, ppc);

    /* BIOS legacy átadás EECP-n keresztül */
    u32 eecp = (hcc >> 8) & 0xFF;
    if (eecp) {
        u32 leg = pci_read32(pcidev, eecp);
        if (leg & 0x00010000u) {
            pci_write32(pcidev, eecp, leg | 0x01000000u);
            for (int i = 0; i < 60; i++) {
                if (!(pci_read32(pcidev, eecp) & 0x00010000u)) break;
                usb_msleep(5);
            }
            kprintf("ehci: BIOS legacy atadva (eecp=%x)\n", eecp);
        }
        pci_write32(pcidev, eecp + 4, 0);  /* SMI kikapcsolása */
    }

    pci_enable_busmaster(pcidev);

    /* HC reset */
    ewr(E_USBCMD, ECMD_RESET);
    for (int i = 0; i < 130; i++) {
        if (!(erd(E_USBCMD) & ECMD_RESET)) break;
        usb_msleep(2);
    }
    usb_msleep(10);
    /* üres aszinkron gyűrű: QH önmagára mutat */
    for (u32 *p = (u32 *)&e_qh; p < (u32 *)&e_qh + 17; p++) *p = 0;
    e_qh.next = ((u32)&e_qh) | (1u << 1);                 /* QH-típus, érvényes */
    e_qh.info1 = QH_HEAD | QH_HS | QH_DTC | (64u << 16);
    e_qh.info2 = QH_MULT1;
    e_qh.current = 1;
    e_qh.ovl_next = 1;
    e_qh.ovl_altnext = 1;
    e_qh.ovl_token = 0;
    e_qtd_used = 0;

    ewr(E_CONFIGFLAG, 1);                  /* routing EHCI-hoz */
    if (ppc) for (int p = 0; p < nports; p++) ewr(E_PORTSC(p), EP_POWER);
    usb_msleep(30);
    ewr(E_TXFILL, 8u << 16);
    ewr(E_ASYNCLIST, (u32)&e_qh);
    ewr(E_USBINTR, 0);
    ewr(E_USBCMD, ECMD_RUN | ECMD_ASE);
    /* várjuk fel, amíg az aszinkron menetrend fut */
    for (int i = 0; i < 100; i++)
        if (erd(E_USBSTS) & ESTS_ASS) break;
        else usb_msleep(2);
    ehci_active = 1;

    ehci_bus.type = 2;
    ehci_bus.pcidev = pcidev;
    ehci_bus.mmio = mmio;
    ehci_bus.nports = nports;
    ehci_bus.control = ehci_control;
    ehci_bus.bulk = ehci_bulk;
    ehci_bus.shutdown = ehci_shutdown;

    kprintf("ehci: vezérlo fut\n");
    usb_msleep(200);                       /* eszközök beállása */
    for (int p = 0; p < nports; p++) {
        u32 st = erd(E_PORTSC(p));
        if (st & EP_CONN) {
            kprintf("ehci: p%d eszkoz csatlakozik\n", p);
            if (ehci_port_reset(p, ppc))
                usb_enumerate_port(&ehci_bus, p, USB_SPEED_HS);
        }
    }
    return 1;
}
