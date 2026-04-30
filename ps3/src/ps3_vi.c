/* ps3_vi.c - Dolphin VI replacement with platform frame pacing. */
#include "ps3_platform.h"

#define VI_TVMODE_NTSC_INT    0
#define VI_TVMODE_NTSC_DS     1
#define VI_TVMODE_PAL_INT     4
#define VI_TVMODE_MPAL_INT    8
#define VI_TVMODE_EURGB60_INT 20
#define PS3_FRAME_US          16667ULL

static u32 retrace_count = 0;
static u64 frame_start_us = 0;
static void (*vi_pre_callback)(u32) = NULL;
static void (*vi_post_callback)(u32) = NULL;

void VIInit(void) {}
void VIConfigure(void* rm) { (void)rm; }
void VISetNextFrameBuffer(void* fb) { (void)fb; }
void VISetNextXFB(void* xfb) { (void)xfb; }
void VIFlush(void) {}

void VIWaitForRetrace(void) {
    u64 now;

    if (vi_pre_callback) {
        vi_pre_callback(retrace_count);
    }

    if (!ps3_platform_poll_events()) {
        g_ps3_running = 0;
        return;
    }

    ps3_platform_swap_buffers();

    if (!g_ps3_no_framelimit && frame_start_us != 0) {
        now = ps3_platform_time_us();
        while ((now - frame_start_us) < PS3_FRAME_US) {
            u64 remain = PS3_FRAME_US - (now - frame_start_us);
            if (remain > 2000) {
                ps3_platform_sleep_ms(1);
            }
            now = ps3_platform_time_us();
        }
    }

    frame_start_us = ps3_platform_time_us();

    if (vi_post_callback) {
        vi_post_callback(retrace_count);
    }

    retrace_count++;
    ps3_frame_counter++;
}

u32 VIGetRetraceCount(void) { return retrace_count; }
void VISetBlack(BOOL black) { (void)black; }
u32 VIGetTvFormat(void) { return 0; }
u32 VIGetDTVStatus(void) { return 0; }
u32 VIGetCurrentLine(void) { return 0; }

void* VISetPreRetraceCallback(void* cb) {
    void* old = (void*)vi_pre_callback;
    vi_pre_callback = (void (*)(u32))cb;
    return old;
}

void* VISetPostRetraceCallback(void* cb) {
    void* old = (void*)vi_post_callback;
    vi_post_callback = (void (*)(u32))cb;
    return old;
}
