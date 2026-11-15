/* vfs.c — minimál VFS: /cd (ISO9660, olvasható) + /mem (RAMFS, írható) */
#include "kernel.h"

/* iso9660.c állapota */

/* ---------- RAMFS ---------- */
#define RAMFS_MAX 8
#define RAMFS_CAP 4096
typedef struct { char name[32]; char buf[RAMFS_CAP]; int size; int used; } ramfile_t;
static ramfile_t ramfs[RAMFS_MAX];

int ramfs_write(const char *name, const char *data, int len) {
    if (len > RAMFS_CAP) len = RAMFS_CAP;
    for (int i = 0; i < RAMFS_MAX; i++) {
        if (ramfs[i].used && ramfs_strcmp(ramfs[i].name, name)) {
            for (int j = 0; j < len; j++) ramfs[i].buf[j] = data[j];
            ramfs[i].size = len;
            return 1;
        }
    }
    for (int i = 0; i < RAMFS_MAX; i++) {
        if (!ramfs[i].used) {
            int j;
            for (j = 0; j < 31 && name[j]; j++) ramfs[i].name[j] = name[j];
            ramfs[i].name[j] = 0;
            for (int k = 0; k < len; k++) ramfs[i].buf[k] = data[k];
            ramfs[i].size = len;
            ramfs[i].used = 1;
            return 1;
        }
    }
    return 0;
}

int ramfs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

int ramfs_count(void) { int n = 0; for (int i = 0; i < RAMFS_MAX; i++) if (ramfs[i].used) n++; return n; }
const char *ramfs_name(int i) { return ramfs[i].used ? ramfs[i].name : ""; }
int ramfs_size(int i) { return ramfs[i].used ? ramfs[i].size : 0; }
int ramfs_read(const char *name, char *buf, int bufsize, int *outlen) {
    for (int i = 0; i < RAMFS_MAX; i++) {
        if (ramfs[i].used && ramfs_strcmp(ramfs[i].name, name)) {
            int n = ramfs[i].size < bufsize ? ramfs[i].size : bufsize;
            for (int j = 0; j < n; j++) buf[j] = ramfs[i].buf[j];
            *outlen = n;
            return 1;
        }
    }
    return 0;
}

/* ---------- VFS ---------- */
int vfs_list(const char *dir, char names[][64], u32 *sizes, u8 *flags, int max) {
    if (vfs_strcmp(dir, "/cd"))  return iso_mounted() ? iso_root_list(names, 0, sizes, flags, max) : -1;
    if (vfs_strcmp(dir, "/mem")) {
        int n = 0;
        for (int i = 0; i < RAMFS_MAX && n < max; i++) {
            if (ramfs[i].used) {
                int j;
                for (j = 0; j < 63 && ramfs[i].name[j]; j++) names[n][j] = ramfs[i].name[j];
                names[n][j] = 0;
                sizes[n] = ramfs[i].size;
                flags[n] = 0;
                n++;
            }
        }
        return n;
    }
    return -1;
}

int vfs_read_file(const char *path, char *buf, int bufsize, int *outlen) {
    /* /cd/NEV vagy /mem/NEV */
    if (path[0] == '/' && path[1] == 'c' && path[2] == 'd' && path[3] == '/') {
        u32 lba, size;
        if (!iso_mounted()) return 0;
        if (iso_find(path + 4, &lba, &size)) return iso_read_file(lba, size, buf, bufsize, outlen);
        return 0;
    }
    if (path[0] == '/' && path[1] == 'm' && path[2] == 'e' && path[3] == 'm' && path[4] == '/') {
        return ramfs_read(path + 5, buf, bufsize, outlen);
    }
    return 0;
}

int vfs_write_file(const char *path, const char *data, int len) {
    if (path[0] == '/' && path[1] == 'm' && path[2] == 'e' && path[3] == 'm' && path[4] == '/') {
        return ramfs_write(path + 5, data, len);
    }
    return 0;   /* csak /mem írható */
}

int vfs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
