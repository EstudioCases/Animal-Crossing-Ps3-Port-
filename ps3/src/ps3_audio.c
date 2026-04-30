/* ps3_audio.c - PS3 AI/DSP facade using PSL1GHT libaudio. */
#include "ps3_platform.h"
#include <dolphin/ai.h>

#if defined(TARGET_PS3)
#include <audio/audio.h>
#include <sys/event_queue.h>
#endif

#define PS3_AUDIO_INPUT_RATE_32K 32000u
#define PS3_AUDIO_OUTPUT_RATE    48000u
#define PS3_AUDIO_RING_FRAMES    32768u
#define PS3_AUDIO_RING_MASK      (PS3_AUDIO_RING_FRAMES - 1u)
/* RPCS3 currently stalls in audioPortStart() for this bootstrap path.
 * Keep the JAudio mixer alive and defer real speaker output until boot is
 * verified past the audio init sequence. */
#ifndef PS3_AUDIO_DEVICE_ENABLED
#define PS3_AUDIO_DEVICE_ENABLED 0
#endif

extern void* OSPhysicalToCached(u32 paddr);

static AIDCallback ai_dma_callback = NULL;
static AISCallback ai_stream_callback = NULL;
static u32 ai_dma_addr = 0;
static u32 ai_dma_length = 0;
static u32 ai_stream_trigger = 0;
static u32 ai_stream_sample_count = 0;
static u32 ai_stream_play_state = AI_STREAM_STOP;
static u32 ai_dsp_sample_rate = AI_SAMPLERATE_32KHZ;
static u32 ai_stream_sample_rate = AI_SAMPLERATE_32KHZ;
static u8 ai_stream_vol_left = 0xFF;
static u8 ai_stream_vol_right = 0xFF;
static BOOL ai_initialized = FALSE;
static BOOL ai_dma_enabled = FALSE;

static s16 s_ring[PS3_AUDIO_RING_FRAMES * 2];
static u32 s_ring_write = 0;
static u64 s_ring_read_q16 = 0;
static u64 s_dma_callback_q16 = 0;
static BOOL s_dma_callback_pending = FALSE;
static u32 s_audio_write_block = 0;

#if defined(TARGET_PS3)
static u32 s_port_num = 0;
static audioPortConfig s_port_config;
static sys_event_queue_t s_audio_queue;
static sys_ipc_key_t s_audio_key;
static int s_audio_open = 0;
static int s_audio_disabled_logged = 0;
static int s_audio_dma_skip_logged = 0;
#endif

static const s16* dma_addr_to_ptr(u32 addr) {
    if (addr != 0 && addr < PS3_MAIN_MEMORY_SIZE) {
        return (const s16*)OSPhysicalToCached(addr);
    }
    return (const s16*)(uintptr_t)addr;
}

static u32 audio_available_frames(void) {
    return s_ring_write - (u32)(s_ring_read_q16 >> 16);
}

static float s16_to_float(s16 v) {
    return (float)v / 32768.0f;
}

static void ring_push_s16(const s16* src, u32 frames) {
    if (!src) return;
    for (u32 i = 0; i < frames; i++) {
        u32 idx = (s_ring_write + i) & PS3_AUDIO_RING_MASK;
        s_ring[idx * 2 + 0] = src[i * 2 + 0];
        s_ring[idx * 2 + 1] = src[i * 2 + 1];
    }
    s_ring_write += frames;

    if (audio_available_frames() > PS3_AUDIO_RING_FRAMES - 1024u) {
        s_ring_read_q16 = (u64)(s_ring_write - 1024u) << 16;
    }
}

static void schedule_dma_callback(void) {
    if (ai_dma_callback && ai_dma_length != 0) {
        s_dma_callback_q16 = (u64)s_ring_write << 16;
        s_dma_callback_pending = TRUE;
    }
}

static void maybe_fire_dma_callback(void) {
    if (s_dma_callback_pending && s_ring_read_q16 >= s_dma_callback_q16) {
        AIDCallback callback = ai_dma_callback;
        s_dma_callback_pending = FALSE;
        if (callback) {
            callback();
        }
    }
}

#if defined(TARGET_PS3)
static u64 audio_current_read_block(void) {
    if (s_port_config.readIndex == 0) {
        return 0;
    }
    return *(volatile u64*)((uintptr_t)s_port_config.readIndex);
}

