/* main.c — Keringő OS belépés */
#include "kernel.h"

/* háttér-pulzus: soros log 1 s-enként */
static void pulzus_task(void) {
    u32 last = 0;
    for (;;) {
        u32 t = tick_count();
        if (t - last >= 100) {
            last = t;
            kprintf("pulzus: %d s\n", t / 100);
        }
        task_yield();
    }
}

/* háttér: CD meghajtó + ISO9660 felcsatolás */
static void lemez_task(void) {
    char names[16][64]; u32 sizes[16]; u8 flags[16];
    for (;;) {
        if (ata_init() && iso_mount()) {
            u32 lbas[16];
            int cd = iso_root_list(names, lbas, sizes, flags, 16);
            ui_fs_set_cd(cd, names, sizes);
            /* az /cd fájljainak előreolvasása RAM-ba (a CD a saját boot-lemezünk) */
            {
                static char fbuf[2048];
                int flen = 0;
                for (int k = 0; k < cd && k < 16; k++) {
                    if (sizes[k] == 0 || sizes[k] > 2048) continue;
                    if (flags[k] & 1) continue;              /* könyvtár */
                    int ok = 0; flen = 0;
                    for (int att = 0; att < 6 && !ok; att++) {
                        ok = iso_read_file(lbas[k], sizes[k], fbuf, 2048, &flen) && flen > 0 && fbuf[0] != 0;
                    }
                    kprintf("[preload] %s lba=%d ok=%d flen=%d b0=%x\n",
                            names[k], lbas[k], ok, flen, (u8)fbuf[0]);
                    if (ok) ui_cd_cache(k, fbuf, flen);
                }
                kprintf("lemez: %d fajl eloreolvasva\n", cd);
            }
            int mem = vfs_list("/mem", names, sizes, flags, 16);
            ui_fs_set_mem(mem, names, sizes);
            kprintf("lemez: kesz, cd=%d mem=%d\n", cd, mem);
            /* innentől pihenő — a CD-tartalom nem változik */
            for (;;) task_yield();
        }
        /* újrapróbálkozás kb. 1 másodperc múlva */
        u32 until = tick_count() + 100;
        while (tick_count() < until) task_yield();
    }
}

void kmain(u32 magic, multiboot_info_t *mbi) {
    serial_init();
    kprintf("\n=== KERINGO OS 0.1.0 boot ===\n");
    if (magic != 0x2BADB002) {
        kprintf("HIBA: multiboot magic rossz: %x\n", magic);
        for (;;) hlt();
    }
    if (!(mbi->flags & MBI_FLAG_FRAMEBUFFER) || mbi->framebuffer_bpp > 64) {
        kprintf("FIGY: nincs hasznalhato framebuffer-info\n");
    }

    cpu_init();
    kprintf("cpu: GDT/IDT/PIC/PIT kesz\n");

    mem_init(mbi);
    kprintf("mem: KB=%d szabad lap=%d fb=%x\n", mem_total_kb(), mem_free_pages(), mem_fb_phys());

    ps2_init();
    kprintf("ps2: billentyuzet+eger kezdemnyezve\n");

    gfx_init(mbi);
    r3d_init();
    ui_init();
    kprintf("ui: keringo asztal kesz\n");

    task_init();
    task_spawn("pulzus", pulzus_task, 0);
    task_spawn("lemez", lemez_task, 0);
    kprintf("task: %d db indulra kesz\n", task_count());

    sti();
    kprintf("indulas: keringes!\n");
    ui_task();   /* a fő task mostantól az asztal */
}
