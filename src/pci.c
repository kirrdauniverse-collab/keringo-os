/* pci.c — PCI konfigurációs tér elérés + eszközkeresés (0xCF8/0xCFC) */
#include "kernel.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

static u32 pci_pack(pci_dev_t d, u8 off) {
    return 0x80000000u | ((u32)d.bus << 16) | ((u32)d.dev << 11)
         | ((u32)d.fn << 8) | (off & 0xFC);
}

u32 pci_read32(pci_dev_t d, u8 off) {
    outl(PCI_ADDR, pci_pack(d, off));
    return inl(PCI_DATA);
}
u16 pci_read16(pci_dev_t d, u8 off) {
    outl(PCI_ADDR, pci_pack(d, off));
    return (u16)(inl(PCI_DATA) >> ((off & 2) * 8));
}
u8 pci_read8(pci_dev_t d, u8 off) {
    outl(PCI_ADDR, pci_pack(d, off));
    return (u8)(inl(PCI_DATA) >> ((off & 3) * 8));
}
void pci_write32(pci_dev_t d, u8 off, u32 v) {
    outl(PCI_ADDR, pci_pack(d, off));
    outl(PCI_DATA, v);
}
void pci_write16(pci_dev_t d, u8 off, u16 v) {
    u32 cur = pci_read32(d, off & ~3u);
    u32 shift = (off & 3) * 8;
    cur = (cur & ~(0xFFFFu << shift)) | ((u32)v << shift);
    pci_write32(d, off & ~3u, cur);
}

void pci_enable_busmaster(pci_dev_t d) {
    u16 cmd = pci_read16(d, 0x04);
    pci_write16(d, 0x04, cmd | 0x0007);   /* IO + MEM + BusMaster */
}

u32 pci_bar_mmio(pci_dev_t d, int idx) {
    u32 bar = pci_read32(d, 0x10 + idx * 4);
    if (!(bar & 1)) return bar & ~0xFu;
    return 0;
}
u32 pci_bar_io(pci_dev_t d, int idx) {
    u32 bar = pci_read32(d, 0x10 + idx * 4);
    if (bar & 1) return bar & ~3u;
    return 0;
}

/* osztály+alosztály szerinti keresés (nth. találat, 0-tól) */
int pci_scan_class(pci_dev_t *out, u16 class, u16 subclass, int nth) {
    int found = 0;
    for (u32 bus = 0; bus < 256; bus++)
        for (u32 dev = 0; dev < 32; dev++)
            for (u32 fn = 0; fn < 8; fn++) {
                pci_dev_t d = { (u8)bus, (u8)dev, (u8)fn };
                u16 vid = pci_read16(d, 0x00);
                if (vid == 0xFFFF) { if (fn == 0) break; continue; }
                u16 cls = pci_read16(d, 0x0A);
                if ((cls >> 8) == class && (cls & 0xFF) == subclass) {
                    if (found == nth) { *out = d; return 1; }
                    found++;
                }
                /* multifunkció nélkül az fn=0 után ugrás */
                if (fn == 0) {
                    u8 hdr = pci_read8(d, 0x0E);
                    if (!(hdr & 0x80)) break;
                }
            }
    return 0;
}

/* debug: minden PCI eszkoz listazasa a soros portra */
void pci_debug_dump(void) {
    kprintf("PCI eszkozok:\n");
    for (u32 bus = 0; bus < 2; bus++)
        for (u32 dev = 0; dev < 32; dev++)
            for (u32 fn = 0; fn < 8; fn++) {
                pci_dev_t d = { (u8)bus, (u8)dev, (u8)fn };
                u16 vid = pci_read16(d, 0x00);
                if (vid == 0xFFFF) { if (fn == 0) break; continue; }
                u16 cls = pci_read16(d, 0x0A);
                kprintf("  bus=%d dev=%d fn=%d vid=%x did=%x cls=%x hdr=%x\n",
                        (u8)bus, (u8)dev, (u8)fn, vid, pci_read16(d, 0x02), cls, pci_read8(d, 0x0E));
                if (fn == 0 && !(pci_read8(d, 0x0E) & 0x80)) break;
            }
}
