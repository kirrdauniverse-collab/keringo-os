/* rtl8139.c — Realtek RTL8139/810x (8139-kompatibilis) meghajtó (valódi laptopokhoz) */
#include "kernel.h"
#include "net.h"

#define R_MAC0      0x00
#define R_TXSTATUS0 0x10
#define R_TXADDR0   0x20
#define R_RXBUF     0x30
#define R_CHIPCMD   0x37
#define R_RXBUFPTR  0x38
#define R_INTRMASK  0x3C
#define R_INTRSTATUS 0x3E
#define R_TXCONFIG  0x40
#define R_RXCONFIG  0x44
#define R_CFG9346   0x50
#define R_CONFIG1   0x52
#define R_HLTCLK    0x5B

#define CMD_RESET   0x10
#define CMD_RXENB   0x08
#define CMD_TXENB   0x04
#define CMD_RXEMPTY 0x01

#define RX_RING (8192 * 4)                  /* 32 KB gyűrű */
#define RX_PAD  16
#define RX_WRAP 2048

static u16 r_io = 0;
static u8 *rx_ring = 0;
static u8 *tx_bounce[4];
static u32 cur_rx = 0;
static u32 tx_flag;
static netdev_t rtl_dev;

static inline u8 rr8(u8 r)  { return inb(r_io + r); }
static inline u16 rr16(u8 r) { return inw(r_io + r); }
static inline u32 rr32(u8 r) { return inl(r_io + r); }
static inline void rw8(u8 r, u8 v)  { outb(r_io + r, v); }
static inline void rw16(u8 r, u16 v) { outw(r_io + r, v); }
static inline void rw32(u8 r, u32 v) { outl(r_io + r, v); }

static int rtl_send(netdev_t *dev, const u8 *pkt, u32 len) {
    (void)dev;
    if (len > 1514) len = 1514;
    static u32 slot = 0;
    /* várjuk meg, amíg a foglalt leíró felszabadul */
    for (int t = 0; t < 4; t++) {
        u32 st = rr32(R_TXSTATUS0 + 4 * slot);
        if (st == 0 || (st & 0x80000000u)) break;   /* TxStatOK / szabad */
        slot = (slot + 1) & 3;
    }
    u8 *buf = tx_bounce[slot];
    for (u32 i = 0; i < len; i++) buf[i] = pkt[i];
    if (len < 60) for (u32 i = len; i < 60; i++) buf[i] = 0;
    rw32(R_TXADDR0 + 4 * slot, (u32)buf);
    rw32(R_TXSTATUS0 + 4 * slot, tx_flag | (len < 60 ? 60 : len));
    /* befejezés pollja */
    u32 until = tick_count() + 10;
    while (!(rr32(R_TXSTATUS0 + 4 * slot) & 0x80000000u)) {
        if ((s32)(tick_count() - until) >= 0) break;
    }
    slot = (slot + 1) & 3;
    return 1;
}

static void rtl_poll(netdev_t *dev) {
    (void)dev;
    while (!(rr16(R_CHIPCMD) & CMD_RXEMPTY)) {
        u32 ring_off = cur_rx % RX_RING;
        u32 pkt_off = ring_off;
        /* csomagfejléc: status(2) + size(2) */
        u16 status = (u16)(rx_ring[pkt_off] | (rx_ring[pkt_off + 1] << 8));
        u16 size = (u16)(rx_ring[pkt_off + 2] | (rx_ring[pkt_off + 3] << 8));
        if (size < 4 || size > 1600) {
            /* hiba: gyűrű alaphelyzet */
            cur_rx = 0;
            rw16(R_RXBUFPTR, (u16)(cur_rx - 16));
            return;
        }
        if (status & 0x0001) {               /* RxStatusOK */
            u32 plen = (u32)size - 4;        /* CRC levágva */
            static u8 tmp[1600];
            for (u32 i = 0; i < plen; i++) {
                tmp[i] = rx_ring[(pkt_off + 4 + i) % RX_RING];
            }
            net_rx(tmp, plen);
        }
        cur_rx = (cur_rx + ((u32)size + 4 + 7) & ~7u) % RX_RING;
        rw16(R_RXBUFPTR, (u16)((cur_rx + RX_RING - 16) % RX_RING));
        /* ha a chip újra üres jelzést ad, végeztünk */
        if (rr16(R_INTRSTATUS) & 0x01) rw16(R_INTRSTATUS, 0x01);
    }
}

static void rtl_shutdown(netdev_t *dev) {
    (void)dev;
    if (!r_io) return;
    rw16(R_INTRMASK, 0);
    rw8(R_CHIPCMD, CMD_RESET);
}

int rtl8139_add_controller(pci_dev_t pcidev) {
    if (r_io) return 0;
    u32 io = pci_bar_io(pcidev, 0);
    if (!io) { kprintf("rtl8139: nincs I/O BAR\n"); return 0; }
    r_io = (u16)io;
    pci_enable_busmaster(pcidev);

    rw8(R_HLTCLK, 'R');                      /* ébresztés */
    /* reset */
    rw8(R_CHIPCMD, CMD_RESET);
    for (int i = 0; i < 1000; i++)
        if (!(rr8(R_CHIPCMD) & CMD_RESET)) break;

    /* MAC */
    for (int i = 0; i < 6; i++) rtl_dev.mac[i] = rr8(R_MAC0 + i);

    /* RX gyűrű */
    rx_ring = (u8 *)kmalloc(RX_RING + RX_PAD + RX_WRAP);
    for (u32 i = 0; i < RX_RING + RX_PAD + RX_WRAP; i++) rx_ring[i] = 0;
    rw32(R_RXBUF, (u32)rx_ring);
    cur_rx = 0;
    rw16(R_RXBUFPTR, 0);

    for (int i = 0; i < 4; i++) tx_bounce[i] = (u8 *)kmalloc(1600);
    tx_flag = (256u << 17) | (6u << 8);

    rw32(R_RXCONFIG, (1u << 12) | (1u << 7) | (7u << 13) | (7u << 8)
                    | (1u << 3) | (1u << 2) | (1u << 1));  /* 32K, nincs wrap, AB|AM|APM */
    rw32(R_TXCONFIG, (3u << 24) | (6u << 8) | (8u << 4));
    rw16(R_INTRMASK, 0);
    rw16(R_INTRSTATUS, 0xFFFF);
    rw8(R_CHIPCMD, CMD_RXENB | CMD_TXENB);

    rtl_dev.type = 2;
    rtl_dev.send = rtl_send;
    rtl_dev.poll = rtl_poll;
    rtl_dev.shutdown = rtl_shutdown;
    net_dev = &rtl_dev;
    kprintf("rtl8139: IO=%x MAC=%02x:%02x:%02x:%02x:%02x:%02x\n", io,
            rtl_dev.mac[0], rtl_dev.mac[1], rtl_dev.mac[2],
            rtl_dev.mac[3], rtl_dev.mac[4], rtl_dev.mac[5]);
    return 1;
}
