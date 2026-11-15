/* serial.c — COM1 soros debug log */
#include "kernel.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);      /* interrupt ki */
    outb(COM1 + 3, 0x80);      /* DLAB = 1 */
    outb(COM1 + 0, 0x01);      /* 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);      /* 8N1 */
    outb(COM1 + 2, 0xC7);      /* FIFO */
    outb(COM1 + 4, 0x0B);      /* RTS/DTR */
}

void serial_putc(char c) {
    int guard = 0;
    while (!(inb(COM1 + 5) & 0x20) && guard++ < 100000) { }
    outb(COM1, (u8)c);
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

/* minimál printf: %s %c %d %u %x %%, fix szélesség %8x támogatás nélkül */
static void kprint_dec(u32 v, int neg) {
    char buf[12]; int i = 0;
    do { buf[i++] = '0' + (v % 10); v /= 10; } while (v);
    if (neg) serial_putc('-');
    while (i) serial_putc(buf[--i]);
}

void kprintf(const char *fmt, ...) {
    u32 *arg = (u32 *)((char *)&fmt + sizeof(char *));
    (void)arg;
    /* változó argumentumok kézi bejárása */
    u32 *ap = (u32 *)(((char *)&fmt) + sizeof(char *));
    const char *p = fmt;
    while (*p) {
        if (*p != '%') { serial_putc(*p++); continue; }
        p++;
        if (*p == 's') { const char *s = (const char *)(void *)*ap++; serial_puts(s ? s : "(null)"); }
        else if (*p == 'c') { serial_putc((char)*ap++); }
        else if (*p == 'd') { s32 v = (s32)*ap++; kprint_dec(v < 0 ? (u32)(-v) : (u32)v, v < 0); }
        else if (*p == 'u') { kprint_dec(*ap++, 0); }
        else if (*p == 'x') {
            u32 v = *ap++;
            char hex[9]; int i;
            for (i = 0; i < 8; i++) { int d = (v >> ((7 - i) * 4)) & 0xF; hex[i] = d < 10 ? '0' + d : 'A' + d - 10; }
            hex[8] = 0;
            /* vezető nullák levágása */
            char *h = hex; while (*h == '0' && h[1]) h++;
            serial_puts(h);
        }
        else if (*p == '%') serial_putc('%');
        p++;
    }
}

/* pufferbe formázás (ksnprintf): %s %c %d %u %x */
static void ksn_put(char **bp, char *end, const char *s) {
    while (*s && *bp < end) *(*bp)++ = *s++;
}
static void ksn_dec_w(char **bp, char *end, u32 v, int width, char pad) {
    char t[12]; int i = 0;
    do { t[i++] = '0' + (v % 10); v /= 10; } while (v);
    int nd = i;                                   /* számjegyek száma */
    while (nd < width && *bp < end) { *(*bp)++ = pad; nd++; }
    while (i && *bp < end) *(*bp)++ = t[--i];
}
void ksnprintf(char *buf, int bufsize, const char *fmt, ...) {
    char *b = buf;
    char *end = buf + (bufsize > 8 ? bufsize - 1 : 1);   /* KEMÉNY korlát */
    u32 *ap = (u32 *)(((char *)&fmt) + sizeof(char *));
    const char *p = fmt;
    while (*p && b < end) {
        if (*p != '%') { *b++ = *p++; continue; }
        p++;
        int width = 0; char pad = ' ';
        if (*p == '0') { pad = '0'; p++; while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; } }
        if (*p == 's') {
            /* %s: szintén korlátos másolás */
            const char *sv = (const char *)(void *)*ap++;
            while (*sv && b < end) *b++ = *sv++;
        }
        else if (*p == 'c') { if (b < end) *b++ = (char)*ap++; }
        else if (*p == 'd') { s32 v = (s32)*ap++; if (v < 0) { if (b < end) *b++ = '-'; ksn_dec_w(&b, end, (u32)(-v), width, pad); } else ksn_dec_w(&b, end, (u32)v, width, pad); }
        else if (*p == 'u') ksn_dec_w(&b, end, *ap++, width, pad);
        else if (*p == 'x') {
            u32 v = *ap++;
            char hex[9];
            for (int i = 0; i < 8; i++) { int d = (v >> ((7 - i) * 4)) & 0xF; hex[i] = d < 10 ? '0' + d : 'A' + d - 10; }
            hex[8] = 0;
            char *h = hex;
            while (*h == '0' && h[1]) h++;
            int hl = 0; { const char *q = h; while (*q++) hl++; }
            for (int z = hl; z < width && b < end; z++) *b++ = pad;
            ksn_put(&b, end, h);
        }
        else if (*p == '%') { if (b < end) *b++ = '%'; }
        p++;
    }
    *b = 0;
}
