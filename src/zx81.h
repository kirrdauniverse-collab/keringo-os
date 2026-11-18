#ifndef KERINGO_ZX81_H
#define KERINGO_ZX81_H
#include "kernel.h"
typedef struct z80{u8 a,f,b,c,d,e,h,l,a2,f2,b2,c2,d2,e2,h2,l2,i,r,im,iff1,iff2,halted,int_pending,ei_delay;u16 pc,sp,ix,iy;u64 tstates;u8(*mem_read)(u16,void*);void(*mem_write)(u16,u8,void*);u8(*io_read)(u16,void*);void(*io_write)(u16,u8,void*);void*ctx;u32 reti_count;}z80_t;
typedef struct{z80_t cpu;u8 mem[16384];u8 key[8];u8 running;u8 border;u32 frames;}zx81_t;
void z80_init(z80_t*);void z80_step(z80_t*);
void zx81_init(zx81_t*);void zx81_reset(zx81_t*);void zx81_frame(zx81_t*);void zx81_key(zx81_t*,u8,u8,u8);void zx81_draw(zx81_t*);int zx81_load(zx81_t*,const u8*,u32);
void zx81_app_enter(void);void zx81_app_frame(u32);void zx81_app_key(u8,u8,u8);void zx81_app_exit(void);
#endif
