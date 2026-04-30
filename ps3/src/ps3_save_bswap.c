#include "ps3_save_bswap.h"

void ps3_save_bswap(Save_t* save, ps3_bswap_dir_t dir) {
    (void)save;
    (void)dir;
}

void ps3_save_bswap_keep_mail(mCD_keep_mail_c* mail, ps3_bswap_dir_t dir) {
    (void)mail;
    (void)dir;
}

void ps3_save_bswap_keep_original(mCD_keep_original_c* orig, ps3_bswap_dir_t dir) {
    (void)orig;
    (void)dir;
}

void ps3_save_bswap_keep_diary(mCD_keep_diary_c* diary, ps3_bswap_dir_t dir) {
    (void)diary;
    (void)dir;
}

int ps3_save_bswap_verify_roundtrip(const u8* original_be, u32 size) {
    (void)original_be;
    (void)size;
    return 0;
}

int ps3_save_bswap_verify_roundtrip_mail(const u8* original_be, u32 size) {
    return ps3_save_bswap_verify_roundtrip(original_be, size);
}

int ps3_save_bswap_verify_roundtrip_original(const u8* original_be, u32 size) {
    return ps3_save_bswap_verify_roundtrip(original_be, size);
}

int ps3_save_bswap_verify_roundtrip_diary(const u8* original_be, u32 size) {
    return ps3_save_bswap_verify_roundtrip(original_be, size);
}

void ps3_save_bswap_foreigner(mCD_foreigner_c* f, ps3_bswap_dir_t dir) {
    (void)f;
    (void)dir;
}

u16 ps3_checksum_be(const u8* data, u32 size, u16 old_checksum) {
    u32 sum = old_checksum;
    for (u32 i = 0; i < size; i += 2) {
        u16 word = (u16)data[i] << 8;
        if (i + 1 < size) {
            word |= data[i + 1];
        }
        sum += word;
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (u16)(sum & 0xFFFFu);
}

