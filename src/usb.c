/* usb.c — USB alrendszer: PCI keresés, eszköz-enumeráció, MSC felismerés */
#include "kernel.h"
#include "usb.h"

usb_dev_t usb_devices[4];
int usb_msc_count = 0;
static int usb_ok = 0;

usb_dev_t *usb_dev_alloc(void) {
    for (int i = 0; i < 4; i++)
        if (!usb_devices[i].alive) {
            for (u8 *p = (u8 *)&usb_devices[i]; p < (u8 *)&usb_devices[i] + sizeof(usb_dev_t); p++) *p = 0;
            usb_devices[i].alive = 1;
            return &usb_devices[i];
        }
    return 0;
}

/* ---- vezérlőátviteli segédek ---- */
static int usb_get_desc(usb_hcd_t *bus, u8 addr, int speed, u8 ep0max,
                        u8 dtype, u8 dindex, void *buf, int len) {
    usb_dev_req_t req;
    req.bmRequestType = 0x80;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = (u16)((dtype << 8) | dindex);
    req.wIndex = 0;
    req.wLength = (u16)len;
    return bus->control(bus, addr, speed, ep0max, &req, buf, len);
}

static int usb_set_address(usb_hcd_t *bus, int speed, u8 newaddr) {
    usb_dev_req_t req;
    req.bmRequestType = 0x00;
    req.bRequest = USB_REQ_SET_ADDRESS;
    req.wValue = newaddr;
    req.wIndex = 0;
    req.wLength = 0;
    return bus->control(bus, 0, speed, 8, &req, 0, 0);
}

static int usb_set_config(usb_dev_t *d) {
    usb_dev_req_t req;
    req.bmRequestType = 0x00;
    req.bRequest = USB_REQ_SET_CONFIGURATION;
    req.wValue = 1;
    req.wIndex = 0;
    req.wLength = 0;
    return d->bus->control(d->bus, d->addr, d->speed, d->ep0max, &req, 0, 0);
}

/* ---- konfiguráció-leíró elemzése: MSC BOT keresés ---- */
static int parse_config(usb_dev_t *d, const u8 *cfg, int len) {
    int total = cfg[2] | (cfg[3] << 8);
    if (total > len) total = len;
    int i = cfg[0];                        /* az első interface után kezdünk */
    u8 ifclass = 0, ifsub = 0, ifproto = 0;
    while (i + 2 <= total) {
        u8 dlen = cfg[i];
        u8 dtype = cfg[i + 1];
        if (dlen < 2) break;
        if (dtype == 4 && i + 9 <= total) {          /* interface */
            ifclass = cfg[i + 5]; ifsub = cfg[i + 6]; ifproto = cfg[i + 7];
        } else if (dtype == 5 && i + 7 <= total) {   /* endpoint */
            u8 epaddr = cfg[i + 2];
            u8 attr  = cfg[i + 3];
            u16 maxp = (u16)(cfg[i + 4] | (cfg[i + 5] << 8));
            if ((attr & 3) == 2 && ifclass == 8 && ifsub == 6 && ifproto == 0x50) {
                if (epaddr & 0x80) { d->bulk_in = epaddr & 0xF; d->bulk_maxpkt = maxp; }
                else               { d->bulk_out = epaddr & 0xF; }
                d->msc = 1;
            }
        }
        i += dlen;
    }
    return d->msc;
}

