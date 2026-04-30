/* ps3_stubs.c - stub definitions for symbols the decomp declares but we don't need */
#include <stdarg.h>
#include <stddef.h>  /* size_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps3_platform.h"
#include "ps3_disc.h"
#include "types.h"

typedef s32 OSPriority;

/* data symbols */
unsigned char cKF_Animation_R_getDataTable[4] = {0};
unsigned char cKF_Animation_R_getFixedTable[4] = {0};
unsigned char cKF_Animation_R_getFlagTable[4] = {0};
unsigned char cKF_Animation_R_getKeyTable[4] = {0};

char _e_data = 0;
char _f_rodata = 0;

/* Famicom is not ported on PS3 yet; keep the full game linkable. */
void* my_malloc_current = NULL;
u8 save_game_image = 0;

/* DVD */
BOOL DVDCheckDisk(void) { return 0; }
BOOL DVDOpenDir(char* dirName, void* dir) { (void)dirName; (void)dir; return 0; }
BOOL DVDReadDir(void* dir, void* dirEntry) { (void)dir; (void)dirEntry; return 0; }
BOOL DVDCloseDir(void* dir) { (void)dir; return 0; }

/* GBA link */
void GBAInit(void) {}
s32 GBAGetStatus(s32 chan, u8* status) { (void)chan; (void)status; return -1; }
s32 GBAGetProcessStatus(s32 chan, u8* percentp) { (void)chan; (void)percentp; return -1; }
s32 GBARead(s32 chan, u8* dst, u8* status) { (void)chan; (void)dst; (void)status; return -1; }
s32 GBAWrite(s32 chan, u8* src, u8* status) { (void)chan; (void)src; (void)status; return -1; }
s32 GBAReset(s32 chan, u8* status) { (void)chan; (void)status; return -1; }
s32 GBAJoyBootAsync(s32 chan, s32 palette_color, s32 palette_speed, u8* programp,
                    s32 length, u8* status, void* callback) {
    (void)chan; (void)palette_color; (void)palette_speed; (void)programp;
    (void)length; (void)status; (void)callback; return -1;
}

/* OS threads */
BOOL OSCreateThread(void* thread, void* (*func)(void*), void* param,
                    void* stack, u32 stackSize, OSPriority priority, u16 attr) {
    (void)thread; (void)func; (void)param; (void)stack;
    (void)stackSize; (void)priority; (void)attr; return 0;
}
void OSCancelThread(void* thread) { (void)thread; }
void OSDetachThread(void* thread) { (void)thread; }
s32 OSResumeThread(void* thread) { (void)thread; return 0; }
s32 OSSuspendThread(void* thread) { (void)thread; return 0; }
BOOL OSIsThreadTerminated(void* thread) { (void)thread; return 1; }
BOOL OSJoinThread(void* thread, void** val) { (void)thread; (void)val; return 1; }
void OSExitThread(void* val) { (void)val; }
s32 OSGetThreadPriority(void* thread) { (void)thread; return 0; }
void OSSleepThread(void* queue) { (void)queue; }
void OSInitThreadQueue(void* queue) { (void)queue; }
s32 OSEnableScheduler(void) { return 0; }
void OSYieldThread(void) {}
long OSCheckActiveThreads(void) { return 0; }
void OSFillFPUContext(void* context) { (void)context; }
void OSSetCurrentContext(void* context) { (void)context; }

/* OS misc */
u16 OSGetFontEncode(void) { return 0; }
u32 OSGetProgressiveMode(void) { return 0; }
void OSSetProgressiveMode(u32 on) { (void)on; }
u32 OSGetSoundMode(void) { return 0; }
void OSSetSoundMode(u32 mode) { (void)mode; }
void OSProtectRange(u32 chan, void* addr, u32 nBytes, u32 control) {
    (void)chan; (void)addr; (void)nBytes; (void)control;
}
void* OSSetErrorHandler(u16 error, void* handler) { (void)error; (void)handler; return NULL; }
void OSReportEnable(void) {}
void* __OSSetInterruptHandler(s32 interrupt, void* handler) {
    (void)interrupt;
    return handler;
}
u32 __OSUnmaskInterrupts(u32 interrupts) { return interrupts; }

/* VI */
void VIConfigurePan(u16 x_origin, u16 y_origin, u16 width, u16 height) {
    (void)x_origin; (void)y_origin; (void)width; (void)height;
}

int __abs(int x) { return x < 0 ? -x : x; }
void _strip(float x) { (void)x; }

