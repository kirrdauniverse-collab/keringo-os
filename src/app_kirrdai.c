#include "kernel.h"

/* KIRRDA - deterministic local assistant: keyword rules + tiny memory. */
static char q[128],mem[6][80]; static int qn,mn;
static void say(const char *s,int row){gfx_draw_text(16,48+row*18,s,0xFF50E878,0xFF10151A);}
static int has(const char *s,const char *k){int i=0;for(;s[i];i++){int j=0;while(k[j]&&s[i+j]==k[j])j++;if(!k[j])return 1;}return 0;}
void kirrda_enter(void){qn=0;mn=0;}
void kirrda_exit(void){}
void kirrda_key(u8 sc,u8 rel,u8 a){
 if(rel)return;
 if(sc==KEY_ENTER){q[qn]=0;gfx_fill_rect(8,42,gfx_width()-16,220,0xFF10151A);
   if(has(q,"hello")||has(q,"szia"))say("KIRRDA: Szia! Lokalisan, internet nelkul is itt vagyok.",0);
   else if(has(q,"help")||has(q,"segit"))say("KIRRDA: hello, time, memory, remember ..., recall, games, astro",0);
   else if(has(q,"time")||has(q,"ido")){char b[64];ksnprintf(b,sizeof(b),"KIRRDA: tick=%u  (lokalis rendszerido-alap)",tick_count());say(b,0);}
   else if(has(q,"memory")||has(q,"memoria")){char b[64];ksnprintf(b,sizeof(b),"KIRRDA: %u KB szabad oldal",mem_free_pages()*4);say(b,0);}
   else if(has(q,"games")||has(q,"jatekok"))say("KIRRDA: Labirintus, Pokoljaró, Elite, Varosepito, Flipper.",0);
   else if(has(q,"astro")||has(q,"csillag"))say("KIRRDA: A csillagok ma is ott vannak. Az univerzum nem frissithető USB-rol.",0);
   else if(has(q,"remember ")||has(q,"jegyezd")){if(mn<6){for(int i=0;i<80;i++){mem[mn][i]=q[i];if(!q[i])break;}mn++;say("KIRRDA: Elraktam a helyi memoriamba.",0);}else say("KIRRDA: Betelt a mini memoriám.",0);}
   else if(has(q,"recall")||has(q,"emlekszel")){for(int i=0;i<mn;i++)say(mem[i],i);if(!mn)say("KIRRDA: Ures a memoriam.",0);}
   else say("KIRRDA: Ezt még nem tanítottad meg. De nem sértődtem meg.",0);
   qn=0;return;
 }
 if(sc==0x0E&&qn){qn--;q[qn]=0;return;}
 if(a>=32&&a<127&&qn<127)q[qn++]=(char)a;
}
void kirrda_frame(u32 t){(void)t;app_chrome("KIRRDA - local AI","Enter: ask   Esc: close",settings_accent_color());gfx_draw_text(16,30,"KIRRDA 0.1  |  rule engine + local memory",0xFFF0F0F0,0xFF10151A);gfx_rect(10,gfx_height()-42,gfx_width()-20,28,0xFF40505A);gfx_draw_text(18,gfx_height()-33,q,0xFFF0F0F0,0xFF10151A);}
