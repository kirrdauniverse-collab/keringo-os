#include "kernel.h"
#include "zx81.h"
static zx81_t zx;
void zx81_app_enter(void){zx81_init(&zx);zx81_load_rom_from_storage(&zx);}
void zx81_app_frame(u32 t){(void)t;zx81_frame(&zx);zx81_draw(&zx);}
void zx81_app_key(u8 sc,u8 rel,u8 a){if(sc==KEY_ESC&&!rel){zx.running=0;return;}zx81_key(&zx,sc,rel,a);}
void zx81_app_exit(void){zx.running=0;}
