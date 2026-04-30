/* ps3_disc.h - GameCube disc image reader (CISO/ISO/GCM) */
#ifndef PS3_DISC_H
#define PS3_DISC_H

#include "types.h"

/* Initialize disc image reader. Searches for .ciso/.iso/.gcm in
 * current dir, orig/, rom/. Parses GCM filesystem into lookup table.
 * Returns 1 on success, 0 if no disc image found. */
int ps3_disc_init(void);

/* Check if a disc image is open. */
int ps3_disc_is_open(void);

/* Find a file in the disc FST by path (e.g., "COPYDATE", "audiores/banks/bank0.aw").
 * Leading '/' is stripped automatically. Returns 1 if found. */
int ps3_disc_find_file(const char* path, u32* disc_offset, u32* file_size);

/* Read bytes from a logical disc offset. Returns 1 on success. */
int ps3_disc_read(u32 offset, void* dest, u32 size);

/* Extract DOL and REL as malloc'd buffers (for ps3_assets.c). */
u8* ps3_disc_extract_dol(void);
u8* ps3_disc_extract_rel(void); /* handles Yaz0 decompression */

/* Close disc image and free resources. */
void ps3_disc_shutdown(void);

#endif /* PS3_DISC_H */
