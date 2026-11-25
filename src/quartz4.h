/* quartz4.h — QUARTZ-4B (KIMI-MATE) 4 bites számítógép Keringő-portja
 * A gépet a repóba feltöltött KIMI-MATE-4bit-computer.html (QUARTZ-4B v3.0)
 * alapján portoltuk; a BASIC nyelvtanát bővítettük (kurzorkezelés, KEY() bővítés). */
#ifndef KERINGO_QUARTZ4_H
#define KERINGO_QUARTZ4_H
#include "kernel.h"

#define Q4_TEXT_W 32
#define Q4_TEXT_H 16
#define Q4_GFX_W  64
#define Q4_GFX_H  48
#define Q4_RAM    4096
#define Q4_PROG_MAX 128
#define Q4_PROG_LEN 112

typedef struct {
    /* CPU: 4 bites akku, 12 bites X/SP/PC */
    u8  ram[Q4_RAM];
    int pc, a, x, sp, halted;
    int fz, fn, fc, fv;                  /* Z N C V jelzők */
    u32 mcu_cycles;
    /* videó */
    int videoMode;                        /* 0 szöveg, 1 grafika */
    u8  vt[Q4_TEXT_W * Q4_TEXT_H];        /* szöveg-karakterek */
    u8  vg[Q4_GFX_W * Q4_GFX_H / 8];     /* grafikabitmap */
    int cx, cy, lastX, lastY;
    /* BASIC programtár */
    int pnum[Q4_PROG_MAX];
    char ptxt[Q4_PROG_MAX][Q4_PROG_LEN];
    int np;
    int vars[26];                         /* A-Z változók (4 bites) */
    int running;                          /* fut-e a program */
    int curline;                          /* aktuális sorszám */
    int gosub[8]; int ng;                 /* GOSUB verem */
    struct { int var, end, step, line; } forst[6]; int nf;
    struct { int line; char cond[64]; } whst[4]; int nw;
    struct { int line; } rpst[4]; int nr;
    int wait_until;                       /* WAIT: tick-ig várakozás */
    /* játék-bemenet (Keringő-bővítés): bit0 bal, 1 jobb, 2 fel, 3 le, 4 tűz */
    u8 keys;
    /* fájlrendszer a ramfs-en: Q_ELNEV.BAS */
    char fs_names[12][16];
} quartz4_t;

void q4k_init(quartz4_t *q);
void q4k_reset(quartz4_t *q);
int  q4k_command(quartz4_t *q, const char *line);       /* 0 OK, 1 STOP */
void q4k_run(quartz4_t *q);
int  q4k_poll(quartz4_t *q, int maxlines);              /* futó program léptetése */
void q4k_mcu_run(quartz4_t *q, int maxcycles);          /* gépi kód futtatása */
/* videó-műveletek (a rajzoló az appban) */
void q4k_putch(quartz4_t *q, int code);
void q4k_print(quartz4_t *q, const char *s);
void q4k_print_at(quartz4_t *q, int x, int y, const char *s);
void q4k_clear(quartz4_t *q);
void q4k_set_mode(quartz4_t *q, int m);
void q4k_plot(quartz4_t *q, int x, int y);
void q4k_unplot(quartz4_t *q, int x, int y);
int  q4k_point(quartz4_t *q, int x, int y);
void q4k_line(quartz4_t *q, int x1, int y1, int x2, int y2);
/* beépített programok */
int  q4k_builtin_count(void);
const char *q4k_builtin_name(int i);
const char *q4k_builtin_text(int i);
/* fájlkezelés a ramfs-en keresztül */
int  q4k_save(quartz4_t *q, const char *name);
int  q4k_load(quartz4_t *q, const char *name);
int  q4k_files(quartz4_t *q);                /* a képernyőre írja a listát */
#endif
