/* kernel.h — Keringő OS közös definíciók */
#ifndef KERINGO_H
#define KERINGO_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef uintptr_t uptr;
typedef int32_t  bool32;
#define TRUE  1
#define FALSE 0

/* ---------- port I/O ---------- */
static inline void outb(u16 port, u8 val)  { __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port)); }
static inline void outw(u16 port, u16 val) { __asm__ volatile("outw %0,%1"::"a"(val),"Nd"(port)); }
static inline void outl(u16 port, u32 val) { __asm__ volatile("outl %0,%1"::"a"(val),"Nd"(port)); }
static inline u8 inb(u16 port)  { u8 v;  __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }
static inline u16 inw(u16 port) { u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(port)); return v; }
static inline u32 inl(u16 port) { u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(port)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }

/* ---------- multiboot1 info ---------- */
typedef struct {
    u32 flags;
    u32 mem_lower, mem_upper;
    u32 boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4];
    u32 mmap_length, mmap_addr;
    u32 drives_length, drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;
    /* flags & (1<<12) framebuffer */
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u32 framebuffer_bpp;
    u32 framebuffer_type;      /* 1 = RGB */
    u32 color_info[6];
} multiboot_info_t;

typedef struct {
    u32 size;
    u64 base_addr;
    u64 length;
    u32 type;
} __attribute__((packed)) multiboot_mmap_t;

#define MBI_FLAG_MEM       (1u<<0)
#define MBI_FLAG_MMAP      (1u<<6)
#define MBI_FLAG_FRAMEBUFFER (1u<<12)

/* ---------- regs_t (isr.S layout, ds-slótól kezdve) ---------- */
typedef struct {
    u32 ds;
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
    u32 int_no, err_code;
    u32 eip, cs, eflags;
} regs_t;

/* ---------- deklarációk ---------- */
void kmain(u32 magic, multiboot_info_t *mbi);

/* serial.c */
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
void kprintf(const char *fmt, ...);
void ksnprintf(char *buf, int bufsize, const char *fmt, ...);

/* cpu.c */
void tick_inc(void);
void cpu_init(void);
void irq_install_handler(int irq, void (*h)(void));
u32   tick_count(void);

/* mem.c */
void mem_init(multiboot_info_t *mbi);
void *kmalloc(u32 size);
u32 mem_total_kb(void);
u32   mem_free_pages(void);
u32   heap_used_bytes(void);
u32   mem_fb_phys(void);
void *mem_fb_vaddr(void);
void *kmalloc_start(void);
void *mem_map_mmio(u32 phys, u32 size);

/* ps2.c */
void ps2_init(void);
/* esemény-küldetés */
typedef struct { u8 scancode; u8 released; u8 ascii; u8 flags; } key_event_t;
typedef struct { s32 dx, dy; u8 buttons; } mouse_event_t;
int  key_pop(key_event_t *out);
int  mouse_pop(mouse_event_t *out);
u8   mouse_buttons(void);

/* task.c */
void task_init(void);
int  task_spawn(const char *name, void (*entry)(void), u32 stack_size);
void task_yield(void);
u32  task_count(void);
const char *task_name(int i);
u32  task_ticks(int i);

/* ata.c */
int  ata_init(void);
int  ata_read_sectors(u32 lba, u32 count, void *buf);
u32  ata_sector_size(void);

/* iso9660.c */
int  iso_mount(void);
/* vfs.c */
int  vfs_list(const char *dir, char names[][64], u32 *sizes, u8 *flags, int max);
int  vfs_read_file(const char *path, char *buf, int bufsize, int *outlen);
int  vfs_write_file(const char *path, const char *buf, int len);
int  vfs_read_any(const char *name, u8 *buf, u32 bufsize, u32 *outlen);

/* ps2 billentyű kódok */
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_ESC   0x84
#define KEY_F1    0x85
#define KEY_F2    0x86
#define KEY_TAB   0x87
#define KEY_ENTER 0x1C   /* Enter scancode-ja (set 1) */
#define KEY_F5    0x88
#define KEY_F6    0x89
#define KEY_F8    0x8A
#define KEY_PGUP  0x8B
#define KEY_PGDN  0x8C
#define KEY_F3    0x8D
#define KEY_F4    0x8E
#define KEY_F7    0x8F
#define KEY_F9    0x90
#define KEY_F10   0x91
#define KEY_F11   0x92
#define KEY_F12   0x93
#define KEY_DEL   0x94
#define KEY_HOME  0x95
#define KEY_END   0x96

