/* usb_uhci.c — UHCI (USB 1.1) hoszt-vezérlő meghajtó: teljes sebességű eszközök
 * (QEMU alapértelmezett piix3-uhci). TD-lánc + egyetlen aszinkron QH.
 * A bitek a Linux uhci-hcd.h alapján (ez a specifikációval azonos). */
#include "kernel.h"
#include "usb.h"

/* ---- regiszterek (I/O, 16 bites) ---- */
#define U_USBCMD      0x00
#define U_USBSTS      0x02
#define U_USBINTR     0x04
#define U_FRNUM       0x06
#define U_FLBASEADD   0x08
#define U_PORTSC(n)   (0x10 + (n) * 2)

#define U_CMD_RS      0x0001
#define U_CMD_RESET   0x0002
#define U_CMD_MAXP64  0x0080
#define U_STS_HCH     0x0020

#define U_P_CCS       0x0001
#define U_P_CSC       0x0002
#define U_P_PE        0x0004
#define U_P_PEC       0x0008
#define U_P_LSDA      0x0100
#define U_P_PR        0x0200
#define U_P_SUSP      0x1000
#define U_P_RWC       (U_P_CSC | U_P_PEC | 0x0800)   /* CSC+PEC+OCC törlés */

/* ---- TD/QH hardver-struktúrák ---- */
typedef struct { u32 link, status, token, buffer; } uhci_td_t;
typedef struct { u32 link, element; } uhci_qh_t;

/* TD státusz bitek */
#define TD_ACTIVE     (1u << 23)
#define TD_STALLED    (1u << 22)
#define TD_DBUFERR    (1u << 21)
#define TD_BABBLE     (1u << 20)
#define TD_NAK        (1u << 19)
#define TD_CRCTIMO    (1u << 18)
#define TD_BITSTUFF   (1u << 17)
#define TD_CERR(x)    ((u32)(x) << 27)
#define TD_ERRMASK    (TD_STALLED|TD_DBUFERR|TD_BABBLE|TD_CRCTIMO|TD_BITSTUFF)
#define TD_ACTLEN(st) (((st) + 1) & 0x7FF)
#define TD_EXPLEN(l)  ((((u32)(l) - 1) & 0x7FF) << 21)

/* PID-ek a token alsó bájtjában */
#define UHCI_PID_SETUP 0x2D
#define UHCI_PID_IN    0x69
#define UHCI_PID_OUT   0xE1

#define UHCI_TOKEN(pid, addr, ep, tog, len) \
    ((u32)(pid) | ((u32)(addr) << 8) | ((u32)(ep) << 15) | \
     ((u32)(tog) << 19) | TD_EXPLEN(len))

/* ---- statikus tárolók (egy vezérlőhöz) ---- */
static uhci_td_t u_tds[128] __attribute__((aligned(16)));
static uhci_qh_t u_qh  __attribute__((aligned(16)));
static u32 u_framelist[1024] __attribute__((aligned(4096)));
static usb_hcd_t uhci_bus;
static int uhci_active = 0;
static u16 uhci_io = 0;
static int td_used = 0;

static inline u16 urd(int reg)  { return inw(uhci_io + reg); }
static inline void uwr(int reg, u16 v) { outw(uhci_io + reg, v); }

void usb_msleep(int ms) {
    u32 until = tick_count() + (u32)ms / 10 + 1;
    while ((s32)(tick_count() - until) < 0) hlt();
}

/* ---------- HC leállítás/indítása TD-csere körül ---------- */
static void uhci_stop(void) {
    uwr(U_USBCMD, urd(U_USBCMD) & ~U_CMD_RS);
    for (int i = 0; i < 2000; i++)
        if (urd(U_USBSTS) & U_STS_HCH) return;
}
static void uhci_start(void) {
    uwr(U_USBCMD, urd(U_USBCMD) | U_CMD_RS);
}

/* ---------- TD készítés ---------- */
static uhci_td_t *td_get(void) {
    if (td_used >= 128) return 0;
    return &u_tds[td_used++];
}

static void td_fill(uhci_td_t *td, u32 link, u8 pid, u8 addr, u8 ep,
                    u8 tog, int len, u32 buf, int ls) {
    td->link = link;
    td->status = TD_ACTIVE | TD_CERR(3) | (ls ? (1u << 26) : 0);
    td->token = UHCI_TOKEN(pid, addr, ep, tog, len);
    td->buffer = buf;
}

/* Várakozás: a teljes lánc befejezésére. Vissza: átvitt bájt vagy <0.
 * A lánc MINDEN TD-jét vizsgálja (az első hibás TD miatt a többit nem futtatja a HC). */
