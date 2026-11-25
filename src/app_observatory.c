#include "kernel.h"

/* Keringo Observatory - tiny procedural sky, intentionally dependency-free. */
static float ang; static int mode;
static u32 stars[96][3];
void obs_enter(void){app_srand(0x5A17u);for(int i=0;i<96;i++){stars[i][0]=app_rand()%1000;stars[i][1]=app_rand()%1000;stars[i][2]=1+app_rand()%3;}ang=0;mode=0;}
void obs_exit(void){}
void obs_key(u8 sc,u8 rel,u8 a){if(rel)return;if(sc==KEY_LEFT)ang-=0.08f;if(sc==KEY_RIGHT)ang+=0.08f;if(a=='m'||a=='M')mode^=1;}
void obs_frame(u32 tick){
 int W=gfx_width(),H=gfx_height(),cx=W/2,cy=(H-21)/2+23;gfx_clear(0xFF030712);
 for(int i=0;i<96;i++){float x=((int)stars[i][0]-500)/500.0f,y=((int)stars[i][1]-500)/500.0f;float c=kosz_cosf(ang),s=kosz_sinf(ang);int sx=cx+(int)((x*c-y*s)*cx),sy=cy+(int)((x*s+y*c)*cy);if(sx>2&&sy>25&&sx<W-2&&sy<H-22)gfx_fill_rect(sx,sy,stars[i][2],stars[i][2],0xFFF0F0D0);}
 /* ekliptika + bolygók, egyszerűsített körpályák */
 for(int r=50;r<230;r+=42) app_circle(cx,cy,r,0x30305070,0);
 for(int p=0;p<5;p++){float a=ang*0.3f+(float)p*1.25f;int r=50+p*42;int x=cx+(int)(kosz_cosf(a)*r),y=cy+(int)(kosz_sinf(a)*r*0.55f);app_circle(x,y,3+p%3,p==2?0xFFE8B24A:0xFF80B0E8,1);}
 gfx_fill_rect(cx-2,cy-2,5,5,0xFFFFE060);
 app_chrome("Keringo Observatory","Bal/Jobb: égforgás   M: ekliptika   procedurális mini planetarium",settings_accent_color());
 char b[80];ksnprintf(b,sizeof(b),"T=%u  |  csillagok: 96  |  Naprendszer: 5 palya",tick/100);gfx_draw_text(12,32,b,0xFF8FA0AC,0xFF030712);
}
