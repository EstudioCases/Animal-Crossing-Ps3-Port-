/* ps3_dvd.c - DVD filesystem: reads from disc image (CISO/ISO/GCM) or extracted files */
#include "ps3_platform.h"
#include "ps3_disc.h"
#include "dolphin/dvd.h"

#undef DVDGetFileInfoStatus

static DVDDiskID disk_id = {
    {'G', 'A', 'F', 'E'},
    {'0', '1'},
    0, 0,
    0, 0,
    {0}
};

DVDDiskID* DVDGetCurrentDiskID(void) { return &disk_id; }

#define MAX_DVD_ENTRIES 512

static struct {
    char path[256];
    int  used;
} dvd_entry_table[MAX_DVD_ENTRIES];
static int dvd_entry_count = 0;

/* File-based fallback path (only used when no disc image) */
static char assets_base_path[512] = {0};
static int assets_fallback_inited = 0;

static void dvd_init_fallback_path(void) {
    if (assets_fallback_inited) return;
    assets_fallback_inited = 1;

    const char* candidates[] = {
        PS3_USRDIR "/assets/files",
        PS3_USRDIR "/assets",
        "/dev_usb000/ACGC/assets/files",
        "/dev_usb000/ACGC/assets",
        "/dev_usb001/ACGC/assets/files",
        "/dev_usb001/ACGC/assets",
        "assets/files",
        "assets",
        "../assets/files",
        "../assets",
        "../../assets/files",
        "../../assets",
    };
    for (int i = 0; i < (int)(sizeof(candidates)/sizeof(candidates[0])); i++) {
        char test[768];
        snprintf(test, sizeof(test), "%s/COPYDATE", candidates[i]);
        FILE* f = fopen(test, "rb");
        if (f) {
            fclose(f);
            strncpy(assets_base_path, candidates[i], sizeof(assets_base_path)-1);
            assets_base_path[sizeof(assets_base_path)-1] = '\0';
            return;
        }
    }
    strncpy(assets_base_path, "assets", sizeof(assets_base_path)-1);
    assets_base_path[sizeof(assets_base_path)-1] = '\0';
}

s32 DVDConvertPathToEntrynum(char* path) {
    for (int i = 0; i < dvd_entry_count; i++) {
        if (dvd_entry_table[i].used && strcmp(dvd_entry_table[i].path, path) == 0) {
            return i;
        }
    }

    if (dvd_entry_count >= MAX_DVD_ENTRIES) {
        fprintf(stderr, "[PS3/DVD] Entry table full (%d entries)! Cannot register: %s\n",
                MAX_DVD_ENTRIES, path);
        return -1;
    }

    int idx = dvd_entry_count++;
    strncpy(dvd_entry_table[idx].path, path, sizeof(dvd_entry_table[idx].path) - 1);
    dvd_entry_table[idx].path[sizeof(dvd_entry_table[idx].path) - 1] = '\0';
    dvd_entry_table[idx].used = 1;
    return idx;
}

/* Store FILE* in DVDCommandBlock.addr. For disc-image backed files, FILE* is
 * set to a sentinel and the disc offset is stored in DVDFileInfo.startAddr. */
#define DISC_SENTINEL ((FILE*)(uintptr_t)0xDEADC0DE)

static FILE* dvd_fi_fp(DVDFileInfo* fileInfo) {
    return (FILE*)fileInfo->cb.addr;
}

