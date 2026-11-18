#include "kernel.h"

/* KIRRDA - deterministic local assistant: keyword rules + tiny memory.
 * Karbantarto-javitas (PR#2): beszélgetés-történet + teljes ujrarajzolas
 * frame-enkent (az asztal elozo kepe nem latszik at). */
static char q[128],mem[6][80];
static char hist[8][96];
static int qn,mn,histn;
static void say(const char *s){if(histn>=8)return;int i=0;while(s[i]&&i<95){hist[histn][i]=s[i];i++;}hist[histn][i]=0;histn++;}
static int has(const char *s,const char *k){int i=0;for(;s[i];i++){int j=0;while(k[j]&&s[i+j]==k[j])j++;if(!k[j])return 1;}return 0;}
void kirrda_enter(void){qn=0;mn=0;histn=0;}
void kirrda_exit(void){}
void kirrda_key(u8 sc,u8 rel,u8 a){
 if(rel)return;
 if(sc==KEY_ENTER){q[qn]=0;
   if(qn){
     say(q);
     if(has(q,"hello")||has(q,"szia"))say("KIRRDA: Szia! Lokalisan, internet nelkul is itt vagyok.");
     else if(has(q,"help")||has(q,"segit"))say("KIRRDA: hello, time, memory, remember ..., recall, games, astro");
     else if(has(q,"time")||has(q,"ido")){char b[64];ksnprintf(b,sizeof(b),"KIRRDA: tick=%u  (lokalis rendszerido-alap)",tick_count());say(b);}
     else if(has(q,"memory")||has(q,"memoria")){char b[64];ksnprintf(b,sizeof(b),"KIRRDA: %u KB szabad oldal",mem_free_pages()*4);say(b);}
     else if(has(q,"games")||has(q,"jatekok"))say("KIRRDA: Labirintus, Pokoljaro, Elite, Varosepito, Flipper.");
     else if(has(q,"astro")||has(q,"csillag"))say("KIRRDA: A csillagok ma is ott vannak. Az univerzum nem frissitheto USB-rol.");
     else if(has(q,"remember ")||has(q,"jegyezd")){if(mn<6){for(int i=0;i<80;i++){mem[mn][i]=q[i];if(!q[i])break;}mn++;say("KIRRDA: Elraktam a helyi memoriamba.");}else say("KIRRDA: Betelt a mini memoriam.");}
     else if(has(q,"recall")||has(q,"emlekszel")){for(int i=0;i<mn;i++)say(mem[i]);if(!mn)say("KIRRDA: Ures a memoriam.");}
     else say("KIRRDA: Ezt meg nem tanitottad meg. De nem sertodtem meg.");
   }
   qn=0;return;
 }
 if(sc==0x0E&&qn){qn--;q[qn]=0;return;}
 if(a>=32&&a<127&&qn<127)q[qn++]=(char)a;
}
void kirrda_frame(u32 t){
 (void)t;int W=gfx_width(),H=gfx_height();
 app_chrome("KIRRDA - local AI","Enter: ask   Esc: close",settings_accent_color());
 gfx_fill_rect(0,23,W,H-44,0xFF10151A);
 gfx_draw_text(16,32,"KIRRDA 0.1  |  rule engine + local memory",0xFFF0F0F0,0xFF10151A);
 int top=52;
 for(int i=0;i<histn;i++)gfx_draw_text(16,top+i*18,hist[i],0xFF50E878,0xFF10151A);
 gfx_rect(10,H-42,W-20,28,0xFF40505A);
 gfx_draw_text(18,H-33,q,0xFFF0F0F0,0xFF10151A);
}
