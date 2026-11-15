/* main.c — Keringő OS 0.2 belépés: mag, tárolók, USB, hálózat, UI */
#include "kernel.h"
#include "net.h"
#include "wifi.h"
#include "crypto.h"

u32 g_mbi_phys = 0;

/* háttér-pulzus: soros log 5 s-enként */
static void pulzus_task(void) {
    u32 last = 0;
    for (;;) {
        u32 t = tick_count();
        if (t - last >= 500) {
            last = t;
            kprintf("pulzus: %u s | halozat=%d usb=%d sys=%d\n",
                    t / 100, net_up(), usb_ready(), sys_mounted());
        }
        task_yield();
    }
}

/* háttér: tárolók felállítása (CD, USB pendrájv, ATA merevlemez, /sys) */
static void lemez_task(void) {
    /* 1. CD (a boot-lemez) */
    if (ata_init() && iso_mount()) {
        kprintf("lemez: CD rendben\n");
    }
    /* 2. USB vezérlők + pendrájv */
    usb_init();
    /* 3. ATA merevlemezek */
    ata_disk_scan();
    /* 4. fájlrendszerek felcsatolása */
    vfs_mount_all();
    /* 5. beállítások betöltése (KERINGO.CFG) + alkalmazás */
    settings_load();
    ui_settings_reloaded();
    ui_fs_announce();
    /* 6. frissítési csomag keresése a pendrájvon */
    update_scan_usb();
    if (update_available_newer())
        kprintf("lemez: UJ FRISSITES ELÉRHETŐ: %s\n", update_version());
    storage_boot_done();
    kprintf("lemez: minden kesz\n");
    for (;;) task_yield();
}

/* háttér: hálózati kártya + DHCP (a UI felé zárással) */
static void halozat_task(void) {
    net_lock_all();
    net_init_hw();
    if (net_dev) {
        kprintf("halozat: kartya: %s\n", net_chipname());
        net_dhcp();
    }
    net_unlock_all();
    kprintf("halozat: kesz (up=%d)\n", net_up());
    for (;;) task_yield();
}

void kmain(u32 magic, multiboot_info_t *mbi) {
    serial_init();
    kprintf("\n=== KERINGO OS %s boot ===\n", KERINGO_VERZIO);
    if (magic != 0x2BADB002) {
        kprintf("HIBA: multiboot magic rossz: %x\n", magic);
        for (;;) hlt();
    }
    g_mbi_phys = (u32)mbi;
    if (!(mbi->flags & MBI_FLAG_FRAMEBUFFER) || mbi->framebuffer_bpp > 64) {
        kprintf("FIGY: nincs hasznalhato framebuffer-info\n");
    }

    cpu_init();
    kprintf("cpu: GDT/IDT/PIC/PIT kesz\n");

    mem_init(mbi);
    kprintf("mem: KB=%d szabad lap=%d fb=%x\n", mem_total_kb(), mem_free_pages(), mem_fb_phys());

    crypto_self_test();
    wifi_init();
    kprintf("wifi: alrendszer kesz\n");
    /* CCMP terheléses önteszt sti után (336 bájtos DHCP-szerű keret, 50x) */
    {
        static u8 k[16], pt[336], ct[344], dt[336];
        for (int i = 0; i < 16; i++) k[i] = (u8)(0x11 * (i + 1));
        for (int i = 0; i < 336; i++) pt[i] = (u8)i;
        u8 nonce[13]; for (int i = 0; i < 13; i++) nonce[i] = (u8)i;
        u8 aad[16]; for (int i = 0; i < 16; i++) aad[i] = (u8)(0xa0 + i);
        int ok = 1;
        for (int c = 0; c < 50 && ok; c++) {
            ccmp_encrypt(k, nonce, aad, 16, pt, 336, ct);
            if (!ccmp_decrypt(k, nonce, aad, 16, ct, 344, dt)) ok = 0;
            for (int i = 0; i < 336; i++) if (dt[i] != pt[i]) { ok = 0; break; }
        }
        kprintf("ccmp stressz: %s\n", ok ? "RENDBEN (50x336B)" : "HIBA");
    }

    ps2_init();
    kprintf("ps2: billentyuzet+eger kezdemnyezve\n");

    gfx_init(mbi);
    r3d_init();
    ui_init();
    kprintf("ui: keringo asztal kesz (9 panel)\n");

    task_init();
    task_spawn("pulzus", pulzus_task, 0);
    task_spawn("lemez", lemez_task, 0);
    task_spawn("halozat", halozat_task, 0);
    kprintf("task: %d db indulra kesz\n", task_count());

    sti();
    kprintf("indulas: keringes!\n");
    ui_task();   /* a fő task mostantól az asztal */
}
