#if defined(TARGET_PC) || defined(TARGET_PS3)
unsigned short hem_1_pal[0x20 / sizeof(unsigned short)] __attribute__((aligned(32)));
#else
extern unsigned short hem_1_pal[] __attribute__((aligned(32))) = {
#include "assets/npc/tex/hem_1_pal.inc"
};
#endif

#if defined(TARGET_PC) || defined(TARGET_PS3)
unsigned char hem_1_tmem_txt[0x740] __attribute__((aligned(32)));
#else
extern unsigned char hem_1_tmem_txt[] __attribute__((aligned(32))) = {
#include "assets/npc/tex/hem_1_tmem_txt.inc"
};
#endif

