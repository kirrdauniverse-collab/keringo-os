#ifndef KERINGO_ZX81_H
#define KERINGO_ZX81_H
#include "kernel.h"
typedef struct z80 {
 u8 a,f,b,c,d,e,h,l, a2,f2,b2,c2,d2,e2,h2,l2,i,r,im,iff1,iff2,halted,int_pending,ei_delay;
 u16 pc,sp,ix,iy;
 u64 tstates;
 u8 (*mem_read)(u16,void*);
 void (*mem_write)(u16,u8,void*);
 void *ctx;
 u32 reti_count;
} z80_t;
void z80_init(z80_t*);
void z80_step(z80_t*);
#endif
