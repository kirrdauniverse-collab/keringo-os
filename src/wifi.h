/* wifi.h — Keringő OS Wi-Fi alrendszer (802.11g STA + WPA2-PSK) */
#ifndef KERINGO_WIFI_H
#define KERINGO_WIFI_H

#include "kernel.h"
#include "net.h"

/* állapotok */
#define WIFI_OFF      0
#define WIFI_SCAN     1
#define WIFI_AUTH     2
#define WIFI_ASSOC    3
#define WIFI_EAPOL    4
#define WIFI_UP       5

void wifi_init(void);
void wifi_poll(void);                       /* FSM + RX feldolgozás (fő ciklusból) */
int  wifi_hw_present(void);                 /* ath9k kártya vagy demo rádió */
const char *wifi_hw_name(void);             /* "AR9285" / "Demo" / "-" */

int  wifi_scan_start(void);                 /* 1 = indult */
int  wifi_scan_done(void);
int  wifi_ap_count(void);
const char *wifi_ap_ssid(int i);
int  wifi_ap_rssi(int i);                   /* 0..100 közelítés */
int  wifi_ap_sec(int i);                    /* 0 = nyílt, 1 = WPA2-PSK */
int  wifi_ap_chan(int i);

int  wifi_connect(int ap_idx, const char *pass);
void wifi_disconnect(void);
int  wifi_status(void);                     /* WIFI_* */
const char *wifi_status_text(int lang);
int  wifi_connected(void);
int  wifi_need_dhcp(void);                  /* 1: futtass net_dhcp()-t! */
void wifi_clear_need_dhcp(void);
const char *wifi_err(void);                 /* utolsó hiba oka (statikus) */

/* demo rádió (KERINGO.CFG: wifi_demo=1) */
void wifi_demo_enable(void);
void wifi_demo_disable(void);
int  wifi_demo_active(void);
const char *wifi_ssid(void);
const u8 *wifi_bssid(void);
int  ath9k_add_controller(pci_dev_t pcidev);

/* ath9k.c belső interfész */
typedef struct wifi_hw wifi_hw_t;
struct wifi_hw {
    int  (*tx)(const u8 *frame, u32 len, u8 rate_code);
    int  (*set_chan)(int chan);
    void (*poll)(void);
    const u8 *mac;
};
void wifi_hw_register(wifi_hw_t *hw);
void wifi_set_mac(const u8 *m);
void wifi_rx_frame(const u8 *buf, u32 len, u8 rssi, u8 rate_code);

/* sebesség-kódok (Atheros legacy) */
#define RATE_1M   0x1b
#define RATE_6M   0x0b

#endif
