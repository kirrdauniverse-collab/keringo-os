/* mem.c — fizikai keret-bitemap, PSE paging, bump-heap */
#include "kernel.h"

extern u32 __kernel_end;            /* linker.ld */
void *kmalloc_start(void) { return &__kernel_end; }

#define BITMAP_PAGES  131072          /* 512 MB / 4K */
#define BITMAP_BYTES  (BITMAP_PAGES / 8)
static u8 frame_bitmap[BITMAP_BYTES];
static u32 free_pages_cnt = 0;
static u32 ram_top = 0;               /* fizikai RAM vége (byte) */
static u32 heap_ptr = 0;              /* következő szabad heap-cím */
static u32 heap_end = 0;
static u32 fb_phys = 0, fb_mapped_mb = 0;

static u32 pgdir[1024] __attribute__((aligned(4096)));

/* ---------- fizikai bitemap ---------- */
static inline void mark_used(u32 page) {
    if (page < BITMAP_PAGES) {
        if (!(frame_bitmap[page >> 3] & (1 << (page & 7)))) {
            frame_bitmap[page >> 3] |= (1 << (page & 7));
            if (page * 4096 < ram_top && free_pages_cnt) free_pages_cnt--;
        }
    }
}

u32 mem_alloc_page(void) {
    for (u32 i = 0; i < BITMAP_PAGES && i * 4096 < ram_top; i++) {
        if (!(frame_bitmap[i >> 3] & (1 << (i & 7)))) { mark_used(i); return i * 4096; }
    }
    return 0;
}

u32 mem_free_pages(void) { return free_pages_cnt; }

/* ---------- heap (bump) ---------- */
void *kmalloc(u32 size) {
    heap_ptr = (heap_ptr + 15) & ~15u;
    void *p = (void *)heap_ptr;
    heap_ptr += size;
    if (heap_ptr > heap_end) return 0;
    return p;
}

u32 heap_used_bytes(void) { return heap_ptr - (u32)kmalloc_start(); }

/* ---------- paging (4 MB-os lapok, PSE) ---------- */
static void pg_map_4mb(u32 vaddr_mb, u32 phys_mb, int present) {
    u32 e = (phys_mb << 21) | 0x82;   /* PS(1<<7) | RW(1<<1) */
    if (!present) e = 0;
    pgdir[vaddr_mb] = e;
}

void mem_init(multiboot_info_t *mbi) {
    kprintf("mem1\n");
/* bitmap nullázás */
    for (u32 i = 0; i < BITMAP_BYTES; i++) frame_bitmap[i] = 0;
    free_pages_cnt = BITMAP_PAGES;

    /* mem_upper (KB) — az első 1 MB alatta mindig foglalt */
    u32 upper_kb = 16384;   /* alapértelmezés, ha nincs mmap/meminfo */
    if (mbi->flags & MBI_FLAG_MEM && mbi->mem_upper > 1024) upper_kb = mbi->mem_upper;
    if (mbi->flags & MBI_FLAG_MMAP) {
        multiboot_mmap_t *m = (multiboot_mmap_t *)mbi->mmap_addr;
        u32 max_top = 0;
        for (u32 off = 0; off < mbi->mmap_length; off += m->size + 4) {
            if (m->type == 1 && (m->base_addr >> 32) == 0) {
                u64 end = m->base_addr + m->length;
                if ((end >> 32) == 0 && (u32)end > max_top) max_top = (u32)end;
            }
        }
        if (max_top > 0x100000) upper_kb = (max_top - 0x100000) / 1024;
    }
    ram_top = 0x100000 + upper_kb * 1024;
    if (ram_top > 512u * 1024 * 1024) ram_top = 512u * 1024 * 1024;

    /* foglaltak: 0..kernel vége */
    u32 kend = (u32)kmalloc_start();
    for (u32 p = 0; p <= kend; p += 4096) mark_used(p / 4096);

    /* heap: kernel vége után, ram_top-ig (kb. 4 MB-os léptékben lencsehéj) */
    heap_ptr = (kend + 0xFFF) & ~0xFFFu;
    heap_end = ram_top;
    /* heap után fenntartás: a lapbitmap+kgdt már a kernelben van; az első 2 MB-ot
       hagyjuk a videó/kód világának — a heap a kernel vége után indul, ez jó */

    kprintf("mem2 heap=%x\n", heap_ptr);
    /* paging: 4 KB-os lapok — 0..ram_top identity, framebuffer 0x80000000-re */
    u32 ram_4mbs = (ram_top + 0x3FFFFF) >> 22;          /* hány 4 MB-os blokk */
    if (ram_4mbs > 255) ram_4mbs = 255;
    u32 *tables = (u32 *)kmalloc(ram_4mbs * 1024 * 4);  /* oldal-katalógusok */
    if (!tables) { for (;;) hlt(); }
    for (u32 b = 0; b < ram_4mbs; b++) {
        u32 *pt = tables + b * 1024;
        for (u32 i = 0; i < 1024; i++) {
            u32 phys = b * 0x400000 + i * 0x1000;
            pt[i] = (phys < ram_top) ? (phys | 0x03) : 0;   /* P | RW */
        }
        pgdir[b] = ((u32)pt & 0xFFFFF000) | 0x03;
    }
    if (mbi->flags & MBI_FLAG_FRAMEBUFFER) {
        fb_phys = (u32)(mbi->framebuffer_addr & 0xFFFFFFFF);
        u32 *ptfb = (u32 *)kmalloc(1024 * 4);
        u32 fb_base = fb_phys & 0xFFFFF000;
        for (u32 i = 0; i < 1024; i++) ptfb[i] = (fb_base + i * 0x1000) | 0x03;
        pgdir[512] = ((u32)ptfb & 0xFFFFF000) | 0x03;   /* virtuális 0x80000000 */
        kprintf("mem: fb atva: virt=%x fiz=%x\n", 512u * 0x400000u, fb_base);
    }

    kprintf("mem5 tablak kesz\n");
    __asm__ volatile("mov %0, %%cr3" :: "r"(pgdir));
    u32 cr0; __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0 | 0x80000000)); /* PG */
    kprintf("mem6 PG bekapcsolva\n");
}

u32 mem_total_kb(void) { return ram_top / 1024; }
u32 mem_fb_phys(void) { return fb_phys; }
void *mem_fb_vaddr(void) { return (void *)0x80000000; }
