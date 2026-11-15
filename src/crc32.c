/* crc32.c — standard CRC-32 (zlib polinom 0xEDB88320), táblás számítás */
#include "kernel.h"

static u32 crc_table[256];
static int table_ready = 0;

static void crc32_make_table(void) {
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    table_ready = 1;
}

u32 crc32_calc(u32 crc, const u8 *buf, u32 len) {
    if (!table_ready) crc32_make_table();
    crc = crc ^ 0xFFFFFFFFu;
    for (u32 i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
