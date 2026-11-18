/* zx81_z80.c — compact Z80 core for Keringo OS.
 * First implementation slice: documented base/CB/ED instruction groups,
 * flags, interrupt state and deterministic stepping. No libc, no dynamic
 * allocation. ZX81 peripherals are kept behind callbacks in zx81.c.
 */
#include "kernel.h"
#include "zx81.h"

#define F_S 0x80
#define F_Z 0x40
#define F_Y 0x20
#define F_H 0x10
#define F_X 0x08
#define F_PV 0x04
#define F_N 0x02
#define F_C 0x01

static u8 rd(z80_t *c, u16 a) { return c->mem_read ? c->mem_read(a,c->ctx) : 0xFF; }
static void wr(z80_t *c, u16 a, u8 v) { if(c->mem_write) c->mem_write(a,v,c->ctx); }
static u8 fetch(z80_t *c) { u8 v=rd(c,c->pc++); c->r=(u8)((c->r&0x80)|((c->r+1)&0x7F)); return v; }
static u16 fetch16(z80_t *c) { u8 l=fetch(c),h=fetch(c); return (u16)(l|((u16)h<<8)); }
static u16 bc(z80_t*c){return (u16)((c->b<<8)|c->c);}
static u16 de(z80_t*c){return (u16)((c->d<<8)|c->e);}
static u16 hl(z80_t*c){return (u16)((c->h<<8)|c->l);}
static void setbc(z80_t*c,u16 v){c->b=v>>8;c->c=v;}
static void setde(z80_t*c,u16 v){c->d=v>>8;c->e=v;}
static void sethl(z80_t*c,u16 v){c->h=v>>8;c->l=v;}
static u16 *rp(z80_t*c,int p){static u16 dummy; switch(p){case 0: dummy=bc(c);return &dummy;case 1:dummy=de(c);return &dummy;case 2:dummy=hl(c);return &dummy;default:dummy=c->sp;return &dummy;}}
static u8 getr(z80_t*c,int r){switch(r){case 0:return c->b;case 1:return c->c;case 2:return c->d;case 3:return c->e;case 4:return c->h;case 5:return c->l;case 6:return rd(c,hl(c));default:return c->a;}}
static void setr(z80_t*c,int r,u8 v){switch(r){case 0:c->b=v;break;case 1:c->c=v;break;case 2:c->d=v;break;case 3:c->e=v;break;case 4:c->h=v;break;case 5:c->l=v;break;case 6:wr(c,hl(c),v);break;default:c->a=v;break;}}
static int parity(u8 v){v^=v>>4;v^=v>>2;v^=v>>1;return !(v&1);}
static u8 sz53(u8 v){return (v&(F_S|F_Y|F_X))|(v?0:F_Z);}
static void push(z80_t*c,u16 v){wr(c,--c->sp,(u8)(v>>8));wr(c,--c->sp,(u8)v);}
static u16 pop(z80_t*c){u8 l=rd(c,c->sp++),h=rd(c,c->sp++);return (u16)(l|((u16)h<<8));}
static u8 inc8(z80_t*c,u8 v){u8 n=v+1;c->f=(c->f&F_C)|sz53(n)|((v&15)==15?F_H:0)|(v==0x7F?F_PV:0);return n;}
static u8 dec8(z80_t*c,u8 v){u8 n=v-1;c->f=(c->f&F_C)|sz53(n)|F_N|((v&15)==0?F_H:0)|(v==0x80?F_PV:0);return n;}
static void alu(z80_t*c,int op,u8 v){
 u16 q;u8 a=c->a,n;
 switch(op&7){
 case 0: q=(u16)a+v;c->a=n=q;c->f=sz53(n)|(((a^v^n)&16)?F_H:0)|((~(a^v)&(a^n)&128)?F_PV:0)|(q>255?F_C:0);break;
 case 1:{u8 cy=c->f&F_C;q=(u16)a+v+cy;c->a=n=q;c->f=sz53(n)|(((a^v^n)&16)?F_H:0)|((~(a^v)&(a^n)&128)?F_PV:0)|(q>255?F_C:0);break;}
 case 2: q=(u16)a-v;c->a=n=q;c->f=sz53(n)|F_N|(((a^v^n)&16)?F_H:0)|(((a^v)&(a^n)&128)?F_PV:0)|(q>255?F_C:0);break;
 case 3:{u8 cy=c->f&F_C;q=(u16)a-v-cy;c->a=n=q;c->f=sz53(n)|F_N|(((a^v^n)&16)?F_H:0)|(((a^v)&(a^n)&128)?F_PV:0)|(q>255?F_C:0);break;}
 case 4:c->a&=v;c->f=sz53(c->a)|F_H|(parity(c->a)?F_PV:0);break;
 case 5:c->a^=v;c->f=sz53(c->a)|(parity(c->a)?F_PV:0);break;
 case 6:c->a|=v;c->f=sz53(c->a)|(parity(c->a)?F_PV:0);break;
 default:n=a-v;c->f=sz53(n)|F_N|(((a^v^n)&16)?F_H:0)|(((a^v)&(a^n)&128)?F_PV:0)|(a<v?F_C:0);break;
 }
}
static void add16(z80_t*c,u16 v){u32 q=(u32)hl(c)+v;u16 n=q;c->f=(c->f&(F_S|F_Z|F_PV))|((n>>8)&(F_Y|F_X))|(((hl(c)^v^n)&0x1000)?F_H:0)|(q>65535?F_C:0);sethl(c,n);}
static void cbop(z80_t*c,u8 op){int r=op&7,g=op>>6,y=(op>>3)&7;u8 v=getr(c,r),n=v;switch(g){case 0:{int cy;switch(y){case 0:cy=v>>7;n=(v<<1)|cy;break;case 1:cy=v&1;n=(v>>1)|(cy<<7);break;case 2:cy=v>>7;n=(v<<1)|(c->f&1);break;case 3:cy=v&1;n=(v>>1)|((c->f&1)<<7);break;case 4:cy=v>>7;n=v<<1;break;case 5:cy=v&1;n=(v>>1)|(v&128);break;case 6:cy=v>>7;n=(v<<1)|1;break;default:cy=v&1;n=v>>1;break;}c->f=sz53(n)|(parity(n)?F_PV:0)|(cy?F_C:0);setr(c,r,n);break;}case 1:{u8 m=(u8)(1<<y);c->f=(c->f&F_C)|F_H|((v&m)?0:F_Z|F_PV)|(y==7&&(v&128)?F_S:0)|(v&(F_Y|F_X));break;}case 2:setr(c,r,v&~(1<<y));break;default:setr(c,r,v|(1<<y));break;}}
void z80_init(z80_t*c){for(u32 i=0;i<sizeof(*c);i++)((u8*)c)[i]=0;c->sp=0xFFFF;c->iff1=c->iff2=0;}
void z80_step(z80_t*c){
 if(c->halted){c->r=(u8)((c->r&128)|((c->r+1)&127));c->tstates+=4;return;}
 if(c->iff1&&c->int_pending&&c->ei_delay==0){c->halted=0;c->iff1=c->iff2=0;push(c,c->pc);c->pc=0x0038;c->int_pending=0;c->tstates+=13;return;}
 u8 op=fetch(c),x=op>>6,y=(op>>3)&7,z=op&7,p=y>>1,q=y&1,v,n;u16 nn,addr;
 if(op==0xCB){cbop(c,fetch(c));c->tstates+=8;return;}
 if(op==0xED){u8 e=fetch(c),ey=(e>>3)&7,ez=e&7;switch(e){
 case 0x44:case 0x4C:case 0x54:case 0x5C:case 0x64:case 0x6C:case 0x74:case 0x7C: alu(c,2,c->a);c->a=(u8)-c->a;c->f=(c->f&~F_C)|(c->a?F_N:0)|F_C|sz53(c->a)|((c->a&15)?F_H:0)|(c->a==128?F_PV:0);break;
 case 0x45:case 0x55:case 0x5D:case 0x65:case 0x6D:case 0x75:case 0x7D:c->pc=pop(c);c->iff1=c->iff2;break;
 case 0x4D:c->pc=pop(c);c->iff1=c->iff2;c->reti_count++;break;
 case 0x46:case 0x4E:case 0x66:case 0x6E:c->im=0;break;
 case 0x56:case 0x76:c->im=1;break;
 case 0x5E:case 0x7E:c->im=2;break;
 case 0x47:c->i=c->a;break;case 0x4F:c->r=c->a;break;
 case 0x57:c->a=c->i;c->f=(c->f&F_C)|sz53(c->a)|(c->iff2?F_PV:0);break;
 case 0x5F:c->a=c->r;c->f=(c->f&F_C)|sz53(c->a)|(c->iff2?F_PV:0);break;
 case 0xA0:case 0xA8:case 0xB0:case 0xB8:{int dir=(e==0xA8||e==0xB8)?-1:1;v=rd(c,hl(c));wr(c,de(c),v);sethl(c,hl(c)+dir);setde(c,de(c)+dir);setbc(c,bc(c)-1);c->f=(c->f&(F_S|F_Z|F_C))|(bc(c)?F_PV:0);if((e==0xB0||e==0xB8)&&bc(c))c->pc-=2;break;}
 case 0xA1:case 0xA9:case 0xB1:case 0xB9:v=rd(c,hl(c));alu(c,7,v);sethl(c,hl(c)+((e==0xA9||e==0xB9)?-1:1));setbc(c,bc(c)-1);if((e==0xB1||e==0xB9)&&bc(c)&&!(c->f&F_Z))c->pc-=2;break;
 case 0xA2:case 0xAA:case 0xB2:case 0xBA:v=rd(c,hl(c));if(c->io_write)c->io_write(bc(c),v,c->ctx);sethl(c,hl(c)+((e==0xAA||e==0xBA)?-1:1));setbc(c,bc(c)-1);c->f=(c->f&F_C)|(bc(c)?F_PV:0)|F_N;break;
 case 0xA3:case 0xAB:case 0xB3:case 0xBB:v=c->io_read?c->io_read(bc(c),c->ctx):0xFF;wr(c,hl(c),v);sethl(c,hl(c)+((e==0xAB||e==0xBB)?-1:1));setbc(c,bc(c)-1);c->f=(c->f&F_C)|(bc(c)?F_PV:0)|F_N;break;
 default:(void)ey;(void)ez;break;}c->tstates+=12;if(c->ei_delay)c->ei_delay--;return;}
 if(x==0){
  switch(z){
  case 0:switch(y){case 0:break;case 1:{u16 t=(c->a<<8)|c->f;c->a=c->a2;c->f=c->f2;c->a2=t>>8;c->f2=t;break;}case 2:{v=c->b--;n=fetch(c);if(v==1)c->pc+=(s8)n;c->tstates+=1;break;}case 3:n=fetch(c);c->pc+=(s8)n;break;default:n=fetch(c);if((y==4&&c->b==0)||(y==5&&c->b!=0)||(y==6&&!(c->f&F_Z))||(y==7&&(c->f&F_Z)))c->pc+=(s8)n;break;}break;
  case 1:if(!q)setbc(c,fetch16(c)),c->tstates+=6;else add16(c,(p==0?bc(c):p==1?de(c):p==2?hl(c):c->sp));break;
  case 2:addr=(p==0?bc(c):p==1?de(c):fetch16(c));if(!q)wr(c,addr,c->a);else c->a=rd(c,addr);break;
  case 3:nn=(p==0?bc(c):p==1?de(c):p==2?hl(c):c->sp);nn+=q?-1:1;if(p==0)setbc(c,nn);else if(p==1)setde(c,nn);else if(p==2)sethl(c,nn);else c->sp=nn;break;
  case 4:setr(c,y,inc8(c,getr(c,y)));break;case 5:setr(c,y,dec8(c,getr(c,y)));break;
  case 6:setr(c,y,fetch(c));break;
  case 7:switch(y){case 0:n=c->a;c->a=(n<<1)|(n>>7);c->f=(c->f&(F_S|F_Z|F_PV))|(c->a&(F_Y|F_X))|(n>>7);break;case 1:n=c->a;c->a=(n>>1)|(n<<7);c->f=(c->f&(F_S|F_Z|F_PV))|(c->a&(F_Y|F_X))|(n&1);break;case 2:n=c->a;c->a=(n<<1)|(c->f&1);c->f=(c->f&(F_S|F_Z|F_PV))|(c->a&(F_Y|F_X))|(n>>7);break;case 3:n=c->a;c->a=(n>>1)|((c->f&1)<<7);c->f=(c->f&(F_S|F_Z|F_PV))|(c->a&(F_Y|F_X))|(n&1);break;case 4:/* DAA */{u8 adj=0;int cy=c->f&1;if((c->f&F_H)||(!(c->f&F_N)&&(c->a&15)>9))adj|=6;if(cy||(!(c->f&F_N)&&c->a>0x99))adj|=0x60;if(c->f&F_N)c->a-=adj;else c->a+=adj;c->f=(c->f&F_N)|sz53(c->a)|(parity(c->a)?F_PV:0)|cy;break;}case 5:c->a^=255;c->f=(c->f&(F_S|F_Z|F_PV|F_C))|(c->a&(F_Y|F_X))|F_H|F_N;break;case 6:c->f=(c->f&(F_S|F_Z|F_PV))|(c->a&(F_Y|F_X))|F_C;break;case 7:n=c->f&1;c->f=(c->f&(F_S|F_Z|F_PV))|(c->a&(F_Y|F_X))|(n?F_H:0)|(n?0:F_C);break;}break;
  }
 } else if(x==1){if(op==0x76)c->halted=1;else setr(c,y,getr(c,z));}
 else if(x==2)alu(c,y,getr(c,z));
 else {switch(z){
 case 0:if((y==0&&!(c->f&F_Z))||(y==1&&(c->f&F_Z))||(y==2&&!(c->f&F_C))||(y==3&&(c->f&F_C))||(y==4&&!(c->f&F_PV))||(y==5&&(c->f&F_PV))||(y==6&&!(c->f&F_S))||(y==7&&(c->f&F_S)))c->pc=pop(c);else c->sp+=0;break;
 case 1:if(!q){nn=pop(c);if(p==0){c->c=nn;c->b=nn>>8;}else if(p==1){c->e=nn;c->d=nn>>8;}else if(p==2)sethl(c,nn);else {c->f=c->a;c->a=nn>>8;}}else {if(p==0)c->pc=pop(c);else if(p==1){c->pc=pop(c);c->iff1=c->iff2;}else if(p==2)c->pc=hl(c);else c->sp=hl(c);}break;
 case 2:nn=fetch16(c);if((y==0&&!(c->f&F_Z))||(y==1&&(c->f&F_Z))||(y==2&&!(c->f&F_C))||(y==3&&(c->f&F_C))||(y==4&&!(c->f&F_PV))||(y==5&&(c->f&F_PV))||(y==6&&!(c->f&F_S))||(y==7&&(c->f&F_S)))c->pc=nn;break;
 case 3:nn=fetch16(c);if(y==0)c->pc=nn;else if(y==2){n=fetch(c);if(c->io_write)c->io_write((u16)((c->a<<8)|n),c->a,c->ctx);}else if(y==3){n=fetch(c);c->a=c->io_read?c->io_read((u16)((c->a<<8)|n),c->ctx):0xFF;}else if(y==4){u16 t=rd(c,nn)|((u16)rd(c,nn+1)<<8);wr(c,nn,c->l);wr(c,nn+1,c->h);sethl(c,t);}else if(y==6)c->iff1=c->iff2=0;else if(y==7)c->iff1=c->iff2=1,c->ei_delay=2;break;
 case 4:nn=fetch16(c);if((y==0&&!(c->f&F_Z))||(y==1&&(c->f&F_Z))||(y==2&&!(c->f&F_C))||(y==3&&(c->f&F_C))||(y==4&&!(c->f&F_PV))||(y==5&&(c->f&F_PV))||(y==6&&!(c->f&F_S))||(y==7&&(c->f&F_S))){push(c,c->pc);c->pc=nn;}break;
 case 5:if(!q){nn=(p==0?bc(c):p==1?de(c):p==2?hl(c):(u16)((c->a<<8)|c->f));push(c,nn);}else if(p==0){nn=fetch16(c);push(c,c->pc);c->pc=nn;}break;
 case 6:alu(c,y,fetch(c));break;
 case 7:push(c,c->pc);c->pc=y*8;break;
 }}
 if(c->ei_delay)c->ei_delay--;
 c->tstates+=4;
}