static int uhci_wait_chain(uhci_td_t *first, uhci_td_t *last, int timeout_ms) {
    u32 until = tick_count() + (u32)timeout_ms / 10 + 1;
    for (;;) {
        /* aktív-e még valamelyik? */
        int all_done = 1;
        int total = 0;
        for (uhci_td_t *t = first; ; t++) {
            u32 st = t->status;
            if (st & TD_ACTIVE) { all_done = 0; break; }
            if (st & TD_STALLED) { kprintf("uhci: STALL\n"); td_used = 0; return -2; }
            if (st & TD_ERRMASK) { kprintf("uhci: td hiba st=%x\n", st); td_used = 0; return -3; }
            int len = (int)((t->token >> 21) & 0x7FF) + 1;
            int act = TD_ACTLEN(st);
            total += act < len ? act : len;
            if (t == last) break;
        }
        if (all_done) return total;
        if ((s32)(tick_count() - until) >= 0) { kprintf("uhci: idotullepes\n"); td_used = 0; return -1; }
        hlt();
    }
}

/* TD-lánc futtatása a QH-on keresztül. Vissza: átvitt bájt vagy <0. */
static int uhci_run(uhci_td_t *first, uhci_td_t *last, int timeout_ms) {
    uhci_stop();
    u_qh.element = (u32)first;            /* TD-re mutat (bit0=0, bit1=0) */
    uwr(U_USBSTS, 0xFFFF);                /* státuszok törlése */
    uhci_start();
    return uhci_wait_chain(first, last, timeout_ms);
    /* megj.: a QH element-t a következő hívás felülírja leállítás után */
}

/* ---------- vezérlőátvitel ---------- */
static int uhci_control(usb_hcd_t *bus, u8 addr, int speed, int ep0max,
                        const usb_dev_req_t *req, void *data, int datalen) {
    (void)bus;
    if (!uhci_active) return -9;
    int ls = (speed == USB_SPEED_LS);
    int tdd = datalen > 0 ? (datalen + 1023) / 1024 : 0;
    if (td_used + tdd + 2 > 128) return -8;

    uhci_td_t *tds[6];
    int n = 0;
    /* SETUP (mindig DATA0) */
    tds[n] = td_get();
    td_fill(tds[n], 0, UHCI_PID_SETUP, addr, 0, 0, 8, (u32)req, ls);
    n++;
    /* ADAT (DATA1-től váltakozva) */
    u8 tog = 1;
    int done = 0;
    for (int i = 0; i < tdd; i++) {
        int chunk = datalen - done; if (chunk > 1024) chunk = 1024;
        tds[n] = td_get();
        td_fill(tds[n], 0, (req->bmRequestType & 0x80) ? UHCI_PID_IN : UHCI_PID_OUT,
                addr, 0, tog, chunk, (u32)((u8 *)data + done), ls);
        tog ^= 1;
        done += chunk;
        n++;
    }
    /* STATUS (mindig DATA1, ellentétes irány) */
    tds[n] = td_get();
    td_fill(tds[n], 1, (req->bmRequestType & 0x80) ? UHCI_PID_OUT : UHCI_PID_IN,
            addr, 0, 1, 0, 0, ls);
    /* lánc összefűzése */
    for (int i = 0; i < n; i++) tds[i]->link = (u32)tds[i + 1];
    tds[n]->link = 1;                     /* TERM */

    int r = uhci_run(tds[0], tds[n], 2000);
    td_used = 0;
    return r;
}

/* ---------- bulk átvitel (több 1024 bájtos TD) ---------- */
static int uhci_bulk(usb_hcd_t *bus, u8 addr, u8 ep, int dir_in, int maxpkt,
                     void *data, int len, u8 *toggle) {
    (void)bus;
    if (maxpkt < 8 || maxpkt > 1024) maxpkt = 64;
    if (!uhci_active) return -9;
    if (len <= 0) return 0;
    int chunk_max = 1024;                  /* UHCI TD max 0x500, biztos 1024 */
    int nt = (len + chunk_max - 1) / chunk_max;
    if (nt > 120) nt = 120;                /* TD-limit: max ~120 KB / hívás */
    if (td_used + nt > 128) return -8;

    uhci_td_t *first = 0, *last = 0;
    u8 tog = *toggle;
    int done = 0;
    for (int i = 0; i < nt; i++) {
        int chunk = len - done; if (chunk > chunk_max) chunk = chunk_max;
        uhci_td_t *td = td_get();
        if (!first) first = td;
        last = td;
        td_fill(td, 0, dir_in ? UHCI_PID_IN : UHCI_PID_OUT,
                addr, ep & 15, tog, chunk, (u32)((u8 *)data + done), 0);
        /* a toggle csomagonként vált: egy TD-n belül a HW váltogatja */
        tog ^= (((u32)chunk + (u32)maxpkt - 1) / (u32)maxpkt) & 1;
        done += chunk;
    }
    /* lánc lezárás */
    uhci_td_t *prev = first;
    for (uhci_td_t *t = first + 1; t <= last; t++) { prev->link = (u32)t; prev = t; }
    last->link = 1;

    int r = uhci_run(first, last, 8000);
    td_used = 0;
    if (r > 0) *toggle = tog;              /* a következő átvitel toggle-je */
    return r;
}