static void dvd_fi_set_fp(DVDFileInfo* fileInfo, FILE* fp) {
    fileInfo->cb.addr = fp;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
    if (entrynum < 0 || entrynum >= dvd_entry_count || !dvd_entry_table[entrynum].used) {
        return FALSE;
    }

    const char* path = dvd_entry_table[entrynum].path;

    /* Try disc image first */
    if (ps3_disc_is_open()) {
        u32 disc_off, disc_sz;
        if (ps3_disc_find_file(path, &disc_off, &disc_sz)) {
            memset(fileInfo, 0, sizeof(*fileInfo));
            dvd_fi_set_fp(fileInfo, DISC_SENTINEL);
            fileInfo->startAddr = disc_off;
            fileInfo->length = disc_sz;
            return TRUE;
        }
    }

    /* Fall back to extracted files */
    dvd_init_fallback_path();
    {
        char fullpath[768];
        FILE* fp;
        u32 len;

        if (path[0] == '/') {
            snprintf(fullpath, sizeof(fullpath), "%s%s", assets_base_path, path);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", assets_base_path, path);
        }

        fp = fopen(fullpath, "rb");
        if (!fp) {
            return FALSE;
        }

        fseek(fp, 0, SEEK_END);
        len = (u32)ftell(fp);
        fseek(fp, 0, SEEK_SET);

        memset(fileInfo, 0, sizeof(*fileInfo));
        dvd_fi_set_fp(fileInfo, fp);
        fileInfo->startAddr = 0;
        fileInfo->length = len;
    }

    return TRUE;
}

BOOL DVDOpen(char* filename, DVDFileInfo* fileInfo) {
    s32 entry = DVDConvertPathToEntrynum(filename);
    if (entry < 0) return FALSE;
    return DVDFastOpen(entry, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
    FILE* fp = dvd_fi_fp(fileInfo);
    if (fp && fp != DISC_SENTINEL) {
        fclose(fp);
    }
    dvd_fi_set_fp(fileInfo, NULL);
    return TRUE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* buf, s32 length, s32 offset, s32 prio) {
    FILE* fp = dvd_fi_fp(fileInfo);
    (void)prio;

    if (fp == DISC_SENTINEL) {
        /* disc image read */
        u32 base = fileInfo->startAddr;
        if (ps3_disc_read(base + (u32)offset, buf, (u32)length))
            return length;
        return -1;
    }

    if (!fp) {
        return -1;
    }

    fseek(fp, offset, SEEK_SET);
    return (s32)fread(buf, 1, length, fp);
}

s32 DVDRead(DVDFileInfo* fileInfo, void* buf, s32 length, s32 offset) {
    return DVDReadPrio(fileInfo, buf, length, offset, 2);
}

u32 DVDGetLength(DVDFileInfo* fileInfo) {
    return fileInfo->length;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* buf, s32 length, s32 offset,
                      DVDCallback callback, s32 prio) {
    s32 nread = DVDReadPrio(fileInfo, buf, length, offset, prio);
    if (callback) {
        callback(nread, fileInfo);
    }
    return TRUE;
}

void OSDVDFatalError(void) {
    fprintf(stderr, "[PS3/DVD] Fatal DVD error\n");
}

void DVDInit(void) {
    /* disc image init is done in ps3_main.c via ps3_disc_init() */
}

void DVDSetAutoFatalMessaging(BOOL enable) { (void)enable; }

s32 DVDGetFileInfoStatus(DVDFileInfo* fileInfo) {
    (void)fileInfo;
    return 0;
}

s32 DVDGetTransferredSize(DVDFileInfo* fileInfo) {
    (void)fileInfo;
    return 0;
}

BOOL DVDFastClose(DVDFileInfo* fileInfo) {
    return DVDClose(fileInfo);
}

s32 DVDGetDriveStatus(void) { return 0; }
s32 DVDCancel(volatile DVDCommandBlock* block) { (void)block; return 0; }
BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) { (void)block; (void)callback; return TRUE; }
s32 DVDChangeDisk(DVDCommandBlock* block, DVDDiskID* id) { (void)block; (void)id; return 0; }
BOOL DVDChangeDiskAsync(DVDCommandBlock* block, DVDDiskID* id, DVDCBCallback callback) { (void)block; (void)id; (void)callback; return TRUE; }
s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) { (void)block; return 0; }

BOOL DVDPrepareStreamAsync(DVDFileInfo* fi, u32 len, u32 off, DVDCallback cb) {
    (void)fi; (void)len; (void)off; (void)cb;
    return TRUE;
}
s32 DVDCancelStream(DVDCommandBlock* block) { (void)block; return 0; }
