#ifndef PS3_PLATFORM_H
#define PS3_PLATFORM_H

#include "types.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PS3_GC_WIDTH       640
#define PS3_GC_HEIGHT      480
#define PS3_SCREEN_WIDTH   PS3_GC_WIDTH
#define PS3_SCREEN_HEIGHT  PS3_GC_HEIGHT

#define PS3_TITLE_ID       "ACGC00001"
#define PS3_GAME_DIR       "/dev_hdd0/game/" PS3_TITLE_ID
#define PS3_USRDIR         PS3_GAME_DIR "/USRDIR"
#define PS3_ROM_DIR        PS3_USRDIR "/rom"
#define PS3_SAVE_DIR       PS3_USRDIR "/save"

#define PS3_MAIN_MEMORY_SIZE   (48 * 1024 * 1024)
#define PS3_ARAM_SIZE          (16 * 1024 * 1024)
#define PS3_FIFO_SIZE          (1024 * 1024)

#define PS3_PI  3.14159265358979323846
#define PS3_PIf 3.14159265358979323846f
#define PS3_DEG_TO_RAD  (PS3_PI / 180.0)
#define PS3_DEG_TO_RADf (PS3_PIf / 180.0f)

#define GC_BUS_CLOCK    162000000u
#define GC_CORE_CLOCK   486000000u
#define GC_TIMER_CLOCK  (GC_BUS_CLOCK / 4)

#ifdef __cplusplus
extern "C" {
#endif

extern int g_ps3_running;
extern int g_ps3_verbose;
extern int g_ps3_no_framelimit;
extern int g_ps3_time_override;
extern int g_ps3_min_override;
extern int g_ps3_sec_override;
extern unsigned int ps3_frame_counter;

void ps3_platform_init(void);
void ps3_platform_shutdown(void);
void ps3_platform_swap_buffers(void);
int  ps3_platform_poll_events(void);
void ps3_platform_sleep_ms(unsigned int ms);
unsigned long long ps3_platform_time_us(void);
void ps3_platform_wait_for_exit_request(void);

void ps3_audio_start_producer_thread(void);
void ps3_audio_pump(void);
int  ps3_audio_get_buffer_fill(void);
int  ps3_audio_is_active(void);
void ps3_audio_shutdown(void);

void ps3_gx_init(void);
void ps3_gx_begin_frame(void);
void ps3_gx_present(void);
void ps3_gx_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PS3_PLATFORM_H */
