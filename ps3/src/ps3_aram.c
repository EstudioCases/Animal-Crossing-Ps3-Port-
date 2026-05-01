/* ps3_aram.c - GC's 16MB auxiliary RAM, replaced with a malloc'd buffer */
#include "ps3_platform.h"

static u8* aram_base = NULL;
static u32 aram_alloc_ptr = 0;

extern u8* ps3_arena_base;
extern u8* ps3_arena_end;
extern void* OSPhysicalToCached(u32 paddr);

static void* aram_mram_ptr(u32 addr, u32 length, const char* caller) {
    static int warned = 0;
    if (addr != 0 && addr < PS3_MAIN_MEMORY_SIZE && length <= PS3_MAIN_MEMORY_SIZE - addr) {
        return OSPhysicalToCached(addr);
    }
    if (ps3_arena_base && ps3_arena_end) {
        u8* p = (u8*)(uintptr_t)addr;
        if (p >= ps3_arena_base && length <= (u32)(ps3_arena_end - p)) {
            return p;
        }
    }
    if (addr >= 0x10000) {
        return (void*)(uintptr_t)addr;
    }
    if (!warned) {
        fprintf(stderr, "[PS3/ARAM] %s invalid MRAM addr=%08x len=%u\n", caller, addr, length);
        warned = 1;
    }
    return NULL;
}

u32 ARInit(u32* stack_idx_addr, u32 length) {
    (void)stack_idx_addr; (void)length;
    if (!aram_base) {
        aram_base = (u8*)malloc(PS3_ARAM_SIZE);
        if (aram_base) {
            memset(aram_base, 0, PS3_ARAM_SIZE);
        }
        aram_alloc_ptr = 0;
    }
    return 0; /* offset-based, base is always 0 */
}

u8* ps3_aram_get_base(void) { return aram_base; }

u32 ARGetBaseAddress(void) { return 0; }
u32 ARGetSize(void) { return PS3_ARAM_SIZE; }

u32 ARAlloc(u32 size) {
    u32 aligned_size = (size + 31) & ~31; /* 32-byte align */
    if (aram_alloc_ptr + aligned_size > PS3_ARAM_SIZE) {
        fprintf(stderr, "[PS3/ARAM] Out of ARAM! Requested %lu, used %lu/%u\n",
                size, aram_alloc_ptr, PS3_ARAM_SIZE);
        return 0;
    }
    u32 addr = aram_alloc_ptr;
    aram_alloc_ptr += aligned_size;
    return addr;
}

void ARFree(u32* addr) {
    (void)addr; /* bump allocator, no-op */
}

/* type 0 = MRAM???ARAM, type 1 = ARAM???MRAM. params are always (type, mram, aram). */
void ARStartDMA(u32 type, u32 mram_addr, u32 aram_addr, u32 length) {
    void* mram_ptr;
    if (!aram_base) return;

    /* some code passes (aram_base + offset) instead of just the offset */
    u32 base = (u32)(uintptr_t)aram_base;
    if (aram_addr >= base && aram_addr < base + PS3_ARAM_SIZE) {
        aram_addr -= base;
    }

    if (length > PS3_ARAM_SIZE || aram_addr > PS3_ARAM_SIZE - length) {
        /* OOB read: zero-fill dest so caller doesn't get garbage (cap 1MB) */
        mram_ptr = aram_mram_ptr(mram_addr, length, "ARStartDMA/OOB");
        if (type == 1 && mram_ptr != NULL && length > 0 && length <= 0x100000) {
            memset(mram_ptr, 0, length);
        }
        return;
    }

    mram_ptr = aram_mram_ptr(mram_addr, length, "ARStartDMA");
    if (!mram_ptr) {
        return;
    }

    if (type == 0) {
        memcpy(aram_base + aram_addr, mram_ptr, length);
    } else {
        memcpy(mram_ptr, aram_base + aram_addr, length);
    }
}

u32 ARGetInternalSize(void) { return PS3_ARAM_SIZE; }
BOOL ARCheckInit(void) { return aram_base != NULL; }

/* ARQ - synchronous wrapper around ARStartDMA.
 * ARQPostRequest's source/dest order differs from ARStartDMA's, so we remap. */
void ARQInit(void) {}
void ARQPostRequest(void* req, u32 owner, u32 type, u32 prio,
                    u32 source, u32 dest, u32 length, void* callback) {
    if (type == 0) {
        ARStartDMA(type, source, dest, length); /* source=mram, dest=aram */
    } else {
        ARStartDMA(type, dest, source, length); /* source=aram, dest=mram ??? swapped */
    }
    if (callback) ((void (*)(u32))callback)((u32)(uintptr_t)req);
}

void ARQFlushQueue(void) {}
