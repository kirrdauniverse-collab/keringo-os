/* app_wifi.c — Keringő OS 0.4 Wi-Fi panel: hálózatkeresés, WPA2 csatlakozás */
#include "kernel.h"
#include "wifi.h"
#include "net.h"

#define COL_BG    0xFF0E1319
#define COL_PANEL 0xFF162028
#define COL_TEXT  0xFFE8ECEF
#define COL_MUT   0xFF8FA0AC
#define COL_AMBER 0xFFF2B23C
#define COL_GREEN 0xFF54D178
#define COL_RED   0xFFE05B5B
#define COL_SEL   0xFF24313C

static int sel = 0;
static int pw_len = 0;
static char pw[64];
static int input_mode = 0;          /* 0 lista, 1 jelszó-bevitel */
static u32  last_frame_tick = 0;
static char statusline[96];

static void make_status(void) {
    int lang = ui_lang();
    if (wifi_connected()) {
        char ip[16];
        net_ip_str(net_self_ip(), ip, sizeof(ip));
        if (net_up())
            ksnprintf(statusline, sizeof(statusline),
                      lang ? "Connected - IP: %s" : "Kapcsolódva - IP: %s", ip);
        else
            ksnprintf(statusline, sizeof(statusline),
                      lang ? "Connected, waiting for IP..." : "Kapcsolódva, IP-re vár...");
    } else {
        const char *st = wifi_status_text(lang);
        const char *er = wifi_err();
        if (er[0])
            ksnprintf(statusline, sizeof(statusline), "%s - %s", st, er);
        else
            ksnprintf(statusline, sizeof(statusline), "%s (%s)", st, wifi_hw_name());
    }
}

void wifiapp_enter(void) {
    sel = 0; pw_len = 0; pw[0] = 0; input_mode = 0;
    if (!wifi_connected() && wifi_status() == WIFI_OFF) wifi_scan_start();
    app_srand(tick_count());
}

void wifiapp_exit(void) { }

static void draw_signal(int x, int y, int rssi, u32 c) {
    for (int b = 0; b < 4; b++) {
        int bh = 3 + b * 3;
        int on = rssi >= (b + 1) * 22;
        u32 col = on ? c : 0xFF2A3640;
        for (int i = 0; i < bh; i++)
            gfx_vline(x + b * 5, y + 12 - i, 3, col);
    }
}

