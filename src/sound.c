/* sound.c — PC hangszóró PIT 2. csatornán + kis dallamlejátszó */
#include "kernel.h"

static u32 spk_until = 0;            /* meddig szól az aktuális hang (tick) */
static const u16 *tune_seq = 0;      /* (freq, hossz) párok */
static int tune_n = 0, tune_i = 0;
static u32 tune_next = 0;

void sound_beep(u16 freq, u8 ticks) {
    if (freq == 0) { sound_stop(); return; }
    outb(0x43, 0xB6);                       /* 2. csatorna, mode 3 */
    u16 div = (u16)(1193182u / freq);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)((div >> 8) & 0xFF));
    u8 p61 = inb(0x61);
    outb(0x61, (p61 & 0xFC) | 3);           /* gate + speaker be */
    spk_until = tick_count() + ticks;
}

void sound_stop(void) {
    u8 p61 = inb(0x61);
    outb(0x61, p61 & 0xFC);
    spk_until = 0;
}

void sound_tune(const u16 *seq, int n) {
    tune_seq = seq;
    tune_n = n;
    tune_i = 0;
    tune_next = 0;
}

void sound_poll(void) {
    u32 now = tick_count();
    if (tune_seq) {
        if ((s32)(now - tune_next) >= 0) {
            if (tune_i >= tune_n) {
                tune_seq = 0;
                sound_stop();
            } else {
                u16 f = tune_seq[tune_i * 2];
                u16 d = tune_seq[tune_i * 2 + 1];
                if (f) sound_beep(f, d);
                else   sound_stop();
                tune_next = now + d + 3;
                tune_i++;
            }
        }
        return;                             /* dallam közben a beep-eket a lejátszó kezeli */
    }
    if (spk_until && (s32)(now - spk_until) >= 0) {
        sound_stop();
    }
}