/* ---------- verzió ---------- */
#define KERINGO_VERZIO "0.4.0"

/* ---------- crc32 ---------- */
u32 crc32_calc(u32 crc, const u8 *buf, u32 len);

/* ---------- PCI ---------- */
#define PCI_VENID(dev)  pci_read16(dev, 0x00)
#define PCI_DEVID(dev)  pci_read16(dev, 0x02)
#define PCI_CLASS(dev)  (pci_read16(dev, 0x0A) >> 8)
#define PCI_SUBCLASS(dev) (pci_read16(dev, 0x0A) & 0xFF)
#define PCI_PROGIF(dev)  (pci_read8(dev, 0x09))
typedef struct { u8 bus, dev, fn; } pci_dev_t;
u32  pci_read32(pci_dev_t d, u8 off);
u16  pci_read16(pci_dev_t d, u8 off);
u8   pci_read8(pci_dev_t d, u8 off);
void pci_write16(pci_dev_t d, u8 off, u16 v);
void pci_write32(pci_dev_t d, u8 off, u32 v);
void pci_enable_busmaster(pci_dev_t d);
u32  pci_bar_mmio(pci_dev_t d, int idx);
u32  pci_bar_io(pci_dev_t d, int idx);
int  pci_scan_class(pci_dev_t *out, u16 class, u16 subclass, int nth);
void pci_debug_dump(void);

/* ---------- blokkeszköz (lemezek) ---------- */
typedef struct {
    char label[16];
    int  usb;                              /* 0 = ATA HDD partíció, 1 = USB */
    int  (*read_secs)(u32 lba, u32 n, void *buf, void *ctx);
    int  (*write_secs)(u32 lba, u32 n, const void *buf, void *ctx);
    u32  start_lba;                        /* partíció kezdete (blokkeszköz-cím) */
    u32  nsecs;
    u32  secsize;
    void *ctx;
} blkdev_t;

/* ---------- FAT32 ---------- */
typedef struct fatfs fatfs_t;
int  fat32_mount(blkdev_t *bd, fatfs_t **out);
int  fat32_list(fatfs_t *fs, const char *path, char names[][64], u32 *sizes, u8 *flags, int max);
int  fat32_read(fatfs_t *fs, const char *path, u8 *buf, u32 bufsize, u32 *outlen);
int  fat32_write(fatfs_t *fs, const char *path, const u8 *buf, u32 len);
int  fat32_flush(fatfs_t *fs);
const char *fat32_label(fatfs_t *fs);

/* ---------- ATA merevlemez (ata.c) ---------- */
void ata_disk_scan(void);
int  ata_disk_count(void);
int  ata_disk_read_secs(int disk, u32 lba, u32 n, void *buf);
int  ata_disk_write_secs(int disk, u32 lba, u32 n, const void *buf);
int  ata_disk_flush(int disk);

/* ---------- USB ---------- */
void usb_init(void);
int  usb_ready(void);
int  usb_read_secs(u32 lba, u32 n, void *buf);
int  usb_write_secs(u32 lba, u32 n, const void *buf);
int  usb_flush(void);
int  usb_flush_and_recheck(void);
u32  msc_sector_count(void);
int  msc_present(void);

/* ---------- jpeg.c / img ---------- */
int  jpeg_decode(const u8 *buf, u32 len, u32 **out_pix, int *out_w, int *out_h);
int  bmp_decode(const u8 *buf, u32 len, u32 **out_pix, int *out_w, int *out_h);
u32 *img_scale(const u32 *src, int sw, int sh, int dw, int dh);

/* ---------- update.c ---------- */
const char *update_version(void);
const char *update_status(void);
int  update_scan_usb(void);
int  update_scan_http(const char *url);
int  update_available_newer(void);
int  update_nfiles(void);
const char *update_fname(int i);
u32  update_fsize(int i);
int  update_apply(int live);
int  update_rollback(void);

/* ---------- settings.c ---------- */
typedef struct {
    int  wallpaper;
    char wp_file[64];
    int  icon_theme;
    int  accent;
    int  lang;
    char update_url[96];
    char note[600];
} settings_t;
extern settings_t settings;
u32  settings_accent_color(void);
const char *settings_wp_names(int i);
const char *settings_icon_names(int i);
void settings_load(void);
int  settings_save(void);
void settings_note_save(const char *note, int len);
int  settings_note_load(char *note, int max);

