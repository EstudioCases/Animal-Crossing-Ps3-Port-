#include "libforest/gbi_extensions.h"
#include "PR/gbi.h"
#include "evw_anime.h"
#include "c_keyframe.h"
#include "ac_npc.h"
#include "ef_effect_control.h"

#if defined(TARGET_PC) || defined(TARGET_PS3)
u8 testbutton[0x80] ATTRIBUTE_ALIGN(32);
#else
u8 testbutton[] ATTRIBUTE_ALIGN(32) = {
#include "assets/testbutton.inc"
};
#endif

#if defined(TARGET_PC) || defined(TARGET_PS3)
Vtx kai_sousa2_v[0xA00 / sizeof(Vtx)];
#else
Vtx kai_sousa2_v[] = {
#include "assets/kai_sousa2_v.inc"
};
#endif
