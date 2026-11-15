/* ps2.c — PS/2 billentyűzet (IRQ1) + egér (IRQ12) */
#include "kernel.h"

#define DATA 0x60
#define STAT 0x64
#define CMD  0x64

static key_event_t   keyq[32];  static volatile int keyh = 0, keyt = 0;
static mouse_event_t mouseq[32]; static volatile int mh = 0, mt = 0;
static volatile u8   mbtn = 0;

static volatile int mouse_cycle = 0;
static volatile u8  mouse_b[3];

static void q_wait_in(void)  { for (int i = 0; i < 100000; i++) if (!(inb(STAT) & 2)) break; }  /* bemenet üres? */
static void q_wait_out(void) { for (int i = 0; i < 100000; i++) if (inb(STAT) & 1) break; }     /* van adat? */

static void kbd_write_cmd(u8 c) { q_wait_in();  outb(CMD, c); }
static void kbd_write_dat(u8 d) { q_wait_in();  outb(DATA, d); q_wait_out(); }
static void mous_write(u8 d)    { q_wait_in();  outb(CMD, 0xD4); q_wait_in(); outb(DATA, d); q_wait_out(); }
static u8   kbd_read(void)      { q_wait_out(); return inb(DATA); }

/* scancode set 1 -> ASCII (US) */
static const char scmap[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',
    0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '-','-',0,'+',0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static int shift_down = 0;
/* a KEY_* konstansok a kernel.h-ból jönnek */

static void push_key(u8 sc, u8 rel, u8 ascii) {
    int nt = (keyt + 1) & 31;
    if (nt != keyh) { keyq[keyt].scancode = sc; keyq[keyt].released = rel; keyq[keyt].ascii = ascii; keyq[keyt].flags = 0; keyt = nt; }
}

static void kbd_irq(void) {
    u8 sc = inb(DATA);
    if (sc == 0xE0) return;        /* E0 előtag: a következő kódot külön értelmezzük */
    if (sc & 0x80) {
        u8 base = sc & 0x7F;
        if (base == 0x2A || base == 0x36) shift_down = 0;
        push_key(base, 1, 0);
    } else {
        u8 base = sc;
        if (base == 0x2A || base == 0x36) { shift_down = 1; return; }
        static const char shsym[10] = "!@#$%^&*(";
        u8 ascii = 0;
        if (base < 128) ascii = scmap[base];
        if (shift_down && ascii >= 'a' && ascii <= 'z') ascii -= 32;
        if (shift_down && ascii >= '1' && ascii <= '9') ascii = shsym[ascii - '1'];
        /* iránygombok és különleges billentyűk */
        if (base == 0x48) { push_key(KEY_UP, 0, 0); return; }
        if (base == 0x50) { push_key(KEY_DOWN, 0, 0); return; }
        if (base == 0x4B) { push_key(KEY_LEFT, 0, 0); return; }
        if (base == 0x4D) { push_key(KEY_RIGHT, 0, 0); return; }
        if (base == 0x01) { push_key(KEY_ESC, 0, 0); return; }
        if (base == 0x3B) { push_key(KEY_F1, 0, 0); return; }
        if (base == 0x3C) { push_key(KEY_F2, 0, 0); return; }
        if (base == 0x0F) { push_key(KEY_TAB, 0, 0); return; }
        if (base == 0x1C) { push_key(KEY_ENTER, 0, '\n'); return; }
        push_key(base, 0, ascii);
    }
}

static void mouse_irq(void) {
    u8 v = inb(DATA);
    switch (mouse_cycle) {
        case 0:
            if (!(v & 0x08)) return;              /* bitszinkron */
            mouse_b[0] = v; mouse_cycle = 1; break;
        case 1: mouse_b[1] = v; mouse_cycle = 2; break;
        case 2: {
            mouse_b[2] = v; mouse_cycle = 0;
            mbtn = mouse_b[0] & 7;
            s32 dx = (s32)(mouse_b[1]) - ((mouse_b[0] & 0x10) ? 256 : 0);
            s32 dy = (s32)(mouse_b[2]) - ((mouse_b[0] & 0x20) ? 256 : 0);
            int nt = (mt + 1) & 31;
            if (nt != mh) { mouseq[mh].dx = dx; mouseq[mh].dy = -dy; mouseq[mh].buttons = mbtn; mh = nt; }
            break; }
    }
}

void ps2_init(void) {
    keyh = keyt = mh = mt = 0;
    /* 1. port engedélyezése + 2. órajel */
    kbd_write_cmd(0xA8);            /* aux engedély */
    kbd_write_cmd(0x20);            /* kontroller-konfig olvasás */
    u8 cfg = kbd_read();
    kbd_write_cmd(0x60);
    kbd_write_dat((cfg | 0x03) & (u8)~0x30);  /* mindkét interrupt engedélyezve */
    /* egér alaphelyzet + report */
    mous_write(0xF6); mous_write(0xF4);
    /* keyboard engedély */
    kbd_write_dat(0xF4);

    irq_install_handler(1, kbd_irq);
    irq_install_handler(12, mouse_irq);
}

int key_pop(key_event_t *out) {
    if (keyh == keyt) return 0;
    *out = keyq[keyh]; keyh = (keyh + 1) & 31; return 1;
}

int mouse_pop(mouse_event_t *out) {
    if (mh == mt) return 0;
    *out = mouseq[mh]; mh = (mh + 1) & 31; return 1;
}

u8 mouse_buttons(void) { return mbtn; }
