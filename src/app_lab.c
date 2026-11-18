#include "kernel.h"

/* Keringo Lab - Mandelbrot + particle playground. */
static float zoom,cx0,cy0; static int mode;
void lab_enter(void){zoom=1.0f;cx0=-0.55f;cy0=0;mode=0;}
void lab_exit(void){}
void lab_key(u8 sc,u8 rel,u8 a){if(rel)return;if(sc==KEY_LEFT)cx0-=0.1f/zoom;if(sc==KEY_RIGHT)cx0+=0.1f/zoom;if(sc==KEY_UP)zoom*=1.25f;if(sc==KEY_DOWN)zoom/=1.25f;if(a=='g'||a=='G')mode^=1;}
static u32 grad(int n){if(n>=40)return 0xFF050508u;int v=(n*6)&255;return 0xFF000000u|((u32)v<<16)|((u32)((v*3)%256)<<8)|(u32)(255-v);}
void lab_frame(u32 tick){
 int W=gfx_width(),H=gfx_height();gfx_clear(0xFF050508);
 if(!mode){for(int y=25;y<H-21;y+=2)for(int x=0;x<W;x+=2){float im=cy0+((y-(H/2))*2.0f/H)/zoom;float re=cx0+((x-W/2)*3.0f/W)/zoom;float a=0,b=0;int n=0;for(;n<40&&a*a+b*b<4;n++){float aa=a*a-b*b+re;b=2*a*b+im;a=aa;}u32 c=grad(n);gfx_fill_rect(x,y,2,2,c);}}
 else {for(int i=0;i<100;i++){float a=(tick*0.01f+i*0.37f),r=20+(i*17)%220;int x=W/2+(int)(kosz_cosf(a)*r),y=(H/2)+23+(int)(kosz_sinf(a*1.7f)*r*0.55f);app_circle(x,y,2+(i&3),0xFF40A0E8,1);}}
 app_chrome("Keringo Lab","Nyilak: navigacio/zoom   G: részecske mod",settings_accent_color());
}