void wifiapp_frame(u32 tick) {
    (void)tick;
    int W = (int)gfx_width(), H = (int)gfx_height();
    int lang = ui_lang();
    u32 acc = settings_accent_color();
    make_status();
    app_chrome(lang ? "Wi-Fi" : "Wi-Fi", statusline, acc);
    gfx_fill_rect(0, 23, W, H - 44, COL_BG);

    int x = (W - 620) / 2; if (x < 10) x = 10;
    int y = 40;
    int w = 620; if (x + w > W - 10) w = W - 20 - x;

    /* hardver sor */
    gfx_fill_rect(x, y, w, 20, COL_PANEL);
    char hb[80];
    ksnprintf(hb, sizeof(hb), "%s: %s",
              lang ? "Radio" : "Rádió", wifi_hw_name());
    gfx_draw_text(x + 10, y + 6, hb, COL_MUT, 0xFF000000u);
    y += 28;

    if (wifi_connected()) {
        gfx_fill_rect(x, y, w, 92, COL_PANEL);
        gfx_rect(x, y, w, 92, COL_GREEN);
        gfx_draw_text(x + 12, y + 10, wifi_ssid(), COL_GREEN, 0xFF000000u);
        char b[96];
        const u8 *m = wifi_bssid();
        ksnprintf(b, sizeof(b), "BSSID: %02x:%02x:%02x:%02x:%02x:%02x",
                  m[0], m[1], m[2], m[3], m[4], m[5]);
        gfx_draw_text(x + 12, y + 28, b, COL_MUT, 0xFF000000u);
        char ip[16];
        net_ip_str(net_self_ip(), ip, sizeof(ip));
        ksnprintf(b, sizeof(b), lang ? "IP: %s" : "IP: %s", ip);
        gfx_draw_text(x + 12, y + 46, b, COL_TEXT, 0xFF000000u);
        ksnprintf(b, sizeof(b),
                  lang ? "D: disconnect   browser works over Wi-Fi too"
                       : "D: szétkapcsolás   a böngésző Wi-Fi-n is megy");
        gfx_draw_text(x + 12, y + 70, b, COL_MUT, 0xFF000000u);
        y += 104;
    }

    /* hálózat lista */
    int n = wifi_ap_count();
    ksnprintf(hb, sizeof(hb), lang ? "Networks (%d)" : "Hálózatok (%d)", n);
    gfx_draw_text(x, y, hb, COL_AMBER, 0xFF000000u);
    y += 16;
    if (wifi_status() == WIFI_SCAN) {
        gfx_draw_text(x + 8, y + 6, lang ? "Scanning channels 1 / 6 / 11..."
                                         : "Csatornák pásztázása 1 / 6 / 11...", COL_MUT, 0xFF000000u);
        y += 20;
    } else if (n == 0) {
        gfx_draw_text(x + 8, y + 6,
                      lang ? "No networks. Enter: rescan"
                           : "Nincs hálózat. Enter: újra keresés", COL_MUT, 0xFF000000u);
        y += 20;
    }
    int rowh = 22;
    for (int i = 0; i < n && y + rowh < H - 120; i++) {
        int selrow = (i == sel) && !input_mode;
        if (selrow) gfx_fill_rect(x, y, w, rowh - 2, COL_SEL);
        draw_signal(x + 6, y + 4, wifi_ap_rssi(i),
                    selrow ? COL_AMBER : COL_TEXT);
        const char *nm = wifi_ap_ssid(i);
        if (!nm[0]) nm = lang ? "(hidden)" : "(rejtett)";
        gfx_draw_text(x + 34, y + 6, nm, selrow ? COL_AMBER : COL_TEXT, 0xFF000000u);
        /* biztonság jel */
        if (wifi_ap_sec(i))
            gfx_draw_text(x + w - 70, y + 6, "WPA2", COL_MUT, 0xFF000000u);
        else
            gfx_draw_text(x + w - 70, y + 6, "open", COL_MUT, 0xFF000000u);
        y += rowh;
    }

    /* jelszó-beviteli sor */
    int py = H - 100;
    gfx_fill_rect(x, py, w, 26, COL_PANEL);
    gfx_rect(x, py, w, 26, input_mode ? COL_AMBER : COL_MUT);
    if (input_mode) {
        gfx_draw_text(x + 8, py + 9, lang ? "WPA2 password: " : "WPA2 jelszó: ",
                     COL_AMBER, 0xFF000000u);
        for (int i = 0; i < pw_len && i < 40; i++)
            gfx_draw_text(x + 110 + i * 8, py + 9, "*", COL_TEXT, 0xFF000000u);
        if ((tick_count() / 20) % 2)
            gfx_fill_rect(x + 110 + pw_len * 8, py + 8, 7, 10, COL_AMBER);
        const char *h2 = lang ? "Enter: connect  Backspace: erase  Esc: cancel"
                              : "Enter: csatlakozás  Backspace: törlés  Esc: mégse";
        gfx_draw_text(x + 8, py + 30, h2, COL_MUT, 0xFF000000u);
    } else {
        const char *h2 = wifi_connected()
            ? (lang ? "D: disconnect   Enter: rescan" : "D: szétkapcsolás  Enter: újraszken")
            : (lang ? "Arrows: choose   Enter: connect / rescan"
                    : "Nyilak: választás  Enter: csatlakozás / újraszken");
        gfx_draw_text(x + 8, py + 9, h2, COL_MUT, 0xFF000000u);
    }
    (void)last_frame_tick;
}

void wifiapp_key(u8 sc, u8 rel, u8 ascii) {
    if (rel) return;
    int lang = ui_lang();
    if (input_mode) {
        if (sc == KEY_ESC) { input_mode = 0; pw_len = 0; pw[0] = 0; return; }
        if (sc == 0x0E) {                    /* backspace */
            if (pw_len > 0) pw[--pw_len] = 0;
            return;
        }
        if (sc == KEY_ENTER) {
            input_mode = 0;
            pw[pw_len] = 0;
            wifi_connect(sel, pw);
            pw_len = 0; pw[0] = 0;
            return;
        }
        if (ascii >= 32 && ascii < 127 && pw_len < 60) {
            pw[pw_len++] = (char)ascii;
            pw[pw_len] = 0;
        }
        return;
    }
    int n = wifi_ap_count();
    if (sc == KEY_UP && sel > 0) sel--;
    else if (sc == KEY_DOWN && sel < n - 1) sel++;
    else if (sc == KEY_ENTER) {
        if (wifi_connected() && wifi_status() == WIFI_UP) {
            wifi_scan_start();
            return;
        }
        if (n == 0) { wifi_scan_start(); return; }
        if (sel < n) {
            if (wifi_ap_sec(sel)) {
                input_mode = 1;
                pw_len = 0; pw[0] = 0;
            } else {
                wifi_connect(sel, "");
            }
        }
    } else if ((ascii == 'd' || ascii == 'D') && wifi_connected()) {
        wifi_disconnect();
        wifi_scan_start();
    } else if (ascii == 'r' || ascii == 'R') {
        wifi_scan_start();
    }
    (void)lang;
}