static void audio_fill_block(u32 block) {
    float* data = (float*)(uintptr_t)s_port_config.audioDataStart;
    float* out = data + block * s_port_config.channelCount * AUDIO_BLOCK_SAMPLES;
    u32 step_q16 = (ai_dsp_sample_rate == AI_SAMPLERATE_48KHZ)
        ? 65536u
        : (u32)(((u64)PS3_AUDIO_INPUT_RATE_32K << 16) / PS3_AUDIO_OUTPUT_RATE);

    for (u32 i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        u32 src_frame = (u32)(s_ring_read_q16 >> 16);
        if ((s_ring_write - src_frame) > 0) {
            u32 idx = src_frame & PS3_AUDIO_RING_MASK;
            out[i * 2 + 0] = s16_to_float(s_ring[idx * 2 + 0]);
            out[i * 2 + 1] = s16_to_float(s_ring[idx * 2 + 1]);
            s_ring_read_q16 += step_q16;
        } else {
            out[i * 2 + 0] = 0.0f;
            out[i * 2 + 1] = 0.0f;
        }
    }
    ai_stream_sample_count += AUDIO_BLOCK_SAMPLES;
    maybe_fire_dma_callback();
}

static int ps3_audio_open_port(void) {
#if !PS3_AUDIO_DEVICE_ENABLED
    if (!s_audio_disabled_logged) {
        printf("[PS3/AUDIO] libaudio output disabled; mixer runs without speaker output\n");
        s_audio_disabled_logged = 1;
    }
    return 0;
#else
    audioPortParam params;
    s32 ret;

    if (s_audio_open) return 1;

    printf("[PS3/AUDIO] audioInit begin\n");
    ret = audioInit();
    if (ret != 0) {
        printf("[PS3/AUDIO] audioInit failed: %08x\n", ret);
        return 0;
    }
    printf("[PS3/AUDIO] audioInit done\n");

    memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks = AUDIO_BLOCK_8;
    params.attrib = AUDIO_PORT_INITLEVEL;
    params.level = 1.0f;

    printf("[PS3/AUDIO] audioPortOpen begin\n");
    ret = audioPortOpen(&params, &s_port_num);
    if (ret != 0) {
        printf("[PS3/AUDIO] audioPortOpen failed: %08x\n", ret);
        audioQuit();
        return 0;
    }
    printf("[PS3/AUDIO] audioPortOpen done port=%u\n", s_port_num);

    printf("[PS3/AUDIO] audioGetPortConfig begin\n");
    ret = audioGetPortConfig(s_port_num, &s_port_config);
    if (ret != 0) {
        printf("[PS3/AUDIO] audioGetPortConfig failed: %08x\n", ret);
        audioPortClose(s_port_num);
        audioQuit();
        return 0;
    }
    printf("[PS3/AUDIO] audioGetPortConfig done blocks=%u channels=%u\n",
           s_port_config.numBlocks, s_port_config.channelCount);

    printf("[PS3/AUDIO] notify queue begin\n");
    if (audioCreateNotifyEventQueue(&s_audio_queue, &s_audio_key) == 0) {
        audioSetNotifyEventQueue(s_audio_key);
        sysEventQueueDrain(s_audio_queue);
    } else {
        s_audio_queue = 0;
        s_audio_key = 0;
    }
    printf("[PS3/AUDIO] notify queue done key=%llu\n", (unsigned long long)s_audio_key);

    printf("[PS3/AUDIO] prefill begin\n");
    for (u32 block = 0; block < s_port_config.numBlocks; block++) {
        audio_fill_block(block);
    }
    s_audio_write_block = 0;
    printf("[PS3/AUDIO] prefill done\n");

    printf("[PS3/AUDIO] audioPortStart begin\n");
    ret = audioPortStart(s_port_num);
    if (ret != 0) {
        printf("[PS3/AUDIO] audioPortStart failed: %08x\n", ret);
    }
    printf("[PS3/AUDIO] audioPortStart done ret=%08x\n", ret);

    s_audio_open = 1;
    return 1;
#endif
}
#endif

void ps3_audio_pump(void) {
#if defined(TARGET_PS3)
    if (!s_audio_open) return;
    {
        u64 read_block = audio_current_read_block();
        u32 guard = 0;
        if (s_audio_queue) {
            sysEventQueueDrain(s_audio_queue);
        }
        while (s_audio_write_block != (u32)read_block && guard < s_port_config.numBlocks) {
            audio_fill_block(s_audio_write_block);
            s_audio_write_block = (s_audio_write_block + 1) % (u32)s_port_config.numBlocks;
            guard++;
        }
    }
#endif
}

void ps3_audio_start_producer_thread(void) {
    ps3_audio_pump();
}

void pc_audio_mq_init(void) {}
void pc_audio_start_producer_thread(void) { ps3_audio_start_producer_thread(); }
void pc_audio_mq_shutdown(void) {}

void AIInit(u8* stack) {
    (void)stack;
    ai_initialized = TRUE;
#if defined(TARGET_PS3)
    printf("[PS3/AUDIO] AIInit complete; output open deferred\n");
#endif
}

void AIReset(void) {
    ai_dma_enabled = FALSE;
    ai_dma_addr = 0;
    ai_dma_length = 0;
    ai_stream_sample_count = 0;
    s_ring_write = 0;
    s_ring_read_q16 = 0;
    s_dma_callback_pending = FALSE;
}

