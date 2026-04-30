#ifndef _DOLPHIN_GXEXTRA
#define _DOLPHIN_GXEXTRA

// Extra types for native ports
#if defined(TARGET_PC) || defined(TARGET_PS3)
#include <dolphin/gx/GXStruct.h>
#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float r;
  float g;
  float b;
  float a;
} GXColorF32;

typedef enum {
  GX_TF_R8_PC = 0x60,
  GX_TF_RGBA8_PC = 0x61,
} GXPCTexFmt;

void GXDestroyTexObj(GXTexObj* obj);
void GXDestroyTlutObj(GXTlutObj* obj);

void GXColor4f32(float r, float g, float b, float a);

#ifdef __cplusplus
}
#endif

#endif // TARGET_PC || TARGET_PS3

#endif // _DOLPHIN_GXEXTRA
