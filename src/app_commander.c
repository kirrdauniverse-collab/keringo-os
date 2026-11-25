/* app_commander.c — Keringő Commander (0.5): duplapaneles fájlkezelő.
 * Panelek: /cd, /mem, /sys, /usb (F1-F4 váltja a paneleken).
 * Műveletek: másolás (F5), átnevezés (F6), törlés (F8), nézet (F3: hex/dec/bin/karakter
 * — szerkesztéssel!), szöveges szerkesztő (F4), archiválás (.KPA, F7) és kicsomagolás.
 * A .KPA saját formátum: RLE-tömörítés, fájllista a fejlécben. */
#include "kernel.h"

#define MAXE 64
typedef struct {
    char path[64];                 /* "/cd" | "/sys" | "/sys/DIR" ... */
    char names[MAXE][64];
    u32  sizes[MAXE];
    u8   dirs[MAXE];
    int  n, sel, top;
    u8   mark[MAXE];
    char dev;                      /* 0 cd, 1 mem, 2 sys, 3 usb */
} pane_t;
static pane_t panes[2];
static int curp = 0;

/* nézőke/szerkesztő mód */
#define V_HEX 0
#define V_DEC 1
#define V_BIN 2
#define V_ASC 3
static int viewmode = 0;
static int viewer_on = 0, editor_on = 0;
static char vfile[72];
static u8  vbuf[512 * 1024];
static int vlen = 0, voff = 0, vcur = 0;
static int vdirty = 0;

/* átnevezéshez bevitel */
static int renmode = 0;
static char renbuf[24]; static int renlen = 0;
static char status[128] = "F1-F4: paneleszköz | Tab: másik panel | F3 nézet | F4 szerkeszt | F5 másol | F6 nevez | F7 csomag | F8 töröl";

/* ---------- panelfrissítés ---------- */
static char *dev_roots[4] = { "/cd", "/mem", "/sys", "/usb" };
static int dev_mounted(int d) {
    if (d == 0) return iso_mounted();
    if (d == 1) return 1;
    if (d == 2) return sys_mounted();
    return usbfs_mounted();
}
static void pane_refresh(pane_t *p) {
    int n = vfs_list(p->path, p->names, p->sizes, p->dirs, MAXE);
    p->n = n > 0 ? n : 0;
    if (p->sel >= p->n) p->sel = p->n - 1;
    if (p->sel < 0) p->sel = 0;
    for (int i = 0; i < MAXE; i++) p->mark[i] = 0;
}
static void pane_setdev(pane_t *p, int d) {
    if (d < 0 || d > 3 || !dev_mounted(d)) return;
    p->dev = (char)d;
    ksnprintf(p->path, sizeof(p->path), "%s", dev_roots[d]);
    pane_refresh(p);
}

/* ---------- .KPA archívum (RLE) ---------- */
static int rle_pack(const u8 *src, int len, u8 *dst, int dstmax) {
    int o = 0, i = 0;
    while (i < len && o < dstmax - 2) {
        u8 b = src[i]; int run = 1;
        while (i + run < len && src[i + run] == b && run < 255) run++;
        if (run >= 3) { dst[o++] = (u8)run; dst[o++] = b; i += run; }
        else { for (int k = 0; k < run && o < dstmax - 1; k++) { dst[o++] = 1; dst[o++] = b; } i += run; }
    }
    return o < dstmax ? o : -1;
}
static int rle_unpack(const u8 *src, int len, u8 *dst, int dstmax) {
    int o = 0, i = 0;
    while (i + 1 < len && o < dstmax) {
        int run = src[i++];
        u8 b = src[i++];
        for (int k = 0; k < run && o < dstmax; k++) dst[o++] = b;
    }
    return o;
}

/* kijelölt fájl elérési útja a pane-ből */
static void pane_file(pane_t *p, int i, char *out, int max) {
    ksnprintf(out, max, "%s/%s", p->path, p->names[i]);
}

/* fájlmásolás pane->pane */
static void copy_sel(void) {
    pane_t *src = &panes[curp], *dst = &panes[curp ^ 1];
    static char msg[96];
    int copied = 0;
    for (int i = 0; i < src->n; i++) {
        if (!(src->mark[i] || i == src->sel) || src->dirs[i]) continue;
        char sp[128], dp[128];
        pane_file(src, i, sp, sizeof(sp));
        ksnprintf(dp, sizeof(dp), "%s/%s", dst->path, src->names[i]);
        int out = 0;
        if (!vfs_read_file(sp, (char *)vbuf, (int)sizeof(vbuf), &out)) {
            ksnprintf(status, sizeof(status), "olvasasi hiba: %s", sp);
            return;
        }
        if (!vfs_write_file(dp, (const char *)vbuf, out)) {
            ksnprintf(status, sizeof(status), "iras hiba: %s (olvashato?)", dp);
            return;
        }
        copied++;
        (void)msg;
    }
    ksnprintf(status, sizeof(status), "atmasolva: %d fajl", copied);
    pane_refresh(dst);
    sound_beep(880, 3);
}

