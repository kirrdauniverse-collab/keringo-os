#include "kernel.h"

/* Keringo BASIC 0.1 - tiny line BASIC, no libc, no heap. */
#define BL 128
#define LL 96
static char lines[BL][LL];
static int nline, curline, mode, pos;
static char inbuf[LL];
static int inlen;
static int vars[26];

static void out(const char *s){ gfx_draw_text(12,34+curline*10,s,0xFFF0F0F0,0xFF10151A); }
static int num(const char **p){
    int s=1,v=0; while(**p==' ')(*p)++; if(**p=='-'){s=-1;(*p)++;}
    while(**p>='0'&&**p<='9'){v=v*10+(*(*p)++-'0');} return v*s;
}
static int expr(const char *p){
    while(*p==' ')p++;
    int v=(*p>='A'&&*p<='Z')?vars[*p++-'A']:num(&p);
    while(*p){
        while(*p==' ')p++;
        char op=*p++; if(!op)break;
        int b=(*p>='A'&&*p<='Z')?vars[*p++-'A']:num(&p);
        if(op=='+')v+=b; else if(op=='-')v-=b; else if(op=='*')v*=b; else if(op=='/'&&b)v/=b;
    } return v;
}
static int findline(int no){for(int i=0;i<nline;i++){int j=0,v=0;while(lines[i][j]>='0'&&lines[i][j]<='9'){v=v*10+lines[i][j++]-'0';}if(v==no)return i;}return -1;}
static void store(char *s){
    int j=0,no=0; while(s[j]>='0'&&s[j]<='9'){no=no*10+s[j++]-'0';}
    while(s[j]==' ')j++;
    int i=findline(no);
    if(!s[j]){if(i>=0){for(int k=i;k<nline-1;k++)for(int q=0;q<LL;q++)lines[k][q]=lines[k+1][q];nline--;}return;}
    if(i<0){if(nline>=BL)return;i=nline++;}
    for(int q=0;q<LL;q++)lines[i][q]=s[q]; lines[i][LL-1]=0;
    for(int a=i;a>0;a--){int x=0,y=0;while(lines[a][x]>='0'&&lines[a][x]<='9')x++;while(lines[a-1][y]>='0'&&lines[a-1][y]<='9')y++;int na=0,nb=0;for(int z=0;z<x;z++)na=na*10+lines[a][z]-'0';for(int z=0;z<y;z++)nb=nb*10+lines[a-1][z]-'0';if(na>=nb)break;for(int z=0;z<LL;z++){char c=lines[a][z];lines[a][z]=lines[a-1][z];lines[a-1][z]=c;}}
}
static void runline(const char *s,int *pc){
    while(*s>='0'&&*s<='9')s++; while(*s==' ')s++;
    if(!*s)return;
    if(s[0]=='P'&&s[1]=='R'&&s[2]=='I'&&s[3]=='N'&&s[4]=='T'){
        s+=5;while(*s==' ')s++;
        if(*s=='"'){s++;char b[LL];int i=0;while(*s&&*s!='"'&&i<LL-1)b[i++]=*s++;b[i]=0;gfx_draw_text(12,44+(*pc)*10,b,0xFF50E878,0xFF10151A);}
        else {char b[24];ksnprintf(b,sizeof(b),"%d",expr(s));gfx_draw_text(12,44+(*pc)*10,b,0xFF50E878,0xFF10151A);}
        (*pc)++;return;
    }
    if(s[0]=='L'&&s[1]=='E'&&s[2]=='T'){s+=3;while(*s==' ')s++;if(*s>='A'&&*s<='Z'){int v=*s++-'A';while(*s==' '||*s=='=')s++;vars[v]=expr(s);}return;}
    if(s[0]=='G'&&s[1]=='O'&&s[2]=='T'&&s[3]=='O'){int no=expr(s+4);int i=findline(no);if(i>=0)*pc=i-1;return;}
    if(s[0]=='C'&&s[1]=='L'&&s[2]=='S'){gfx_fill_rect(0,23,gfx_width(),gfx_height()-44,0xFF10151A);return;}
}
static void execute(void){
    gfx_fill_rect(0,23,gfx_width(),gfx_height()-44,0xFF10151A);
    int pc=0,outrow=0; for(int guard=0;pc<nline&&guard<10000;guard++,pc++)runline(lines[pc]+0,&pc);
    (void)outrow;
}
void basic_enter(void){nline=0;inlen=0;curline=0;for(int i=0;i<26;i++)vars[i]=0;}
void basic_exit(void){}
void basic_key(u8 sc,u8 rel,u8 a){
    if(rel)return;
    if(sc==KEY_ENTER){
        inbuf[inlen]=0;
        if(inlen){int j=0;while(inbuf[j]==' ')j++;if(inbuf[j]>='0'&&inbuf[j]<='9')store(inbuf);
            else if(inbuf[0]=='R'&&inbuf[1]=='U'&&inbuf[2]=='N')execute();
            else if(inbuf[0]=='L'&&inbuf[1]=='I'&&inbuf[2]=='S'&&inbuf[3]=='T'){
                gfx_fill_rect(0,23,gfx_width(),gfx_height()-44,0xFF10151A);
                for(int i=0;i<nline;i++)gfx_draw_text(12,34+i*10,lines[i],0xFFF0F0F0,0xFF10151A);
            } else if(inbuf[0]=='C'&&inbuf[1]=='L'&&inbuf[2]=='S')gfx_fill_rect(0,23,gfx_width(),gfx_height()-44,0xFF10151A);
            else if(inbuf[0]=='H'&&inbuf[1]=='E'&&inbuf[2]=='L')gfx_draw_text(12,70,"RUN LIST PRINT LET A=1+2 CLS",0xFF8FA0AC,0xFF10151A);
        } inlen=0;return;
    }
    if(sc==0x0E&&inlen){inlen--;inbuf[inlen]=0;return;}
    if(a>=32&&a<127&&inlen<LL-1){inbuf[inlen++]=(char)a;inbuf[inlen]=0;}
}
void basic_frame(u32 t){
    (void)t; app_chrome("Keringo BASIC","Enter: execute   numbered lines: edit   RUN/LIST/HELP",settings_accent_color());
    gfx_draw_text(12,gfx_height()-34,"READY.",0xFF50E878,0xFF10151A);
    gfx_draw_text(70,gfx_height()-34,inbuf,0xFFF0F0F0,0xFF10151A);
    gfx_rect(8,gfx_height()-40,gfx_width()-16,26,0xFF40505A);
}
