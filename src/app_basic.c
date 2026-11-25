#include "kernel.h"

/* Keringo BASIC 0.1 - tiny line BASIC, no libc, no heap.
 * Karbantarto-javitas (PR#2): teljes ujrarajzolas frame-enkent kimeneti
 * elozmennyel (az asztal elozo kepe nem latszik at), es a bevitel
 * nagybetusitese (klasszikus BASIC: 'print' es 'PRINT' ugyanaz). */
#define BL 128
#define LL 96
#define OL 16
static char lines[BL][LL];
static char outh[OL][LL];
static int nline, curline, nout;
static char inbuf[LL];
static int inlen;
static int vars[26];

static void up(char *s){for(;*s;s++)if(*s>='a'&&*s<='z')*s=(char)(*s-32);}
static void emit(const char *s){if(nout>=OL)return;int i=0;while(s[i]&&i<LL-1){outh[nout][i]=s[i];i++;}outh[nout][i]=0;nout++;}
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
    if(!s[j]){if(i>=0){for(int k=i;k<nline-1;k++)for(int q2=0;q2<LL;q2++)lines[k][q2]=lines[k+1][q2];nline--;}return;}
    if(i<0){if(nline>=BL)return;i=nline++;}
    for(int q2=0;q2<LL;q2++)lines[i][q2]=s[q2]; lines[i][LL-1]=0;
    for(int a=i;a>0;a--){int x=0,y=0;while(lines[a][x]>='0'&&lines[a][x]<='9')x++;while(lines[a-1][y]>='0'&&lines[a-1][y]<='9')y++;int na=0,nb=0;for(int z=0;z<x;z++)na=na*10+lines[a][z]-'0';for(int z=0;z<y;z++)nb=nb*10+lines[a-1][z]-'0';if(na>=nb)break;for(int z=0;z<LL;z++){char c=lines[a][z];lines[a][z]=lines[a-1][z];lines[a-1][z]=c;}}
}
static void runline(const char *s,int *pc){
    while(*s>='0'&&*s<='9')s++; while(*s==' ')s++;
    if(!*s)return;
    if(s[0]=='P'&&s[1]=='R'&&s[2]=='I'&&s[3]=='N'&&s[4]=='T'){
        s+=5;while(*s==' ')s++;
        if(*s=='"'){s++;char b[LL];int i=0;while(*s&&*s!='"'&&i<LL-1)b[i++]=*s++;b[i]=0;emit(b);}
        else {char b[24];ksnprintf(b,sizeof(b),"%d",expr(s));emit(b);}
        return;
    }
    if(s[0]=='L'&&s[1]=='E'&&s[2]=='T'){s+=3;while(*s==' ')s++;if(*s>='A'&&*s<='Z'){int v=*s++-'A';while(*s==' '||*s=='=')s++;vars[v]=expr(s);}return;}
    if(s[0]=='G'&&s[1]=='O'&&s[2]=='T'&&s[3]=='O'){int no=expr(s+4);int i=findline(no);if(i>=0)*pc=i-1;return;}
    if(s[0]=='C'&&s[1]=='L'&&s[2]=='S'){nout=0;return;}
}
static void execute(void){
    nout=0; int pc=0; for(int guard=0;pc<nline&&guard<10000;guard++,pc++)runline(lines[pc]+0,&pc);
}
void basic_enter(void){nline=0;inlen=0;curline=0;nout=0;for(int i=0;i<26;i++)vars[i]=0;}
void basic_exit(void){}
void basic_key(u8 sc,u8 rel,u8 a){
    if(rel)return;
    if(sc==KEY_ENTER){
        inbuf[inlen]=0; up(inbuf);
        if(inlen){int j=0;while(inbuf[j]==' ')j++;if(inbuf[j]>='0'&&inbuf[j]<='9')store(inbuf);
            else if(inbuf[0]=='R'&&inbuf[1]=='U'&&inbuf[2]=='N')execute();
            else if(inbuf[0]=='L'&&inbuf[1]=='I'&&inbuf[2]=='S'&&inbuf[3]=='T'){
                nout=0;
                for(int i=0;i<nline&&nout<OL;i++)emit(lines[i]);
            } else if(inbuf[0]=='C'&&inbuf[1]=='L'&&inbuf[2]=='S')nout=0;
            else if(inbuf[0]=='H'&&inbuf[1]=='E'&&inbuf[2]=='L')emit("RUN LIST PRINT LET A=1+2 CLS");
        } inlen=0;return;
    }
    if(sc==0x0E&&inlen){inlen--;inbuf[inlen]=0;return;}
    if(a>=32&&a<127&&inlen<LL-1){inbuf[inlen++]=(char)a;inbuf[inlen]=0;}
}
void basic_frame(u32 t){
    (void)t;
    int W=gfx_width(),H=gfx_height();
    app_chrome("Keringo BASIC","Enter: execute   numbered lines: edit   RUN/LIST/HELP",settings_accent_color());
    gfx_fill_rect(0,23,W,H-44,0xFF10151A);
    for(int i=0;i<nout&&i<((H-70)/10);i++)gfx_draw_text(12,34+i*10,outh[i],0xFF50E878,0xFF10151A);
    gfx_draw_text(12,H-34,"READY.",0xFF50E878,0xFF10151A);
    gfx_draw_text(70,H-34,inbuf,0xFFF0F0F0,0xFF10151A);
    gfx_rect(8,H-40,W-16,26,0xFF40505A);
}