/* ---------- nézet/szerkesztő ---------- */
static void view_open(pane_t *p, int i) {
    char fp[128];
    pane_file(p, i, fp, sizeof(fp));
    int out = 0;
    if (!vfs_read_file(fp, (char *)vbuf, (int)sizeof(vbuf), &out) || out <= 0) {
        ksnprintf(status, sizeof(status), "nem nyithato: %s", fp);
        return;
    }
    vlen = out;
    voff = 0; vcur = 0;
    ksnprintf(vfile, sizeof(vfile), "%s", fp);
    viewer_on = 1; editor_on = 0; vdirty = 0;
}
static void view_save(void) {
    if (!vdirty) return;
    if (!vfs_write_file(vfile, (const char *)vbuf, vlen)) {
        ksnprintf(status, sizeof(status), "MENTES SIKERTELEN (olvashato?)");
        return;
    }
    vdirty = 0;
    ksnprintf(status, sizeof(status), "mentve: %s", vfile);
    pane_refresh(&panes[0]); pane_refresh(&panes[1]);
}

/* 16 bájt/sor hexnézet */
static void draw_viewer(void) {
    int W = (int)gfx_width(), H = (int)gfx_height();
    gfx_fill_rect(0, 22, W, H - 44, 0xFF10151A);
    u32 accent = settings_accent_color();
    char b[96];
    const char *mn[] = { "HEX", "DEC", "BIN", "KARAKTER" };
    ksnprintf(b, sizeof(b), "NEZOK: %s | V: valtas | nyilak: mozgas | szamjegy: szerkesztes | F2: ment | Esc: ki", mn[viewmode]);
    gfx_draw_text(12, 32, vfile, accent, 0xFF000000u);
    gfx_draw_text(12, 46, b, 0xFF8FA0AC, 0xFF000000u);
    int rows = (H - 100) / 16;
    for (int r = 0; r < rows; r++) {
        int off = voff + r * 16;
        if (off >= vlen) break;
        char ln[128];
        ksnprintf(ln, sizeof(ln), "%08X  ", (u32)off);
        int px = 12;
        gfx_draw_text(px, 64 + r * 16, ln, 0xFF8FA0AC, 0xFF000000u);
        px += 10 * 8 + 8;
        for (int c = 0; c < 16 && off + c < vlen; c++) {
            u8 v = vbuf[off + c];
            char cell[12];
            if (viewmode == V_HEX) ksnprintf(cell, sizeof(cell), "%02X", v);
            else if (viewmode == V_DEC) ksnprintf(cell, sizeof(cell), "%3u", (unsigned)v);
            else if (viewmode == V_BIN) {
                cell[8] = 0;
                for (int k = 0; k < 8; k++) cell[k] = (v & (0x80 >> k)) ? '1' : '.';
            } else ksnprintf(cell, sizeof(cell), "%c", v >= 32 && v < 127 ? v : '.');
            int selected = (off + c == voff + vcur);
            u32 fg = selected ? 0xFF10151A : 0xFFF0F0F0;
            u32 bg = selected ? 0xFFE8B24A : 0xFF10151A;
            int cw = viewmode == V_DEC ? 3 : viewmode == V_BIN ? 8 : 2;
            int wpx = cw * 8 + 8;
            gfx_fill_rect(px, 62 + r * 16, wpx, 14, bg);
            gfx_draw_text(px + 4, 64 + r * 16, cell, fg, bg);
            px += wpx + 2;
        }
        /* ASCII oszlop hex módban */
        if (viewmode == V_HEX) {
            px += 8;
            for (int c = 0; c < 16 && off + c < vlen; c++) {
                char s[2] = { (char)(vbuf[off + c] >= 32 && vbuf[off + c] < 127 ? vbuf[off + c] : '.'), 0 };
                gfx_draw_text(px, 64 + r * 16, s, 0xFF5AD2C8, 0xFF000000u);
                px += 8;
            }
        }
    }
    char st[96];
    ksnprintf(st, sizeof(st), "%d / %d bájt%s", vcur + 1, vlen, vdirty ? " * MÓDOSÍTVA" : "");
    app_chrome("Keringő Commander — nézetke", st, accent);
}

