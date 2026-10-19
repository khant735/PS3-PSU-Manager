#ifndef PS2_SAVE_ARCHIVE_HEADER_H
#define PS2_SAVE_ARCHIVE_HEADER_H
#include <stddef.h>
#include <stdint.h>
#include "ps2_save_format_detect.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    ps2_save_format format;
    char directory[33];
    char icon_sys_name[33];
    uint32_t compressed_bytes;
    uint32_t unpacked_bytes;
    uint32_t entry_count;
    size_t payload_offset;
} ps2_archive_header;
/* Return 0 on valid bounded metadata; -1 invalid argument; -2 invalid/truncated archive.
   No decryption, decompression, extraction or write operations occur. */
int ps2_archive_read_header(const unsigned char *bytes, size_t length, ps2_archive_header *out);
#ifdef __cplusplus
}
#endif
#endif
