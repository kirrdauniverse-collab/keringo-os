/* usb.h — USB alrendszer belős közös definíciók (csak USB modulok használják) */
#ifndef KERINGO_USB_H
#define KERINGO_USB_H

#include "kernel.h"

/* ---------- USB kérés (8 bájt) ---------- */
typedef struct {
    u8 bmRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} __attribute__((packed)) usb_dev_req_t;

#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_SET_ADDRESS    5
#define USB_REQ_SET_CONFIGURATION 9
#define USB_DESC_DEVICE        1
#define USB_DESC_CONFIG        2

/* ---------- eszköz-sebesség ---------- */
#define USB_SPEED_FS 0
#define USB_SPEED_HS 1
#define USB_SPEED_LS 2

/* ---------- host controller ---------- */
typedef struct usb_hcd usb_hcd_t;
struct usb_hcd {
    int type;                 /* 1 = UHCI, 2 = EHCI */
    pci_dev_t pcidev;
    u32 io;                   /* UHCI: I/O bázis */
    u32 mmio;                 /* EHCI: MMIO bázis (identity-térben) */
    int nports;
    int (*control)(usb_hcd_t *bus, u8 addr, int speed, int ep0max,
                   const usb_dev_req_t *req, void *data, int datalen);
    int (*bulk)(usb_hcd_t *bus, u8 addr, u8 ep, int dir_in, int maxpkt,
                void *data, int len, u8 *toggle);
    void (*shutdown)(usb_hcd_t *bus);
};

/* ---------- USB eszköz ---------- */
typedef struct {
    usb_hcd_t *bus;
    u8 addr;
    int speed;
    u8 ep0max;
    u8 msc;                   /* MSC BOT tömegtároló */
    u8 bulk_in;               /* endpoint szám (bit7 = IN irány) */
    u8 bulk_out;
    u16 bulk_maxpkt;
    u8 tog_in;                /* bulk adat-toggle állapotok */
    u8 tog_out;
    u32 tag;                  /* MSC CBW címke */
    int alive;
} usb_dev_t;

extern usb_dev_t usb_devices[4];
usb_dev_t *usb_dev_alloc(void);
void usb_msleep(int ms);

/* modulok között */
int usb_enumerate_port(usb_hcd_t *bus, int port, int speed);
int msc_attach(usb_dev_t *dev);
int uhci_add_controller(pci_dev_t pcidev);
int ehci_add_controller(pci_dev_t pcidev);

/* MSC vezérlés (msc.c) */
int msc_read_secs(u32 lba, u32 n, u8 *buf);
int msc_write_secs(u32 lba, u32 n, const u8 *buf);
int msc_sync(void);
int msc_present(void);
u32 msc_sector_count(void);

/* elfogott eszközök száma */
extern int usb_msc_count;

#endif
