#include "libforest/gbi_extensions.h"
#include "PR/gbi.h"
#include "evw_anime.h"
#include "c_keyframe.h"


#if defined(TARGET_PC) || defined(TARGET_PS3)
Vtx obj_stoneD_shadow_v[0x80 / sizeof(Vtx)];
#else
Vtx obj_stoneD_shadow_v[] = { 
#include "assets/obj_stoneD_shadow_v.inc"
};
#endif

Gfx obj_stoneD_shadowT_gfx_model[] = { 
gsSPNTrianglesInit_5b(6, 0, 1, 2, 0, 2, 3, 4, 5, 6),
gsSPNTriangles_5b(4, 6, 7, 3, 2, 5, 3, 5, 4, 0, 0, 0),
gsSPEndDisplayList()
};

