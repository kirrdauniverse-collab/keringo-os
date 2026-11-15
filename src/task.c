/* task.c — kernel-szintű preemptív többfeladat (IRQ0, ring0) */
#include "kernel.h"

#define MAX_TASKS 8
#define STACK_SZ  8192

typedef struct {
    char  name[16];
    void (*entry)(void);
    u32   stack_top;
    u32   frame;          /* regs_t* (ds-slótra mutat) a saját stackjén */
    u32   ticks;
    int   alive;
} task_t;

static task_t tasks[MAX_TASKS];
static int    ntasks = 0;
static int    current = 0;

u32 schedule_tick(regs_t *r);   /* IRQ0: tick + EOI + ütemezés */
u32 schedule_only(regs_t *r);   /* int 0x30: csak ütemezés (task_yield) */

void task_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) tasks[i].alive = 0;
    tasks[0].alive = 1; tasks[0].entry = 0; tasks[0].frame = 0; tasks[0].ticks = 0;
    for (int i = 0; i < 15; i++) tasks[0].name[i] = "fo-task"[i] ? "fo-task"[i] : 0;
    ntasks = 1;
    current = 0;
}

int task_spawn(const char *name, void (*entry)(void), u32 stack_size) {
    if (ntasks >= MAX_TASKS) return -1;
    if (stack_size == 0) stack_size = STACK_SZ;
    int i = ntasks++;
    task_t *t = &tasks[i];
    for (int k = 0; k < 15 && name[k]; k++) t->name[k] = name[k];
    t->name[15] = 0;
    t->entry = entry;
    u32 *sp = (u32 *)kmalloc(stack_size);
    sp = (u32 *)((u32)sp + stack_size);          /* teteje */
    sp[-1] = 0x202;                              /* eflags: IF */
    sp[-2] = 0x08;                               /* cs */
    sp[-3] = (u32)entry;                         /* eip — a task nem térhet vissza! */
    sp[-4] = 0;                                  /* err */
    sp[-5] = 32;                                 /* int_no */
    sp[-6] = 0;                                  /* eax */
    sp[-7] = 0;                                  /* ecx */
    sp[-8] = 0;                                  /* edx */
    sp[-9] = 0;                                  /* ebx */
    sp[-10] = (u32)sp;                           /* esp (formális) */
    sp[-11] = 0;                                 /* ebp */
    sp[-12] = 0;                                 /* esi */
    sp[-13] = 0;                                 /* edi */
    sp[-14] = 0x10;                              /* ds */
    t->stack_top = (u32)sp;
    t->frame = (u32)(sp - 14);                   /* &ds */
    t->ticks = 0;
    t->alive = 1;
    return i;
}

/* közös váltó-logika: következő élő task kiválasztása */
static u32 pick_next(regs_t *r) {
    if (ntasks <= 1) return (u32)r;
    int next = current;
    for (int k = 1; k <= ntasks; k++) {
        int cand = (current + k) % ntasks;
        if (tasks[cand].alive) { next = cand; break; }
    }
    if (next == current) return (u32)r;
    tasks[current].frame = (u32)r;
    current = next;
    return tasks[current].frame;
}

/* IRQ0-ból hívódik: tick növelés, EOI, ütemezés */
u32 schedule_tick(regs_t *r) {
    tick_inc();
    tasks[current].ticks++;
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
    return pick_next(r);
}

/* task_yield-ból (int 0x30): tick és EOI NÉLKÜL */
u32 schedule_only(regs_t *r) {
    return pick_next(r);
}

void task_yield(void) { __asm__ volatile("int $0x30"); }

u32 task_count(void) { return ntasks; }
const char *task_name(int i) { return (i >= 0 && i < ntasks) ? tasks[i].name : "?"; }
u32 task_ticks(int i) { return (i >= 0 && i < ntasks) ? tasks[i].ticks : 0; }
