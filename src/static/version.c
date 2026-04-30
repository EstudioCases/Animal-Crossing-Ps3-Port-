#include "version.h"

#if defined(TARGET_PC) || defined(TARGET_PS3)
char __Creator__[0xC];
#else
char __Creator__[] = {
#include "assets/__Creator__.inc"
};
#endif

#if defined(TARGET_PC) || defined(TARGET_PS3)
char __DateTime__[0x12];
#else
char __DateTime__[] = {
#include "assets/__DateTime__.inc"
};
#endif