/* ---------- kexec.c ---------- */
int  kexec_load_and_boot(const u8 *elfbuf, u32 len);

/* ---------- usb.c ---------- */
void usb_shutdown_all(void);

/* ---------- net zárás ---------- */
void net_lock_all(void);
void net_unlock_all(void);
void net_poll_raw(void);
void msc_shutdown(void);

/* ---------- ui (0.2) ---------- */
void ui_settings_reloaded(void);

/* ---------- hatter.c (háttérkép) ---------- */
void wallpaper_apply(void);
int  wallpaper_active(void);
void wallpaper_blit(void);

/* ---------- net (main/UI) ---------- */
void net_dhcp(void);
void net_init_hw(void);
int  net_up(void);
int  net_hw_ready(void);
u32  net_self_ip(void);
const char *net_chipname(void);

/* ---------- csatolt fájlrendszerek (vfs.c 0.2) ---------- */
int  vfs_mount_all(void);                  /* /sys + /usb felcsatolás */
int  sys_mounted(void);
int  usbfs_mounted(void);
void storage_boot_done(void);
int  storage_ready_for_ui(void);
void ui_fs_announce(void);                 /* UI-listák frissítése a lemez taskból */

/* render3d.c */
#define STARS_MAX 320
extern float cam_yaw, cam_pitch, cam_dist, cam_target_dist;
void r3d_init(void);
void r3d_camera_update(void);
void r3d_draw_background(u32 tick);
void r3d_draw_orbit_ring(float radius, float y, u32 color, u32 tick);
void r3d_draw_core(u32 tick);
int  r3d_project_point(float x, float y, float z, float *sx, float *sy, float *depth);
float r3d_focal(void);
/* saját float matematika (nincs libc) */
float kosz_cosf(float x);
float kosz_sinf(float x);

/* iso9660.c */
int  iso_mount(void);
int  iso_mounted(void);
int  iso_root_list(char names[][64], u32 *lbas, u32 *sizes, u8 *flags, int max);
int  iso_read_file(u32 lba, u32 size, char *buf, int bufsize, int *outlen);
int  iso_find(const char *name, u32 *lba, u32 *size);

/* vfs.c / ramfs */
int  vfs_strcmp(const char *a, const char *b);
int  ramfs_strcmp(const char *a, const char *b);
int  ramfs_write(const char *name, const char *data, int len);
int  ramfs_read(const char *name, char *buf, int bufsize, int *outlen);
int  ramfs_count(void);
const char *ramfs_name(int i);
int  ramfs_size(int i);

/* ata.c */
int  ata_present(void);

/* gfx.c */
u32 *gfx_backbuf(void);   /* belső háttérpuffer (gyors rajzoláshoz) */
void gfx_init(multiboot_info_t *mbi);
u32  gfx_width(void);
u32  gfx_height(void);
void gfx_flip(void);
void gfx_clear(u32 color);
void gfx_fill_rect(int x, int y, int w, int h, u32 color);
void gfx_rect(int x, int y, int w, int h, u32 color);
void gfx_hline(int x, int y, int len, u32 color);
void gfx_vline(int x, int y, int len, u32 color);
void gfx_pixel(int x, int y, u32 color);
void gfx_draw_text(int x, int y, const char *s, u32 fg, u32 bg);
int  gfx_text_width(const char *s);
void gfx_draw_text_scaled(int x, int y, const char *s, u32 fg, u32 scale);
/* textúra buffer 3D panelekhez */
typedef struct { int w, h; u32 *px; } texture_t;
texture_t *texture_new(int w, int h);
void       texture_free(texture_t *t);
void gfx_blit_texture(texture_t *t, int dx, int dy);
void gfx_render_texture3d(texture_t *t, float vx[4], float vy[4], float depth);

/* render3d.c */
void r3d_init(void);
void r3d_draw_quads(void);   /* belső: ütemezett rajzolás */

/* ui.c */
void ui_init(void);
void ui_task(void);          /* fő UI ciklus (fő taskként fut) */
void ui_handle_key(u8 sc, u8 rel, u8 ascii);
void ui_handle_mouse(int dx, int dy, u8 btn);
void ui_fs_set_cd(int n, char names[][64], u32 *sizes);
void ui_fs_set_mem(int n, char names[][64], u32 *sizes);
void ui_cd_cache(int idx, const char *buf, int len);
void ui_set_lang(int lang);
int  ui_lang(void);


