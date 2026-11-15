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

#endif