void AIInitDMA(u32 start_addr, u32 length) {
    const s16* src;
    u32 frames;

    ai_dma_addr = start_addr;
    ai_dma_length = length;
#if defined(TARGET_PS3) && !PS3_AUDIO_DEVICE_ENABLED
    if (!s_audio_dma_skip_logged) {
        printf("[PS3/AUDIO] AIInitDMA skipped; output disabled addr=%08x length=%u\n",
               start_addr, length);
        s_audio_dma_skip_logged = 1;
    }
    return;
#endif
    src = dma_addr_to_ptr(start_addr);
    frames = (length / sizeof(s16)) / 2u;
    ring_push_s16(src, frames);
    schedule_dma_callback();
    ps3_audio_pump();
}

void AIStartDMA(void) {
    ai_dma_enabled = TRUE;
    schedule_dma_callback();
#if defined(TARGET_PS3)
    printf("[PS3/AUDIO] AIStartDMA begin\n");
    ps3_audio_open_port();
#endif
    ps3_audio_pump();
#if defined(TARGET_PS3)
    printf("[PS3/AUDIO] AIStartDMA done\n");
#endif
}

void AIStopDMA(void) { ai_dma_enabled = FALSE; }
BOOL AIGetDMAEnableFlag(void) { return ai_dma_enabled; }
u32  AIGetDMABytesLeft(void) {
    if (!ai_dma_enabled || !s_dma_callback_pending) return 0;
    {
        u64 cur_frame = s_ring_read_q16 >> 16;
        u64 end_frame = s_dma_callback_q16 >> 16;
        u64 remaining_frames = (end_frame > cur_frame) ? (end_frame - cur_frame) : 0;
        u64 remaining_bytes = remaining_frames * sizeof(s16) * 2u;
        return remaining_bytes > ai_dma_length ? ai_dma_length : (u32)remaining_bytes;
    }
}
u32  AIGetDMAStartAddr(void) { return ai_dma_addr; }
u32  AIGetDMALength(void) { return ai_dma_length; }
BOOL AICheckInit(void) { return ai_initialized; }

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    AIDCallback old = ai_dma_callback;
    ai_dma_callback = callback;
    schedule_dma_callback();
    return old;
}

AISCallback AIRegisterStreamCallback(AISCallback callback) {
    AISCallback old = ai_stream_callback;
    ai_stream_callback = callback;
    return old;
}

void AISetStreamTrigger(u32 trigger) { ai_stream_trigger = trigger; }
u32  AIGetStreamTrigger(void) { return ai_stream_trigger; }
u32  AIGetStreamSampleCount(void) { return ai_stream_sample_count; }
void AIResetStreamSampleCount(void) { ai_stream_sample_count = 0; }
void AISetStreamPlayState(u32 state) { ai_stream_play_state = state; }
u32  AIGetStreamPlayState(void) { return ai_stream_play_state; }
void AISetStreamSampleRate(u32 rate) { ai_stream_sample_rate = rate; }
u32  AIGetStreamSampleRate(void) { return ai_stream_sample_rate; }
void AISetStreamVolLeft(u8 vol) { ai_stream_vol_left = vol; }
void AISetStreamVolRight(u8 vol) { ai_stream_vol_right = vol; }
u8   AIGetStreamVolLeft(void) { return ai_stream_vol_left; }
u8   AIGetStreamVolRight(void) { return ai_stream_vol_right; }
void AISetDSPSampleRate(u32 rate) { ai_dsp_sample_rate = rate; }
u32  AIGetDSPSampleRate(void) { return ai_dsp_sample_rate; }

void DSPInit(void) {}
BOOL DSPCheckMailToDSP(void) { return FALSE; }
BOOL DSPCheckMailFromDSP(void) { return FALSE; }
u32  DSPReadMailFromDSP(void) { return 0; }
void DSPSendMailToDSP(u32 mail) { (void)mail; }
void DSPAssertInt(void) {}
void* DSPAddTask(void* task) { return task; }

int ps3_audio_get_buffer_fill(void) { return (int)audio_available_frames(); }
int ps3_audio_is_active(void) {
#if defined(TARGET_PS3)
    return s_audio_open;
#else
    return ai_initialized;
#endif
}

void ps3_audio_shutdown(void) {
    ai_dma_enabled = FALSE;
#if defined(TARGET_PS3)
    if (s_audio_open) {
        audioPortStop(s_port_num);
        if (s_audio_key) audioRemoveNotifyEventQueue(s_audio_key);
        audioPortClose(s_port_num);
        if (s_audio_queue) sysEventQueueDestroy(s_audio_queue, 0);
        audioQuit();
        s_audio_open = 0;
    }
#endif
}