/* ---------- portok ---------- */
static int uhci_port_reset(int port) {
    u16 reg = U_PORTSC(port);
    u16 st = urd(reg);
    if (!(st & U_P_CCS)) return 0;
    /* reset jelzés: korábbi írható bitek megtartásával */
    uwr(reg, (u16)((st & ~(U_P_RWC | U_P_SUSP)) | U_P_PR));
    usb_msleep(60);
    /* reset vége + port engedélyezése (QEMU: a PE bitet a vendég írja be;
       valódi vasalon a HC állítja, az újraírás ártalmatlan) */
    uwr(reg, (u16)((urd(reg) & ~(U_P_RWC | U_P_PR | U_P_SUSP)) | U_P_PE));
    usb_msleep(20);
    /* változásbitek törlése */
    uwr(reg, (u16)(urd(reg) | U_P_RWC));
    usb_msleep(5);
    st = urd(reg);
    if (!(st & U_P_CCS)) return 0;
    if (st & U_P_LSDA) { kprintf("uhci: p%d low-speed (atugorva)\n", port); return 0; }
    if (!(st & U_P_PE))  { kprintf("uhci: p%d nem engedelyezett (st=%x)\n", port, st); return 0; }
    return 1;
}

static void uhci_shutdown(usb_hcd_t *bus) {
    (void)bus;
    if (!uhci_active) return;
    uhci_stop();
    uwr(U_USBCMD, U_CMD_RESET);
    usb_msleep(30);
    uhci_active = 0;
}

/* ---------- vezérlő felállítása ---------- */
int uhci_add_controller(pci_dev_t pcidev) {
    if (uhci_active) return 0;             /* csak az első */
    u32 io = pci_bar_io(pcidev, 4);
    if (!io) { kprintf("uhci: nincs I/O BAR\n"); return 0; }

    /* BIOS legacy átadás (USBLEGSUP @ 0xC0) */
    u32 legs = pci_read32(pcidev, 0xC0);
    if (legs & 0x00010000u) {
        pci_write32(pcidev, 0xC0, legs | 0x01000000u);
        for (int i = 0; i < 100; i++) {
            legs = pci_read32(pcidev, 0xC0);
            if (!(legs & 0x00010000u)) break;
            usb_msleep(5);
        }
        pci_write32(pcidev, 0xC4, 0);
        kprintf("uhci: BIOS legacy atadva\n");
    }

    pci_enable_busmaster(pcidev);
    uhci_io = (u16)io;

    /* HC reset */
    uwr(U_USBCMD, U_CMD_RESET);
    for (int i = 0; i < 100; i++) {
        if (!(urd(U_USBCMD) & U_CMD_RESET)) break;
        usb_msleep(2);
    }
    /* frame lista + üres QH */
    u_qh.link = 1;                          /* TERM (nincs következő QH) */
    u_qh.element = 1;                       /* TERM */
    for (int i = 0; i < 1024; i++) u_framelist[i] = (u32)&u_qh | 2;   /* QH ptr */
    outl(uhci_io + U_FLBASEADD, (u32)u_framelist);
    uwr(U_FRNUM, 0);
    uwr(U_USBINTR, 0);
    uwr(U_USBSTS, 0xFFFF);
    uwr(U_USBCMD, U_CMD_RS | U_CMD_MAXP64);
    uhci_active = 1;
    td_used = 0;

    uhci_bus.type = 1;
    uhci_bus.pcidev = pcidev;
    uhci_bus.io = io;
    uhci_bus.control = uhci_control;
    uhci_bus.bulk = uhci_bulk;
    uhci_bus.shutdown = uhci_shutdown;
    uhci_bus.nports = 2;

    kprintf("uhci: vezérlo IO=%x megvagyok\n", io);
    /* portok szkennelése (PIIX: 2 port) */
    for (int p = 0; p < 2; p++) {
        u16 st = urd(U_PORTSC(p));
        if (st == 0xFFFF || st == 0) break;          /* nincs több port */
        if (st & U_P_CCS) {
            kprintf("uhci: p%d eszkoz csatlakozik\n", p);
            if (uhci_port_reset(p))
                usb_enumerate_port(&uhci_bus, p, USB_SPEED_FS);
        }
    }
    return 1;
}