/* famicom (NES emulator) */
void ksNesEmuFrame(void* wp, void* sp, u32 flags) { (void)wp; (void)sp; (void)flags; }
int famicom_getErrorChan(void) { return -1; }
void famicom_setCallback_getSaveChan(void* proc) { (void)proc; }
int famicom_mount_archive_end_check(void) { return 1; }
void famicom_mount_archive(void) {}
int famicom_init(int rom_idx, void* malloc_info, int player_no) {
    (void)rom_idx;
    (void)malloc_info;
    (void)player_no;
    return -1;
}
int famicom_cleanup(void) { return 0; }
void famicom_1frame(void) {}
int famicom_rom_load_check(void) { return -1; }
int famicom_internal_data_load(void) { return -1; }
int famicom_internal_data_save(void) { return -1; }
int famicom_external_data_save(void) { return -1; }
int famicom_external_data_save_check(void) { return -1; }
int famicom_get_disksystem_titles(int* n_games, char* title_name_bufp, int namebuf_size) {
    (void)title_name_bufp;
    (void)namebuf_size;
    if (n_games) {
        *n_games = 0;
    }
    return 0;
}
static void ps3_bswap_asset_u16(void* data, unsigned int size) {
    u16* p = (u16*)data;
    unsigned int count = size / 2;
    for (unsigned int i = 0; i < count; i++) {
        p[i] = (u16)((p[i] >> 8) | (p[i] << 8));
    }
}

static void ps3_bswap_asset_u32(void* data, unsigned int size) {
    u32* p = (u32*)data;
    unsigned int count = size / 4;
    for (unsigned int i = 0; i < count; i++) {
        p[i] = ((p[i] >> 24) & 0xFF) | ((p[i] >> 8) & 0xFF00) |
               ((p[i] << 8) & 0xFF0000) | ((p[i] << 24) & 0xFF000000);
    }
}

static void ps3_bswap_asset_vtx(void* data, unsigned int size) {
    u8* p = (u8*)data;
    unsigned int count = size / 16;
    for (unsigned int i = 0; i < count; i++) {
        for (int j = 0; j < 12; j += 2) {
            u8 t = p[j];
            p[j] = p[j + 1];
            p[j + 1] = t;
        }
        p += 16;
    }
}

static void ps3_swap_loaded_asset(void* data, unsigned int size, int swap_type) {
    switch (swap_type) {
        case 1: ps3_bswap_asset_u16(data, size); break;
        case 2: ps3_bswap_asset_vtx(data, size); break;
        case 3: ps3_bswap_asset_u32(data, size); break;
        default: break;
    }
}

