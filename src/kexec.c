/* kexec.c — új kernel életterejtése: ELF betöltése scratch-be, eszközök leállítása, trambulin */
#include "kernel.h"
#include "net.h"

extern u8 kexec_trampoline[], kexec_trampoline_end[];

extern void usb_shutdown_all(void);

/* az eredeti multiboot-info fizikai címe (main.c menti el) */
extern u32 g_mbi_phys;

int kexec_load_and_boot(const u8 *elfbuf, u32 len) {
    if (len < 52 || elfbuf[0] != 0x7F || elfbuf[1] != 'E' || elfbuf[2] != 'L' || elfbuf[3] != 'F') {
        kprintf("kexec: nem ELF32\n");
        return 0;
    }
    u32 entry = *(const u32 *)(elfbuf + 24);
    u32 phoff = *(const u32 *)(elfbuf + 28);
    u16 phentsize = *(const u16 *)(elfbuf + 42);
    u16 phnum = *(const u16 *)(elfbuf + 44);
    if (phnum == 0 || phnum > 16 || phoff + (u32)phnum * phentsize > len) {
        kprintf("kexec: rossz programfejléc\n");
        return 0;
    }
    /* szegmensek mérete: min/max paddr */
    u32 min_pa = 0xFFFFFFFFu, max_pa = 0;
    for (int i = 0; i < phnum; i++) {
        const u8 *ph = elfbuf + phoff + (u32)i * phentsize;
        if (*(const u32 *)(ph + 0) != 1) continue;             /* PT_LOAD */
        u32 pa = *(const u32 *)(ph + 12);
        u32 memsz = *(const u32 *)(ph + 20);
        if (pa < min_pa) min_pa = pa;
        if (pa + memsz > max_pa) max_pa = pa + memsz;
    }
    if (min_pa != 0x100000u) {
        kprintf("kexec: a kernel nem 1 MB-ra linkelt (min=%x)\n", min_pa);
        return 0;
    }
    u32 imgsz = max_pa - min_pa;
    if (imgsz > 28u * 1024 * 1024) { kprintf("kexec: túl nagy kernel (%u B)\n", imgsz); return 0; }

    /* scratch: a teljes kép (fájlrész + kinullázott BSS) + trambulin */
    u32 tramp_off = (imgsz + 4095) & ~4095u;
    u32 total = tramp_off + 16384;
    u8 *scratch = (u8 *)kmalloc(total);
    if (!scratch) { kprintf("kexec: nincs memória (%u B)\n", total); return 0; }
    for (u32 i = 0; i < tramp_off; i++) scratch[i] = 0;
    for (int i = 0; i < phnum; i++) {
        const u8 *ph = elfbuf + phoff + (u32)i * phentsize;
        if (*(const u32 *)(ph + 0) != 1) continue;
        u32 off = *(const u32 *)(ph + 4);
        u32 pa = *(const u32 *)(ph + 12);
        u32 filesz = *(const u32 *)(ph + 16);
        u32 memsz = *(const u32 *)(ph + 20);
        if (off + filesz > len) { kprintf("kexec: szegmens a fájl végén túl\n"); return 0; }
        u32 d = pa - min_pa;
        for (u32 k = 0; k < filesz; k++) scratch[d + k] = elfbuf[off + k];
        for (u32 k = filesz; k < memsz; k++) scratch[d + k] = 0;  /* BSS */
    }

    /* trambulin másolása + paraméterek */
    u8 *tramp = scratch + tramp_off;
    u32 tsize = (u32)(kexec_trampoline_end - kexec_trampoline);
    if (tsize > 64) { kprintf("kexec: trambulin túl hosszú\n"); return 0; }
    for (u32 i = 0; i < tsize; i++) tramp[i] = kexec_trampoline[i];
    *(u32 *)(tramp + 64 + 0) = (u32)scratch;                    /* forrás */
    *(u32 *)(tramp + 64 + 4) = 0x100000u;                       /* cél */
    *(u32 *)(tramp + 64 + 8) = imgsz / 4;                       /* dwordok */
    *(u32 *)(tramp + 64 + 12) = entry;                          /* belépés */
    *(u32 *)(tramp + 64 + 16) = g_mbi_phys;                     /* mbi */

    kprintf("kexec: uj kernel indul (kep=%u B, entry=%x)\n", imgsz, entry);

    /* eszközök leállítása */
    usb_shutdown_all();
    if (net_dev && net_dev->shutdown) net_dev->shutdown(net_dev);

    /* PIC maszkolása */
    cli();
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    /* ugrás a trambulinra — nincs visszatérés */
    ((void (*)(void))tramp)();
    return 0;                                   /* nem jutunk ide */
}
