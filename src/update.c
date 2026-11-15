/* update.c — KERINGO.UPD csomagkezelés: keresés (USB/hálózat), CRC-ellenőrzés,
 * telepítés a /sys partícióra, biztonsági mentés + visszaállítás, élő kexec. */
#include "kernel.h"
#include "net.h"

#define UPD_MAXFILES 8
#define UPD_MAXSIZE  (4 * 1024 * 1024)

typedef struct {
    char magic[8];
    u32  file_count;
    char version[32];
    u32  header_crc;                            /* bájt 0..43 CRC-je */
} __attribute__((packed)) upd_hdr_t;

typedef struct {
    char name[64];
    u32  size;
    u32  crc32;
    u32  offset;
} __attribute__((packed)) upd_ent_t;

static u8 *pkg = 0;
static u32 pkg_len = 0;
static upd_hdr_t hdr;
static upd_ent_t ents[UPD_MAXFILES];
static char pkg_version[40];
static int pkg_ok = 0;
static int pkg_source = 0;                      /* 0 = USB, 1 = hálózat */
static char status_msg[96];

const char *update_version(void) { return pkg_ok ? pkg_version : 0; }
const char *update_status(void)  { return status_msg; }

static void set_status(const char *msg) {
    int i;
    for (i = 0; i < 95 && msg[i]; i++) status_msg[i] = msg[i];
    status_msg[i] = 0;
}

/* csomag-ellenőrzés */
static int upd_parse(void) {
    pkg_ok = 0;
    if (pkg_len < sizeof(upd_hdr_t)) { set_status("A csomag túl kicsi"); return 0; }
    for (int i = 0; i < 8; i++)
        if (pkg[i] != "KRNGUPD1"[i]) { set_status("Rossz csomag-aláírás"); return 0; }
    u32 hcrc = crc32_calc(0, pkg, 44);
    if (hcrc != *(const u32 *)(pkg + 44)) { set_status("Fejléc-CRC hiba"); return 0; }
    hdr = *(const upd_hdr_t *)pkg;
    if (hdr.file_count == 0 || hdr.file_count > UPD_MAXFILES) { set_status("Fájlszám hiba"); return 0; }
    u32 off = sizeof(upd_hdr_t);
    for (u32 i = 0; i < hdr.file_count; i++) {
        ents[i] = *(const upd_ent_t *)(pkg + off);
        off += sizeof(upd_ent_t);
        if (ents[i].size > UPD_MAXSIZE) { set_status("Túl nagy fájl a csomagban"); return 0; }
        if (ents[i].offset + ents[i].size > pkg_len) { set_status("Csomag csonkolt"); return 0; }
        u32 c = crc32_calc(0, pkg + ents[i].offset, ents[i].size);
        if (c != ents[i].crc32) { set_status("Fájl-CRC hiba"); return 0; }
    }
    for (int i = 0; i < 39 && hdr.version[i]; i++) pkg_version[i] = hdr.version[i];
    pkg_version[39] = 0;
    pkg_ok = 1;
    set_status("Csomag rendben");
    return 1;
}

int update_scan_usb(void) {
    pkg_ok = 0;
    if (!usbfs_mounted()) { set_status("Nincs pendrájv csatlakoztatva"); return 0; }
    static u8 *buf = 0;
    if (!buf) buf = (u8 *)kmalloc(UPD_MAXSIZE);
    pkg = buf;
    int outlen;
    if (!vfs_read_file("/usb/KERINGO.UPD", (char *)pkg, UPD_MAXSIZE, &outlen)) {
        if (!vfs_read_file("/usb/keringo.upd", (char *)pkg, UPD_MAXSIZE, &outlen)) {
            set_status("Nincs KERINGO.UPD a pendrájvon");
            return 0;
        }
    }
    pkg_len = (u32)outlen;
    pkg_source = 0;
    kprintf("update: csomag betoltve a pendrajvrol (%u B)\n", pkg_len);
    return upd_parse();
}

int update_scan_http(const char *url) {
    pkg_ok = 0;
    if (!net_up()) { set_status("Nincs hálózat"); return 0; }
    http_result_t res;
    int r = http_fetch(url, &res);
    if (r != 1 || !res.body || res.body_len == 0) { set_status("Letöltés nem sikerült"); return 0; }
    if (res.body_len > UPD_MAXSIZE) { set_status("Csomag túl nagy"); return 0; }
    static u8 *buf = 0;
    if (!buf) buf = (u8 *)kmalloc(UPD_MAXSIZE);
    for (u32 i = 0; i < res.body_len; i++) buf[i] = res.body[i];
    pkg = buf;
    pkg_len = res.body_len;
    pkg_source = 1;
    kprintf("update: csomag letoltve halozatrol (%u B)\n", pkg_len);
    return upd_parse();
}

