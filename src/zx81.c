/* zx81.c — ZX81 gépburok: memóriatérkép, billentyűmátrix, display-file renderelés.
 * 0.5: ROM automatikus betöltése a lemezről (/cd|/usb|/sys: adatok/ZX81.ROM),
 * valódi D_FILE renderelés, 64 karakteres ZX81 karakterkészlet (saját rajzú),
 * nyolcsoros billentyűmátrix (a valódi ROM ellenkezétéig jellegzetes kiosztás). */
#include "kernel.h"
#include "zx81.h"
#define ZX_ROM_SIZE 8192
static u8 zx_rom[ZX_ROM_SIZE];static int zx_rom_valid;
static u8 mr(u16 a,void*ctx){zx81_t*z=ctx;if(a<ZX_ROM_SIZE&&zx_rom_valid)return zx_rom[a];if(a>=0x4000&&a<0x8000)return z->mem[a-0x4000];return 0xFF;}
static void mw(u16 a,u8 v,void*ctx){zx81_t*z=ctx;if(a>=0x4000&&a<0x8000)z->mem[a-0x4000]=v;}

/* billentyű-mátrix: kijelölt sor (0 bit = sor aktív) + soronként 5 gomb */
static u8 kbd_latch=0xFF;
static u8 ir(u16 p,void*ctx){
    zx81_t*z=ctx;(void)p;
    u8 v=0xFF;
    for(int r=0;r<8;r++) if(!(kbd_latch&(1u<<r))) v&=(u8)~z->key[r];
    return v;
}
static void iw(u16 p,u8 v,void*ctx){(void)p;(void)ctx;kbd_latch=v;}

void zx81_init(zx81_t*z){for(u32 i=0;i<sizeof(*z);i++)((u8*)z)[i]=0;z80_init(&z->cpu);z->cpu.mem_read=mr;z->cpu.mem_write=mw;z->cpu.io_read=ir;z->cpu.io_write=iw;z->cpu.ctx=z;z->running=1;kbd_latch=0xFF;zx81_reset(z);}
void zx81_reset(zx81_t*z){for(int i=0;i<8;i++)z->key[i]=0;for(int i=0;i<16384;i++)z->mem[i]=0;z->cpu.a=z->cpu.f=z->cpu.b=z->cpu.c=z->cpu.d=z->cpu.e=z->cpu.h=z->cpu.l=0;z->cpu.pc=0;z->cpu.sp=0x43FF;z->cpu.ix=z->cpu.iy=0;z->cpu.iff1=z->cpu.iff2=z->cpu.halted=0;z->cpu.tstates=0;z->frames=0;}
int zx81_rom_valid(void){return zx_rom_valid;}
void zx81_rom_unload(void){zx_rom_valid=0;}
int zx81_load(zx81_t*z,const u8*b,u32 n){(void)z;if(!b||!n)return 0;if(n>ZX_ROM_SIZE)n=ZX_ROM_SIZE;for(u32 i=0;i<n;i++)zx_rom[i]=b[i];zx_rom_valid=(n==ZX_ROM_SIZE);zx81_reset(z);return zx_rom_valid;}
/* 0.5: ROM keresése a tárolókról — /cd, /usb, /sys sorrendben */
int zx81_load_rom_from_storage(zx81_t*z){
    static u8 rombuf[ZX_ROM_SIZE];
    u32 got=0;
    const char*paths[]={"/cd/adatok/ZX81.ROM","/usb/ZX81.ROM","/sys/ZX81.ROM","/cd/ZX81.ROM"};
    for(int i=0;i<4;i++){
        int out=0;
        if(vfs_read_file((char*)paths[i],(char*)rombuf,ZX_ROM_SIZE,&out)&&out==ZX_ROM_SIZE){
            got=(u32)out;
            if(zx81_load(z,rombuf,got)){kprintf("zx81: ROM betoltve (%s)\n",paths[i]);return 1;}
        }
    }
    return 0;
}

void zx81_frame(zx81_t*z){if(!z->running)return;if(!zx_rom_valid){z->frames++;return;}u32 end=(u32)z->cpu.tstates+65000;while(z->cpu.tstates<end)z80_step(&z->cpu);z->frames++;}

/* ---------- ZX81 karakterkészlet (saját 8x8 bitkép, 64 karakter + inverz) ----------
 * 0x00-0x0F: 2x2 kvadráns-blokkok (bit0=bal-felső, 1=jobb-felső, 2=bal-alsó, 3=jobb-alsó)
 * 0x10-0x1B: írásjelek, 0x1C-0x25: számjegyek, 0x26-0x3F: A-Z */
