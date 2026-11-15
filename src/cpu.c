/* cpu.c — GDT, IDT, PIC, PIT (100 Hz), kivételek */
#include "kernel.h"

/* ---------- GDT ---------- */
static u64 gdt[5];
static struct { u16 limit; u32 base; } __attribute__((packed)) gdtr;

/* ---------- IDT ---------- */
struct idt_entry { u16 lo; u16 sel; u8 zero; u8 flags; u16 hi; } __attribute__((packed));
static struct idt_entry idt[256];
static struct { u16 limit; u32 base; } __attribute__((packed)) idtr;

/* isr.S által exportált stubtábla: 0..31 kivételek, 32..47 IRQ */
extern void (*isr_stubs[48])(void);
extern void irq0_entry(void);

static void (*irq_handlers[16])(void) = {0};
void irq_install_handler(int irq, void (*h)(void)) { irq_handlers[irq] = h; }

void isr_handler_c(regs_t *r) {
    if (r->int_no < 32) {
        kprintf("KIVETEL %d err=%x eip=%x cs=%x esp=%x\n", r->int_no, r->err_code, r->eip, r->cs, r->esp);
        kprintf("eax=%x ebx=%x ecx=%x edx=%x\n", r->eax, r->ebx, r->ecx, r->edx);
        for (;;) hlt();
    }
    if (r->int_no >= 32 && r->int_no < 48) {
        int irq = r->int_no - 32;
        if (irq_handlers[irq]) irq_handlers[irq]();
        if (irq >= 8) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
}

static void gdt_set(int i, u32 base, u32 limit, u8 access, u8 gran) {
    /* bitek: 0-15 limit_lo, 16-39 base_lo, 40-47 access, 48-51 limit_hi,
       52-55 gran (G,D), 56-63 base_hi */
    gdt[i] = ((u64)(base & 0xFFFF) << 16)
           | ((u64)((base >> 16) & 0xFF) << 32)
           | ((u64)(limit & 0xFFFF))
           | ((u64)((limit >> 16) & 0xF) << 48)
           | ((u64)(gran & 0xF) << 52)
           | ((u64)access << 40)
           | ((u64)((base >> 24) & 0xFF) << 56);
}

static void gdt_load(void) {
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u32)gdt;
    __asm__ volatile("lgdt (%0)" :: "m"(gdtr));
    __asm__ volatile(
        "ljmp $0x08, $1f\n 1:\n"
        "mov $0x10, %%ax\n mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%ss\n mov %%ax, %%fs\n mov %%ax, %%gs"
        ::: "eax");
}

static void idt_set(int i, void (*h)(void)) {
    u32 a = (u32)h;
    idt[i].lo = a & 0xFFFF;
    idt[i].sel = 0x08;
    idt[i].zero = 0;
    idt[i].flags = 0x8E;
    idt[i].hi = (a >> 16) & 0xFFFF;
}

static void idt_load(void) {
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u32)idt;
    __asm__ volatile("lidt (%0)" :: "m"(idtr));
}

/* ---------- IRQ regisztráció ---------- */
/* ---------- PIT ---------- */
static volatile u32 ticks = 0;
u32 tick_count(void) { return ticks; }
void tick_inc(void) { ticks++; }

void pit_init(u32 hz) {
    u32 div = 1193182 / hz;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, (div >> 8) & 0xFF);
}

void cpu_init(void) {
    /* FPU engedélyezése (x87): CR0 EM(2)/TS(3) törlése */
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(u32)0xC;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xC);  /* kód */
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xC);  /* adat */
    gdt_set(3, 0, 0xFFFFFFFF, 0xFA, 0xC);  /* user kód (tartalék) */
    gdt_set(4, 0, 0xFFFFFFFF, 0xF2, 0xC);  /* user adat */
    gdt_load();

    for (int i = 0; i < 48; i++) idt_set(i, isr_stubs[i]);
    idt_set(32, irq0_entry);                /* IRQ0 = ütemező út */
    { extern void sched_entry(void); idt_set(48, sched_entry); } /* task_yield */
    idt_load();

    /* PIC remap 0x20..0x2F */
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0x00);                       /* mind engedélyezve */
    outb(0xA1, 0x00);

    pit_init(100);   /* az IRQ0-t a task.c schedule_tick-je kezeli */
}
