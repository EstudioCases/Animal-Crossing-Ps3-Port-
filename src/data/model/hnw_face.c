#include "libforest/gbi_extensions.h"
#include "PR/gbi.h"
#include "evw_anime.h"
#include "c_keyframe.h"
#include "ac_npc.h"

#if defined(TARGET_PC) || defined(TARGET_PS3)
u16 hnw_face[0x20 / sizeof(u16)];
#else
u16 hnw_face[] = {
#include "assets/hnw_face.inc"
};
#endif

#if defined(TARGET_PC) || defined(TARGET_PS3)
u8 hnw_tmem_txt[0x800];
#else
u8 hnw_tmem_txt[] = {
#include "assets/hnw_tmem_txt.inc"
};
#endif
