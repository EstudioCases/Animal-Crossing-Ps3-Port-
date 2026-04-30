#include "libforest/gbi_extensions.h"
#include "PR/gbi.h"
#include "evw_anime.h"
#include "c_keyframe.h"
#include "ac_npc.h"
#include "ef_effect_control.h"

#if defined(TARGET_PC) || defined(TARGET_PS3)
u8 needlework_actable[0x60];
#else
u8 needlework_actable[] = {
#include "assets/needlework_actable.inc"
};
#endif
