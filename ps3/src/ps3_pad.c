/* ps3_pad.c - GameCube PAD facade for PS3 controllers. */
#include "ps3_platform.h"
#include <dolphin/pad.h>

#if defined(TARGET_PS3)
#include <io/pad.h>
#endif

static u32 g_pad_spec = PAD_SPEC_5;
static PADSamplingCallback g_sampling_callback = NULL;

#if defined(TARGET_PS3)
static padData g_cached_pad[MAX_PORT_NUM];
static u8 g_press_mode_enabled[MAX_PORT_NUM];
static u8 g_pad_initialized = 0;

static void reset_cached_pad(u32 port) {
    memset(&g_cached_pad[port], 0, sizeof(g_cached_pad[port]));
    g_cached_pad[port].ANA_L_H = 128;
    g_cached_pad[port].ANA_L_V = 128;
    g_cached_pad[port].ANA_R_H = 128;
    g_cached_pad[port].ANA_R_V = 128;
}
#endif

static s8 axis_to_gc(u32 v, int invert) {
    int out = (int)v - 128;
    if (invert) out = -out;
    if (out > 127) out = 127;
    if (out < -128) out = -128;
    return (s8)out;
}

static void neutral_status(PADStatus* status) {
    memset(status, 0, sizeof(PADStatus) * PAD_MAX_CONTROLLERS);
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        status[i].err = PAD_ERR_NO_CONTROLLER;
    }
}

BOOL PADInit(void) {
#if defined(TARGET_PS3)
    if (g_pad_initialized) return TRUE;

    for (u32 port = 0; port < MAX_PORT_NUM; port++) {
        reset_cached_pad(port);
        g_press_mode_enabled[port] = 0;
    }
    {
        s32 ret = ioPadInit(MAX_PORT_NUM);
        if (ret != PAD_OK && ret != (s32)PAD_ERROR_ALREADY_INITIALIZED) {
            return FALSE;
        }
    }
    g_pad_initialized = 1;
#endif
    return TRUE;
}

u32 PADRead(PADStatus* status) {
    u32 connected_mask = 0;
    neutral_status(status);

#if defined(TARGET_PS3)
    {
        padInfo2 info;
        if (!g_pad_initialized && !PADInit()) {
            if (g_sampling_callback) {
                g_sampling_callback();
            }
            return connected_mask;
        }

        memset(&info, 0, sizeof(info));
        if (ioPadGetInfo2(&info) == 0) {
            for (u32 port = 0; port < MAX_PORT_NUM && port < PAD_MAX_CONTROLLERS; port++) {
                padData data;
                const padData* src;
                u16 buttons = 0;

                if ((info.port_status[port] & 1u) == 0) {
                    reset_cached_pad(port);
                    g_press_mode_enabled[port] = 0;
                    continue;
                }
                if (!g_press_mode_enabled[port]) {
                    ioPadSetPressMode(port, PAD_PRESS_MODE_ON);
                    ioPadSetPortSetting(port, PAD_SETTINGS_PRESS_ON);
                    g_press_mode_enabled[port] = 1;
                }

                memset(&data, 0, sizeof(data));
                if (ioPadGetData(port, &data) == 0 && data.len > 0) {
                    g_cached_pad[port] = data;
                }
                src = &g_cached_pad[port];

                if (src->BTN_CROSS) buttons |= PAD_BUTTON_A;
                if (src->BTN_CIRCLE) buttons |= PAD_BUTTON_B;
                if (src->BTN_SQUARE) buttons |= PAD_BUTTON_X;
                if (src->BTN_TRIANGLE) buttons |= PAD_BUTTON_Y;
                if (src->BTN_START || src->BTN_SELECT) buttons |= PAD_BUTTON_START;
                if (src->BTN_LEFT) buttons |= PAD_BUTTON_LEFT;
                if (src->BTN_RIGHT) buttons |= PAD_BUTTON_RIGHT;
                if (src->BTN_DOWN) buttons |= PAD_BUTTON_DOWN;
                if (src->BTN_UP) buttons |= PAD_BUTTON_UP;
                if (src->BTN_L1 || src->BTN_L2) buttons |= PAD_TRIGGER_L;
                if (src->BTN_R1) buttons |= PAD_TRIGGER_R;
                if (src->BTN_R2) buttons |= PAD_TRIGGER_Z;

                status[port].button = buttons;
                status[port].stickX = axis_to_gc(src->ANA_L_H, 0);
                status[port].stickY = axis_to_gc(src->ANA_L_V, 1);
                status[port].substickX = axis_to_gc(src->ANA_R_H, 0);
                status[port].substickY = axis_to_gc(src->ANA_R_V, 1);
                status[port].triggerLeft = src->PRE_L2 ? (u8)src->PRE_L2 : (src->BTN_L2 ? 0xFF : 0);
                status[port].triggerRight = src->PRE_R2 ? (u8)src->PRE_R2 : (src->BTN_R2 ? 0xFF : 0);
                status[port].analogA = src->PRE_CROSS ? (u8)src->PRE_CROSS : (src->BTN_CROSS ? 0xFF : 0);
                status[port].analogB = src->PRE_CIRCLE ? (u8)src->PRE_CIRCLE : (src->BTN_CIRCLE ? 0xFF : 0);
                status[port].err = PAD_ERR_NONE;
                connected_mask |= (PAD_CHAN0_BIT >> port);
            }
        }
    }
#else
    status[0].err = PAD_ERR_NONE;
    connected_mask = PAD_CHAN0_BIT;
#endif

    if (g_sampling_callback) {
        g_sampling_callback();
    }
    return connected_mask;
}

void PADControlMotor(s32 chan, u32 command) {
#if defined(TARGET_PS3)
    if (g_pad_initialized && chan >= 0 && chan < MAX_PORT_NUM) {
        padActParam act;
        memset(&act, 0, sizeof(act));
        if (command == PAD_MOTOR_RUMBLE) {
            act.large_motor = 0xFF;
        }
        ioPadSetActDirect((u32)chan, &act);
    }
#else
    (void)chan;
    (void)command;
#endif
}

void PADControlAllMotors(const u32* commandArray) {
    if (!commandArray) return;
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        PADControlMotor(i, commandArray[i]);
    }
}

BOOL PADReset(u32 mask) { (void)mask; return TRUE; }
BOOL PADRecalibrate(u32 mask) { (void)mask; return TRUE; }
BOOL PADSync(void) { return TRUE; }
void PADSetSamplingRate(u32 msec) { (void)msec; }
void __PADTestSamplingRate(u32 tvmode) { (void)tvmode; }
void PADSetSpec(u32 spec) { g_pad_spec = spec; }
u32 PADGetSpec(void) { return g_pad_spec; }
void PADSetAnalogMode(u32 mode) { (void)mode; }
BOOL PADGetType(s32 chan, u32* type) {
    (void)chan;
    if (type) *type = 0x09000000;
    return TRUE;
}

PADSamplingCallback PADSetSamplingCallback(PADSamplingCallback callback) {
    PADSamplingCallback old = g_sampling_callback;
    g_sampling_callback = callback;
    return old;
}
