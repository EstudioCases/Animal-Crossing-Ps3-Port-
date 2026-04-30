#include "libforest/gbi_extensions.h"
#include "PR/gbi.h"
#include "evw_anime.h"
#include "c_keyframe.h"
#include "ac_npc.h"
#include "ef_effect_control.h"

#if defined(TARGET_PC) || defined(TARGET_PS3)
u16 inv_mwin_kaseki2_pal[0x20 / sizeof(u16)] ATTRIBUTE_ALIGN(32);
#else
u16 inv_mwin_kaseki2_pal[] ATTRIBUTE_ALIGN(32) = {
#include "assets/inv_mwin_kaseki2_pal.inc"
};
#endif

#if defined(TARGET_PC) || defined(TARGET_PS3)
u16 inv_mwin_kaseki_pal[0x20 / sizeof(u16)];
#else
u16 inv_mwin_kaseki_pal[] = {
#include "assets/inv_mwin_kaseki_pal.inc"
};
#endif

#if defined(TARGET_PC) || defined(TARGET_PS3)
u8 inv_mwin_kaseki2_tex[0x200];
#else
u8 inv_mwin_kaseki2_tex[] = {
#include "assets/inv_mwin_kaseki2_tex.inc"
};
#endif

#if defined(TARGET_PC) || defined(TARGET_PS3)
u8 inv_mwin_kaseki_tex[0x200];
#else
u8 inv_mwin_kaseki_tex[] = {
#include "assets/inv_mwin_kaseki_tex.inc"
};
#endif