/* ---------- egyszerű szövegszerkesztő a fájlra ---------- */
/* a vbuf-ban \n-elvált sorok; kurzor = bájtpozíció */
static void editor_open(pane_t *p, int i) {
    view_open(p, i);
    if (viewer_on) { viewer_on = 0; editor_on = 1; }
}
static void editor_key(u8 sc, u8 rel, u8 ascii) {
    if (rel) return;
    if (sc == KEY_ESC) { editor_on = 0; return; }
    if (sc == KEY_F2) { view_save(); return; }
    if (sc == KEY_LEFT && vcur > 0) vcur--;
    if (sc == KEY_RIGHT && vcur < vlen - 1) vcur++;
    if (sc == KEY_UP) { int k = vcur - 1; while (k >= 0 && vbuf[k] != '\n') k--; vcur = k + 1 > 0 ? k + 1 : 0; }
    if (sc == KEY_DOWN) { int k = vcur; while (k < vlen && vbuf[k] != '\n') k++; vcur = k < vlen ? k + 1 : vlen; }
    if (vcur > vlen) vcur = vlen;
    if (ascii == '\b') {
        if (vcur > 0) { for (int k = vcur - 1; k < vlen - 1; k++) vbuf[k] = vbuf[k + 1]; vlen--; vcur--; vdirty = 1; }
    } else if (ascii == '\n') {
        if (vlen < (int)sizeof(vbuf) - 1) { for (int k = vlen; k > vcur; k--) vbuf[k] = vbuf[k - 1]; vbuf[vcur++] = '\n'; vlen++; vdirty = 1; }
    } else if (ascii >= 32 && ascii < 127) {
        if (vlen < (int)sizeof(vbuf) - 1) { for (int k = vlen; k > vcur; k--) vbuf[k] = vbuf[k - 1]; vbuf[vcur++] = ascii; vlen++; vdirty = 1; }
    }
}
static void draw_editor(void) {
    int W = (int)gfx_width(), H = (int)gfx_height();
    gfx_fill_rect(0, 22, W, H - 44, 0xFF10151A);
    u32 accent = settings_accent_color();
    gfx_draw_text(12, 32, vfile, accent, 0xFF000000u);
    gfx_draw_text(12, 46, "Szovegszerkeszto | F2: ment | Esc: ki", 0xFF8FA0AC, 0xFF000000u);
    int cols = (W - 24) / 8;
    int rows = (H - 108) / 16;
    int line = 0, col = 0, shown = 0, k = 0;
    int curline = 0, curlinebeg = 0;
    for (int q = 0; q < vcur; q++) { if (vbuf[q] == '\n') curline++; }
    int first = curline - rows + 2; if (first < 0) first = 0;
    k = 0; line = 0;
    while (k < vlen && line < first) { if (vbuf[k++] == '\n') line++; }
    for (int r = 0; r < rows && k < vlen; r++) {
        int x = 12;
        for (int c = 0; c < cols && k < vlen; c++, k++) {
            char s[2] = { (char)(vbuf[k] == '\n' ? ' ' : vbuf[k]), 0 };
            if (k == vcur && (tick_count() / 25) % 2 == 0)
                gfx_fill_rect(x, 64 + r * 16, 8, 14, 0xFFE8B24A);
            gfx_draw_text(x, 64 + r * 16, s, k == vcur ? 0xFF10151A : 0xFFF0F0F0,
                          k == vcur ? 0xFFE8B24A : 0xFF10151A);
            x += 8;
            if (vbuf[k] == '\n') break;
        }
        if (k >= vlen) break;
        k++; line++; shown++;
    }
    (void)shown;
    app_chrome("Keringő Commander — szerkesztő", status, accent);
}