static void zx81_glyph(int px,int py,u8 code,u32 fg,u32 bg){
    u8 c=code&0x7F;
    int inv=(code&0x80)!=0;
    /* alaphátér (inverz videónál a cella előtér-színű) */
    for(int y=0;y<8;y++)for(int x=0;x<8;x++)gfx_pixel(px+x,py+y,inv?fg:bg);
    u8 g[8];
    for(int y=0;y<8;y++)g[y]=0;
    if(c<16){                        /* kvadráns-blokkok */
        for(int y=0;y<8;y++)for(int x=0;x<8;x++){
            int q=(y<4?(x<4?1:2):(x<4?4:8));
            if(c&q)g[y]|=(u8)(0x80>>x);
        }
    } else if(c>=0x26&&c<=0x3F){     /* A-Z a 8x8 alapfontból */
        char s[2]={ (char)('A'+(c-0x26)),0 };
        gfx_draw_text(px,py,s,inv?bg:fg,inv?fg:bg);
        return;
    } else if(c>=0x1C&&c<=0x25){     /* 0-9 */
        char s[2]={ (char)('0'+(c-0x1C)),0 };
        gfx_draw_text(px,py,s,inv?bg:fg,inv?fg:bg);
        return;
    } else {                          /* írásjelek: 0x10-0x1B hozzávetőleges kiosztás */
        static const char punct[12] = { ' ', '.', ',', '"', '$', ':', ';', '?', '(', ')', '+', '-' };
        if(c>=0x10&&c<0x1C){
            char ch=punct[c-0x10];
            if(ch&&ch!=' '){ char s[2]={ch,0}; gfx_draw_text(px,py,s,inv?bg:fg,inv?fg:bg); return; }
        }
    }
    /* blokk-glyphok kirajzolása */
    for(int y=0;y<8;y++){u8 row=g[y];for(int x=0;x<8;x++)if(row&(0x80>>x))gfx_pixel(px+x,py+y,inv?bg:fg);}
}

/* ---------- valódi display-file renderelés ----------
 * A D_FILE rendszerváltozó a 0x400C-nél (RAM-ban: 0x0C) áll; 32 karakter soronként,
 * 0x76 (HALT) a sor vége, összesen 16 sor, az utolsó után 0x76 zár. */
static void render_dfile(zx81_t*z,int ox,int oy,int sc){
    u16 dfile=(u16)(z->mem[0x0C]|((u16)z->mem[0x0D]<<8));
    if(dfile<0x4000||dfile>=0x8000)dfile=0x407D;
    u16 a=dfile;
    u32 fg=0xFFE7F3FF,bg=0xFF081018;
    int cx=0,cy=0;
    for(int guard=0;guard<2048;guard++){
        u8 ch8=z->mem[a-0x4000];
        if(ch8==0x76){cx=0;cy++;a++;if(cy>=16)break;continue;}
        if(cx<32)zx81_glyph(ox+cx*8*sc,oy+cy*8*sc,ch8,fg,bg);
        cx++;a++;
        if(a<0x4000||a>=0x8000)break;
    }
}

void zx81_draw(zx81_t*z){
    int W=gfx_width(),H=gfx_height();
    gfx_fill_rect(0,0,W,H,0xFF05080C);
    int sc=1;                                  /* nagyítás a felbontáshoz */
    int gw=256*sc,gh=192*sc;
    while(gw*2<=W-16&&gh*2<=H-40){sc++;gw=256*sc;gh=192*sc;}
    int ox=(W-gw)/2,oy=24+(H-24-20-gh)/2;
    gfx_fill_rect(ox-8,oy-8,gw+16,gh+16,0xFF18212A);
    gfx_rect(ox-1,oy-1,gw+2,gh+2,0xFF6E8B9A);
    if(zx_rom_valid){
        render_dfile(z,ox,oy,sc);
        gfx_draw_text(10,10,"ZX81 (Z80, 3.25 MHz) — valódi ROM-mal",0xFFE7F3FF,0xFF000000u);
        gfx_draw_text(10,H-18,"Esc = kilépés",0xFF8FA0AC,0xFF000000u);
    } else {
        /* demó-útvonal: nincs ROM — tájékoztató kép */
        const char*lines[] = {
            "ZX81 RETRO — ROM nélküli demó",
            "",
            "Az emulátor (Z80 mag + gépburok) kész,",
            "de a valódi 8 KB-os ZX81 ROM hiányzik.",
            "",
            "Tegyél a lemezre / pendrajvra egy",
            "adatok/ZX81.ROM fájlt (8192 bájt),",
            "és automatikusan betöltődik!",
            "",
            "Addig demó-jelek futnak a képernyőn."
        };
        int n = 10, y = oy + 8;
        for (int i = 0; i < n; i++, y += 8 * 2)
            gfx_draw_text(ox + 8, y, lines[i], 0xFFE7F3FF, 0xFF000000u);
        /* minta-matrix a demó kedvéért */
        for (int yy = 0; yy < 24; yy++) for (int xx = 0; xx < 32; xx++) {
            u8 c = (u8)(32 + ((xx * 7 + yy * 13 + (int)(z->frames >> 3)) & 31));
            zx81_glyph(ox + 1 + xx * 8, oy + 1 + yy * 8, c, 0xFF9AE8A0, 0xFF081018);
        }
        gfx_draw_text(10, 10, "ZX81 RETRO — demó mód (ROM nélkül)", 0xFFE7F3FF, 0xFF000000u);
        gfx_draw_text(10, H - 18, "Nincs ROM betöltve | Esc = kilépés", 0xFF8FA0AC, 0xFF000000u);
    }
}

void zx81_key(zx81_t*z,u8 sc,u8 rel,u8 a){
    (void)sc;(void)rel;(void)a;
    /* a 0.4-hez képest: a mátrixkezelés a ROM-érkezésig nem használatos;
       a gépelés nyil-karakterként a demóban jelenik meg */
    if(rel)return;
    if(a)z->mem[0x01]=a;
}
