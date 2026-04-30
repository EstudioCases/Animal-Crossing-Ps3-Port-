/* ps3_save_bswap.h - Bidirectional LE<->BE byte-swap for GCI save data */
#ifndef PS3_SAVE_BSWAP_H
#define PS3_SAVE_BSWAP_H

#include "m_common_data.h"
#include "m_card.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Direction matters for bitfield structs where bswap is not self-inverse */
typedef enum {
    PS3_BSWAP_TO_BE,
    PS3_BSWAP_FROM_BE
} ps3_bswap_dir_t;

void ps3_save_bswap(Save_t* save, ps3_bswap_dir_t dir);

void ps3_save_bswap_keep_mail(mCD_keep_mail_c* mail, ps3_bswap_dir_t dir);
void ps3_save_bswap_keep_original(mCD_keep_original_c* orig, ps3_bswap_dir_t dir);
void ps3_save_bswap_keep_diary(mCD_keep_diary_c* diary, ps3_bswap_dir_t dir);

/* Round-trip verification: swap BE->LE->BE and compare.
 * Returns 0 on match, >0 = mismatch count, -1 = error. */
int ps3_save_bswap_verify_roundtrip(const u8* original_be, u32 size);
int ps3_save_bswap_verify_roundtrip_mail(const u8* original_be, u32 size);
int ps3_save_bswap_verify_roundtrip_original(const u8* original_be, u32 size);
int ps3_save_bswap_verify_roundtrip_diary(const u8* original_be, u32 size);

/* Byte-swap foreigner (passport) data for Card B travel */
void ps3_save_bswap_foreigner(mCD_foreigner_c* f, ps3_bswap_dir_t dir);

/* Compute BE checksum matching mFRm_GetFlatCheckSum on real GC */
u16 ps3_checksum_be(const u8* data, u32 size, u16 old_checksum);

#define ps3_BSWAP_TO_BE PS3_BSWAP_TO_BE
#define ps3_BSWAP_FROM_BE PS3_BSWAP_FROM_BE

#ifdef __cplusplus
}
#endif

#endif /* PS3_SAVE_BSWAP_H */
