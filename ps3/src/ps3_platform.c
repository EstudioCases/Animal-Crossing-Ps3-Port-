#include "ps3_platform.h"

#include <stdio.h>
#include <sys/time.h>

#if defined(TARGET_PS3)
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

int g_ps3_running = 1;
int g_ps3_verbose = 1;
int g_ps3_no_framelimit = 0;
int g_ps3_time_override = -1;
int g_ps3_min_override = -1;
int g_ps3_sec_override = -1;
unsigned int ps3_frame_counter = 0;

void ps3_platform_init(void) {
    ps3_gx_init();
    printf("[PS3] platform init\n");
}

void ps3_platform_shutdown(void) {
    ps3_audio_shutdown();
    ps3_gx_shutdown();
    printf("[PS3] platform shutdown\n");
}

void ps3_platform_swap_buffers(void) {
    ps3_audio_pump();
    ps3_gx_present();
}

int ps3_platform_poll_events(void) {
    return g_ps3_running;
}

void ps3_platform_sleep_ms(unsigned int ms) {
#if defined(TARGET_PS3)
    usleep(ms * 1000);
#elif defined(_WIN32)
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

unsigned long long ps3_platform_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((unsigned long long)tv.tv_sec * 1000000ull) + (unsigned long long)tv.tv_usec;
}

void ps3_platform_wait_for_exit_request(void) {
#if defined(TARGET_PS3)
    sleep(3);
#else
    printf("[PS3] host build: no console wait loop\n");
#endif
}
