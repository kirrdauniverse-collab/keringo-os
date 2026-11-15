/* e1000.c — Intel e1000 (QEMU alap NIC) meghajtó: MMIO, RX/TX gyűrűk, polling */
#include "kernel.h"
#include "net.h"

#define E_CTRL      0x0000
#define E_STATUS    0x0008
#define E_IMC       0x00D8
#define E_RCTL      0x0100
#define E_TCTL      0x0400
#define E_RDBAL     0x2800
#define E_RDBAH     0x2804
#define E_RDLEN     0x2808
#define E_RDH       0x2810
#define E_RDT       0x2818
#define E_TDBAL     0x3800
#define E_TDBAH     0x3804
#define E_TDLEN     0x3808
#define E_TDH       0x3810
#define E_TDT       0x3818
#define E_RAL0      0x5400
#define E_RAH0      0x5404

#define RX_N 64
#define TX_N 64

/* byte-pontos legacy leírók (lásd e1000 spec 3.3.1 / 3.3.2) */
typedef struct {
    u32 lo, hi;          /* puffer cím */
    u16 len;             /* hossz */
    u16 csum;            /* checksum */
    u8  status;          /* DD stb. */
    u8  errors;
    u16 special;
} __attribute__((packed)) rxdesc_t;
typedef struct {
    u32 lo, hi;          /* puffer cím */
    u16 len;             /* hossz */
    u8  cso;
    u8  cmd;             /* EOP|IFCS|RS */
    u8  status;          /* DD */
    u8  css;
    u16 special;
} __attribute__((packed)) txdesc_t;

static rxdesc_t *rxring = 0;
static txdesc_t *txring = 0;
static u8 *rxbufs[RX_N];
static u8 *txbufs[TX_N];
static u32 rx_tail = 0;          /* a következő kiolvasandó = RDH */
static u32 tx_next = 0;
static u32 e_mmio = 0;
static netdev_t e1000_dev;

static inline u32 erd(u32 off) { return *(volatile u32 *)(e_mmio + off); }
static inline void ewr(u32 off, u32 v) { *(volatile u32 *)(e_mmio + off) = v; }

static int e1000_send(netdev_t *dev, const u8 *pkt, u32 len) {
    (void)dev;
    if (len > 1514) len = 1514;
    u32 idx = tx_next % TX_N;
    for (u32 i = 0; i < len; i++) txbufs[idx][i] = pkt[i];
    txdesc_t *d = &txring[idx];
    d->lo = (u32)txbufs[idx];
    d->hi = 0;
    d->len = (u16)len;
    d->cso = 0;
    d->cmd = 0x0B;                          /* EOP | IFCS | RS */
    d->status = 0;
    tx_next++;
    ewr(E_TDT, tx_next % TX_N);
    /* várakozás a DD-re (rövid, busy-poll) */
    u32 until = tick_count() + 10;
    while (!(txring[idx].status & 0x01)) {
        if ((s32)(tick_count() - until) >= 0) return 0;
    }
    return 1;
}

static void e1000_poll(netdev_t *dev) {
    (void)dev;
    u32 head = erd(E_RDH);
    while (rx_tail != head) {
        rxdesc_t *d = &rxring[rx_tail];
        if (!(d->status & 0x01)) break;      /* nem kész */
        u32 len = d->len;
        if (len > 14 && len < 1600) net_rx(rxbufs[rx_tail], len);
        rx_tail = (rx_tail + 1) % RX_N;
        ewr(E_RDT, (rx_tail + RX_N - 1) % RX_N);
        head = erd(E_RDH);
    }
}

static void e1000_shutdown(netdev_t *dev) {
    (void)dev;
    if (!e_mmio) return;
    ewr(E_RCTL, 0);
    ewr(E_TCTL, 0);
    ewr(E_IMC, 0xFFFFFFFF);
    ewr(E_CTRL, erd(E_CTRL) | 0x04000000u);  /* RST */
}

int e1000_add_controller(pci_dev_t pcidev) {
    if (e_mmio) return 0;
    u32 mmio = pci_bar_mmio(pcidev, 0);
    if (!mmio) { kprintf("e1000: nincs MMIO\n"); return 0; }
    e_mmio = (u32)mem_map_mmio(mmio, 0x20000);
    if (!e_mmio) { kprintf("e1000: MMIO lekerpezes nem sikerult\n"); return 0; }
    pci_enable_busmaster(pcidev);

    ewr(E_IMC, 0xFFFFFFFF);                 /* megszakítások tiltva */
    /* reset */
    ewr(E_CTRL, erd(E_CTRL) | 0x04000000u);
    u32 until = tick_count() + 25;
    while ((erd(E_CTRL) & 0x04000000u) && (s32)(tick_count() - until) < 0) { }

    /* MAC cím a regiszterekből */
    u32 ral = erd(E_RAL0), rah = erd(E_RAH0);
    e1000_dev.mac[0] = ral & 0xFF; e1000_dev.mac[1] = (ral >> 8) & 0xFF;
    e1000_dev.mac[2] = (ral >> 16) & 0xFF; e1000_dev.mac[3] = (ral >> 24) & 0xFF;
    e1000_dev.mac[4] = rah & 0xFF; e1000_dev.mac[5] = (rah >> 8) & 0xFF;

    /* RX gyűrű */
    rxring = (rxdesc_t *)kmalloc(RX_N * 16);
    txring = (txdesc_t *)kmalloc(TX_N * 16);
    if (!rxring || !txring) return 0;
    for (u32 *p = (u32 *)rxring; p < (u32 *)rxring + RX_N * 4; p++) *p = 0;
    for (u32 *p = (u32 *)txring; p < (u32 *)txring + TX_N * 4; p++) *p = 0;
    for (int i = 0; i < RX_N; i++) {
        rxbufs[i] = (u8 *)kmalloc(2048);
        rxring[i].lo = (u32)rxbufs[i];
        rxring[i].hi = 0;
        rxring[i].status = 0;
    }
    for (int i = 0; i < TX_N; i++) txbufs[i] = (u8 *)kmalloc(1600);

    ewr(E_RDBAL, (u32)rxring);
    ewr(E_RDBAH, 0);
    ewr(E_RDLEN, RX_N * 16);
    ewr(E_RDH, 0);
    ewr(E_RDT, RX_N - 1);
    rx_tail = 0;

    ewr(E_TDBAL, (u32)txring);
    ewr(E_TDBAH, 0);
    ewr(E_TDLEN, TX_N * 16);
    ewr(E_TDH, 0);
    ewr(E_TDT, 0);
    tx_next = 0;

    /* RCTL: engedély + broadcast + 2048 bájtos pufferek */
    ewr(E_RCTL, (1u << 1) | (1u << 15) | (0u << 16));
    /* TCTL: engedély + PSP + full-duplex COLD */
    ewr(E_TCTL, (1u << 1) | (1u << 3) | (0x40u << 12));

    e1000_dev.type = 1;
    e1000_dev.send = e1000_send;
    e1000_dev.poll = e1000_poll;
    e1000_dev.shutdown = e1000_shutdown;
    net_dev = &e1000_dev;
    kprintf("e1000: MMIO=%x MAC=%x:%x:%x:%x:%x:%x\n", mmio,
            e1000_dev.mac[0], e1000_dev.mac[1], e1000_dev.mac[2],
            e1000_dev.mac[3], e1000_dev.mac[4], e1000_dev.mac[5]);
    return 1;
}
