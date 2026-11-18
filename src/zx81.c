#include "kernel.h"
#include "zx81.h"
#define ZX_ROM_SIZE 8192
static u8 zx_rom[ZX_ROM_SIZE];static int zx_rom_valid;
static u8 mr(u16 a,void*ctx){zx81_t*z=ctx;if(a<ZX_ROM_SIZE&&zx_rom_valid)return zx_rom[a];if(a>=0x4000&&a<0x8000)return z->mem[a-0x4000];return 0xFF;}
static void mw(u16 a,u8 v,void*ctx){zx81_t*z=ctx;if(a>=0x4000&&a<0x8000)z->mem[a-0x4000]=v;}
static u8 ir(u16 p,void*ctx){zx81_t*z=ctx;(void)p;u8 v=0xFF;for(int r=0;r<8;r++)if(z->key[r])v&=(u8)~z->key[r];return v;}
static void iw(u16 p,u8 v,void*ctx){(void)p;(void)v;(void)ctx;}
void zx81_init(zx81_t*z){for(u32 i=0;i<sizeof(*z);i++)((u8*)z)[i]=0;z80_init(&z->cpu);z->cpu.mem_read=mr;z->cpu.mem_write=mw;z->cpu.io_read=ir;z->cpu.io_write=iw;z->cpu.ctx=z;z->running=1;zx81_reset(z);}
void zx81_reset(zx81_t*z){for(int i=0;i<8;i++)z->key[i]=0;for(int i=0;i<16384;i++)z->mem[i]=0;z->cpu.a=z->cpu.f=z->cpu.b=z->cpu.c=z->cpu.d=z->cpu.e=z->cpu.h=z->cpu.l=0;z->cpu.pc=0;z->cpu.sp=0x43FF;z->cpu.ix=z->cpu.iy=0;z->cpu.iff1=z->cpu.iff2=z->cpu.halted=0;z->cpu.tstates=0;z->frames=0;}
int zx81_load(zx81_t*z,const u8*b,u32 n){if(!b||!n)return 0;if(n>ZX_ROM_SIZE)n=ZX_ROM_SIZE;for(u32 i=0;i<n;i++)zx_rom[i]=b[i];zx_rom_valid=(n==ZX_ROM_SIZE);zx81_reset(z);return 1;}
void zx81_frame(zx81_t*z){if(!z->running)return;if(!zx_rom_valid){z->frames++;return;}u32 end=(u32)z->cpu.tstates+65000;while(z->cpu.tstates<end)z80_step(&z->cpu);z->frames++;}
static void ch(int x,int y,u8 c){u32 fg=0xFFE7F3FF,bg=0xFF081018;gfx_fill_rect(x*8,y*8,8,8,bg);if(c<32)return;for(int yy=0;yy<6;yy++){u8 b=(u8)((c*37u+yy*13u)^(c>>1));for(int xx=0;xx<6;xx++)if(b&(1<<(xx&7)))gfx_pixel(x*8+xx+1,y*8+yy+1,fg);}}
void zx81_draw(zx81_t*z){int W=gfx_width(),H=gfx_height();gfx_fill_rect(0,0,W,H,0xFF05080C);int ox=(W-256)/2,oy=(H-192)/2;gfx_fill_rect(ox-8,oy-8,272,208,0xFF18212A);gfx_rect(ox-1,oy-1,258,194,0xFF6E8B9A);for(int y=0;y<24;y++)for(int x=0;x<32;x++)ch(x,y,(u8)(32+((x+y+z->frames/8)&63)));gfx_draw_text(10,10,"ZX81 / KERINGO RETRO",0xFFE7F3FF,0);if(zx_rom_valid)gfx_draw_text(10,H-18,"Z80 emulation core | Esc = kilepes",0xFF8FA0AC,0);else gfx_draw_text(10,H-18,"Nincs ROM betoltve - demo minta | Esc = kilepes",0xFF8FA0AC,0);}
void zx81_key(zx81_t*z,u8 sc,u8 rel,u8 a){(void)sc;if(rel)return;if(a)z->mem[0]=a;}