/* ---------- archiválás / kicsomagolás ---------- */
static void pack_marked(void) {
    pane_t *src = &panes[curp], *dst = &panes[curp ^ 1];
    static u8 arch[512 * 1024];
    int ao = 4 + 2;
    arch[0]='K'; arch[1]='P'; arch[2]='A'; arch[3]='1';
    int nfiles = 0;
    for (int i = 0; i < src->n; i++) {
        if (!(src->mark[i] || i == src->sel) || src->dirs[i]) continue;
        char fp[128];
        pane_file(src, i, fp, sizeof(fp));
        int out = 0;
        if (!vfs_read_file(fp, (char *)vbuf, (int)sizeof(vbuf), &out)) { ksnprintf(status, sizeof(status), "olvasas: %s", fp); return; }
        u8 tmp[512 * 1024];
        int clen = rle_pack(vbuf, out, tmp, (int)sizeof(tmp));
        if (clen < 0) { ksnprintf(status, sizeof(status), "túl nagy: %s", src->names[i]); return; }
        int nl = 0; while (src->names[i][nl]) nl++;
        if (ao + 1 + nl + 8 + clen > (int)sizeof(arch)) { ksnprintf(status, sizeof(status), "az archivum tuls nagy"); return; }
        arch[ao++] = (u8)nl;
        for (int k = 0; k < nl; k++) arch[ao++] = (u8)src->names[i][k];
        *(u32 *)(arch + ao) = (u32)out; ao += 4;
        *(u32 *)(arch + ao) = (u32)clen; ao += 4;
        for (int k = 0; k < clen; k++) arch[ao++] = tmp[k];
        nfiles++;
    }
    *(u16 *)(arch + 4) = (u16)nfiles;
    if (!nfiles) { ksnprintf(status, sizeof(status), "nincs kijelolt fajl (Space = jeloles)"); return; }
    char ap[128];
    ksnprintf(ap, sizeof(ap), "%s/CSOMAG.KPA", dst->path);
    if (!vfs_write_file(ap, (const char *)arch, ao)) { ksnprintf(status, sizeof(status), "iras hiba"); return; }
    ksnprintf(status, sizeof(status), "CSOMAG.KPA: %d fajl, %d KB -> RLE %d KB", nfiles, ao / 1024, ao / 1024);
    pane_refresh(dst);
    sound_tune((const u16[]){ 523, 5, 659, 5, 784, 8 }, 3);
}
static void unpack_selected(void) {
    pane_t *src = &panes[curp];
    if (src->sel >= src->n || src->dirs[src->sel]) { ksnprintf(status, sizeof(status), "valassz .KPA-t"); return; }
    char fp[128];
    pane_file(src, src->sel, fp, sizeof(fp));
    static u8 arch[512 * 1024];
    int out = 0;
    if (!vfs_read_file(fp, (char *)arch, (int)sizeof(arch), &out) || out < 8) { ksnprintf(status, sizeof(status), "hibas KPA"); return; }
    if (!(arch[0]=='K' && arch[1]=='P' && arch[2]=='A' && arch[3]=='1')) { ksnprintf(status, sizeof(status), "nem KPA1"); return; }
    int n = *(u16 *)(arch + 4);
    int ao = 6;
    int done = 0;
    for (int f = 0; f < n && ao + 1 < out; f++) {
        int nl = arch[ao++];
        char name[64];
        for (int k = 0; k < nl && k < 63; k++) name[k] = (char)arch[ao++];
        name[nl < 63 ? nl : 63] = 0;
        u32 sz = *(u32 *)(arch + ao); ao += 4;
        u32 cs = *(u32 *)(arch + ao); ao += 4;
        if (rle_unpack(arch + ao, (int)cs, vbuf, (int)sizeof(vbuf)) != (int)sz) { ksnprintf(status, sizeof(status), "hibas adat: %s", name); return; }
        ao += (int)cs;
        char dp[128];
        ksnprintf(dp, sizeof(dp), "%s/%s", src->path, name);
        if (!vfs_write_file(dp, (const char *)vbuf, (int)sz)) { ksnprintf(status, sizeof(status), "iras: %s", name); return; }
        done++;
    }
    ksnprintf(status, sizeof(status), "kicsomagolva: %d fajl", done);
    pane_refresh(src);
    pane_refresh(&panes[curp ^ 1]);
}

/* ---------- app-keretrendszer ---------- */
void commander_enter(void) {
    pane_setdev(&panes[0], 0);
    if (sys_mounted()) pane_setdev(&panes[1], 2);
    else if (usbfs_mounted()) pane_setdev(&panes[1], 3);
    else pane_setdev(&panes[1], 1);
    viewer_on = editor_on = renmode = 0;
    sound_beep(880, 4);
}
void commander_exit(void) { viewer_on = editor_on = renmode = 0; }

