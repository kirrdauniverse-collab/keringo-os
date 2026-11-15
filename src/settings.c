/* settings.c — beállítások: háttérkép, ikontéma, kiemelőszín, nyelv, frissítési URL.
 * KERINGO.CFG: "kulcs=ertek" sorok; betöltés /sys, /usb, /cd közül. */
#include "kernel.h"

static int kstreq(const char *a, const char *b);

settings_t settings = {
    0, "HATTER1.JPG", 0, 0, 0,
    "http://10.0.2.2:8000/KERINGO.UPD",
    ""
};

/* kiemelőszín-paletták */
u32 settings_accent_color(void) {
    static const u32 pal[6] = {
        0xFF248BBE,   /* égkék (alap) */
        0xFFE8B24A,   /* borostyán */
        0xFF62D26F,   /* zöld */
        0xFFE86A6A,   /* koralpiros */
        0xFFB07AE8,   /* lila */
        0xFF5AD2C8,   /* türkiz */
    };
    if (settings.accent < 0) settings.accent = 0;
    if (settings.accent > 5) settings.accent = 5;
    return pal[settings.accent];
}

const char *settings_wp_names(int i) {
    static const char *n[2][4] = {
        { "Csillagmező", "Köd", "Színátmenet", "Fotó" },
        { "Starfield", "Nebula", "Gradient", "Photo" },
    };
    int l = settings.lang & 1;
    if (i < 0 || i > 3) return "?";
    return n[l][i];
}

const char *settings_icon_names(int i) {
    static const char *n[2][3] = {
        { "Keringő", "Pixel", "Vonal" },
        { "Orbit", "Pixel", "Line" },
    };
    int l = settings.lang & 1;
    if (i < 0 || i > 2) return "?";
    return n[l][i];
}

static int parse_int(const char *v) {
    u32 n = 0; int neg = 0;
    if (*v == '-') { neg = 1; v++; }
    while (*v >= '0' && *v <= '9') n = n * 10 + (u32)(*v++ - '0');
    return neg ? -(int)n : (int)n;
}

void settings_load(void) {
    static char cfg[2048];
    u32 got;
    if (!vfs_read_any((const u8 *)"KERINGO.CFG", (u8 *)cfg, sizeof(cfg) - 1, &got)) return;
    cfg[got] = 0;
    /* egyszerű szkenner: kulcs=sor elején */
    char *p = cfg;
    char key[24], val[96];
    while (*p) {
        char *eol = p;
        while (*eol && *eol != '\n') eol++;
        char save = *eol;
        *eol = 0;
        int ki = 0;
        while (p[ki] && p[ki] != '=' && ki < 23) { key[ki] = p[ki]; ki++; }
        key[ki] = 0;
        if (p[ki] == '=') {
            int vi = 0;
            const char *v = p + ki + 1;
            while (*v && vi < 95) val[vi++] = *v++;
            val[vi] = 0;
            if (kstreq(key, "wallpaper")) settings.wallpaper = parse_int(val);
            else if (kstreq(key, "icon_theme")) settings.icon_theme = parse_int(val);
            else if (kstreq(key, "accent")) settings.accent = parse_int(val);
            else if (kstreq(key, "lang")) settings.lang = parse_int(val) & 1;
            else if (kstreq(key, "update_url")) {
                int j;
                for (j = 0; j < 95 && val[j]; j++) settings.update_url[j] = val[j];
                settings.update_url[j] = 0;
            }
            else if (kstreq(key, "wp_file")) {
                int j;
                for (j = 0; j < 63 && val[j]; j++) settings.wp_file[j] = val[j];
                settings.wp_file[j] = 0;
            }
        }
        *eol = save;
        p = eol;
        if (*p == '\n') p++;
    }
    kprintf("beallitasok: betoltve (hatter=%d ikon=%d szin=%d)\n",
            settings.wallpaper, settings.icon_theme, settings.accent);
}

static int kstreq(const char *a, const char *b);

int settings_save(void) {
    char cfg[1024];
    ksnprintf(cfg, sizeof(cfg),
              "wallpaper=%d\nwp_file=%s\nicon_theme=%d\naccent=%d\nlang=%d\nupdate_url=%s\n",
              settings.wallpaper, settings.wp_file, settings.icon_theme,
              settings.accent, settings.lang, settings.update_url);
    int len = 0;
    while (cfg[len]) len++;
    if (sys_mounted() && vfs_write_file("/sys/KERINGO.CFG", cfg, len)) return 1;
    if (usbfs_mounted() && vfs_write_file("/usb/KERINGO.CFG", cfg, len)) return 2;
    if (vfs_write_file("/mem/KERINGO.CFG", cfg, len)) return 3;
    return 0;
}

/* jegyzet perzisztenciája */
void settings_note_save(const char *note, int len) {
    if (len > 599) len = 599;
    for (int i = 0; i < len; i++) settings.note[i] = note[i];
    settings.note[len] = 0;
    char path[64];
    if (sys_mounted()) {
        ksnprintf(path, sizeof(path), "/sys/jegyzet.txt");
        vfs_write_file(path, note, len);
    }
}

int settings_note_load(char *note, int max) {
    int outlen;
    if (sys_mounted() && vfs_read_file("/sys/jegyzet.txt", note, max, &outlen)) return outlen;
    if (usbfs_mounted() && vfs_read_file("/usb/jegyzet.txt", note, max, &outlen)) return outlen;
    return 0;
}

static int kstreq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