/* ---- porton talált eszköz enumerálása ---- */
int usb_enumerate_port(usb_hcd_t *bus, int port, int speed) {
    static u8 descbuf[512];
    usb_dev_t *dev = usb_dev_alloc();
    if (!dev) return 0;
    dev->bus = bus;
    dev->speed = speed;
    dev->ep0max = (speed == USB_SPEED_HS) ? 64 : 8;

    kprintf("usb: p%d enumeracio indul (maxp0=%d)\n", port, dev->ep0max);
    /* 1. elsõ 8 bájt: bMaxPacketSize0 */
    for (int tr = 0; tr < 3; tr++) {
        if (usb_get_desc(bus, 0, speed, dev->ep0max, USB_DESC_DEVICE, 0, descbuf, 8) >= 0) break;
        usb_msleep(30);
    }
    if (!(descbuf[0] == 18 && descbuf[1] == 1)) {
        kprintf("usb: p%d desc-olvasas gaz (b0=%d b1=%d)\n", port, descbuf[0], descbuf[1]);
        dev->alive = 0;
        return 0;
    }
    dev->ep0max = descbuf[7];
    if (dev->ep0max < 8) dev->ep0max = 8;
    if (dev->ep0max > 64) dev->ep0max = 64;

    /* 2. cím beállítása */
    u8 addr = 2 + (int)(dev - usb_devices);
    if (usb_set_address(bus, speed, addr) < 0) {
        kprintf("usb: p%d SET_ADDRESS hiba\n", port);
        dev->alive = 0;
        return 0;
    }
    usb_msleep(20);
    dev->addr = addr;

    /* 3. teljes eszköz-leíró */
    if (usb_get_desc(bus, addr, speed, dev->ep0max, USB_DESC_DEVICE, 0, descbuf, 18) < 0) {
        kprintf("usb: p%d device-desc hiba\n", port);
        dev->alive = 0;
        return 0;
    }
    u16 vid = (u16)(descbuf[8] | (descbuf[9] << 8));
    u16 pid = (u16)(descbuf[10] | (descbuf[11] << 8));
    kprintf("usb: p%d VID=%x PID=%x osztaly=%d\n", port, vid, pid, descbuf[4]);

    /* 4. konfiguráció-leíró (max 255 bájt, rövid csomag lezárja) */
    int got = usb_get_desc(bus, addr, speed, dev->ep0max, USB_DESC_CONFIG, 0, descbuf, 255);
    if (got < 9) {
        kprintf("usb: p%d config-desc hiba (%d)\n", port, got);
        dev->alive = 0;
        return 0;
    }
    if (!parse_config(dev, descbuf, got)) {
        kprintf("usb: p%d nem MSC BOT eszkoz\n", port);
        dev->alive = 0;
        return 0;
    }

    /* 5. konfiguráció kiválasztása */
    if (usb_set_config(dev) < 0) {
        kprintf("usb: p%d SET_CONFIG hiba\n", port);
        dev->alive = 0;
        return 0;
    }
    usb_msleep(50);
    kprintf("usb: p%d MSC BOT: in=%d out=%d maxp=%d\n",
            port, dev->bulk_in, dev->bulk_out, dev->bulk_maxpkt);

    /* 6. MSC felcsatolása */
    if (msc_attach(dev) > 0) {
        usb_msc_count++;
        kprintf("usb: p%d tolto-tarolo kesz!\n", port);
        return 1;
    }
    dev->alive = 0;
    return 0;
}

/* ---- nyilvános API ---- */
void usb_shutdown_all(void) {
    /* minden élő bus leállítása a kexec előtt */
    for (int i = 0; i < 4; i++) {
        if (usb_devices[i].alive && usb_devices[i].bus && usb_devices[i].bus->shutdown)
            usb_devices[i].bus->shutdown(usb_devices[i].bus);
    }
    msc_shutdown();
}

void usb_init(void) {
    pci_dev_t pc;
    int n = 0;
    usb_ok = 0;
    pci_debug_dump();
    /* minden USB vezérlõ (osztály 0x0C, alosztély 0x03); prog-if dönt: 0=UHCI 0x10=OHCI 0x20=EHCI 0x30=xHCI */
    while (n < 12 && pci_scan_class(&pc, 0x0C, 0x03, n)) {
        u8 progif = PCI_PROGIF(pc);
        if (progif == 0x20) {
            kprintf("usb: EHCI @ %x:%x:%x\n", pc.bus, pc.dev, pc.fn);
            ehci_add_controller(pc);
        } else if (progif == 0x00) {
            kprintf("usb: UHCI @ %x:%x:%x\n", pc.bus, pc.dev, pc.fn);
            uhci_add_controller(pc);
        } else {
            kprintf("usb: vezérlõ progif=%x @ %x:%x:%x (nem tamogatott)\n",
                    progif, pc.bus, pc.dev, pc.fn);
        }
        n++;
    }
    usb_ok = (usb_msc_count > 0);
    kprintf("usb: kesz, msc=%d\n", usb_msc_count);
}

int usb_ready(void) { return usb_ok; }
int usb_flush(void) { return msc_sync(); }
int usb_flush_and_recheck(void) { return msc_sync(); }

int usb_read_secs(u32 lba, u32 n, void *buf) {
    return msc_read_secs(lba, n, (u8 *)buf);
}
int usb_write_secs(u32 lba, u32 n, const void *buf) {
    return msc_write_secs(lba, n, (const u8 *)buf);
}