/* verzió-összehasonlítás: 1 ha az a > b */
static int ver_newer(const char *a, const char *b) {
    u32 av[4] = {0,0,0,0}, bv[4] = {0,0,0,0};
    int ai = 0, bi = 0;
    for (const char *p = a; *p && ai < 4; ) {
        if (*p >= '0' && *p <= '9') { av[ai] = av[ai] * 10 + (u32)(*p - '0'); }
        else if (*p == '.') ai++;
        p++;
    }
    for (const char *p = b; *p && bi < 4; ) {
        if (*p >= '0' && *p <= '9') { bv[bi] = bv[bi] * 10 + (u32)(*p - '0'); }
        else if (*p == '.') bi++;
        p++;
    }
    for (int i = 0; i < 4; i++) {
        if (av[i] != bv[i]) return av[i] > bv[i];
    }
    return 0;
}

int update_available_newer(void) {
    return pkg_ok && ver_newer(pkg_version, KERINGO_VERZIO);
}

int update_nfiles(void) { return pkg_ok ? (int)hdr.file_count : 0; }
const char *update_fname(int i) { return (i >= 0 && i < (int)hdr.file_count) ? ents[i].name : ""; }
u32 update_fsize(int i) { return (i >= 0 && i < (int)hdr.file_count) ? ents[i].size : 0; }

/* név-egyezés kis-nagybetűt nem számítva */
static int nm_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
        char cb = *b >= 'b' && *b <= 'z' ? *b - 32 : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* telepítés a /sys partícióra */
int update_apply(int live) {
    if (!pkg_ok) return 0;
    if (!sys_mounted()) { set_status("Nincs /sys rendszerpartíció!"); return 0; }
    char path[96];
    int kernel_idx = -1;
    /* régi kernel mentése KERINGO.BAK-ba */
    for (u32 i = 0; i < hdr.file_count; i++) {
        if (nm_eq(ents[i].name, "BOOT/KERINGO.ELF") || nm_eq(ents[i].name, "KERINGO.ELF")) kernel_idx = (int)i;
    }
    if (kernel_idx >= 0) {
        static u8 *cur = 0;
        if (!cur) cur = (u8 *)kmalloc(4 * 1024 * 1024);
        int outlen;
        if (vfs_read_file("/sys/BOOT/KERINGO.ELF", (char *)cur, 4 * 1024 * 1024, &outlen)) {
            vfs_write_file("/sys/BOOT/KERINGO.BAK", (const char *)cur, outlen);
            kprintf("update: regi kernel mentve BAK-ba (%d B)\n", outlen);
        }
    }
    /* fájlok kiírása */
    for (u32 i = 0; i < hdr.file_count; i++) {
        ksnprintf(path, sizeof(path), "/sys/%s", ents[i].name);
        kprintf("update: %s -> %s (%u B)\n", ents[i].name, path, ents[i].size);
        if (!vfs_write_file(path, (const char *)(pkg + ents[i].offset), (int)ents[i].size)) {
            kprintf("update: fajl iras NEM SIKERULT\n");
            set_status("Hiba: fájlírás a /sys-re meghiúsult");
            return 0;
        }
    }
    /* gyorsítótárak kiürítése */
    usb_flush();
    set_status("Telepítés kész!");
    kprintf("update: telepites kesz\n");
    if (live && kernel_idx >= 0) {
        kprintf("update: ELŐ KEXEC inditas...\n");
        return kexec_load_and_boot(pkg + ents[kernel_idx].offset, ents[kernel_idx].size);
    }
    return 1;
}

/* visszaállítás a KERINGO.BAK-ból */
int update_rollback(void) {
    if (!sys_mounted()) { set_status("Nincs /sys partíció"); return 0; }
    static u8 *cur = 0;
    if (!cur) cur = (u8 *)kmalloc(4 * 1024 * 1024);
    int outlen;
    if (!vfs_read_file("/sys/BOOT/KERINGO.BAK", (char *)cur, 4 * 1024 * 1024, &outlen)) {
        set_status("Nincs KERINGO.BAK mentés");
        return 0;
    }
    if (!vfs_write_file("/sys/BOOT/KERINGO.ELF", (const char *)cur, outlen)) {
        set_status("Visszaírás nem sikerült");
        return 0;
    }
    usb_flush();
    set_status("Visszaállítás kész (régi verzió)");
    return 1;
}
