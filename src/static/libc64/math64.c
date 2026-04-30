#include "libc64/math64.h"
#if defined(TARGET_PS3)
#include "sys_math.h"
#else
#include "MSL_C/w_math.h"
#endif

f32 fatan2(f32 x, f32 y) {
    return atan2(x, y);
}

f32 fsqrt(f32 x) {
    return sqrtf(x);
}

f32 facos(f32 x) {
    return acos(x);
}