void commander_key(u8 sc, u8 rel, u8 ascii) {
    /* átnevezés */
    if (renmode) {
        if (sc == KEY_ESC) { renmode = 0; return; }
        if (sc == KEY_ENTER) {
            renbuf[renlen] = 0;
            pane_t *p = &panes[curp];
            if (p->sel < p->n) {
                char fp[128], newname[24];
                pane_file(p, p->sel, fp, sizeof(fp));
                for (int k = 0; k < 23 && renbuf[k]; k++) newname[k] = renbuf[k];
                newname[renlen < 23 ? renlen : 23] = 0;
                if (p->dev == 2 && fat32_rename(vfs_sys_fs(), fp + 5, newname)) {
                    ksnprintf(status, sizeof(status), "atnevezve: %s", newname);
                    pane_refresh(p);
                } else if (p->dev == 3 && fat32_rename(vfs_usb_fs(), fp + 5, newname)) {
                    ksnprintf(status, sizeof(status), "atnevezve: %s", newname);
                    pane_refresh(p);
                } else ksnprintf(status, sizeof(status), "atnevezes nem lehetseges itt");
            }
            renmode = 0;
            return;
        }
        if (ascii == '\b') { if (renlen > 0) renbuf[--renlen] = 0; return; }
        if (ascii >= 32 && ascii < 127 && renlen < 23) { renbuf[renlen++] = ascii; renbuf[renlen] = 0; }
        return;
    }
    /* nézetke */
    if (viewer_on) {
        if (rel) return;
        if (sc == KEY_ESC) { viewer_on = 0; return; }
        if (sc == KEY_F2) { view_save(); return; }
        if (ascii == 'v' || ascii == 'V') { viewmode = (viewmode + 1) & 3; sound_beep(700, 2); return; }
        int rows = ((int)gfx_height() - 100) / 16;
        if (sc == KEY_UP && vcur >= 16) { vcur -= 16; if (vcur < voff) voff = vcur; }
        if (sc == KEY_DOWN && vcur + 16 < vlen) { vcur += 16; if (vcur >= voff + rows * 16) voff = vcur - (rows - 1) * 16; }
        if (sc == KEY_LEFT && vcur > 0) { vcur--; if (vcur < voff) voff = vcur; }
        if (sc == KEY_RIGHT && vcur < vlen - 1) { vcur++; if (vcur >= voff + rows * 16) voff += 16; }
        if (sc == KEY_PGUP && voff >= rows * 16) { voff -= rows * 16; vcur = voff; }
        if (sc == KEY_PGDN && voff + rows * 16 < vlen) { voff += rows * 16; vcur = voff; }
        /* szerkesztés hex módban: számjegyek */
        if (viewmode == V_HEX && ((ascii >= '0' && ascii <= '9') || (ascii >= 'A' && ascii <= 'F'))) {
            u8 d = (ascii >= '0' && ascii <= '9') ? (u8)(ascii - '0') : (u8)(ascii - 'A' + 10);
            u8 cur = vbuf[vcur];
            vbuf[vcur] = (u8)((cur & 0x0F) | (d << 4));
            vdirty = 1;
            if (vcur < vlen - 1) vcur++;
        }
        if (vcur < 0) vcur = 0;
        if (vcur >= vlen) vcur = vlen - 1;
        return;
    }
    /* szövegszerkesztő */
    if (editor_on) { editor_key(sc, rel, ascii); return; }

    if (rel) return;
    pane_t *p = &panes[curp];
    switch (sc) {
        case KEY_TAB: curp ^= 1; sound_beep(600, 2); break;
        case KEY_UP:   if (p->sel > 0) p->sel--; if (p->sel < p->top) p->top = p->sel; break;
        case KEY_DOWN: if (p->sel < p->n - 1) p->sel++; if (p->sel >= p->top + 24) p->top = p->sel - 23; break;
        case KEY_F1: pane_setdev(p, 0); break;
        case KEY_F2: pane_setdev(p, 1); break;
        case KEY_F3:
            if (p->sel < p->n && !p->dirs[p->sel]) view_open(p, p->sel);
            break;
        case KEY_F4:
            if (p->sel < p->n && !p->dirs[p->sel]) editor_open(p, p->sel);
            break;
        case KEY_F5: copy_sel(); break;
        case KEY_F6:
            if (p->sel < p->n && !p->dirs[p->sel]) { renmode = 1; renlen = 0; renbuf[0] = 0; }
            break;
        case KEY_F7:
            if (p->sel < p->n && p->n > 0) {
                int iskpa = 0;
                const char *nm = p->names[p->sel];
                int l = 0; while (nm[l]) l++;
                if (l > 4 && nm[l-4]=='.' && (nm[l-3]=='K'||nm[l-3]=='k') && (nm[l-2]=='P'||nm[l-2]=='p') && (nm[l-1]=='A'||nm[l-1]=='a')) iskpa = 1;
                if (iskpa) unpack_selected();
                else pack_marked();
            }
            break;
        case KEY_F8: {
            if (p->sel < p->n && !p->dirs[p->sel]) {
                char fp[128];
                pane_file(p, p->sel, fp, sizeof(fp));
                int ok = 0;
                if (p->dev == 2) ok = fat32_delete(vfs_sys_fs(), fp + 5);
                else if (p->dev == 3) ok = fat32_delete(vfs_usb_fs(), fp + 5);
                if (ok) { ksnprintf(status, sizeof(status), "torolve: %s", p->names[p->sel]); pane_refresh(p); sound_beep(300, 4); }
                else ksnprintf(status, sizeof(status), "itt nem torolheto (cd?)");
            }
            break; }
        case KEY_ENTER: {
            if (p->sel >= p->n) break;
            if (p->dirs[p->sel]) {
                char np[96];
                ksnprintf(np, sizeof(np), "%s/%s", p->path, p->names[p->sel]);
                ksnprintf(p->path, sizeof(p->path), "%s", np);
                pane_refresh(p);
                p->sel = 0; p->top = 0;
            } else view_open(p, p->sel);
            break; }
    }
    if (ascii == ' ') {
        if (p->sel < p->n) p->mark[p->sel] ^= 1;
    }
}