/* ---------- browser.c ---------- */
void browser_init(void);
int  browser_open(const char *url);
void browser_scroll(int d);
int  browser_follow_link(int n);
void browser_stream_poll(void);
int  browser_is_streaming(void);
const char *browser_url(void);
const char *browser_title(void);
const char *browser_msg(void);
u32  browser_stream_frames(void);
u32  browser_stream_fps(void);
void browser_render(texture_t *t, u32 tick, int focused);

/* ---------- apps.c (0.3 alkalmazás-keretrendszer) ---------- */
typedef struct {
    const char *nev[2];                 /* HU, EN */
    void (*enter)(void);
    void (*frame)(u32 tick);
    void (*key)(u8 sc, u8 rel, u8 ascii);
    void (*mouse)(int mx, int my, int dx, int dy, u8 btn, u8 prev);
    void (*exit)(void);
    int  (*esc)(void);                  /* 1: az app elfogyasztja az Escet */
} app_t;
extern const app_t app_list[];
extern const int   APP_COUNT;
extern const int   app_hotkey_app[];
extern const u8    app_hotkey_key[];
extern const int   APP_HOTKEYS;
int   app_active(void);
void  app_launch(int i);
void  app_close(void);
void  app_key_event(u8 sc, u8 rel, u8 ascii);
void  app_mouse_event(int dx, int dy, u8 btn);
void  app_frame(u32 tick);
int   app_mouse_x(void);
int   app_mouse_y(void);
u8    app_mouse_btn(void);
/* kozos app-segedletek */
void  app_chrome(const char *title, const char *status, u32 accent);
void  app_line(int x0, int y0, int x1, int y1, u32 c);
void  app_circle(int cx, int cy, int r, u32 c, int filled);
u32   app_rand(void);
void  app_srand(u32 seed);
int   app_in_rect(int x, int y, int rx, int ry, int rw, int rh);
void  app_button(int x, int y, int w, int h, const char *label, u32 accent, int pressed);

/* ---------- sound.c (PC hangszoro) ---------- */
void  sound_beep(u16 freq, u8 ticks);
void  sound_tune(const u16 *seq, int n);   /* (frekvencia, hossz) parok */
void  sound_poll(void);
void  sound_stop(void);

/* ---------- saver.c (kepernyovedok) ---------- */
int   saver_count(void);
const char *saver_name(int i, int lang);
void  saver_start(int which);              /* -1: veletlen */
int   saver_running(void);
void  saver_stop(void);
void  saver_frame(u32 tick);

/* app belepopontok (app_*.c / game_*.c) */
void paint_enter(void); void paint_frame(u32 t); void paint_key(u8 sc, u8 rel, u8 a); void paint_mouse(int mx,int my,int dx,int dy,u8 btn,u8 prev); void paint_exit(void);
void text_enter(void); void text_frame(u32 t); void text_key(u8 sc, u8 rel, u8 a); void text_exit(void);
void savermenu_enter(void); void savermenu_frame(u32 t); void savermenu_key(u8 sc, u8 rel, u8 a);
int  savermenu_esc(void);
void ray_enter_lab(void); void ray_enter_doom(void); void ray_frame(u32 t); void ray_key(u8 sc, u8 rel, u8 a); void ray_exit(void);
void elite_enter(void); void elite_frame(u32 t); void elite_key(u8 sc, u8 rel, u8 a); void elite_exit(void);
void city_enter(void); void city_frame(u32 t); void city_key(u8 sc, u8 rel, u8 a); void city_mouse(int mx,int my,int dx,int dy,u8 btn,u8 prev); void city_exit(void);
void pin_enter(void); void pin_frame(u32 t); void pin_key(u8 sc, u8 rel, u8 a); void pin_exit(void);
void calc_enter(void); void calc_frame(u32 t); void calc_key(u8 sc, u8 rel, u8 a); void calc_mouse(int mx,int my,int dx,int dy,u8 btn,u8 prev);
void wifiapp_enter(void); void wifiapp_frame(u32 t); void wifiapp_key(u8 sc, u8 rel, u8 a); void wifiapp_exit(void);



#endif