static u32 ps3_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static int ps3_try_load_asset_path(const char* path, void* dest, unsigned int size) {
    FILE* f;
    if (!path || !dest || !size) {
        return 0;
    }
    f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (fread(dest, 1, size, f) != size) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static int ps3_scan_disc_range_for_font(u32 start, u32 range_size, void* dest, unsigned int size) {
    static const u8 magic[8] = { 'F', 'O', 'N', 'T', 'b', 'f', 'n', '1' };
    const u32 chunk_size = 64 * 1024;
    u8* chunk;
    u32 pos = 0;
    int found = 0;

    if (!dest || size < 0x20 || range_size < 0x20) {
        return 0;
    }

    chunk = (u8*)malloc(chunk_size + sizeof(magic) - 1);
    if (!chunk) {
        return 0;
    }

    while (pos < range_size && !found) {
        u32 read_size = chunk_size + sizeof(magic) - 1;
        if (pos + read_size > range_size) {
            read_size = range_size - pos;
        }
        if (!ps3_disc_read(start + pos, chunk, read_size)) {
            break;
        }

        for (u32 i = 0; i + 12 <= read_size; i++) {
            if (memcmp(chunk + i, magic, sizeof(magic)) == 0) {
                u32 font_size = ps3_be32(chunk + i + 8);
                if (font_size >= size) {
                    if (ps3_disc_read(start + pos + i, dest, size)) {
                        printf("[PS3] Loaded system font at disc offset 0x%X (%u bytes)\n",
                               start + pos + i, size);
                        found = 1;
                    }
                    break;
                }
            }
        }

        if (range_size - pos <= chunk_size) {
            break;
        }
        pos += chunk_size;
    }

    free(chunk);
    return found;
}

static int ps3_try_load_system_font_from_tgc(void* dest, unsigned int size) {
    static const char* tgc_candidates[] = {
        "tgc/forest_Eng_Final_PAL50.tgc",
        "tgc/forest_Frn_Final_PAL50.tgc",
        "tgc/forest_Gmn_Final_PAL50.tgc",
        "tgc/forest_Itl_Final_PAL50.tgc",
        "tgc/forest_Spn_Final_PAL50.tgc",
        "forest_Eng_Final_PAL50.tgc",
        "forest_Frn_Final_PAL50.tgc",
        "forest_Gmn_Final_PAL50.tgc",
        "forest_Itl_Final_PAL50.tgc",
        "forest_Spn_Final_PAL50.tgc",
        NULL
    };

    for (int i = 0; tgc_candidates[i]; i++) {
        u32 off = 0;
        u32 sz = 0;
        if (ps3_disc_find_file(tgc_candidates[i], &off, &sz)) {
            if (ps3_scan_disc_range_for_font(off, sz, dest, size)) {
                return 1;
            }
        }
    }

    return 0;
}

enum { PS3_ASSET_SRC_REL = 0, PS3_ASSET_SRC_DOL = 1, PS3_ASSET_SRC_NONE = 2 };

static u8* ps3_asset_rel_data = NULL;
static u8* ps3_asset_dol_data = NULL;
static int ps3_asset_rom_init_done = 0;

static void ps3_asset_rom_init(void) {
    if (ps3_asset_rom_init_done) {
        return;
    }
    ps3_asset_rom_init_done = 1;

    if (!ps3_disc_is_open()) {
        return;
    }

    ps3_asset_dol_data = ps3_disc_extract_dol();
    ps3_asset_rel_data = ps3_disc_extract_rel();
}

void pc_load_asset(const char* bin_path, void* dest, unsigned int size, unsigned int rom_off, int rom_src, int swap_type) {
    char path[512];
    int loaded = 0;
    int loaded_from_file = 0;

    if (!dest || !size) {
        return;
    }

    if (!bin_path && rom_src == PS3_ASSET_SRC_DOL && size == 0x4160) {
        loaded = ps3_try_load_system_font_from_tgc(dest, size);
    }

    if (!loaded && rom_src != PS3_ASSET_SRC_NONE) {
        u8* rom = NULL;
        ps3_asset_rom_init();
        if (rom_src == PS3_ASSET_SRC_REL) {
            rom = ps3_asset_rel_data;
        } else if (rom_src == PS3_ASSET_SRC_DOL) {
            rom = ps3_asset_dol_data;
        }
        if (rom) {
            memcpy(dest, rom + rom_off, size);
            loaded = 1;
        }
    }

    if (bin_path) {
        if (!loaded) {
            loaded = ps3_try_load_asset_path(bin_path, dest, size);
            loaded_from_file = loaded;
        }
        if (!loaded) {
            snprintf(path, sizeof(path), "%s/%s", PS3_USRDIR, bin_path);
            loaded = ps3_try_load_asset_path(path, dest, size);
            loaded_from_file = loaded;
        }
        if (!loaded) {
            snprintf(path, sizeof(path), "%s/%s", PS3_ROM_DIR, bin_path);
            loaded = ps3_try_load_asset_path(path, dest, size);
            loaded_from_file = loaded;
        }
    }

    if (!loaded) {
        memset(dest, 0, size);
    } else if (loaded_from_file) {
        ps3_swap_loaded_asset(dest, size, swap_type);
    }
}
void PC_DIAG(int limit, const char* fmt, ...) {
    (void)limit;
    (void)fmt;
}

/* famicom.cpp dependencies: CARD functions, nesinfo, misc */
int CARDGetAttributes(int chan, int fileNo, u8* attr) { (void)chan; (void)fileNo; (void)attr; return -1; }
int CARDSetAttributes(int chan, int fileNo, u8 attr) { (void)chan; (void)fileNo; (void)attr; return -1; }
int CARDFastOpen(int chan, int fileNo, void* fileInfo) { (void)chan; (void)fileNo; (void)fileInfo; return -1; }
int bcmp(const void* a, const void* b, size_t n) { return memcmp(a, b, n); }

/* nesinfo ??? now provided by famicom_nesinfo.cpp */

/* libultra */
void osContGetQuery(void* status) { (void)status; }
void osContGetReadData(void* pad) { (void)pad; }
s32 osContInit(void* mq, u8* pattern_p, void* status) { (void)mq; (void)pattern_p; (void)status; return 0; }
s32 osContSetCh(u8 num_controllers) { (void)num_controllers; return 0; }
s32 osContStartQuery(void* mq) { (void)mq; return 0; }
s32 osContStartReadData(void* mq) { (void)mq; return 0; }
void osDestroyThread(void* t) { (void)t; }
s32 osGetThreadId(void* thread) { (void)thread; return 0; }
int osSetTimer(void* t, s64 countdown, s64 interval, void* mq, void* msg) {
    (void)t; (void)countdown; (void)interval; (void)mq; (void)msg; return 0;
}
void osSyncPrintf(const char* fmt, ...) { (void)fmt; }
void osWritebackDCache(void* vaddr, u32 nbytes) { (void)vaddr; (void)nbytes; }

int vaprintf(void* func, const char* fmt, va_list ap) { (void)func; (void)fmt; (void)ap; return 0; }