void commander_frame(u32 t) {
    (void)t;
    u32 accent = settings_accent_color();
    if (viewer_on) { draw_viewer(); return; }
    if (editor_on) { draw_editor(); return; }
    int W = (int)gfx_width(), H = (int)gfx_height();
    gfx_fill_rect(0, 22, W, H - 44, 0xFF10151A);
    int pw = (W - 40) / 2;
    for (int pi = 0; pi < 2; pi++) {
        pane_t *p = &panes[pi];
        int x = 8 + pi * (pw + 24);
        int y = 26;
        u32 bcol = pi == curp ? accent : 0xFF3A5562;
        gfx_fill_rect(x, y, pw, H - 92, 0xFF151E28);
        gfx_rect(x, y, pw, H - 92, bcol);
        gfx_fill_rect(x, y, pw, 18, pi == curp ? accent : 0xFF233040);
        char h[80];
        ksnprintf(h, sizeof(h), " %s", p->path);
        gfx_draw_text(x + 4, y + 5, h, 0xFF10151A, 0xFF000000u);
        /* lista */
        for (int i = p->top; i < p->n && i < p->top + 24; i++) {
            int ry = y + 26 + (i - p->top) * 15;
            if (i == p->sel) gfx_fill_rect(x + 2, ry - 1, pw - 4, 14, 0xFF1C2A38);
            char ln[80];
            ksnprintf(ln, sizeof(ln), "%s%s %7u", p->mark[i] ? "*" : " ", p->names[i], p->sizes[i]);
            gfx_draw_text(x + 8, ry, ln,
                          p->dirs[i] ? 0xFF5AD2C8 : (i == p->sel ? 0xFFE8B24A : 0xFFF0F0F0),
                          0xFF000000u);
        }
        if (p->n == 0) gfx_draw_text(x + 8, y + 30, "(ures vagy nincs csatolva)", 0xFF5A6A76, 0xFF000000u);
    }
    /* alsó állapotsor */
    gfx_draw_text(8, (int)gfx_height() - 60, status, 0xFF8FA0AC, 0xFF000000u);
    gfx_draw_text(8, (int)gfx_height() - 44 + 2, ui_lang() ? "F1-F4: cd/mem/sys/usb  F5 copy  F6 rename  F7 pack/unpack  F8 delete" :
                                                            "F1-F4: cd/mem/sys/usb  F5 masol  F6 atnevez  F7 csomag  F8 torol", 0xFF8FA0AC, 0xFF000000u);
    if (renmode) {
        gfx_fill_rect(8, (int)gfx_height() - 84, 300, 24, 0xFF0A1018);
        gfx_rect(8, (int)gfx_height() - 84, 300, 24, accent);
        gfx_draw_text(16, (int)gfx_height() - 78, "UJ NEV: ", 0xFFF0F0F0, 0xFF000000u);
        gfx_draw_text(16 + 8 * 8, (int)gfx_height() - 78, renbuf, 0xFFE8B24A, 0xFF000000u);
    }
    app_chrome(ui_lang() ? "Keringo Commander" : "Keringő Commander", status, accent);
}
