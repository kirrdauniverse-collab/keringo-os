#include "kernel.h"

/* Orbit Miner - tiny native asteroid game. */
#define NAST 24
typedef struct{int x,y,r;u8 alive;} ast_t;
static ast_t a[NAST];static int sx,sy,score,fuel;static u32 seed;
void miner_enter(void){seed=tick_count()+0x91;app_srand(seed);sx=gfx_width()/2;sy=gfx_height()/2;score=0;fuel=100;for(int i=0;i<NAST;i++){a[i].x=30+app_rand()%(gfx_width()-60);a[i].y=45+app_rand()%(gfx_height()-100);a[i].r=5+app_rand()%12;a[i].alive=1;}}
void miner_exit(void){}
void miner_key(u8 sc,u8 rel,u8 ch){if(rel)return;if(sc==KEY_LEFT)sx-=8;if(sc==KEY_RIGHT)sx+=8;if(sc==KEY_UP)sy-=8;if(sc==KEY_DOWN)sy+=8;if(ch==' '){for(int i=0;i<NAST;i++)if(a[i].alive){int dx=a[i].x-sx,dy=a[i].y-sy;if(dx*dx+dy*dy<(a[i].r+12)*(a[i].r+12)){a[i].alive=0;score+=10;fuel+=3;break;}}}if(ch=='r'||ch=='R')miner_enter();if(sx<15)sx=15;if(sx>gfx_width()-15)sx=gfx_width()-15;if(sy<40)sy=40;if(sy>gfx_height()-45)sy=gfx_height()-45;}
void miner_frame(u32 t){(void)t;gfx_clear(0xFF03050A);for(int i=0;i<NAST;i++)if(a[i].alive){app_circle(a[i].x,a[i].y,a[i].r,0xFF7A8088,1);app_circle(a[i].x-2,a[i].y-2,a[i].r/3,0xFFB0A080,1);}app_circle(sx,sy,10,0xFF40C0E8,1);app_line(sx-14,sy,sx+14,sy,0xFFFFFFFF);app_line(sx,sy-14,sx,sy+14,0xFFFFFFFF);char b[96];ksnprintf(b,sizeof(b),"Pont: %d   Uzemanyag: %d   Space: bányászat",score,fuel);app_chrome("Orbit Miner","Nyilak: mozgas   Space: banyaszat   R: ujra",settings_accent_color());gfx_draw_text(12,34,b,0xFFF0F0F0,0xFF03050A);}
